# System Architecture -- AOI for Texas Instruments CSE Semiconductor Products

**Project:** Automated Optical Inspection System for TI CSE Products  
**Built by:** Rongxuan Zhou, Sole Engineer  
**Company:** Dinnar Automation  
**Client:** Texas Instruments  

---

## 1. Machine Physical Specifications

| Parameter | Value |
|-----------|-------|
| Overall Dimensions (L x W x H) | 1800 mm x 1600 mm x 2000 mm |
| Frame Construction | Aluminum extrusion frame with steel base plate |
| Base Support | Roller casters with adjustable leveling feet and holder base for stability |
| Access | Front-loading operator interface with protective enclosure |

---

## 2. High-Level System Architecture

```mermaid
graph TB
    subgraph "AOI Machine Enclosure (1800x1600x2000mm)"
        MC[Main Controller<br/>Industrial PC / PLC]
        
        subgraph "Vision Subsystem"
            CCD1["CCD#1 -- Top Check<br/>MV-GE501GC"]
            CCD2["CCD#2 -- Side Check<br/>MV-GE501GC"]
            CCD3["CCD#3 -- Bottom Check<br/>MV-GE501GC"]
            CCD4["CCD#4 -- Lighting Check<br/>MV-GE2000C-T1P-C4"]
        end
        
        subgraph "Motion Subsystem"
            ROBOT["Epson SCARA Robot<br/>Controller"]
            TR["Transfer Mechanisms<br/>6x Linear Axes"]
            CYL["Pneumatic Cylinders<br/>Basket Feed / Pitch Change"]
            MOT["360-degree Rotation Motor<br/>Side Check Station"]
        end
        
        subgraph "Illumination Subsystem"
            L1["DN-COS60-W Coaxial Light<br/>(CCD#1)"]
            L2["DN-2BS32738-W Bar Light<br/>(CCD#2)"]
            L3["DN-COS60-W Coaxial Light<br/>(CCD#3)"]
            L4["DN-HSP25-W Hyper Spot Light<br/>(CCD#4)"]
        end
        
        HMI["HMI Touchscreen<br/>Operator Interface"]
        IND["Tri-Color Indicator Light"]
        OGP["Optical Grating<br/>Protection"]
        ESTOP["E-Stop Buttons"]
    end
    
    MC --> CCD1
    MC --> CCD2
    MC --> CCD3
    MC --> CCD4
    MC --> ROBOT
    MC --> TR
    MC --> CYL
    MC --> MOT
    MC --> L1
    MC --> L2
    MC --> L3
    MC --> L4
    MC --> HMI
    MC --> IND
    MC --> OGP
    MC --> ESTOP
```

---

## 3. Network Topology

```mermaid
graph LR
    subgraph "GigE Vision Network"
        SW["GigE Switch"]
        CCD1["CCD#1<br/>MV-GE501GC"]
        CCD2["CCD#2<br/>MV-GE501GC"]
        CCD3["CCD#3<br/>MV-GE501GC"]
        CCD4["CCD#4<br/>MV-GE2000C-T1P-C4"]
        SW --- CCD1
        SW --- CCD2
        SW --- CCD3
        SW --- CCD4
    end
    
    subgraph "Control Network"
        IPC["Main Controller / IPC"]
        PLC["PLC I/O Modules"]
        EPSON["Epson SCARA<br/>Robot Controller"]
        IPC --- PLC
        IPC --- EPSON
    end
    
    SW --- IPC
    
    subgraph "Operator Interface"
        HMI["HMI Touchscreen"]
        IPC --- HMI
    end
```

All four Hikrobot cameras communicate with the main controller over GigE Vision protocol via a dedicated Gigabit Ethernet switch, ensuring deterministic image transfer with minimal latency. The Epson SCARA robot controller connects to the main controller via dedicated communication link (RS-232 / Ethernet, per Epson RC+ configuration). PLC I/O modules handle all pneumatic valve control, sensor inputs, and actuator outputs.

---

## 4. Hardware Topology and Equipment Layout

