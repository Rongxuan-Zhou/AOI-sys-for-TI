"""
CCD#4 closed-chamber lighting-check analyser.

The lighting check station places the CSE package inside a sealed dark
chamber with controlled illumination through the sapphire glass window.
CCD#4 (MV-GE2000C, 20 MP, 0.0069 mm/px effective resolution via telecentric
optics) captures the transmitted/reflected light pattern.

Detection targets:
  1. Light leakage -- indicates cracked or improperly sealed sapphire glass.
  2. Edge staining -- contamination at the glass-to-frame bond line.
  3. Yellow glass cement -- discoloration of the optical adhesive.

Author: Rongxuan Zhou
"""

from __future__ import annotations

import logging
from dataclasses import dataclass, field
from enum import Enum, auto
from typing import List, Optional, Tuple

import numpy as np

from src.data_types.defect_types import (
    CameraID,
    DefectDetail,
    DefectSeverity,
    DefectType,
)
from src.global_variables.system_config import CAMERA_SPECS, THRESHOLDS

logger = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# Result dataclass
# ---------------------------------------------------------------------------

class LightingDefect(Enum):
    """Specific defect sub-types detectable at the lighting-check station."""
    LIGHT_LEAKAGE = auto()
    EDGE_STAINING = auto()
    YELLOW_GLASS_CEMENT = auto()


@dataclass
class LightingCheckResult:
    """Outcome of the CCD#4 lighting-check analysis for one CSE unit."""
    passed: bool = True
    confidence: float = 1.0
    defects_found: List[LightingDefect] = field(default_factory=list)
    defect_details: List[DefectDetail] = field(default_factory=list)
    leakage_area_ratio: float = 0.0
    staining_score: float = 0.0
    yellow_cement_score: float = 0.0
    defect_mask: Optional[np.ndarray] = None   # Binary mask of flagged pixels


# ---------------------------------------------------------------------------
# Analyser
# ---------------------------------------------------------------------------

