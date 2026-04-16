<div align="center">

# AOI System for TI CSE Semiconductor

### 4-CCD Multi-Angle Automated Optical Inspection

[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![Platform: Python 3.10+](https://img.shields.io/badge/Python-3.10%2B-3776AB.svg?logo=python&logoColor=white)](https://www.python.org)
[![Vision: GigE Vision 2.0](https://img.shields.io/badge/Vision-GigE%20Vision%202.0-00979D.svg)](https://www.visiononline.org)
[![Camera: Hikrobot](https://img.shields.io/badge/Camera-Hikrobot%20MV--GE-green.svg)](https://www.hikrobotics.com)
[![Robot: Epson SCARA](https://img.shields.io/badge/Robot-Epson%20SCARA-orange.svg)](https://epson.com/industrial-robots)
[![Fieldbus: CC-Link IE](https://img.shields.io/badge/Fieldbus-CC--Link%20IE%20Field-7B2D8B.svg)](https://www.cc-link.org)
[![Safety: ISO 13849 PLd](https://img.shields.io/badge/Safety-ISO%2013849%20PLd-red.svg)](#safety-architecture)
[![Defects: 19 Categories](https://img.shields.io/badge/Defects-19%20Categories-critical.svg)](#defect-classification)

*Production-grade automated optical inspection system for Texas Instruments CSE (Chip Scale Element)*
*semiconductor packages. 4 industrial CCD cameras, 19 defect categories, 85,000+ units/day throughput.*

---

[Architecture](#system-architecture) · [Process Flow](#process-flow) · [Vision System](#vision-system) · [Source Code](#source-code) · [Documentation](#documentation) · [Tech Stack](#technology-stack)

</div>

<br/>

## Machine Overview

<p align="center">
  <img src="docs/images/pdf_pages/ccd_deployment_specs.png" width="48%" alt="Machine Overview - 3D View with Dimensions"/>
  <img src="docs/images/pdf_pages/machine_enclosure_exploded.png" width="48%" alt="Machine Overview - Enclosure and Exploded View"/>
</p>

<p align="center">
  <img src="docs/images/pdf_pages/machine_3d_overview.png" width="70%" alt="Machine Overview - 18 Station Layout"/>
</p>

<p align="center">
  <em>Top left: 3D view with dimensions (1800 x 1600 x 2000 mm), HMI, Tri-Color indicator, optical grating.
  Top right: Enclosure and exploded internal layout.
  Bottom: All 18 numbered stations -- Loading (1-4), Inspection (5-11), Output (12-16), NG Path (17-18).</em>
</p>

---

## Overview

This project implements the complete vision inspection and material handling control system for a **4-CCD multi-angle Automated Optical Inspection (AOI) machine** designed for Texas Instruments CSE semiconductor products. CSE packages are circular ceramic carriers with conductive pins on the underside and laser-marked identification codes on the top surface. The system performs 100% inline inspection across 19 defect categories with zero escapes on provided reference samples.

<table>
<tr><td>

**What it does**

- Inspects CSE packages from four angles (top, side, bottom, internal lighting)
- Classifies 19 defect types across Function, Cosmetic, Assembly, and Alignment groups
- Performs closed-chamber lighting checks through sapphire glass and glass cover
- Applies NG double-check reconfirmation to minimize false rejects at high volume
- Compensates CSE orientation via servo rotation before side inspection

</td><td>

**How it works**

- Epson SCARA robot loads CSE with Poka-Yoke orientation check
- Pitch change mechanism with e-cylinder and 180-degree flip positions parts
- 6 transfer stations move CSE through the 18-step inspection sequence
- CCD #1/#2/#3 perform surface and geometry checks at 0.0115 mm/pixel
- CCD #4 performs lighting check in sealed chamber at 0.0069 mm/pixel

</td></tr>
</table>

### Key Specifications

| | |
|---|---|
| **Product** | Texas Instruments CSE (Chip Scale Element) -- circular ceramic package with pins |
| **Machine Dimensions** | 1800 x 1600 x 2000 mm |
| **Throughput** | > 85,000 units/day (pipelined cycle: 3.8s per 4-unit batch) |
| **Detection Rate** | 100% on provided reference samples |
| **Defect Categories** | 19 total -- 8 Function, 4 Cosmetic, 5 Assembly, 2 Alignment |
| **Controller** | IPC (i7-12700) + PLC (2ms scan) + Epson RC700-A + Gardasoft RT820F-20 |
| **Vision Network** | GigE Vision 2.0, VLAN-isolated, Jumbo Frame, hardware-triggered |
| **Fieldbus** | CC-Link IE Field (1 Gbps, 0.5ms cyclic) for servo drives and remote I/O |
| **PLC Communication** | EtherNet/IP (CIP implicit messaging, 10ms cycle) |
| **Safety** | ISO 13849-1 PLd, Category 3 architecture, STO on all servo drives |

---

## Vision System

<p align="center">
  <img src="docs/images/pdf_pages/testing_report_defect_matrix.png" width="80%" alt="Visual Systems Deploy - CCD1-4 Specifications"/>
</p>

<p align="center">
  <em>4-CCD deployment specifications: CCD#1 Top, CCD#2 Side, CCD#3 Bottom (MV-GE501GC, 0.0115mm/px), CCD#4 Inner lighting check (MV-GE2000C, 0.0069mm/px).</em>
</p>

| Camera | Model | Lens | Light Source | Resolution | FOV | WD | Function |
|:-------|:------|:-----|:------------|:-----------|:----|:---|:---------|
| CCD #1 | MV-GE501GC | WWK03-110-230 | DN-COS60-W (coaxial) | 0.0115 mm/px | 28 x 24 mm | 110 +/- 2 mm | Top surface, markings, misalignment |
| CCD #2 | MV-GE501GC | WWK03-110-230 | DN-2BS32738-W (bar) | 0.0115 mm/px | 28 x 24 mm | 110 +/- 2 mm | Side + pin inspection, 360-deg rotation |
| CCD #3 | MV-GE501GC | WWK03-110-230 | DN-COS60-W (coaxial) | 0.0115 mm/px | 28 x 24 mm | 110 +/- 2 mm | Bottom surface, epoxy, cracks |
| CCD #4 | MV-GE2000C-T1P-C | DTCM110-48 (telecentric) | DN-HSP25-W (hyper spot) | 0.0069 mm/px | 37.9 x 25.3 mm | 140 +/- 3 mm | Lighting check in closed chamber |

---

## Defect Classification

| Group | Defects | CCD |
|:------|:--------|:----|
| **Function (8)** | Lighting Check, Crack\*, Broken\*, Epoxy Exposal, Pin Missing, Electrical Contamination, Gold Exposal, Insufficient Epoxy | #1, #2, #3, #4 |
| **Cosmetic (4)** | Dyeing Contamination, Non-Electrical Contamination, No Code, Code Blur | #1 |
| **Assembly (5)** | Pin Bent\*, Pin Oxidized, Pin Bur\*, Pin Mis-cut, Epoxy Higher Than Ceramic | #1, #2 |
| **Alignment (2)** | Misalignment\*, Staining on Edge | #4 |

\* = Critical defect (zero tolerance, immediate reject)

---

## System Architecture

```mermaid
flowchart TB
    subgraph LOADING["Loading Area"]
        LB["<b>Loading Basket</b><br/><i>Manual basket stack</i>"]
        SBF["<b>Single Basket Feeding</b><br/><i>Cylinder + L-trigger + lifter</i>"]
    end

    subgraph ROBOT["CSE Loading"]
        SCARA["<b>Epson SCARA Robot</b><br/><i>Dual nozzle, 4 CSE/cycle</i><br/><i>Poka-Yoke orientation check</i>"]
    end

    subgraph PITCH["Pitch Change"]
        PC["<b>Pitch Change Mechanism</b><br/><i>E-cylinder, blue holder</i><br/><i>180-degree flip (1st case)</i>"]
    end

    subgraph INSPECTION["Inspection Stations"]
        T1["<b>Transfer #1</b>"]
        CCD4["<b>CCD #4 -- Lighting Check</b><br/><i>Closed chamber + sapphire glass</i><br/><i>0.0069 mm/pixel</i>"]
        CCD3["<b>CCD #3 -- Bottom Check</b><br/><i>0.0115 mm/pixel</i>"]
        CCD1["<b>CCD #1 -- Top Check</b><br/><i>0.0115 mm/pixel</i>"]
        OC["<b>Orientation Compensation</b><br/><i>Servo motor rotation</i>"]
        CCD2["<b>CCD #2 -- Side Check</b><br/><i>360-degree rotation</i><br/><i>0.0115 mm/pixel</i>"]
        T5["<b>Transfer #5</b>"]
    end

    subgraph NG_PATH["NG Path"]
        NGC["<b>NG Check CCD</b><br/><i>Reconfirm before sorting</i>"]
        NGV["<b>NG Conveyor</b>"]
        NGU["<b>NG Unloading</b>"]
    end

    subgraph UNLOADING["Unloading Area"]
        UT["<b>Unloading Tray</b>"]
        FTS["<b>Full Tray Stack</b>"]
        MU["<b>Manual Unloading</b>"]
    end

    LB --> SBF
    SBF --> SCARA
    SCARA --> PC
    PC --> T1
    T1 --> CCD4
    CCD4 --> CCD3
    CCD3 --> CCD1
    CCD1 --> OC
    OC --> CCD2
    CCD2 --> T5
    T5 --> UT
    UT --> FTS
    FTS --> MU

    T5 -- "NG Detected" --> NGC
    NGC -- "NG Confirmed" --> NGV
    NGV --> NGU
    NGC -- "False Reject" --> UT
```

---

## Process Flow

<p align="center">
  <img src="docs/images/pdf_pages/machine_exploded_view.png" width="90%" alt="Process Flow - Top-down Layout and Flowchart"/>
</p>

<p align="center">
  <em>18-step process flow: top-down machine layout (left) with numbered station positions, and corresponding step-by-step flowchart (right) showing OK and NG paths.</em>
</p>

---

## Documentation

Detailed technical documentation covering every subsystem:

| Document | Topics | Key Content |
|:---------|:-------|:------------|
| **[System Architecture](docs/system-architecture.md)** | Controller hierarchy, communication, I/O | ISA-95 3-tier architecture, GigE Vision 2.0, CC-Link IE Field, EtherNet/IP CIP, VLAN network topology, I/O allocation (128 DI + 48 DO), safety relay architecture |
| **[Process Flow](docs/process-flow.md)** | 18-step sequence, timing, pipelining | Cycle time analysis (3.8s/4-unit batch), station-by-station breakdown, concurrent operation scheduling, throughput calculation |
| **[Vision System](docs/vision-system.md)** | Cameras, optics, illumination, calibration | 4-CCD heterogeneous configuration, Gardasoft RT820F-20 strobe controller, GenICam interface, spatial/illumination calibration pipeline |
| **[Defect Classification](docs/defect-classification.md)** | 19 defect categories, detection algorithms | Per-defect detection method, CCD assignment, severity classification, testing report (100% on all samples) |
| **[Mechanical Design](docs/mechanical-design.md)** | Mechanisms, fixtures, chamber design | Pitch change e-cylinder, SCARA dual nozzle, closed chamber sapphire glass assembly, 360-deg rotation stage, basket feeding mechanism |
| **[Safety Design](docs/safety-design.md)** | E-Stop, light curtains, interlocks | ISO 13849-1 PLd Category 3, Pilz PNOZ safety relay, STO on servo drives, optical grating (IEC 61496 Type 2) |

### Architecture Diagrams

| Diagram | Format | Description |
|:--------|:-------|:------------|
| **[System Architecture Diagram](docs/diagrams/system-architecture.md)** | HTML/CSS (Steel Blue) | 6-layer architecture with sidebars: Material Handling, Transfer, Vision, NG Management, Output, Safety |
| **[Process Flow Diagram](docs/diagrams/process-flow.md)** | HTML/CSS (Pipeline) | 18-step pipeline with color-coded stages: Loading, 4-CCD Inspection, Side Inspection, OK Output, NG Path |
| **[Defect Taxonomy](docs/diagrams/defect-taxonomy.md)** | PlantUML Mindmap | 19 defect categories organized by group, color-coded by severity (critical / needs sample / standard) |

---

## Source Code

> **C++ 17 / IEC 61131-3 ST** -- Real-time vision pipeline and PLC control logic. C++ handles performance-critical image acquisition and defect detection via Hikrobot MVS SDK. PLC structured text implements the 18-step inspection state machine and safety logic.

| Module | File | Description |
|:-------|:-----|:------------|
| **Vision (C++)** | [`GigEVisionCapture.cpp`](src/vision/GigEVisionCapture.cpp) | GigE Vision 2.0 acquisition, HW trigger, Jumbo Frame, lock-free ring buffer |
| | [`ImagePreprocessor.cpp`](src/vision/ImagePreprocessor.cpp) | Bayer demosaic, CLAHE, flat-field correction, sapphire glass compensation, cylindrical unwrap |
| | [`DefectDetector.cpp`](src/vision/DefectDetector.cpp) | 19-category detection engine: template matching, blob analysis, pin geometry, code OCR |
| | [`pybind_module.cpp`](src/vision/pybind_module.cpp) | pybind11 bindings for offline analysis tools |
| | [`CMakeLists.txt`](src/vision/CMakeLists.txt) | Build config: OpenCV 4.x, Hikrobot MVS SDK, pybind11 |
| **PLC (ST)** | [`main_sequence.st`](src/plc/main_sequence.st) | 18-step inspection state machine, CC-Link IE Field cyclic I/O, vision trigger handshake |
| | [`safety_program.st`](src/plc/safety_program.st) | ISO 13849-1 PLd Cat.3 safety logic, E-Stop, light curtain, STO |
| | [`servo_config.st`](src/plc/servo_config.st) | 8-axis servo profiles, homing sequences, CC-Link IE Field drive mapping |
| | [`io_mapping.csv`](src/plc/io_mapping.csv) | I/O allocation table: 128 DI + 48 DO |

### Offline Tools

> **Python 3.10+** -- Non-real-time utilities for calibration, data analysis, and offline simulation. Not part of the production control path.

| Tool | File | Description |
|:-----|:-----|:------------|
| **Calibration** | [`camera_calibration.py`](tools/calibration/camera_calibration.py) | Spatial and illumination calibration pipeline for 4-CCD system |
| | [`orientation_calibration.py`](tools/calibration/orientation_calibration.py) | Poka-Yoke orientation model training and compensation angle calculation |
| **Analysis** | [`defect_classification_tool.py`](tools/analysis/defect_classification_tool.py) | Offline defect classification validation and threshold tuning |
| | [`lighting_check_analysis.py`](tools/analysis/lighting_check_analysis.py) | CCD #4 light leakage analysis and reporting |
| | [`ng_statistics.py`](tools/analysis/ng_statistics.py) | NG rate analysis, per-defect statistics, SPC charts |
| **Simulation** | [`inspection_sequence_sim.py`](tools/simulation/inspection_sequence_sim.py) | Offline simulation of 18-step inspection sequence and timing analysis |

---

## Configuration Files

Hardware configuration files for each inspection station:

| File | Description |
|:-----|:------------|
| [`ccd1_top_check.yaml`](config/vision-system/ccd1_top_check.yaml) | CCD #1 -- exposure, gain, ROI, 9 defect thresholds, coaxial illumination profile |
| [`ccd2_side_check.yaml`](config/vision-system/ccd2_side_check.yaml) | CCD #2 -- 360-deg rotation (36 frames), pin geometry tolerances, cylindrical unwrap config |
| [`ccd3_bottom_check.yaml`](config/vision-system/ccd3_bottom_check.yaml) | CCD #3 -- dark chamber enclosure, CLAHE preprocessing, bottom surface defect thresholds |
| [`ccd4_lighting_check.yaml`](config/vision-system/ccd4_lighting_check.yaml) | CCD #4 -- sealed chamber sequence (shade close, DUT power, dark/lit frame), telecentric lens config |
| [`illumination_profiles.yaml`](config/lighting/illumination_profiles.yaml) | Gardasoft RT820F-20 4-channel profiles -- per-station power/strobe/current/delay settings |

---

## Technology Stack

| Layer | Components |
|:------|:-----------|
| **Vision** | Hikrobot MV-GE501GC (x3) + MV-GE2000C-T1P-C (x1), GigE Vision 2.0 (Jumbo Frame, HW trigger), Hikrobot MVS 4.x SDK (GenICam / GenTL) |
| **Optics** | WWK03-110-230 (x3), DTCM110-48 telecentric (x1), Gardasoft RT820F-20 4-channel strobe controller |
| **Illumination** | DN-COS60-W coaxial (x2), DN-2BS32738-W dual bar (x1), DN-HSP25-W hyper spot (x1) |
| **Material Handling** | Epson SCARA (RC700-A controller, SPEL+ TCP/IP), pitch change e-cylinder, 6 linear transfer axes |
| **Control** | IPC (i7-12700 / 32GB / NVMe) + PLC (2ms scan, CC-Link IE Field) + EtherNet/IP CIP |
| **Fieldbus** | CC-Link IE Field (1 Gbps, 0.5ms cyclic) -- 8 servo drives + remote I/O station |
| **Safety** | ISO 13849-1 PLd Cat.3, Pilz PNOZ safety relay, IEC 61496 Type 2 light curtains, ISO 13850 E-Stop |
| **Pneumatics** | SMC SY3000 valve manifold (19 solenoid valves), 0.4-0.6 MPa, vacuum generators |

---

<div align="center">

**Apache License 2.0** -- See [LICENSE](LICENSE) for details.

</div>
