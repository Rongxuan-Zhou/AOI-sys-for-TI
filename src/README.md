# AOI System for TI CSE Semiconductor Products -- Source Code

Automated Optical Inspection system for Texas Instruments CSE (round ceramic)
packages, implemented to spec `CSE Round Lighting check and FI AOI V1.4`
(Dinnar, 2024-05-25). 19 defect categories, 4 inspection stations with 4
cameras each, > 85 000 units per day.

Author: Rongxuan Zhou


## Directory Structure

```
src/
  vision/                    C++17 / OpenCV vision core (station algorithms + SDK capture)
    CameraID.h               Station identifiers (CCD1..4, NG check, Poka-Yoke, tray monitor)
    DefectTypes.h            19 spec defects + 2 inner-residue sub-checks, groups, severities
    CseLocator.h/.cpp        Disc centre/radius (two-pass LSQ), key-notch angle, interior
                             mask, polar unwrap -- shared by every station
    LightingCheck.h/.cpp     CCD#4 inner check: ring-median baseline on the unwrapped
                             annulus, leakage / edge staining / yellow glass cement,
                             nest-ring alignment, thickness class (amber/green/blue)
    DefectDetector.h/.cpp    Per-station routing (spec slide 9) and the detectors:
                             crack, broken, misalignment, top-side pins and epoxy rings,
                             contamination classes, marking code, side-view pin
                             geometry, gold exposal, epoxy bump
    ImagePreprocessor.*      Bayer demosaic, flat field, CLAHE, chromatic correction
    GigEVisionCapture.*      Hikrobot MVS SDK wrapper, one handle per (station, unit)
    pybind_module.cpp        Python bindings (_aoi_vision_core)
    CMakeLists.txt           AOI_WITH_MVS (default ON), AOI_BUILD_TESTS (default OFF)
    tests/                   GoogleTest suite on synthetic CSE images

  plc/                       IEC 61131-3 Structured Text, Mitsubishi iQ-R
    main_sequence.st         State machine on the 18-step process flow (slide 15)
    servo_config.st          15 axes: 6 transfers, 4 theta units, 4 side rotators,
                             pitch e-cylinder; homing order
    safety_program.st        ISO 13849-1 PLd Cat.3 safety logic
    io_mapping.csv           CC-Link IE Field remote I/O allocation
```


## Build and Test (no camera SDK needed)

```
cmake -S src/vision -B src/vision/build -DAOI_WITH_MVS=OFF -DAOI_BUILD_TESTS=ON
cmake --build src/vision/build -j8
ctest --test-dir src/vision/build --output-on-failure
python3 tools/plc/st_lint.py src/plc/*.st
```

The production build (`AOI_WITH_MVS=ON`) additionally needs the Hikrobot MVS
SDK under `/opt/MVS` and pybind11.


## Key Design Decisions

- **One geometry, every station**: `CseLocator` finds the disc and the key
  notch once; rim, interior and code ROI all derive from it, so an inverted
  part (notch at the top, slides 34/38) is read correctly.
- **Colour-independent lighting check**: the CCD#4 judgement is a residual
  against the per-radius ring median, so amber, green and blue samples
  (slide 31) share one threshold set.
- **Spec routing wins**: gold exposal is a side-view defect (slide 37), the
  pins and their epoxy rings are top-side (slide 9), yellow glass cement and
  staining on edge belong to the inner check (slide 8).
- **Per-unit results**: the PLC keeps `VISION_RESULT[station, unit]` for the
  4-unit batch; one hardware trigger per station fires its four cameras.
- **Every threshold is a parameter**: `DetectionParams` in C++, mirrored by
  name in `config/vision-system/ccdN_*.yaml`.
