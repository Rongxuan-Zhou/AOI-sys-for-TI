(* ==========================================================================
 * Program:     SERVO_CONFIG
 * Description: 8-Axis Servo Drive Configuration and Homing Sequences
 * Drives:      Mitsubishi MR-J5-G series via CC-Link IE Field
 * Network:     CC-Link IE Field 1 Gbps, 0.5 ms communication cycle
 * Motion:      Position mode (CP), electronic gear via drive parameters
 * ========================================================================== *)

PROGRAM SERVO_CONFIG

(* ---- Axis Configuration Type ------------------------------------------- *)
TYPE ST_AXIS_CONFIG : STRUCT
    AXIS_NAME        : STRING[32];
    MOTION_TYPE      : USINT;        (* 0=Linear [mm], 1=Rotary [deg] *)
    GEAR_NUM         : UDINT;        (* Electronic gear numerator *)
    GEAR_DEN         : UDINT;        (* Electronic gear denominator *)
    ENCODER_PPR      : UDINT;        (* Encoder pulses per revolution *)
    BALL_SCREW_LEAD  : LREAL;        (* [mm/rev], 0 for direct-drive rotary *)
    MAX_VELOCITY     : LREAL;        (* [mm/s] or [deg/s] *)
    MAX_ACCEL        : LREAL;        (* [mm/s^2] or [deg/s^2] *)
    MAX_DECEL        : LREAL;
    JOG_VELOCITY     : LREAL;        (* Manual jog speed *)
    HOME_VELOCITY    : LREAL;        (* Homing approach speed *)
    HOME_CREEP       : LREAL;        (* Slow final approach to home sensor *)
    HOME_OFFSET      : LREAL;        (* Distance from home sensor to datum *)
    HOME_DIRECTION   : BOOL;         (* FALSE=negative, TRUE=positive *)
    SW_LIMIT_POS     : LREAL;        (* Software positive limit *)
    SW_LIMIT_NEG     : LREAL;        (* Software negative limit *)
    IN_POS_BAND      : LREAL;        (* In-position tolerance band *)
    TORQUE_LIMIT     : LREAL;        (* [%] of rated torque *)
END_STRUCT;
END_TYPE

TYPE ST_AXIS_STATUS : STRUCT
    ACTUAL_POS       : LREAL;
    ACTUAL_VEL       : LREAL;
    ACTUAL_TORQUE    : LREAL;        (* [%] of rated *)
    IS_HOMED         : BOOL;
    IS_ENABLED       : BOOL;
    IS_MOVING        : BOOL;
    IS_IN_POSITION   : BOOL;
    ALARM_CODE       : WORD;
    ALARM_HISTORY    : ARRAY[0..7] OF WORD;  (* Last 8 alarms for diagnostics *)
END_STRUCT;
END_TYPE

(* ---- Homing State Machine ---------------------------------------------- *)
TYPE E_HOME_STATE : (
    HOME_IDLE        := 0,
    HOME_SEEK_SWITCH := 1,       (* Fast approach toward home sensor *)
    HOME_BACK_OFF    := 2,       (* Reverse off sensor at creep speed *)
    HOME_FINAL_APP   := 3,       (* Final slow approach to sensor edge *)
    HOME_SET_ORIGIN  := 4,       (* Capture position, apply offset *)
    HOME_COMPLETE    := 5,
    HOME_ERROR       := 99
);
END_TYPE

(* ---- Variables --------------------------------------------------------- *)
VAR
    AXIS_CFG         : ARRAY[1..8] OF ST_AXIS_CONFIG;
    AXIS_STAT        : ARRAY[1..8] OF ST_AXIS_STATUS;
    HOME_STATE       : ARRAY[1..8] OF E_HOME_STATE;
    HOME_REQUEST     : BOOL;           (* Triggers all-axis homing sequence *)
    HOME_AXIS_INDEX  : USINT := 1;     (* Sequential homing: one axis at a time *)
    ALL_AXES_HOMED   : BOOL;
    HOME_TIMER       : TON;
    RT_HOME_REQ      : R_TRIG;
    i                : INT;
END_VAR

VAR CONSTANT
    T_HOME_TIMEOUT   : TIME := T#30s;  (* Per-axis homing timeout *)
END_VAR

(* ======================================================================== *)
(* AXIS PARAMETER INITIALIZATION                                            *)
(* Run once on first scan or after parameter download from HMI              *)
(* Electronic gear: (GEAR_NUM / GEAR_DEN) * ENCODER_PPR = pulses per mm     *)
(* ======================================================================== *)