class LightingCheckAnalyzer:
    """
    Image-processing pipeline for the closed-chamber lighting inspection.

    Processing stages:
      1. ROI extraction -- isolate the sapphire glass region using a
         pre-calibrated bounding box.
      2. Histogram analysis -- compute intensity distribution to detect
         abnormal bright regions (light leakage).
      3. Edge-band staining check -- analyse the glass perimeter for
         contamination via local contrast metrics.
      4. Yellow cement detection -- convert to HSV and threshold the
         hue channel for discoloured adhesive.
    """

    def __init__(
        self,
        glass_roi: Tuple[int, int, int, int] = (1800, 1200, 1900, 1300),
        edge_band_width_px: int = 60,
    ) -> None:
        """
        Args:
            glass_roi: (x, y, w, h) bounding box for the sapphire glass region
                       in the full CCD#4 frame.
            edge_band_width_px: Width of the perimeter band used for edge
                                staining analysis.
        """
        self._glass_roi = glass_roi
        self._edge_band_width = edge_band_width_px
        self._spec = CAMERA_SPECS[CameraID.CCD4_INNER]
        self._effective_px_mm: float = 0.0069   # mm per pixel at object plane

    # -- public interface ----------------------------------------------------

    def analyse(self, frame: np.ndarray) -> LightingCheckResult:
        """
        Run the full lighting-check pipeline on a single CCD#4 frame.

        Args:
            frame: 2-D uint8 array (grayscale) from CCD#4.

        Returns:
            LightingCheckResult with pass/fail and per-defect scores.
        """
        result = LightingCheckResult()

        roi_img = self._extract_roi(frame)
        if roi_img is None:
            logger.error("ROI extraction failed; frame shape %s.", frame.shape)
            result.passed = False
            result.confidence = 0.0
            return result

        mask = np.zeros(roi_img.shape[:2], dtype=np.uint8)

        # Stage 1 -- light leakage
        leakage_ratio, leakage_mask = self._detect_light_leakage(roi_img)
        result.leakage_area_ratio = leakage_ratio
        mask = np.bitwise_or(mask, leakage_mask)

        if leakage_ratio > THRESHOLDS.light_leakage_area_ratio:
            result.passed = False
            result.defects_found.append(LightingDefect.LIGHT_LEAKAGE)
            result.defect_details.append(DefectDetail(
                defect_type=DefectType.A_LIGHT_LEAKAGE,
                severity=DefectSeverity.ASSEMBLY,
                confidence=min(1.0, leakage_ratio / (THRESHOLDS.light_leakage_area_ratio * 3)),
                camera_id=CameraID.CCD4_INNER,
                description=f"Light leakage area ratio {leakage_ratio:.4f}",
            ))

        # Stage 2 -- edge staining
        staining_score, staining_mask = self._detect_edge_staining(roi_img)
        result.staining_score = staining_score
        mask = np.bitwise_or(mask, staining_mask)

        if staining_score > THRESHOLDS.staining_contrast_threshold:
            result.passed = False
            result.defects_found.append(LightingDefect.EDGE_STAINING)
            result.defect_details.append(DefectDetail(
                defect_type=DefectType.A_EDGE_STAINING,
                severity=DefectSeverity.ASSEMBLY,
                confidence=min(1.0, staining_score),
                camera_id=CameraID.CCD4_INNER,
                description=f"Edge staining contrast score {staining_score:.3f}",
            ))

        # Stage 3 -- yellow glass cement
        yellow_score, yellow_mask = self._detect_yellow_cement(roi_img)
        result.yellow_cement_score = yellow_score
        mask = np.bitwise_or(mask, yellow_mask)

        if yellow_score > THRESHOLDS.staining_contrast_threshold:
            result.passed = False
            result.defects_found.append(LightingDefect.YELLOW_GLASS_CEMENT)
            result.defect_details.append(DefectDetail(
                defect_type=DefectType.A_YELLOW_GLASS_CEMENT,
                severity=DefectSeverity.ASSEMBLY,
                confidence=min(1.0, yellow_score),
                camera_id=CameraID.CCD4_INNER,
                description=f"Yellow cement score {yellow_score:.3f}",
            ))

        # Aggregate confidence
        if result.passed:
            result.confidence = 1.0 - max(leakage_ratio, staining_score, yellow_score)
        else:
            scores = [d.confidence for d in result.defect_details]
            result.confidence = max(scores) if scores else 0.0

        result.defect_mask = mask
        return result

    # -- internal stages -----------------------------------------------------

    def _extract_roi(self, frame: np.ndarray) -> Optional[np.ndarray]:
        """Crop the sapphire glass region from the full frame."""
        x, y, w, h = self._glass_roi
        fh, fw = frame.shape[:2]
        if x + w > fw or y + h > fh:
            # Fall back to full frame if ROI exceeds bounds (e.g. during testing)
            logger.warning("Glass ROI exceeds frame; using full frame.")
            return frame
        return frame[y : y + h, x : x + w]

    def _detect_light_leakage(self, roi: np.ndarray) -> Tuple[float, np.ndarray]:
        """
        Threshold-based bright-spot detection for light leakage.

        Returns (area_ratio, binary_mask).
        """
        threshold = THRESHOLDS.light_leakage_intensity_min
        bright_mask = (roi > threshold).astype(np.uint8) * 255
        total_pixels = roi.shape[0] * roi.shape[1]
        bright_pixels = int(np.count_nonzero(bright_mask))
        ratio = bright_pixels / total_pixels if total_pixels > 0 else 0.0
        return ratio, bright_mask

    def _detect_edge_staining(self, roi: np.ndarray) -> Tuple[float, np.ndarray]:
        """
        Analyse the perimeter band of the glass region for contamination.

        Computes the standard deviation of pixel intensities in the edge band
        relative to the interior mean. High contrast indicates staining.
        """
        h, w = roi.shape[:2]
        bw = self._edge_band_width
        mask = np.zeros_like(roi, dtype=np.uint8)

        # Create edge band mask
        edge_mask = np.zeros((h, w), dtype=bool)
        edge_mask[:bw, :] = True
        edge_mask[-bw:, :] = True
        edge_mask[:, :bw] = True
        edge_mask[:, -bw:] = True

        interior_mask = ~edge_mask
        if not np.any(interior_mask):
            return 0.0, mask

        interior_mean = float(np.mean(roi[interior_mask]))
        edge_pixels = roi[edge_mask].astype(np.float64)

        if interior_mean < 1.0:
            interior_mean = 1.0

        edge_contrast = float(np.std(edge_pixels)) / interior_mean
        # Mark pixels in the edge band that deviate significantly
        deviation = np.abs(edge_pixels - interior_mean) / interior_mean
        stain_idx = deviation > THRESHOLDS.staining_contrast_threshold
        stain_full = np.zeros((h, w), dtype=np.uint8)
        temp = np.zeros_like(edge_pixels, dtype=np.uint8)
        temp[stain_idx] = 255
        stain_full[edge_mask] = temp

        return edge_contrast, stain_full

    def _detect_yellow_cement(self, roi: np.ndarray) -> Tuple[float, np.ndarray]:
        """
        Detect discoloured glass cement via hue-channel thresholding.

        For a grayscale input (mono camera), this simulates hue analysis by
        looking at mid-intensity warm-toned regions. In production with a
        colour camera, a proper HSV conversion is used.
        """
        h_lo, h_hi = THRESHOLDS.yellow_cement_hue_range
        # Simulate: treat intensity range [h_lo*4, h_hi*4] as a proxy for
        # yellow-ish appearance under controlled lighting.
        lo_intensity = h_lo * 4
        hi_intensity = h_hi * 4
        yellow_mask = ((roi >= lo_intensity) & (roi <= hi_intensity)).astype(np.uint8) * 255

        total = roi.shape[0] * roi.shape[1]
        yellow_area = int(np.count_nonzero(yellow_mask))
        score = yellow_area / total if total > 0 else 0.0
        return score, yellow_mask
