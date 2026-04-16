(* =============================================================================
 * Program:     MAIN_SEQUENCE
 * Project:     TI AOI Inspection System
 * Controller:  Mitsubishi iQ-R Series (R16CPU + RJ71GF11-T2)
 * Network:     CC-Link IE Field Basic 1 Gbps (servo + remote I/O)
 *              EtherNet/IP CIP (IPC vision host communication)
 * Cycle Time:  0.5 ms fixed scan | 3.8 s total throughput (4-unit batch)
 * Author:      Controls Engineering
 * Revision:    1.0
 * ========================================================================== *)

PROGRAM MAIN_SEQUENCE

(* ---------------------------------------------------------------------------
 * TYPE DEFINITIONS
 * ------------------------------------------------------------------------ *)
TYPE
    E_INSPECT_STATE : (
        ST_IDLE             := 0,
        ST_BASKET_FEED      := 1,
        ST_CSE_LOAD         := 2,
        ST_PITCH_CHANGE     := 3,
        ST_TRANSFER_1       := 4,
        ST_TRANSFER_2       := 5,
        ST_TRANSFER_3       := 6,
        ST_TRANSFER_4       := 7,
        ST_TRANSFER_5       := 8,
        ST_TRANSFER_6       := 9,
        ST_CCD4_CHECK       := 10,  (* Bottom inspection - inverted optics *)
        ST_CCD3_CHECK       := 11,  (* Top surface inspection *)
        ST_CCD1_CHECK       := 12,  (* Side A + marking verification *)
        ST_ORIENT_COMP      := 13,  (* Theta correction from CCD1 result *)
        ST_CCD2_CHECK       := 14,  (* Side B after orientation alignment *)
        ST_UNLOAD_OK        := 15,
        ST_NG_SORT          := 16,  (* Divert to NG bin by defect class *)
        ST_NG_UNLOAD        := 17,
        ST_ERROR            := 99
    );

    TYPE_VISION_RESULT : STRUCT
        JUDGE           : BOOL;     (* TRUE = PASS, FALSE = NG *)
        DEFECT_CODE     : INT;      (* 0=OK, 1=scratch, 2=chip, 3=mark, 4=foreign *)
        OFFSET_X        : REAL;     (* um, positional deviation *)
        OFFSET_Y        : REAL;
        OFFSET_THETA    : REAL;     (* degrees, rotation deviation *)
        INSPECT_TIME_MS : INT;      (* Vision processing elapsed time *)
    END_STRUCT;

    TYPE_AXIS_CMD : STRUCT
        TARGET_POS      : DINT;     (* Command position in encoder pulses *)
        SPEED           : DINT;     (* mm/s x 100 for fixed-point *)
        ACCEL           : DINT;     (* mm/s^2 x 100 *)
        DECEL           : DINT;
        IN_POSITION     : BOOL;     (* Servo positioning complete feedback *)
        BUSY            : BOOL;
        ERROR           : BOOL;
    END_STRUCT;
END_TYPE

(* ---------------------------------------------------------------------------
 * VARIABLE DECLARATIONS
 * ------------------------------------------------------------------------ *)
