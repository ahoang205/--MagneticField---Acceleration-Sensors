/*
 * ESP32 WiFi Telemetry & Target Detector Server
 * For "Low-Power Instrument" Project
 * 
 * Hardware Connection:
 * - STM32 USART3 TX (PC4) -> ESP32 RX0 (GPIO3) [or RX2 (GPIO16)]
 * - STM32 USART3 RX (PC5) -> ESP32 TX0 (GPIO1) [or TX2 (GPIO17)]
 * 
 * Dependencies:
 * - Arduino WebSockets library by Markus Sattler (Install via Library Manager)
 */

#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <pgmspace.h>

// ===================== CONFIGURATION =====================
const char* ap_ssid = "ESP32_Sensor_AP";
const char* ap_password = "12345678";

// Đổi thành Serial nếu cắm vào chân RX0/TX0 của ESP32 (GPIO3/GPIO1)
#define STM32_SERIAL Serial
#define DEBUG_PRINTS_ENABLED false // Đặt thành false để tắt debug print khi dùng chung cổng Serial0 (tránh xung đột nạp code và sai lệnh cho STM32)

// Các giá trị cấu hình mặc định (có thể điều chỉnh qua Web GUI)
float delta_z_max = 100.0; // Ngưỡng chênh lệch từ trường (đơn vị mG)
float tilt_limit = 15.0;    // Góc nghiêng tối đa (độ) để chống báo giả khi mạch bị đổ/lệch
bool is_moving = false;     // Trạng thái IMU đang chuyển động/rung lắc

// ===================== GLOBALS =====================
WebServer server(80);
WebSocketsServer webSocket(81);

// Dữ liệu cảm biến nhận từ STM32
int32_t mx1 = 0, my1 = 0, mz1 = 0;
int32_t mx2 = 0, my2 = 0, mz2 = 0;
int16_t ax = 0, ay = 0, az = 0;

// Các giá trị tính toán
float pitch = 0.0;
float roll = 0.0;
float yaw = 0.0;
float delta_z = 0.0;
bool is_tilted = false;
bool is_triggered = false;

// Cấu hình bù trừ sai lệch từ trường tĩnh (Hard-iron offset calibration)
float delta_z_zero = 0.0;
bool auto_calibrated = false;
int calib_samples = 0;
float calib_sum = 0.0;

unsigned long last_ws_send = 0;