```mermaid
graph TD
    subgraph "Operator Side (Front)"
        LOAD["Manual Loading<br/>Basket Input"]
        UNLOAD["Manual Unloading<br/>Tray Output"]
        NG_OUT["NG Tray<br/>Output"]
    end
    
    subgraph "Machine Interior -- Left to Right Flow"
        BF["Basket Feeding<br/>Mechanism"]
        CSE_LOAD["CSE Loading<br/>Epson SCARA"]
        PITCH["Pitch Change<br/>Station"]
        T1["Transfer #1"]
        CCD4_ST["CCD#4 Station<br/>Lighting Check<br/>(Closed Chamber)"]
        T2["Transfer #2"]
        CCD3_ST["CCD#3 Station<br/>Bottom Check"]
        CCD1_ST["CCD#1 Station<br/>Top Check"]
        COMP["Orientation<br/>Compensation"]
        POS["Positioning"]
        CCD2_ST["CCD#2 Station<br/>Side Check<br/>(360-degree Rotation)"]
        T5["Transfer #5"]
        TRAY_UL["Tray Unloading<br/>and Stacking"]
    end
    
    subgraph "Reject Path"
        NG_CCD["NG Check CCD<br/>Reconfirm"]
        NG_CONV["NG Conveyor"]
        NG_TRAY["NG Tray"]
    end
    
    subgraph "Basket Return"
        EBC["Empty Basket<br/>Collector"]
    end
    
    LOAD --> BF
    BF --> CSE_LOAD
    CSE_LOAD --> PITCH
    PITCH --> T1
    T1 --> CCD4_ST
    CCD4_ST --> T2
    T2 --> CCD3_ST
    CCD3_ST --> CCD1_ST
    CCD1_ST --> COMP
    COMP --> POS
    POS --> CCD2_ST
    CCD2_ST --> T5
    T5 --> TRAY_UL
    TRAY_UL --> UNLOAD
    
    T5 -->|NG Detected| NG_CCD
    NG_CCD --> NG_CONV
    NG_CONV --> NG_TRAY
    NG_TRAY --> NG_OUT
    
    BF -->|Empty Basket| EBC
```

---

## 5. Electrical Architecture

### 5.1 Main Controller

The main controller is an industrial PC (IPC) running the vision inspection software and master process orchestration logic. It coordinates all subsystems through:

- **GigE Vision interface** to all four Hikrobot cameras
- **Digital I/O** via PLC modules for pneumatic valve control, sensor readback, and actuator commands
- **Communication link** to the Epson SCARA robot controller for pick-and-place sequencing
- **HMI interface** for operator interaction, recipe management, and result display

### 5.2 Epson SCARA Robot Controller

The Epson SCARA robot is controlled by its dedicated Epson RC+ controller, which receives high-level motion commands from the main controller. The robot performs:

- CSE pick-up from the basket feeding station using dual vacuum nozzles
- Poka-Yoke orientation verification via CCD check before placement
- 90-degree rotation for correct orientation
- Placement of 4 units per cycle onto the pitch change platform

### 5.3 Camera System (4x Hikrobot Cameras)

| Camera ID | Model | Purpose | Interface |
|-----------|-------|---------|-----------|
| CCD#1 | MV-GE501GC | Top surface inspection | GigE Vision |
| CCD#2 | MV-GE501GC | Side / pin inspection (360-degree) | GigE Vision |
| CCD#3 | MV-GE501GC | Bottom surface inspection | GigE Vision |
| CCD#4 | MV-GE2000C-T1P-C4 | Lighting check (functional) | GigE Vision |

### 5.4 HMI (Human-Machine Interface)

The HMI touchscreen provides:

- Real-time production status display (throughput, pass/fail counts, yield)
- Recipe selection and parameter adjustment
- Alarm history and diagnostics
- Manual/jog mode for maintenance and setup
- Camera live view and inspection result review

---

## 6. Safety Features

### 6.1 Optical Grating Protection

Optical grating (light curtain) sensors are installed at the manual loading and unloading areas. When an operator's hand or body breaks the light curtain beam during machine operation, the system immediately:

1. Halts all motion axes (robot, transfer, rotation motor)
2. Closes pneumatic valves to safe state
3. Triggers a fault alarm on the Tri-Color indicator
4. Requires operator acknowledgment before restart

This prevents injury from moving mechanical components while operators load baskets or unload trays.

### 6.2 Tri-Color Indicator Light