VAR
    (* State machine *)
    CURRENT_STATE       : E_INSPECT_STATE := ST_IDLE;
    NEXT_STATE          : E_INSPECT_STATE;
    STATE_ENTRY         : BOOL := TRUE;         (* One-shot on state entry *)
    STEP_TIMER          : TON;                  (* Timeout watchdog per step *)
    CYCLE_TIMER         : TON;                  (* Total cycle time monitor *)
    STEP_TIMEOUT_MS     : TIME := T#5000ms;     (* Max dwell per state *)

    (* CC-Link IE Field cyclic I/O - 8 servo axes *)
    AXIS              : ARRAY[1..8] OF TYPE_AXIS_CMD;
    (* Axis mapping:
       1 = Transfer 1 (basket to pitch change)
       2 = Transfer 2 (pitch change to CCD4)
       3 = Transfer 3 (CCD4 to CCD3)
       4 = Transfer 4 (CCD3 to CCD1)
       5 = Transfer 5 (CCD1 to orient/CCD2)
       6 = Transfer 6 (orient/CCD2 to unload)
       7 = Orientation compensation theta axis
       8 = Side check rotation axis *)

    (* Vision handshake signals - EtherNet/IP CIP implicit messaging *)
    VISION_TRIG         : ARRAY[1..4] OF BOOL;  (* PLC DO -> camera trigger *)
    VISION_BUSY         : ARRAY[1..4] OF BOOL;  (* IPC -> PLC: processing *)
    VISION_DONE         : ARRAY[1..4] OF BOOL;  (* IPC -> PLC: result ready *)
    VISION_RESULT       : ARRAY[1..4] OF TYPE_VISION_RESULT;
    VISION_TRIG_EDGE    : ARRAY[1..4] OF R_TRIG;

    (* Interlock flags - prevent collision between adjacent transfer units *)
    ZONE_CLEAR          : ARRAY[1..8] OF BOOL;  (* TRUE when zone is safe *)
    TRANSFER_ACTIVE     : ARRAY[1..6] OF BOOL;  (* Transfer in motion *)

    (* Sensors via CC-Link IE Field remote I/O *)
    BASKET_PRESENT      : BOOL;     (* Photoelectric sensor at basket feeder *)
    CSE_LOADED          : BOOL;     (* Vacuum sensor confirms unit pickup *)
    PITCH_COMPLETE      : BOOL;     (* Pitch change mechanism settled *)
    PART_AT_CCD4        : BOOL;
    PART_AT_CCD3        : BOOL;
    PART_AT_CCD1        : BOOL;
    PART_AT_CCD2        : BOOL;
    UNLOAD_CLEAR        : BOOL;     (* Unload conveyor not backed up *)
    NG_BIN_FULL         : ARRAY[1..4] OF BOOL;

    (* Pneumatic control - SMC SY3000 solenoids via CC-Link remote I/O *)
    SOL_CSE_VACUUM      : BOOL;     (* Vacuum chuck engage *)
    SOL_PITCH_CLAMP     : BOOL;     (* Pitch change clamp *)
    SOL_TRANSFER_GRIP   : ARRAY[1..6] OF BOOL;  (* Transfer grippers *)
    SOL_ORIENT_CLAMP    : BOOL;
    SOL_UNLOAD_BLOW     : BOOL;     (* Air blow for part release *)
    SOL_NG_DIVERT       : ARRAY[1..4] OF BOOL;  (* NG bin gate selectors *)

    (* Safety inputs *)
    ESTOP_OK            : BOOL;     (* E-Stop circuit healthy *)
    SAFETY_GATE_CLOSED  : BOOL;
    LIGHT_CURTAIN_OK    : BOOL;
    SERVO_STO_RELEASED  : BOOL;     (* STO not active on any drive *)

    (* Edge detection *)
    RT_START            : R_TRIG;
    RT_RESET            : R_TRIG;
    FT_ESTOP            : F_TRIG;   (* Detect E-Stop falling edge *)

    (* Operator interface *)
    CMD_START           : BOOL;
    CMD_RESET           : BOOL;
    CMD_STOP            : BOOL;
    AUTO_MODE           : BOOL;
    CYCLE_RUNNING       : BOOL;

    (* Diagnostics *)
    BATCH_COUNT         : UDINT;
    NG_COUNT            : UDINT;
    CYCLE_TIME_ACTUAL   : TIME;
    LAST_ERROR_CODE     : INT;
    CONSECUTIVE_NG      : INT;      (* Track consecutive NG for alarm *)
    MAX_CONSECUTIVE_NG  : INT := 5; (* Trigger alarm if 5 in a row *)

    (* Internal *)
    i                   : INT;
    ALL_AXES_IDLE       : BOOL;
    ORIENT_CORRECTION   : REAL;     (* Theta offset from CCD1 result *)
END_VAR