// ===================== HTML WEB PAGE =====================
const char HTML_CONTENT[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="vi">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Hệ Thống Telemetry Từ Trường & IMU</title>
    <!-- Google Fonts -->
    <link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;800&family=Share+Tech+Mono&display=swap" rel="stylesheet">
    <!-- Three.js -->
    <script src="https://cdnjs.cloudflare.com/ajax/libs/three.js/r128/three.min.js"></script>
    <style>
        :root {
            --bg-color: #080b11;
            --card-bg: rgba(13, 17, 28, 0.7);
            --border-color: rgba(255, 255, 255, 0.07);
            --text-primary: #f3f4f6;
            --text-secondary: #9ca3af;
            --accent-cyan: #06b6d4;
            --accent-green: #10b981;
            --accent-red: #ef4444;
            --accent-purple: #8b5cf6;
        }
        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
        }
        body {
            background-color: var(--bg-color);
            color: var(--text-primary);
            font-family: 'Outfit', sans-serif;
            overflow-x: hidden;
            display: flex;
            flex-direction: column;
            height: 100vh;
        }
        header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 15px 30px;
            background: rgba(8, 11, 17, 0.8);
            backdrop-filter: blur(10px);
            border-bottom: 1px solid var(--border-color);
            z-index: 10;
        }
        .logo-section h1 {
            font-size: 20px;
            font-weight: 800;
            letter-spacing: 1px;
            background: linear-gradient(to right, #fff, var(--accent-cyan));
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }
        .logo-section p {
            font-size: 11px;
            color: var(--text-secondary);
            text-transform: uppercase;
        }
        .conn-status {
            display: flex;
            align-items: center;
            gap: 8px;
            font-size: 13px;
            font-weight: 600;
            background: rgba(255,255,255,0.03);
            padding: 6px 14px;
            border-radius: 20px;
            border: 1px solid var(--border-color);
        }
        .conn-dot {
            width: 8px;
            height: 8px;
            background-color: var(--accent-red);
            border-radius: 50%;
            box-shadow: 0 0 8px var(--accent-red);
        }
        .conn-dot.active {
            background-color: var(--accent-green);
            box-shadow: 0 0 10px var(--accent-green);
            animation: pulse 1.5s infinite;
        }
        @keyframes pulse {
            0% { transform: scale(1); opacity: 1; }
            50% { transform: scale(1.2); opacity: 0.5; }
            100% { transform: scale(1); opacity: 1; }
        }
        .dashboard-container {
            display: flex;
            flex: 1;
            overflow: hidden;
            width: 100%;
        }
        .sidebar {
            width: 360px;
            background: var(--card-bg);
            border-right: 1px solid var(--border-color);
            padding: 25px;
            display: flex;
            flex-direction: column;
            gap: 20px;
            overflow-y: auto;
            backdrop-filter: blur(16px);
        }
        .main-view {
            flex: 1;
            display: flex;
            flex-direction: column;
            background: radial-gradient(circle at center, #111827 0%, #080b11 100%);
            position: relative;
        }
        .glass-card {
            background: rgba(255, 255, 255, 0.02);
            border: 1px solid var(--border-color);
            border-radius: 12px;
            padding: 20px;
        }
        .card-title {
            font-size: 11px;
            font-weight: 600;
            text-transform: uppercase;
            color: var(--text-secondary);
            letter-spacing: 1.5px;
            margin-bottom: 12px;
            display: flex;
            justify-content: space-between;
        }
        .stat-grid {
            display: grid;
            grid-template-cols: repeat(3, 1fr);
            gap: 10px;
        }
        .stat-box {
            background: rgba(0,0,0,0.3);
            border: 1px solid rgba(255,255,255,0.03);
            border-radius: 8px;
            padding: 10px;
            text-align: center;
        }
        .stat-label {
            font-size: 10px;
            color: var(--text-secondary);
            text-transform: uppercase;
        }
        .stat-value {
            font-family: 'Share Tech Mono', monospace;
            font-size: 16px;
            font-weight: 600;
            margin-top: 4px;
        }
        .stat-value.cyan { color: var(--accent-cyan); }
        .stat-value.purple { color: var(--accent-purple); }
        .stat-value.green { color: var(--accent-green); }
        
        .slider-group {
            display: flex;
            flex-direction: column;
            gap: 15px;
        }
        .slider-item {
            display: flex;
            flex-direction: column;
            gap: 5px;
        }
        .slider-header {
            display: flex;
            justify-content: space-between;
            font-size: 12px;
        }
        .slider-header span:last-child {
            font-family: 'Share Tech Mono', monospace;
            font-weight: 600;
            color: var(--accent-cyan);
        }
        .custom-slider {
            -webkit-appearance: none;
            width: 100%;
            height: 6px;
            border-radius: 3px;
            background: #1f2937;
            outline: none;
        }
        .custom-slider::-webkit-slider-thumb {
            -webkit-appearance: none;
            appearance: none;
            width: 16px;
            height: 16px;
            border-radius: 50%;
            background: var(--accent-cyan);
            cursor: pointer;
            box-shadow: 0 0 8px var(--accent-cyan);
            transition: transform 0.1s;
        }
        .custom-slider::-webkit-slider-thumb:hover {
            transform: scale(1.2);
        }
        
        .toggle-list {
            display: flex;
            flex-direction: column;
            gap: 10px;
        }
        .toggle-item {
            display: flex;
            justify-content: space-between;
            align-items: center;
            font-size: 12px;
        }
        .switch {
            position: relative;
            display: inline-block;
            width: 38px;
            height: 20px;
        }
        .switch input { opacity: 0; width: 0; height: 0; }
        .slider-switch {
            position: absolute;
            cursor: pointer;
            top: 0; left: 0; right: 0; bottom: 0;
            background-color: #1f2937;
            transition: .3s;
            border-radius: 34px;
            border: 1px solid var(--border-color);
        }
        .slider-switch:before {
            position: absolute;
            content: "";
            height: 14px;
            width: 14px;
            left: 2px;
            bottom: 2px;
            background-color: white;
            transition: .3s;
            border-radius: 50%;
        }
        input:checked + .slider-switch { background-color: var(--accent-cyan); }
        input:checked + .slider-switch:before { transform: translateX(18px); }

        .alarm-indicator {
            padding: 15px;
            border-radius: 10px;
            text-align: center;
            font-weight: 800;
            font-size: 18px;
            letter-spacing: 2px;
            transition: 0.3s;
            border: 1px solid var(--border-color);
        }
        .alarm-indicator.normal {
            background: rgba(16, 185, 129, 0.08);
            border-color: rgba(16, 185, 129, 0.3);
            color: var(--accent-green);
            box-shadow: inset 0 0 15px rgba(16, 185, 129, 0.1);
        }
        .alarm-indicator.triggered {
            background: rgba(239, 68, 68, 0.15);
            border-color: rgba(239, 68, 68, 0.5);
            color: var(--accent-red);
            box-shadow: inset 0 0 20px rgba(239, 68, 68, 0.2), 0 0 15px rgba(239, 68, 68, 0.2);
            animation: flash-red 1s infinite alternate;
        }
        .alarm-indicator.tilted {
            background: rgba(239, 68, 68, 0.05);
            border-color: rgba(239, 68, 68, 0.3);
            color: #f97316;
            box-shadow: inset 0 0 15px rgba(249, 115, 22, 0.1);
        }
        @keyframes flash-red {
            0% { opacity: 0.8; }
            100% { opacity: 1; }
        }

        #canvas3d {
            flex: 1;
            width: 100%;
        }
        
        .chart-container {
            height: 250px;
            background: rgba(8, 11, 17, 0.9);
            border-top: 1px solid var(--border-color);
            padding: 15px 30px;
            display: flex;
            flex-direction: column;
            z-index: 5;
        }
        .chart-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            font-size: 11px;
            color: var(--text-secondary);
            margin-bottom: 5px;
        }
        .chart-legends {
            display: flex;
            gap: 15px;
        }
        .legend-item {
            display: flex;
            align-items: center;
            gap: 6px;
        }
        .legend-color {
            width: 12px;
            height: 3px;
            border-radius: 1px;
        }
        .chart-wrapper {
            flex: 1;
            position: relative;
        }
        #scrolling-chart {
            width: 100%;
            height: 100%;
            display: block;
        }
        @media (max-width: 768px) {
            body {
                height: auto;
                overflow-y: auto;
            }
            header {
                padding: 12px 20px;
                flex-direction: column;
                gap: 8px;
                align-items: flex-start;
            }
            .logo-section h1 {
                font-size: 18px;
            }
            .dashboard-container {
                flex-direction: column;
                height: auto;
                overflow: visible;
            }
            .sidebar {
                width: 100%;
                border-right: none;
                border-bottom: 1px solid var(--border-color);
                padding: 20px 15px;
                order: 2;
            }
            .main-view {
                width: 100%;
                height: auto;
                order: 1;
            }
            #canvas3d {
                height: 300px;
                min-height: 250px;
            }
            .chart-container {
                height: 200px;
                padding: 10px 15px;
            }
        }
    </style>