(* --- Axis 1: Transfer 1 (basket -> CSE loader) --- *)
(* Mitsubishi HK-KT23W, 200W, ball screw driven linear slide *)
AXIS_CFG[1].AXIS_NAME       := 'TRANSFER_1';
AXIS_CFG[1].MOTION_TYPE     := 0;          (* Linear *)
AXIS_CFG[1].GEAR_NUM        := 1;
AXIS_CFG[1].GEAR_DEN        := 1;
AXIS_CFG[1].ENCODER_PPR     := 4194304;    (* 22-bit absolute encoder *)
AXIS_CFG[1].BALL_SCREW_LEAD := 10.0;       (* 10 mm/rev C5 precision *)
AXIS_CFG[1].MAX_VELOCITY    := 1000.0;     (* mm/s *)
AXIS_CFG[1].MAX_ACCEL       := 10000.0;    (* mm/s^2, ~1G *)
AXIS_CFG[1].MAX_DECEL       := 10000.0;
AXIS_CFG[1].JOG_VELOCITY    := 50.0;
AXIS_CFG[1].HOME_VELOCITY   := 100.0;
AXIS_CFG[1].HOME_CREEP      := 5.0;
AXIS_CFG[1].HOME_OFFSET     := 2.0;
AXIS_CFG[1].HOME_DIRECTION  := FALSE;      (* Home toward negative limit *)
AXIS_CFG[1].SW_LIMIT_POS    := 200.0;
AXIS_CFG[1].SW_LIMIT_NEG    := -1.0;
AXIS_CFG[1].IN_POS_BAND     := 0.005;      (* 5 um *)
AXIS_CFG[1].TORQUE_LIMIT    := 100.0;

(* --- Axis 2: Transfer 2 (CSE -> pitch change) --- *)
AXIS_CFG[2].AXIS_NAME       := 'TRANSFER_2';
AXIS_CFG[2].MOTION_TYPE     := 0;
AXIS_CFG[2].GEAR_NUM        := 1;
AXIS_CFG[2].GEAR_DEN        := 1;
AXIS_CFG[2].ENCODER_PPR     := 4194304;
AXIS_CFG[2].BALL_SCREW_LEAD := 10.0;
AXIS_CFG[2].MAX_VELOCITY    := 1000.0;
AXIS_CFG[2].MAX_ACCEL       := 10000.0;
AXIS_CFG[2].MAX_DECEL       := 10000.0;
AXIS_CFG[2].JOG_VELOCITY    := 50.0;
AXIS_CFG[2].HOME_VELOCITY   := 100.0;
AXIS_CFG[2].HOME_CREEP      := 5.0;
AXIS_CFG[2].HOME_OFFSET     := 2.0;
AXIS_CFG[2].HOME_DIRECTION  := FALSE;
AXIS_CFG[2].SW_LIMIT_POS    := 250.0;
AXIS_CFG[2].SW_LIMIT_NEG    := -1.0;
AXIS_CFG[2].IN_POS_BAND     := 0.005;
AXIS_CFG[2].TORQUE_LIMIT    := 100.0;

(* --- Axis 3: Transfer 3 (pitch change -> CCD4) --- *)
AXIS_CFG[3].AXIS_NAME       := 'TRANSFER_3';
AXIS_CFG[3].MOTION_TYPE     := 0;
AXIS_CFG[3].GEAR_NUM        := 1;
AXIS_CFG[3].GEAR_DEN        := 1;
AXIS_CFG[3].ENCODER_PPR     := 4194304;
AXIS_CFG[3].BALL_SCREW_LEAD := 10.0;
AXIS_CFG[3].MAX_VELOCITY    := 1200.0;     (* Faster: longer travel *)
AXIS_CFG[3].MAX_ACCEL       := 12000.0;
AXIS_CFG[3].MAX_DECEL       := 12000.0;
AXIS_CFG[3].JOG_VELOCITY    := 50.0;
AXIS_CFG[3].HOME_VELOCITY   := 100.0;
AXIS_CFG[3].HOME_CREEP      := 5.0;
AXIS_CFG[3].HOME_OFFSET     := 2.0;
AXIS_CFG[3].HOME_DIRECTION  := FALSE;
AXIS_CFG[3].SW_LIMIT_POS    := 500.0;
AXIS_CFG[3].SW_LIMIT_NEG    := -1.0;
AXIS_CFG[3].IN_POS_BAND     := 0.005;
AXIS_CFG[3].TORQUE_LIMIT    := 100.0;

