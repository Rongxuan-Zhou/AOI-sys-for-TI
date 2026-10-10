# System Architecture

## 1. Machine Physical Specifications

| Parameter | Value |
|-----------|-------|
| Overall Dimensions (L x W x H) | 1800 x 1600 x 2000 mm |
| Frame Construction | Welded steel base plate + aluminum extrusion enclosure frame |
| Base Support | Heavy-duty roller casters with 4x adjustable leveling feet (M16 thread) |
| Weight | Approx. 1200 kg (fully loaded) |
| Access | Front-loading operator interface, rear maintenance panel with interlocked doors |
| Facility Requirements | AC 220V/380V 3-phase, 0.4-0.6 MPa compressed air, -0.06 MPa vacuum |

---

## 2. Controller Hierarchy

The system follows a three-tier industrial automation architecture compliant with ISA-95 / IEC 62264:

```
Level 2  ─ Supervisory ──────────────────────────────────────────
           │  Industrial PC (IPC)
           │  - Vision pipeline orchestration
           │  - Recipe management and HMI server
           │  - MES interface (future-ready)
           │
Level 1  ─ Cell Control ─────────────────────────────────────────
           │  Motion Controller / PLC
           │  - 18-step sequence state machine
           │  - I/O scan cycle: 2 ms deterministic
           │  - Pneumatic valve sequencing
           │  - Safety logic (SIL 2 capable)
           │
           │  Epson RC+ Robot Controller
           │  - 4-DOF SCARA motion planning
           │  - Dual vacuum nozzle end-effector I/O
           │  - Built-in I/O for gripper + sensor
           │
           │  Gardasoft RT820F-20 Lighting Controller
           │  - 4-channel programmable strobe
           │  - Hardware trigger synchronized
           │  - Per-channel current/duration profiles
           │
Level 0  ─ Field Devices ───────────────────────────────────────
              - 4x Hikrobot GigE cameras
              - 6x Transfer linear actuators (servo/stepper)
              - 1x 360-deg rotation servo motor
              - Pneumatic solenoid valve manifold (SMC SY series)
              - Vacuum generators + ejectors
              - Proximity / photoelectric sensors
              - Optical grating (light curtain)
              - E-Stop relays (safety-rated)
```

### 2.1 Industrial PC (IPC)

| Parameter | Specification |
|-----------|--------------|
| Platform | Advantech IPC-610 or equivalent rackmount |
| CPU | Intel Core i7-12700 (12C/20T) |
| RAM | 32 GB DDR4 |
| Storage | 512 GB NVMe SSD (OS + App) + 2 TB HDD (image archive) |
| OS | Windows 10 IoT Enterprise LTSC 2021 |
| Network | 2x Intel GbE (1x GigE Vision dedicated, 1x control/HMI) |
| GPU | NVIDIA T400 4GB (optional, for CNN-based defect detection) |
| Vision SDK | Hikrobot MVS 4.x (GenICam / GigE Vision 2.0 compliant) |

### 2.2 Motion Controller / PLC

| Parameter | Specification |
|-----------|--------------|
| Platform | Keyence KV-8000 or Mitsubishi iQ-R Series |
| I/O Scan Cycle | 2 ms (deterministic) |
| Digital I/O | 148 DI + 78 DO (see `src/plc/io_mapping.csv`) |
| Analog I/O | 8 AI + 4 AO (pressure, vacuum monitoring) |
| Communication | EtherNet/IP (to IPC), CC-Link IE Field (to servo drives), RS-232C (to Epson) |
| Motion Control | 15-axis positioning: 6 transfers, 4 orientation theta units on Transfer #4, 4 side-check rotators, 1 pitch e-cylinder |
| Safety CPU | Integrated safety controller (SIL 2 / PLd per ISO 13849-1) |

### 2.3 Epson SCARA Robot Controller

| Parameter | Specification |
|-----------|--------------|
| Controller | Epson RC700-A |
| Robot | Epson T6-602S (or equivalent 4-DOF SCARA) |
| Reach | 600 mm |
| Repeatability | +/- 0.02 mm |
| End-Effector | Custom dual vacuum nozzle (1st bottom-suction + 2nd top-suction) |
| Communication | TCP/IP Ethernet (SPEL+ remote command) or RS-232C (SPEL+ command protocol) |
| I/O | 16 DI / 16 DO directly wired to PLC for handshake signals |