</head>
<body>

    <header>
        <div class="logo-section">
            <h1>TELEMETRY DASHBOARD</h1>
            <p>Hệ Thống Cảm Biến Từ Trường & Gia Tốc Độ Trễ Thấp</p>
        </div>
        <div class="conn-status">
            <span id="status-dot" class="conn-dot"></span>
            <span id="status-text">Đang kết nối...</span>
        </div>
    </header>

    <div class="dashboard-container">
        
        <!-- BẢNG ĐIỀU KHIỂN BÊN TRÁI -->
        <div class="sidebar">
            <!-- TRẠNG THÁI HỆ THỐNG -->
            <div id="alarm-box" class="alarm-indicator normal">
                HỆ THỐNG BÌNH THƯỜNG
            </div>

            <!-- GÓC NGHIÊNG THỜI GIAN THỰC -->
            <div class="glass-card">
                <div class="card-title">GÓC NGHIÊNG IMU</div>
                <div class="stat-grid">
                    <div class="stat-box">
                        <div class="stat-label">ROLL</div>
                        <div id="val-roll" class="stat-value green">0.0°</div>
                    </div>
                    <div class="stat-box">
                        <div class="stat-label">PITCH</div>
                        <div id="val-pitch" class="stat-value cyan">0.0°</div>
                    </div>
                    <div class="stat-box">
                        <div class="stat-label">YAW</div>
                        <div id="val-yaw" class="stat-value purple">0.0°</div>
                    </div>
                </div>
            </div>

            <!-- THIẾT LẬP NGƯỠNG -->
            <div class="glass-card">
                <div class="card-title">CÀI ĐẶT NGƯỠNG KÍCH HOẠT</div>
                <div class="slider-group">
                    <div class="slider-item">
                        <div class="slider-header">
                            <span>Ngưỡng Từ Trường (mG)</span>
                            <span id="lbl-dz-max">100</span>
                        </div>
                        <input type="range" id="sld-dz-max" class="custom-slider" min="50" max="10000" step="10" value="100">
                    </div>
                    <div class="slider-item">
                        <div class="slider-header">
                            <span>Giới Hạn Góc Nghiêng (Độ)</span>
                            <span id="lbl-tilt-limit">15°</span>
                        </div>
                        <input type="range" id="sld-tilt-limit" class="custom-slider" min="5" max="45" step="1" value="15">
                    </div>
                </div>
            </div>

            <!-- CẤU HÌNH TRỤC XOAY -->
            <div class="glass-card">
                <div class="card-title">CẤU HÌNH TRỤC 3D</div>
                <div class="toggle-list">
                    <div class="toggle-item">
                        <span>Đảo ngược trục X (Pitch)</span>
                        <label class="switch">
                            <input type="checkbox" id="inv-pitch">
                            <span class="slider-switch"></span>
                        </label>
                    </div>
                    <div class="toggle-item">
                        <span>Đảo ngược trục Z (Roll)</span>
                        <label class="switch">
                            <input type="checkbox" id="inv-roll">
                            <span class="slider-switch"></span>
                        </label>
                    </div>
                    <div class="toggle-item">
                        <span>Đảo ngược trục Y (Yaw)</span>
                        <label class="switch">
                            <input type="checkbox" id="inv-yaw" checked>
                            <span class="slider-switch"></span>
                        </label>
                    </div>
                    <div class="toggle-item">
                        <span>Tráo đổi trục Pitch & Roll</span>
                        <label class="switch">
                            <input type="checkbox" id="swap-axes">
                            <span class="slider-switch"></span>
                        </label>
                    </div>
                </div>
            </div>

            <!-- THÔNG SỐ TỪ TRƯỜNG CHI TIẾT -->
            <div class="glass-card">
                <div class="card-title">GIÁ TRỊ TỪ TRƯỜNG THỰC TẾ</div>
                <div style="font-family:'Share Tech Mono', monospace; font-size:12px; display:flex; flex-direction:column; gap:6px;">
                    <div style="display:flex; justify-content:space-between;">
                        <span style="color:var(--text-secondary);">Cảm biến 1 (Z):</span>
                        <span id="val-mz1" style="color:var(--accent-cyan); font-weight:bold;">0</span>
                    </div>
                    <div style="display:flex; justify-content:space-between;">
                        <span style="color:var(--text-secondary);">Cảm biến 2 (Z):</span>
                        <span id="val-mz2" style="color:var(--accent-purple); font-weight:bold;">0</span>
                    </div>
                    <div style="display:flex; justify-content:space-between; border-top:1px solid rgba(255,255,255,0.05); padding-top:4px;">
                        <span style="color:var(--text-secondary); font-weight:600;">Chênh lệch (ΔZ):</span>
                        <span id="val-dz" style="color:#fbbf24; font-weight:bold;">0</span>
                    </div>
                    <button id="btn-calibrate" style="margin-top:8px; width:100%; padding:8px; background:rgba(6,182,212,0.15); border:1px solid var(--accent-cyan); border-radius:6px; color:var(--accent-cyan); font-weight:600; cursor:pointer; font-family:'Outfit'; font-size:11px; transition:0.2s;" onmouseover="this.style.background='rgba(6,182,212,0.3)'" onmouseout="this.style.background='rgba(6,182,212,0.15)'">HIỆU CHUẨN ĐIỂM KHÔNG</button>
                </div>
            </div>
        </div>

        <!-- PHẦN HIỂN THỊ CHÍNH -->
        <div class="main-view">
            <!-- Canvas Three.js hiển thị 3D PCB -->
            <div id="canvas3d"></div>

            <!-- ĐỒ THỊ CUỘN SÓNG PHÍA DƯỚI -->
            <div class="chart-container">
                <div class="chart-header">
                    <span>ĐỒ THỊ BIẾN THIÊN TỪ TRƯỜNG THỜI GIAN THỰC</span>
                    <div class="chart-legends">
                        <div class="legend-item">
                            <span class="legend-color" style="background:#06b6d4;"></span>
                            <span>Cảm Biến 1 (mz1)</span>
                        </div>
                        <div class="legend-item">
                            <span class="legend-color" style="background:#8b5cf6;"></span>
                            <span>Cảm Biến 2 (mz2)</span>
                        </div>
                        <div class="legend-item">
                            <span class="legend-color" style="background:#fbbf24;"></span>
                            <span>Chênh lệch (ΔZ)</span>
                        </div>
                        <div class="legend-item">
                            <span class="legend-color" style="border-top:1px dashed #ef4444; height:0;"></span>
                            <span>Đường Ngưỡng</span>
                        </div>
                    </div>
                </div>
                <div class="chart-wrapper">
                    <canvas id="scrolling-chart"></canvas>
                </div>
            </div>
        </div>

    </div>

    <script>
        // ===================== WEBSOCKETS =====================
        let socket;
        const statusDot = document.getElementById('status-dot');
        const statusText = document.getElementById('status-text');

        function connectWS() {
            const espIP = window.location.hostname || "192.168.4.1";
            socket = new WebSocket(`ws://${espIP}:81`);
            
            socket.onopen = () => {
                statusDot.className = "conn-dot active";
                statusText.innerText = "Đã kết nối";
            };

            socket.onclose = () => {
                statusDot.className = "conn-dot";
                statusText.innerText = "Mất kết nối. Đang thử lại...";
                setTimeout(connectWS, 1500);
            };

            socket.onerror = (err) => {
                console.error("WS error:", err);
            };

            socket.onmessage = (event) => {
                const data = JSON.parse(event.data);
                processIncomingData(data);
            };
        }

        // ===================== DATA PROCESSING =====================
        const radToDeg = 180 / Math.PI;
        
        let targetPitch = 0.0;
        let targetRoll = 0.0;
        let targetYaw = 0.0;
        
        let currentDzMax = 1500.0;

        function processIncomingData(data) {
            // Cập nhật giá trị hiển thị chữ
            document.getElementById('val-pitch').innerText = (data.pitch * radToDeg).toFixed(1) + '°';
            document.getElementById('val-roll').innerText = (data.roll * radToDeg).toFixed(1) + '°';
            document.getElementById('val-yaw').innerText = (data.yaw * radToDeg).toFixed(1) + '°';

            document.getElementById('val-mz1').innerText = data.mz1;
            document.getElementById('val-mz2').innerText = data.mz2;
            document.getElementById('val-dz').innerText = Math.round(data.dz);

            // Xử lý đảo chiều / đổi trục động
            const invPitch = document.getElementById('inv-pitch').checked ? -1 : 1;
            const invRoll = document.getElementById('inv-roll').checked ? -1 : 1;
            const invYaw = document.getElementById('inv-yaw').checked ? -1 : 1;
            const swapAxes = document.getElementById('swap-axes').checked;

            let pitchAngle = data.pitch;
            let rollAngle = data.roll;
            if (swapAxes) {
                pitchAngle = data.roll;
                rollAngle = data.pitch;
            }

            targetPitch = pitchAngle * invPitch;
            targetRoll = rollAngle * invRoll;
            targetYaw = data.yaw * invYaw;

            // Cập nhật Alarm Badge
            const alarmBox = document.getElementById('alarm-box');
            if (data.tilted === 1) {
                alarmBox.className = "alarm-indicator tilted";
                alarmBox.innerText = "CẢNH BÁO: CẢM BIẾN BỊ LỆCH!";
            } else if (data.moving === 1) {
                alarmBox.className = "alarm-indicator tilted";
                alarmBox.innerText = "IMU: ĐANG RUNG LẮC (KHOÁ KÍCH)";
            } else if (data.trig === 1) {
                alarmBox.className = "alarm-indicator triggered";
                alarmBox.innerText = "CÓ PHÁT HIỆN MỤC TIÊU";
            } else {
                alarmBox.className = "alarm-indicator normal";
                alarmBox.innerText = "HỆ THỐNG BÌNH THƯỜNG";
            }

            // Đồng bộ thanh trượt nếu thay đổi từ ESP32
            if (data.dz_max !== currentDzMax) {
                currentDzMax = data.dz_max;
                document.getElementById('sld-dz-max').value = data.dz_max;
                document.getElementById('lbl-dz-max').innerText = Math.round(data.dz_max);
            }
            document.getElementById('lbl-tilt-limit').innerText = Math.round(data.tilt_lim) + '°';
            document.getElementById('sld-tilt-limit').value = data.tilt_lim;

            // Đẩy dữ liệu vào mảng vẽ đồ thị
            pushToGraph(data.mz1, data.mz2, data.dz);
        }

        // Gửi thay đổi cấu hình lên ESP32
        document.getElementById('sld-dz-max').addEventListener('input', (e) => {
            const val = e.target.value;
            document.getElementById('lbl-dz-max').innerText = val;
            if (socket && socket.readyState === WebSocket.OPEN) {
                socket.send(`THRES:${val}`);
            }
        });

        document.getElementById('sld-tilt-limit').addEventListener('input', (e) => {
            const val = e.target.value;
            document.getElementById('lbl-tilt-limit').innerText = val + '°';
            if (socket && socket.readyState === WebSocket.OPEN) {
                socket.send(`TILT:${val}`);
            }
        });

        document.getElementById('btn-calibrate').addEventListener('click', () => {
            if (socket && socket.readyState === WebSocket.OPEN) {
                socket.send("CALIBRATE");
            }
        });

        // ===================== THREE.JS 3D PCB GRAPHICS =====================
        const container = document.getElementById('canvas3d');
        const scene = new THREE.Scene();
        scene.background = null; // transparent to see gradient

        const camera = new THREE.PerspectiveCamera(45, container.clientWidth / container.clientHeight, 0.1, 100);
        camera.position.set(0, 4.5, 9);
        camera.lookAt(0, 0, 0);

        const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
        renderer.setSize(container.clientWidth, container.clientHeight);
        renderer.setPixelRatio(window.devicePixelRatio);
        renderer.shadowMap.enabled = true;
        container.appendChild(renderer.domElement);

        // Lights
        const ambientLight = new THREE.AmbientLight(0xffffff, 0.35);
        scene.add(ambientLight);

        const dirLight = new THREE.DirectionalLight(0xffffff, 0.85);
        dirLight.position.set(5, 10, 6);
        scene.add(dirLight);

        const pointLight = new THREE.PointLight(0x06b6d4, 1.8, 15);
        pointLight.position.set(-3, 3.5, -3);
        scene.add(pointLight);

        // Grid
        const gridHelper = new THREE.GridHelper(16, 16, 0x1e293b, 0x0f172a);
        gridHelper.position.y = -1.8;
        scene.add(gridHelper);

        const boardGroup = new THREE.Group();
        scene.add(boardGroup);

        // Tọa độ bao ngoài PCB trích xuất từ KiCad
        const pcbOutline = [
            [-0.1938, -1.8574], [0.1613, -1.8574], [0.2450, -1.8920], [0.2796, -1.9757], [0.2796, -2.1570], 
            [0.3003, -2.2019], [0.3479, -2.2155], [1.6057, -1.5807], [2.2500, -0.3278], [2.2366, -0.2800], 
            [2.1915, -0.2591], [2.0129, -0.2591], [1.9292, -0.2244], [1.8945, -0.1407], [1.8945, 0.2144], 
            [1.9292, 0.2981], [2.0129, 0.3327], [2.1915, 0.3327], [2.2366, 0.3536], [2.2500, 0.4014], 
            [1.7183, 1.5342], [0.6688, 2.2155], [0.6162, 2.2065], [0.5923, 2.1589], [0.5922, 1.8481], 
            [0.5576, 1.7643], [0.4739, 1.7297], [-0.4732, 1.7297], [-0.5569, 1.7643], [-0.5915, 1.8481], 
            [-0.5915, 2.1589], [-0.6155, 2.2065], [-0.6681, 2.2155], [-1.7192, 1.5324], [-2.2500, 0.3967], 
            [-2.2366, 0.3489], [-2.1915, 0.3281], [-2.0122, 0.3281], [-1.9285, 0.2934], [-1.8938, 0.2097], 
            [-1.8938, -0.1453], [-1.9285, -0.2290], [-2.0122, -0.2637], [-2.1900, -0.2637], [-2.2352, -0.2846], 
            [-2.2484, -0.3325], [-1.6155, -1.5702], [-0.3813, -2.2099], [-0.3333, -2.1968], [-0.3122, -2.1516], 
            [-0.3122, -1.9757], [-0.2775, -1.8920], [-0.1938, -1.8574]
        ];

        // Vẽ biên dạng PCB dập nổi
        const shape = new THREE.Shape();
        shape.moveTo(pcbOutline[0][0], pcbOutline[0][1]);
        for (let i = 1; i < pcbOutline.length; i++) {
            shape.lineTo(pcbOutline[i][0], pcbOutline[i][1]);
        }
        shape.closePath();

        const extrudeSettings = {
            depth: 0.12,
            bevelEnabled: true,
            bevelSegments: 3,
            steps: 1,
            bevelSize: 0.015,
            bevelThickness: 0.015
        };

        const boardGeometry = new THREE.ExtrudeGeometry(shape, extrudeSettings);
        const boardMaterial = new THREE.MeshStandardMaterial({ 
            color: 0x0f1c18, // Màu xanh lá hàn nhám PCB
            roughness: 0.45,
            metalness: 0.15
        });
        const boardMesh = new THREE.Mesh(boardGeometry, boardMaterial);
        boardMesh.rotation.x = -Math.PI / 2; // Nằm phẳng
        boardMesh.position.y = -0.06;
        boardGroup.add(boardMesh);

        // Tạo các linh kiện 3D trên mặt bo
        const zOffset = 0.12;
        const scale = 4.5 / 76.02;

        // MCU U6
        const mcuGeom = new THREE.BoxGeometry(10 * scale, 10 * scale, 0.09);
        const mcuMat = new THREE.MeshStandardMaterial({ color: 0x18181b, roughness: 0.6 });
        const mcu = new THREE.Mesh(mcuGeom, mcuMat);
        mcu.position.set(0.06, -1.22, zOffset + 0.045);
        mcu.rotation.z = Math.PI / 2;
        boardMesh.add(mcu);

        // Siêu tụ điện C23 (Hình trụ đỏ)
        const capGeom = new THREE.CylinderGeometry(8 * scale, 8 * scale, 29 * scale, 24);
        const capMat = new THREE.MeshStandardMaterial({ color: 0xd97706, metalness: 0.6, roughness: 0.3 });
        const cap = new THREE.Mesh(capGeom, capMat);
        cap.position.set(-0.06, 1.18, zOffset + 8 * scale);
        cap.rotation.x = Math.PI / 2;
        cap.rotation.y = 135 * Math.PI / 180;
        boardMesh.add(cap);

        // Cảm biến từ U4, U5 & IMU U3
        const sensorGeom = new THREE.BoxGeometry(3 * scale, 3 * scale, 0.06);
        const sensorMat = new THREE.MeshStandardMaterial({ color: 0xd97706, metalness: 0.5, roughness: 0.4 });
        
        const u3 = new THREE.Mesh(sensorGeom, sensorMat); // IMU
        u3.position.set(-0.1, -0.48, zOffset + 0.03);
        u3.rotation.z = Math.PI / 2;
        boardMesh.add(u3);

        const u4 = new THREE.Mesh(sensorGeom, sensorMat); // Mag 1
        u4.position.set(-1.18, -1.75, zOffset + 0.03);
        boardMesh.add(u4);

        const u5 = new THREE.Mesh(sensorGeom, sensorMat); // Mag 2
        u5.position.set(1.18, -1.75, zOffset + 0.03);
        boardMesh.add(u5);

        // Xử lý co giãn màn hình
        window.addEventListener('resize', () => {
            camera.aspect = container.clientWidth / container.clientHeight;
            camera.updateProjectionMatrix();
            renderer.setSize(container.clientWidth, container.clientHeight);
        });

        // LERP giải quyết điểm lật góc từ -PI sang PI
        function lerpAngle(current, target, factor) {
            let diff = target - current;
            while (diff < -Math.PI) diff += 2 * Math.PI;
            while (diff > Math.PI) diff -= 2 * Math.PI;
            return current + diff * factor;
        }

        // Loop hoạt ảnh xoay 3D (60 FPS LERP)
        function animate() {
            requestAnimationFrame(animate);

            boardGroup.rotation.x = lerpAngle(boardGroup.rotation.x, targetPitch, 0.12);
            boardGroup.rotation.z = lerpAngle(boardGroup.rotation.z, -targetRoll, 0.12);
            boardGroup.rotation.y = lerpAngle(boardGroup.rotation.y, -targetYaw, 0.12);

            renderer.render(scene, camera);
        }
        animate();

        // ===================== REAL-TIME CANVAS CHART =====================
        const canvas = document.getElementById('scrolling-chart');
        const ctx = canvas.getContext('2d');

        // Phóng to độ phân giải canvas để vẽ mượt hơn
        function resizeCanvas() {
            canvas.width = canvas.parentElement.clientWidth * window.devicePixelRatio;
            canvas.height = canvas.parentElement.clientHeight * window.devicePixelRatio;
            ctx.scale(window.devicePixelRatio, window.devicePixelRatio);
        }
        window.addEventListener('resize', resizeCanvas);
        resizeCanvas();

        const historyLength = 180;
        const dataHistory = {
            mz1: new Array(historyLength).fill(131000),
            mz2: new Array(historyLength).fill(131000),
            dz: new Array(historyLength).fill(0)
        };

        function pushToGraph(mz1, mz2, dz) {
            dataHistory.mz1.push(mz1);
            dataHistory.mz1.shift();
            
            dataHistory.mz2.push(mz2);
            dataHistory.mz2.shift();
            
            dataHistory.dz.push(dz);
            dataHistory.dz.shift();

            drawChart();
        }

        function drawChart() {
            const w = canvas.width / window.devicePixelRatio;
            const h = canvas.height / window.devicePixelRatio;
            ctx.clearRect(0, 0, w, h);

            // Tìm min, max để căn tỷ lệ tự động
            let maxVal = -Infinity;
            let minVal = Infinity;
            
            // Chỉ lấy tỷ lệ dựa trên dữ liệu mz1 và mz2
            for (let i = 0; i < historyLength; i++) {
                maxVal = Math.max(maxVal, dataHistory.mz1[i], dataHistory.mz2[i], currentDzMax);
                minVal = Math.min(minVal, dataHistory.mz1[i], dataHistory.mz2[i], 0);
            }
            const range = (maxVal - minVal) * 1.15 || 100;
            const center = (maxVal + minVal) / 2;

            function getY(val) {
                // Đảo ngược trục Y đồ thị (hệ tọa độ canvas gốc ở trên)
                return h - 15 - ((val - minVal) / range) * (h - 30);
            }

            // Vẽ lưới đồ thị (Grid Lines)
            ctx.strokeStyle = 'rgba(255,255,255,0.03)';
            ctx.lineWidth = 1;
            for (let yGrid = 0; yGrid <= 4; yGrid++) {
                const yPos = 15 + (yGrid / 4) * (h - 30);
                ctx.beginPath();
                ctx.moveTo(0, yPos);
                ctx.lineTo(w, yPos);
                ctx.stroke();

                // Nhãn trục Y
                const gridVal = maxVal - (yGrid / 4) * (maxVal - minVal);
                ctx.fillStyle = '#6b7280';
                ctx.font = '9px "Share Tech Mono"';
                ctx.fillText(Math.round(gridVal), 5, yPos - 3);
            }

            // Vẽ đường dữ liệu
            const step = w / (historyLength - 1);

            function drawLine(history, color, width, isDashed = false) {
                ctx.strokeStyle = color;
                ctx.lineWidth = width;
                if (isDashed) ctx.setLineDash([4, 4]);
                else ctx.setLineDash([]);
                
                ctx.beginPath();
                ctx.moveTo(0, getY(history[0]));
                for (let i = 1; i < historyLength; i++) {
                    ctx.lineTo(i * step, getY(history[i]));
                }
                ctx.stroke();
            }

            // Vẽ mz1 (Cyan)
            drawLine(dataHistory.mz1, '#06b6d4', 1.5);
            // Vẽ mz2 (Purple)
            drawLine(dataHistory.mz2, '#8b5cf6', 1.5);
            // Vẽ dz (Yellow)
            drawLine(dataHistory.dz, '#fbbf24', 2.0);

            // Vẽ đường ngưỡng Delta Z Max (Dashed Red, đổi mG sang LSB để vẽ đồ thị)
            const currentDzMaxLsb = currentDzMax * 16.384;
            const yThres = getY(currentDzMaxLsb);
            ctx.strokeStyle = '#ef4444';
            ctx.lineWidth = 1.5;
            ctx.setLineDash([6, 4]);
            ctx.beginPath();
            ctx.moveTo(0, yThres);
            ctx.lineTo(w, yThres);
            ctx.stroke();
            
            // Nhãn chữ đường ngưỡng
            ctx.fillStyle = '#ef4444';
            ctx.font = 'bold 9px "Outfit"';
            ctx.setLineDash([]);
            ctx.fillText("NGƯỠNG: " + currentDzMax + " mG", w - 85, yThres - 5);
        }

        // Khởi chạy
        connectWS();
        drawChart();
    </script>