| Color | State | Meaning |
|-------|-------|---------|
| Green | Steady | Normal operation / Running |
| Yellow | Steady or Flashing | Warning condition (e.g., low material, approaching maintenance interval) |
| Red | Steady or Flashing | Fault / Emergency stop active / Intervention required |

The indicator tower is mounted on top of the machine enclosure for visibility from across the production floor.

### 6.3 Emergency Stop (E-Stop)

Multiple E-Stop mushroom-head buttons are located at:

- The operator loading station (front-left)
- The operator unloading station (front-right)
- The rear maintenance panel

Pressing any E-Stop button immediately de-energizes all motion systems and engages pneumatic brakes where applicable. The system enters a safe hold state and requires a deliberate reset sequence before resuming operation.

### 6.4 Enclosed Machine Frame

The machine is fully enclosed with transparent polycarbonate panels where visual access is needed, and interlocked access doors for maintenance. Door interlocks prevent operation when any access panel is open.

---

## 7. Power and Pneumatic Architecture

```mermaid
graph TD
    subgraph "Facility Supply"
        AC["AC Power<br/>220V / 380V"]
        AIR["Compressed Air<br/>0.4 - 0.6 MPa"]
        VAC_SUP["Vacuum Supply"]
    end
    
    subgraph "Power Distribution"
        PS1["24V DC Power Supply<br/>(Sensors, Valves, I/O)"]
        PS2["Camera Power Supply"]
        PS3["IPC Power Supply"]
        PS4["Robot Controller Power"]
        PS5["Lighting Controller Power"]
    end
    
    subgraph "Pneumatic Distribution"
        FRL["Filter-Regulator-Lubricator"]
        MAN["Solenoid Valve Manifold"]
        CYL1["Basket Feed Cylinders"]
        CYL2["Pitch Change E-Cylinder"]
        CYL3["Gripper Cylinders"]
        CYL4["Shade Close Cylinder"]
        VAC1["Vacuum Nozzles<br/>(SCARA)"]
        VAC2["Vacuum Pads<br/>(Pitch Change Platform)"]
    end
    
    AC --> PS1
    AC --> PS2
    AC --> PS3
    AC --> PS4
    AC --> PS5
    
    AIR --> FRL
    FRL --> MAN
    MAN --> CYL1
    MAN --> CYL2
    MAN --> CYL3
    MAN --> CYL4
    VAC_SUP --> VAC1
    VAC_SUP --> VAC2
```

---

## 8. Software Architecture Overview

```mermaid
graph TB
    subgraph "Application Layer"
        HMI_APP["HMI Application"]
        RECIPE["Recipe Manager"]
        LOG["Data Logger /<br/>Statistics Engine"]
    end
    
    subgraph "Process Control Layer"
        SEQ["Sequence Controller<br/>(Master State Machine)"]
        MOTION["Motion Coordinator"]
        VISION["Vision Pipeline<br/>Manager"]
    end
    
    subgraph "Device Layer"
        CAM_DRV["Hikrobot Camera<br/>SDK / Driver"]
        ROB_DRV["Epson RC+<br/>Communication"]
        IO_DRV["PLC I/O<br/>Driver"]
        LIGHT_DRV["Lighting<br/>Controller"]
    end
    
    HMI_APP --> SEQ
    RECIPE --> SEQ
    SEQ --> MOTION
    SEQ --> VISION
    MOTION --> ROB_DRV
    MOTION --> IO_DRV
    VISION --> CAM_DRV
    VISION --> LIGHT_DRV
    SEQ --> LOG
```

The master state machine in the sequence controller orchestrates all process steps, coordinating vision acquisition triggers with mechanical motion to minimize cycle time. The vision pipeline manager handles image acquisition, preprocessing, defect detection, and classification for all four CCD stations in a pipelined fashion, allowing inspection at one station to overlap with mechanical transfer at another.

---

## 9. Summary

This AOI system integrates precision mechanical handling (Epson SCARA robot, 6-axis linear transfer, pitch-change mechanism), a 4-camera Hikrobot vision subsystem with specialized illumination, and comprehensive safety features into a compact 1800 x 1600 x 2000 mm footprint. The architecture is designed to sustain a throughput target exceeding 85,000 units per day with sub-1-second cycle time per unit, while maintaining 100% detection coverage across all 19 defined defect categories for TI CSE semiconductor products.
