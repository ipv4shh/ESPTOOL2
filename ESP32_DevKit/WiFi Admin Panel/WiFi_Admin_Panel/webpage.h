#pragma once
#include <Arduino.h>

const String webpage = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESPTOOL2 | CONTROL CONSOLE</title>
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=JetBrains+Mono:wght@400;500;700&display=swap" rel="stylesheet">
  <style>
    :root {
      --primary-color: #ff4122;
      --bg-color: #050508;
      --panel-bg: #09090b;
      --border-color: #27272a;
      --text-main: #e4e4e7;
      --text-muted: #71717a;
      --green-status: #10b981;
      --red-status: #ef4444;
      --amber-status: #f59e0b;
    }
    * { margin: 0; padding: 0; box-sizing: border-box; font-family: 'JetBrains Mono', monospace; }
    body {
      background: var(--bg-color);
      color: var(--text-main);
      display: flex;
      justify-content: center;
      align-items: center;
      min-height: 100vh;
      padding: 16px;
    }
    .container {
      background: var(--panel-bg);
      padding: 20px;
      border-radius: 4px;
      max-width: 650px;
      width: 100%;
      border: 1px solid var(--border-color);
      text-align: center;
    }
    .ascii-art {
      font-size: 8px;
      line-height: 1.1;
      color: var(--primary-color);
      text-align: center;
      margin-bottom: 16px;
      white-space: pre;
      display: block;
      overflow-x: auto;
    }
    @media (min-width: 480px) {
      .ascii-art {
        font-size: 10px;
      }
    }
    .sub {
      color: var(--text-muted);
      font-size: 10px;
      margin-bottom: 16px;
      letter-spacing: 1px;
      text-transform: uppercase;
    }
    .connection-status {
      display: flex;
      align-items: center;
      justify-content: space-between;
      background: #020203;
      border: 1px solid var(--border-color);
      padding: 8px 12px;
      border-radius: 4px;
      margin-bottom: 16px;
      font-size: 11px;
    }
    .status-info {
      display: flex;
      align-items: center;
      gap: 8px;
    }
    .status-dot {
      width: 6px;
      height: 6px;
      border-radius: 50%;
      display: inline-block;
    }
    .status-dot.offline { background: var(--red-status); }
    .status-dot.online { background: var(--green-status); }
    .status-dot.testing { background: var(--amber-status); }

    .btn-check, .btn-clear {
      background: transparent;
      border: 1px solid var(--border-color);
      color: var(--text-main);
      padding: 4px 10px;
      border-radius: 2px;
      cursor: pointer;
      font-size: 11px;
    }
    .btn-check:hover, .btn-clear:hover {
      border-color: var(--text-muted);
      background: rgba(255, 255, 255, 0.01);
    }

    .tabs {
      display: flex;
      gap: 4px;
      margin-bottom: 12px;
      background: #020203;
      padding: 2px;
      border-radius: 4px;
      border: 1px solid var(--border-color);
      overflow-x: auto;
    }
    .tab {
      flex: 1;
      padding: 6px;
      border-radius: 2px;
      border: none;
      background: transparent;
      color: var(--text-muted);
      font-size: 11px;
      font-weight: 600;
      cursor: pointer;
      text-align: center;
      white-space: nowrap;
    }
    .tab:hover {
      color: var(--text-main);
    }
    .tab.active {
      background: rgba(255, 65, 34, 0.08);
      color: var(--primary-color);
      border: 1px solid rgba(255, 65, 34, 0.15);
    }
    .tab-content { display: none; }
    .tab-content.active {
      display: block;
    }

    .grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 6px;
      margin-bottom: 16px;
    }
    .attack-btn {
      padding: 10px;
      border-radius: 2px;
      border: 1px solid var(--border-color);
      background: transparent;
      color: var(--text-main);
      cursor: pointer;
      text-align: center;
      display: flex;
      flex-direction: column;
      align-items: center;
    }
    .attack-btn:hover {
      border-color: var(--primary-color);
    }
    .attack-btn.selected {
      border-color: var(--primary-color);
      background: rgba(255, 65, 34, 0.05);
    }
    .attack-btn .label {
      font-size: 11px;
      font-weight: 600;
    }
    .action-row {
      display: flex;
      gap: 6px;
      margin-bottom: 12px;
    }
    .action-row button {
      flex: 1;
      padding: 10px;
      border: 1px solid var(--border-color);
      border-radius: 4px;
      font-size: 11px;
      font-weight: 700;
      cursor: pointer;
      background: transparent;
      color: var(--text-main);
    }
    .btn-start {
      border-color: var(--primary-color) !important;
      color: var(--primary-color) !important;
    }
    .btn-start:hover {
      background: rgba(255, 65, 34, 0.05);
    }
    .btn-stop:hover {
      border-color: var(--text-muted);
    }
    .status {
      text-align: center;
      padding: 10px;
      background: #020203;
      border-radius: 4px;
      border: 1px solid var(--border-color);
      font-size: 11px;
      color: var(--text-muted);
      min-height: 36px;
      display: flex;
      justify-content: center;
      align-items: center;
      margin-bottom: 12px;
    }
    .console-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin: 12px 0 6px;
      font-size: 10px;
      color: var(--text-muted);
    }
    .console-box {
      background: #020203;
      border: 1px solid var(--border-color);
      border-radius: 4px;
      padding: 10px;
      font-size: 11px;
      line-height: 1.4;
      color: #34d399;
      height: 120px;
      overflow-y: auto;
      white-space: pre-wrap;
      text-align: left;
    }
    .footer {
      text-align: center;
      margin-top: 16px;
      font-size: 9px;
      color: var(--text-muted);
    }
    .footer a {
      color: var(--text-muted);
      text-decoration: none;
    }
    @media (max-width: 480px) {
      .grid { grid-template-columns: 1fr; }
    }
    .dashboard-section {
      background: #020203;
      border: 1px solid var(--border-color);
      border-radius: 4px;
      padding: 10px;
      margin-top: 12px;
      text-align: left;
    }
    .dashboard-box {
      margin-top: 6px;
      max-height: 160px;
      overflow-y: auto;
    }
    .scan-table {
      width: 100%;
      border-collapse: collapse;
      font-size: 11px;
    }
    .scan-table th, .scan-table td {
      padding: 4px 6px;
      text-align: left;
      border-bottom: 1px solid var(--border-color);
    }
    .scan-table th {
      color: var(--text-muted);
      font-weight: 600;
      font-size: 9px;
    }
    .client-card {
      background: transparent;
      border: 1px solid var(--border-color);
      border-radius: 2px;
      padding: 6px 10px;
      margin-bottom: 3px;
      display: flex;
      justify-content: space-between;
      align-items: center;
    }
    .client-mac {
      font-weight: 700;
      color: #34d399;
      font-size: 11px;
    }
    .client-status {
      font-size: 9px;
      padding: 1px 3px;
      border-radius: 2px;
      background: rgba(16, 185, 129, 0.05);
      color: #10b981;
      border: 1px solid rgba(16, 185, 129, 0.1);
    }

    /* Modal style */
    .modal {
      display: none; 
      position: fixed; 
      z-index: 1000; 
      left: 0;
      top: 0;
      width: 100%; 
      height: 100%; 
      background-color: rgba(0,0,0,0.85); 
      align-items: center;
      justify-content: center;
    }
    .modal-content {
      background-color: #0c0c0e;
      margin: auto;
      padding: 16px;
      border: 1px solid var(--primary-color);
      border-radius: 4px;
      width: 85%;
      max-width: 450px;
      color: #fca5a5;
      font-size: 11px;
      position: relative;
      text-align: left;
    }
    .close-btn {
      color: var(--text-muted);
      position: absolute;
      top: 4px;
      right: 10px;
      font-size: 18px;
      font-weight: bold;
      cursor: pointer;
    }
  </style>
