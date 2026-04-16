"""
NG sorting and double-check management for the TI CSE AOI system.

When any CCD flags a unit as NG (No Good), the unit is routed to the
NG Check CCD for reconfirmation before final disposition. This prevents
false rejects and maintains yield.

Physical flow:
  1. NG-flagged unit transferred to NG Check CCD station.
  2. NG Check CCD re-inspects the unit with the same defect algorithm.
  3. If confirmed NG: placed on NG conveyor belt, holder bar stops at
     the designated position, NG transfer moves unit to NG tray.
  4. If overturned (false positive): returned to good-output path.

Tracks per-defect-category NG statistics for SPC and yield reporting.

Author: Rongxuan Zhou
"""

from __future__ import annotations

import logging
from collections import defaultdict
from dataclasses import dataclass, field
from enum import Enum, auto
from typing import Dict, List, Optional

from src.data_types.defect_types import (
    CameraID,
    DefectDetail,
    DefectSeverity,
    DefectType,
    InspectionResult,
)
from src.global_variables.system_config import NG_TRAY_CAPACITY, THRESHOLDS

logger = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# Types
# ---------------------------------------------------------------------------

class NGDisposition(Enum):
    """Final disposition after double-check."""
    CONFIRMED_NG = auto()
    FALSE_POSITIVE = auto()
    PENDING = auto()


@dataclass
class NGRecord:
    """Tracking record for one NG-flagged unit."""
    unit_id: str
    original_result: InspectionResult
    flagging_cameras: List[CameraID] = field(default_factory=list)
    primary_defect: Optional[DefectType] = None
    double_check_confidence: float = 0.0
    disposition: NGDisposition = NGDisposition.PENDING
    tray_slot: Optional[int] = None


@dataclass
class NGTray:
    """Physical NG tray with fixed capacity."""
    tray_id: int
    capacity: int = NG_TRAY_CAPACITY
    records: List[NGRecord] = field(default_factory=list)

    @property
    def is_full(self) -> bool:
        return len(self.records) >= self.capacity

    @property
    def count(self) -> int:
        return len(self.records)


# ---------------------------------------------------------------------------
# NG sorter
# ---------------------------------------------------------------------------

class NGSorter:
    """
    NG sorting controller with double-check verification.

    Maintains a queue of NG-flagged units, executes the reconfirmation
    sequence, and tracks statistics by defect category.
    """

    def __init__(self) -> None:
        self._pending_queue: List[NGRecord] = []
        self._trays: List[NGTray] = [NGTray(tray_id=0)]
        self._stats: Dict[DefectType, int] = defaultdict(int)
        self._false_positive_count: int = 0
        self._total_checked: int = 0

    # -- intake --------------------------------------------------------------

    def flag_ng(self, result: InspectionResult) -> NGRecord:
        """
        Register an NG-flagged unit for double-check processing.

        Extracts the flagging cameras and primary defect from the
        inspection result and queues the unit.
        """
        flagging_cams = [
            cam for cam, passed in result.ccd_results.items() if not passed
        ]
        primary = result.defects[0].defect_type if result.defects else None

        record = NGRecord(
            unit_id=result.unit_id,
            original_result=result,
            flagging_cameras=flagging_cams,
            primary_defect=primary,
        )
        self._pending_queue.append(record)
        logger.info("Unit %s flagged NG (primary=%s, cameras=%s).",
                     result.unit_id,
                     primary.value if primary else "unknown",
                     [c.name for c in flagging_cams])
        return record

    # -- double-check --------------------------------------------------------

    def perform_double_check(
        self,
        record: NGRecord,
        recheck_confidence: float,
    ) -> NGDisposition:
        """
        Execute the NG double-check with the NG Check CCD result.

        Args:
            record: The pending NG record.
            recheck_confidence: Confidence score from the NG Check CCD
                                re-inspection (0.0 = likely good, 1.0 = confirmed NG).

        Returns:
            Final disposition: CONFIRMED_NG or FALSE_POSITIVE.
        """
        self._total_checked += 1
        record.double_check_confidence = recheck_confidence

        if recheck_confidence >= THRESHOLDS.ng_double_check_confirm:
            record.disposition = NGDisposition.CONFIRMED_NG
            self._route_to_tray(record)
            if record.primary_defect is not None:
                self._stats[record.primary_defect] += 1
            logger.info("Unit %s confirmed NG (conf=%.2f).", record.unit_id, recheck_confidence)
        else:
            record.disposition = NGDisposition.FALSE_POSITIVE
            self._false_positive_count += 1
            logger.info("Unit %s overturned -- false positive (conf=%.2f).",
                         record.unit_id, recheck_confidence)

        # Remove from pending queue
        if record in self._pending_queue:
            self._pending_queue.remove(record)

        return record.disposition

    # -- tray management -----------------------------------------------------

    def _route_to_tray(self, record: NGRecord) -> None:
        """Place a confirmed-NG unit into the current NG tray."""
        current_tray = self._trays[-1]
        if current_tray.is_full:
            new_tray = NGTray(tray_id=current_tray.tray_id + 1)
            self._trays.append(new_tray)
            current_tray = new_tray
            logger.info("NG tray %d full; switched to tray %d.",
                         current_tray.tray_id - 1, current_tray.tray_id)
        record.tray_slot = current_tray.count
        current_tray.records.append(record)

    # -- statistics ----------------------------------------------------------

    def get_ng_stats(self) -> Dict[DefectType, int]:
        """Return NG counts by defect type."""
        return dict(self._stats)

    def get_ng_rate_by_severity(self) -> Dict[DefectSeverity, int]:
        """Aggregate NG counts by severity tier."""
        from src.data_types.defect_types import DEFECT_SEVERITY_MAP
        severity_counts: Dict[DefectSeverity, int] = defaultdict(int)
        for defect_type, count in self._stats.items():
            sev = DEFECT_SEVERITY_MAP.get(defect_type, DefectSeverity.ASSEMBLY)
            severity_counts[sev] += count
        return dict(severity_counts)

    @property
    def false_positive_rate(self) -> float:
        """Fraction of double-checked units that were overturned."""
        if self._total_checked == 0:
            return 0.0
        return self._false_positive_count / self._total_checked

    @property
    def pending_count(self) -> int:
        return len(self._pending_queue)

    @property
    def total_confirmed_ng(self) -> int:
        return sum(t.count for t in self._trays)

    @property
    def current_tray(self) -> NGTray:
        return self._trays[-1]