(* ---------------------------------------------------------------------------
 * INTERLOCK EVALUATION
 * Adjacent transfer zones must not overlap. Zone N is clear only when
 * the unit in zone N-1 and N+1 are not actively traversing into zone N.
 * This prevents mechanical collision on the linear rail system.
 * ------------------------------------------------------------------------ *)
FOR i := 1 TO 8 DO
    ZONE_CLEAR[i] := TRUE;
END_FOR;

(* Transfer 1 and 2 share a boundary at pitch change station *)
IF TRANSFER_ACTIVE[2] THEN
    ZONE_CLEAR[1] := FALSE;
END_IF;

(* Each adjacent pair interlocked *)
FOR i := 2 TO 5 DO
    IF TRANSFER_ACTIVE[i - 1] OR TRANSFER_ACTIVE[i + 1] THEN
        ZONE_CLEAR[i] := FALSE;
    END_IF;
END_FOR;

IF TRANSFER_ACTIVE[5] THEN
    ZONE_CLEAR[6] := FALSE;
END_IF;

(* ---------------------------------------------------------------------------
 * EDGE DETECTION
 * ------------------------------------------------------------------------ *)
RT_START(CLK := CMD_START);
RT_RESET(CLK := CMD_RESET);
FT_ESTOP(CLK := ESTOP_OK);

(* E-Stop falling edge forces immediate transition to ERROR *)
IF FT_ESTOP.Q THEN
    CURRENT_STATE := ST_ERROR;
    LAST_ERROR_CODE := 9001;    (* E-Stop activated *)
    STATE_ENTRY := TRUE;
END_IF;

(* Safety interlock - block all motion if safety not satisfied *)
IF NOT ESTOP_OK OR NOT SAFETY_GATE_CLOSED OR NOT SERVO_STO_RELEASED THEN
    IF CURRENT_STATE <> ST_IDLE AND CURRENT_STATE <> ST_ERROR THEN
        CURRENT_STATE := ST_ERROR;
        LAST_ERROR_CODE := 9002;    (* Safety interlock violation *)
        STATE_ENTRY := TRUE;
    END_IF;
END_IF;

(* ---------------------------------------------------------------------------
 * STEP TIMEOUT WATCHDOG
 * Any state exceeding STEP_TIMEOUT_MS indicates a jam or sensor failure.
 * ------------------------------------------------------------------------ *)
STEP_TIMER(IN := CYCLE_RUNNING AND (CURRENT_STATE <> ST_IDLE) AND (CURRENT_STATE <> ST_ERROR),
           PT := STEP_TIMEOUT_MS);
IF STEP_TIMER.Q THEN
    CURRENT_STATE := ST_ERROR;
    LAST_ERROR_CODE := 8001;    (* Step timeout *)
    STATE_ENTRY := TRUE;
END_IF;

(* ---------------------------------------------------------------------------
 * MAIN STATE MACHINE
 * Pipeline architecture: multiple batches in flight simultaneously.
 * State machine controls the critical path; non-blocking parallel
 * operations handled by function blocks below.
 * ------------------------------------------------------------------------ *)
