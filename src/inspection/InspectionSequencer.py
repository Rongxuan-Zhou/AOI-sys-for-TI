"""
Master inspection state machine for the TI CSE AOI system.

Orchestrates the full 18-step process flow from IDLE through all inspection
stations to final output or NG sorting. Tracks per-unit results from each
CCD and implements the NG double-check protocol: if any CCD flags NG, the
unit is routed to the NG Check CCD for reconfirmation before sorting.

Process flow:
  IDLE -> LOADING -> ORIENTATION_PRE_CHECK -> PITCH_CHANGE ->
  TRANSFER_TO_INSPECT_1 -> TRANSFER_TO_INSPECT_2 -> LIGHTING_CHECK ->
  BOTTOM_CHECK -> TOP_CHECK -> ORIENTATION_COMP -> SIDE_CHECK ->
  TRANSFER_TO_UNLOAD_1 -> TRANSFER_TO_UNLOAD_2 ->
  [NG_DOUBLE_CHECK -> NG_SORTING] | [UNLOADING -> GOOD_OUTPUT]

The sequencer processes 4 CSE units per cycle (matching the SCARA robot
pick count) and pipelines multiple batches to sustain >85 000 units/day.

Author: Rongxuan Zhou
"""

from __future__ import annotations

import logging
import time
from dataclasses import dataclass, field
from enum import Enum, auto
from typing import Callable, Dict, List, Optional

import numpy as np

from src.data_types.defect_types import (
    CameraID,
    DefectDetail,
    InspectionResult,
    ProcessStep,
)
from src.global_variables.system_config import (
    STEP_TIME_BUDGET,
    TARGET_CYCLE_TIME_SEC,
    UNITS_PER_PICK_CYCLE,
)
from src.material_handling.PitchChanger import HolderMode, PitchChanger
from src.material_handling.RobotController import NozzleID, RobotController, RobotPose
from src.material_handling.TransferControl import AxisID, TransferControl
from src.ng_management.NGSorter import NGDisposition, NGSorter
from src.vision.CameraController import CameraController
from src.vision.DefectClassifier import DefectClassifier
from src.vision.LightingCheckAnalyzer import LightingCheckAnalyzer
from src.vision.OrientationDetector import OrientationDetector

logger = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# Transition table type
# ---------------------------------------------------------------------------

# Maps current step -> (action callable, next step on success, next step on failure)
_TransitionEntry = tuple  # (Callable, ProcessStep, ProcessStep)


# ---------------------------------------------------------------------------
# Sequencer
# ---------------------------------------------------------------------------

@dataclass
class CycleMetrics:
    """Timing and count metrics for a single 4-unit cycle."""
    cycle_id: int = 0
    step_times: Dict[ProcessStep, float] = field(default_factory=dict)
    units_passed: int = 0
    units_ng: int = 0
    total_time_sec: float = 0.0


