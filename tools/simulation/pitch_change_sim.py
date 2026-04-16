"""
Pitch-change mechanism controller for the TI CSE AOI system.

The pitch changer expands the spacing between CSE units from the compact
basket pitch (2.54 mm) to the wider inspection pitch (12.0 mm) required
for individual station processing.

Mechanical description:
  - **Purple platform**: E-cylinder driven expansion stage with vacuum
    hold pads. Receives 4 CSE units from the SCARA robot in compact
    spacing, then expands to inspection spacing.
  - **Blue holder**: Positioning fixture below the platform. In the 1st
    case (bottom-side up), the holder flips the unit 180 degrees; in the
    2nd case (top-side up), it simply holds the unit for transfer pickup.

Author: Rongxuan Zhou
"""

from __future__ import annotations

import logging
from dataclasses import dataclass, field
from enum import Enum, auto
from typing import List

from src.global_variables.system_config import (
    PITCH_COMPACT_SPACING_MM,
    PITCH_INSPECTION_SPACING_MM,
    UNITS_PER_PICK_CYCLE,
)

logger = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# Types
# ---------------------------------------------------------------------------

class PitchState(Enum):
    """State of the pitch-change mechanism."""
    COMPACT = auto()          # Units at basket spacing
    EXPANDING = auto()        # E-cylinder moving
    INSPECTION_PITCH = auto() # Units at inspection spacing
    CONTRACTING = auto()      # Returning to compact
    ERROR = auto()


class HolderMode(Enum):
    """Blue holder operation mode."""
    FLIP_180 = auto()   # First case: flip unit upside-down
    HOLD_ONLY = auto()  # Second case: no flip, position only


@dataclass
class SlotStatus:
    """Status of a single CSE slot on the platform."""
    index: int
    occupied: bool = False
    vacuum_on: bool = False
    flipped: bool = False
    position_mm: float = 0.0


# ---------------------------------------------------------------------------
# Controller
# ---------------------------------------------------------------------------

class PitchChanger:
    """
    Controls the e-cylinder expansion platform and blue positioning holder.

    Typical cycle::

        pc = PitchChanger()
        pc.receive_units(count=4)        # Robot places 4 CSE at compact pitch
        pc.expand()                       # E-cylinder expands to inspection pitch
        pc.release_unit(slot=0)           # Transfer picks unit from slot 0
        ...
        pc.contract()                     # Return to compact for next batch
    """

    def __init__(
        self,
        num_slots: int = UNITS_PER_PICK_CYCLE,
        holder_mode: HolderMode = HolderMode.FLIP_180,
    ) -> None:
        self._num_slots = num_slots
        self._holder_mode = holder_mode
        self._state = PitchState.COMPACT
        self._slots: List[SlotStatus] = [
            SlotStatus(index=i, position_mm=i * PITCH_COMPACT_SPACING_MM)
            for i in range(num_slots)
        ]

    # -- unit loading --------------------------------------------------------

    def receive_units(self, count: int) -> int:
        """
        Mark *count* slots as occupied after the robot places CSE units.

        Returns the number of slots actually loaded (capped at capacity).
        """
        loaded = 0
        for slot in self._slots:
            if loaded >= count:
                break
            if not slot.occupied:
                slot.occupied = True
                slot.vacuum_on = True
                loaded += 1
        logger.info("Pitch changer received %d units (mode=%s).", loaded, self._holder_mode.name)
        return loaded

    # -- expansion / contraction ---------------------------------------------

    def expand(self) -> bool:
        """Actuate the e-cylinder to expand spacing to inspection pitch."""
        if self._state != PitchState.COMPACT:
            logger.warning("Cannot expand: current state is %s.", self._state.name)
            return False
        self._state = PitchState.EXPANDING

        for slot in self._slots:
            slot.position_mm = slot.index * PITCH_INSPECTION_SPACING_MM

        # Apply flip if in FLIP_180 mode
        if self._holder_mode == HolderMode.FLIP_180:
            for slot in self._slots:
                if slot.occupied:
                    slot.flipped = True

        self._state = PitchState.INSPECTION_PITCH
        logger.info("Pitch expanded to %.1f mm spacing.", PITCH_INSPECTION_SPACING_MM)
        return True

    def contract(self) -> bool:
        """Return platform to compact spacing for the next batch."""
        if self._state != PitchState.INSPECTION_PITCH:
            logger.warning("Cannot contract: current state is %s.", self._state.name)
            return False
        self._state = PitchState.CONTRACTING

        for slot in self._slots:
            slot.position_mm = slot.index * PITCH_COMPACT_SPACING_MM
            slot.flipped = False

        self._state = PitchState.COMPACT
        logger.info("Pitch contracted to %.2f mm spacing.", PITCH_COMPACT_SPACING_MM)
        return True

    # -- per-slot release ----------------------------------------------------

    def release_unit(self, slot_index: int) -> bool:
        """Release vacuum on a single slot so the transfer gripper can pick it."""
        if slot_index < 0 or slot_index >= self._num_slots:
            return False
        slot = self._slots[slot_index]
        if not slot.occupied:
            logger.warning("Slot %d is already empty.", slot_index)
            return False
        slot.vacuum_on = False
        slot.occupied = False
        logger.debug("Slot %d released.", slot_index)
        return True

    # -- status --------------------------------------------------------------

    @property
    def state(self) -> PitchState:
        return self._state

    @property
    def holder_mode(self) -> HolderMode:
        return self._holder_mode

    @holder_mode.setter
    def holder_mode(self, mode: HolderMode) -> None:
        self._holder_mode = mode

    def occupied_count(self) -> int:
        """Number of slots currently holding a CSE unit."""
        return sum(1 for s in self._slots if s.occupied)

    def get_slot(self, index: int) -> SlotStatus:
        return self._slots[index]

    def all_released(self) -> bool:
        """True when every slot has been picked up by the transfer system."""
        return all(not s.occupied for s in self._slots)