</head>
<body>
<div class="container">
  <pre class="ascii-art">
 _____ ____  ____ _____ ___   ___  _     ____  
| ____/ ___||  _ \_   _/ _ \ / _ \| |   |___ \ 
|  _| \___ \| |_) || || | | | | | | |     __) |
| |___ ___) |  __/ | || |_| | |_| | |___ / __/ 
|_____|____/|_|    |_| \___/ \___/|_____|_____|</pre>
  <div class="sub">Multi-band Wireless Console</div>

  <div class="connection-status" id="conn-status">
    <div class="status-info">
      <span class="status-dot offline" id="status-dot"></span>
      <span id="status-text">Slave ESP32-S3: OFFLINE</span>
    </div>
    <button class="btn-check" onclick="checkConnection()">REFRESH</button>
  </div>

  <div class="tabs" id="tabs">
    <button class="tab active" data-tab="wifi">Wi-Fi</button>
    <button class="tab" data-tab="ble">Bluetooth</button>
    <button class="tab" data-tab="ghz">2.4GHz</button>
    <button class="tab" data-tab="subghz">Sub-GHz</button>
    <button class="tab" data-tab="ir">IR</button>
  </div>

  <div id="tab-wifi" class="tab-content active">
    <div class="grid">
      <button class="attack-btn selected" data-attack="beacon" onclick="selectAttack(this)"><span class="label">Beacon Spam</span></button>
      <button class="attack-btn" data-attack="deauth" onclick="selectAttack(this)"><span class="label">Deauth</span></button>
      <button class="attack-btn" data-attack="probe" onclick="selectAttack(this)"><span class="label">Probe Flood</span></button>
      <button class="attack-btn" data-attack="wifi_scan" onclick="selectAttack(this)"><span class="label">Wi-Fi Scan</span></button>
      <button class="attack-btn" data-attack="evil_twin" onclick="selectAttack(this)"><span class="label">Evil Twin</span></button>
      <button class="attack-btn" data-attack="powerful_jammer" onclick="selectAttack(this)"><span class="label">Powerful Jammer</span></button>
    </div>
  </div>

  <div id="tab-ble" class="tab-content">
    <div class="grid">
      <button class="attack-btn" data-attack="ble_scan" onclick="selectAttack(this)"><span class="label">BLE Scan</span></button>
      <button class="attack-btn" data-attack="ble_spoofer" onclick="selectAttack(this)"><span class="label">BLE Spoofer</span></button>
      <button class="attack-btn" data-attack="ble_jammer" onclick="selectAttack(this)"><span class="label">BLE Jammer</span></button>
      <button class="attack-btn" data-attack="sour_apple" onclick="selectAttack(this)"><span class="label">Sour Apple</span></button>
    </div>
  </div>

  <div id="tab-ghz" class="tab-content">
    <div class="grid">
      <button class="attack-btn" data-attack="ghz_scan" onclick="selectAttack(this)"><span class="label">2.4GHz Scanner</span></button>
      <button class="attack-btn" data-attack="protokill" onclick="selectAttack(this)"><span class="label">Protokill</span></button>
    </div>
  </div>

  <div id="tab-subghz" class="tab-content">
    <div class="grid">
      <button class="attack-btn" data-attack="subghz_replay" onclick="selectAttack(this)"><span class="label">Replay</span></button>
      <button class="attack-btn" data-attack="subghz_jammer" onclick="selectAttack(this)"><span class="label">Jammer</span></button>
    </div>
  </div>

  <div id="tab-ir" class="tab-content">
    <div class="grid">
      <button class="attack-btn" data-attack="ir_replay" onclick="selectAttack(this)"><span class="label">IR Replay</span></button>
    </div>
  </div>

  <div style="display: flex; align-items: center; justify-content: flex-start; gap: 8px; margin-bottom: 12px; font-size: 11px; padding-left: 2px;">
    <input type="checkbox" id="boost-mode" onchange="toggleBoostMode(this)" style="accent-color: var(--primary-color);">
    <label for="boost-mode" style="cursor: pointer; color: var(--text-muted);">Enable Boost Mode (Extreme Transmission)</label>
  </div>

  <div class="action-row">
    <button class="btn-start" onclick="sendCmd('start')">START</button>
    <button class="btn-stop" onclick="sendCmd('stop')">STOP</button>
  </div>

  <div class="status" id="status">STATUS: READY</div>

  <div class="dashboard-section" id="dashboard-section" style="display: none;">
    <div class="console-header" style="margin-top: 0;">
      <span id="dashboard-title">SCAN RESULTS</span>
    </div>
    <div class="dashboard-box" id="dashboard-box"></div>
  </div>

  <div class="console-header">
    <span>CONSOLE LOGS</span>
    <button class="btn-clear" onclick="clearLogs()">CLEAR</button>
  </div>
  <div class="console-box" id="console-box">Awaiting connection...</div>

  <div class="footer">ESPTOOL2 · <a href="#" onclick="location.reload()">Reload Console</a></div>
