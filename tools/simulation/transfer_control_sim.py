"""
Linear transfer axis control for the TI CSE AOI system.

Manages six independent linear transfer axes (Transfer #1 through #6), each
consisting of a linear actuator with a vacuum gripper. The transfers
coordinate hand-offs between inspection stations:

  - Transfer #1: Pitch changer -> Lighting check (CCD4)
  - Transfer #2: Lighting check -> Bottom check (CCD3) -- triggers CCD3
                  capture *during* motion for bottom-side imaging.
  - Transfer #3: Bottom check -> Top check (CCD1)
  - Transfer #4: Top check -> Orientation compensation (servo rotation) ->
                  Side check (CCD2). Performs in-transit servo rotation
                  for orientation compensation.
  - Transfer #5: Side check -> Unload station (good parts)
  - Transfer #6: NG check -> NG sorting station

Author: Rongxuan Zhou
"""

from __future__ import annotations

import logging
import time
from dataclasses import dataclass, field
from enum import Enum, auto
from typing import Callable, Dict, List, Optional

from src.data_types.defect_types import CameraID
from src.global_variables.system_config import TRANSFER_POSITIONS, TransferPosition

logger = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# Types
# ---------------------------------------------------------------------------

class TransferState(Enum):
    """Operational state of a single transfer axis."""
    HOME = auto()
    MOVING_FORWARD = auto()
    AT_TARGET = auto()
    MOVING_HOME = auto()
    VACUUM_GRIP = auto()
    ERROR = auto()


class AxisID(Enum):
    """Logical identifier for each transfer axis."""
    TRANSFER_1 = 1
    TRANSFER_2 = 2
    TRANSFER_3 = 3
    TRANSFER_4 = 4
    TRANSFER_5 = 5
    TRANSFER_6 = 6


@dataclass
class AxisStatus:
    """Runtime status of a transfer axis."""
    axis_id: AxisID
    state: TransferState = TransferState.HOME
    position_mm: float = 0.0
    vacuum_on: bool = False
    has_part: bool = False
    servo_angle_deg: float = 0.0   # Only used by Transfer #4


# ---------------------------------------------------------------------------
# Single-axis controller
# ---------------------------------------------------------------------------

class _LinearAxis:
    """Low-level controller for one linear transfer axis with vacuum gripper."""

    def __init__(self, axis_id: AxisID, spec: TransferPosition) -> None:
        self._id = axis_id
        self._spec = spec
        self._status = AxisStatus(axis_id=axis_id)
        self._trigger_callback: Optional[Callable[[], None]] = None

    # -- motion --------------------------------------------------------------

    def move_to_target(self) -> bool:
        """Move from home to the target position."""
        if self._status.state not in (TransferState.HOME, TransferState.AT_TARGET):
            logger.error("Axis %s cannot move: state=%s.", self._id.name, self._status.state.name)
            return False
        self._status.state = TransferState.MOVING_FORWARD
        travel = abs(self._spec.target_mm - self._status.position_mm)
        move_time = travel / self._spec.velocity_mm_s if self._spec.velocity_mm_s > 0 else 0
        logger.info("Axis %s moving to target (%.1f mm, ~%.3f s).",
                     self._id.name, self._spec.target_mm, move_time)
        # Simulate motion completion
        self._status.position_mm = self._spec.target_mm
        self._status.state = TransferState.AT_TARGET

        # Fire trigger callback if registered (e.g. CCD capture during motion)
        if self._trigger_callback is not None:
            self._trigger_callback()

        return True

    def move_to_home(self) -> bool:
        """Return the axis to the home position."""
        self._status.state = TransferState.MOVING_HOME
        self._status.position_mm = self._spec.home_mm
        self._status.state = TransferState.HOME
        logger.info("Axis %s returned to home.", self._id.name)
        return True

    # -- vacuum --------------------------------------------------------------

    def vacuum_grip(self) -> None:
        self._status.vacuum_on = True
        self._status.has_part = True
        self._status.state = TransferState.VACUUM_GRIP
        logger.debug("Axis %s vacuum grip ON.", self._id.name)

    def vacuum_release(self) -> None:
        self._status.vacuum_on = False
        self._status.has_part = False
        logger.debug("Axis %s vacuum released.", self._id.name)

    # -- servo rotation (Transfer #4 only) -----------------------------------

    def set_servo_angle(self, angle_deg: float) -> None:
        """Rotate the in-transit servo to the given angle (Transfer #4)."""
        self._status.servo_angle_deg = angle_deg
        logger.info("Axis %s servo rotated to %.1f deg.", self._id.name, angle_deg)

    # -- trigger callback ----------------------------------------------------

    def register_trigger(self, callback: Callable[[], None]) -> None:
        """Register a callback to fire during forward motion (e.g. CCD trigger)."""
        self._trigger_callback = callback

    # -- accessors -----------------------------------------------------------

    @property
    def status(self) -> AxisStatus:
        return self._status

    @property
    def spec(self) -> TransferPosition:
        return self._spec