(* --- Axis 4: Transfer 4 (CCD4 -> CCD3) --- *)
AXIS_CFG[4].AXIS_NAME       := 'TRANSFER_4';
AXIS_CFG[4].MOTION_TYPE     := 0;
AXIS_CFG[4].GEAR_NUM        := 1;
AXIS_CFG[4].GEAR_DEN        := 1;
AXIS_CFG[4].ENCODER_PPR     := 4194304;
AXIS_CFG[4].BALL_SCREW_LEAD := 10.0;
AXIS_CFG[4].MAX_VELOCITY    := 1200.0;
AXIS_CFG[4].MAX_ACCEL       := 12000.0;
AXIS_CFG[4].MAX_DECEL       := 12000.0;
AXIS_CFG[4].JOG_VELOCITY    := 50.0;
AXIS_CFG[4].HOME_VELOCITY   := 100.0;
AXIS_CFG[4].HOME_CREEP      := 5.0;
AXIS_CFG[4].HOME_OFFSET     := 2.0;
AXIS_CFG[4].HOME_DIRECTION  := FALSE;
AXIS_CFG[4].SW_LIMIT_POS    := 500.0;
AXIS_CFG[4].SW_LIMIT_NEG    := -1.0;
AXIS_CFG[4].IN_POS_BAND     := 0.005;
AXIS_CFG[4].TORQUE_LIMIT    := 100.0;

(* --- Axis 5: Transfer 5 (CCD3 -> CCD1/orientation) --- *)
AXIS_CFG[5].AXIS_NAME       := 'TRANSFER_5';
AXIS_CFG[5].MOTION_TYPE     := 0;
AXIS_CFG[5].GEAR_NUM        := 1;
AXIS_CFG[5].GEAR_DEN        := 1;
AXIS_CFG[5].ENCODER_PPR     := 4194304;
AXIS_CFG[5].BALL_SCREW_LEAD := 10.0;
AXIS_CFG[5].MAX_VELOCITY    := 800.0;      (* Slower: precision placement *)
AXIS_CFG[5].MAX_ACCEL       := 8000.0;
AXIS_CFG[5].MAX_DECEL       := 8000.0;
AXIS_CFG[5].JOG_VELOCITY    := 30.0;
AXIS_CFG[5].HOME_VELOCITY   := 80.0;
AXIS_CFG[5].HOME_CREEP      := 3.0;
AXIS_CFG[5].HOME_OFFSET     := 2.0;
AXIS_CFG[5].HOME_DIRECTION  := FALSE;
AXIS_CFG[5].SW_LIMIT_POS    := 800.0;
AXIS_CFG[5].SW_LIMIT_NEG    := -1.0;
AXIS_CFG[5].IN_POS_BAND     := 0.003;      (* Tighter: orientation station *)
AXIS_CFG[5].TORQUE_LIMIT    := 80.0;

(* --- Axis 6: Transfer 6 (CCD2 -> unload) --- *)
AXIS_CFG[6].AXIS_NAME       := 'TRANSFER_6';
AXIS_CFG[6].MOTION_TYPE     := 0;
AXIS_CFG[6].GEAR_NUM        := 1;
AXIS_CFG[6].GEAR_DEN        := 1;
AXIS_CFG[6].ENCODER_PPR     := 4194304;
AXIS_CFG[6].BALL_SCREW_LEAD := 10.0;
AXIS_CFG[6].MAX_VELOCITY    := 1000.0;
AXIS_CFG[6].MAX_ACCEL       := 10000.0;
AXIS_CFG[6].MAX_DECEL       := 10000.0;
AXIS_CFG[6].JOG_VELOCITY    := 50.0;
AXIS_CFG[6].HOME_VELOCITY   := 100.0;
AXIS_CFG[6].HOME_CREEP      := 5.0;
AXIS_CFG[6].HOME_OFFSET     := 2.0;
AXIS_CFG[6].HOME_DIRECTION  := FALSE;
AXIS_CFG[6].SW_LIMIT_POS    := 950.0;
AXIS_CFG[6].SW_LIMIT_NEG    := -1.0;
AXIS_CFG[6].IN_POS_BAND     := 0.005;
AXIS_CFG[6].TORQUE_LIMIT    := 100.0;