</body>
</html>
)rawhtml";

// ===================== TELEMETRY SEND TO CLIENT =====================
void sendTelemetryData() {
    String payload = "{";
    payload += "\"mz1\":" + String(mx1) + ",";
    payload += "\"my1\":" + String(my1) + ",";
    payload += "\"mz1\":" + String(mz1) + ",";
    payload += "\"mx2\":" + String(mx2) + ",";
    payload += "\"my2\":" + String(my2) + ",";
    payload += "\"mz2\":" + String(mz2) + ",";
    payload += "\"ax\":" + String(ax) + ",";
    payload += "\"ay\":" + String(ay) + ",";
    payload += "\"az\":" + String(az) + ",";
    payload += "\"pitch\":" + String(pitch, 4) + ",";
    payload += "\"roll\":" + String(roll, 4) + ",";
    payload += "\"yaw\":" + String(yaw, 4) + ",";
    payload += "\"dz\":" + String(delta_z, 1) + ",";
    payload += "\"dz_max\":" + String(delta_z_max, 1) + ",";
    payload += "\"tilt_lim\":" + String(tilt_limit, 1) + ",";
    payload += "\"tilted\":" + String(is_tilted ? 1 : 0) + ",";
    payload += "\"moving\":" + String(is_moving ? 1 : 0) + ",";
    payload += "\"trig\":" + String(is_triggered ? 1 : 0);
    payload += "}";
    
    webSocket.broadcastTXT(payload);
}