# ---------------------------------------------------------------------------
# Multi-axis coordinator
# ---------------------------------------------------------------------------

class TransferControl:
    """
    Coordinates all six linear transfer axes and their vacuum grippers.

    Provides high-level hand-off sequences that move a CSE between stations,
    trigger camera captures mid-motion where required, and apply orientation
    compensation rotations.
    """

    def __init__(self) -> None:
        self._axes: Dict[AxisID, _LinearAxis] = {}
        for axis_id in AxisID:
            spec = TRANSFER_POSITIONS[axis_id.value]
            self._axes[axis_id] = _LinearAxis(axis_id, spec)

    # -- registration --------------------------------------------------------

    def register_camera_trigger(self, axis_id: AxisID, callback: Callable[[], None]) -> None:
        """
        Register a camera-trigger callback on a specific transfer axis.

        The callback fires once during the forward motion, enabling CCD
        capture while the part is in transit (e.g. Transfer #2 -> CCD3).
        """
        axis = self._axes.get(axis_id)
        if axis is not None:
            axis.register_trigger(callback)

    # -- high-level hand-off sequences ---------------------------------------

    def execute_handoff(self, axis_id: AxisID) -> bool:
        """
        Execute a complete pick-transfer-place cycle on the given axis.

        Sequence: vacuum grip -> move to target -> release -> return home.
        """
        axis = self._axes.get(axis_id)
        if axis is None:
            return False

        axis.vacuum_grip()
        if not axis.move_to_target():
            return False
        axis.vacuum_release()
        axis.move_to_home()
        return True

    def transfer_with_bottom_capture(
        self,
        capture_fn: Callable[[], None],
    ) -> bool:
        """
        Transfer #2: move part from lighting check to bottom check while
        triggering CCD3 capture during transit.

        The capture callback is fired automatically when the axis reaches
        the mid-travel position.
        """
        axis = self._axes[AxisID.TRANSFER_2]
        axis.register_trigger(capture_fn)
        return self.execute_handoff(AxisID.TRANSFER_2)

    def transfer_with_orientation_comp(
        self,
        compensation_angle_deg: float,
    ) -> bool:
        """
        Transfer #4: move part from top check toward side check while
        performing servo-based orientation compensation.

        The servo rotates to ``compensation_angle_deg`` during transit so
        the CSE arrives at CCD2 in the correct angular reference.
        """
        axis = self._axes[AxisID.TRANSFER_4]
        axis.vacuum_grip()
        axis.set_servo_angle(compensation_angle_deg)
        if not axis.move_to_target():
            return False
        axis.vacuum_release()
        axis.move_to_home()
        return True

    def transfer_to_ng_sort(self) -> bool:
        """Transfer #6: move NG-confirmed part to the NG sorting station."""
        return self.execute_handoff(AxisID.TRANSFER_6)

    # -- batch / parallel operations -----------------------------------------

    def home_all(self) -> None:
        """Return every axis to its home position."""
        for axis in self._axes.values():
            axis.vacuum_release()
            axis.move_to_home()
        logger.info("All transfer axes homed.")

    def get_axis_status(self, axis_id: AxisID) -> AxisStatus:
        return self._axes[axis_id].status

    def all_axes_idle(self) -> bool:
        """Check whether every axis is at home and not holding a part."""
        return all(
            ax.status.state == TransferState.HOME and not ax.status.has_part
            for ax in self._axes.values()
        )
