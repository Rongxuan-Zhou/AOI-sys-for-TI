"""
Epson SCARA robot interface for the TI CSE AOI system.

Controls an Epson SCARA robot equipped with dual vacuum nozzles:
  - Nozzle 1: bottom-side suction (picks CSE from basket underside)
  - Nozzle 2: top-side suction (picks CSE from top for flip operations)

The robot handles 4 CSE units per pick cycle and can perform 90-degree
in-hand rotation for orientation correction. Communication with the
Epson RC+ controller uses a TCP/IP socket protocol.

Motion sequences:
  1. Pick 4 units from basket using nozzle 1.
  2. Rotate 90 degrees if orientation correction is needed.
  3. Place units onto the pitch-change platform.

Author: Rongxuan Zhou
"""

from __future__ import annotations

import logging
import socket
import struct
import time
from dataclasses import dataclass, field
from enum import Enum, auto
from typing import Dict, List, Optional, Tuple

from src.global_variables.system_config import (
    ROBOT_TCP_IP,
    ROBOT_TCP_PORT,
    ROBOT_VACUUM_PRESSURE_KPA,
    ROBOT_ROTATION_SPEED_DEG_S,
    UNITS_PER_PICK_CYCLE,
)

logger = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# Types
# ---------------------------------------------------------------------------

class NozzleID(Enum):
    """Physical vacuum nozzle identifier."""
    NOZZLE_1_BOTTOM = 1
    NOZZLE_2_TOP = 2


class RobotState(Enum):
    """High-level state of the SCARA robot."""
    IDLE = auto()
    MOVING = auto()
    PICKING = auto()
    PLACING = auto()
    ROTATING = auto()
    ERROR = auto()
    DISCONNECTED = auto()


class MotionCommand(Enum):
    """Supported motion commands sent to the RC+ controller."""
    MOVE_TO = "MOVETO"
    PICK = "PICK"
    PLACE = "PLACE"
    ROTATE = "ROTATE"
    HOME = "HOME"
    VACUUM_ON = "VAC_ON"
    VACUUM_OFF = "VAC_OFF"
    STATUS = "STATUS"


@dataclass
class RobotPose:
    """Cartesian pose of the SCARA end-effector (mm, degrees)."""
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0
    r: float = 0.0   # rotation about Z


@dataclass
class NozzleState:
    """State of a single vacuum nozzle."""
    nozzle_id: NozzleID
    vacuum_on: bool = False
    pressure_kpa: float = 0.0
    has_part: bool = False


# ---------------------------------------------------------------------------
# TCP/IP communication layer (simulated)
# ---------------------------------------------------------------------------

class _EpsonComm:
    """
    TCP/IP communication helper for the Epson RC+ controller.

    Protocol: newline-terminated ASCII commands, ACK/NAK responses.
    In simulation mode, no actual socket is opened.
    """

    def __init__(self, ip: str, port: int, simulate: bool = True) -> None:
        self._ip = ip
        self._port = port
        self._simulate = simulate
        self._socket: Optional[socket.socket] = None

    def connect(self) -> bool:
        if self._simulate:
            logger.info("Simulated connection to Epson RC+ at %s:%d.", self._ip, self._port)
            return True
        try:
            self._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._socket.settimeout(5.0)
            self._socket.connect((self._ip, self._port))
            logger.info("Connected to Epson RC+ at %s:%d.", self._ip, self._port)
            return True
        except OSError as exc:
            logger.error("Connection failed: %s", exc)
            return False

    def disconnect(self) -> None:
        if self._socket is not None:
            self._socket.close()
            self._socket = None
        logger.info("Disconnected from Epson RC+.")

    def send_command(self, cmd: MotionCommand, payload: str = "") -> str:
        """
        Send a command and return the controller response.

        Returns 'ACK' on success, 'NAK:<reason>' on failure.
        """
        message = f"{cmd.value} {payload}".strip() + "\n"
        if self._simulate:
            logger.debug("SIM TX: %s", message.strip())
            return "ACK"
        if self._socket is None:
            return "NAK:NOT_CONNECTED"
        try:
            self._socket.sendall(message.encode("ascii"))
            data = self._socket.recv(1024)
            return data.decode("ascii").strip()
        except OSError as exc:
            logger.error("Communication error: %s", exc)
            return f"NAK:{exc}"


# ---------------------------------------------------------------------------
# Robot controller
# ---------------------------------------------------------------------------