// ===================== WEBSOCKET EVENT HANDLER =====================
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    if (type == WStype_TEXT) {
        String msg = String((char*)payload).substring(0, length);
        
        // Nhận lệnh cài đặt ngưỡng từ trường: THRES:<val>
        if (msg.startsWith("THRES:")) {
            float val = msg.substring(6).toFloat();
            if (val > 0) {
                delta_z_max = val;
                // Gửi cập nhật tức thì đến tất cả thiết bị khác
                sendTelemetryData();
            }
        }
        // Nhận lệnh cài đặt giới hạn nghiêng: TILT:<val>
        else if (msg.startsWith("TILT:")) {
            float val = msg.substring(5).toFloat();
            if (val >= 5 && val <= 45) {
                tilt_limit = val;
                sendTelemetryData();
            }
        }
        // Nhận lệnh thiết lập điểm 0: CALIBRATE
        else if (msg.equals("CALIBRATE")) {
            delta_z_zero = (float)(mz1 - mz2);
            auto_calibrated = true;
            if (DEBUG_PRINTS_ENABLED) {
                Serial.print("[Calibrate] Set baseline delta_z_zero to: ");
                Serial.println(delta_z_zero);
            }
            sendTelemetryData();
        }
    }
}

// ===================== ARDUINO INITIALIZATION =====================
void setup() {
    // Khởi tạo serial nạp code và giao tiếp với STM32 (dùng chung Serial0 - GPIO1 & GPIO3)
    Serial.begin(115200);
    
    // Khởi tạo cổng giao tiếp với STM32 (nếu dùng chung cổng Serial0)
    // STM32_SERIAL.begin(115200);
    // Serial2.begin(115200, SERIAL_8N1, 16, 17); // Đã vô hiệu hóa Serial2 khi dùng GPIO1/3

    // Phát Wifi Access Point riêng
    WiFi.softAP(ap_ssid, ap_password);
    IPAddress IP = WiFi.softAPIP();
    
    // Ghi debug ra máy tính lúc khởi động
    if (DEBUG_PRINTS_ENABLED) {
        Serial.println();
        Serial.print("Access Point SSID: ");
        Serial.println(ap_ssid);
        Serial.print("Web server chay tai dia chi IP: http://");
        Serial.println(IP);
    }

    // Cấu hình HTTP Server để phục vụ trang web
    server.on("/", HTTP_GET, []() {
        server.send_P(200, "text/html", HTML_CONTENT);
    });
    server.begin();

    // Khởi động WebSocket Server
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
}