class InspectionSequencer:
    """
    Master state machine that drives the AOI inspection process.

    Owns instances of every subsystem controller (cameras, transfers, robot,
    pitch changer, vision analysers, NG sorter) and sequences their
    operations according to the process flow.
    """

    def __init__(self) -> None:
        # -- subsystems --
        self._cameras = CameraController()
        self._transfers = TransferControl()
        self._robot = RobotController(simulate=True)
        self._pitch_changer = PitchChanger()
        self._defect_classifier = DefectClassifier()
        self._lighting_analyzer = LightingCheckAnalyzer()
        self._orientation_detector = OrientationDetector()
        self._ng_sorter = NGSorter()

        # -- state --
        self._current_step = ProcessStep.IDLE
        self._cycle_count: int = 0
        self._unit_results: List[InspectionResult] = []
        self._current_batch: List[InspectionResult] = []
        self._is_running: bool = False

        # Register CCD3 bottom-check trigger on Transfer #2
        self._transfers.register_camera_trigger(
            AxisID.TRANSFER_2,
            self._on_transfer2_trigger,
        )

    # -- lifecycle -----------------------------------------------------------

    def initialise(self) -> None:
        """Power-on initialisation of all subsystems."""
        self._cameras.initialise_all()
        self._robot.connect()
        self._robot.home()
        self._transfers.home_all()
        self._current_step = ProcessStep.IDLE
        self._is_running = True
        logger.info("Inspection sequencer initialised. Target cycle time: %.2f s.",
                     TARGET_CYCLE_TIME_SEC)

    def shutdown(self) -> None:
        """Orderly shutdown."""
        self._is_running = False
        self._cameras.shutdown_all()
        self._robot.disconnect()
        self._transfers.home_all()
        self._current_step = ProcessStep.IDLE
        logger.info("Inspection sequencer shut down.")

    # -- main cycle ----------------------------------------------------------

    def run_cycle(self) -> CycleMetrics:
        """
        Execute one complete 4-unit inspection cycle through all steps.

        Returns metrics for the cycle including per-step timing.
        """
        metrics = CycleMetrics(cycle_id=self._cycle_count)
        self._current_batch = [InspectionResult() for _ in range(UNITS_PER_PICK_CYCLE)]
        cycle_start = time.monotonic()

        step_sequence: List[tuple] = [
            (ProcessStep.LOADING, self._step_loading),
            (ProcessStep.ORIENTATION_PRE_CHECK, self._step_orientation_pre_check),
            (ProcessStep.PITCH_CHANGE, self._step_pitch_change),
            (ProcessStep.TRANSFER_TO_INSPECT_1, self._step_transfer_to_inspect_1),
            (ProcessStep.TRANSFER_TO_INSPECT_2, self._step_transfer_to_inspect_2),
            (ProcessStep.LIGHTING_CHECK, self._step_lighting_check),
            (ProcessStep.BOTTOM_CHECK, self._step_bottom_check),
            (ProcessStep.TOP_CHECK, self._step_top_check),
            (ProcessStep.ORIENTATION_COMP, self._step_orientation_comp),
            (ProcessStep.SIDE_CHECK, self._step_side_check),
            (ProcessStep.TRANSFER_TO_UNLOAD_1, self._step_transfer_to_unload_1),
            (ProcessStep.TRANSFER_TO_UNLOAD_2, self._step_transfer_to_unload_2),
        ]

        for step, action in step_sequence:
            self._current_step = step
            t0 = time.monotonic()
            try:
                action()
            except Exception as exc:
                logger.error("Error in step %s: %s", step.name, exc)
                self._current_step = ProcessStep.ERROR
                break
            metrics.step_times[step] = time.monotonic() - t0

        # -- NG routing / good output --
        for result in self._current_batch:
            if not result.is_pass:
                self._current_step = ProcessStep.NG_DOUBLE_CHECK
                self._step_ng_double_check(result)
                metrics.units_ng += 1
            else:
                self._current_step = ProcessStep.GOOD_OUTPUT
                metrics.units_passed += 1

        metrics.total_time_sec = time.monotonic() - cycle_start
        self._cycle_count += 1
        self._unit_results.extend(self._current_batch)
        self._current_step = ProcessStep.IDLE

        logger.info("Cycle %d complete: %d pass, %d NG, %.3f s.",
                     metrics.cycle_id, metrics.units_passed, metrics.units_ng,
                     metrics.total_time_sec)
        return metrics

    # -- step implementations ------------------------------------------------

    def _step_loading(self) -> None:
        """LOADING: SCARA robot picks 4 CSE from basket."""
        basket_poses = [
            RobotPose(x=10.0 * i, y=0.0, z=-5.0, r=0.0)
            for i in range(UNITS_PER_PICK_CYCLE)
        ]
        self._robot.pick_from_basket(basket_poses, NozzleID.NOZZLE_1_BOTTOM)

    def _step_orientation_pre_check(self) -> None:
        """ORIENTATION_PRE_CHECK: Check CSE orientation before pitch change."""
        frame = self._cameras.capture(CameraID.CCD1_TOP)
        if frame is not None:
            orient_result = self._orientation_detector.detect_pre_load(frame)
            if orient_result.orientation.name == "FLIPPED_180":
                self._robot.rotate_for_orientation(180.0)
                for r in self._current_batch:
                    r.orientation_angle = 180.0

    def _step_pitch_change(self) -> None:
        """PITCH_CHANGE: Expand spacing on purple platform."""
        platform_poses = [
            RobotPose(x=12.0 * i, y=50.0, z=0.0, r=0.0)
            for i in range(UNITS_PER_PICK_CYCLE)
        ]
        self._robot.place_on_pitch_changer(platform_poses, NozzleID.NOZZLE_1_BOTTOM)
        self._pitch_changer.receive_units(UNITS_PER_PICK_CYCLE)
        self._pitch_changer.expand()

    def _step_transfer_to_inspect_1(self) -> None:
        """TRANSFER_TO_INSPECT_1: Transfer from pitch changer to lighting check."""
        self._transfers.execute_handoff(AxisID.TRANSFER_1)

    def _step_transfer_to_inspect_2(self) -> None:
        """TRANSFER_TO_INSPECT_2: Transfer from lighting check to bottom check (CCD3 fires mid-transit)."""
        self._transfers.transfer_with_bottom_capture(self._on_transfer2_trigger)

    def _step_lighting_check(self) -> None:
        """LIGHTING_CHECK: CCD4 closed-chamber inspection."""
        for i, result in enumerate(self._current_batch):
            frame = self._cameras.capture(CameraID.CCD4_INNER)
            if frame is None:
                continue
            lc_result = self._lighting_analyzer.analyse(frame)
            result.ccd_results[CameraID.CCD4_INNER] = lc_result.passed
            if not lc_result.passed:
                for d in lc_result.defect_details:
                    result.add_defect(d)

    def _step_bottom_check(self) -> None:
        """BOTTOM_CHECK: CCD3 bottom-side defect classification."""
        for result in self._current_batch:
            frame = self._cameras.capture(CameraID.CCD3_BOTTOM)
            if frame is not None:
                self._defect_classifier.classify_bottom(frame, result)

    def _step_top_check(self) -> None:
        """TOP_CHECK: CCD1 top-side defect classification."""
        self._transfers.execute_handoff(AxisID.TRANSFER_3)
        for result in self._current_batch:
            frame = self._cameras.capture(CameraID.CCD1_TOP)
            if frame is not None:
                self._defect_classifier.classify_top(frame, result)

    def _step_orientation_comp(self) -> None:
        """ORIENTATION_COMP: Compute and apply rotation before side check."""
        frame = self._cameras.capture(CameraID.CCD1_TOP)
        comp_angle = 0.0
        if frame is not None:
            comp_angle = self._orientation_detector.compute_compensation_angle(frame)
        self._transfers.transfer_with_orientation_comp(comp_angle)

    def _step_side_check(self) -> None:
        """SIDE_CHECK: CCD2 360-degree side inspection."""
        for result in self._current_batch:
            frames = self._cameras.capture_side_rotation(n_frames=8)
            if frames:
                self._defect_classifier.classify_side(frames, result)

    def _step_transfer_to_unload_1(self) -> None:
        """TRANSFER_TO_UNLOAD_1: Move toward unload station."""
        self._transfers.execute_handoff(AxisID.TRANSFER_5)

    def _step_transfer_to_unload_2(self) -> None:
        """TRANSFER_TO_UNLOAD_2: Final positioning at output."""
        # In the physical machine this is a second short transfer; here it
        # is a logical placeholder for the handoff completion.
        pass

    # -- NG double-check -----------------------------------------------------

    def _step_ng_double_check(self, result: InspectionResult) -> None:
        """
        NG double-check: re-inspect with NG Check CCD and confirm/overturn.

        If confirmed NG, the unit is sorted to the NG tray via Transfer #6.
        If overturned, the unit is returned to the good-output path.
        """
        record = self._ng_sorter.flag_ng(result)

        # Re-capture with NG Check CCD
        ng_frame = self._cameras.capture(CameraID.CCD1_TOP)  # Reuse CCD1 as NG check proxy
        recheck_conf = 0.0
        if ng_frame is not None and result.defects:
            # Re-run the primary defect detector
            primary = result.defects[0]
            recheck_result = InspectionResult()
            if primary.camera_id == CameraID.CCD1_TOP:
                self._defect_classifier.classify_top(ng_frame, recheck_result)
            elif primary.camera_id == CameraID.CCD3_BOTTOM:
                self._defect_classifier.classify_bottom(ng_frame, recheck_result)
            else:
                frames = self._cameras.capture_side_rotation(n_frames=4)
                self._defect_classifier.classify_side(frames, recheck_result)

            if recheck_result.defects:
                recheck_conf = max(d.confidence for d in recheck_result.defects)

        disposition = self._ng_sorter.perform_double_check(record, recheck_conf)

        if disposition == NGDisposition.CONFIRMED_NG:
            self._current_step = ProcessStep.NG_SORTING
            self._transfers.transfer_to_ng_sort()
            result.ng_confirmed = True
        else:
            result.is_pass = True   # Overturned -- return to good output
            result.ng_confirmed = False

        result.ng_double_checked = True

    # -- trigger callbacks ---------------------------------------------------

    def _on_transfer2_trigger(self) -> None:
        """Callback fired by Transfer #2 during motion to capture CCD3 bottom image."""
        logger.debug("Transfer #2 trigger: capturing CCD3 bottom frame.")
        # Actual frame retrieval is handled in _step_bottom_check;
        # this callback signals the hardware trigger line to fire CCD3.

    # -- accessors -----------------------------------------------------------

    @property
    def current_step(self) -> ProcessStep:
        return self._current_step

    @property
    def cycle_count(self) -> int:
        return self._cycle_count

    @property
    def ng_sorter(self) -> NGSorter:
        return self._ng_sorter

    def get_all_results(self) -> List[InspectionResult]:
        return list(self._unit_results)

    def get_yield_rate(self) -> float:
        """Compute the cumulative yield (pass rate) across all processed units."""
        if not self._unit_results:
            return 1.0
        passed = sum(1 for r in self._unit_results if r.is_pass)
        return passed / len(self._unit_results)
