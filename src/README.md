# AOI System for TI CSE Semiconductor Products -- Source Code

Automated Optical Inspection system designed for Texas Instruments CSE
(Chip Scale Encapsulated) semiconductor packages. The system inspects
19 defect categories using 4 CCD cameras at a throughput exceeding
85,000 units per day.

Author: Rongxuan Zhou


## Directory Structure

```
src/
  data_types/
    defect_types.py        Enumerations and dataclasses shared across
                           all modules: 19 DefectType categories,
                           DefectSeverity tiers, InspectionResult,
                           CameraID, ProcessStep.

  global_variables/
    system_config.py       Hardware constants and tuning parameters:
                           camera specs (resolution, FOV, WD per CCD),
                           detection thresholds, transfer axis positions,
                           timing budgets, production rate targets.

  inspection/
    InspectionSequencer.py Master state machine orchestrating the 18-step
                           process (IDLE through NG_SORTING). Owns all
                           subsystem controllers and sequences their
                           operations. Implements NG double-check logic.

  vision/
    CameraController.py    Multi-CCD camera manager wrapping the Hikrobot
                           MVS SDK (simulated). Controls 3x MV-GE501GC
                           and 1x MV-GE2000C with hardware trigger,
                           exposure, gain, and ROI configuration.

    LightingCheckAnalyzer.py
                           CCD#4 closed-chamber analysis pipeline:
                           sapphire glass ROI extraction, histogram-based
                           light leakage detection, edge staining check,
                           yellow glass cement detection.

    DefectClassifier.py    Routes each CCD image to the appropriate set
                           of detection algorithms covering all 19 defect
                           types. Aggregates results with severity.

    OrientationDetector.py Poka-Yoke orientation detection. Pre-load
                           check (pin position, code orientation, 180-deg
                           flip decision) and fine orientation compensation
                           angle computation for CCD#2 side check.

  material_handling/
    RobotController.py     Epson SCARA robot interface: dual vacuum nozzle
                           control, 4-unit pick cycles, 90-deg rotation,
                           TCP/IP communication with Epson RC+ controller.

    TransferControl.py     Six linear transfer axes with vacuum grippers.
                           Coordinates station-to-station hand-offs.
                           Transfer#2 triggers CCD#3 during motion;
                           Transfer#4 applies servo orientation compensation.

    PitchChanger.py        E-cylinder pitch expansion mechanism. Converts
                           compact basket spacing (2.54 mm) to inspection
                           spacing (12 mm). Supports 180-deg flip mode.

  ng_management/
    NGSorter.py            NG double-check and sorting. Queues NG-flagged
                           units for NG Check CCD reconfirmation. Routes
                           confirmed NG to tray via conveyor. Tracks
                           defect statistics by category and severity.
```


## Key Design Decisions

- **Typed throughout**: all public APIs use Python type hints; shared
  data structures are defined as `dataclass` or `Enum` in `data_types/`.
- **Simulated hardware layer**: camera SDK calls and robot TCP
  communication are abstracted behind simulation-capable wrappers so the
  codebase runs without physical hardware for development and testing.
- **NG double-check protocol**: every NG decision is verified by a
  secondary inspection pass to minimise false rejects and protect yield.
- **Pipelined throughput**: the sequencer is designed for batch processing
  (4 units/cycle) with concurrent transfer and capture operations to
  meet the >85K units/day target.
