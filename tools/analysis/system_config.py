"""
System configuration constants for the TI CSE AOI inspection machine.

Centralizes all hardware parameters, detection thresholds, transfer positions,
timing budgets, and production-rate targets. Values are derived from the
machine's mechanical design and camera/optics specifications.

Author: Rongxuan Zhou
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Dict, Tuple

from src.data_types.defect_types import CameraID


# ---------------------------------------------------------------------------
# Camera hardware parameters
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class CameraSpec:
    """Optical and sensor specification for a single CCD station."""
    model: str
    resolution_px: Tuple[int, int]      # (width, height) pixels
    pixel_size_mm: float                # Physical pixel pitch on sensor (mm)
    fov_mm: Tuple[float, float]         # (width, height) field of view in mm
    working_distance_mm: float
    magnification: float
    lens_type: str
    trigger_mode: str                   # "hardware" | "software"


CAMERA_SPECS: Dict[CameraID, CameraSpec] = {
    CameraID.CCD1_TOP: CameraSpec(
        model="MV-GE501GC",
        resolution_px=(2592, 1944),
        pixel_size_mm=0.0022,
        fov_mm=(5.7, 4.3),
        working_distance_mm=65.0,
        magnification=1.0,
        lens_type="telecentric_1x",
        trigger_mode="hardware",
    ),
    CameraID.CCD2_SIDE: CameraSpec(
        model="MV-GE501GC",
        resolution_px=(2592, 1944),
        pixel_size_mm=0.0022,
        fov_mm=(5.7, 4.3),
        working_distance_mm=65.0,
        magnification=1.0,
        lens_type="telecentric_1x",
        trigger_mode="hardware",
    ),
    CameraID.CCD3_BOTTOM: CameraSpec(
        model="MV-GE501GC",
        resolution_px=(2592, 1944),
        pixel_size_mm=0.0022,
        fov_mm=(5.7, 4.3),
        working_distance_mm=65.0,
        magnification=1.0,
        lens_type="telecentric_1x",
        trigger_mode="hardware",
    ),
    CameraID.CCD4_INNER: CameraSpec(
        model="MV-GE2000C",
        resolution_px=(5472, 3648),
        pixel_size_mm=0.0024,
        fov_mm=(37.7, 25.1),
        working_distance_mm=120.0,
        magnification=0.35,
        lens_type="telecentric_0.35x",
        trigger_mode="hardware",
    ),
}


# ---------------------------------------------------------------------------
# Detection thresholds
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class DetectionThresholds:
    """Threshold parameters for vision algorithms."""
    # Lighting check (CCD4)
    light_leakage_intensity_min: int = 180
    light_leakage_area_ratio: float = 0.002
    staining_contrast_threshold: float = 0.35
    yellow_cement_hue_range: Tuple[int, int] = (18, 35)

    # Crack / broken detection
    crack_edge_strength: float = 0.45
    crack_min_length_px: int = 30
    broken_area_ratio: float = 0.05

    # Epoxy defects
    epoxy_exposal_brightness: int = 200
    epoxy_overflow_area_px: int = 500
    insufficient_epoxy_gap_px: int = 20

    # Pin defects (CCD2 side)
    pin_bend_angle_deg: float = 5.0
    pin_oxidation_color_delta: float = 25.0
    pin_bur_protrusion_px: int = 8
    pin_miscut_length_ratio: float = 0.85

    # Code quality
    code_blur_variance: float = 100.0
    code_absence_template_score: float = 0.3

    # Orientation
    orientation_match_threshold: float = 0.7
    orientation_angle_tolerance_deg: float = 2.0

    # General
    confidence_accept: float = 0.85
    ng_double_check_confirm: float = 0.80


THRESHOLDS = DetectionThresholds()


# ---------------------------------------------------------------------------
# Transfer axis positions (mm) and timing
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class TransferPosition:
    """Home and target positions for a single linear transfer axis."""
    axis_id: int
    home_mm: float
    target_mm: float
    velocity_mm_s: float
    acceleration_mm_s2: float


TRANSFER_POSITIONS: Dict[int, TransferPosition] = {
    1: TransferPosition(axis_id=1, home_mm=0.0, target_mm=150.0, velocity_mm_s=200.0, acceleration_mm_s2=800.0),
    2: TransferPosition(axis_id=2, home_mm=0.0, target_mm=180.0, velocity_mm_s=150.0, acceleration_mm_s2=600.0),
    3: TransferPosition(axis_id=3, home_mm=0.0, target_mm=120.0, velocity_mm_s=200.0, acceleration_mm_s2=800.0),
    4: TransferPosition(axis_id=4, home_mm=0.0, target_mm=160.0, velocity_mm_s=180.0, acceleration_mm_s2=700.0),
    5: TransferPosition(axis_id=5, home_mm=0.0, target_mm=140.0, velocity_mm_s=200.0, acceleration_mm_s2=800.0),
    6: TransferPosition(axis_id=6, home_mm=0.0, target_mm=130.0, velocity_mm_s=200.0, acceleration_mm_s2=800.0),
}


# ---------------------------------------------------------------------------
# Production and timing targets
# ---------------------------------------------------------------------------

UNITS_PER_PICK_CYCLE: int = 4
TARGET_UNITS_PER_DAY: int = 85_000
OPERATING_HOURS_PER_DAY: float = 22.0          # 2 hours for changeover / maintenance
TARGET_CYCLE_TIME_SEC: float = (OPERATING_HOURS_PER_DAY * 3600) / (TARGET_UNITS_PER_DAY / UNITS_PER_PICK_CYCLE)

# Individual step time budgets (seconds)
STEP_TIME_BUDGET: Dict[str, float] = {
    "loading": 0.8,
    "orientation_pre_check": 0.3,
    "pitch_change": 0.6,
    "transfer": 0.4,
    "lighting_check": 0.5,
    "bottom_check": 0.4,
    "top_check": 0.4,
    "orientation_comp": 0.3,
    "side_check": 0.6,
    "ng_double_check": 0.5,
    "unloading": 0.8,
    "ng_sorting": 1.0,
}

# Robot parameters
ROBOT_TCP_IP: str = "192.168.1.10"
ROBOT_TCP_PORT: int = 2000
ROBOT_VACUUM_PRESSURE_KPA: float = -60.0
ROBOT_ROTATION_SPEED_DEG_S: float = 360.0

# Pitch changer
PITCH_COMPACT_SPACING_MM: float = 2.54
PITCH_INSPECTION_SPACING_MM: float = 12.0

# NG management
NG_TRAY_CAPACITY: int = 50
NG_CONVEYOR_SPEED_MM_S: float = 100.0