---

## 3. Communication Architecture

### 3.1 Network Topology

```mermaid
graph TB
    subgraph "VLAN 10 — GigE Vision Network (Dedicated)"
        SW1["Managed GbE Switch<br/>8-port, Jumbo Frame enabled<br/>VLAN 10: 192.168.10.x/24"]
        CCD1["CCD#1 Top x4<br/>192.168.10.11-14<br/>MV-GE501GC"]
        CCD2["CCD#2 Side x4<br/>192.168.10.21-24<br/>MV-GE501GC"]
        CCD3["CCD#3 Bottom x4<br/>192.168.10.31-34<br/>MV-GE501GC"]
        CCD4["CCD#4 Lighting x4<br/>192.168.10.41-44<br/>MV-GE2000C"]
        NGCCD["NG Check / Poka-Yoke / Tray CCD<br/>192.168.10.51-53<br/>MV-GE501GC"]
        SW1 --- CCD1
        SW1 --- CCD2
        SW1 --- CCD3
        SW1 --- CCD4
        SW1 --- NGCCD
    end

    subgraph "VLAN 20 — Control Network"
        SW2["Control Switch<br/>VLAN 20: 192.168.20.x/24"]
        IPC["IPC<br/>192.168.20.1<br/>NIC-1: VLAN 10<br/>NIC-2: VLAN 20"]
        PLC["PLC<br/>192.168.20.10<br/>EtherNet/IP"]
        EPSON["Epson RC700-A<br/>192.168.20.20<br/>TCP/IP SPEL+"]
        HMI["HMI Panel<br/>192.168.20.30"]
        LIGHT["Gardasoft RT820F<br/>192.168.20.40<br/>Ethernet"]
        SW2 --- PLC
        SW2 --- EPSON
        SW2 --- HMI
        SW2 --- LIGHT
    end

    IPC --- SW1
    IPC --- SW2

    subgraph "Fieldbus — CC-Link IE Field"
        PLC ---|"CC-Link IE Field<br/>1 Gbps cyclic"| SERVO1["Transfer Servo #1-#6"]
        PLC ---|"CC-Link IE Field"| SERVO2["Rotation Servo"]
        PLC ---|"CC-Link IE Field"| SERVO3["Pitch Servo"]
        PLC ---|"CC-Link IE Field"| RIO["Remote I/O Station<br/>Pneumatic valves<br/>Sensors"]
    end
```

### 3.2 Protocol Stack Summary

| Layer | Protocol | Purpose | Devices | Cycle / Latency |
|-------|----------|---------|---------|-----------------|
| Vision | GigE Vision 2.0 (UDP) | Image acquisition + camera control | CCD#1-#4, NG CCD | Hardware-triggered, ~5 ms transfer per frame (5MP) |
| Vision Config | GenICam GenTL | Camera parameter access (exposure, gain, ROI, trigger) | All cameras | On-demand |
| Robot Command | TCP/IP SPEL+ Protocol | SCARA motion commands + status query | Epson RC700-A | ~10 ms round-trip |
| Robot Handshake | Hardwired 24V DI/DO | Cycle start, busy, complete, error signals | Epson via PLC | <1 ms (direct wire) |
| PLC ↔ IPC | EtherNet/IP (CIP) | Inspection results, recipe download, trigger commands | PLC ↔ IPC | 10 ms implicit messaging |
| Fieldbus | CC-Link IE Field | Servo position commands, I/O cyclic data | Servo drives, remote I/O | 0.5 ms cyclic (1 Gbps) |
| Lighting | Ethernet + Hardware Trigger | Strobe profile selection, trigger sync | Gardasoft RT820F-20 | <100 us trigger-to-light |
| Safety | Safety-rated hardwired | E-Stop, light curtain, door interlocks | Safety relay module | <10 ms response (Category 3) |
| HMI | HTTP / WebSocket | Operator interface, live camera feed | IPC → HMI Panel | Non-critical |