// ===================== DATA PARSING & PROCESS =====================
void parseAndProcessLine(const String& line) {
    // Định dạng nhận từ STM32: "MMC1: %ld %ld %ld | MMC2: %ld %ld %ld | ACC: %d %d %d"
    int32_t raw_mx1, raw_my1, raw_mz1;
    int32_t raw_mx2, raw_my2, raw_mz2;
    int raw_ax, raw_ay, raw_az;
    
    int parsed = sscanf(line.c_str(), 
                        "MMC1: %d %d %d | MMC2: %d %d %d | ACC: %d %d %d", 
                        &raw_mx1, &raw_my1, &raw_mz1, 
                        &raw_mx2, &raw_my2, &raw_mz2, 
                        &raw_ax, &raw_ay, &raw_az);
                        
    if (DEBUG_PRINTS_ENABLED) {
        Serial.print("[Debug Parse] sscanf parsed: ");
        Serial.println(parsed);
    }
                        
    if (parsed == 9) {
        // Lưu giá trị thô
        mx1 = raw_mx1; my1 = raw_my1; mz1 = raw_mz1;
        mx2 = raw_mx2; my2 = raw_my2; mz2 = raw_mz2;
        ax = raw_ax; ay = raw_ay; az = raw_az;

        // Lưu trữ giá trị cũ của gia tốc để phát hiện chuyển động/rung lắc
        static int16_t prev_ax = 0;
        static int16_t prev_ay = 0;
        static int16_t prev_az = 0;
        static bool first_run = true;
        static unsigned long last_motion_time = 0;

        if (first_run) {
            prev_ax = ax;
            prev_ay = ay;
            prev_az = az;
            first_run = false;
        }

        // Tính độ biến động gia tốc (rung lắc) giữa 2 chu kỳ đọc liên tiếp
        int32_t motion = abs(ax - prev_ax) + abs(ay - prev_ay) + abs(az - prev_az);
        
        // Cập nhật giá trị cũ cho chu kỳ tiếp theo
        prev_ax = ax;
        prev_ay = ay;
        prev_az = az;

        // Nếu gia tốc biến động mạnh (> 300 LSB), ghi nhận thời điểm chuyển động cuối cùng
        if (motion > 300) {
            last_motion_time = millis();
        }

        // Khóa báo động trong vòng 500ms sau lần chuyển động/rung lắc cuối cùng
        is_moving = (millis() - last_motion_time < 500);

        // Quy đổi gia tốc (STM32 LSB sang đơn vị g, tầm đo +-2g tương đương 16-bit signed)
        float ax_g = ax / 16384.0;
        float ay_g = ay / 16384.0;
        float az_g = az / 16384.0;

        // Tính Pitch, Roll (radian)
        pitch = atan2(ay_g, sqrt(ax_g * ax_g + az_g * az_g));
        roll = atan2(-ax_g, az_g);
        
        // Tính Yaw đơn giản từ Mag 1
        yaw = atan2((float)my1, (float)mx1);

        // 1. Tự động lấy trung bình 20 mẫu đầu tiên làm điểm zero tĩnh
        if (!auto_calibrated) {
            calib_sum += (float)(mz1 - mz2);
            calib_samples++;
            if (calib_samples >= 20) {
                delta_z_zero = calib_sum / 20.0;
                auto_calibrated = true;
                if (DEBUG_PRINTS_ENABLED) {
                    Serial.print("[Auto-Calibrate] Completed! delta_z_zero = ");
                    Serial.println(delta_z_zero);
                }
            }
        }

        // Tính chênh lệch từ trường Z giữa 2 cảm biến (đã bù trừ điểm zero tĩnh) (đơn vị LSB)
        delta_z = abs(((float)(mz1 - mz2)) - delta_z_zero);

        // Quy đổi góc sang Độ để so sánh giới hạn nghiêng
        float pitch_deg = abs(pitch * 180.0 / PI);
        float roll_deg = abs(roll * 180.0 / PI);

        // Kiểm tra trạng thái nghiêng lệch mạch (được truyền về web GUI hiển thị)
        is_tilted = (pitch_deg > tilt_limit) || (roll_deg > tilt_limit);

        // Quy đổi ngưỡng cài đặt từ mG sang LSB để so sánh trực tiếp với delta_z
        float delta_z_max_lsb = delta_z_max * 16.384;

        // Kích hoạt LED/Relay: Khi chênh lệch từ trường vượt ngưỡng VÀ mạch đang đứng yên (chỉ kích hoạt sau khi đã hiệu chuẩn xong)
        is_triggered = auto_calibrated && (delta_z > delta_z_max_lsb) && (!is_moving);

        if (DEBUG_PRINTS_ENABLED) {
            Serial.print("[Debug Trigger] dz_lsb: ");
            Serial.print(delta_z);
            Serial.print(" max_lsb: ");
            Serial.print(delta_z_max_lsb);
            Serial.print(" moving: ");
            Serial.print(is_moving ? "YES" : "NO");
            Serial.print(" trig: ");
            Serial.println(is_triggered);
        }

        // Gửi lệnh đóng/ngắt LED về lại cho STM32 qua UART3 RX
        if (is_triggered) {
            STM32_SERIAL.print('1'); // Lệnh bật nguồn J1/J3 (Bật LED/Relay)
            if (DEBUG_PRINTS_ENABLED) {
                Serial.println("[Debug Send] Sent '1' to STM32");
            }
        } else {
            STM32_SERIAL.print('0'); // Lệnh tắt nguồn J1/J3 (Tắt LED/Relay)
            if (DEBUG_PRINTS_ENABLED) {
                Serial.println("[Debug Send] Sent '0' to STM32");
            }
        }

        // Truyền gói dữ liệu lên Web GUI thông qua WebSockets (tần số tối đa 40Hz)
        if (millis() - last_ws_send >= 25) {
            last_ws_send = millis();
            sendTelemetryData();
        }
    }
}

// ===================== MAIN LOOP =====================
void loop() {
    server.handleClient();
    webSocket.loop();

    // Đọc và phân tích luồng Serial từ STM32 phi chặn (Non-blocking)
    static String rx_buffer = "";
    while (STM32_SERIAL.available()) {
        char c = STM32_SERIAL.read();
        if (c == '\n') {
            rx_buffer.trim();
            if (rx_buffer.length() > 0) {
                if (DEBUG_PRINTS_ENABLED) {
                    Serial.print("[Debug UART] Raw: ");
                    Serial.println(rx_buffer);
                }
                parseAndProcessLine(rx_buffer);
            }
            rx_buffer = "";
        } else if (c != '\r') {
            rx_buffer += c;
            if (rx_buffer.length() > 150) { // Chống tràn bộ đệm
                rx_buffer = "";
            }
        }
    }
}
