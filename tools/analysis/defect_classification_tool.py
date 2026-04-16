"""
Defect classification pipeline for the TI CSE AOI system.

Routes images from each of the four CCD stations to the appropriate
detection algorithms and aggregates results across all 19 defect
categories organised into three severity tiers:

  - Function / Critical (8): crack, broken, epoxy exposal, insufficient
    epoxy, epoxy overflow, pin bent, pin oxidized, pin mis-cut
  - Cosmetic (4): dyeing contamination, non-electrical contamination,
    staining, code blur
  - Assembly (5+): no code, misalignment, pin bur, gold exposal,
    light leakage, yellow glass cement, edge staining

Each CCD maps to a specific subset of defect types. CCD2 (side) processes
multiple frames from a 360-degree rotation sequence.

Author: Rongxuan Zhou
"""

from __future__ import annotations

import logging
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional, Tuple

import numpy as np

from src.data_types.defect_types import (
    CameraID,
    DefectDetail,
    DefectSeverity,
    DefectType,
    InspectionResult,
    DEFECT_SEVERITY_MAP,
)
from src.global_variables.system_config import THRESHOLDS

logger = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# Per-defect detection primitives
# ---------------------------------------------------------------------------

def _compute_edge_strength(image: np.ndarray) -> np.ndarray:
    """Sobel-like edge magnitude (simplified)."""
    gx = np.diff(image.astype(np.float64), axis=1)
    gy = np.diff(image.astype(np.float64), axis=0)
    min_h, min_w = min(gx.shape[0], gy.shape[0]), min(gx.shape[1], gy.shape[1])
    return np.sqrt(gx[:min_h, :min_w] ** 2 + gy[:min_h, :min_w] ** 2)


def _laplacian_variance(image: np.ndarray) -> float:
    """Variance of a simple Laplacian for blur detection."""
    kernel_response = (
        image[2:, 1:-1].astype(np.float64)
        + image[:-2, 1:-1].astype(np.float64)
        + image[1:-1, 2:].astype(np.float64)
        + image[1:-1, :-2].astype(np.float64)
        - 4.0 * image[1:-1, 1:-1].astype(np.float64)
    )
    return float(np.var(kernel_response))


def _template_match_score(image: np.ndarray, template_mean: float = 120.0) -> float:
    """Simplified normalized cross-correlation proxy for code presence."""
    roi_mean = float(np.mean(image))
    roi_std = float(np.std(image))
    if roi_std < 1.0:
        return 0.0
    return abs(roi_mean - template_mean) / (roi_std + 1e-6)


# ---------------------------------------------------------------------------
# Detection functions per CCD
# ---------------------------------------------------------------------------