</div>

<div id="modal-container" class="modal">
  <div class="modal-content">
    <span class="close-btn" onclick="closeModal()">&times;</span>
    <div id="modal-text"></div>
  </div>
</div>

<script>
  let selectedAttack = 'beacon';
  let isBoostMode = false;
  const warnings = {
    ghz_scan: 'External NRF24L01 radio module is required for 2.4GHz Scanner!',
    protokill: 'External NRF24L01 radio module is required for Protokill!',
    subghz_replay: 'External CC1101 radio module is required for Sub-GHz Replay!',
    subghz_jammer: 'External CC1101 radio module is required for Sub-GHz Jammer!',
    ir_replay: 'Connected Infrared LED is required for IR Replay!'
  };

  const reportingAttacks = ['wifi_scan', 'ble_scan', 'evil_twin'];

  document.querySelectorAll('.tab').forEach(tab => {
    tab.addEventListener('click', function() {
      document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
      document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
      this.classList.add('active');
      document.getElementById('tab-' + this.dataset.tab).classList.add('active');
    });
  });

  function selectAttack(btn) {
    document.querySelectorAll('.attack-btn').forEach(b => b.classList.remove('selected'));
    btn.classList.add('selected');
    selectedAttack = btn.dataset.attack;
    document.getElementById('status').innerHTML = 'TARGET: ' + btn.querySelector('.label').textContent.toUpperCase();
    
    if (warnings[selectedAttack]) {
      showModal(warnings[selectedAttack]);
    }
    
    updateDashboardUI();
  }

  function toggleBoostMode(checkbox) {
    if (checkbox.checked) {
      if (confirm("WARNING: Boost Mode increases transmitter output and packet rate. This may cause high power consumption, board heating, or system crash. Proceed?")) {
        isBoostMode = true;
      } else {
        checkbox.checked = false;
        isBoostMode = false;
      }
    } else {
      isBoostMode = false;
    }
  }

  function showModal(text) {
    document.getElementById('modal-text').innerHTML = text;
    document.getElementById('modal-container').style.display = 'flex';
  }

  function closeModal() {
    document.getElementById('modal-container').style.display = 'none';
  }

  let wifiNetworks = [];
  let bleDevices = [];
  let evilTwinClients = [];
  let isClearing = false;

  function parseLogs(text) {
    const lines = text.split('\n');
    let wifiTemp = [];
    let bleTemp = [];
    let evilTemp = [];
    
    lines.forEach(line => {
      if (line.includes('[WIFI_DEV]')) {
        const parts = line.split('[WIFI_DEV]')[1].split('|');
        if (parts.length >= 2) {
          wifiTemp.push({ 
            ssid: parts[0], 
            rssi: parseInt(parts[1]), 
            enc: parts[2] || 'OPEN', 
            bssid: parts[3] || 'UNKNOWN' 
          });
        }
      }
      else if (line.includes('[BLE_DEV]')) {
        const parts = line.split('[BLE_DEV]')[1].split('|');
        if (parts.length >= 3) {
          bleTemp.push({ 
            name: parts[0], 
            addr: parts[1], 
            rssi: parseInt(parts[2]) 
          });
        } else if (parts.length === 2) {
          bleTemp.push({ 
            name: parts[0], 
            addr: 'UNKNOWN', 
            rssi: parseInt(parts[1]) 
          });
        }
      }
      else if (line.includes('[EVIL_CLIENT]')) {
        const mac = line.split('[EVIL_CLIENT]')[1].trim();
        if (mac && !evilTemp.includes(mac)) {
          evilTemp.push(mac);
        }
      }
    });

    wifiNetworks = wifiTemp;
    bleDevices = bleTemp;
    evilTwinClients = evilTemp;
    
    updateDashboardUI();
  }

  function updateDashboardUI() {
    const dashSection = document.getElementById('dashboard-section');
    const dashBox = document.getElementById('dashboard-box');
    const dashTitle = document.getElementById('dashboard-title');
    
    if (!reportingAttacks.includes(selectedAttack)) {
      dashSection.style.display = 'none';
      return;
    }

    dashSection.style.display = 'block';
    
    if (selectedAttack === 'wifi_scan') {
      dashTitle.innerHTML = 'WIFI NETWORKS DETECTED';
      if (wifiNetworks.length === 0) {
        dashBox.innerHTML = '<div style="color:var(--text-muted);font-size:11px;text-align:center;padding:12px;">AWAITING WIFI SCAN RESULTS...</div>';
      } else {
        let html = '<table class="scan-table"><thead><tr><th>SSID</th><th>BSSID</th><th>SIGNAL</th><th>SECURITY</th><th>LEVEL</th></tr></thead><tbody>';
        wifiNetworks.forEach(net => {
          let rssiClass = 'rssi-high';
          let levelText = 'STRONG';
          if (net.rssi < -80) {
            rssiClass = 'offline';
            levelText = 'WEAK';
          } else if (net.rssi < -67) {
            rssiClass = 'testing';
            levelText = 'MEDIUM';
          } else {
            rssiClass = 'online';
          }
          
          let secRating = 'SECURE';
          if (net.enc === 'OPEN') secRating = 'VULNERABLE';
          else if (net.enc === 'WEP') secRating = 'WEAK';
          
          html += '<tr>' +
                  '<td style="font-weight:600;color:#fff;">' + net.ssid + '</td>' +
                  '<td style="color:var(--text-muted);">' + net.bssid + '</td>' +
                  '<td>' + net.rssi + ' dBm</td>' +
                  '<td><span style="color:' + (secRating === 'VULNERABLE' ? 'var(--primary-color)' : 'var(--text-muted)') + ';">' + net.enc + ' (' + secRating + ')</span></td>' +
                  '<td><span class="status-dot ' + rssiClass + '"></span> ' + levelText + '</td>' +
                  '</tr>';
        });
        html += '</tbody></table>';
        dashBox.innerHTML = html;
      }
    } 
    else if (selectedAttack === 'ble_scan') {
      dashTitle.innerHTML = 'BLE DEVICES DETECTED';
      if (bleDevices.length === 0) {
        dashBox.innerHTML = '<div style="color:var(--text-muted);font-size:11px;text-align:center;padding:12px;">AWAITING BLE SCAN RESULTS...</div>';
      } else {
        let html = '<table class="scan-table"><thead><tr><th>DEVICE</th><th>MAC ADDRESS</th><th>SIGNAL</th><th>LEVEL</th></tr></thead><tbody>';
        bleDevices.forEach(dev => {
          let rssiClass = 'rssi-high';
          let levelText = 'STRONG';
          if (dev.rssi < -80) {
            rssiClass = 'offline';
            levelText = 'WEAK';
          } else if (dev.rssi < -67) {
            rssiClass = 'testing';
            levelText = 'MEDIUM';
          } else {
            rssiClass = 'online';
          }
          html += '<tr>' +
                  '<td style="font-weight:600;color:#fff;">' + dev.name + '</td>' +
                  '<td style="color:var(--text-muted);">' + dev.addr + '</td>' +
                  '<td>' + dev.rssi + ' dBm</td>' +
                  '<td><span class="status-dot ' + rssiClass + '"></span> ' + levelText + '</td>' +
                  '</tr>';
        });
        html += '</tbody></table>';
        dashBox.innerHTML = html;
      }
    }
    else if (selectedAttack === 'evil_twin') {
      dashTitle.innerHTML = 'EVIL TWIN CONNECTED CLIENTS';
      if (evilTwinClients.length === 0) {
        dashBox.innerHTML = '<div style="color:var(--text-muted);font-size:11px;text-align:center;padding:12px;">NO CLIENTS CONNECTED</div>';
      } else {
        let html = '<div style="display:grid;grid-template-columns:1fr;gap:4px;">';
        evilTwinClients.forEach(client => {
          html += '<div class="client-card"><span class="client-mac">' + client + '</span><span class="client-status">CONNECTED</span></div>';
        });
        html += '</div>';
        dashBox.innerHTML = html;
      }
    }
  }

  function sendCmd(cmd) {
    if (cmd === 'start') {
      wifiNetworks = [];
      bleDevices = [];
      evilTwinClients = [];
      updateDashboardUI();
    }
    const status = document.getElementById('status');
    status.innerHTML = 'PENDING: SENDING COMMAND TO SLAVE...';
    
    let attackParam = selectedAttack;
    if (cmd === 'start' && isBoostMode) {
      attackParam = selectedAttack + '_boost';
    }
    
    fetch('/cmd?q=' + cmd + '&attack=' + attackParam)
      .then(r => r.text())
      .then(data => { status.innerHTML = 'STATUS: ' + data.toUpperCase(); })
      .catch(() => { status.innerHTML = 'ERROR: MASTER ESP32 UNREACHABLE'; });
  }

  function checkConnection() {
    const dot = document.getElementById('status-dot');
    const text = document.getElementById('status-text');
    dot.className = 'status-dot testing';
    text.innerHTML = 'Slave ESP32-S3: PINGING...';
    
    fetch('/cmd?q=ping&attack=')
      .then(() => {
        setTimeout(() => {
          fetch('/status')
            .then(r => r.text())
            .then(data => {
              const parts = data.split('|');
              const status = parts[0].trim();
              const mac = parts[1] ? parts[1].trim() : '';
              
              if (status === 'online') {
                dot.className = 'status-dot online';
                text.innerHTML = 'Slave ESP32-S3: ONLINE' + (mac ? ' (' + mac + ')' : '');
              } else {
                dot.className = 'status-dot offline';
                text.innerHTML = 'Slave ESP32-S3: OFFLINE';
              }
            })
            .catch(() => {
              dot.className = 'status-dot offline';
              text.innerHTML = 'Slave ESP32-S3: OFFLINE';
            });
        }, 1200);
      })
      .catch(() => {
        dot.className = 'status-dot offline';
        text.innerHTML = 'Slave ESP32-S3: OFFLINE';
      });
  }

  function updateConsole() {
    if (isClearing) return;
    fetch('/logs')
      .then(r => r.text())
      .then(data => {
        if (isClearing) return;
        parseLogs(data);
        
        const filteredLogs = data.split('\n')
          .filter(line => !line.includes('[WIFI_DEV]') && !line.includes('[BLE_DEV]') && !line.includes('[EVIL_CLIENT]'))
          .join('\n');

        const consoleBox = document.getElementById('console-box');
        if (filteredLogs.trim() === '') {
          consoleBox.innerHTML = 'Logs are empty. Standby...';
        } else {
          consoleBox.innerHTML = filteredLogs;
          consoleBox.scrollTop = consoleBox.scrollHeight;
        }
      })
      .catch(err => {
        console.error("Console update error:", err);
      });
  }

  function clearLogs() {
    isClearing = true;
    document.getElementById('console-box').innerHTML = 'Clearing server logs...';
    wifiNetworks = [];
    bleDevices = [];
    evilTwinClients = [];
    updateDashboardUI();
    
    fetch('/clear_logs')
      .then(r => r.text())
      .then(() => {
        document.getElementById('console-box').innerHTML = 'Console cleared.';
        setTimeout(() => { isClearing = false; }, 600);
      })
      .catch(err => {
        document.getElementById('console-box').innerHTML = 'Error clearing logs.';
        console.error(err);
        isClearing = false;
      });
  }

  // Initial connection check
  setTimeout(checkConnection, 1000);
  
  // Log poll interval (1.5 seconds)
  setInterval(updateConsole, 1500);
</script>
</body>
</html>
)rawliteral";
