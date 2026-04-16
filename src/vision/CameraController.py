"""
Multi-CCD camera controller for the TI CSE AOI system.

Manages four Hikrobot industrial cameras:
  - CCD1, CCD2, CCD3: MV-GE501GC (5 MP GigE)
  - CCD4:             MV-GE2000C (20 MP GigE)

Provides a unified interface for acquisition triggering, exposure control,
ROI configuration, and frame retrieval. The controller wraps a simulated
Hikrobot MVS SDK (MvCamera class) and supports hardware-trigger mode for
synchronised capture with transfer-axis motion signals.

Author: Rongxuan Zhou
"""

from __future__ import annotations

import logging
import time
from dataclasses import dataclass, field
from enum import Enum, auto
from typing import Dict, List, Optional, Tuple

import numpy as np

from src.data_types.defect_types import CameraID
from src.global_variables.system_config import CAMERA_SPECS, CameraSpec

logger = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# Simulated Hikrobot SDK primitives
# ---------------------------------------------------------------------------

class TriggerMode(Enum):
    """Camera trigger modes mirroring MVS SDK constants."""
    SOFTWARE = auto()
    HARDWARE_LINE0 = auto()
    HARDWARE_LINE1 = auto()


class PixelFormat(Enum):
    """Supported pixel formats."""
    MONO8 = "Mono8"
    BAYER_RG8 = "BayerRG8"
    RGB8 = "RGB8Packed"


@dataclass
class AcquisitionParams:
    """Per-camera acquisition configuration."""
    exposure_us: float = 5000.0
    gain_db: float = 0.0
    trigger_mode: TriggerMode = TriggerMode.HARDWARE_LINE0
    pixel_format: PixelFormat = PixelFormat.MONO8
    roi: Optional[Tuple[int, int, int, int]] = None   # (offset_x, offset_y, width, height)
    frame_rate_hz: float = 30.0


class MvCamera:
    """
    Simulated Hikrobot MvCamera SDK wrapper.

    In production this class delegates to ``MvCameraControl_class.MvCamera``
    from the MVS SDK. Here the interface is preserved so that the integration
    layer stays identical when linking against real hardware.
    """

    def __init__(self, camera_id: CameraID, spec: CameraSpec) -> None:
        self._camera_id = camera_id
        self._spec = spec
        self._is_open: bool = False
        self._is_grabbing: bool = False
        self._params = AcquisitionParams()
        self._frame_counter: int = 0

    # -- lifecycle -----------------------------------------------------------

    def open_device(self) -> bool:
        """Open connection to the camera device."""
        if self._is_open:
            logger.warning("Camera %s already open.", self._camera_id.name)
            return True
        self._is_open = True
        logger.info("Opened camera %s (%s).", self._camera_id.name, self._spec.model)
        return True

    def close_device(self) -> None:
        """Release camera resources."""
        if self._is_grabbing:
            self.stop_grabbing()
        self._is_open = False
        logger.info("Closed camera %s.", self._camera_id.name)

    # -- acquisition ---------------------------------------------------------

    def start_grabbing(self) -> bool:
        """Begin continuous or triggered acquisition."""
        if not self._is_open:
            logger.error("Cannot grab: camera %s not open.", self._camera_id.name)
            return False
        self._is_grabbing = True
        logger.info("Camera %s grabbing started (trigger=%s).",
                     self._camera_id.name, self._params.trigger_mode.name)
        return True

    def stop_grabbing(self) -> None:
        """Stop acquisition."""
        self._is_grabbing = False
        logger.info("Camera %s grabbing stopped.", self._camera_id.name)

    def send_software_trigger(self) -> bool:
        """Issue a software trigger command (only valid in SOFTWARE trigger mode)."""
        if self._params.trigger_mode != TriggerMode.SOFTWARE:
            logger.error("Software trigger sent but camera is in %s mode.",
                         self._params.trigger_mode.name)
            return False
        self._frame_counter += 1
        return True

    def get_one_frame(self, timeout_ms: int = 1000) -> Optional[np.ndarray]:
        """
        Retrieve the latest frame buffer.

        Returns a synthesised numpy array matching the configured resolution
        and ROI. In production this calls ``MV_CC_GetOneFrameTimeout``.
        """
        if not self._is_grabbing:
            return None
        w, h = self._spec.resolution_px
        if self._params.roi is not None:
            _, _, w, h = self._params.roi
        self._frame_counter += 1
        # Synthesise a dummy frame with mild noise (for offline testing)
        rng = np.random.default_rng(seed=self._frame_counter)
        frame = rng.integers(40, 200, size=(h, w), dtype=np.uint8)
        return frame

    # -- parameter setters ---------------------------------------------------

    def set_exposure(self, exposure_us: float) -> None:
        self._params.exposure_us = exposure_us
        logger.debug("Camera %s exposure set to %.1f us.", self._camera_id.name, exposure_us)

    def set_gain(self, gain_db: float) -> None:
        self._params.gain_db = gain_db

    def set_trigger_mode(self, mode: TriggerMode) -> None:
        self._params.trigger_mode = mode

    def set_roi(self, offset_x: int, offset_y: int, width: int, height: int) -> None:
        """Configure the hardware ROI (must be set before grabbing starts)."""
        max_w, max_h = self._spec.resolution_px
        if offset_x + width > max_w or offset_y + height > max_h:
            raise ValueError(f"ROI ({offset_x},{offset_y},{width},{height}) exceeds "
                             f"sensor bounds ({max_w}x{max_h}).")
        self._params.roi = (offset_x, offset_y, width, height)

    def clear_roi(self) -> None:
        self._params.roi = None

    @property
    def params(self) -> AcquisitionParams:
        return self._params

    @property
    def is_open(self) -> bool:
        return self._is_open