class _CCD1TopDetector:
    """Detection algorithms for top-side inspection (CCD1)."""

    @staticmethod
    def detect_crack(image: np.ndarray) -> Optional[DefectDetail]:
        edges = _compute_edge_strength(image)
        strong = edges > (THRESHOLDS.crack_edge_strength * 255)
        max_run = _CCD1TopDetector._longest_run(strong)
        if max_run >= THRESHOLDS.crack_min_length_px:
            conf = min(1.0, max_run / (THRESHOLDS.crack_min_length_px * 3))
            return DefectDetail(DefectType.F_CRACK, DefectSeverity.CRITICAL, conf, CameraID.CCD1_TOP)
        return None

    @staticmethod
    def detect_broken(image: np.ndarray) -> Optional[DefectDetail]:
        dark_ratio = float(np.mean(image < 30))
        if dark_ratio > THRESHOLDS.broken_area_ratio:
            return DefectDetail(DefectType.F_BROKEN, DefectSeverity.CRITICAL,
                                min(1.0, dark_ratio / 0.15), CameraID.CCD1_TOP)
        return None

    @staticmethod
    def detect_epoxy_exposal(image: np.ndarray) -> Optional[DefectDetail]:
        bright_ratio = float(np.mean(image > THRESHOLDS.epoxy_exposal_brightness))
        if bright_ratio > 0.01:
            return DefectDetail(DefectType.F_EPOXY_EXPOSAL, DefectSeverity.CRITICAL,
                                min(1.0, bright_ratio / 0.05), CameraID.CCD1_TOP)
        return None

    @staticmethod
    def detect_insufficient_epoxy(image: np.ndarray) -> Optional[DefectDetail]:
        edges = _compute_edge_strength(image)
        gap_mask = edges < 10
        gap_cols = np.sum(gap_mask, axis=0)
        max_gap = int(np.max(gap_cols)) if gap_cols.size > 0 else 0
        if max_gap > THRESHOLDS.insufficient_epoxy_gap_px:
            return DefectDetail(DefectType.F_INSUFFICIENT_EPOXY, DefectSeverity.CRITICAL,
                                min(1.0, max_gap / 60.0), CameraID.CCD1_TOP)
        return None

    @staticmethod
    def detect_dyeing_contamination(image: np.ndarray) -> Optional[DefectDetail]:
        local_std = _block_std(image, block=32)
        anomaly = float(np.mean(local_std > 50))
        if anomaly > 0.05:
            return DefectDetail(DefectType.C_DYEING_CONTAMINATION, DefectSeverity.COSMETIC,
                                min(1.0, anomaly / 0.15), CameraID.CCD1_TOP)
        return None

    @staticmethod
    def detect_non_electrical_contamination(image: np.ndarray) -> Optional[DefectDetail]:
        mean_val = float(np.mean(image))
        outlier_ratio = float(np.mean(np.abs(image.astype(np.float64) - mean_val) > 80))
        if outlier_ratio > 0.02:
            return DefectDetail(DefectType.C_NON_ELECTRICAL_CONTAMINATION, DefectSeverity.COSMETIC,
                                min(1.0, outlier_ratio / 0.08), CameraID.CCD1_TOP)
        return None

    @staticmethod
    def detect_no_code(image: np.ndarray) -> Optional[DefectDetail]:
        score = _template_match_score(image)
        if score < THRESHOLDS.code_absence_template_score:
            return DefectDetail(DefectType.A_NO_CODE, DefectSeverity.ASSEMBLY,
                                min(1.0, 1.0 - score), CameraID.CCD1_TOP)
        return None

    @staticmethod
    def detect_code_blur(image: np.ndarray) -> Optional[DefectDetail]:
        variance = _laplacian_variance(image)
        if variance < THRESHOLDS.code_blur_variance:
            return DefectDetail(DefectType.C_CODE_BLUR, DefectSeverity.COSMETIC,
                                min(1.0, THRESHOLDS.code_blur_variance / (variance + 1)),
                                CameraID.CCD1_TOP)
        return None

    @staticmethod
    def detect_misalignment(image: np.ndarray) -> Optional[DefectDetail]:
        h, w = image.shape[:2]
        left_mean = float(np.mean(image[:, : w // 4]))
        right_mean = float(np.mean(image[:, 3 * w // 4 :]))
        asymmetry = abs(left_mean - right_mean) / (max(left_mean, right_mean) + 1e-6)
        if asymmetry > 0.20:
            return DefectDetail(DefectType.A_MISALIGNMENT, DefectSeverity.ASSEMBLY,
                                min(1.0, asymmetry / 0.4), CameraID.CCD1_TOP)
        return None

    @staticmethod
    def _longest_run(mask: np.ndarray) -> int:
        """Longest horizontal run of True values in a 2-D boolean array."""
        best = 0
        for row in mask:
            run = 0
            for v in row:
                if v:
                    run += 1
                    best = max(best, run)
                else:
                    run = 0
        return best


class _CCD2SideDetector:
    """Detection algorithms for side-view inspection (CCD2, 360-degree frames)."""

    @staticmethod
    def detect_pin_bent(frames: List[np.ndarray]) -> Optional[DefectDetail]:
        for frame in frames:
            col_means = np.mean(frame, axis=0)
            diffs = np.abs(np.diff(col_means))
            max_diff = float(np.max(diffs)) if diffs.size > 0 else 0.0
            if max_diff > THRESHOLDS.pin_bend_angle_deg * 10:
                return DefectDetail(DefectType.F_PIN_BENT, DefectSeverity.CRITICAL,
                                    min(1.0, max_diff / 100.0), CameraID.CCD2_SIDE)
        return None

    @staticmethod
    def detect_pin_oxidized(frames: List[np.ndarray]) -> Optional[DefectDetail]:
        for frame in frames:
            std_val = float(np.std(frame))
            if std_val > THRESHOLDS.pin_oxidation_color_delta:
                return DefectDetail(DefectType.F_PIN_OXIDIZED, DefectSeverity.CRITICAL,
                                    min(1.0, std_val / 60.0), CameraID.CCD2_SIDE)
        return None

    @staticmethod
    def detect_pin_bur(frames: List[np.ndarray]) -> Optional[DefectDetail]:
        for frame in frames:
            edges = _compute_edge_strength(frame)
            protrusions = np.sum(edges > 200)
            if protrusions > THRESHOLDS.pin_bur_protrusion_px * frame.shape[1]:
                return DefectDetail(DefectType.A_PIN_BUR, DefectSeverity.ASSEMBLY,
                                    min(1.0, protrusions / 5000.0), CameraID.CCD2_SIDE)
        return None

    @staticmethod
    def detect_pin_miscut(frames: List[np.ndarray]) -> Optional[DefectDetail]:
        for frame in frames:
            h, w = frame.shape[:2]
            bottom_strip = frame[int(h * 0.8):, :]
            fill_ratio = float(np.mean(bottom_strip > 60))
            if fill_ratio < THRESHOLDS.pin_miscut_length_ratio:
                return DefectDetail(DefectType.F_PIN_MIS_CUT, DefectSeverity.CRITICAL,
                                    min(1.0, (1.0 - fill_ratio) / 0.3), CameraID.CCD2_SIDE)
        return None

    @staticmethod
    def detect_gold_exposal(frames: List[np.ndarray]) -> Optional[DefectDetail]:
        for frame in frames:
            bright_ratio = float(np.mean(frame > 220))
            if bright_ratio > 0.005:
                return DefectDetail(DefectType.A_GOLD_EXPOSAL, DefectSeverity.ASSEMBLY,
                                    min(1.0, bright_ratio / 0.02), CameraID.CCD2_SIDE)
        return None


class _CCD3BottomDetector:
    """Detection algorithms for bottom-side inspection (CCD3)."""

    @staticmethod
    def detect_crack(image: np.ndarray) -> Optional[DefectDetail]:
        edges = _compute_edge_strength(image)
        strong_count = int(np.sum(edges > THRESHOLDS.crack_edge_strength * 255))
        total = edges.shape[0] * edges.shape[1]
        ratio = strong_count / total if total > 0 else 0.0
        if ratio > 0.005:
            return DefectDetail(DefectType.F_CRACK, DefectSeverity.CRITICAL,
                                min(1.0, ratio / 0.02), CameraID.CCD3_BOTTOM)
        return None

    @staticmethod
    def detect_broken(image: np.ndarray) -> Optional[DefectDetail]:
        dark_ratio = float(np.mean(image < 25))
        if dark_ratio > THRESHOLDS.broken_area_ratio:
            return DefectDetail(DefectType.F_BROKEN, DefectSeverity.CRITICAL,
                                min(1.0, dark_ratio / 0.15), CameraID.CCD3_BOTTOM)
        return None

    @staticmethod
    def detect_epoxy_exposal(image: np.ndarray) -> Optional[DefectDetail]:
        bright = float(np.mean(image > THRESHOLDS.epoxy_exposal_brightness))
        if bright > 0.01:
            return DefectDetail(DefectType.F_EPOXY_EXPOSAL, DefectSeverity.CRITICAL,
                                min(1.0, bright / 0.05), CameraID.CCD3_BOTTOM)
        return None

    @staticmethod
    def detect_epoxy_overflow(image: np.ndarray) -> Optional[DefectDetail]:
        edges = _compute_edge_strength(image)
        overflow_mask = edges > 100
        overflow_area = int(np.sum(overflow_mask))
        if overflow_area > THRESHOLDS.epoxy_overflow_area_px:
            return DefectDetail(DefectType.F_EPOXY_OVERFLOW, DefectSeverity.CRITICAL,
                                min(1.0, overflow_area / 2000.0), CameraID.CCD3_BOTTOM)
        return None

    @staticmethod
    def detect_staining(image: np.ndarray) -> Optional[DefectDetail]:
        mean_val = float(np.mean(image))
        stain_mask = np.abs(image.astype(np.float64) - mean_val) > 60
        stain_ratio = float(np.mean(stain_mask))
        if stain_ratio > 0.03:
            return DefectDetail(DefectType.C_STAINING, DefectSeverity.COSMETIC,
                                min(1.0, stain_ratio / 0.1), CameraID.CCD3_BOTTOM)
        return None


# ---------------------------------------------------------------------------
# Utility
# ---------------------------------------------------------------------------

def _block_std(image: np.ndarray, block: int = 32) -> np.ndarray:
    """Compute local standard deviation over non-overlapping blocks."""
    h, w = image.shape[:2]
    bh, bw = h // block, w // block
    result = np.zeros((bh, bw), dtype=np.float64)
    for i in range(bh):
        for j in range(bw):
            patch = image[i * block : (i + 1) * block, j * block : (j + 1) * block]
            result[i, j] = float(np.std(patch))
    return result


# ---------------------------------------------------------------------------
# Aggregating classifier
# ---------------------------------------------------------------------------

class DefectClassifier:
    """
    Master defect classification pipeline.

    Routes each CCD frame to the relevant set of detection algorithms,
    collects findings, and merges them into a single ``InspectionResult``.
    """

    def __init__(self) -> None:
        self._ccd1 = _CCD1TopDetector()
        self._ccd2 = _CCD2SideDetector()
        self._ccd3 = _CCD3BottomDetector()

    def classify_top(self, image: np.ndarray, result: InspectionResult) -> None:
        """Run all CCD1 top-side detectors and update *result*."""
        detectors = [
            self._ccd1.detect_crack,
            self._ccd1.detect_broken,
            self._ccd1.detect_epoxy_exposal,
            self._ccd1.detect_insufficient_epoxy,
            self._ccd1.detect_dyeing_contamination,
            self._ccd1.detect_non_electrical_contamination,
            self._ccd1.detect_no_code,
            self._ccd1.detect_code_blur,
            self._ccd1.detect_misalignment,
        ]
        self._run_detectors(image, detectors, CameraID.CCD1_TOP, result)

    def classify_side(self, frames: List[np.ndarray], result: InspectionResult) -> None:
        """Run all CCD2 side detectors on the rotation frame sequence."""
        for detect_fn in [
            self._ccd2.detect_pin_bent,
            self._ccd2.detect_pin_oxidized,
            self._ccd2.detect_pin_bur,
            self._ccd2.detect_pin_miscut,
            self._ccd2.detect_gold_exposal,
        ]:
            defect = detect_fn(frames)
            if defect is not None and defect.confidence >= THRESHOLDS.confidence_accept:
                result.add_defect(defect)
        if CameraID.CCD2_SIDE not in result.ccd_results:
            result.ccd_results[CameraID.CCD2_SIDE] = True

    def classify_bottom(self, image: np.ndarray, result: InspectionResult) -> None:
        """Run all CCD3 bottom-side detectors and update *result*."""
        detectors = [
            self._ccd3.detect_crack,
            self._ccd3.detect_broken,
            self._ccd3.detect_epoxy_exposal,
            self._ccd3.detect_epoxy_overflow,
            self._ccd3.detect_staining,
        ]
        self._run_detectors(image, detectors, CameraID.CCD3_BOTTOM, result)

    def classify_all(
        self,
        top_frame: Optional[np.ndarray],
        side_frames: Optional[List[np.ndarray]],
        bottom_frame: Optional[np.ndarray],
        lighting_defects: Optional[List[DefectDetail]] = None,
    ) -> InspectionResult:
        """
        Convenience method: run classification across all CCDs and return
        a unified InspectionResult.
        """
        result = InspectionResult()

        if top_frame is not None:
            self.classify_top(top_frame, result)
        if side_frames is not None:
            self.classify_side(side_frames, result)
        if bottom_frame is not None:
            self.classify_bottom(bottom_frame, result)
        if lighting_defects:
            for d in lighting_defects:
                result.add_defect(d)

        return result

    # -- internal helpers ----------------------------------------------------

    @staticmethod
    def _run_detectors(
        image: np.ndarray,
        detectors: List[Callable],
        camera_id: CameraID,
        result: InspectionResult,
    ) -> None:
        """Execute a list of single-image detectors and accumulate defects."""
        for detect_fn in detectors:
            defect = detect_fn(image)
            if defect is not None and defect.confidence >= THRESHOLDS.confidence_accept:
                result.add_defect(defect)
        # Mark CCD as passed if no defects were added from this camera
        if camera_id not in result.ccd_results:
            result.ccd_results[camera_id] = True