(* --- Axis 7: Orientation Compensation (rotary, direct-drive) --- *)
(* Mitsubishi HK-KT13, 100W, direct-drive rotary table *)
AXIS_CFG[7].AXIS_NAME       := 'ORIENT_COMP';
AXIS_CFG[7].MOTION_TYPE     := 1;          (* Rotary *)
AXIS_CFG[7].GEAR_NUM        := 1;
AXIS_CFG[7].GEAR_DEN        := 72;         (* 72:1 harmonic drive reduction *)
AXIS_CFG[7].ENCODER_PPR     := 4194304;
AXIS_CFG[7].BALL_SCREW_LEAD := 0.0;        (* N/A for rotary *)
AXIS_CFG[7].MAX_VELOCITY    := 360.0;      (* deg/s *)
AXIS_CFG[7].MAX_ACCEL       := 3600.0;     (* deg/s^2 *)
AXIS_CFG[7].MAX_DECEL       := 3600.0;
AXIS_CFG[7].JOG_VELOCITY    := 10.0;
AXIS_CFG[7].HOME_VELOCITY   := 30.0;
AXIS_CFG[7].HOME_CREEP      := 2.0;
AXIS_CFG[7].HOME_OFFSET     := 0.0;
AXIS_CFG[7].HOME_DIRECTION  := TRUE;       (* Home toward positive/CW *)
AXIS_CFG[7].SW_LIMIT_POS    := 180.0;      (* +/- 180 deg range *)
AXIS_CFG[7].SW_LIMIT_NEG    := -180.0;
AXIS_CFG[7].IN_POS_BAND     := 0.01;       (* 0.01 deg = 36 arcsec *)
AXIS_CFG[7].TORQUE_LIMIT    := 50.0;       (* Low inertia load *)

(* --- Axis 8: CCD3 Side-Check 360-deg Rotation --- *)
(* Mitsubishi HK-KT23W, 200W, direct-drive continuous rotation *)
AXIS_CFG[8].AXIS_NAME       := 'SIDE_CHECK_ROT';
AXIS_CFG[8].MOTION_TYPE     := 1;          (* Rotary *)
AXIS_CFG[8].GEAR_NUM        := 1;
AXIS_CFG[8].GEAR_DEN        := 50;         (* 50:1 worm gear for backlash-free *)
AXIS_CFG[8].ENCODER_PPR     := 4194304;
AXIS_CFG[8].BALL_SCREW_LEAD := 0.0;
AXIS_CFG[8].MAX_VELOCITY    := 1080.0;     (* deg/s - 3 rev/s max *)
AXIS_CFG[8].MAX_ACCEL       := 7200.0;
AXIS_CFG[8].MAX_DECEL       := 7200.0;
AXIS_CFG[8].JOG_VELOCITY    := 30.0;
AXIS_CFG[8].HOME_VELOCITY   := 60.0;
AXIS_CFG[8].HOME_CREEP      := 5.0;
AXIS_CFG[8].HOME_OFFSET     := 0.0;
AXIS_CFG[8].HOME_DIRECTION  := TRUE;
AXIS_CFG[8].SW_LIMIT_POS    := 99999.0;    (* No limit: continuous rotation *)
AXIS_CFG[8].SW_LIMIT_NEG    := -99999.0;
AXIS_CFG[8].IN_POS_BAND     := 0.05;       (* Relaxed: full-revolution scan *)
AXIS_CFG[8].TORQUE_LIMIT    := 80.0;

(* ======================================================================== *)
(* HOMING SEQUENCE STATE MACHINE                                            *)
(* Sequential homing: axes home one at a time to prevent collision.          *)
(* Order: 7 (orient) -> 8 (rotation) -> 1 -> 2 -> 3 -> 4 -> 5 -> 6        *)
(* Rotary axes home first as they have no collision risk with linear axes.  *)
(* ======================================================================== *)

VAR CONSTANT
    HOME_ORDER : ARRAY[1..8] OF USINT := [7, 8, 1, 2, 3, 4, 5, 6];
END_VAR

RT_HOME_REQ(CLK := HOME_REQUEST);
IF RT_HOME_REQ.Q THEN
    HOME_AXIS_INDEX := 1;
    FOR i := 1 TO 8 DO
        HOME_STATE[i] := HOME_IDLE;
        AXIS_STAT[i].IS_HOMED := FALSE;
    END_FOR;
END_IF;