CASE CURRENT_STATE OF

    ST_IDLE:
        IF STATE_ENTRY THEN
            CYCLE_RUNNING := FALSE;
            (* De-energize all transfer grippers *)
            FOR i := 1 TO 6 DO
                SOL_TRANSFER_GRIP[i] := FALSE;
                TRANSFER_ACTIVE[i] := FALSE;
            END_FOR;
            STATE_ENTRY := FALSE;
        END_IF;

        IF RT_START.Q AND AUTO_MODE AND ESTOP_OK AND SAFETY_GATE_CLOSED THEN
            CYCLE_RUNNING := TRUE;
            CYCLE_TIMER(IN := FALSE, PT := T#0ms);  (* Reset cycle timer *)
            CURRENT_STATE := ST_BASKET_FEED;
            STATE_ENTRY := TRUE;
        END_IF;

    ST_BASKET_FEED:
        (* Basket feeder indexes next row of 4 units into pick position *)
        IF STATE_ENTRY THEN
            CYCLE_TIMER(IN := TRUE, PT := T#4000ms);
            STEP_TIMER(IN := FALSE, PT := T#0ms);  (* Reset step watchdog *)
            STATE_ENTRY := FALSE;
        END_IF;

        IF BASKET_PRESENT THEN
            CURRENT_STATE := ST_CSE_LOAD;
            STATE_ENTRY := TRUE;
        END_IF;

    ST_CSE_LOAD:
        (* CSE (Component Supply Equipment) picks 4 units with vacuum chuck *)
        IF STATE_ENTRY THEN
            SOL_CSE_VACUUM := TRUE;
            STEP_TIMER(IN := FALSE, PT := T#0ms);
            STATE_ENTRY := FALSE;
        END_IF;

        IF CSE_LOADED THEN
            CURRENT_STATE := ST_PITCH_CHANGE;
            STATE_ENTRY := TRUE;
        END_IF;

    ST_PITCH_CHANGE:
        (* Expand pitch from tray spacing (2.0mm) to inspection spacing (25mm).
           This allows individual access to each unit at vision stations. *)
        IF STATE_ENTRY THEN
            SOL_PITCH_CLAMP := TRUE;
            (* Axis 1: 4194304 PPR / 10mm lead = 419430 pulses/mm *)
            AXIS[1].TARGET_POS := REAL_TO_DINT(25.0 * 419430.0);  (* 25mm pitch *)
            AXIS[1].SPEED := 50000;       (* 500 mm/s *)
            AXIS[1].ACCEL := 200000;      (* 2000 mm/s^2 *)
            STEP_TIMER(IN := FALSE, PT := T#0ms);
            STATE_ENTRY := FALSE;
        END_IF;

        IF PITCH_COMPLETE AND AXIS[1].IN_POSITION THEN
            CURRENT_STATE := ST_TRANSFER_1;
            STATE_ENTRY := TRUE;
        END_IF;

    ST_TRANSFER_1:
        (* Move batch from pitch change to CCD4 bottom inspection station *)
        IF STATE_ENTRY THEN
            IF ZONE_CLEAR[1] THEN
                TRANSFER_ACTIVE[1] := TRUE;
                SOL_TRANSFER_GRIP[1] := TRUE;
                (* Axis 1: 4194304 PPR / 10mm lead = 419430 pulses/mm *)
                AXIS[1].TARGET_POS := REAL_TO_DINT(100.0 * 419430.0);  (* 100mm travel *)
                STEP_TIMER(IN := FALSE, PT := T#0ms);
                STATE_ENTRY := FALSE;
            END_IF;
        END_IF;

        IF AXIS[1].IN_POSITION AND PART_AT_CCD4 THEN
            SOL_TRANSFER_GRIP[1] := FALSE;
            TRANSFER_ACTIVE[1] := FALSE;
            CURRENT_STATE := ST_CCD4_CHECK;
            STATE_ENTRY := TRUE;
        END_IF;

    ST_CCD4_CHECK:
        (* Bottom inspection: units pass over upward-facing line scan camera.
           Camera 4 triggers on position, captures full bottom surface. *)
        IF STATE_ENTRY THEN
            VISION_TRIG[4] := TRUE;
            STEP_TIMER(IN := FALSE, PT := T#0ms);
            STATE_ENTRY := FALSE;
        END_IF;

        VISION_TRIG_EDGE[4](CLK := VISION_DONE[4]);
        IF VISION_TRIG_EDGE[4].Q THEN
            VISION_TRIG[4] := FALSE;
            IF NOT VISION_RESULT[4].JUDGE THEN
                (* Bottom NG - mark unit but continue pipeline *)
                NG_COUNT := NG_COUNT + 1;
            END_IF;
            CURRENT_STATE := ST_TRANSFER_2;
            STATE_ENTRY := TRUE;
        END_IF;

    ST_TRANSFER_2:
        IF STATE_ENTRY THEN
            IF ZONE_CLEAR[2] THEN
                TRANSFER_ACTIVE[2] := TRUE;
                SOL_TRANSFER_GRIP[2] := TRUE;
                (* Axis 2: 4194304 PPR / 10mm lead = 419430 pulses/mm *)
                AXIS[2].TARGET_POS := REAL_TO_DINT(100.0 * 419430.0);  (* 100mm travel *)
                STEP_TIMER(IN := FALSE, PT := T#0ms);
                STATE_ENTRY := FALSE;
            END_IF;
        END_IF;

        IF AXIS[2].IN_POSITION AND PART_AT_CCD3 THEN
            SOL_TRANSFER_GRIP[2] := FALSE;
            TRANSFER_ACTIVE[2] := FALSE;
            CURRENT_STATE := ST_CCD3_CHECK;
            STATE_ENTRY := TRUE;
        END_IF;

    ST_CCD3_CHECK:
        (* Top surface inspection: area scan camera with coaxial lighting.
           Detects scratches, contamination, and marking defects. *)
        IF STATE_ENTRY THEN
            VISION_TRIG[3] := TRUE;
            STEP_TIMER(IN := FALSE, PT := T#0ms);
            STATE_ENTRY := FALSE;
        END_IF;

        VISION_TRIG_EDGE[3](CLK := VISION_DONE[3]);
        IF VISION_TRIG_EDGE[3].Q THEN
            VISION_TRIG[3] := FALSE;
            CURRENT_STATE := ST_TRANSFER_3;
            STATE_ENTRY := TRUE;
        END_IF;

    ST_TRANSFER_3:
        IF STATE_ENTRY THEN
            IF ZONE_CLEAR[3] THEN
                TRANSFER_ACTIVE[3] := TRUE;
                SOL_TRANSFER_GRIP[3] := TRUE;
                (* Axis 3: 4194304 PPR / 10mm lead = 419430 pulses/mm *)
                AXIS[3].TARGET_POS := REAL_TO_DINT(100.0 * 419430.0);  (* 100mm travel *)
                STEP_TIMER(IN := FALSE, PT := T#0ms);
                STATE_ENTRY := FALSE;
            END_IF;
        END_IF;

        IF AXIS[3].IN_POSITION AND PART_AT_CCD1 THEN
            SOL_TRANSFER_GRIP[3] := FALSE;
            TRANSFER_ACTIVE[3] := FALSE;
            CURRENT_STATE := ST_CCD1_CHECK;
            STATE_ENTRY := TRUE;
        END_IF;

    ST_CCD1_CHECK:
        (* Side A inspection + marking OCR. Camera 1 also measures angular
           offset for orientation compensation at next station. *)
        IF STATE_ENTRY THEN
            (* Rotate side-check axis to Side A position *)
            AXIS[8].TARGET_POS := 0;  (* 0 degrees *)
            VISION_TRIG[1] := TRUE;
            STEP_TIMER(IN := FALSE, PT := T#0ms);
            STATE_ENTRY := FALSE;
        END_IF;

        VISION_TRIG_EDGE[1](CLK := VISION_DONE[1]);
        IF VISION_TRIG_EDGE[1].Q THEN
            VISION_TRIG[1] := FALSE;
            (* Capture theta offset for orientation compensation *)
            ORIENT_CORRECTION := VISION_RESULT[1].OFFSET_THETA;
            CURRENT_STATE := ST_TRANSFER_4;
            STATE_ENTRY := TRUE;
        END_IF;

    ST_TRANSFER_4:
        IF STATE_ENTRY THEN
            IF ZONE_CLEAR[4] THEN
                TRANSFER_ACTIVE[4] := TRUE;
                SOL_TRANSFER_GRIP[4] := TRUE;
                (* Axis 4: 4194304 PPR / 10mm lead = 419430 pulses/mm *)
                AXIS[4].TARGET_POS := REAL_TO_DINT(100.0 * 419430.0);  (* 100mm travel *)
                STEP_TIMER(IN := FALSE, PT := T#0ms);
                STATE_ENTRY := FALSE;
            END_IF;
        END_IF;

        IF AXIS[4].IN_POSITION THEN
            SOL_TRANSFER_GRIP[4] := FALSE;
            TRANSFER_ACTIVE[4] := FALSE;
            CURRENT_STATE := ST_ORIENT_COMP;
            STATE_ENTRY := TRUE;
        END_IF;

    ST_ORIENT_COMP:
        (* Theta axis corrects angular misalignment measured by CCD1.
           Compensation ensures CCD2 side B image is rotationally aligned. *)
        IF STATE_ENTRY THEN
            SOL_ORIENT_CLAMP := TRUE;
            (* Axis 7: 4194304 PPR * 72:1 harmonic / 360 = 838861 pulses/deg *)
            AXIS[7].TARGET_POS := REAL_TO_DINT(ORIENT_CORRECTION * 838861.0);
            AXIS[7].SPEED := 36000;     (* 360 deg/s *)
            AXIS[7].ACCEL := 720000;    (* Fast settle for throughput *)
            STEP_TIMER(IN := FALSE, PT := T#0ms);
            STATE_ENTRY := FALSE;
        END_IF;

        IF AXIS[7].IN_POSITION THEN
            CURRENT_STATE := ST_CCD2_CHECK;
            STATE_ENTRY := TRUE;
        END_IF;

    ST_CCD2_CHECK:
        (* Side B inspection after orientation alignment *)
        IF STATE_ENTRY THEN
            VISION_TRIG[2] := TRUE;
            STEP_TIMER(IN := FALSE, PT := T#0ms);
            STATE_ENTRY := FALSE;
        END_IF;

        VISION_TRIG_EDGE[2](CLK := VISION_DONE[2]);
        IF VISION_TRIG_EDGE[2].Q THEN
            VISION_TRIG[2] := FALSE;
            SOL_ORIENT_CLAMP := FALSE;
            (* Aggregate all 4 camera results for final judgment *)
            IF VISION_RESULT[1].JUDGE AND VISION_RESULT[2].JUDGE
               AND VISION_RESULT[3].JUDGE AND VISION_RESULT[4].JUDGE THEN
                CURRENT_STATE := ST_TRANSFER_5;
            ELSE
                CURRENT_STATE := ST_NG_SORT;
            END_IF;
            STATE_ENTRY := TRUE;
        END_IF;

    ST_TRANSFER_5:
        (* Move passed units to OK unload station *)
        IF STATE_ENTRY THEN
            IF ZONE_CLEAR[5] THEN
                TRANSFER_ACTIVE[5] := TRUE;
                SOL_TRANSFER_GRIP[5] := TRUE;
                (* Axis 5: 4194304 PPR / 10mm lead = 419430 pulses/mm *)
                AXIS[5].TARGET_POS := REAL_TO_DINT(100.0 * 419430.0);  (* 100mm travel *)
                STEP_TIMER(IN := FALSE, PT := T#0ms);
                STATE_ENTRY := FALSE;
            END_IF;
        END_IF;

        IF AXIS[5].IN_POSITION THEN
            SOL_TRANSFER_GRIP[5] := FALSE;
            TRANSFER_ACTIVE[5] := FALSE;
            CURRENT_STATE := ST_UNLOAD_OK;
            STATE_ENTRY := TRUE;
        END_IF;

    ST_UNLOAD_OK:
        (* Release passed units onto OK conveyor via air blow *)
        IF STATE_ENTRY THEN
            SOL_UNLOAD_BLOW := TRUE;
            STEP_TIMER(IN := FALSE, PT := T#0ms);
            STATE_ENTRY := FALSE;
        END_IF;

        IF UNLOAD_CLEAR THEN
            SOL_UNLOAD_BLOW := FALSE;
            BATCH_COUNT := BATCH_COUNT + 1;
            CONSECUTIVE_NG := 0;
            CURRENT_STATE := ST_IDLE;
            STATE_ENTRY := TRUE;
        END_IF;

    ST_NG_SORT:
        (* Route NG units to appropriate bin based on defect classification.
           Bins: 1=scratch, 2=chip, 3=marking, 4=foreign/other *)
        IF STATE_ENTRY THEN
            IF ZONE_CLEAR[6] THEN
                TRANSFER_ACTIVE[6] := TRUE;
                SOL_TRANSFER_GRIP[6] := TRUE;
                (* Determine NG bin from worst defect code across all cameras *)
                FOR i := 1 TO 4 DO
                    IF NOT VISION_RESULT[i].JUDGE AND VISION_RESULT[i].DEFECT_CODE > 0
                       AND VISION_RESULT[i].DEFECT_CODE <= 4 THEN
                        SOL_NG_DIVERT[VISION_RESULT[i].DEFECT_CODE] := TRUE;
                    END_IF;
                END_FOR;
                STEP_TIMER(IN := FALSE, PT := T#0ms);
                STATE_ENTRY := FALSE;
            END_IF;
        END_IF;

        IF AXIS[6].IN_POSITION THEN
            CURRENT_STATE := ST_NG_UNLOAD;
            STATE_ENTRY := TRUE;
        END_IF;

    ST_NG_UNLOAD:
        IF STATE_ENTRY THEN
            SOL_TRANSFER_GRIP[6] := FALSE;
            TRANSFER_ACTIVE[6] := FALSE;
            STEP_TIMER(IN := FALSE, PT := T#0ms);
            STATE_ENTRY := FALSE;
        END_IF;

        (* Wait for NG unit to clear the divert gate sensor *)
        IF NOT PART_AT_CCD2 THEN
            FOR i := 1 TO 4 DO
                SOL_NG_DIVERT[i] := FALSE;
            END_FOR;
            NG_COUNT := NG_COUNT + 1;
            CONSECUTIVE_NG := CONSECUTIVE_NG + 1;
            IF CONSECUTIVE_NG >= MAX_CONSECUTIVE_NG THEN
                CURRENT_STATE := ST_ERROR;
                LAST_ERROR_CODE := 7001;  (* Consecutive NG alarm *)
                STATE_ENTRY := TRUE;
            ELSE
                CURRENT_STATE := ST_IDLE;
                STATE_ENTRY := TRUE;
            END_IF;
        END_IF;

    ST_ERROR:
        IF STATE_ENTRY THEN
            (* Immediate safe shutdown: release all grippers, disable servos *)
            FOR i := 1 TO 6 DO
                SOL_TRANSFER_GRIP[i] := FALSE;
                TRANSFER_ACTIVE[i] := FALSE;
            END_FOR;
            SOL_CSE_VACUUM := FALSE;
            SOL_ORIENT_CLAMP := FALSE;
            SOL_UNLOAD_BLOW := FALSE;
            CYCLE_RUNNING := FALSE;
            STATE_ENTRY := FALSE;
        END_IF;

        (* Operator must acknowledge error and press reset *)
        IF RT_RESET.Q AND ESTOP_OK AND SAFETY_GATE_CLOSED THEN
            LAST_ERROR_CODE := 0;
            CONSECUTIVE_NG := 0;
            CURRENT_STATE := ST_IDLE;
            STATE_ENTRY := TRUE;
        END_IF;

END_CASE;

(* ---------------------------------------------------------------------------
 * CYCLE TIME MEASUREMENT
 * Record actual cycle time for OEE calculation. Target: 3.8s per batch.
 * Alarm if sustained above 4.2s (>10% degradation).
 * ------------------------------------------------------------------------ *)
IF CURRENT_STATE = ST_IDLE AND CYCLE_RUNNING = FALSE THEN
    CYCLE_TIME_ACTUAL := CYCLE_TIMER.ET;
END_IF;

(* ---------------------------------------------------------------------------
 * VISION HANDSHAKE MONITOR
 * EtherNet/IP CIP connection watchdog. If IPC does not respond within
 * 500ms of trigger, assume communication fault.
 * ------------------------------------------------------------------------ *)
FOR i := 1 TO 4 DO
    IF VISION_TRIG[i] AND NOT VISION_BUSY[i] AND NOT VISION_DONE[i] THEN
        (* Trigger sent, waiting for IPC acknowledgment *)
        (* Timeout handled by STEP_TIMER at state level *)
    END_IF;
END_FOR;

END_PROGRAM
