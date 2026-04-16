"""
Poka-Yoke orientation detection for the TI CSE AOI system.

Provides two distinct orientation services:

1. **Pre-load orientation check** -- Before the SCARA robot loads a CSE from
   the basket, this module analyses a CCD image to determine pin position and
   marking-code orientation. If the unit is inverted (180-degree flip), the
   robot is instructed to rotate the nozzle before placement.

2. **Orientation compensation** -- After top/bottom inspection and before
   CCD#2 side check, the module computes the precise rotation angle needed
   so that the CSE is aligned to the reference orientation for the 360-degree
   side scan.

Author: Rongxuan Zhou
"""

from __future__ import annotations

import logging
import math
from dataclasses import dataclass
from enum import Enum, auto
from typing import Optional, Tuple

import numpy as np

from src.data_types.defect_types import CameraID
from src.global_variables.system_config import THRESHOLDS

logger = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# Types
# ---------------------------------------------------------------------------

class Orientation(Enum):
    """Detected CSE orientation state."""
    CORRECT = auto()
    FLIPPED_180 = auto()
    UNKNOWN = auto()


@dataclass
class OrientationResult:
    """Result of an orientation analysis."""
    orientation: Orientation
    rotation_angle_deg: float = 0.0
    confidence: float = 0.0
    pin_centroid: Optional[Tuple[int, int]] = None
    code_centroid: Optional[Tuple[int, int]] = None


# ---------------------------------------------------------------------------
# Detector
# ---------------------------------------------------------------------------

class OrientationDetector:
    """
    Vision-based orientation analyser for CSE units.

    Uses intensity-profile asymmetry (pin side vs. glass side) and
    marking-code blob detection to determine unit orientation.
    """

    def __init__(
        self,
        pin_region_fraction: float = 0.25,
        code_region_fraction: float = 0.20,
    ) -> None:
        """
        Args:
            pin_region_fraction: Fraction of the image height treated as the
                                 pin-side strip (from the bottom edge).
            code_region_fraction: Fraction of the image height treated as the
                                  code/marking strip (from the top edge).
        """
        self._pin_frac = pin_region_fraction
        self._code_frac = code_region_fraction

    # -- public interface ----------------------------------------------------

    def detect_pre_load(self, image: np.ndarray) -> OrientationResult:
        """
        Determine whether a CSE in the basket needs a 180-degree flip.

        Strategy:
          - The pin side has higher mean intensity (metallic reflection).
          - The marking code appears as a dark text region on the top side.
          - If pins are detected at the top of the image, the unit is flipped.
        """
        h, w = image.shape[:2]
        top_strip = image[: int(h * self._pin_frac), :]
        bottom_strip = image[int(h * (1.0 - self._pin_frac)) :, :]

        top_mean = float(np.mean(top_strip))
        bottom_mean = float(np.mean(bottom_strip))

        # Pins reflect more light -> higher intensity
        pin_at_top = top_mean > bottom_mean
        intensity_diff = abs(top_mean - bottom_mean)
        max_val = max(top_mean, bottom_mean, 1.0)
        confidence = min(1.0, intensity_diff / (max_val * 0.3))

        # Code detection: look for high-variance region (text edge energy)
        code_top_var = float(np.var(image[: int(h * self._code_frac), :]))
        code_bottom_var = float(np.var(image[int(h * (1.0 - self._code_frac)) :, :]))
        code_at_top = code_top_var > code_bottom_var

        # Decision logic: pins should be at bottom, code at top
        if pin_at_top:
            orientation = Orientation.FLIPPED_180
            angle = 180.0
        elif not code_at_top:
            orientation = Orientation.FLIPPED_180
            angle = 180.0
        else:
            orientation = Orientation.CORRECT
            angle = 0.0

        if confidence < THRESHOLDS.orientation_match_threshold:
            orientation = Orientation.UNKNOWN

        pin_cy = int(h * 0.1) if pin_at_top else int(h * 0.9)
        code_cy = int(h * 0.1) if code_at_top else int(h * 0.9)

        return OrientationResult(
            orientation=orientation,
            rotation_angle_deg=angle,
            confidence=confidence,
            pin_centroid=(w // 2, pin_cy),
            code_centroid=(w // 2, code_cy),
        )

    def compute_compensation_angle(self, image: np.ndarray) -> float:
        """
        Compute the fine rotation angle needed before the CCD#2 side check.

        Uses principal-axis analysis on the binarised package outline to
        find the orientation offset relative to the nominal 0-degree
        reference.

        Returns:
            Rotation angle in degrees (positive = counter-clockwise).
        """
        binary = (image > np.mean(image)).astype(np.uint8)
        angle = self._principal_axis_angle(binary)
        # Snap to nearest 90-degree if close to axis-aligned
        for ref in [0.0, 90.0, 180.0, 270.0, 360.0]:
            if abs(angle - ref) < THRESHOLDS.orientation_angle_tolerance_deg:
                angle = ref
                break
        return angle % 360.0

    # -- internal helpers ----------------------------------------------------

    @staticmethod
    def _principal_axis_angle(binary: np.ndarray) -> float:
        """
        Compute the orientation of the principal axis of a binary blob
        using image moments (simplified PCA on foreground pixels).
        """
        ys, xs = np.nonzero(binary)
        if len(xs) < 10:
            return 0.0

        cx = float(np.mean(xs))
        cy = float(np.mean(ys))
        dx = xs.astype(np.float64) - cx
        dy = ys.astype(np.float64) - cy

        # Second-order central moments
        mu20 = float(np.sum(dx * dx))
        mu02 = float(np.sum(dy * dy))
        mu11 = float(np.sum(dx * dy))

        # Orientation of the major axis
        theta = 0.5 * math.atan2(2.0 * mu11, mu20 - mu02)
        return math.degrees(theta) % 360.0