(* Process current homing axis *)
IF HOME_AXIS_INDEX >= 1 AND HOME_AXIS_INDEX <= 8 THEN

    CASE HOME_STATE[HOME_ORDER[HOME_AXIS_INDEX]] OF

        HOME_IDLE:
            (* Initiate homing for current axis in sequence *)
            IF HOME_REQUEST THEN
                HOME_STATE[HOME_ORDER[HOME_AXIS_INDEX]] := HOME_SEEK_SWITCH;
                HOME_TIMER(IN := FALSE);  (* Reset timeout *)
            END_IF;

        HOME_SEEK_SWITCH:
            (* Fast approach toward home sensor *)
            (* Velocity = HOME_VELOCITY in configured HOME_DIRECTION *)
            HOME_TIMER(IN := TRUE, PT := T_HOME_TIMEOUT);

            (* Simulated: in real system, motion command issued to drive via
               CC-Link IE Field parameter Pr.52 (home mode), Pr.53 (home speed).
               Drive handles the physical motion; PLC monitors completion. *)

            IF AXIS_STAT[HOME_ORDER[HOME_AXIS_INDEX]].IS_IN_POSITION THEN
                HOME_STATE[HOME_ORDER[HOME_AXIS_INDEX]] := HOME_BACK_OFF;
            END_IF;

            IF HOME_TIMER.Q THEN
                HOME_STATE[HOME_ORDER[HOME_AXIS_INDEX]] := HOME_ERROR;
            END_IF;

        HOME_BACK_OFF:
            (* Reverse direction at creep speed to back off home sensor *)
            (* This gives a clean edge detection on second approach *)
            HOME_STATE[HOME_ORDER[HOME_AXIS_INDEX]] := HOME_FINAL_APP;

        HOME_FINAL_APP:
            (* Final slow approach to home sensor rising edge *)
            (* Drive captures encoder position at sensor transition *)
            IF AXIS_STAT[HOME_ORDER[HOME_AXIS_INDEX]].IS_IN_POSITION THEN
                HOME_STATE[HOME_ORDER[HOME_AXIS_INDEX]] := HOME_SET_ORIGIN;
            END_IF;

        HOME_SET_ORIGIN:
            (* Apply home offset: datum = sensor_pos + HOME_OFFSET *)
            (* Write absolute position origin via CC-Link IE Field Pr.901 *)
            AXIS_STAT[HOME_ORDER[HOME_AXIS_INDEX]].IS_HOMED := TRUE;
            AXIS_STAT[HOME_ORDER[HOME_AXIS_INDEX]].ACTUAL_POS := 0.0;
            HOME_STATE[HOME_ORDER[HOME_AXIS_INDEX]] := HOME_COMPLETE;

        HOME_COMPLETE:
            (* Advance to next axis in homing order *)
            HOME_TIMER(IN := FALSE);
            IF HOME_AXIS_INDEX < 8 THEN
                HOME_AXIS_INDEX := HOME_AXIS_INDEX + 1;
                HOME_STATE[HOME_ORDER[HOME_AXIS_INDEX]] := HOME_SEEK_SWITCH;
            END_IF;

        HOME_ERROR:
            (* Homing failed: alarm and halt sequence *)
            (* Operator must resolve obstruction and re-request homing *)
            HOME_TIMER(IN := FALSE);
            HOME_REQUEST := FALSE;

    END_CASE;

END_IF;

(* --- Aggregate homed status --------------------------------------------- *)
ALL_AXES_HOMED := TRUE;
FOR i := 1 TO 8 DO
    IF NOT AXIS_STAT[i].IS_HOMED THEN
        ALL_AXES_HOMED := FALSE;
    END_IF;
END_FOR;

(* ======================================================================== *)
(* CC-LINK IE FIELD SERVO PARAMETER MAPPING                                 *)
(* MR-J5-G drive parameters mapped to cyclic communication buffer           *)
(*                                                                          *)
(* Cyclic Transmission (RWw/RWr):                                           *)
(*   RWw0: Control word (servo ON, start, stop, home)                       *)
(*   RWw1-RWw2: Target position [pulse] (32-bit)                            *)
(*   RWw3: Target velocity [rpm]                                            *)
(*   RWw4: Acceleration time [ms]                                           *)
(*   RWw5: Deceleration time [ms]                                           *)
(*   RWr0: Status word (ready, in-position, alarm, homed)                   *)
(*   RWr1-RWr2: Actual position [pulse] (32-bit)                            *)
(*   RWr3: Actual velocity [rpm]                                            *)
(*   RWr4: Actual torque [0.1%]                                             *)
(*   RWr5: Alarm code                                                       *)
(*                                                                          *)
(* Station assignment (CC-Link IE Field):                                   *)
(*   Station 2, Slots 0-1: Axes 1-4 (transfer drives)                      *)
(*   Station 3, Slots 0-1: Axes 5-8 (transfer + rotary drives)             *)
(* ======================================================================== *)

END_PROGRAM
