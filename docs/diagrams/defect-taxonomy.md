# CSE Defect Classification Taxonomy

```plantuml
@startmindmap
<style>
mindmapDiagram {
  node {
    FontSize 12
    RoundCorner 8
    Padding 8
    Margin 4
  }
  arrow {
    LineThickness 1.5
    LineColor #64748b
  }
  :depth(0) {
    FontSize 16
    FontStyle bold
    BackgroundColor #1e3a5f
    FontColor #ffffff
    RoundCorner 12
    Padding 12
  }
  :depth(1) {
    FontSize 13
    FontStyle bold
    Padding 10
  }
  :depth(2) {
    FontSize 11
    Padding 6
  }
  .critical {
    BackgroundColor #fecdd3
    LineColor #e11d48
    LineThickness 2.5
    FontStyle bold
  }
  .sample_needed {
    BackgroundColor #fef3c7
    LineColor #d97706
    LineThickness 2
    FontStyle italic
  }
  .functional {
    BackgroundColor #dbeafe
    LineColor #2563eb
  }
  .cosmetic {
    BackgroundColor #e0e7ff
    LineColor #6366f1
  }
  .assembly {
    BackgroundColor #fce7f3
    LineColor #db2777
  }
  .alignment {
    BackgroundColor #ecfdf5
    LineColor #059669
  }
}
</style>

*[#1e3a5f] <b>19 Defect Categories</b>\n<i>Spec V1.4 slide 8 | 100% on samples\n4 stations x 4 cameras</i>

**[#dbeafe]:Function (9)
<i>Functional integrity defects</i>;
***[#fecdd3] Lighting Check <<critical>>\n<size:9>CCD#4 | CRITICAL\nTransmitted-light leakage</size>
***[#fecdd3] Crack <<critical>>\n<size:9>CCD#1 / CCD#3 | CRITICAL\nThin dark structure</size>
***[#fecdd3] Broken <<critical>>\n<size:9>CCD#1 / CCD#3 | CRITICAL\nRim chip, notch exempt</size>
***[#dbeafe] Epoxy Exposal <<functional>>\n<size:9>CCD#1 | Granular grey\npatch next to a pin</size>
***[#dbeafe] Pin Missing <<functional>>\n<size:9>CCD#1 | Hole without\nmetallic core</size>
***[#dbeafe] Electrical Contamination <<functional>>\n<size:9>CCD#1 / CCD#3 | Dark\nmetallic textured streak</size>
***[#dbeafe] Gold Exposal <<functional>>\n<size:9>CCD#2 | Gold patch\non the side wall</size>
***[#dbeafe] Insufficient Epoxy <<functional>>\n<size:9>CCD#1 | Epoxy ring\naround a pin under-filled</size>
***[#fef3c7] Epoxy Overflow <<sample_needed>>\n<size:9>CCD#1 | Epoxy blob\ntoo large (provisional)</size>

left side

**[#e0e7ff]:Cosmetic (4)
<i>Visual appearance defects</i>;
***[#e0e7ff] Dyeing Contamination <<cosmetic>>\n<size:9>CCD#1 / CCD#3 | Saturated\ncolour stain</size>
***[#e0e7ff] Non-Electrical Contamination <<cosmetic>>\n<size:9>CCD#1 / CCD#3 | Light\nsmudge</size>
***[#e0e7ff] No Code <<cosmetic>>\n<size:9>CCD#1 | Missing laser\nmarking entirely</size>
***[#e0e7ff] Code Blur <<cosmetic>>\n<size:9>CCD#1 | Illegible or\nsmeared marking</size>

**[#fce7f3]:Assembly (6)
<i>Mechanical assembly defects</i>;
***[#fce7f3] Pin Bent <<assembly>>\n<size:9>CCD#2 | Angle from\nvertical beyond tolerance</size>
***[#fce7f3] Pin Oxidized <<assembly>>\n<size:9>CCD#2 | Pin dull/dark\ninstead of gold</size>
***[#fef3c7] Pin Bur <<sample_needed>>\n<size:9>CCD#2 | Tip wider than\nthe pin (provisional)</size>
***[#fef3c7] Pin Mis-cut <<sample_needed>>\n<size:9>CCD#2 | Length out of\ntolerance (provisional)</size>
***[#fce7f3] Epoxy Higher Than Ceramic <<assembly>>\n<size:9>CCD#2 | Bump in the\ntop-edge profile</size>
***[#fecdd3] Misalignment <<critical>>\n<size:9>CCD#1 / CCD#3 / CCD#4 | CRITICAL\nCentre vs fixture / nest ring</size>

**[#ecfdf5]:Inner Staining Residue (2)
<i>Sub-checks of the lighting check</i>;
***[#ecfdf5] Staining on Edge <<alignment>>\n<size:9>CCD#4 | Dark compact patch\nin the outer band</size>
***[#ecfdf5] Yellow Glass Cement <<alignment>>\n<size:9>CCD#4 | Yellow blob brighter /\nmore saturated than its ring</size>

@endmindmap
```

## Legend

| Marker | Meaning |
|:-------|:--------|
| Red background | CRITICAL defect -- zero tolerance, immediate reject |
| Yellow background | No real sample in the V1.4 testing report -- thresholds marked `provisional` |
| Blue background | Function group defect |
| Purple background | Cosmetic group defect |
| Pink background | Assembly group defect |
| Green background | Inner staining residue (lighting-check sub-checks) |

## CCD Assignment Summary

| Camera | Defects Detected | Count |
|:-------|:----------------|------:|
| CCD#1 (Top) | Crack, Broken, Misalignment, Pin Missing, Insufficient Epoxy, Epoxy Overflow, Epoxy Exposal, Dyeing / Electrical / Non-Electrical Contamination, No Code, Code Blur | 12 |
| CCD#2 (Side) | Pin Bent, Pin Oxidized, Pin Bur, Pin Mis-cut, Gold Exposal, Epoxy Higher Than Ceramic | 6 |
| CCD#3 (Bottom) | Crack, Broken, Misalignment, Dyeing / Electrical / Non-Electrical Contamination | 6 |
| CCD#4 (Inner check) | Lighting Check, Staining on Edge, Yellow Glass Cement, Misalignment (nest ring) | 4 |
