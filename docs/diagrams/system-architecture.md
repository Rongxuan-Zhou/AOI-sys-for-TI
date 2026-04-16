# CSE AOI System Architecture

<div style="width: 1200px; box-sizing: border-box; position: relative; background: #f0f4f8; padding: 20px; border-radius: 8px; border: 1px solid #c8d6e5;">
  <style scoped>
    .arch-wrapper { display: flex; gap: 12px; }.arch-sidebar { width: 165px; flex-shrink: 0; }.arch-main { flex: 1; min-width: 0; }.arch-title { text-align: center; font-size: 22px; font-weight: bold; color: #1a365d; margin-bottom: 4px; font-family: Georgia, serif; }.arch-subtitle { text-align: center; font-size: 12px; color: #64748b; margin-bottom: 16px; }
    .arch-layer { margin: 8px 0; padding: 14px; border-radius: 6px; box-shadow: 0 1px 4px rgba(30, 58, 138, 0.08); }.arch-layer-title { font-size: 13px; font-weight: bold; margin-bottom: 10px; text-align: center; }
    .arch-grid { display: grid; gap: 8px; }.arch-grid-2 { grid-template-columns: repeat(2, 1fr); }.arch-grid-3 { grid-template-columns: repeat(3, 1fr); }.arch-grid-4 { grid-template-columns: repeat(4, 1fr); }.arch-grid-5 { grid-template-columns: repeat(5, 1fr); }.arch-grid-6 { grid-template-columns: repeat(6, 1fr); }
    .arch-box { border-radius: 4px; padding: 8px; text-align: center; font-size: 11px; font-weight: 600; line-height: 1.35; color: #1e293b; background: #ffffff; border: 1px solid #cbd5e1; }.arch-box.highlight { background: linear-gradient(135deg, #dbeafe 0%, #bfdbfe 100%); border: 2px solid #2563eb; }.arch-box.tech { font-size: 10px; color: #475569; background: #f1f5f9; }
    .arch-layer.user { background: linear-gradient(135deg, #dbeafe 0%, #bfdbfe 100%); border: 2px solid #3b82f6; }.arch-layer.user .arch-layer-title { color: #1e40af; }
    .arch-layer.application { background: linear-gradient(135deg, #e0f2fe 0%, #bae6fd 100%); border: 2px solid #0284c7; }.arch-layer.application .arch-layer-title { color: #075985; }
    .arch-layer.ai { background: linear-gradient(135deg, #e0e7ff 0%, #c7d2fe 100%); border: 2px solid #6366f1; }.arch-layer.ai .arch-layer-title { color: #3730a3; }
    .arch-layer.data { background: linear-gradient(135deg, #ecfdf5 0%, #d1fae5 100%); border: 2px solid #10b981; }.arch-layer.data .arch-layer-title { color: #065f46; }
    .arch-layer.infra { background: linear-gradient(135deg, #f5f3ff 0%, #ede9fe 100%); border: 2px solid #7c3aed; }.arch-layer.infra .arch-layer-title { color: #5b21b6; }
    .arch-layer.external { background: linear-gradient(135deg, #fef3c7 0%, #fde68a 100%); border: 2px solid #d97706; }.arch-layer.external .arch-layer-title { color: #92400e; }
    .arch-sidebar-panel { border-radius: 6px; padding: 10px; background: linear-gradient(135deg, #edf1f7 0%, #dde3eb 100%); border: 2px solid #8da3bd; margin-bottom: 8px; box-shadow: 0 1px 3px rgba(30, 58, 138, 0.06); }.arch-sidebar-title { font-size: 12px; font-weight: bold; text-align: center; color: #1a365d; margin-bottom: 6px; }.arch-sidebar-item { font-size: 10px; text-align: center; color: #334155; background: #ffffff; padding: 5px; border-radius: 3px; margin: 3px 0; border: 1px solid #d1d9e4; }.arch-sidebar-item.metric { background: #dbeafe; border: 1px solid #93c5fd; color: #1e40af; font-weight: 600; }
    .arch-subgroup { display: flex; gap: 8px; margin-top: 8px; }.arch-subgroup-box { flex: 1; border-radius: 6px; padding: 8px; background: rgba(255, 255, 255, 0.5); border: 1px solid rgba(0, 0, 0, 0.08); }.arch-subgroup-title { font-size: 10px; font-weight: bold; color: #374151; text-align: center; margin-bottom: 6px; }
  </style>
  <div class="arch-title">CSE AOI Inspection System</div>
  <div class="arch-subtitle">4-CCD Multi-Angle Automated Optical Inspection for TI Semiconductor | 1800 x 1600 x 2000 mm</div>
  <div class="arch-wrapper">
    <div class="arch-sidebar">
      <div class="arch-sidebar-panel"><div class="arch-sidebar-title">Performance</div><div class="arch-sidebar-item metric">>85,000 units/day</div><div class="arch-sidebar-item metric"><1s cycle time</div><div class="arch-sidebar-item metric">100% detection</div><div class="arch-sidebar-item metric">19 defect types</div></div>
      <div class="arch-sidebar-panel"><div class="arch-sidebar-title">Vision Specs</div><div class="arch-sidebar-item">CCD#1/2/3: 0.0115 mm/px</div><div class="arch-sidebar-item">CCD#4: 0.0069 mm/px</div><div class="arch-sidebar-item">FOV: 28x24 mm</div><div class="arch-sidebar-item">FOV#4: 37.9x25.3 mm</div></div>
      <div class="arch-sidebar-panel"><div class="arch-sidebar-title">Defect Groups</div><div class="arch-sidebar-item">Function: 8 types</div><div class="arch-sidebar-item">Cosmetic: 4 types</div><div class="arch-sidebar-item">Assembly: 5 types</div><div class="arch-sidebar-item">Alignment: 2 types</div></div>
    </div>
    <div class="arch-main">
      <div class="arch-layer user">
        <div class="arch-layer-title">Material Handling Layer</div>
        <div class="arch-grid arch-grid-4"><div class="arch-box">Loading Basket<br><small>Manual basket stack</small></div><div class="arch-box">Single Basket Feeding<br><small>Cylinder + L-trigger + Lifter</small></div><div class="arch-box highlight">Epson SCARA Robot<br><small>Dual Nozzle, 4 CSE/cycle<br>Poka-Yoke orientation</small></div><div class="arch-box">Pitch Change<br><small>E-cylinder, Blue Holder<br>180-deg flip (1st case)</small></div></div>
      </div>
      <div class="arch-layer application">
        <div class="arch-layer-title">Transfer System Layer</div>
        <div class="arch-grid arch-grid-6"><div class="arch-box tech">Transfer #1<br><small>To lighting check</small></div><div class="arch-box tech">Transfer #2<br><small>Triggers CCD#3<br>during motion</small></div><div class="arch-box tech">Transfer #3<br><small>To top check</small></div><div class="arch-box tech">Transfer #4<br><small>Orientation comp.<br>servo rotation</small></div><div class="arch-box tech">Transfer #5<br><small>To unloading</small></div><div class="arch-box tech">Transfer #6<br><small>Tray handling</small></div></div>
      </div>
      <div class="arch-layer ai">
        <div class="arch-layer-title">Vision Inspection Layer</div>
        <div class="arch-subgroup">
          <div class="arch-subgroup-box">
            <div class="arch-subgroup-title">Surface Inspection (MV-GE501GC x3)</div>
            <div class="arch-grid arch-grid-3"><div class="arch-box">CCD#1 Top Check<br><small>WWK03-110-230<br>DN-COS60-W coaxial<br>0.0115 mm/px</small></div><div class="arch-box">CCD#2 Side Check<br><small>WWK03-110-230<br>DN-2BS32738-W bar<br>360-deg rotation</small></div><div class="arch-box">CCD#3 Bottom Check<br><small>WWK03-110-230<br>DN-COS60-W coaxial<br>dark enclosure</small></div></div>
          </div>
          <div class="arch-subgroup-box">
            <div class="arch-subgroup-title">Functional Test (MV-GE2000C)</div>
            <div class="arch-grid arch-grid-1" style="grid-template-columns: 1fr;"><div class="arch-box highlight">CCD#4 Lighting Check<br><small>DTCM110-48 telecentric<br>DN-HSP25-W hyper light<br>Closed chamber<br>Sapphire glass + Glass cover<br>0.0069 mm/px</small></div></div>
          </div>
        </div>
      </div>
      <div class="arch-layer external">
        <div class="arch-layer-title">NG Management Layer</div>
        <div class="arch-grid arch-grid-4"><div class="arch-box highlight">NG Check CCD<br><small>Reconfirmation<br>before sorting</small></div><div class="arch-box">NG Conveyor<br><small>Belt + holder bar<br>position stop</small></div><div class="arch-box">NG Tray<br><small>Sorted by<br>defect category</small></div><div class="arch-box">False Reject Recovery<br><small>Route back to<br>OK unloading</small></div></div>
      </div>
      <div class="arch-layer data">
        <div class="arch-layer-title">Output Layer</div>
        <div class="arch-grid arch-grid-3"><div class="arch-box">Unloading Tray<br><small>OK products placed<br>by Transfer #5</small></div><div class="arch-box">Full Tray Stack<br><small>Gripper lift + new tray<br>camera-monitored</small></div><div class="arch-box">Manual Unloading<br><small>Operator collects<br>stacked full trays</small></div></div>
      </div>
      <div class="arch-layer infra">
        <div class="arch-layer-title">Safety and HMI Layer</div>
        <div class="arch-grid arch-grid-4"><div class="arch-box">Optical Grating<br><small>Loading/unloading<br>area protection</small></div><div class="arch-box">Tri-Color Indicator<br><small>Red: fault<br>Yellow: warning<br>Green: running</small></div><div class="arch-box">E-Stop<br><small>3 locations<br>hardwired relay</small></div><div class="arch-box">HMI Touchscreen<br><small>Recipe management<br>diagnostics</small></div></div>
      </div>
    </div>
    <div class="arch-sidebar">
      <div class="arch-sidebar-panel"><div class="arch-sidebar-title">Camera Models</div><div class="arch-sidebar-item">Hikrobot MV-GE501GC</div><div class="arch-sidebar-item">Hikrobot MV-GE2000C</div><div class="arch-sidebar-item">NG Check CCD</div><div class="arch-sidebar-item">Orientation CCD</div></div>
      <div class="arch-sidebar-panel"><div class="arch-sidebar-title">Optics</div><div class="arch-sidebar-item">WWK03-110-230 x3</div><div class="arch-sidebar-item">DTCM110-48 telecentric</div><div class="arch-sidebar-item">DN-COS60-W coaxial</div><div class="arch-sidebar-item">DN-2BS32738-W bar</div><div class="arch-sidebar-item">DN-HSP25-W hyper</div></div>
      <div class="arch-sidebar-panel"><div class="arch-sidebar-title">Standards</div><div class="arch-sidebar-item">ISO 13849 PLd</div><div class="arch-sidebar-item">IEC 61496</div><div class="arch-sidebar-item">ISO 13850 E-Stop</div><div class="arch-sidebar-item">ISO 12100</div></div>
    </div>
  </div>
</div>
