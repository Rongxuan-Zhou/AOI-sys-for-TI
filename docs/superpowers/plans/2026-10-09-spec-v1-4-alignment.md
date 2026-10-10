# CSE Round Lighting Check & FI AOI V1.4 — Spec Alignment Implementation Plan (rev 2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the vision core (C++) and PLC sequence (ST) implement the machine described in `CSE_Round_Lighting_check_and_FI_AOI_V1_4.pdf` (Dinnar, V1.4, 2024-05-25): the 19-item defect list, the per-CCD inspection split, the CCD#4 closed-chamber lighting check with inner-staining-residue sub-checks, and the 18-step mechanical flow.

**Architecture:** Keep the existing module layout (`src/vision/` OpenCV core + pybind11, `src/plc/` IEC 61131-3 ST, `config/`, `docs/`). Add two shared units to the vision core — `CseLocator` (disc/notch geometry used by every CCD) and `LightingCheck` (CCD#4 inner check) — and rewrite the algorithm bodies in `DefectDetector` so that each defect is detected on the CCD that actually sees it. Rewrite `MAIN_SEQUENCE` so its states are the process steps of the spec, with per-unit (4 per batch) vision results, 4 cameras per station, 4 theta units on Transfer #4, 4 rotators at the side check, and the real actuators (basket feeder, SCARA handshake, blue-holder flip, chamber shade, CCD#1 reach-out, on-the-fly CCD#3 trigger, NG conveyor, tray loader/stacker).

**Tech Stack:** C++17, OpenCV 4.6, GoogleTest 1.14 (system package, config-mode `find_package(GTest)` verified on this host), CMake 3.28, pybind11 2.11; IEC 61131-3 Structured Text (Mitsubishi GX Works3 dialect, no compiler on this host — validated by a structural lint script); YAML configs; Markdown docs.

**Spec:** the PDF (43 pages). Text extraction and page renders live in the session scratchpad (`spec.txt`, `s-NN.png`); slide numbers below are the slide footer numbers (PDF page + 2). The "Design brief" records every decision so the plan is self-contained.

**Review history:** rev 1 was reviewed by two independent agents (spec fidelity; engineering feasibility with the synthetic tests executed in Python). All their blocker/major findings are folded into this revision; the ones that are design *decisions* rather than spec facts are marked "(decision)".

---

## Design brief (what the spec says, and what we decided)

### D1. Product and views (slides 9, 10, 18–26, 30–45)

- CSE is a round white ceramic disc with one semicircular **key notch** on the rim. Top side carries **pins** (3 or 4 gold pins seated in epoxy-filled holes), a **2-line laser code**, and a small "*" mark. Bottom side is plain ceramic with an amber **epoxy (banana-shaped) region**. Pins point **up** in the machine orientation at the CCD2 station (slide 9 CCD2 thumbnail; slide 20 "all the CSEs are in below status" shows pins up after the 1st-case flip). Bench photos on slides 37/43 show pins down, so the side-view detector takes a `pins_up` flag.
- Slide 9: CCD#1,#3 = top/bottom side check + **misalignment**; CCD#2 = side check & **PIN** check; CCD#4 = **inner check**. Slide 9 note: "Pin exist on top side." The misalignment sample (slide 45) is a backlit disc inside the dark round **nest ring** of the CCD#4 station; slide 9 places that image under CCD#4 while the text lists misalignment under CCD#1,#3. Decision: MISALIGNMENT is judged on CCD1/CCD3 against the recipe fixture centre **and** on CCD4 against the detected nest ring centre.
- Slide 10: **4 cameras per station** ("MV-GE501GC * 4", "MV-GE2000C-T1P-C*4"); slides 21/23/26 show 4 lenses / 4 rotators. One trigger per station images all 4 units of a batch. The IPC owns the (station, unit) → physical camera mapping; the detector sees one unit's frame at a time.
- CCD#4 images (slides 30–31) are transmitted-light colour images: an amber annulus with a dark centre, dark banana and pin-hole shadows, one bright radial line; thick samples look **green** or **blue**. The detect result marks a small bright spot near the outer edge = leakage ("浅色泄露 / light colour leakage", slide 5).

### D2. Defect list (slide 8) → enum and routing

21 enumerators: the 19 spec items plus the 2 "Inner Staining Residue" sub-items (slide 8, right table).

| Group | DefectType enumerators | Detected on | Critical (purple, slide 8) |
|---|---|---|---|
| FUNCTION (9) | LIGHTING_CHECK, CRACK, BROKEN, EPOXY_EXPOSAL, PIN_MISSING, ELECTRICAL_CONTAMINATION, GOLD_EXPOSAL, INSUFFICIENT_EPOXY, EPOXY_OVERFLOW | CCD4; CCD1+CCD3; CCD1+CCD3; CCD1; CCD1; CCD1+CCD3; CCD2; CCD1; CCD1 | LIGHTING_CHECK, CRACK, BROKEN |
| COSMETIC (4) | DYEING_CONTAMINATION, NON_ELECTRICAL_CONTAMINATION, NO_CODE, CODE_BLUR | CCD1+CCD3; CCD1+CCD3; CCD1; CCD1 | — |
| ASSEMBLY (6) | PIN_BENT, PIN_OXIDIZED, PIN_BUR, PIN_MIS_CUT, EPOXY_HIGHER_THAN_CERAMIC, MISALIGNMENT | CCD2 ×5; CCD1+CCD3+CCD4 | MISALIGNMENT |
| INNER_RESIDUE (2) | STAINING_ON_EDGE, YELLOW_GLASS_CEMENT | CCD4 | — |

Severity: CRITICAL for the 4 purple items; MAJOR for every other FUNCTION/ASSEMBLY item; MINOR for COSMETIC and INNER_RESIDUE (decision — slide 8 gives no severity for the residue table). Any detected defect makes the unit NG; severity is for sorting/statistics only. Epoxy overflow, Pin bur and Pin mis-cut had 0 real samples ("base on real sample", slide 8): their YAML thresholds carry `provisional: true` and `docs/defect-classification.md` says so.

Where the old code disagreed with the spec, the spec wins: gold exposal moves from CCD3 to CCD2 (slide 37 is a side image); yellow glass cement and edge staining move to CCD4 (slide 8 lists them under "Inner Staining Residue"); pins are vertical in CCD2 (slide 43); `C_STAINING`, `A_LIGHT_LEAKAGE`, `A_EDGE_STAINING` names are replaced by the spec names.

### D3. Algorithms (one per defect; every number is a `DetectionParams` field)

Scale note: header defaults are tuned for the 640–800 px synthetic test frames; production frames are 2448×2048 (CCD1–3) and 5472×3648 (CCD4) and the per-station YAML carries the production values. Tests call `aoi::test::SyntheticParams()` which returns the header defaults.

Common: `CseLocator::Locate(bgr)` → disc centre, radius, key-notch angle. Disc = largest bright blob (Otsu on V; the Otsu level must be ≥ `min_otsu_thresh` (30) or the frame is "not located" — a noisy dark frame must not split into a fake disc), `minEnclosingCircle` then two-pass Kåsa least-squares refit keeping contour points with `| |p−c| − R | ≤ fit_inlier_ratio·R` so a chip or the notch does not pull the fit (prototype: centre error < 0.5 px on the synthetic disc). Notch: rim profile over 720 rays; residual runs > `0.4·notch_depth_px`; the run whose angular width is within `[0.4, 2.0]·notch_width_deg` (default `notch_width_deg = 7`, `notch_depth_px = 10`; the synthetic notch measures 6.5° / 9.5 px) with the largest depth is the key notch; none → `notch_angle_deg = NAN`.

Interior mask for every on-disc detector: `disc_mask` eroded by `interior_margin_px` (20) **minus** a circle of radius `1.5·notch_depth_px` at the notch (removes the anti-aliased rim and the notch itself, which otherwise produce 200–5000 px of false texture).

- **CRACK** (CCD1, CCD3): `MORPH_BLACKHAT` (kernel `crack_blackhat_kernel_px` = 15, ellipse) on grey inside the interior mask minus the CCD-specific exclusion mask; threshold `crack_blackhat_thresh` (25); per connected component take `cv::minAreaRect`: long/short side ≥ `crack_aspect_min` (4) and long side ≥ `crack_min_length_px` (30) → CRACK. Confidence = clamp(long / (4·min_length)).
- **BROKEN** (CCD1, CCD3): rim-profile residual runs (same profile as the locator) with depth > `broken_depth_px` (6). Exemption is **by angle only**: a run whose centre angle lies within `±notch_width_deg` of `notch_angle_deg` is the key notch and is exempt **if** its max depth ≤ `1.5·notch_depth_px`; every other run, an over-deep notch run, or any run when the notch is NAN → BROKEN. Confidence = clamp(max residual / (3·broken_depth_px)).
- **MISALIGNMENT**: CCD1/CCD3: offset of disc centre from `fixture_center_ccd1/ccd3` (recipe; `(-1,-1)` = image centre) × `mm_per_px_ccd123` > `misalign_max_offset_mm` (0.15, from `ccd1_top_check.yaml`). CCD4: offset of the lit disc centre from the nest-ring centre (ring = largest contour of `V > nest_val_min` (25), i.e. the not-black region; its `minEnclosingCircle` centre) × `mm_per_px_ccd4`; if no ring larger than `1.1·R` is found, no judgement. Rotation is not judged (compensated by the theta axes).
- **Top pins** (CCD1): holes = dark circular blobs (grey < `pin_head_dark_thresh` (110), circularity > 0.6, radius within `pin_head_radius_px` (18) ± 40 %) inside the interior mask. A hole **has a pin** when a bright metallic core (V > `pin_core_val_min` (170), area ≥ `pin_core_area_min_px` (20)) lies within `0.6·r_hole` of its centre (slide 35: the empty hole is a dark/greenish filled disc with no core). Holes without a core → PIN_MISSING (bbox = the hole); additionally `#holes < expected_pin_count` → PIN_MISSING (bbox = disc). Epoxy ring per hole: pixels in `[r_hole, r_hole + epoxy_ring_px (10)]` with S > `epoxy_sat_min` (50) or V < `epoxy_val_max` (160); ring fill < `epoxy_ring_min_fill` (0.6) → INSUFFICIENT_EPOXY. Overflow is **relative**: epoxy-coloured area connected to the hole > `epoxy_overflow_ratio` (2.5) × nominal ring area π((r+ring)² − r²) → EPOXY_OVERFLOW. Pins + rings go into `pin_mask_out` (dilated 6 px) and are excluded from crack/contamination/exposal.
- **EPOXY_EXPOSAL** (CCD1): low-saturation (S ≤ `exposal_sat_max` (40)), high-texture (local 7×7 std-dev > `exposal_texture_min` (18)) patch of area ≥ `exposal_area_min_px` (300) inside the interior mask minus pins/code.
- **Contamination** (CCD1, CCD3): candidate mask = adaptive threshold (31, 12, `THRESH_BINARY_INV`) on grey OR `S > dyeing_sat_min` (70), AND interior mask, AND NOT exclusion (CCD1: pins + code; CCD3: amber epoxy mask `H ∈ [epoxy_hue_low (8), epoxy_hue_high (30)] ∧ S > epoxy_sat_min` dilated 10 px). Per blob ≥ `contam_area_min_px` (40): mean S ≥ `dyeing_sat_min` → DYEING_CONTAMINATION; else mean grey ≤ `electrical_dark_max` (120) ∧ grey std ≥ `electrical_texture_min` (20) → ELECTRICAL_CONTAMINATION (dark metallic streak, slide 36); else → NON_ELECTRICAL_CONTAMINATION.
- **NO_CODE / CODE_BLUR** (CCD1): code ROI = `code_roi_w × code_roi_h` (260 × 90) centred at disc centre + `code_offset_px` (30) along the notch direction and rotated by `notch_angle_deg − 90` (slides 34/38 show the code inverted with the notch at the top: the ROI follows the notch). Ink = grey < (ROI mean − `code_ink_contrast` (30)); ink fraction < `code_min_ink_ratio` (0.03) → NO_CODE; else Laplacian variance < `code_laplacian_blur_limit` (80) → CODE_BLUR. The code ROI goes into `code_mask_out`.
- **Side view** (CCD2, one frame per rotation step): if `pins_up` (default true) the frame is flipped vertically first (bboxes mapped back). Foreground = grey > `side_fg_thresh` (40) (not Otsu — Otsu at ≈95 swallowed a dark oxidised pin in the feasibility run); body = largest component of the foreground after a horizontal opening of width `pin_w_max·2` (so connected pins are not part of the body); `y_b` = body bottom. Pins = foreground components below `y_b` with width ≤ `pin_w_max` (14). Per pin: `cv::fitLine(DIST_L2)` → angle from vertical > `pin_bend_angle_deg_max` (2.0) → PIN_BENT; height outside `pin_length_px` (120) ± `pin_length_tol_px` (15) → PIN_MIS_CUT; width of the last 6 rows − median width > `pin_bur_width_px` (4) → PIN_BUR; oxidation = pin mean V (over pixels with S > 60) < `pin_oxide_val_max` (120) — healthy pins are bright gold (slide 43), oxidised ones are dull/dark.
- **GOLD_EXPOSAL** (CCD2): inside the body rect, H ∈ [10, 35], S > 60, V ∈ [60, 220], area ≥ `gold_area_min_px` (60).
- **EPOXY_HIGHER_THAN_CERAMIC** (CCD2): body top-edge profile (first foreground row per column); a run ≥ `epoxy_bump_width_px` (8) columns where the profile is above the median top by > `epoxy_bump_px` (5).
- **LIGHTING_CHECK + inner residue** (CCD4, `LightingCheck`): lit disc on V (Otsu ≥ `min_otsu_thresh`); polar unwrap of H, S, V over `[inner_zone_ratio (0.40)·R, outer_zone_ratio (0.98)·R]` into `unwrap_w × unwrap_h` (1440 × 160); per-row (= per-radius) median of V and S = baseline; residual = value − baseline. Leakage = residual_V > `leak_residual_min` (45), area ≥ `leak_area_min_px` (80), **compact** (minAreaRect long/short ≤ `blob_aspect_max` (4) — the bright radial line on slide 30 is elongated and must not count). Staining on edge = residual_V < −`stain_residual_min` (45) in rows ≥ `edge_band_ratio` (0.85), area in [`stain_area_min_px` (120), `stain_area_max_px` (3000)] and compact (the dark banana and hole shadows are large/elongated structures, not stains). Yellow glass cement = H ∈ [`cement_hue_low` (20), `cement_hue_high` (40)] ∧ (S − baseline_S > `cement_sat_delta` (40) ∨ V − baseline_V > `cement_val_delta` (30)) — the V term is what detects cement on amber samples where the saturation baseline is already ≈ 233. Thickness class = mode of the annulus hue histogram (30 bins): AMBER 10–35, GREEN 40–85, BLUE 90–130, else UNKNOWN; reported only. A golden-template residual mode is the production upgrade path and is noted in the YAML as `baseline_mode: ring_median` (only mode implemented).

### D4. Mechanical flow (slides 14–29) → PLC states

Process steps (slide 15): 1 manual basket load, 2 single basket feeding, 3 CSE loading, 4 pitch change & positioning, 5 Transfer #1, 6 shade close & lighting check CCD#4, 7 bottom check CCD#3 (during Transfer #2), 8 top check CCD#1, 9 orientation compensation, 10 positioning, 11 side check CCD#2, 12 Transfer #5, 13 unloading to tray, 14 full tray stack, 15 manual unloading, 16 single tray feeding, 17 unloading to NG conveyor (if NG), 18 NG unloading.

| State | Steps | What the state does |
|---|---|---|
| ST_IDLE | — | wait for START in AUTO with safety OK; also the resting state after STOP or after an error reset |
| ST_BASKET_FEED | 2 (+aux) | only when no basket is on the holder or the current basket is empty: push the empty basket to the collector; hold the stack, holding block + L-trigger, lifter splits the last basket, lower it onto the holder |
| ST_POKAYOKE_CHECK | 3.1 | trigger Poka-Yoke CCD; result gives `CASE_SEL` (1 = 1st case: nozzle sucks the bottom side, needs the 180° flip; 2 = 2nd case, default) and GO/NOGO |
| ST_ROBOT_PICK | 3.2–3.3 | handshake with Epson RC700-A; robot picks 4, rotates 90°, places on the purple platform; platform vacuum ON |
| ST_PITCH_CHANGE | 4 | axis 15 (e-cylinder) expands pitch; blue holder closes; if `CASE_SEL = 1` flip 180°; settled |
| ST_TRANSFER_1 | 5 | axis 1 grips the 4 units, platform vacuum OFF, blue holder opens, move to the lighting nest, nest vacuum ON, release; flip back home, axis 15 back to 0 (mechanism ready for the next batch) |
| ST_LIGHTING_CHECK | 6 | shade forward (closed sensor, own 500 ms timeout → 6003) → one CCD4 trigger → 4 results → shade back |
| ST_TRANSFER_2_BOTTOM | 7 | axis 2 grips from the nest (nest vacuum OFF), moves toward Transfer #3; one CCD3 hardware trigger pulse when axis 2 crosses `T2_TRIG_POS_MM`; at the hand-off and with 4 results → release onto Transfer #3 |
| ST_TOP_CHECK | 8 | CCD1 reach-out cylinder extend → one CCD1 trigger → 4 results incl. theta per unit → retract; Transfer #3 (axis 3) shuttles to the pick position |
| ST_TRANSFER_4_COMP | 9–10 | axis 4 grips from Transfer #3; axes 7–10 (one theta unit per CSE) rotate by `−UNIT_THETA[u]` during the move; place on the side-check nests, nest vacuum ON |
| ST_SIDE_CHECK | 11 | grippers lift → axes 11–14 (rotators, commanded together) one revolution at `SIDE_STEP_DEG · SIDE_FRAMES / SIDE_SWEEP_S` with a CCD2 trigger pulse every `SIDE_STEP_DEG` (36 frames in 1.8 s = 20 fps ≤ 23 fps) → results → lower |
| ST_TRANSFER_5_SORT | 12–13, 17 | axis 5 grips 4; per unit: OK → tray slot (row/col grid), NG → NG belt (`NG_ON_BELT += 1`) |
| ST_TRAY_HANDLING | 14, 16 | tray full (`TRAY_FULL` from the upside camera or slot counter): stack gripper lifts, full tray moves pos1 → pos2, gripper releases; loader feeds one empty tray |
| ST_NG_UNLOAD | 18 | loop while `NG_ON_BELT > 0`: belt runs to the holder bar → NG check CCD → axis 6 unloads to the NG tray (confirmed) or the OK tray (overturned, `FALSE_REJECT_COUNT`) |
| ST_ERROR | — | safe stop, operator reset |

Batch loop: after ST_TRANSFER_5_SORT (and tray/NG handling) the sequence goes to ST_POKAYOKE_CHECK while a basket with units remains (`BASKET_AT_HOLDER ∧ ¬BASKET_EMPTY`), to ST_BASKET_FEED when the basket is empty, and to ST_IDLE only on `CMD_STOP` or `¬AUTO_MODE`. Steps 1 and 15 are operator actions (no state).

Decisions: (a) `VISION_RESULT[cam, unit]` is per unit; the IPC sets `VISION_DONE[cam]` once all 4 cameras of a station have reported; (b) the NG check CCD (machine overview #13) reconfirms before NG unloading and overturned units go back to the OK tray via axis 6 — the spec only says "If NG → unload to NG conveyor → NG unloading"; (c) one batch in the state machine at a time (the README's 3.8 s/batch is a project figure, the spec states only > 85 K/day; inter-batch pipelining is a later PLC task); (d) axes: 1–6 transfers, 7–10 theta units on Transfer #4, 11–14 side rotators, 15 pitch e-cylinder; (e) the CCD4 hyper light and all strobes are driven by the Gardasoft controller under IPC control (`config/lighting/illumination_profiles.yaml`), the PLC only pulses camera trigger DOs.

### D5. Out of scope

Hikrobot SDK integration beyond compiling (no SDK on this host), Epson RC+ program, HMI, recipe storage, the Python simulators under `tools/` (only imports and the DefectType mirror are updated so they import and run), inter-batch PLC pipelining, golden-template baseline for CCD4.

---

## Global Constraints

- C++17, OpenCV ≥ 4.0 (host has 4.6), no new third-party runtime deps; GoogleTest only for tests. `-Wall -Wextra` clean.
- `DefectTypes.h`, `CseLocator.h`, `LightingCheck.h`, `ImagePreprocessor.h`, `DefectDetector.h` compile **without** `MvCameraControl.h`.
- Build must work with `cmake -DAOI_WITH_MVS=OFF -DAOI_BUILD_TESTS=ON` on this host; default `AOI_WITH_MVS=ON` keeps the production targets, so `pybind_module.cpp` and `GigEVisionCapture.*` must stay consistent with the new headers even though they are not compiled here.
- Every detection threshold lives in `DetectionParams` / `LocatorParams` / `LightingParams` and is mirrored by name in `config/vision-system/ccdN_*.yaml`.
- Pixel pitch: CCD1–3 0.0115 mm/px (FOV 28×24 mm → 2448×2048), CCD4 0.0069 mm/px (37.9×25.3 mm → 5472×3648) (slide 10). Expected pin count default 4 (slides 35/38/39/44 show 4; 40–43 show 3 — recipe value).
- Every detector returns, never throws: an empty `cv::Mat` is handled before any `cvtColor`.
- ST dialect: GX Works3 style as already used (`R_TRIG`, `TON`, `ARRAY[1..n] OF`, `16#` literals, literal initial values only).
- Commit messages in English, conventional-commit prefixes, **no** Co-Authored-By trailer. Author line in new file headers: `Rongxuan Zhou`.

## Review Focus

1. Disc not found (empty Mat, dark frame, noisy dark frame): `Detect()` returns exactly one `DefectResult` of `PrimaryTypeOf(cam)` with `description="disc not located"` and confidence 1.0; never throws. Tests in Task 3 (`Routing.*`), Task 2 (`NoDiscReturnsNotFound` incl. a noisy dark frame).
2. Key notch vs broken rim: a good part passes; a chip at another angle, or an over-deep notch, is BROKEN. Tests in Task 4.
3. CCD4 colour independence and structure tolerance: clean amber/green/blue pass; a dark banana plus a thin bright radial line pass; a compact bright spot fails on all three colours. Tests in Task 5.
4. Pin recipe and empty holes: 3-pin and 4-pin recipes; a hole without a core is PIN_MISSING even when the hole count is right. Tests in Task 6.
5. PLC: a station whose result never arrives ends in ST_ERROR with code 8002 (via `VISION_PENDING`), ST_ERROR clears triggers/pending/shade/vacuum/belt and keeps the holding-type outputs. Checked by the lint's required-reset list and by reading, Task 9.

---

### Task 1: SDK-free algorithm library, multi-camera slots, test scaffolding

**Files:**
- Create: `src/vision/CameraID.h`
- Modify: `src/vision/GigEVisionCapture.h`, `src/vision/GigEVisionCapture.cpp` (remove the enum, include `CameraID.h`, add `unit` to `CameraConfig`, address handles by (station, unit), bounds checks)
- Modify: `src/vision/ImagePreprocessor.h` (`#include "CameraID.h"` instead of `GigEVisionCapture.h`)
- Modify: `src/vision/CMakeLists.txt`
- Create: `src/vision/CseLocator.h/.cpp` and `src/vision/LightingCheck.h/.cpp` as **compilable placeholders** (`#pragma once` + `#include <opencv2/core.hpp>` + `namespace aoi {}` / `.cpp` includes its header) — replaced in Tasks 2 and 3
- Create: `src/vision/tests/CMakeLists.txt`, `src/vision/tests/synthetic.h`, `src/vision/tests/test_smoke.cpp`

**Interfaces:**
- `enum class aoi::CameraID : int { CCD1_TOP=1, CCD2_SIDE=2, CCD3_BOTTOM=3, CCD4_INNER=4, NG_CHECK=5, POKAYOKE=6, TRAY_MONITOR=7 }`.
- `CameraConfig` gains `int unit = 1;` (1..4 for the inspection stations, 1 otherwise). `GigEVisionCapture`: `static constexpr int kMaxCameras = 20; std::vector<CameraHandle> cameras_;` and `int FindSlot(CameraID id, int unit) const` (−1 when absent). Public API gains the `unit` parameter with default 1: `StartAcquisition(CameraID, int unit = 1)`, `StopAcquisition(...)`, `SetTriggerMode(...)`, `SetExposure(CameraID, int unit, float)`, `RegisterFrameCallback(CameraID, int unit, FrameCallback)`, `GetFrame(CameraID, int unit = 1, int timeout_ms = 1000)`; `FrameCallback` becomes `(CameraID, int unit, uint64_t, const cv::Mat&)`. `StopAll()` iterates the vector. Every accessor returns false / empty on slot −1.
- `aoi::test` helpers (exact code in Step 3).

- [ ] **Step 1: Create `CameraID.h`** (as in Interfaces, with the header comment "Author: Rongxuan Zhou").
- [ ] **Step 2: Edit `GigEVisionCapture.h/.cpp`** per Interfaces. `CameraHandle` keeps `handle, config, callback, ring, acquiring`; because `std::atomic` and the ring are non-copyable, store `std::vector<std::unique_ptr<CameraHandle>>`. `Initialize()` creates one handle per config and matches by serial+subnet as today; log line prints `CCD%d/%d`.
- [ ] **Step 3: Create `src/vision/tests/synthetic.h`**

```cpp
#pragma once
#include <cmath>
#include <vector>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
namespace aoi::test {

// White ceramic disc on black; notch angle in image convention (0 = +x, 90 = +y/down).
inline cv::Mat MakeDisc(int w, int h, cv::Point2f c, float r, bool notch = true,
                        float notch_angle_deg = 270.f, float notch_r = 0.f) {
    cv::Mat img(h, w, CV_8UC3, cv::Scalar(8, 8, 8));
    cv::circle(img, c, (int)r, cv::Scalar(235, 238, 240), cv::FILLED, cv::LINE_AA);
    if (notch) {
        if (notch_r <= 0) notch_r = r * 0.06f;
        float a = notch_angle_deg * (float)CV_PI / 180.f;
        cv::Point2f n(c.x + r * std::cos(a), c.y + r * std::sin(a));
        cv::circle(img, n, (int)notch_r, cv::Scalar(8, 8, 8), cv::FILLED, cv::LINE_AA);
    }
    return img;
}

// Pin hole with epoxy ring. has_pin draws the bright gold core; ring_fill in [0,1]; overflow_px grows the epoxy blob.
inline void DrawPinHead(cv::Mat& img, cv::Point2f p, float r_head, float ring_px,
                        float ring_fill = 1.f, float overflow_px = 0.f, bool has_pin = true) {
    const cv::Scalar epoxy(40, 90, 120);
    if (overflow_px > 0)
        cv::circle(img, p, (int)(r_head + ring_px + overflow_px), epoxy, cv::FILLED, cv::LINE_AA);
    cv::ellipse(img, p, cv::Size((int)(r_head + ring_px / 2), (int)(r_head + ring_px / 2)),
                0, 0, 360.0 * ring_fill, epoxy, (int)ring_px, cv::LINE_AA);
    cv::circle(img, p, (int)r_head, cv::Scalar(70, 80, 70), cv::FILLED, cv::LINE_AA);   // dark hole
    if (has_pin) cv::circle(img, p, (int)(r_head * 0.4f), cv::Scalar(120, 200, 240), cv::FILLED, cv::LINE_AA); // gold core
}

inline void DrawCode(cv::Mat& img, cv::Point2f center, bool blur = false) {
    cv::putText(img, "27501-76HC", center + cv::Point2f(-70, -8), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(30, 30, 40), 2);
    cv::putText(img, "52149001-8", center + cv::Point2f(-70, 18), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(30, 30, 40), 2);
    if (blur) {
        cv::Rect roi((int)center.x - 90, (int)center.y - 30, 180, 60);
        roi &= cv::Rect(0, 0, img.cols, img.rows);
        cv::GaussianBlur(img(roi), img(roi), cv::Size(7, 7), 2);
    }
}

// Grey (single-channel) noise patch converted to BGR, so saturation stays ~0.
inline void DrawGreyNoise(cv::Mat& img, cv::Rect r, int lo, int hi) {
    cv::Mat g(r.size(), CV_8UC1); cv::randu(g, lo, hi);
    cv::Mat bgr; cv::cvtColor(g, bgr, cv::COLOR_GRAY2BGR);
    bgr.copyTo(img(r));
}

// Side view in the *detector's internal* orientation (pins below the body). Tests pass pins_up=false.
// pin_colour default is bright gold like a healthy pin.
inline cv::Mat MakeSideView(int w, int h, cv::Rect body, const std::vector<int>& pin_x,
                            const std::vector<float>& pin_angles_deg, const std::vector<int>& pin_len_px,
                            int pin_w = 6, cv::Scalar pin_colour = cv::Scalar(40, 170, 220)) {
    cv::Mat img(h, w, CV_8UC3, cv::Scalar(8, 8, 8));
    cv::rectangle(img, body, cv::Scalar(230, 232, 235), cv::FILLED);
    for (size_t i = 0; i < pin_x.size(); ++i) {
        float a = pin_angles_deg[i] * (float)CV_PI / 180.f;
        cv::Point2f top((float)pin_x[i], (float)(body.y + body.height));
        cv::Point2f tip(top.x + pin_len_px[i] * std::sin(a), top.y + pin_len_px[i] * std::cos(a));
        cv::line(img, top, tip, pin_colour, pin_w, cv::LINE_AA);
    }
    return img;
}

// CCD4 transmitted-light frame: dark background, tinted annulus [0.35R, R] with radial falloff, dark centre.
// With nest_ring=true a dark-grey nest ring of radius 1.25R is drawn around (centre + ring_offset).
inline cv::Mat MakeLitAnnulus(int w, int h, cv::Point2f c, float R, cv::Scalar bgr_tint,
                              bool nest_ring = false, cv::Point2f ring_offset = {0, 0}) {
    cv::Mat img(h, w, CV_8UC3, cv::Scalar(5, 5, 5));
    if (nest_ring) cv::circle(img, c + ring_offset, (int)(R * 1.25f), cv::Scalar(60, 60, 60), (int)(R * 0.12f), cv::LINE_AA);
    cv::Mat disc(h, w, CV_8UC3, cv::Scalar(0, 0, 0));
    cv::circle(disc, c, (int)R, bgr_tint, cv::FILLED, cv::LINE_AA);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float d = std::hypot(x - c.x, y - c.y) / R;
            if (d <= 1.f) { cv::Vec3b& v = disc.at<cv::Vec3b>(y, x); for (int k = 0; k < 3; ++k) v[k] = (uchar)(v[k] * (1.f - 0.25f * d)); img.at<cv::Vec3b>(y, x) = v; }
        }
    cv::circle(img, c, (int)(R * 0.35f), cv::Scalar(10, 10, 10), cv::FILLED, cv::LINE_AA);
    return img;
}

}  // namespace aoi::test
```

- [ ] **Step 4: Rewrite `src/vision/CMakeLists.txt`** (options `AOI_WITH_MVS` ON, `AOI_BUILD_TESTS` OFF; `aoi_vision_algo` STATIC from `CseLocator.cpp ImagePreprocessor.cpp LightingCheck.cpp DefectDetector.cpp` with `target_compile_options(... PRIVATE -Wall -Wextra)`; under `AOI_WITH_MVS`: pybind11 + MVS lookup, `aoi_vision_core` SHARED from `GigEVisionCapture.cpp` linking `aoi_vision_algo`, `pybind11_add_module(_aoi_vision_core pybind_module.cpp)`; under `AOI_BUILD_TESTS`: `enable_testing()` + `add_subdirectory(tests)`).
- [ ] **Step 5: `tests/CMakeLists.txt`**: `find_package(GTest REQUIRED)`, `add_executable(aoi_vision_tests test_smoke.cpp)`, link `aoi_vision_algo GTest::gtest GTest::gtest_main`, `add_test`. `test_smoke.cpp`: `CameraID::CCD4_INNER == 4`, `NG_CHECK == 5`; `MakeDisc` centre pixel > 200 and corner < 20.
- [ ] **Step 6: Build & run** — `cmake -S src/vision -B src/vision/build -DAOI_WITH_MVS=OFF -DAOI_BUILD_TESTS=ON && cmake --build src/vision/build -j8 && ctest --test-dir src/vision/build --output-on-failure` → 2 pass.
- [ ] **Step 7: Commit** — `build: SDK-free algorithm library, per-unit camera slots, gtest scaffold`

---

### Task 2: `CseLocator`

**Files:** `src/vision/CseLocator.h/.cpp` (real content), `src/vision/tests/test_cse_locator.cpp`

**Interfaces:**
```cpp
#pragma once
#include <cmath>
#include <vector>
#include <opencv2/core.hpp>
namespace aoi {
struct CseGeometry {
    bool found = false; cv::Point2f center; float radius_px = 0.f;
    float notch_angle_deg = NAN; float notch_depth_px = 0.f; float notch_width_deg = 0.f;
    cv::Mat disc_mask;                    // CV_8U, 255 inside the fitted circle
    std::vector<cv::Point> contour;       // outer contour of the bright blob
    cv::Mat fg_mask;                      // Otsu foreground used for the rim profile
};
struct LocatorParams {
    int   min_disc_radius_px = 60; int min_otsu_thresh = 30;
    float notch_width_deg = 7.f; float notch_depth_px = 10.f; float fit_inlier_ratio = 0.03f;
    int   interior_margin_px = 20;
};
class CseLocator {
public:
    explicit CseLocator(LocatorParams p = {});
    CseGeometry Locate(const cv::Mat& bgr_or_gray) const;
    std::vector<float> RimProfile(const CseGeometry& g, int n_angles = 720) const;
    /// disc_mask eroded by interior_margin_px minus a 1.5*notch_depth circle at the notch.
    cv::Mat InteriorMask(const CseGeometry& g) const;
    static cv::Mat PolarUnwrap(const cv::Mat& img, cv::Point2f c, float r_in, float r_out, int out_w, int out_h);
    static bool FitCircleLSQ(const std::vector<cv::Point>& pts, cv::Point2f& c, float& r);
    const LocatorParams& params() const { return p_; }
private: LocatorParams p_;
};
}
```

- [ ] **Step 1: Tests** — `FindsCenterRadiusAndNotch` (R=150, notch_r=9 at 270°: centre ±1.5, radius ±2, angle ±4, depth > 5), `NotchDoesNotBiasFit`, `NoDiscReturnsNotFound` (empty Mat; constant (5,5,5); **noisy dark** frame `randu(0, 25)`), `RimProfileFlatExceptNotch` (notch at 90°: `prof[0]≈150±2`, `prof[90] < 145`), `PolarUnwrapShape` (60×720), `InteriorMaskExcludesRimAndNotch` (pixel at the notch and at the rim are 0, centre is 255).
- [ ] **Step 2: Run → fail.** **Step 3: Implement** per D3 (Kåsa fit = the scratchpad prototype; rim march from `1.08R` inward in 0.5 px steps until `fg_mask` is 255; runs with wrap-around). **Step 4: Run → pass.**
- [ ] **Step 5: Commit** — `feat(vision): CseLocator for disc fit, key-notch angle, interior mask and polar unwrap`

---

### Task 3: Taxonomy, parameter structs, `LightingCheck` declaration, `DefectDetector` skeleton

**Files:**
- Create: `src/vision/DefectTypes.h` (enums, `DefectResult`, `GroupOf`, `SeverityOf`, `ToString`)
- Create/replace: `src/vision/LightingCheck.h` (full declaration), `src/vision/LightingCheck.cpp` (`Analyse` returns `located=false` until Task 5; `SetParams`/`params()` real)
- Rewrite: `src/vision/DefectDetector.h/.cpp` (routing, guards, primitives returning `{}`)
- Modify: `src/vision/pybind_module.cpp` (remove `load_templates`; enums → 21 names + `DefectGroup`; `DetectionParams` readwrite list = the new fields; expose `LightingCheck`)
- Test: `src/vision/tests/test_defect_detector_routing.cpp`; add `aoi::test::SyntheticParams()` to `synthetic.h` (returns `DetectionParams{}` — header defaults are the synthetic scale)

**Interfaces:**
```cpp
// DefectTypes.h
enum class DefectGroup : int { FUNCTION, COSMETIC, ASSEMBLY, INNER_RESIDUE };
enum class DefectType : int { LIGHTING_CHECK = 0, CRACK, BROKEN, EPOXY_EXPOSAL, PIN_MISSING, ELECTRICAL_CONTAMINATION,
    GOLD_EXPOSAL, INSUFFICIENT_EPOXY, EPOXY_OVERFLOW, DYEING_CONTAMINATION, NON_ELECTRICAL_CONTAMINATION, NO_CODE, CODE_BLUR,
    PIN_BENT, PIN_OXIDIZED, PIN_BUR, PIN_MIS_CUT, EPOXY_HIGHER_THAN_CERAMIC, MISALIGNMENT, STAINING_ON_EDGE, YELLOW_GLASS_CEMENT, COUNT };
enum class Severity : int { CRITICAL = 0, MAJOR = 1, MINOR = 2 };
DefectGroup GroupOf(DefectType); Severity SeverityOf(DefectType); const char* ToString(DefectType);  // snake_case names
struct DefectResult { DefectType type; Severity severity; float confidence; cv::Rect bbox; CameraID camera; std::string description; };

// LightingCheck.h
enum class ThicknessClass : int { UNKNOWN = 0, AMBER, GREEN, BLUE };
struct LightingParams { float inner_zone_ratio = 0.40f, outer_zone_ratio = 0.98f, edge_band_ratio = 0.85f;
    int unwrap_w = 1440, unwrap_h = 160; int leak_residual_min = 45, leak_area_min_px = 80;
    int stain_residual_min = 45, stain_area_min_px = 120, stain_area_max_px = 3000;
    int cement_hue_low = 20, cement_hue_high = 40, cement_sat_delta = 40, cement_val_delta = 30, cement_area_min_px = 120;
    double blob_aspect_max = 4.0; int min_lit_radius_px = 60, min_otsu_thresh = 30;
    bool nest_ring_enabled = true; int nest_val_min = 25; double mm_per_px = 0.0069, misalign_max_offset_mm = 0.15; };
struct LightingResult { bool located = false, passed = false; ThicknessClass thickness = ThicknessClass::UNKNOWN;
    cv::Point2f center; float radius_px = 0; bool nest_found = false; cv::Point2f nest_center; float offset_mm = 0;
    int leak_area_px = 0, stain_area_px = 0, cement_area_px = 0; std::vector<DefectResult> defects; };
class LightingCheck { public: explicit LightingCheck(LightingParams p = {}); LightingResult Analyse(const cv::Mat& bgr) const;
    void SetParams(const LightingParams&); const LightingParams& params() const; private: LightingParams p_; };

// DefectDetector.h
struct DetectionParams {
    LocatorParams locator; LightingParams lighting;
    double mm_per_px_ccd123 = 0.0115; cv::Point2f fixture_center_ccd1{-1,-1}, fixture_center_ccd3{-1,-1}; double misalign_max_offset_mm = 0.15;
    int expected_pin_count = 4;
    int crack_blackhat_kernel_px = 15, crack_blackhat_thresh = 25; double crack_aspect_min = 4.0; int crack_min_length_px = 30; float broken_depth_px = 6.f;
    int pin_head_dark_thresh = 110; float pin_head_radius_px = 18.f, epoxy_ring_px = 10.f; int pin_core_val_min = 170, pin_core_area_min_px = 20;
    int epoxy_sat_min = 50, epoxy_val_max = 160; double epoxy_ring_min_fill = 0.6, epoxy_overflow_ratio = 2.5; int epoxy_hue_low = 8, epoxy_hue_high = 30;
    int exposal_sat_max = 40; double exposal_texture_min = 18.0; int exposal_area_min_px = 300;
    int contam_area_min_px = 40, dyeing_sat_min = 70, electrical_dark_max = 120; double electrical_texture_min = 20.0;
    int code_roi_w = 260, code_roi_h = 90, code_offset_px = 30, code_ink_contrast = 30; double code_min_ink_ratio = 0.03, code_laplacian_blur_limit = 80.0;
    bool pins_up = true; int side_fg_thresh = 40, pin_w_max = 14; double pin_bend_angle_deg_max = 2.0; int pin_length_px = 120, pin_length_tol_px = 15, pin_bur_width_px = 4, pin_oxide_val_max = 120;
    int gold_area_min_px = 60, epoxy_bump_px = 5, epoxy_bump_width_px = 8;
};
class DefectDetector {
public:
    DefectDetector(); explicit DefectDetector(const DetectionParams&);
    std::vector<DefectResult> Detect(CameraID cam, const cv::Mat& image);
    void SetParams(const DetectionParams& p);   // also rebuilds locator_ and lighting_
    const DetectionParams& GetParams() const;
    static DefectType PrimaryTypeOf(CameraID cam);   // CCD1 MISALIGNMENT, CCD2 PIN_BENT, CCD3 BROKEN, CCD4 LIGHTING_CHECK, others MISALIGNMENT
    static DefectResult NotLocated(CameraID cam);
private: DetectionParams params_; CseLocator locator_; LightingCheck lighting_; /* primitives as in rev 1, plus DetectNestMisalignment via LightingResult */ };
```

- [ ] **Step 1: Tests** (`Taxonomy.Has21TypesInSpecOrder`, `Taxonomy.CriticalSet`, `Routing.EmptyOrDarkFrameIsNgNotLocated` for CCD1–4 with empty Mat, constant (3,3,3) and `randu(0,25)` frames, `Routing.GrayInputAccepted`, `Routing.SetParamsPropagates`: set `locator.notch_width_deg = 9` then `GetParams().locator.notch_width_deg == 9`).
- [ ] **Step 2: Run → fail.** **Step 3: Implement** (guards: `if (image.empty()) return {NotLocated(cam)};` first thing in `Detect` and in `LightingCheck::Analyse` return `located=false`). **Step 4: pybind update.** **Step 5: Build & run → pass.**
- [ ] **Step 6: Commit** — `feat(vision): spec V1.4 defect taxonomy, parameter structs and CCD routing skeleton`

---

### Task 4: CRACK, BROKEN, MISALIGNMENT on CCD1/CCD3

**Files:** `DefectDetector.cpp`; `tests/test_ceramic_defects.cpp`

- [ ] **Step 1: Tests** — `GoodDiscWithNotchPasses` (R=150, notch_r=9: no BROKEN/CRACK/MISALIGNMENT), `SecondChipIsBroken` (chip r=14 at 0°), `OversizedNotchIsBroken` (notch_r=28), `NoNotchFoundIsBroken` (disc without notch but with a 10 px chip → BROKEN), `ThinDarkLineIsCrack` (2 px line (120,120,120) from (260,200) to (380,300)), `RoundDotIsNotCrack`, `OffsetDiscIsMisaligned` (fixture (320,240): 2 px ok, 20 px NG).
- [ ] **Step 2: fail. Step 3: implement per D3 (minAreaRect elongation; interior mask from the locator; by-angle notch exemption). Step 4: pass.**
- [ ] **Step 5: Commit** — `feat(vision): crack, broken and misalignment detection on CCD1/CCD3`

---

### Task 5: `LightingCheck` implementation (CCD4)

**Files:** `LightingCheck.cpp`; `DefectDetector.cpp` (`DetectCCD4`: convert `LightingResult` → results, add MISALIGNMENT from `offset_mm`); `tests/test_lighting_check.cpp`

- [ ] **Step 1: Tests** (tints AMBER (20,150,230), GREEN (140,190,150), BLUE (220,190,150)):
  - `CleanAnnulusPassesInAllThreeColours`; `ThicknessClassFollowsHue`;
  - `StructuredAnnulusPasses`: AMBER + dark banana (`ellipse` centre c+(0,−0.6R), axes (0.35R, 0.12R), colour (15,15,15)) + bright radial line (`line` from 0.45R to 0.95R at 30°, 2 px, white) → `passed`;
  - `BrightSpotIsLeakageInAllColours` (r=9 white dot at 0.7R; CRITICAL);
  - `DarkCompactPatchOnRimIsEdgeStaining` (r=10 dark dot at 0.925R; STAINING_ON_EDGE and not LIGHTING_CHECK);
  - `SaturatedYellowBlobIsCementOnBlueAndAmber` ((0,200,230) r=11 at 0.65R on BLUE **and** on AMBER);
  - `NestRingOffsetIsMisalignment` (`nest_ring=true`, `ring_offset=(0,0)` → `offset_mm < 0.05`, `ring_offset=(30,0)` → MISALIGNMENT via `DefectDetector::Detect(CCD4)`);
  - `DarkFrameNotLocated` (constant and noisy).
- [ ] **Step 2: fail. Step 3: implement per D3 (per-row median via `cv::sort` on each row; residual CV_16S; blob filters by area and minAreaRect aspect; polar → source bbox by mapping the 4 polar-bbox corners; nest ring from `V > nest_val_min`). Step 4: pass.**
- [ ] **Step 5: Commit** — `feat(vision): CCD4 lighting check with thickness class, inner staining residue and nest alignment`

---

### Task 6: Top-side pins, epoxy, code (CCD1)

**Files:** `DefectDetector.cpp`; `tests/test_top_side.cpp`

Synthetic top part: disc R=220 at (400,300), notch at 90° r=12; pin positions `{265,200},{355,160},{445,160},{535,200}` (≥ 90 px apart so rings never merge); `DrawPinHead(…, 18, 10, fill, overflow, has_pin)`; code at (400,330).

- [ ] **Step 1: Tests** — `GoodPartPasses` (prints every result as failure), `HoleWithoutCoreIsPinMissing` (4 holes, one `has_pin=false`), `ThreeOfFourHolesIsMissing`, `ThreePinRecipePasses`, `HalfRingIsInsufficientEpoxy` (fill 0.4), `BigBlobIsOverflow` (overflow 22 px), `NoCodeAndBlur` (no code → NO_CODE; 7×7/σ2 blur → CODE_BLUR and not NO_CODE), `GreyGranularPatchIsEpoxyExposal` (`DrawGreyNoise(img, {300,420,80,60}, 90, 200)`).
- [ ] **Step 2: fail. Step 3: implement per D3. Step 4: pass; rerun Task 4 tests.**
- [ ] **Step 5: Commit** — `feat(vision): top-side pin, epoxy and marking-code inspection on CCD1`

---

### Task 7: Contamination (CCD1/CCD3)

**Files:** `DefectDetector.cpp`; `tests/test_contamination.cpp`

- [ ] **Step 1: Tests** on a bottom part (R=220 disc, notch 90°, amber banana ellipse (90,40) at 30°): `CleanBottomPasses`, `DarkTexturedStreakIsElectrical` (`DrawGreyNoise(img, {470,180,110,30}, 20, 120)`), `LightSmudgeIsNonElectrical` (grey ellipse (170,175,180)), `MagentaStainIsDyeing` ((200,120,220) ellipse).
- [ ] **Step 2–4: fail → implement per D3 → pass.** **Step 5: Commit** — `feat(vision): classify dyeing, electrical and non-electrical contamination`

---

### Task 8: Side view (CCD2)

**Files:** `DefectDetector.cpp`; `tests/test_side_view.cpp`

Tests set `p.pins_up = false` (synthetic draws pins below) except `PinsUpFrameIsFlipped` which flips the synthetic image with `cv::flip(img, img, 0)` and runs with `pins_up = true`, expecting the same pass result.

- [ ] **Step 1: Tests** — `StraightPinsPass`, `PinsUpFrameIsFlipped`, `TiltedPinIsBent` (6°), `ShortPinIsMisCut` (80 px), `WideTipIsBur` (18×10 block at the tip), `DarkPinIsOxidized` (pin colour (20,50,100)), `BrownPatchOnWallIsGoldExposal`, `BumpOnTopEdgeIsEpoxyHigher`.
- [ ] **Step 2–4: fail → implement per D3 → pass.** **Step 5: Commit** — `feat(vision): side-view pin geometry, oxidation, gold exposal and epoxy bump on CCD2`

---

### Task 9: PLC `MAIN_SEQUENCE` rewrite, servo config, I/O map, lint

**Files:** `src/plc/main_sequence.st` (rewrite), `src/plc/servo_config.st`, `src/plc/io_mapping.csv`, create `tools/plc/st_lint.py`

**PLC ↔ IPC interface (EtherNet/IP CIP implicit):**
- Camera triggers are **hardware DO pulses** (`Y140..Y143`, `Y234` NG, `Y235` Poka-Yoke) of `T_TRIG_PULSE := T#2ms` generated by a per-station `TON`; the PLC sets `VISION_PENDING[cam] := TRUE` and `VISION_SEQ := VISION_SEQ + 1` when it pulses. IPC → PLC: `VISION_RESULT : ARRAY[1..5, 1..4] OF TYPE_VISION_RESULT` (`JUDGE, DEFECT_CODE (0 = OK, 1..21 = DefectType+1), CONFIDENCE, OFFSET_X_MM, OFFSET_Y_MM, THETA_DEG, INSPECT_TIME_MS`), `VISION_DONE[cam]` (set when all 4 cameras of the station reported for `VISION_SEQ_ECHO = VISION_SEQ`), `IPC_HEARTBEAT`. The PLC clears `VISION_PENDING[cam]` on the `VISION_DONE[cam]` rising edge and copies results.
- Poka-Yoke: `PY_DONE_EDGE : R_TRIG`, `PY_CASE_SEL` (1|2, default 2 when 0), `PY_OK`.
- Robot: `ROBOT_START`, `ROBOT_CASE_SEL` (DO); `ROBOT_READY`, `ROBOT_BUSY`, `ROBOT_DONE`, `ROBOT_ALARM` (DI).

- [ ] **Step 1: `tools/plc/st_lint.py`** — block pairs with `(?:SAFETY_)?PROGRAM`, VAR variants `VAR(_INPUT|_OUTPUT|_IN_OUT|_TEMP|_GLOBAL|_EXTERNAL|_STAT)?`, `ELSIF` not counted, CASE labels regex `^\s*(ST_[A-Z0-9_]+)\s*:(?!=)`, per-file exit status, and a required-reset check: inside the `ST_ERROR:` block of `main_sequence.st` every name in `REQUIRED_ERROR_RESETS = [VISION_PENDING, SOL_SHADE_FWD, SOL_PLATFORM_VACUUM, SOL_NEST_VACUUM_CCD4, SOL_NEST_VACUUM_SIDE, SOL_GRIPPER_LIFT, NG_BELT_RUN, CYCLE_RUNNING, ROBOT_START]` must appear on the left of `:= FALSE`. Run on the three existing files first: `safety_program.st OK`, `servo_config.st OK`, `main_sequence.st` currently OK except the reset list (expected to fail until Step 3).
- [ ] **Step 2: Rewrite `main_sequence.st`.** Structure:
  - TYPE: `E_INSPECT_STATE` (ST_IDLE 0, ST_BASKET_FEED 1, ST_POKAYOKE_CHECK 2, ST_ROBOT_PICK 3, ST_PITCH_CHANGE 4, ST_TRANSFER_1 5, ST_LIGHTING_CHECK 6, ST_TRANSFER_2_BOTTOM 7, ST_TOP_CHECK 8, ST_TRANSFER_4_COMP 9, ST_SIDE_CHECK 10, ST_TRANSFER_5_SORT 11, ST_TRAY_HANDLING 12, ST_NG_UNLOAD 13, ST_ERROR 99); `TYPE_VISION_RESULT` as above; `TYPE_AXIS_CMD` + `ACTUAL_POS : REAL` (engineering units) and `REL_START : REAL`.
  - VAR: as D4 requires; all names used in the bodies are declared (checklist: `SUB_STEP`, `NEXT_STATE`, `SUB_STEP_PREV`, `STEP_TIMEOUT_SEL : TIME`, `AXIS : ARRAY[1..15]`, `VISION_PENDING[1..5]`, `VISION_SEQ`, `VISION_DONE_EDGE[1..5]`, `TRIG_PULSE[1..5] : TON`, `UNIT_NG[1..4]`, `UNIT_THETA[1..4]`, `NG_ON_BELT : INT`, `CASE_SEL`, `PY_DONE_EDGE`, `LIFTER_UP/LIFTER_DOWN`, `HOLDER_OPEN/HOLDER_CLOSED`, `FLIP_DONE/FLIP_HOME`, `SOL_NEST_VACUUM_CCD4`, `SOL_NEST_VACUUM_SIDE`, `SOL_T5_RELEASE[1..4]`, `TRAY_ROW/TRAY_COL : INT`, `TRAY_ROWS := 5`, `TRAY_COLS := 10`, `TRAY_PITCH_X_MM := 16.0`, `TRAY_PITCH_Y_MM := 16.0`, `SHADE_TIMER : TON`, `HB_TIMER : TON`, `HB_EDGE : R_TRIG`, `GROUPS_PER_BASKET := 10`, `GROUPS_PICKED : INT`).
  - VAR CONSTANT (literal values): `POS_T1_LIGHTING_MM := 180.0`, `POS_T2_HANDOFF_MM := 220.0`, `T2_TRIG_POS_MM := 60.0`, `POS_T3_PICK_MM := 80.0`, `POS_T4_SIDE_MM := 200.0`, `POS_T5_TRAY_X0_MM := 240.0`, `POS_T5_NG_BELT_MM := 320.0`, `POS_T6_NG_TRAY_MM := 150.0`, `POS_T6_OK_TRAY_MM := 60.0`, `PITCH_EXPAND_MM := 9.46`, `SIDE_FRAMES := 36`, `SIDE_STEP_DEG := 10.0`, `SIDE_SWEEP_S := 1.8`, `T_TRIG_PULSE := T#2ms`, `T_SHADE := T#500ms`, `T_STEP_DEFAULT := T#5s`, `T_STEP_LONG := T#15s`, `T_HB := T#1s`.
  - Transition rule: bodies only assign `NEXT_STATE`. After `END_CASE`: `IF NEXT_STATE <> CURRENT_STATE THEN CURRENT_STATE := NEXT_STATE; STATE_ENTRY := TRUE; SUB_STEP := 0; END_IF;`. Watchdog: `STEP_TIMER` resets whenever `STATE_ENTRY` or `SUB_STEP <> SUB_STEP_PREV`; `STEP_TIMEOUT_SEL` is `T_STEP_LONG` in ST_ROBOT_PICK, ST_SIDE_CHECK, ST_TRANSFER_5_SORT, ST_TRAY_HANDLING, ST_NG_UNLOAD, else `T_STEP_DEFAULT`; on expiry: code 8002 if any `VISION_PENDING[i]`, else 8001.
  - Vision block (runs every scan, before the CASE): for i = 1..5: `TRIG_PULSE[i](IN := VISION_TRIG[i], PT := T_TRIG_PULSE); IF TRIG_PULSE[i].Q THEN VISION_TRIG[i] := FALSE; END_IF; VISION_DONE_EDGE[i](CLK := VISION_DONE[i]); IF VISION_DONE_EDGE[i].Q THEN VISION_PENDING[i] := FALSE; END_IF;`. Helper semantics "fire(cam)" = `VISION_SEQ := VISION_SEQ + 1; VISION_PENDING[cam] := TRUE; VISION_TRIG[cam] := TRUE;` and "done(cam)" = `NOT VISION_PENDING[cam]`.
  - State bodies as in D4, written with SUB_STEP; ST_TRANSFER_2_BOTTOM fires CCD3 once when `AXIS[2].ACTUAL_POS >= T2_TRIG_POS_MM` (latched by `T2_FIRED`); ST_SIDE_CHECK records `AXIS[11].REL_START := AXIS[11].ACTUAL_POS`, commands axes 11–14 to `REL_START + 360.0` at `360.0 / SIDE_SWEEP_S` deg/s and fires CCD2 when `ACTUAL_POS >= REL_START + SIDE_FRAME_IDX * SIDE_STEP_DEG` until 36; waits `NOT VISION_PENDING[2]` after the sweep. ST_NG_UNLOAD loops while `NG_ON_BELT > 0`. Heartbeat: `HB_EDGE(CLK := IPC_HEARTBEAT); HB_TIMER(IN := CYCLE_RUNNING AND NOT HB_EDGE.Q, PT := T_HB)`; `HB_TIMER.Q` → 8003.
  - ST_ERROR entry resets exactly: `VISION_TRIG[1..5]`, `VISION_PENDING[1..5]`, `SOL_TRANSFER_GRIP[1..6]`, `TRANSFER_ACTIVE[1..6]`, `SOL_PLATFORM_VACUUM`, `SOL_NEST_VACUUM_CCD4`, `SOL_NEST_VACUUM_SIDE`, `SOL_SHADE_FWD`, `SOL_CCD1_REACH`, `SOL_GRIPPER_LIFT`, `SOL_T5_RELEASE[1..4]`, `NG_BELT_RUN`, `ROBOT_START`, `CYCLE_RUNNING`, `SUB_STEP`. Holding-type outputs are kept: `SOL_BASKET_STACK_HOLD`, `SOL_HOLDING_BLOCK`, `SOL_L_TRIGGER`, `SOL_BLUE_HOLDER_CLOSE`, `SOL_FLIP_180`, `SOL_STACK_GRIPPER`, `SOL_NG_HOLDER_BAR` (a comment says why).
  - Error codes: 9001 E-stop, 9002 interlock, 8001 step timeout, 8002 vision timeout, 8003 IPC heartbeat, 7001 consecutive NG batches, 6001 Poka-Yoke NOGO, 6002 robot alarm, 6003 shade failed to close.
- [ ] **Step 3: `servo_config.st`** — arrays `[1..15]`; axis names/comments: 1 TRANSFER_1 (pitch mech → lighting nest), 2 TRANSFER_2 (lighting nest → Transfer 3, CCD3 on the fly), 3 TRANSFER_3 (top-check shuttle), 4 TRANSFER_4 (Transfer 3 → side nests, theta-compensated), 5 TRANSFER_5 (side nests → tray / NG belt), 6 TRANSFER_6 (NG belt → NG tray / OK tray), 7–10 THETA_1..4 (rotary, 72:1, ±180°), 11–14 SIDE_ROT_1..4 (rotary, 50:1, continuous), 15 PITCH_ECYL (linear, lead 5 mm, 200 mm/s, limits −1..15 mm, in-pos 0.01); `HOME_ORDER := [7,8,9,10,11,12,13,14,15,1,2,3,4,5,6]`; station assignment comment updated (axes 1–4 station 2, 5–8 station 3, 9–12 station 4, 13–15 station 5).
- [ ] **Step 4: `io_mapping.csv`** — append rows for every new DI/DO (basket feeder, Poka-Yoke, robot, pitch/flip/holder, nest vacuums, shade, CCD1 reach, gripper lift, tray loader/stacker/full camera, NG belt/holder bar, NG CCD & Poka-Yoke triggers, SERVO9..15 status/enable). Addresses: X200–X2xx / Y200–Y2xx in blocks per subsystem; keep existing rows.
- [ ] **Step 5: Lint** → all three files OK (including the reset list); read ST_ERROR and the vision block against Review Focus 5.
- [ ] **Step 6: Commit** — `feat(plc): rewrite main sequence to the V1.4 process flow with per-unit results and 15 axes`

---

### Task 10: Configs, docs, Python mirror

**Files:** `config/vision-system/*.yaml`, `docs/defect-classification.md`, `docs/vision-system.md`, `README.md`, `src/README.md`, `tools/analysis/defect_types.py`, every `tools/**/*.py` with `src.*` imports

- [ ] **Step 1: YAMLs** — `defects_detected` per D2 (CCD1: crack, broken, misalignment, pin_missing, insufficient_epoxy, epoxy_overflow, epoxy_exposal, dyeing_contamination, non_electrical_contamination, electrical_contamination, no_code, code_blur; CCD2: pin_bent, pin_oxidized, pin_bur, pin_mis_cut, gold_exposal, epoxy_higher_than_ceramic; CCD3: crack, broken, misalignment, electrical_contamination, non_electrical_contamination, dyeing_contamination; CCD4: lighting_check, staining_on_edge, yellow_glass_cement, misalignment). `inspection_thresholds` keys = parameter names; `provisional: true` on epoxy_overflow, pin_bur, pin_mis_cut; CCD2 `pins_up: true`; CCD4 `thickness_classes`, `baseline_mode: ring_median`, `nest_ring_enabled: true`.
- [ ] **Step 2: Docs** — `docs/defect-classification.md` group table = D2 + provisional note + residue-severity decision; `docs/vision-system.md` "Defects Detected" per CCD + 4 cameras per station; README Defect Classification table (Function 9 / Cosmetic 4 / Assembly 6 + 2 inner-residue sub-checks) and the vision/PLC blurbs; `src/README.md` describes the real `src/vision` + `src/plc` layout and the test build.
- [ ] **Step 3: Python** — `defect_types.py`: `DefectType` = the 21 snake_case values in spec order, `DefectGroup`, `DEFECT_GROUP_MAP`, `DEFECT_SEVERITY_MAP`; keep the old member names as deprecated aliases (`F_CRACK = CRACK`, `A_LIGHT_LEAKAGE = LIGHTING_CHECK`, `C_STAINING = STAINING_ON_EDGE`, `A_EDGE_STAINING = STAINING_ON_EDGE`, `A_YELLOW_GLASS_CEMENT = YELLOW_GLASS_CEMENT`, etc. — Python `Enum` aliases are legal) so `defect_classification_tool.py`, `lighting_check_analysis.py`, `ng_statistics.py` keep working. Imports: `src.data_types.defect_types` → `tools.analysis.defect_types`; `src.global_variables.system_config` → `tools.analysis.system_config`; `src.material_handling.{PitchChanger,RobotController,TransferControl}` → `tools.simulation.{pitch_change_sim,robot_control_sim,transfer_control_sim}`; `src.ng_management.NGSorter` → `tools.analysis.ng_statistics`; `src.vision.DefectClassifier` → `tools.analysis.defect_classification_tool`; `src.vision.LightingCheckAnalyzer` → `tools.analysis.lighting_check_analysis`; `src.vision.CameraController` → `tools.calibration.camera_calibration`; `src.vision.OrientationDetector` → `tools.calibration.orientation_calibration` (verify each class with `grep -n "^class" -r tools`). Add empty `__init__.py` files under `tools/`, `tools/analysis`, `tools/simulation`, `tools/calibration`.
- [ ] **Step 4: Verify** — `python3 -c "import tools.simulation.inspection_sequence_sim as s; print(len(s.DefectType))"` prints 21 and `python3 -m compileall -q tools` is clean.
- [ ] **Step 5: Commit** — `docs/config: align defect groups, per-CCD routing and tool imports with spec V1.4`

---

### Task 11: Final verification and review

- [ ] **Step 1:** clean rebuild with `-DAOI_WITH_MVS=OFF -DAOI_BUILD_TESTS=ON`, `ctest` all green, zero warnings.
- [ ] **Step 2:** `python3 tools/plc/st_lint.py src/plc/*.st` OK; `python3 -m compileall -q tools`.
- [ ] **Step 3:** `git status` clean (build dir ignored).
- [ ] **Step 4:** whole-diff code review (`superpowers:requesting-code-review`), fix, commit.
