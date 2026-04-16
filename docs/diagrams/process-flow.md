# CSE AOI 18-Step Process Flow

<div style="width: 1200px; box-sizing: border-box; position: relative; background: #f0f4f8; padding: 20px; border-radius: 8px; border: 1px solid #c8d6e5;">
  <style scoped>
    .arch-title { text-align: center; font-size: 20px; font-weight: bold; color: #1a365d; margin-bottom: 4px; font-family: Georgia, serif; }.arch-subtitle { text-align: center; font-size: 12px; color: #64748b; margin-bottom: 16px; }
    .arch-pipeline { display: flex; gap: 0; align-items: stretch; }
    .arch-stage { flex: 1; padding: 12px 8px; border-radius: 6px; display: flex; flex-direction: column; box-shadow: 0 1px 4px rgba(30, 58, 138, 0.08); }
    .arch-stage-title { font-size: 11px; font-weight: 700; text-align: center; text-transform: uppercase; letter-spacing: 0.5px; margin-bottom: 8px; }
    .arch-arrow { display: flex; align-items: center; justify-content: center; width: 28px; flex-shrink: 0; font-size: 18px; color: #64748b; font-weight: bold; }
    .arch-box { border-radius: 4px; padding: 7px 5px; text-align: center; font-size: 10px; font-weight: 600; line-height: 1.3; color: #1e293b; background: #ffffff; border: 1px solid #cbd5e1; margin: 3px 0; }.arch-box.highlight { background: linear-gradient(135deg, #dbeafe 0%, #bfdbfe 100%); border: 2px solid #2563eb; }.arch-box.ng { background: linear-gradient(135deg, #fef3c7 0%, #fde68a 100%); border: 2px solid #d97706; color: #92400e; }.arch-box.critical { background: linear-gradient(135deg, #fce7f3 0%, #fbcfe8 100%); border: 2px solid #db2777; color: #831843; }
    .stage-loading { background: linear-gradient(135deg, #dbeafe 0%, #bfdbfe 100%); border: 2px solid #3b82f6; }.stage-loading .arch-stage-title { color: #1e40af; }
    .stage-inspect { background: linear-gradient(135deg, #e0e7ff 0%, #c7d2fe 100%); border: 2px solid #6366f1; }.stage-inspect .arch-stage-title { color: #3730a3; }
    .stage-output { background: linear-gradient(135deg, #ecfdf5 0%, #d1fae5 100%); border: 2px solid #10b981; }.stage-output .arch-stage-title { color: #065f46; }
    .stage-ng { background: linear-gradient(135deg, #fef3c7 0%, #fde68a 100%); border: 2px solid #d97706; }.stage-ng .arch-stage-title { color: #92400e; }
    .arch-step-num { display: inline-block; width: 16px; height: 16px; border-radius: 50%; background: #3b82f6; color: white; font-size: 9px; font-weight: 700; line-height: 16px; text-align: center; margin-right: 2px; vertical-align: middle; }
    .arch-step-num.inspect { background: #6366f1; }
    .arch-step-num.output { background: #10b981; }
    .arch-step-num.ng { background: #d97706; }
  </style>
  <div class="arch-title">18-Step Inspection Process Flow</div>
  <div class="arch-subtitle">Target: >85,000 units/day | Cycle time <1 second/unit | 4 CSE processed per cycle</div>
  <div class="arch-pipeline">
    <div class="arch-stage stage-loading">
      <div class="arch-stage-title">Loading</div>
      <div class="arch-box"><span class="arch-step-num">1</span> Manual Loading<br><small>Basket stack</small></div>
      <div class="arch-box"><span class="arch-step-num">2</span> Basket Feeding<br><small>Cylinder + L-trigger</small></div>
      <div class="arch-box highlight"><span class="arch-step-num">3</span> CSE Loading<br><small>SCARA dual nozzle<br>Poka-Yoke check<br>90-deg rotate</small></div>
      <div class="arch-box"><span class="arch-step-num">4</span> Pitch Change<br><small>E-cylinder expand<br>Blue holder position<br>180-deg flip (1st)</small></div>
    </div>
    <div class="arch-arrow">&#10132;</div>
    <div class="arch-stage stage-inspect">
      <div class="arch-stage-title">4-CCD Inspection</div>
      <div class="arch-box"><span class="arch-step-num inspect">5</span> Transfer #1<br><small>To lighting station</small></div>
      <div class="arch-box critical"><span class="arch-step-num inspect">6</span> CCD#4 Lighting<br><small>Shade close<br>Sapphire glass chamber<br>Light leakage test</small></div>
      <div class="arch-box"><span class="arch-step-num inspect">7</span> CCD#3 Bottom<br><small>During Transfer #2<br>Coaxial light</small></div>
      <div class="arch-box"><span class="arch-step-num inspect">8</span> CCD#1 Top<br><small>Surface + marking<br>Coaxial light</small></div>
    </div>
    <div class="arch-arrow">&#10132;</div>
    <div class="arch-stage stage-inspect">
      <div class="arch-stage-title">Side Inspection</div>
      <div class="arch-box"><span class="arch-step-num inspect">9</span> Orientation Comp.<br><small>Servo rotation<br>Uniform direction</small></div>
      <div class="arch-box"><span class="arch-step-num inspect">10</span> Positioning<br><small>Re-align for<br>side check</small></div>
      <div class="arch-box highlight"><span class="arch-step-num inspect">11</span> CCD#2 Side<br><small>Gripper lift<br>360-deg motor rotation<br>Pin + gold check</small></div>
    </div>
    <div class="arch-arrow">&#10132;</div>
    <div class="arch-stage stage-output">
      <div class="arch-stage-title">OK Output</div>
      <div class="arch-box"><span class="arch-step-num output">12</span> Transfer #5<br><small>To unloading area</small></div>
      <div class="arch-box"><span class="arch-step-num output">13</span> Unload to Tray<br><small>Place OK CSE</small></div>
      <div class="arch-box"><span class="arch-step-num output">14</span> Full Tray Stack<br><small>Gripper lift + new</small></div>
      <div class="arch-box"><span class="arch-step-num output">15</span> Manual Unload<br><small>Operator collects</small></div>
      <div class="arch-box"><span class="arch-step-num output">16</span> Tray Feeding<br><small>Empty tray supply</small></div>
    </div>
    <div class="arch-arrow">&#10132;</div>
    <div class="arch-stage stage-ng">
      <div class="arch-stage-title">NG Path</div>
      <div class="arch-box ng"><span class="arch-step-num ng">&#10008;</span> NG Detected<br><small>Any CCD flags defect</small></div>
      <div class="arch-box critical"><span class="arch-step-num ng">R</span> NG Check CCD<br><small>Reconfirmation<br>Double-check</small></div>
      <div class="arch-box ng"><span class="arch-step-num ng">17</span> NG Conveyor<br><small>Belt + holder bar</small></div>
      <div class="arch-box ng"><span class="arch-step-num ng">18</span> NG Unloading<br><small>To NG tray</small></div>
      <div class="arch-box" style="background: #ecfdf5; border-color: #10b981;"><span class="arch-step-num output">&#10003;</span> False Reject<br><small>Route back to OK</small></div>
    </div>
  </div>
</div>