# ---------------------------------------------------------------------------
# Multi-camera controller
# ---------------------------------------------------------------------------

class CameraController:
    """
    Manages the full set of CCD cameras on the AOI machine.

    Typical lifecycle::

        ctrl = CameraController()
        ctrl.initialise_all()
        frame = ctrl.capture(CameraID.CCD1_TOP)
        ...
        ctrl.shutdown_all()
    """

    def __init__(self) -> None:
        self._cameras: Dict[CameraID, MvCamera] = {}
        for cam_id, spec in CAMERA_SPECS.items():
            self._cameras[cam_id] = MvCamera(cam_id, spec)

    # -- bulk lifecycle ------------------------------------------------------

    def initialise_all(self) -> None:
        """Open all cameras, apply default parameters, and start grabbing."""
        for cam_id, cam in self._cameras.items():
            cam.open_device()
            spec = CAMERA_SPECS[cam_id]
            cam.set_trigger_mode(
                TriggerMode.HARDWARE_LINE0 if spec.trigger_mode == "hardware"
                else TriggerMode.SOFTWARE
            )
            # Apply per-station default exposures
            default_exposures = {
                CameraID.CCD1_TOP: 4000.0,
                CameraID.CCD2_SIDE: 3500.0,
                CameraID.CCD3_BOTTOM: 4000.0,
                CameraID.CCD4_INNER: 8000.0,
            }
            cam.set_exposure(default_exposures.get(cam_id, 5000.0))
            cam.start_grabbing()

    def shutdown_all(self) -> None:
        """Stop grabbing and close every camera."""
        for cam in self._cameras.values():
            cam.close_device()
        logger.info("All cameras shut down.")

    # -- capture interface ---------------------------------------------------

    def capture(self, camera_id: CameraID, timeout_ms: int = 1000) -> Optional[np.ndarray]:
        """
        Acquire a single frame from the specified camera.

        For hardware-triggered cameras, the trigger signal is expected to
        originate from the transfer-axis motion controller (PLC I/O line).
        For testing, a software trigger fallback is provided.
        """
        cam = self._cameras.get(camera_id)
        if cam is None:
            logger.error("No camera registered for %s.", camera_id.name)
            return None
        if not cam.is_open:
            logger.error("Camera %s is not open.", camera_id.name)
            return None
        frame = cam.get_one_frame(timeout_ms=timeout_ms)
        if frame is None:
            logger.warning("Frame timeout on camera %s.", camera_id.name)
        return frame

    def capture_side_rotation(self, n_frames: int = 8) -> List[np.ndarray]:
        """
        Capture a series of frames from CCD2 during 360-degree rotation.

        The rotation servo triggers CCD2 at equal angular intervals.
        Returns *n_frames* images covering the full perimeter of the CSE.
        """
        frames: List[np.ndarray] = []
        cam = self._cameras.get(CameraID.CCD2_SIDE)
        if cam is None:
            return frames
        for _ in range(n_frames):
            f = cam.get_one_frame(timeout_ms=500)
            if f is not None:
                frames.append(f)
        return frames

    def set_camera_exposure(self, camera_id: CameraID, exposure_us: float) -> None:
        """Runtime exposure adjustment (e.g. for auto-brightness compensation)."""
        cam = self._cameras.get(camera_id)
        if cam is not None:
            cam.set_exposure(exposure_us)

    def set_camera_roi(self, camera_id: CameraID,
                       offset_x: int, offset_y: int,
                       width: int, height: int) -> None:
        """Configure hardware ROI for a specific camera."""
        cam = self._cameras.get(camera_id)
        if cam is not None:
            cam.set_roi(offset_x, offset_y, width, height)

    def get_camera(self, camera_id: CameraID) -> Optional[MvCamera]:
        """Direct access to a MvCamera instance for advanced configuration."""
        return self._cameras.get(camera_id)