### 3.3 GigE Vision Protocol Details

The system uses **GigE Vision 2.0** over a dedicated VLAN with the following optimizations:

- **Jumbo Frames**: MTU set to 9000 bytes on the dedicated switch and NIC to reduce CPU interrupt rate during image transfer
- **Packet Resend**: Enabled with 100 ms timeout; resend rate monitored as image quality indicator
- **Inter-Packet Delay**: Configured per-camera to prevent bus contention when multiple cameras fire within the same cycle
- **Hardware Trigger Mode**: All cameras configured for Line 0 hardware trigger input — trigger signal is generated by the PLC synchronized to transfer axis position, ensuring image acquisition occurs at the exact inspection position
- **Bandwidth Allocation**: every inspection station carries 4 cameras (spec slide 10), 19 cameras in all. Per 4-unit batch: 4 x 5 MB (CCD#1) + 4 x 5 MB (CCD#3) + 4 x 36 x 5 MB (CCD#2 sweep) + 4 x 20 MB (CCD#4) = ~840 MB; at one batch per 3.8 s that is ~1.8 Gbit/s aggregate, so the stations sit on separate switch uplinks / NIC ports and the CCD#2 sweep is spread over its 1.8 s

| Camera | Resolution | Pixel Size | Frame Size | Trigger Source |
|--------|-----------|------------|-----------|----------------|
| CCD#1 x4 | 2448 x 2048 (5 MP) | 3.45 um | ~5 MB each | one PLC DO pulse → Line 0 of all 4 (CCD#1 reach-out extended) |
| CCD#2 x4 | 2448 x 2048 (5 MP) | 3.45 um | ~5 MB x 36 frames each | PLC DO pulse per 10-deg step of the rotators; one DONE per sweep |
| CCD#3 x4 | 2448 x 2048 (5 MP) | 3.45 um | ~5 MB each | one PLC DO pulse at `T2_TRIG_POS_MM` during Transfer #2 motion |
| CCD#4 x4 | 5472 x 3648 (20 MP) | 2.4 um | ~20 MB each | one PLC DO pulse after SHADE_CLOSED |
| NG CCD / Poka-Yoke / Tray | 2448 x 2048 (5 MP) | 3.45 um | ~5 MB | PLC DO → Camera Line 0 |

### 3.4 Lighting Controller Protocol

The **Gardasoft RT820F-20** strobe controller provides 4 independently programmable channels with nanosecond-precision trigger response:

| Channel | Light Source | Camera | Drive Current | Strobe Duration |
|---------|-------------|--------|---------------|-----------------|
| CH1 | DN-COS60-W (Coaxial) | CCD#1 Top | 800 mA | 200 us |
| CH2 | DN-2BS32738-W (Bar x2) | CCD#2 Side | 1200 mA | 150 us |
| CH3 | DN-COS60-W (Coaxial) | CCD#3 Bottom | 800 mA | 200 us |
| CH4 | DN-HSP25-W (Hyper Spot) | CCD#4 Lighting | 2000 mA | 500 us |

- **Trigger Mode**: Each channel fires on its own hardware trigger input (from PLC), synchronized with the corresponding camera trigger
- **Multi-Profile**: Each channel stores up to 8 illumination profiles (power/duration combinations), selectable at runtime for different CSE product variants via EtherNet/IP recipe download
- **Overdrive**: Strobe mode allows 3-5x continuous current rating for short pulses, enabling higher illuminance without thermal limits

---

## 4. Hardware Topology and Equipment Layout

### 4.1 Station Layout (18 Numbered Stations)

```
    ┌──────────────────────────────────────────────────────────────────┐
    │                   Machine Enclosure (Top View)                   │
    │  1800 mm x 1600 mm                                               │
    │                                                                   │
    │  ┌─────┐  ┌─────┐  ┌───────┐  ┌─────┐  ┌──────┐  ┌──────┐     │
    │  │  1  │→ │  2  │→ │   3   │→ │  4  │→ │  5   │→ │  6   │     │
    │  │Load │  │Feed │  │ SCARA │  │Pitch│  │Xfer1 │  │CCD#4 │     │
    │  │Bskt │  │Mech │  │ Robot │  │Chng │  │      │  │Light │     │
    │  └─────┘  └─────┘  └───────┘  └─────┘  └──────┘  └──────┘     │
    │                                                                   │
    │  ┌─────┐  ┌─────┐  ┌─────┐  ┌──────┐  ┌──────┐  ┌──────┐     │
    │  │  8  │← │  7  │← │  ←  │  │  9   │→ │  10  │→ │  11  │     │
    │  │Bskt │  │CCD#3│  │Xfer2│  │Ori.  │  │ Pos  │  │CCD#2 │     │
    │  │Coll │  │Botm │  │     │  │Comp  │  │      │  │Side  │     │
    │  └─────┘  └─────┘  └─────┘  └──────┘  └──────┘  └──────┘     │
    │                     ┌─────┐                                      │
    │                     │CCD#1│ (Top check between Xfer2→Xfer3)     │
    │                     └─────┘                                      │
    │  ┌──────┐  ┌──────┐  ┌──────┐  ┌──────┐  ┌──────┐             │
    │  │  16  │  │  15  │← │  14  │← │  13  │← │  12  │             │
    │  │Tray  │  │Man.  │  │Stack │  │Unlod │  │Xfer5 │             │
    │  │Feed  │  │Unlod │  │      │  │Tray  │  │      │             │
    │  └──────┘  └──────┘  └──────┘  └──────┘  └──────┘             │
    │                                                                   │
    │  ┌──────────────────┐                    ┌────┐  ┌────┐         │
    │  │ NG Path          │                    │ 17 │→ │ 18 │         │
    │  │ [NG CCD] → [Conv]│                    │Conv│  │NG  │         │
    │  └──────────────────┘                    │    │  │Tray│         │
    │                                           └────┘  └────┘         │
    │                                                                   │
    │  [HMI]  [E-Stop x3]  [Tri-Color]  [Optical Grating x2]        │
    └──────────────────────────────────────────────────────────────────┘
```

---

## 5. I/O Architecture

### 5.1 Digital I/O Allocation

| Group | Signals | Points | Direction |
|-------|---------|--------|-----------|
| Camera Triggers | CCD#1-#4 (one pulse per station), NG CCD, Poka-Yoke | 6 | DO |
| Vision Handshake | Result ready / pass per station, orientation valid, IPC heartbeat, tray full | DI via EtherNet/IP | DI |
| Robot Handshake | Ready, busy, done, alarm / start, case select | 4 DI + 2 DO | Mixed |
| Servo Status / Enable | Ready, in-position, alarm, home-done (x15 axes) / enable (x15) | 44 DI + 15 DO | Mixed |
| Limit Switches | Overtravel limits, index pulse, magazine low | 16 DI | DI |
| Basket Feeder | Lifter up/down, basket at holder, basket empty, stack present, collector | 6 DI + 5 DO | Mixed |
| Pitch Mechanism | Holder open/closed, flip done/home, pitch settled / platform vacuum, holder close, flip | 5 DI + 3 DO | Mixed |
| Chamber / Top Check / Side Check | Shade closed/open, CCD#1 extended/retracted, gripper up/down / shade, reach-out, lift, nest vacuums | 6 DI + 5 DO | Mixed |
| Transfer Grippers | Per-station vacuum, Transfer 5 per-unit release | 8 DO | DO |
| Tray Handling | Tray at pos 1, empty tray present, stack gripper up / stack gripper, push to pos 2, loader feed | 3 DI + 3 DO | Mixed |
| NG Path | At holder bar, NG tray present / belt run, holder bar, diverter | 2 DI + 3 DO | Mixed |
| Safety | E-Stop x3, light curtain x2, door interlock x2, STO feedback x2, muting x4, reset, mat (dual channel) | 24 DI + 4 DO (safety-rated) | Mixed |
| Indicators / Operator | Tower light, buzzer, muting lamps / start, stop, mode, reset, home, bypass | 6 DI + 6 DO | Mixed |
| **Total** | | **148 DI + 78 DO** (`src/plc/io_mapping.csv`) | |

### 5.2 Pneumatic Valve Manifold

SMC SY3000 series solenoid valve manifold, 24V DC:

| Valve | Function | Type | Flow |
|-------|----------|------|------|
| V1-V3 | Basket feeding: stack hold, L-trigger, lifter | 5/2 single solenoid | 200 L/min |
| V4-V5 | Pitch change: lock pin / platform vacuum (pitch itself is servo axis 15) | 5/2 double solenoid | 300 L/min |
| V6-V7 | Pitch change: blue holder clamp/release, 180-deg flip | 5/2 single solenoid | 200 L/min |
| V8-V9 | Shade close/open (CCD#4 closed chamber) | 5/2 double solenoid | 100 L/min |
| V10-V15 | Transfer gripper vacuum on/off (x6 stations) | 3/2 normally closed | N/A (vacuum) |
| V16-V19 | Pitch change platform vacuum (x4 positions) | 3/2 normally closed | N/A (vacuum) |

---

## 6. Safety Architecture

### 6.1 Safety System Diagram

```mermaid
graph TD
    subgraph "Safety Circuit (Category 3 / PLd per ISO 13849-1)"
        ES1["E-Stop #1<br/>Loading station"]
        ES2["E-Stop #2<br/>Unloading station"]
        ES3["E-Stop #3<br/>Rear panel"]
        LC1["Light Curtain<br/>Loading area<br/>IEC 61496 Type 2"]
        LC2["Light Curtain<br/>Unloading area<br/>IEC 61496 Type 2"]
        DI1["Door Interlock #1<br/>Left access"]
        DI2["Door Interlock #2<br/>Right access"]
    end

    subgraph "Safety Relay Module"
        SR["Pilz PNOZ s4.1<br/>or equivalent<br/>Dual-channel input<br/>Monitored outputs"]
    end

    subgraph "Safe Outputs"
        STO_SERVO["STO — Servo Drives<br/>(Safe Torque Off)"]
        STO_ROBOT["STO — Robot Controller<br/>(Epson Category Stop 0)"]
        PNEUM["Pneumatic dump valve<br/>(exhaust all cylinders)"]
    end

    ES1 --> SR
    ES2 --> SR
    ES3 --> SR
    LC1 --> SR
    LC2 --> SR
    DI1 --> SR
    DI2 --> SR
    SR --> STO_SERVO
    SR --> STO_ROBOT
    SR --> PNEUM
```

### 6.2 Safety Standards Compliance

| Standard | Requirement | Implementation |
|----------|-------------|----------------|
| ISO 13849-1 | Performance Level d (PLd) | Dual-channel safety relay, Category 3 architecture |
| IEC 61496 | Electro-sensitive protective equipment | Type 2 light curtain at operator access points |
| ISO 13850 | Emergency stop design | Red mushroom-head, positive-action NC contacts, manual reset required |
| ISO 12100 | General risk assessment | Documented risk assessment per machine directive |
| ISO 14120 | Guards and protective devices | Interlocked doors, polycarbonate panels |

---

## 7. Real-Time Cycle Analysis

### 7.1 Timing Budget (per 4-unit cycle)

Target: >85,000 units/day = >3,542 units/hour = ~1.02 seconds per unit = **~4.07 seconds per 4-unit cycle**

| Phase | Duration | Overlap |
|-------|----------|---------|
| SCARA pick 4 CSE from basket | 1.2 s | Concurrent with previous cycle's CCD#2 side check |
| Pitch change + positioning | 0.8 s | Concurrent with SCARA return-to-home |
| Transfer #1 to CCD#4 station | 0.3 s | — |
| CCD#4 shade close + acquisition + analysis | 0.6 s | — |
| Transfer #2 (CCD#3 bottom check during motion) | 0.4 s | CCD#3 acquisition during transfer motion |
| CCD#1 top check | 0.3 s | — |
| Orientation compensation + positioning | 0.4 s | — |
| CCD#2 side check (360-deg rotation, 36 frames) | 1.0 s | Concurrent with next cycle's SCARA pick |
| Transfer #5 to unloading | 0.3 s | — |
| Tray placement | 0.2 s | — |
| **Total sequential** | **~5.5 s** | |
| **Pipelined effective** | **~3.8 s** | 2 stations overlap with next cycle |

Effective throughput: 4 units / 3.8 s = **~3,789 units/hour = ~90,947 units/day** (exceeds 85K target with margin).

---

## 8. Software Architecture

```mermaid
graph TB
    subgraph "HMI Layer"
        HMI_APP["HMI Application<br/>(Qt / WinForms)"]
        RECIPE["Recipe Manager<br/>Product variant configs"]
        LOG["Data Logger<br/>CSV + SQL archive<br/>Image traceability"]
    end

    subgraph "Process Control Layer"
        SEQ["Inspection Sequencer<br/>(18-step state machine)"]
        MOTION["Motion Coordinator<br/>(PLC EtherNet/IP client)"]
        VISION["Vision Pipeline Manager<br/>(multi-threaded acquisition<br/>+ classification)"]
    end

    subgraph "Vision Engine Layer"
        ACQ["Image Acquisition<br/>Hikrobot MVS SDK<br/>GenICam interface"]
        PREPROC["Preprocessing<br/>ROI extraction<br/>Illumination normalization"]
        DETECT["Defect Detection<br/>19-category classifier<br/>Traditional CV + optional CNN"]
        ORIENT["Orientation Detector<br/>Poka-Yoke + compensation"]
    end

    subgraph "Device Driver Layer"
        CAM_DRV["Hikrobot MVS Driver<br/>GigE Vision 2.0"]
        ROB_DRV["Epson SPEL+ Protocol<br/>TCP/IP or RS-232C"]
        PLC_DRV["EtherNet/IP Client<br/>CIP implicit messaging"]
        LIGHT_DRV["Gardasoft Ethernet<br/>Profile selection"]
    end

    HMI_APP --> SEQ
    RECIPE --> SEQ
    SEQ --> MOTION
    SEQ --> VISION
    SEQ --> LOG
    MOTION --> PLC_DRV
    MOTION --> ROB_DRV
    VISION --> ACQ
    ACQ --> PREPROC
    PREPROC --> DETECT
    PREPROC --> ORIENT
    ACQ --> CAM_DRV
    VISION --> LIGHT_DRV
```

### 8.1 Vision Pipeline Threading Model

```
Main Thread          Camera Thread x19      Classification Thread x4
─────────────        ─────────────────      ─────────────────────────
Sequencer loop  →    Wait for trigger  →    Pull frame from queue
  ↓                  Acquire frame          Run per-CCD detector
  Send trigger       Push to queue          Return InspectionResult
  ↓                  Signal ready           ↓
  Wait for result ←──────────────────────── Aggregate all 4 CCDs
  ↓                                         Apply NG double-check
  Route OK/NG
```

- **19 camera threads** (4 per inspection station + NG check, Poka-Yoke, tray monitor): each blocks on the hardware trigger, then acquires and enqueues its (station, unit) frame
- **4 classification threads**: Each CCD's defect detection runs in its own thread to maximize parallelism
- **1 main sequencer thread**: Coordinates triggers, waits for aggregated results, makes pass/fail decision

---

## 9. Power Distribution

| Circuit | Voltage | Breaker | Load |
|---------|---------|---------|------|
| Main Power | 380V 3-phase | 32A MCCB | Total machine supply |
| IPC + Vision | 220V single-phase | 16A MCB | IPC, cameras, switch, lighting controller |
| Robot Controller | 220V single-phase | 16A MCB | Epson RC700-A |
| Servo Drives | 220V single-phase | 20A MCB | 15 servo drives (CC-Link IE Field) |
| 24V DC Supply | 24V DC (Mean Well NDR-480-24) | 20A | PLC, I/O modules, solenoid valves, sensors |
| Pneumatic | 0.4-0.6 MPa | N/A | SMC FRL + valve manifold + vacuum generators |
| Safety Circuit | 24V DC (dedicated) | 10A | Safety relay, E-Stop, light curtains (galvanically isolated) |