class RobotController:
    """
    High-level interface for the Epson SCARA robot on the AOI line.

    Manages dual-nozzle vacuum control, multi-unit pick/place cycles,
    and orientation rotation. All motion commands are dispatched through
    the ``_EpsonComm`` TCP layer.
    """

    def __init__(self, simulate: bool = True) -> None:
        self._comm = _EpsonComm(ROBOT_TCP_IP, ROBOT_TCP_PORT, simulate=simulate)
        self._state = RobotState.DISCONNECTED
        self._pose = RobotPose()
        self._nozzles: Dict[NozzleID, NozzleState] = {
            NozzleID.NOZZLE_1_BOTTOM: NozzleState(NozzleID.NOZZLE_1_BOTTOM),
            NozzleID.NOZZLE_2_TOP: NozzleState(NozzleID.NOZZLE_2_TOP),
        }
        self._units_held: int = 0

    # -- lifecycle -----------------------------------------------------------

    def connect(self) -> bool:
        """Establish communication with the Epson RC+ controller."""
        if self._comm.connect():
            self._state = RobotState.IDLE
            return True
        self._state = RobotState.ERROR
        return False

    def disconnect(self) -> None:
        self._comm.disconnect()
        self._state = RobotState.DISCONNECTED

    def home(self) -> bool:
        """Move robot to the home/safe position."""
        resp = self._comm.send_command(MotionCommand.HOME)
        if resp == "ACK":
            self._pose = RobotPose()
            self._state = RobotState.IDLE
            return True
        self._state = RobotState.ERROR
        return False

    # -- vacuum control ------------------------------------------------------

    def vacuum_on(self, nozzle: NozzleID) -> bool:
        resp = self._comm.send_command(MotionCommand.VACUUM_ON, str(nozzle.value))
        if resp == "ACK":
            ns = self._nozzles[nozzle]
            ns.vacuum_on = True
            ns.pressure_kpa = ROBOT_VACUUM_PRESSURE_KPA
            return True
        return False

    def vacuum_off(self, nozzle: NozzleID) -> bool:
        resp = self._comm.send_command(MotionCommand.VACUUM_OFF, str(nozzle.value))
        if resp == "ACK":
            ns = self._nozzles[nozzle]
            ns.vacuum_on = False
            ns.pressure_kpa = 0.0
            ns.has_part = False
            return True
        return False

    # -- motion sequences ----------------------------------------------------

    def pick_from_basket(
        self,
        basket_positions: List[RobotPose],
        nozzle: NozzleID = NozzleID.NOZZLE_1_BOTTOM,
    ) -> int:
        """
        Pick up to ``UNITS_PER_PICK_CYCLE`` CSE units from the basket.

        Args:
            basket_positions: List of poses for each unit in the basket.
            nozzle: Which nozzle to use for suction.

        Returns:
            Number of units successfully picked.
        """
        self._state = RobotState.PICKING
        picked = 0
        self.vacuum_on(nozzle)

        for pose in basket_positions[:UNITS_PER_PICK_CYCLE]:
            resp = self._comm.send_command(
                MotionCommand.MOVE_TO,
                f"{pose.x:.2f},{pose.y:.2f},{pose.z:.2f},{pose.r:.2f}",
            )
            if resp != "ACK":
                logger.warning("Move to basket position failed.")
                continue
            resp = self._comm.send_command(MotionCommand.PICK)
            if resp == "ACK":
                picked += 1
                self._nozzles[nozzle].has_part = True

        self._units_held = picked
        self._pose = basket_positions[0] if basket_positions else self._pose
        self._state = RobotState.IDLE
        logger.info("Picked %d / %d units from basket.", picked, len(basket_positions))
        return picked

    def rotate_for_orientation(self, angle_deg: float) -> bool:
        """
        Rotate the end-effector (in-hand rotation) by the specified angle.

        Typically 90 degrees for orientation correction after the
        pre-load orientation check flags a needed adjustment.
        """
        self._state = RobotState.ROTATING
        resp = self._comm.send_command(MotionCommand.ROTATE, f"{angle_deg:.1f}")
        if resp == "ACK":
            self._pose.r = (self._pose.r + angle_deg) % 360.0
            self._state = RobotState.IDLE
            return True
        self._state = RobotState.ERROR
        return False

    def place_on_pitch_changer(
        self,
        platform_positions: List[RobotPose],
        nozzle: NozzleID = NozzleID.NOZZLE_1_BOTTOM,
    ) -> int:
        """
        Place held CSE units onto the pitch-change platform.

        Returns the number of units successfully placed.
        """
        self._state = RobotState.PLACING
        placed = 0

        for pose in platform_positions[:self._units_held]:
            resp = self._comm.send_command(
                MotionCommand.MOVE_TO,
                f"{pose.x:.2f},{pose.y:.2f},{pose.z:.2f},{pose.r:.2f}",
            )
            if resp != "ACK":
                continue
            resp = self._comm.send_command(MotionCommand.PLACE)
            if resp == "ACK":
                placed += 1

        self.vacuum_off(nozzle)
        self._units_held -= placed
        self._state = RobotState.IDLE
        logger.info("Placed %d units on pitch-change platform.", placed)
        return placed

    # -- status --------------------------------------------------------------

    @property
    def state(self) -> RobotState:
        return self._state

    @property
    def pose(self) -> RobotPose:
        return self._pose

    @property
    def units_held(self) -> int:
        return self._units_held

    def get_nozzle_state(self, nozzle: NozzleID) -> NozzleState:
        return self._nozzles[nozzle]
