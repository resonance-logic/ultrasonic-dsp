#ifndef __WEB_PAGE_H
#define __WEB_PAGE_H

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Ultrasonic Cavitation Controller</title>
    <style>
        body { font-family: Arial, sans-serif; background: #121212; color: #e0e0e0; margin: 20px; text-align: center; }
        .container { max-width: 600px; margin: 0 auto; background: #1e1e1e; padding: 20px; border-radius: 8px; box-shadow: 0 4px 10px rgba(0,0,0,0.5); }
        h1 { color: #00adb5; font-size: 24px; }
        .card { background: #252525; padding: 15px; margin: 10px 0; border-radius: 6px; text-align: left; }
        .label { font-size: 14px; color: #888; }
        .value { font-size: 22px; font-weight: bold; color: #fff; }
        .slider-container { margin: 20px 0; text-align: left; }
        input[type=range] { width: 100%; height: 8px; border-radius: 5px; background: #393e46; outline: none; -webkit-appearance: none; }
        input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; width: 20px; height: 20px; border-radius: 50%; background: #00adb5; cursor: pointer; }
        .btn { background: #00adb5; color: #fff; border: none; padding: 12px 24px; font-size: 16px; border-radius: 4px; cursor: pointer; width: 100%; font-weight: bold; margin-top: 10px; }
        .btn:hover { background: #007a82; }
        svg { background: #1a1a1a; border: 1px solid #333; margin-top: 15px; border-radius: 4px; }
        polyline { fill: none; stroke: #00adb5; stroke-width: 2; }
    </style>
</head>
<body>
    <div class="container">
        <h1>Ultrasonic Control Dashboard</h1>
        
        <div class="card">
            <div class="label">Current Resonance Frequency</div>
            <div id="freq_val" class="value">0 Hz</div>
        </div>

        <div class="card">
            <div class="label">Hydrophone Feedback (RMS)</div>
            <div id="rms_val" class="value">0.0000</div>
        </div>

        <div class="slider-container">
            <div class="label">Generator Power Level: <span id="power_txt">50</span>%</div>
            <input type="range" id="power_slider" min="0" max="100" value="50" oninput="updatePower(this.value)">
        </div>

        <button class="btn" id="scan_btn" onclick="triggerScan()">START CAVITATION SCAN</button>

        <!-- Native high-performance SVG chart instead of Chart.js -->
        <svg id="chart" width="560" height="200" viewBox="0 0 560 200">
            <polyline id="grid" points="0,150 560,150" stroke="#333" stroke-width="1" stroke-dasharray="5,5"/>
            <polyline id="sparkline" points=""></polyline>
        </svg>
    </div>

    <script>
        let gateway = `ws://${window.location.hostname}/ws`;
        let websocket;
        let isWebUpdating = false;
        let points = [];
        const maxPoints = 50;

        function initWebSocket() {
            websocket = new WebSocket(gateway);
            websocket.onmessage = onMessage;
        }

        function onMessage(event) {
            let data = JSON.parse(event.data);
            if (data.freq) document.getElementById('freq_val').innerText = data.freq + " Hz";
            if (data.rms) {
                document.getElementById('rms_val').innerText = data.rms;
                updateChart(parseFloat(data.rms));
            }
            if (data.power && !isWebUpdating) {
                document.getElementById('power_slider').value = data.power;
                document.getElementById('power_txt').innerText = data.power;
            }
        }

        function updatePower(val) {
            isWebUpdating = true;
            document.getElementById('power_txt').innerText = val;
            websocket.send(JSON.stringify({'power': parseInt(val)}));
            setTimeout(() => { isWebUpdating = false; }, 200); // Prevent echo feedback loop
        }

        function triggerScan() {
            websocket.send(JSON.stringify({'cmd': 'scan'}));
        }

        function updateChart(val) {
            points.push(val);
            if (points.length > maxPoints) points.shift();
            
            let maxVal = Math.max(...points, 1.0);
            let svgPoints = "";
            let stepX = 560 / (maxPoints - 1);
            
            for (let i = 0; i < points.length; i++) {
                let x = i * stepX;
                // Scale value to fit 200px height SVG (inverted Y-axis in SVG)
                let y = 180 - ((points[i] / maxVal) * 160);
                svgPoints += `${x},${y} `;
            }
            document.getElementById('sparkline').setAttribute('points', svgPoints);
        }

        window.onload = initWebSocket;
    </script>
</body>
</html>
)rawliteral";

#endif /* __WEB_PAGE_H */
