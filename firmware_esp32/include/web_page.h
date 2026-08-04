#ifndef WEB_PAGE_H
#define WEB_PAGE_H

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Ultrasonic Generator. Magnetostrictive transducer Panel. </title>
    <style>
        /* Fully Autonomous Styles: Industrial Dashboard */
        * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
        body { background-color: #0f172a; color: #f8fafc; padding: 15px; display: flex; flex-direction: column; align-items: center; }
        .container { width: 100%; max-width: 650px; display: flex; flex-direction: column; gap: 15px; }
        
        /* Grid for information cards */
        .grid-2 { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
        
        /* cards */
        .card { background-color: #1e293b; border-radius: 12px; padding: 15px; border: 1px solid #334155; transition: all 0.2s ease; }
        .card.changed { border-color: #eab308; box-shadow: 0 0 10px rgba(234, 179, 8, 0.2); }
        .card.alarm { border-color: #ef4444; background-color: #451a1a; box-shadow: 0 0 15px rgba(239, 68, 68, 0.4); }
        
        h3 { font-size: 13px; color: #94a3b8; text-transform: uppercase; margin-bottom: 8px; letter-spacing: 0.5px; }
        .val { font-size: 24px; font-weight: bold; color: #38bdf8; }
        .warn { color: #eab308 !important; }
        
        /* Slider */
        input[type=range] { width: 100%; margin-top: 10px; accent-color: #38bdf8; cursor: pointer; height: 8px; border-radius: 4px; }
        
        /* Forms and settings */
        .config-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-top: 8px; }
        .input-group { display: flex; flex-direction: column; gap: 4px; }
        .input-group label { font-size: 11px; color: #64748b; text-transform: uppercase; }
        input[type=number] { background-color: #0f172a; border: 1px solid #334155; color: #f8fafc; padding: 8px; border-radius: 6px; font-size: 14px; font-weight: bold; width: 100%; }
        input[type=number]:focus { border-color: #38bdf8; outline: none; }
        
        /* Buttons */
        .btn-group { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; margin-top: 5px; }
        button { padding: 14px; border: none; border-radius: 8px; font-weight: bold; cursor: pointer; font-size: 14px; transition: 0.2s; color: white; text-transform: uppercase; letter-spacing: 0.5px; }
        .btn-start { background-color: #10b981; } .btn-start:hover { background-color: #059669; }
        .btn-stop { background-color: #ef4444; } .btn-stop:hover { background-color: #dc2626; }
        .btn-save { background-color: #6366f1; grid-column: span 2; margin-top: 5px; padding: 10px; font-size: 12px; }
        .btn-save:hover { background-color: #4f46e5; }
        
        /* Standalone SVG graphic */
        .chart-container { width: 100%; height: 200px; margin-top: 10px; background: #0f172a; border-radius: 8px; position: relative; border: 1px solid #334155; overflow: hidden; }
        svg { width: 100%; height: 100%; }
        polyline { fill: none; stroke: #10b981; stroke-width: 2.5; stroke-linecap: round; stroke-linejoin: round; }
        .chart-grid { stroke: #1e293b; stroke-width: 1; }
        .axis-label { font-size: 9px; fill: #475569; }
    </style>
</head>
<body>
    <div class="container">
        
        <!-- Top row: Status and Timer -->
        <div class="grid-2">
            <div class="card" id="statusCard">
                <h3>System status</h3>
                <div class="val" id="valStatus">CONNECTION..</div>
            </div>
            <div class="card" id="timerCard">
                <h3>Work timer</h3>
                <div class="val" id="valTimer">00:00 / 00:00</div>
            </div>
        </div>

        <!-- Middle row: Power and Temperature -->
        <div class="grid-2">
            <!-- Power control in the "Sollwert / Istwert style" -->
            <div class="card" id="pwrCard">
                <div style="display: flex; justify-content: space-between; align-items: center;">
                    <h3 id="valTargetPwr">Sollwert (Set): 0 %</h3>
                    <span id="valActualPwr" style="font-size: 14px; font-weight: bold; color: #10b981; background: #0f172a; padding: 2px 8px; border-radius: 4px; border: 1px solid #334155;">Istwert: 0 %</span>
                </div>
                <input type="range" id="pwrSlider" min="0" max="100" value="0" oninput="previewPower(this.value)" onchange="applyPower(this.value)">
            </div>
            <div class="card" id="tempCard">
                <h3>Temperature of magnetostrictive transducer</h3>
                <div class="val" id="valTemp">0.0 °C</div>
            </div>
        </div>

        <!-- Main control buttons -->
        <div class="btn-group">
            <button class="btn-start" onclick="startSweep()">Scan</button>
            <button class="btn-stop" onclick="stopGenerator()">Stop generation</button>
        </div>

        <!-- Generator settings block (Configuration) -->
        <div class="card">
            <h3>Scanning parameters and limits</h3>
            <div class="config-grid">
                <div class="input-group">
                    <label>Nominal (Hz)</label>
                    <input type="number" id="cfgNom" value="35000">
                </div>
                <div class="input-group">
                    <label>Scan start (Hz)</label>
                    <input type="number" id="cfgStart" value="50000">
                </div>
                <div class="input-group">
                    <label>Stop scan (Hz)</label>
                    <input type="number" id="cfgEnd" value="25000">
                </div>
                <div class="input-group">
                    <label>Frequency step (Hz)</label>
                    <input type="number" id="cfgStep" value="100">
                </div>
                <div class="input-group">
                    <label>Time limit (sec)</label>
                    <input type="number" id="cfgTime" value="60">
                </div>
                <div class="input-group">
                    <label>Target temp. (°C)</label>
                    <input type="number" id="cfgTargetTemp" step="0.1" value="25.0">
                </div>
                <button class="btn-save" onclick="saveConfig()">Save and upload configuration</button>
            </div>
        </div>

        <!-- Cavitation resonance graph -->
        <div class="card">
            <h3>Cavitation noise spectrum (RMS hydrophone)</h3>
            <div class="chart-container">
                <svg id="svgChart" viewBox="0 0 500 150" preserveAspectRatio="none">
                    <!-- Horizontal level grid -->
                    <line x1="0" y1="37.5" x2="500" y2="37.5" class="chart-grid" />
                    <line x1="0" y1="75" x2="500" y2="75" class="chart-grid" />
                    <line x1="0" y1="112.5" x2="500" y2="112.5" class="chart-grid" />
                    
                    <!-- Vertical grid -->
                    <line x1="125" y1="0" x2="125" y2="150" class="chart-grid" />
                    <line x1="250" y1="0" x2="250" y2="150" class="chart-grid" />
                    <line x1="375" y1="0" x2="375" y2="150" class="chart-grid" />
                    
                    <!-- Text labels of levels (for clarity) -->
                    <text x="5" y="32" class="axis-label">75%</text>
                    <text x="5" y="70" class="axis-label">50%</text>
                    <text x="5" y="107" class="axis-label">25%</text>

                    <!-- Dynamic trend line -->
                    <polyline id="chartLine" points=""></polyline>
                </svg>
            </div>
        </div>

    </div>

    <!-- DSP DEBUG MATRIX TABLE TABLE -->
    <div class="card" style="margin-top: 15px; background: #151515; border: 1px solid #ff9800; padding: 12px; border-radius: 6px;">
        <h3 style="margin-top:0; color:#ff9800; font-size: 14px; font-family: sans-serif;">DSP Debug Matrix (Current + Last 10 History)</h3>
        <div style="overflow-x: auto;">
            <table id="debug-table" style="width:100%; border-collapse: collapse; text-align: left; font-family: monospace; font-size: 12px;">
                <thead>
                    <tr style="border-bottom: 2px solid #333; color: #888;">
                        <th style="padding: 6px; width: 140px; text-align: left;">Parameter</th>
                        <th style="padding: 6px; color: #00ff00; width: 65px;">Current</th>
                        <th style="padding: 6px; opacity: 0.9; width: 55px;">-1</th>
                        <th style="padding: 6px; opacity: 0.8; width: 55px;">-2</th>
                        <th style="padding: 6px; opacity: 0.7; width: 55px;">-3</th>
                        <th style="padding: 6px; opacity: 0.6; width: 55px;">-4</th>
                        <th style="padding: 6px; opacity: 0.5; width: 55px;">-5</th>
                        <th style="padding: 6px; opacity: 0.4; width: 55px;">-6</th>
                        <th style="padding: 6px; opacity: 0.3; width: 55px;">-7</th>
                        <th style="padding: 6px; opacity: 0.2; width: 55px;">-8</th>
                        <th style="padding: 6px; opacity: 0.1; width: 55px;">-9</th>
                    </tr>
                </thead>
                <tbody>
                    <tr id="row-freq" style="border-bottom: 1px solid #222; color: #eee;">
                        <td style="padding: 6px; font-weight: bold; color: #fff;">Freq (Hz)</td>
                        <td colspan="10" style="padding: 6px; color: #555; font-style: italic;">Waiting for sweep data...</td>
                    </tr>
                    <tr id="row-rms" style="border-bottom: 1px solid #222; color: #eee;">
                        <td style="padding: 6px; font-weight: bold; color: #fff;">Cav RMS</td>
                        <td colspan="10" style="padding: 6px; color: #555; font-style: italic;">Waiting for sweep data...</td>
                    </tr>
                </tbody>
            </table>
        </div>
    </div>

    <script>
        var websocket;
        var lastPreviewTime = 0;
        var scanData = []; 
        const MAX_POINTS = 120; // Limitation of 120 points, as was in Chart.js
        
        // Initialize global historical sliding arrays at the top of your script block
        var freqHistory = Array(10).fill(0);
        var rmsHistory = Array(10).fill(0.0000);
        var lastTableUpdateTime = 0;

        function initWebSocket() {
            websocket = new WebSocket('ws://' + window.location.hostname + '/ws');
            
            websocket.onopen = function() { 
                document.getElementById('valStatus').innerHTML = "CONNECTION OK"; 
            };
            
            websocket.onclose = function() { 
                document.getElementById('valStatus').innerHTML = "DISABLED";
                setTimeout(initWebSocket, 2000); 
            };
            
                websocket.onmessage = function(event) {
                var data = JSON.parse(event.data);
                
                // Updating basic parameters
                document.getElementById('valStatus').innerHTML = data.status;
                document.getElementById('valTemp').innerHTML = Number(data.temp).toFixed(1) + " °C";
                
                if(data.timer) {
                    document.getElementById('valTimer').innerHTML = data.timer;
                }
                
                // Synchronize the slider with a physical encoder (only works when the mouse is released)
                var slider = document.getElementById('pwrSlider');
                var targetLabel = document.getElementById('valTargetPwr');
                
                if (slider && !slider.classList.contains('changed')) {
                    slider.value = data.target_pwr;
                    targetLabel.innerHTML = "Power setting: " + data.target_pwr + " %";
                }
                
                // Changing the color of cards in case of an accident
                var statusCard = document.getElementById('statusCard');
                if (statusCard) {
                    if (data.status.includes("ALARM") || data.status === "CRITICAL_FREEZE" || data.status.includes("STOP")) {
                        statusCard.classList.add('alarm');
                    } else { statusCard.classList.remove('alarm'); }
                }

                var tempCard = document.getElementById('tempCard');
                if (tempCard) {
                    if (Number(data.temp) < 3.0 || Number(data.temp) > 75.0) { tempCard.classList.add('alarm'); }
                    else { tempCard.classList.remove('alarm'); }
                }

                // Collect points while scanning the STM frequency
                // Check scan_freq or freq depending on what the STM sends
                var currentFreq = Number(data.scan_freq) > 0 ? Number(data.scan_freq) : Number(data.freq);
                var currentRms = Number(data.scan_rms) > 0 ? Number(data.scan_rms) : Number(data.rms);
                
                if (data.status === "SCANNING" && currentFreq > 0) {
                    scanData.push({ f: currentFreq, rms: currentRms });
                    if (scanData.length > MAX_POINTS) { 
                        scanData.shift(); 
                    }
                    renderSvgChart();
                }

                // --- UPDATED STATUS AND POWER LOGIC ---
                var slider = document.getElementById('pwrSlider');
                var targetLabel = document.getElementById('valTargetPwr');
                var actualLabel = document.getElementById('valActualPwr');
                var pwrCard = document.getElementById('pwrCard');
                var statusValue = document.getElementById('valStatus');

                // 1. We display the status and the current generation frequency in the main card
                if (statusValue) {
                    var freqKhz = (Number(data.freq) / 1000).toFixed(2);
                    statusValue.innerHTML = data.status + `<div style="font-size: 16px; color: #94a3b8; font-weight: normal; margin-top: 4px;">F_output: ${freqKhz} kHz</div>`;
                }

                // 2. We derive the actual current power from the power section (Istwert)
                if (actualLabel) {
                    actualLabel.innerHTML = "Istwert: " + data.pwr + " %";
                }

                // 3. If the user is NOT DRAGGING the slider with the mouse right now
                if (slider && !slider.classList.contains('changed')) {
                    slider.value = data.target_pwr;
                    targetLabel.innerHTML = "Sollwert: " + data.target_pwr + " %";
                    targetLabel.classList.remove('warn');
                    
                    // If the actual power has reached the specified value, remove the yellow waiting frame.
                    if (Number(data.pwr) === Number(data.target_pwr)) {
                        pwrCard.classList.remove('changed');
                    } else {
                        // Power is still in the process of changing (soft start/braking)
                        pwrCard.classList.add('changed');
                    }
                }

                // ==================================================================
                // --- NEW CODE: DYNAMIC DSP DEBUG MATRIX TRACKING (5 Hz) ---
                // ==================================================================
                var now = Date.now();
                if (now - lastTableUpdateTime >= 200) { // Throttled to 5 times per second (200ms)
                    lastTableUpdateTime = now;
                    
                    // Push current values to the front of our sliding history rows
                    freqHistory.unshift(currentFreq);
                    rmsHistory.unshift(currentRms);
                    
                    // Cap row array lengths to maximum history depths (Discard items older than index 10)
                    if (freqHistory.length > 10) freqHistory.pop();
                    if (rmsHistory.length > 10) rmsHistory.pop();
                    
                    // Re-render Frequency Row Values dynamically
                    var freqRowHtml = '<td style="padding: 6px; font-weight: bold; color: #fff;">Freq (Hz)</td>';
                    freqHistory.forEach(function(val, idx) {
                        var color = idx === 0 ? '#00ff00' : `rgba(255,255,255,${(1.0 - idx * 0.09).toFixed(2)})`;
                        freqRowHtml += `<td style="padding: 6px; color: ${color};">${val}</td>`;
                    });
                    var rowFreqEl = document.getElementById('row-freq');
                    if (rowFreqEl) rowFreqEl.innerHTML = freqRowHtml;
                    
                    // Re-render Cavitation Noise RMS Row Values dynamically
                    var rmsRowHtml = '<td style="padding: 6px; font-weight: bold; color: #fff;">Cav RMS</td>';
                    rmsHistory.forEach(function(val, idx) {
                        var color = idx === 0 ? '#00ff00' : `rgba(0,255,0,${(1.0 - idx * 0.09).toFixed(2)})`;
                        rmsRowHtml += `<td style="padding: 6px; color: ${color};">${Number(val).toFixed(4)}</td>`;
                    });
                    var rowRmsEl = document.getElementById('row-rms');
                    if (rowRmsEl) rowRmsEl.innerHTML = rmsRowHtml;
                }
            };
        }

        // Offline vector rendering
        function renderSvgChart() {
            if (scanData.length === 0) return;
            
            var maxRms = Math.max(...scanData.map(d => d.rms));
            if (maxRms === 0) maxRms = 1;
            
            var pointsStr = "";
            for (var i = 0; i < scanData.length; i++) {
                var x = (i / (scanData.length > 1 ? scanData.length - 1 : 1)) * 500;
                var y = 150 - ((scanData[i].rms / maxRms) * 125); // leave a gap at the top
                pointsStr += x.toFixed(1) + "," + y.toFixed(1) + " ";
            }
            document.getElementById('chartLine').setAttribute("points", pointsStr);
        }

        // While dragging (The word "Waiting" appears ONLY HERE)
        function previewPower(val) {
            document.getElementById('valTargetPwr').innerHTML = "Sollwert (Waiting): " + val + " %";
            document.getElementById('valTargetPwr').classList.add('warn');
            document.getElementById('pwrCard').classList.add('changed');
            document.getElementById('pwrSlider').classList.add('changed'); // Blocking slider overwriting from WebSocket
            
            var now = Date.now();
            if (now - lastPreviewTime > 50) {
                if (websocket && websocket.readyState === WebSocket.OPEN) {
                    websocket.send("PREVIEW_PWR:" + val);
                }
                lastPreviewTime = now;
            }
        }

        // Release the mouse/finger (Remove the lock, send the command)
        function applyPower(val) {
            document.getElementById('pwrSlider').classList.remove('changed'); // Allow WebSocket to update the slider
            if (websocket && websocket.readyState === WebSocket.OPEN) {
                websocket.send("PREVIEW_PWR:" + val);
                setTimeout(function() { 
                    websocket.send("APPLY_PWR"); 
                }, 10);
            }
        }

        function startSweep() {
            scanData = [];
            document.getElementById('chartLine').setAttribute("points", "");
            if (websocket && websocket.readyState === WebSocket.OPEN) { 
                websocket.send("START_SWEEP"); 
            }
        }

        function stopGenerator() {
            if (websocket && websocket.readyState === WebSocket.OPEN) { 
                websocket.send("STOP_GEN"); 
            }
        }

        function saveConfig() {
            var msg = "SET_CONFIG:" + 
                      document.getElementById('cfgNom').value + "," + 
                      document.getElementById('cfgStart').value + "," + 
                      document.getElementById('cfgEnd').value + "," + 
                      document.getElementById('cfgStep').value + "," + 
                      document.getElementById('cfgTime').value + "," + 
                      document.getElementById('cfgTargetTemp').value;
                      
            if (websocket && websocket.readyState === WebSocket.OPEN) { 
                websocket.send(msg); 
            }
        }

        window.onload = initWebSocket;
    </script>
</body>
</html>
)rawliteral";

#endif