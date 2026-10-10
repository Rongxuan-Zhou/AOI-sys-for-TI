"""
Data type definitions for the TI CSE AOI inspection system.

Defines all enumerations and dataclasses used across inspection, vision,
material handling, and NG management subsystems. The 19 defect categories
of spec V1.4 (slide 8) plus the two inner-staining-residue sub-checks
mirror src/vision/DefectTypes.h.

Author: Rongxuan Zhou
"""

from __future__ import annotations

import uuid
from dataclasses import dataclass, field
from datetime import datetime
from enum import Enum, IntEnum, auto
from typing import Dict, List, Optional, Tuple


# ---------------------------------------------------------------------------
# Camera and process enumerations
# ---------------------------------------------------------------------------

class CameraID(IntEnum):
    """Physical CCD identifiers on the AOI machine."""
    CCD1_TOP = 1       # Top-side inspection (MV-GE501GC)
    CCD2_SIDE = 2      # Side inspection via 360-degree rotation (MV-GE501GC)
    CCD3_BOTTOM = 3    # Bottom-side inspection (MV-GE501GC)
    CCD4_INNER = 4     # Closed-chamber lighting check (MV-GE2000C)
    NG_CHECK = 5       # NG reconfirmation camera


class ProcessStep(Enum):
    """18-step inspection process states."""
    IDLE = auto()
    LOADING = auto()
    ORIENTATION_PRE_CHECK = auto()
    PITCH_CHANGE = auto()
    TRANSFER_TO_INSPECT_1 = auto()
    TRANSFER_TO_INSPECT_2 = auto()
    LIGHTING_CHECK = auto()
    BOTTOM_CHECK = auto()
    TOP_CHECK = auto()
    ORIENTATION_COMP = auto()
    SIDE_CHECK = auto()
    TRANSFER_TO_UNLOAD_1 = auto()
    TRANSFER_TO_UNLOAD_2 = auto()
    NG_DOUBLE_CHECK = auto()
    UNLOADING = auto()
    NG_SORTING = auto()
    GOOD_OUTPUT = auto()
    ERROR = auto()


# ---------------------------------------------------------------------------
# Defect severity and type
# ---------------------------------------------------------------------------

class DefectSeverity(Enum):
    """Severity classification for defect types."""
    CRITICAL = "critical"       # purple items on spec slide 8: lighting check, crack, broken, misalignment
    COSMETIC = "cosmetic"       # cosmetic group -- unit functions but fails appearance spec
    ASSEMBLY = "assembly"       # function / assembly items that are not critical (MAJOR in the C++ core)


class DefectGroup(Enum):
    """Grouping of spec V1.4 slide 8 (mirrors aoi::DefectGroup)."""
    FUNCTION = "function"
    COSMETIC = "cosmetic"
    ASSEMBLY = "assembly"
    INNER_RESIDUE = "inner_residue"


class DefectType(Enum):
    """
    Defect categories of spec V1.4 (slide 8) in spec order, mirroring
    aoi::DefectType in src/vision/DefectTypes.h. The value is the snake_case
    name used by the YAML configs; the PLC DEFECT_CODE is index + 1.

    The pre-V1.4 member names (F_CRACK, A_LIGHT_LEAKAGE, ...) are kept as
    aliases so the offline tools keep working; new code uses the spec names.
    """
    # -- Function (9) --
    LIGHTING_CHECK = "lighting_check"
    CRACK = "crack"
    BROKEN = "broken"
    EPOXY_EXPOSAL = "epoxy_exposal"
    PIN_MISSING = "pin_missing"
    ELECTRICAL_CONTAMINATION = "electrical_contamination"
    GOLD_EXPOSAL = "gold_exposal"
    INSUFFICIENT_EPOXY = "insufficient_epoxy"
    EPOXY_OVERFLOW = "epoxy_overflow"
    # -- Cosmetic (4) --
    DYEING_CONTAMINATION = "dyeing_contamination"
    NON_ELECTRICAL_CONTAMINATION = "non_electrical_contamination"
    NO_CODE = "no_code"
    CODE_BLUR = "code_blur"
    # -- Assembly (6) --
    PIN_BENT = "pin_bent"
    PIN_OXIDIZED = "pin_oxidized"
    PIN_BUR = "pin_bur"
    PIN_MIS_CUT = "pin_mis_cut"
    EPOXY_HIGHER_THAN_CERAMIC = "epoxy_higher_than_ceramic"
    MISALIGNMENT = "misalignment"
    # -- Inner staining residue (lighting-check sub-checks) --
    STAINING_ON_EDGE = "staining_on_edge"
    YELLOW_GLASS_CEMENT = "yellow_glass_cement"

    # -- deprecated aliases (pre-V1.4 names) --
    F_CRACK = "crack"
    F_BROKEN = "broken"
    F_EPOXY_EXPOSAL = "epoxy_exposal"
    F_INSUFFICIENT_EPOXY = "insufficient_epoxy"
    F_EPOXY_OVERFLOW = "epoxy_overflow"
    F_PIN_BENT = "pin_bent"
    F_PIN_OXIDIZED = "pin_oxidized"
    F_PIN_MIS_CUT = "pin_mis_cut"
    C_DYEING_CONTAMINATION = "dyeing_contamination"
    C_NON_ELECTRICAL_CONTAMINATION = "non_electrical_contamination"
    C_STAINING = "staining_on_edge"
    C_CODE_BLUR = "code_blur"
    A_NO_CODE = "no_code"
    A_MISALIGNMENT = "misalignment"
    A_PIN_BUR = "pin_bur"
    A_GOLD_EXPOSAL = "gold_exposal"
    A_LIGHT_LEAKAGE = "lighting_check"
    A_YELLOW_GLASS_CEMENT = "yellow_glass_cement"
    A_EDGE_STAINING = "staining_on_edge"


