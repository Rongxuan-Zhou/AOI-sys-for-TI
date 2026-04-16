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

*[#1e3a5f] <b>19 Defect Categories</b>\n<i>100% Detection Rate\n4-CCD Multi-Angle AOI</i>

**[#dbeafe]:Function (8)
<i>Functional integrity defects</i>;
***[#dbeafe] Lighting Check <<functional>>\n<size:9>CCD#4 | Light leakage\nClosed chamber test</size>
***[#fecdd3] Crack <<critical>>\n<size:9>CCD#1 / CCD#3 | CRITICAL\nEdge + surface fracture</size>
***[#fecdd3] Broken <<critical>>\n<size:9>CCD#1 / CCD#3 | CRITICAL\nStructural failure</size>
***[#dbeafe] Epoxy Exposal <<functional>>\n<size:9>CCD#1 | Epoxy visible\non ceramic surface</size>
***[#dbeafe] Pin Missing <<functional>>\n<size:9>CCD#1 | Absent pin\ngeometry check</size>
***[#dbeafe] Electrical Contamination <<functional>>\n<size:9>CCD#1 | Conductive\nforeign matter</size>
***[#dbeafe] Gold Exposal <<functional>>\n<size:9>CCD#2 | Gold layer\nexposed on side</size>
***[#dbeafe] Insufficient Epoxy <<functional>>\n<size:9>CCD#1 | Below minimum\ncoverage threshold</size>

left side

**[#e0e7ff]:Cosmetic (4)
<i>Visual appearance defects</i>;
***[#e0e7ff] Dyeing Contamination <<cosmetic>>\n<size:9>CCD#1 | Purple/pink\nstaining on surface</size>
***[#e0e7ff] Non-Electrical Contamination <<cosmetic>>\n<size:9>CCD#1 | Non-conductive\nforeign particles</size>
***[#e0e7ff] No Code <<cosmetic>>\n<size:9>CCD#1 | Missing laser\nmarking entirely</size>
***[#e0e7ff] Code Blur <<cosmetic>>\n<size:9>CCD#1 | Illegible or\nsmeared marking</size>

**[#fce7f3]:Assembly (5)
<i>Mechanical assembly defects</i>;
***[#fecdd3] Pin Bent <<critical>>\n<size:9>CCD#2 | CRITICAL\nBent pin geometry</size>
***[#fce7f3] Pin Oxidized <<assembly>>\n<size:9>CCD#2 | Surface\noxidation on pins</size>
***[#fef3c7] Pin Bur <<sample_needed>>\n<size:9>CCD#2 | CRITICAL\nNeeds real sample</size>
***[#fef3c7] Pin Mis-cut <<sample_needed>>\n<size:9>CCD#2\nNeeds real sample</size>
***[#fce7f3] Epoxy Higher Than Ceramic <<assembly>>\n<size:9>CCD#1 | Height\nexceedance check</size>

**[#ecfdf5]:Alignment (2)
<i>Positional accuracy defects</i>;
***[#fecdd3] Misalignment <<critical>>\n<size:9>CCD#4 | CRITICAL\nComponent offset</size>
***[#ecfdf5] Staining on Edge <<alignment>>\n<size:9>CCD#4 | Inner residue\nYellow glass cement</size>

@endmindmap
```

## Legend

| Marker | Meaning |
|:-------|:--------|
| Red background | CRITICAL defect -- zero tolerance, immediate reject |
| Yellow background | Requires real production samples for validation |
| Blue background | Function group defect |
| Purple background | Cosmetic group defect |
| Pink background | Assembly group defect |
| Green background | Alignment group defect |

## CCD Assignment Summary

| Camera | Defects Detected | Count |
|:-------|:----------------|------:|
| CCD#1 (Top) | Crack, Broken, Epoxy Exposal, Pin Missing, Electrical Contamination, Insufficient Epoxy, Dyeing Contamination, Non-Electrical Contamination, No Code, Code Blur, Epoxy Higher Than Ceramic | 11 |
| CCD#2 (Side) | Gold Exposal, Pin Bent, Pin Oxidized, Pin Bur, Pin Mis-cut | 5 |
| CCD#3 (Bottom) | Crack, Broken | 2 |
| CCD#4 (Lighting) | Lighting Check, Misalignment, Staining on Edge | 3 |
