(* =============================================================================
 * Program:     MAIN_SEQUENCE
 * Project:     TI CSE AOI Inspection System (spec V1.4, 2024-05-25)
 * Controller:  Mitsubishi iQ-R Series (R16CPU + RJ71GF11-T2)
 * Network:     CC-Link IE Field 1 Gbps (15 servo axes + remote I/O)
 *              EtherNet/IP CIP implicit messaging (IPC vision host)
 * Scan:        0.5 ms fixed
 * Author:      Rongxuan Zhou
 * Revision:    2.0 - states follow the 18-step process flow (spec slide 15)
 *
 * Process steps -> states
 *   2  Single basket feeding            ST_BASKET_FEED
 *   3  CSE loading (Poka-Yoke + robot)  ST_POKAYOKE_CHECK, ST_ROBOT_PICK
 *   4  Pitch change and positioning     ST_PITCH_CHANGE
 *   5  Transfer #1                      ST_TRANSFER_1
 *   6  Shade close + lighting check     ST_LIGHTING_CHECK        (CCD#4)
 *   7  Bottom check during Transfer #2  ST_TRANSFER_2_BOTTOM     (CCD#3)
 *   8  Top check                        ST_TOP_CHECK             (CCD#1)
 *   9-10 Orientation comp. + positioning ST_TRANSFER_4_COMP
 *   11 Side check, 360 deg rotation     ST_SIDE_CHECK            (CCD#2)
 *   12-13, 17 Transfer #5, tray / NG belt ST_TRANSFER_5_SORT
 *   14, 16 Full tray stack, tray feeding ST_TRAY_HANDLING
 *   18 NG unloading (+ NG check CCD)    ST_NG_UNLOAD
 *   1 and 15 are operator actions.
 *
 * Every inspection station carries 4 cameras (slide 10): one hardware
 * trigger pulse per station images the whole 4-unit batch and the IPC
 * returns VISION_RESULT[cam, 1..4] and sets VISION_DONE[cam] once per
 * trigger. CCD#2 is the exception: 36 trigger pulses per sweep, ONE DONE
 * after the last frame, and SIDE_FRAMES_ECHO reports the frames received.
 * ========================================================================== *)

PROGRAM MAIN_SEQUENCE

(* ---------------------------------------------------------------------------
 * TYPE DEFINITIONS
 * ------------------------------------------------------------------------ *)
TYPE
    E_INSPECT_STATE : (
        ST_IDLE              := 0,
        ST_BASKET_FEED       := 1,
        ST_POKAYOKE_CHECK    := 2,
        ST_ROBOT_PICK        := 3,
        ST_PITCH_CHANGE      := 4,
        ST_TRANSFER_1        := 5,
        ST_LIGHTING_CHECK    := 6,
        ST_TRANSFER_2_BOTTOM := 7,
        ST_TOP_CHECK         := 8,
        ST_TRANSFER_4_COMP   := 9,
        ST_SIDE_CHECK        := 10,
        ST_TRANSFER_5_SORT   := 11,
        ST_TRAY_HANDLING     := 12,
        ST_NG_UNLOAD         := 13,
        ST_ERROR             := 99
    );

    (* One entry per (station, unit). DEFECT_CODE: 0 = OK, 1..21 = DefectType + 1
       in the order of src/vision/DefectTypes.h (1 lighting_check ... 21 yellow_glass_cement). *)
    TYPE_VISION_RESULT : STRUCT
        JUDGE           : BOOL;     (* TRUE = PASS, FALSE = NG *)
        DEFECT_CODE     : INT;
        CONFIDENCE      : REAL;     (* 0.0 - 1.0 *)
        OFFSET_X_MM     : REAL;     (* disc centre offset from the fixture *)
        OFFSET_Y_MM     : REAL;
        THETA_DEG       : REAL;     (* key-notch angle, CCD#1 -> orientation compensation *)
        INSPECT_TIME_MS : INT;
    END_STRUCT;

    TYPE_AXIS_CMD : STRUCT
        TARGET_POS      : DINT;     (* command position in encoder pulses *)
        TARGET_EU       : REAL;     (* the same target in engineering units, for AT_TARGET *)
        REL_MODE        : BOOL;     (* TRUE = incremental move (rotators, drive in degree unit) *)
        SPEED           : DINT;     (* mm/s or deg/s x 100 *)
        ACCEL           : DINT;     (* mm/s^2 or deg/s^2 x 100 *)
        DECEL           : DINT;
        ACTUAL_POS      : REAL;     (* feedback in engineering units (mm or deg) *)
        REL_START       : REAL;     (* start position captured for relative moves *)
        IN_POSITION     : BOOL;
        BUSY            : BOOL;
        ERROR           : BOOL;
    END_STRUCT;
END_TYPE

(* ---------------------------------------------------------------------------
 * CONSTANTS (literal initial values only - GX Works3)
 * ------------------------------------------------------------------------ *)
VAR CONSTANT
    (* axis scaling *)
    PULSES_PER_MM        : REAL := 419430.0;    (* 4194304 PPR / 10 mm lead, axes 1-6 *)
    PULSES_PER_MM_AX15   : REAL := 838861.0;    (* 4194304 PPR / 5 mm lead, pitch e-cylinder *)
    PULSES_PER_DEG_AX7   : REAL := 838861.0;    (* 4194304 x 72 / 360, theta units 7-10 *)
    PULSES_PER_DEG_AX11  : REAL := 582542.0;    (* 4194304 x 50 / 360, rotators 11-14 *)

    (* positions, mm *)
    POS_T1_LIGHTING_MM   : REAL := 180.0;       (* Transfer 1: pitch mech -> lighting nest *)
    POS_T2_HANDOFF_MM    : REAL := 220.0;       (* Transfer 2: lighting nest -> Transfer 3 *)
    T2_TRIG_POS_MM       : REAL := 60.0;        (* Transfer 2 position where CCD#3 fires *)
    POS_T3_PICK_MM       : REAL := 80.0;        (* Transfer 3 shuttle: pick position for Transfer 4 *)
    POS_T3_INSPECT_MM    : REAL := 0.0;         (* Transfer 3 shuttle: under CCD#1 *)
    POS_T4_SIDE_MM       : REAL := 200.0;       (* Transfer 4: Transfer 3 -> side-check nests *)
    POS_T5_TRAY_X0_MM    : REAL := 240.0;       (* Transfer 5: first tray column *)
    POS_T5_NG_BELT_MM    : REAL := 320.0;       (* Transfer 5: NG belt drop *)
    POS_T6_NG_TRAY_MM    : REAL := 150.0;       (* Transfer 6: NG tray *)
    POS_T6_OK_TRAY_MM    : REAL := 60.0;        (* Transfer 6: back to the OK tray (overturned NG) *)
    POS_T6_BELT_MM       : REAL := 0.0;         (* Transfer 6: pick at the holder bar *)
    PITCH_EXPAND_MM      : REAL := 9.46;        (* 12.0 inspection pitch - 2.54 basket pitch *)
    TRAY_PITCH_X_MM      : REAL := 16.0;
    TRAY_PITCH_Y_MM      : REAL := 16.0;

    (* side check *)
    SIDE_FRAMES          : INT  := 36;          (* 10-degree steps, one full revolution *)
    SIDE_STEP_DEG        : REAL := 10.0;
    SIDE_SWEEP_S         : REAL := 1.8;         (* 36 frames / 1.8 s = 20 fps <= 23 fps camera limit *)

    (* timing *)
    T_TRIG_PULSE         : TIME := T#2ms;       (* camera trigger DO pulse, >= 1 EtherNet/IP RPI *)
    T_SHADE              : TIME := T#500ms;     (* shade must report closed within this *)
    T_STEP_DEFAULT       : TIME := T#5s;
    T_STEP_LONG          : TIME := T#15s;       (* robot, side sweep, sorting, tray, NG unload *)
    T_HB                 : TIME := T#1s;        (* IPC heartbeat must toggle within this *)
    T_PUSH               : TIME := T#500ms;     (* empty basket push pulse *)
    T_RELEASE            : TIME := T#100ms;     (* Transfer 5 per-unit release pulse *)
    POS_TOL_EU           : REAL := 0.05;        (* AT_TARGET window, mm or deg *)

    (* capacities *)
    GROUPS_PER_BASKET    : INT  := 10;          (* 4-unit groups per basket (recipe) *)
    TRAY_ROWS            : INT  := 5;
    TRAY_COLS            : INT  := 10;
    MAX_CONSECUTIVE_NG   : INT  := 5;           (* consecutive all-NG batches -> alarm *)
END_VAR

(* ---------------------------------------------------------------------------
 * VARIABLES
 * ------------------------------------------------------------------------ *)
VAR
    (* ---- state machine ---- *)
    CURRENT_STATE       : E_INSPECT_STATE := ST_IDLE;
    NEXT_STATE          : E_INSPECT_STATE := ST_IDLE;
    STATE_ENTRY         : BOOL := TRUE;
    SUB_STEP            : INT := 0;
    SUB_STEP_PREV       : INT := 0;
    STEP_TIMER          : TON;
    STEP_TIMEOUT_SEL    : TIME := T#5s;
    STEP_RESET          : BOOL;
    CYCLE_TIMER         : TON;
    LAST_ERROR_CODE     : INT := 0;

    (* ---- servo axes (CC-Link IE Field cyclic) ----
       1-6 transfers, 7-10 theta units on Transfer 4, 11-14 side rotators, 15 pitch e-cylinder *)
    AXIS                : ARRAY[1..15] OF TYPE_AXIS_CMD;
    AXIS_STOP           : ARRAY[1..15] OF BOOL;     (* drive control word: controlled stop *)
    AT_TARGET           : ARRAY[1..15] OF BOOL;     (* IN_POSITION confirmed by position compare *)
    ERROR_REQ           : BOOL;                     (* latched by the pre-CASE monitors *)
    ERROR_CODE_REQ      : INT := 0;

    (* ---- vision handshake (EtherNet/IP CIP implicit) ----
       index 1..4 = CCD#1..#4, 5 = NG check CCD *)
    VISION_TRIG         : ARRAY[1..5] OF BOOL;      (* DO: hardware trigger pulse *)
    VISION_PENDING      : ARRAY[1..5] OF BOOL;      (* trigger sent, results not yet in *)
    VISION_DONE         : ARRAY[1..5] OF BOOL;      (* IPC -> PLC: all 4 cameras reported *)
    VISION_DONE_EDGE    : ARRAY[1..5] OF R_TRIG;
    TRIG_PULSE          : ARRAY[1..5] OF TON;
    VISION_RESULT       : ARRAY[1..5, 1..4] OF TYPE_VISION_RESULT;
    VISION_SEQ          : INT := 0;                 (* increments per trigger; the IPC tags results with it *)
    IPC_HEARTBEAT       : BOOL;
    HB_EDGE             : R_TRIG;
    HB_FALL             : F_TRIG;
    HB_TIMER            : TON;
    SIDE_FRAME_IDX      : INT := 0;
    SIDE_FRAMES_ECHO    : INT := 0;                 (* IPC: frames received for the current sweep *)
    SIDE_SWEPT_DEG      : REAL := 0.0;

    (* ---- batch bookkeeping ---- *)
    UNIT_NG             : ARRAY[1..4] OF BOOL;
    UNIT_THETA          : ARRAY[1..4] OF REAL;      (* deg, from CCD#1 *)
    CASE_SEL            : INT := 2;                 (* 1 = 1st case (flip 180), 2 = 2nd case (default) *)
    NG_ON_BELT          : INT := 0;
    NG_CONFIRMED        : BOOL;
    GROUPS_PICKED       : INT := 0;
    TRAY_ROW            : INT := 0;
    TRAY_COL            : INT := 0;
    T2_FIRED            : BOOL;
    RELEASE_TIMER       : TON;
    BATCH_HAS_NG        : BOOL;
    u                   : INT;                      (* unit loop index *)
    i                   : INT;

    (* ---- basket feeder (slide 16-17) ---- *)
    SOL_BASKET_STACK_HOLD : BOOL;   (* holds the upper baskets *)
    SOL_HOLDING_BLOCK     : BOOL;   (* holds the bottom of the last basket *)
    SOL_L_TRIGGER         : BOOL;   (* keeps the last basket from moving up *)
    SOL_LIFTER_UP         : BOOL;   (* lifter splits the last basket from the stack *)
    SOL_EMPTY_BASKET_PUSH : BOOL;   (* pushes the empty basket to the collector *)
    LIFTER_UP             : BOOL;
    LIFTER_DOWN           : BOOL;
    BASKET_AT_HOLDER      : BOOL;
    BASKET_EMPTY          : BOOL;   (* no CSE left in the basket on the holder *)
    BASKET_STACK_PRESENT  : BOOL;
    PUSH_TIMER            : TON;

    (* ---- Poka-Yoke CCD and robot (slide 18) ---- *)
    PY_TRIG             : BOOL;
    PY_DONE             : BOOL;
    PY_DONE_EDGE        : R_TRIG;
    PY_CASE_SEL         : INT;      (* 0 = unknown, 1, 2 *)
    PY_OK               : BOOL;
    ROBOT_START         : BOOL;
    ROBOT_CASE_SEL      : INT;
    ROBOT_READY         : BOOL;
    ROBOT_BUSY          : BOOL;
    ROBOT_DONE          : BOOL;
    ROBOT_ALARM         : BOOL;

    (* ---- pitch change mechanism (slides 19-20) ---- *)
    SOL_PLATFORM_VACUUM   : BOOL;   (* purple platform *)
    SOL_BLUE_HOLDER_CLOSE : BOOL;
    SOL_FLIP_180          : BOOL;
    HOLDER_OPEN           : BOOL;
    HOLDER_CLOSED         : BOOL;
    FLIP_DONE             : BOOL;
    FLIP_HOME             : BOOL;
    PITCH_SETTLED         : BOOL;

    (* ---- lighting check chamber (slides 21-22) ---- *)
    SOL_NEST_VACUUM_CCD4  : BOOL;
    SOL_SHADE_FWD         : BOOL;
    SHADE_CLOSED          : BOOL;
    SHADE_OPEN            : BOOL;
    SHADE_TIMER           : TON;
    PART_AT_CCD4          : BOOL;

    (* ---- top check (slide 24) ---- *)
    SOL_CCD1_REACH        : BOOL;
    CCD1_EXTENDED         : BOOL;
    CCD1_RETRACTED        : BOOL;

    (* ---- side check (slide 26) ---- *)
    SOL_NEST_VACUUM_SIDE  : BOOL;
    SOL_GRIPPER_LIFT      : BOOL;
    GRIPPER_UP            : BOOL;
    GRIPPER_DOWN          : BOOL;

    (* ---- transfers and sorting (slides 23, 25, 27) ---- *)
    SOL_TRANSFER_GRIP     : ARRAY[1..6] OF BOOL;
    SOL_T5_RELEASE        : ARRAY[1..4] OF BOOL;   (* per-unit release on the Transfer 5 head *)
    TRANSFER_ACTIVE       : ARRAY[1..6] OF BOOL;
    ZONE_CLEAR            : ARRAY[1..6] OF BOOL;

    (* ---- tray handling (slides 28-29) ---- *)
    TRAY_FULL             : BOOL;   (* upside camera *)
    TRAY_AT_POS1          : BOOL;
    EMPTY_TRAY_PRESENT    : BOOL;
    SOL_STACK_GRIPPER     : BOOL;
    SOL_TRAY_PUSH_POS2    : BOOL;
    SOL_TRAY_LOADER_FEED  : BOOL;

    (* ---- NG path (slide 27) ---- *)
    NG_BELT_RUN           : BOOL;
    SOL_NG_HOLDER_BAR     : BOOL;
    NG_AT_HOLDER_BAR      : BOOL;

    (* ---- safety and operator ---- *)
    ESTOP_OK              : BOOL;
    SAFETY_GATE_CLOSED    : BOOL;
    SERVO_STO_RELEASED    : BOOL;
    CMD_START             : BOOL;
    CMD_STOP              : BOOL;
    CMD_RESET             : BOOL;
    AUTO_MODE             : BOOL;
    CYCLE_RUNNING         : BOOL;
    STOP_REQUESTED        : BOOL;
    RT_START              : R_TRIG;
    RT_RESET              : R_TRIG;
    FT_ESTOP              : F_TRIG;

    (* ---- diagnostics ---- *)
    BATCH_COUNT           : UDINT;
    UNIT_COUNT            : UDINT;
    NG_COUNT              : UDINT;
    FALSE_REJECT_COUNT    : UDINT;
    CONSECUTIVE_NG        : INT := 0;
    CYCLE_TIME_ACTUAL     : TIME;
END_VAR

(* ---------------------------------------------------------------------------
 * INTERLOCKS - adjacent transfer heads share a hand-off zone
 * ------------------------------------------------------------------------ *)
FOR i := 1 TO 6 DO
    ZONE_CLEAR[i] := TRUE;
END_FOR;
IF TRANSFER_ACTIVE[2] THEN ZONE_CLEAR[1] := FALSE; END_IF;
FOR i := 2 TO 5 DO
    IF TRANSFER_ACTIVE[i - 1] OR TRANSFER_ACTIVE[i + 1] THEN
        ZONE_CLEAR[i] := FALSE;
    END_IF;
END_FOR;
IF TRANSFER_ACTIVE[5] THEN ZONE_CLEAR[6] := FALSE; END_IF;

(* ---------------------------------------------------------------------------
 * VISION TRIGGER / RESULT HANDSHAKE (every scan)
 *   fire(cam):  VISION_SEQ := VISION_SEQ + 1; VISION_PENDING[cam] := TRUE;
 *               VISION_TRIG[cam] := TRUE;      -> 2 ms DO pulse
 *   done(cam):  NOT VISION_PENDING[cam]        (cleared on the DONE rising edge)
 * ------------------------------------------------------------------------ *)
FOR i := 1 TO 5 DO
    TRIG_PULSE[i](IN := VISION_TRIG[i], PT := T_TRIG_PULSE);
    IF TRIG_PULSE[i].Q THEN
        VISION_TRIG[i] := FALSE;
    END_IF;
    VISION_DONE_EDGE[i](CLK := VISION_DONE[i]);
    IF VISION_DONE_EDGE[i].Q THEN
        VISION_PENDING[i] := FALSE;
    END_IF;
END_FOR;

(* ---------------------------------------------------------------------------
 * AXIS ARRIVAL (every scan)
 * The drive's IN_POSITION bit stays TRUE for at least one communication cycle
 * after a new target is written, so a state must never advance on it alone.
 * Absolute axes: arrival = IN_POSITION, not BUSY, and the feedback within
 * POS_TOL_EU of the commanded engineering-unit target (a zero-length move is
 * therefore complete at once). Rotators 11-14 move incrementally in the
 * drive's degree unit; their arrival is the end of the 36-frame sweep.
 * ------------------------------------------------------------------------ *)
FOR i := 1 TO 15 DO
    IF AXIS[i].REL_MODE THEN
        AT_TARGET[i] := AXIS[i].IN_POSITION AND NOT AXIS[i].BUSY AND (SIDE_FRAME_IDX >= SIDE_FRAMES);
    ELSE
        AT_TARGET[i] := AXIS[i].IN_POSITION AND NOT AXIS[i].BUSY
                        AND (ABS(AXIS[i].ACTUAL_POS - AXIS[i].TARGET_EU) <= POS_TOL_EU);
    END_IF;
END_FOR;

(* ---------------------------------------------------------------------------
 * EDGES AND SAFETY - monitors latch ERROR_REQ; the transition block applies
 * it after the state bodies so a body transition in the same scan cannot win.
 * ------------------------------------------------------------------------ *)
RT_START(CLK := CMD_START);
RT_RESET(CLK := CMD_RESET);
FT_ESTOP(CLK := ESTOP_OK);
IF CMD_STOP THEN
    STOP_REQUESTED := TRUE;
END_IF;

IF FT_ESTOP.Q THEN
    ERROR_REQ := TRUE;
    ERROR_CODE_REQ := 9001;     (* E-Stop activated *)
END_IF;

IF NOT ESTOP_OK OR NOT SAFETY_GATE_CLOSED OR NOT SERVO_STO_RELEASED THEN
    IF CURRENT_STATE <> ST_IDLE AND CURRENT_STATE <> ST_ERROR THEN
        ERROR_REQ := TRUE;
        ERROR_CODE_REQ := 9002;     (* safety interlock violation *)
    END_IF;
END_IF;

(* ---------------------------------------------------------------------------
 * STEP WATCHDOG - restarts on state entry and on every SUB_STEP advance.
 * 8002 when a vision result is outstanding, 8001 otherwise.
 * ------------------------------------------------------------------------ *)
CASE CURRENT_STATE OF
    ST_ROBOT_PICK, ST_SIDE_CHECK, ST_TRANSFER_5_SORT, ST_TRAY_HANDLING, ST_NG_UNLOAD:
        STEP_TIMEOUT_SEL := T_STEP_LONG;
ELSE
        STEP_TIMEOUT_SEL := T_STEP_DEFAULT;
END_CASE;

STEP_RESET := STATE_ENTRY OR (SUB_STEP <> SUB_STEP_PREV);
SUB_STEP_PREV := SUB_STEP;
STEP_TIMER(IN := CYCLE_RUNNING AND NOT STEP_RESET
                 AND (CURRENT_STATE <> ST_IDLE) AND (CURRENT_STATE <> ST_ERROR),
           PT := STEP_TIMEOUT_SEL);
IF STEP_TIMER.Q AND CURRENT_STATE <> ST_ERROR THEN
    ERROR_REQ := TRUE;
    ERROR_CODE_REQ := 8001;     (* step timeout *)
    FOR i := 1 TO 5 DO
        IF VISION_PENDING[i] THEN
            ERROR_CODE_REQ := 8002;     (* vision result timeout *)
        END_IF;
    END_FOR;
END_IF;

(* IPC heartbeat: must toggle within T_HB while a cycle runs *)
HB_EDGE(CLK := IPC_HEARTBEAT);
HB_FALL(CLK := IPC_HEARTBEAT);
HB_TIMER(IN := CYCLE_RUNNING AND NOT HB_EDGE.Q AND NOT HB_FALL.Q, PT := T_HB);
IF HB_TIMER.Q AND CURRENT_STATE <> ST_ERROR THEN
    ERROR_REQ := TRUE;
    ERROR_CODE_REQ := 8003;     (* IPC heartbeat lost *)
END_IF;

(* ---------------------------------------------------------------------------
 * MAIN STATE MACHINE - bodies assign NEXT_STATE only; the transition block
 * after END_CASE performs the switch and resets SUB_STEP.
 * ------------------------------------------------------------------------ *)
CASE CURRENT_STATE OF

    (* ======================================================================= *)
    ST_IDLE:
        IF STATE_ENTRY THEN
            CYCLE_RUNNING := FALSE;
            STOP_REQUESTED := FALSE;
            FOR i := 1 TO 6 DO
                SOL_TRANSFER_GRIP[i] := FALSE;
                TRANSFER_ACTIVE[i] := FALSE;
            END_FOR;
            STATE_ENTRY := FALSE;
        END_IF;

        IF RT_START.Q AND AUTO_MODE AND ESTOP_OK AND SAFETY_GATE_CLOSED AND SERVO_STO_RELEASED THEN
            CYCLE_RUNNING := TRUE;
            CYCLE_TIMER(IN := FALSE, PT := T#0ms);
            IF BASKET_AT_HOLDER AND NOT BASKET_EMPTY THEN
                NEXT_STATE := ST_POKAYOKE_CHECK;
            ELSE
                NEXT_STATE := ST_BASKET_FEED;
            END_IF;
        END_IF;

    (* ======================================================================= *)
    ST_BASKET_FEED:
        (* Step 2 (slides 16-17). Entered only when the holder is empty or the
           basket on it has been emptied. *)
        IF STATE_ENTRY THEN
            STATE_ENTRY := FALSE;
        END_IF;

        CASE SUB_STEP OF
            0:  (* push the finished basket to the collector: empty by sensor,
                   or all groups picked (the sensor may still see leftovers) *)
                IF BASKET_AT_HOLDER AND (BASKET_EMPTY OR GROUPS_PICKED >= GROUPS_PER_BASKET) THEN
                    SOL_EMPTY_BASKET_PUSH := TRUE;
                    PUSH_TIMER(IN := TRUE, PT := T_PUSH);
                    IF PUSH_TIMER.Q THEN
                        SOL_EMPTY_BASKET_PUSH := FALSE;
                        PUSH_TIMER(IN := FALSE, PT := T_PUSH);
                        SUB_STEP := 1;
                    END_IF;
                ELSE
                    SUB_STEP := 1;
                END_IF;
            1:  (* hold the stack, hold the bottom of the last basket *)
                IF NOT BASKET_STACK_PRESENT THEN
                    NEXT_STATE := ST_ERROR;
                    LAST_ERROR_CODE := 6004;    (* basket stack empty - operator load *)
                ELSE
                    SOL_BASKET_STACK_HOLD := TRUE;
                    SOL_HOLDING_BLOCK := TRUE;
                    SUB_STEP := 2;
                END_IF;
            2:  (* lifter splits the last basket *)
                SOL_LIFTER_UP := TRUE;
                IF LIFTER_UP THEN
                    SOL_L_TRIGGER := TRUE;
                    SUB_STEP := 3;
                END_IF;
            3:  (* lower the basket onto the holder *)
                SOL_HOLDING_BLOCK := FALSE;
                SOL_LIFTER_UP := FALSE;
                IF LIFTER_DOWN AND BASKET_AT_HOLDER THEN
                    SOL_L_TRIGGER := FALSE;
                    GROUPS_PICKED := 0;
                    SUB_STEP := 4;
                END_IF;
            4:
                NEXT_STATE := ST_POKAYOKE_CHECK;
        END_CASE;

    (* ======================================================================= *)
    ST_POKAYOKE_CHECK:
        (* Step 3.1 (slide 18): CCD checks CSE orientation, gives the case. *)
        IF STATE_ENTRY THEN
            PY_TRIG := TRUE;
            STATE_ENTRY := FALSE;
        END_IF;

        PY_DONE_EDGE(CLK := PY_DONE);
        IF PY_DONE_EDGE.Q THEN
            PY_TRIG := FALSE;
            IF NOT PY_OK THEN
                NEXT_STATE := ST_ERROR;
                LAST_ERROR_CODE := 6001;    (* Poka-Yoke NOGO: wrong orientation in basket *)
            ELSE
                IF PY_CASE_SEL = 1 THEN
                    CASE_SEL := 1;
                ELSE
                    CASE_SEL := 2;          (* 2nd case is the default ("all the rest") *)
                END_IF;
                NEXT_STATE := ST_ROBOT_PICK;
            END_IF;
        END_IF;

    (* ======================================================================= *)
    ST_ROBOT_PICK:
        (* Step 3.2-3.3 (slide 18): Epson RC700-A picks 4 CSEs, rotates 90 deg,
           places them on the purple platform. Case 1 sucks the bottom side. *)
        IF STATE_ENTRY THEN
            STATE_ENTRY := FALSE;
        END_IF;

        IF ROBOT_ALARM THEN
            NEXT_STATE := ST_ERROR;
            LAST_ERROR_CODE := 6002;    (* robot alarm *)
        END_IF;

        CASE SUB_STEP OF
            0:
                IF ROBOT_READY THEN
                    ROBOT_CASE_SEL := CASE_SEL;
                    SOL_PLATFORM_VACUUM := TRUE;
                    ROBOT_START := TRUE;
                    SUB_STEP := 1;
                END_IF;
            1:
                IF ROBOT_BUSY THEN
                    ROBOT_START := FALSE;
                    SUB_STEP := 2;
                END_IF;
            2:
                IF ROBOT_DONE AND NOT ROBOT_BUSY THEN
                    GROUPS_PICKED := GROUPS_PICKED + 1;
                    NEXT_STATE := ST_PITCH_CHANGE;
                END_IF;
        END_CASE;

    (* ======================================================================= *)
    ST_PITCH_CHANGE:
        (* Step 4 (slides 19-20): e-cylinder expands the pitch, blue holder
           closes and positions; 1st case: holder turns the CSEs over 180 deg. *)
        IF STATE_ENTRY THEN
            STATE_ENTRY := FALSE;
        END_IF;

        CASE SUB_STEP OF
            0:
                AXIS[15].TARGET_EU := PITCH_EXPAND_MM;
                AXIS[15].TARGET_POS := REAL_TO_DINT(PITCH_EXPAND_MM * PULSES_PER_MM_AX15);
                AXIS[15].SPEED := 20000;    (* 200 mm/s *)
                AXIS[15].ACCEL := 200000;
                SUB_STEP := 1;
            1:
                IF AT_TARGET[15] AND PITCH_SETTLED THEN
                    SOL_BLUE_HOLDER_CLOSE := TRUE;
                    SUB_STEP := 2;
                END_IF;
            2:
                IF HOLDER_CLOSED THEN
                    IF CASE_SEL = 1 THEN
                        SOL_FLIP_180 := TRUE;
                        SUB_STEP := 3;
                    ELSE
                        SUB_STEP := 4;
                    END_IF;
                END_IF;
            3:
                IF FLIP_DONE THEN
                    SUB_STEP := 4;
                END_IF;
            4:
                NEXT_STATE := ST_TRANSFER_1;
        END_CASE;

    (* ======================================================================= *)
    ST_TRANSFER_1:
        (* Step 5 (slide 21): Transfer 1 picks the 4 CSEs off the pitch
           mechanism and places them in the lighting-check nest. The pitch
           mechanism is released and returned home for the next batch. *)
        IF STATE_ENTRY THEN
            STATE_ENTRY := FALSE;
        END_IF;

        CASE SUB_STEP OF
            0:
                IF ZONE_CLEAR[1] THEN
                    TRANSFER_ACTIVE[1] := TRUE;
                    SOL_TRANSFER_GRIP[1] := TRUE;
                    SUB_STEP := 1;
                END_IF;
            1:  (* release the mechanism once the head holds the parts *)
                SOL_PLATFORM_VACUUM := FALSE;
                SOL_BLUE_HOLDER_CLOSE := FALSE;
                IF HOLDER_OPEN THEN
                    AXIS[1].TARGET_EU := POS_T1_LIGHTING_MM;
                    AXIS[1].TARGET_POS := REAL_TO_DINT(POS_T1_LIGHTING_MM * PULSES_PER_MM);
                    AXIS[1].SPEED := 100000;    (* 1000 mm/s *)
                    AXIS[1].ACCEL := 1000000;
                    SUB_STEP := 2;
                END_IF;
            2:
                IF AT_TARGET[1] THEN
                    SOL_NEST_VACUUM_CCD4 := TRUE;
                    SOL_TRANSFER_GRIP[1] := FALSE;
                    SUB_STEP := 3;
                END_IF;
            3:
                IF PART_AT_CCD4 THEN
                    TRANSFER_ACTIVE[1] := FALSE;
                    (* pitch mechanism back home: flip back, e-cylinder to 0 *)
                    SOL_FLIP_180 := FALSE;
                    AXIS[15].TARGET_EU := 0.0;
                    AXIS[15].TARGET_POS := 0;
                    AXIS[1].TARGET_EU := 0.0;
                    AXIS[1].TARGET_POS := 0;
                    SUB_STEP := 4;
                END_IF;
            4:
                IF FLIP_HOME OR CASE_SEL = 2 THEN
                    NEXT_STATE := ST_LIGHTING_CHECK;
                END_IF;
        END_CASE;

    (* ======================================================================= *)
    ST_LIGHTING_CHECK:
        (* Step 6 (slides 21-22): light source and shade move forward and
           cover the CSEs with the shell; CCD#4 (x4) inspects; shade opens. *)
        IF STATE_ENTRY THEN
            SOL_SHADE_FWD := TRUE;
            STATE_ENTRY := FALSE;
        END_IF;

        SHADE_TIMER(IN := SOL_SHADE_FWD AND NOT SHADE_CLOSED, PT := T_SHADE);
        IF SHADE_TIMER.Q THEN
            NEXT_STATE := ST_ERROR;
            LAST_ERROR_CODE := 6003;    (* shade failed to close *)
        END_IF;

        CASE SUB_STEP OF
            0:
                IF SHADE_CLOSED THEN
                    VISION_SEQ := VISION_SEQ + 1;
                    VISION_PENDING[4] := TRUE;
                    VISION_TRIG[4] := TRUE;
                    SUB_STEP := 1;
                END_IF;
            1:
                IF NOT VISION_PENDING[4] THEN
                    FOR u := 1 TO 4 DO
                        UNIT_NG[u] := NOT VISION_RESULT[4, u].JUDGE;
                    END_FOR;
                    SOL_SHADE_FWD := FALSE;
                    SUB_STEP := 2;
                END_IF;
            2:
                IF SHADE_OPEN THEN
                    NEXT_STATE := ST_TRANSFER_2_BOTTOM;
                END_IF;
        END_CASE;

    (* ======================================================================= *)
    ST_TRANSFER_2_BOTTOM:
        (* Steps 7 (slide 23): Transfer 2 moves the CSEs from the lighting nest
           to Transfer 3; CCD#3 (x4) fires on the fly at T2_TRIG_POS_MM. *)
        IF STATE_ENTRY THEN
            T2_FIRED := FALSE;
            STATE_ENTRY := FALSE;
        END_IF;

        CASE SUB_STEP OF
            0:
                IF ZONE_CLEAR[2] THEN
                    TRANSFER_ACTIVE[2] := TRUE;
                    SOL_TRANSFER_GRIP[2] := TRUE;
                    SUB_STEP := 1;
                END_IF;
            1:
                SOL_NEST_VACUUM_CCD4 := FALSE;
                AXIS[2].TARGET_EU := POS_T2_HANDOFF_MM;
                AXIS[2].TARGET_POS := REAL_TO_DINT(POS_T2_HANDOFF_MM * PULSES_PER_MM);
                AXIS[2].SPEED := 60000;     (* 600 mm/s: keeps motion blur under 1 px at 10 us strobe *)
                AXIS[2].ACCEL := 600000;
                SUB_STEP := 2;
            2:
                IF NOT T2_FIRED AND AXIS[2].ACTUAL_POS >= T2_TRIG_POS_MM THEN
                    T2_FIRED := TRUE;
                    VISION_SEQ := VISION_SEQ + 1;
                    VISION_PENDING[3] := TRUE;
                    VISION_TRIG[3] := TRUE;
                END_IF;
                IF AT_TARGET[2] AND T2_FIRED AND NOT VISION_PENDING[3] THEN
                    FOR u := 1 TO 4 DO
                        UNIT_NG[u] := UNIT_NG[u] OR NOT VISION_RESULT[3, u].JUDGE;
                    END_FOR;
                    SOL_TRANSFER_GRIP[2] := FALSE;
                    TRANSFER_ACTIVE[2] := FALSE;
                    AXIS[2].TARGET_EU := 0.0;
                    AXIS[2].TARGET_POS := 0;
                    NEXT_STATE := ST_TOP_CHECK;
                END_IF;
        END_CASE;

    (* ======================================================================= *)
    ST_TOP_CHECK:
        (* Step 8 (slide 24): CCD#1 (x4) reaches out, inspects the top side and
           returns the key-notch angle per unit; CCD#1 moves back; Transfer 3
           shuttles to the pick position. *)
        IF STATE_ENTRY THEN
            SOL_CCD1_REACH := TRUE;
            STATE_ENTRY := FALSE;
        END_IF;

        CASE SUB_STEP OF
            0:
                IF CCD1_EXTENDED THEN
                    VISION_SEQ := VISION_SEQ + 1;
                    VISION_PENDING[1] := TRUE;
                    VISION_TRIG[1] := TRUE;
                    SUB_STEP := 1;
                END_IF;
            1:
                IF NOT VISION_PENDING[1] THEN
                    FOR u := 1 TO 4 DO
                        UNIT_NG[u] := UNIT_NG[u] OR NOT VISION_RESULT[1, u].JUDGE;
                        UNIT_THETA[u] := VISION_RESULT[1, u].THETA_DEG;
                    END_FOR;
                    SOL_CCD1_REACH := FALSE;
                    SUB_STEP := 2;
                END_IF;
            2:
                IF CCD1_RETRACTED THEN
                    AXIS[3].TARGET_EU := POS_T3_PICK_MM;
                    AXIS[3].TARGET_POS := REAL_TO_DINT(POS_T3_PICK_MM * PULSES_PER_MM);
                    AXIS[3].SPEED := 80000;
                    AXIS[3].ACCEL := 800000;
                    SUB_STEP := 3;
                END_IF;
            3:
                IF AT_TARGET[3] THEN
                    NEXT_STATE := ST_TRANSFER_4_COMP;
                END_IF;
        END_CASE;

    (* ======================================================================= *)
    ST_TRANSFER_4_COMP:
        (* Steps 9-10 (slide 25): Transfer 4 picks from Transfer 3 and adjusts
           the orientation of each CSE (theta units 7-10) while it travels to
           the side-check position. *)
        IF STATE_ENTRY THEN
            STATE_ENTRY := FALSE;
        END_IF;

        CASE SUB_STEP OF
            0:
                IF ZONE_CLEAR[4] THEN
                    TRANSFER_ACTIVE[4] := TRUE;
                    SOL_TRANSFER_GRIP[4] := TRUE;
                    SUB_STEP := 1;
                END_IF;
            1:
                AXIS[4].TARGET_EU := POS_T4_SIDE_MM;
                AXIS[4].TARGET_POS := REAL_TO_DINT(POS_T4_SIDE_MM * PULSES_PER_MM);
                AXIS[4].SPEED := 100000;
                AXIS[4].ACCEL := 1000000;
                FOR u := 1 TO 4 DO
                    AXIS[6 + u].TARGET_EU := 0.0 - UNIT_THETA[u];
                    AXIS[6 + u].TARGET_POS := REAL_TO_DINT(AXIS[6 + u].TARGET_EU * PULSES_PER_DEG_AX7);
                    AXIS[6 + u].SPEED := 36000;     (* 360 deg/s *)
                    AXIS[6 + u].ACCEL := 360000;
                END_FOR;
                (* Transfer 3 shuttle back under CCD#1 for the next batch *)
                AXIS[3].TARGET_EU := POS_T3_INSPECT_MM;
                AXIS[3].TARGET_POS := REAL_TO_DINT(POS_T3_INSPECT_MM * PULSES_PER_MM);
                SUB_STEP := 2;
            2:
                IF AT_TARGET[4] AND AT_TARGET[7] AND AT_TARGET[8]
                   AND AT_TARGET[9] AND AT_TARGET[10] THEN
                    SOL_NEST_VACUUM_SIDE := TRUE;
                    SOL_TRANSFER_GRIP[4] := FALSE;
                    SUB_STEP := 3;
                END_IF;
            3:
                TRANSFER_ACTIVE[4] := FALSE;
                AXIS[4].TARGET_EU := 0.0;
                AXIS[4].TARGET_POS := 0;
                FOR u := 1 TO 4 DO
                    AXIS[6 + u].TARGET_EU := 0.0;
                    AXIS[6 + u].TARGET_POS := 0;
                END_FOR;
                NEXT_STATE := ST_SIDE_CHECK;
        END_CASE;

    (* ======================================================================= *)
    ST_SIDE_CHECK:
        (* Step 11 (slide 26): grippers lift and position the CSEs; rotators
           11-14 turn them 360 deg while CCD#2 (x4) captures 36 frames. *)
        IF STATE_ENTRY THEN
            SOL_GRIPPER_LIFT := TRUE;
            SIDE_FRAME_IDX := 0;
            STATE_ENTRY := FALSE;
        END_IF;

        CASE SUB_STEP OF
            0:
                IF GRIPPER_UP THEN
                    (* one incremental revolution; the drives run in the degree
                       unit (modulo 360), so the command never accumulates *)
                    FOR i := 11 TO 14 DO
                        AXIS[i].REL_MODE := TRUE;
                        AXIS[i].REL_START := AXIS[i].ACTUAL_POS;
                        AXIS[i].TARGET_EU := 360.0;
                        AXIS[i].TARGET_POS := REAL_TO_DINT(360.0 * PULSES_PER_DEG_AX11);
                        AXIS[i].SPEED := REAL_TO_DINT(360.0 / SIDE_SWEEP_S * 100.0);
                        AXIS[i].ACCEL := 720000;
                    END_FOR;
                    (* CCD#2 contract: 36 trigger pulses, ONE DONE after the last
                       frame, SIDE_FRAMES_ECHO = frames received *)
                    VISION_SEQ := VISION_SEQ + 1;
                    VISION_PENDING[2] := TRUE;
                    SUB_STEP := 1;
                END_IF;
            1:  (* position-compare trigger every SIDE_STEP_DEG on the first rotator *)
                SIDE_SWEPT_DEG := AXIS[11].ACTUAL_POS - AXIS[11].REL_START;
                IF SIDE_SWEPT_DEG < 0.0 THEN
                    SIDE_SWEPT_DEG := SIDE_SWEPT_DEG + 360.0;   (* degree unit wrapped *)
                END_IF;
                IF SIDE_FRAME_IDX < SIDE_FRAMES
                   AND SIDE_SWEPT_DEG >= INT_TO_REAL(SIDE_FRAME_IDX) * SIDE_STEP_DEG THEN
                    VISION_TRIG[2] := TRUE;
                    SIDE_FRAME_IDX := SIDE_FRAME_IDX + 1;
                END_IF;
                IF SIDE_FRAME_IDX >= SIDE_FRAMES AND AT_TARGET[11] AND AT_TARGET[12]
                   AND AT_TARGET[13] AND AT_TARGET[14] THEN
                    FOR i := 11 TO 14 DO
                        AXIS[i].REL_MODE := FALSE;
                    END_FOR;
                    SUB_STEP := 2;
                END_IF;
            2:
                IF NOT VISION_PENDING[2] AND SIDE_FRAMES_ECHO >= SIDE_FRAMES THEN
                    FOR u := 1 TO 4 DO
                        UNIT_NG[u] := UNIT_NG[u] OR NOT VISION_RESULT[2, u].JUDGE;
                    END_FOR;
                    SOL_GRIPPER_LIFT := FALSE;
                    SUB_STEP := 3;
                END_IF;
            3:
                IF GRIPPER_DOWN THEN
                    NEXT_STATE := ST_TRANSFER_5_SORT;
                END_IF;
        END_CASE;

    (* ======================================================================= *)
    ST_TRANSFER_5_SORT:
        (* Steps 12-13, 17 (slide 27): Transfer 5 unloads OK CSEs to the tray
           and NG CSEs to the NG belt, one unit at a time. *)
        IF STATE_ENTRY THEN
            BATCH_HAS_NG := FALSE;
            STATE_ENTRY := FALSE;
        END_IF;

        CASE SUB_STEP OF
            0:
                IF ZONE_CLEAR[5] THEN
                    TRANSFER_ACTIVE[5] := TRUE;
                    SOL_TRANSFER_GRIP[5] := TRUE;
                    SOL_NEST_VACUUM_SIDE := FALSE;
                    u := 1;
                    SUB_STEP := 1;
                END_IF;
            1:  (* command the drop position for unit u *)
                IF UNIT_NG[u] THEN
                    AXIS[5].TARGET_EU := POS_T5_NG_BELT_MM;
                    AXIS[5].TARGET_POS := REAL_TO_DINT(POS_T5_NG_BELT_MM * PULSES_PER_MM);
                ELSE
                    AXIS[5].TARGET_EU := POS_T5_TRAY_X0_MM
                                         + INT_TO_REAL(TRAY_COL) * TRAY_PITCH_X_MM
                                         + INT_TO_REAL(TRAY_ROW) * TRAY_PITCH_Y_MM;
                    AXIS[5].TARGET_POS := REAL_TO_DINT(AXIS[5].TARGET_EU * PULSES_PER_MM);
                END_IF;
                AXIS[5].SPEED := 80000;
                AXIS[5].ACCEL := 800000;
                SUB_STEP := 2;
            2:
                IF AT_TARGET[5] THEN
                    SOL_T5_RELEASE[u] := TRUE;
                END_IF;
                RELEASE_TIMER(IN := SOL_T5_RELEASE[u], PT := T_RELEASE);
                IF RELEASE_TIMER.Q THEN
                    SOL_T5_RELEASE[u] := FALSE;
                    SUB_STEP := 3;
                END_IF;
            3:  (* bookkeeping after the release *)
                UNIT_COUNT := UNIT_COUNT + 1;
                IF UNIT_NG[u] THEN
                    NG_ON_BELT := NG_ON_BELT + 1;
                    BATCH_HAS_NG := TRUE;
                ELSE
                    TRAY_COL := TRAY_COL + 1;
                    IF TRAY_COL >= TRAY_COLS THEN
                        TRAY_COL := 0;
                        TRAY_ROW := TRAY_ROW + 1;
                    END_IF;
                END_IF;
                IF u < 4 THEN
                    u := u + 1;
                    SUB_STEP := 1;
                ELSE
                    SUB_STEP := 4;
                END_IF;
            4:
                SOL_TRANSFER_GRIP[5] := FALSE;
                TRANSFER_ACTIVE[5] := FALSE;
                AXIS[5].TARGET_EU := 0.0;
                AXIS[5].TARGET_POS := 0;
                BATCH_COUNT := BATCH_COUNT + 1;
                IF BATCH_HAS_NG THEN
                    CONSECUTIVE_NG := CONSECUTIVE_NG + 1;
                ELSE
                    CONSECUTIVE_NG := 0;
                END_IF;
                IF CONSECUTIVE_NG >= MAX_CONSECUTIVE_NG THEN
                    NEXT_STATE := ST_ERROR;
                    LAST_ERROR_CODE := 7001;    (* consecutive NG batches *)
                ELSIF TRAY_FULL OR TRAY_ROW >= TRAY_ROWS THEN
                    NEXT_STATE := ST_TRAY_HANDLING;
                ELSIF NG_ON_BELT > 0 THEN
                    NEXT_STATE := ST_NG_UNLOAD;
                ELSIF STOP_REQUESTED OR NOT AUTO_MODE THEN
                    NEXT_STATE := ST_IDLE;
                ELSIF BASKET_EMPTY OR GROUPS_PICKED >= GROUPS_PER_BASKET THEN
                    NEXT_STATE := ST_BASKET_FEED;
                ELSE
                    NEXT_STATE := ST_POKAYOKE_CHECK;
                END_IF;
        END_CASE;

    (* ======================================================================= *)
    ST_TRAY_HANDLING:
        (* Steps 14, 16 (slides 28-29): gripper lifts the full-tray stack, the
           new full tray moves from position 1 to 2, gripper releases; the
           loader feeds one empty tray. *)
        IF STATE_ENTRY THEN
            STATE_ENTRY := FALSE;
        END_IF;

        CASE SUB_STEP OF
            0:
                SOL_STACK_GRIPPER := TRUE;
                SUB_STEP := 1;
            1:
                SOL_TRAY_PUSH_POS2 := TRUE;
                IF NOT TRAY_AT_POS1 THEN
                    SOL_STACK_GRIPPER := FALSE;
                    SOL_TRAY_PUSH_POS2 := FALSE;
                    SUB_STEP := 2;
                END_IF;
            2:
                SOL_TRAY_LOADER_FEED := TRUE;
                IF EMPTY_TRAY_PRESENT AND TRAY_AT_POS1 THEN
                    SOL_TRAY_LOADER_FEED := FALSE;
                    TRAY_ROW := 0;
                    TRAY_COL := 0;
                    SUB_STEP := 3;
                END_IF;
            3:
                IF NG_ON_BELT > 0 THEN
                    NEXT_STATE := ST_NG_UNLOAD;
                ELSIF STOP_REQUESTED OR NOT AUTO_MODE THEN
                    NEXT_STATE := ST_IDLE;
                ELSIF BASKET_EMPTY OR GROUPS_PICKED >= GROUPS_PER_BASKET THEN
                    NEXT_STATE := ST_BASKET_FEED;
                ELSE
                    NEXT_STATE := ST_POKAYOKE_CHECK;
                END_IF;
        END_CASE;

    (* ======================================================================= *)
    ST_NG_UNLOAD:
        (* Step 18 (slide 27): NG CSEs on the belt move to the holder bar; the
           NG check CCD reconfirms; the NG transfer (axis 6) unloads to the NG
           tray, or back to the OK tray when the NG is overturned. Loops until
           the belt is empty. *)
        IF STATE_ENTRY THEN
            STATE_ENTRY := FALSE;
        END_IF;

        CASE SUB_STEP OF
            0:
                IF NG_ON_BELT <= 0 THEN
                    SUB_STEP := 6;
                ELSE
                    SOL_NG_HOLDER_BAR := TRUE;
                    NG_BELT_RUN := TRUE;
                    SUB_STEP := 1;
                END_IF;
            1:
                IF NG_AT_HOLDER_BAR THEN
                    NG_BELT_RUN := FALSE;
                    VISION_SEQ := VISION_SEQ + 1;
                    VISION_PENDING[5] := TRUE;
                    VISION_TRIG[5] := TRUE;
                    SUB_STEP := 2;
                END_IF;
            2:
                IF NOT VISION_PENDING[5] THEN
                    NG_CONFIRMED := NOT VISION_RESULT[5, 1].JUDGE;
                    IF NG_CONFIRMED THEN
                        NG_COUNT := NG_COUNT + 1;
                    ELSE
                        FALSE_REJECT_COUNT := FALSE_REJECT_COUNT + 1;
                    END_IF;
                    IF ZONE_CLEAR[6] THEN
                        TRANSFER_ACTIVE[6] := TRUE;
                        AXIS[6].TARGET_EU := POS_T6_BELT_MM;
                        AXIS[6].TARGET_POS := REAL_TO_DINT(POS_T6_BELT_MM * PULSES_PER_MM);
                        AXIS[6].SPEED := 80000;
                        AXIS[6].ACCEL := 800000;
                        SUB_STEP := 3;
                    END_IF;
                END_IF;
            3:
                IF AT_TARGET[6] THEN
                    SOL_TRANSFER_GRIP[6] := TRUE;
                    SOL_NG_HOLDER_BAR := FALSE;
                    IF NG_CONFIRMED THEN
                        AXIS[6].TARGET_EU := POS_T6_NG_TRAY_MM;
                        AXIS[6].TARGET_POS := REAL_TO_DINT(POS_T6_NG_TRAY_MM * PULSES_PER_MM);
                    ELSE
                        AXIS[6].TARGET_EU := POS_T6_OK_TRAY_MM;
                        AXIS[6].TARGET_POS := REAL_TO_DINT(POS_T6_OK_TRAY_MM * PULSES_PER_MM);
                    END_IF;
                    SUB_STEP := 4;
                END_IF;
            4:
                IF AT_TARGET[6] THEN
                    SOL_TRANSFER_GRIP[6] := FALSE;
                    NG_ON_BELT := NG_ON_BELT - 1;
                    SUB_STEP := 5;
                END_IF;
            5:
                TRANSFER_ACTIVE[6] := FALSE;
                SUB_STEP := 0;
            6:
                IF STOP_REQUESTED OR NOT AUTO_MODE THEN
                    NEXT_STATE := ST_IDLE;
                ELSIF BASKET_EMPTY OR GROUPS_PICKED >= GROUPS_PER_BASKET THEN
                    NEXT_STATE := ST_BASKET_FEED;
                ELSE
                    NEXT_STATE := ST_POKAYOKE_CHECK;
                END_IF;
        END_CASE;

    (* ======================================================================= *)
    ST_ERROR:
        (* Safe stop. Every axis gets a controlled stop (AXIS_STOP -> drive
           control word), then motion, vacuum, shade, belt and camera handshakes
           are dropped. Holding-type outputs are kept so nothing falls:
           SOL_BASKET_STACK_HOLD, SOL_HOLDING_BLOCK, SOL_L_TRIGGER,
           SOL_BLUE_HOLDER_CLOSE, SOL_FLIP_180, SOL_STACK_GRIPPER, SOL_NG_HOLDER_BAR. *)
        IF STATE_ENTRY THEN
            FOR i := 1 TO 15 DO
                AXIS_STOP[i] := TRUE;
            END_FOR;
            FOR i := 1 TO 6 DO
                SOL_TRANSFER_GRIP[i] := FALSE;
                TRANSFER_ACTIVE[i] := FALSE;
            END_FOR;
            FOR i := 1 TO 5 DO
                VISION_TRIG[i] := FALSE;
                VISION_PENDING[i] := FALSE;
            END_FOR;
            FOR u := 1 TO 4 DO
                SOL_T5_RELEASE[u] := FALSE;
            END_FOR;
            SOL_PLATFORM_VACUUM := FALSE;
            SOL_NEST_VACUUM_CCD4 := FALSE;
            SOL_NEST_VACUUM_SIDE := FALSE;
            SOL_SHADE_FWD := FALSE;
            SOL_CCD1_REACH := FALSE;
            SOL_GRIPPER_LIFT := FALSE;
            SOL_LIFTER_UP := FALSE;
            SOL_EMPTY_BASKET_PUSH := FALSE;
            SOL_TRAY_PUSH_POS2 := FALSE;
            SOL_TRAY_LOADER_FEED := FALSE;
            NG_BELT_RUN := FALSE;
            ROBOT_START := FALSE;
            PY_TRIG := FALSE;
            FOR i := 11 TO 14 DO
                AXIS[i].REL_MODE := FALSE;
            END_FOR;
            CYCLE_RUNNING := FALSE;
            SUB_STEP := 0;
            STATE_ENTRY := FALSE;
        END_IF;

        (* operator acknowledges, then presses reset *)
        IF RT_RESET.Q AND ESTOP_OK AND SAFETY_GATE_CLOSED AND SERVO_STO_RELEASED THEN
            FOR i := 1 TO 15 DO
                AXIS_STOP[i] := FALSE;
            END_FOR;
            LAST_ERROR_CODE := 0;
            CONSECUTIVE_NG := 0;
            NEXT_STATE := ST_IDLE;
        END_IF;

END_CASE;

(* ---------------------------------------------------------------------------
 * STATE TRANSITION
 * ------------------------------------------------------------------------ *)
IF ERROR_REQ AND CURRENT_STATE <> ST_ERROR THEN
    NEXT_STATE := ST_ERROR;
    LAST_ERROR_CODE := ERROR_CODE_REQ;
END_IF;
ERROR_REQ := FALSE;
IF NEXT_STATE <> CURRENT_STATE THEN
    CURRENT_STATE := NEXT_STATE;
    STATE_ENTRY := TRUE;
    SUB_STEP := 0;
    SUB_STEP_PREV := 0;
END_IF;

(* ---------------------------------------------------------------------------
 * CYCLE TIME - per batch, for OEE (target 3.8 s per 4-unit batch)
 * ------------------------------------------------------------------------ *)
(* the timer restarts when a batch starts (Poka-Yoke entry) and is read when
   the batch reaches the sorting station *)
CYCLE_TIMER(IN := CYCLE_RUNNING AND NOT (CURRENT_STATE = ST_POKAYOKE_CHECK AND STATE_ENTRY),
            PT := T#60s);
IF CURRENT_STATE = ST_TRANSFER_5_SORT AND STATE_ENTRY THEN
    CYCLE_TIME_ACTUAL := CYCLE_TIMER.ET;
END_IF;

END_PROGRAM