DEFECT_GROUP_MAP: Dict[DefectType, DefectGroup] = {
    **{t: DefectGroup.FUNCTION for t in (
        DefectType.LIGHTING_CHECK, DefectType.CRACK, DefectType.BROKEN, DefectType.EPOXY_EXPOSAL,
        DefectType.PIN_MISSING, DefectType.ELECTRICAL_CONTAMINATION, DefectType.GOLD_EXPOSAL,
        DefectType.INSUFFICIENT_EPOXY, DefectType.EPOXY_OVERFLOW)},
    **{t: DefectGroup.COSMETIC for t in (
        DefectType.DYEING_CONTAMINATION, DefectType.NON_ELECTRICAL_CONTAMINATION,
        DefectType.NO_CODE, DefectType.CODE_BLUR)},
    **{t: DefectGroup.ASSEMBLY for t in (
        DefectType.PIN_BENT, DefectType.PIN_OXIDIZED, DefectType.PIN_BUR, DefectType.PIN_MIS_CUT,
        DefectType.EPOXY_HIGHER_THAN_CERAMIC, DefectType.MISALIGNMENT)},
    DefectType.STAINING_ON_EDGE: DefectGroup.INNER_RESIDUE,
    DefectType.YELLOW_GLASS_CEMENT: DefectGroup.INNER_RESIDUE,
}

CRITICAL_DEFECTS = frozenset({
    DefectType.LIGHTING_CHECK, DefectType.CRACK, DefectType.BROKEN, DefectType.MISALIGNMENT,
})

# Mapping from DefectType to its severity tier (see DefectSeverity)
DEFECT_SEVERITY_MAP: Dict[DefectType, DefectSeverity] = {
    t: (DefectSeverity.CRITICAL if t in CRITICAL_DEFECTS
        else DefectSeverity.COSMETIC if DEFECT_GROUP_MAP[t] in (DefectGroup.COSMETIC, DefectGroup.INNER_RESIDUE)
        else DefectSeverity.ASSEMBLY)
    for t in DefectType
}


def plc_defect_code(t: DefectType) -> int:
    """DEFECT_CODE written by the IPC into the PLC: 0 = OK, 1..21 = spec order."""
    return list(DefectType).index(t) + 1


# ---------------------------------------------------------------------------
# Inspection result dataclasses
# ---------------------------------------------------------------------------

@dataclass
class DefectDetail:
    """Single defect finding from one CCD analysis pass."""
    defect_type: DefectType
    severity: DefectSeverity
    confidence: float                       # 0.0 - 1.0
    camera_id: CameraID
    bounding_box: Optional[Tuple[int, int, int, int]] = None   # (x, y, w, h) in pixels
    description: str = ""


@dataclass
class InspectionResult:
    """Aggregated inspection result for a single CSE unit."""
    unit_id: str = field(default_factory=lambda: uuid.uuid4().hex[:12])
    timestamp: datetime = field(default_factory=datetime.now)
    is_pass: bool = True
    defects: List[DefectDetail] = field(default_factory=list)
    ccd_results: Dict[CameraID, bool] = field(default_factory=dict)
    ng_double_checked: bool = False
    ng_confirmed: bool = False
    orientation_angle: float = 0.0          # Degrees of rotation applied
    process_time_ms: float = 0.0

    @property
    def worst_severity(self) -> Optional[DefectSeverity]:
        """Return the most severe defect category found, or None if passed."""
        if not self.defects:
            return None
        priority = {DefectSeverity.CRITICAL: 0, DefectSeverity.ASSEMBLY: 1, DefectSeverity.COSMETIC: 2}
        return min((d.severity for d in self.defects), key=lambda s: priority[s])

    def add_defect(self, detail: DefectDetail) -> None:
        """Register a defect and mark unit as failed."""
        self.defects.append(detail)
        self.is_pass = False
        self.ccd_results[detail.camera_id] = False
