"""
Data type definitions for the TI CSE AOI inspection system.

Defines all enumerations and dataclasses used across inspection, vision,
material handling, and NG management subsystems. The 19 defect categories
are organized into three severity tiers: Function (critical), Cosmetic,
and Assembly.

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
    CRITICAL = "critical"       # Function defects -- unit is non-functional
    COSMETIC = "cosmetic"       # Visual defects -- unit functions but fails appearance spec
    ASSEMBLY = "assembly"       # Assembly process defects -- rework or scrap


class DefectType(Enum):
    """
    19 defect categories detected by the AOI system.

    Naming convention: <SEVERITY_PREFIX>_<DEFECT_NAME>
    F_ = Function (critical), C_ = Cosmetic, A_ = Assembly
    """
    # -- Function defects (8) -- detected on CCD1/CCD3 --
    F_CRACK = "crack"
    F_BROKEN = "broken"
    F_EPOXY_EXPOSAL = "epoxy_exposal"
    F_INSUFFICIENT_EPOXY = "insufficient_epoxy"
    F_EPOXY_OVERFLOW = "epoxy_overflow"
    F_PIN_BENT = "pin_bent"
    F_PIN_OXIDIZED = "pin_oxidized"
    F_PIN_MIS_CUT = "pin_mis_cut"

    # -- Cosmetic defects (4) --
    C_DYEING_CONTAMINATION = "dyeing_contamination"
    C_NON_ELECTRICAL_CONTAMINATION = "non_electrical_contamination"
    C_STAINING = "staining"
    C_CODE_BLUR = "code_blur"

    # -- Assembly defects (5+) --
    A_NO_CODE = "no_code"
    A_MISALIGNMENT = "misalignment"
    A_PIN_BUR = "pin_bur"
    A_GOLD_EXPOSAL = "gold_exposal"
    A_LIGHT_LEAKAGE = "light_leakage"
    A_YELLOW_GLASS_CEMENT = "yellow_glass_cement"
    A_EDGE_STAINING = "edge_staining"


# Mapping from DefectType to its severity tier
DEFECT_SEVERITY_MAP: Dict[DefectType, DefectSeverity] = {
    DefectType.F_CRACK: DefectSeverity.CRITICAL,
    DefectType.F_BROKEN: DefectSeverity.CRITICAL,
    DefectType.F_EPOXY_EXPOSAL: DefectSeverity.CRITICAL,
    DefectType.F_INSUFFICIENT_EPOXY: DefectSeverity.CRITICAL,
    DefectType.F_EPOXY_OVERFLOW: DefectSeverity.CRITICAL,
    DefectType.F_PIN_BENT: DefectSeverity.CRITICAL,
    DefectType.F_PIN_OXIDIZED: DefectSeverity.CRITICAL,
    DefectType.F_PIN_MIS_CUT: DefectSeverity.CRITICAL,
    DefectType.C_DYEING_CONTAMINATION: DefectSeverity.COSMETIC,
    DefectType.C_NON_ELECTRICAL_CONTAMINATION: DefectSeverity.COSMETIC,
    DefectType.C_STAINING: DefectSeverity.COSMETIC,
    DefectType.C_CODE_BLUR: DefectSeverity.COSMETIC,
    DefectType.A_NO_CODE: DefectSeverity.ASSEMBLY,
    DefectType.A_MISALIGNMENT: DefectSeverity.ASSEMBLY,
    DefectType.A_PIN_BUR: DefectSeverity.ASSEMBLY,
    DefectType.A_GOLD_EXPOSAL: DefectSeverity.ASSEMBLY,
    DefectType.A_LIGHT_LEAKAGE: DefectSeverity.ASSEMBLY,
    DefectType.A_YELLOW_GLASS_CEMENT: DefectSeverity.ASSEMBLY,
    DefectType.A_EDGE_STAINING: DefectSeverity.ASSEMBLY,
}


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
