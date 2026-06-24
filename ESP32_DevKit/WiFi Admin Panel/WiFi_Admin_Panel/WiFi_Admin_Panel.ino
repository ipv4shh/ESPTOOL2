#include <WiFi.h>
#include <WebServer.h>
#include <esp_now.h>
#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

// ===== НАСТРОЙКИ =====
const char* ap_ssid = "ESP-Admin";
const char* ap_password = "Esp_div_admin";
WebServer server(80);

// MAC-адрес SLAVE (Начинается с широковещательного FF:FF:FF:FF:FF:FF для автосопряжения)
uint8_t slaveMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
bool slavePaired = false;
unsigned long lastPongTime = 0;

// Накопитель логов
String attackLogs = "";

// ===== СТРУКТУРА ДЛЯ ESP-NOW =====
typedef struct struct_message {
  char command[16];
  char attack[32];
  char logMsg[64];
} struct_message;
struct_message myData;
esp_now_peer_info_t peerInfo;

void addLog(const String& logMsg) {
  // Добавляем лог в хронологическом порядке (новые снизу)
  String timeStr = "[" + String(millis() / 1000) + "s] ";
  attackLogs = attackLogs + timeStr + logMsg + "\n";
  // Ограничиваем размер буфера (последние ~2000 символов)
  if (attackLogs.length() > 2000) {
    attackLogs = attackLogs.substring(attackLogs.length() - 2000);
  }
}

// ===== КОЛБЭК ОТПРАВКИ ESP-NOW =====
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void OnDataSent(const esp_now_send_info_t *info, esp_now_send_status_t status) {
#else
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
#endif
  Serial.print("ESP-NOW статус отправки: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "✅ Успешно" : "❌ Ошибка");
}

// ===== ВЕБ-СТРАНИЦА (КОНСОЛЬ С АВТОПРОКРУТКОЙ) =====
String webpage = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESPTOOL2 | Консоль управления</title>
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700;800&family=JetBrains+Mono:wght@400;500;700&display=swap" rel="stylesheet">
  <style>
    :root {
      --primary-gradient: linear-gradient(135deg, #ff4122 0%, #ee3a17 100%);
      --accent-glow: rgba(238, 58, 23, 0.45);
      --bg-gradient: linear-gradient(135deg, #0a0b0d 0%, #050507 100%);
      --panel-bg: rgba(18, 20, 26, 0.75);
      --border-color: rgba(255, 255, 255, 0.06);
      --text-main: #f4f4f5;
      --text-muted: #8e8e96;
    }
    * { margin: 0; padding: 0; box-sizing: border-box; font-family: 'Inter', sans-serif; }
    body {
      background: var(--bg-gradient);
      color: var(--text-main);
      display: flex;
      justify-content: center;
      align-items: center;
      min-height: 100vh;
      padding: 16px;
      overflow-x: hidden;
    }
    .container {
      background: var(--panel-bg);
      backdrop-filter: blur(24px);
      -webkit-backdrop-filter: blur(24px);
      padding: 32px 28px;
      border-radius: 20px;
      max-width: 650px;
      width: 100%;
      border: 1px solid var(--border-color);
      box-shadow: 0 30px 60px rgba(0, 0, 0, 0.6), inset 0 1px 0 rgba(255, 255, 255, 0.05);
      position: relative;
      transition: all 0.3s ease;
    }
    .container::before {
      content: '';
      position: absolute;
      top: -2px; left: -2px; right: -2px; bottom: -2px;
      background: linear-gradient(135deg, rgba(238,58,23,0.1), rgba(255,255,255,0.01), rgba(238,58,23,0.05));
      border-radius: 22px;
      z-index: -1;
      pointer-events: none;
    }
    h1 {
      text-align: center;
      font-size: 28px;
      font-weight: 800;
      letter-spacing: -0.5px;
      margin-bottom: 4px;
    }
    h1 span {
      background: var(--primary-gradient);
      -webkit-background-clip: text;
      -webkit-text-fill-color: transparent;
      filter: drop-shadow(0 2px 8px rgba(238, 58, 23, 0.3));
    }
    .sub {
      text-align: center;
      color: var(--text-muted);
      font-size: 13px;
      margin-bottom: 28px;
      font-family: 'JetBrains Mono', monospace;
      letter-spacing: 1px;
      text-transform: uppercase;
    }
    .connection-status {
      display: flex;
      align-items: center;
      justify-content: space-between;
      background: rgba(255, 255, 255, 0.02);
      border: 1px solid rgba(255, 255, 255, 0.04);
      padding: 10px 18px;
      border-radius: 12px;
      margin-bottom: 24px;
      font-family: 'JetBrains Mono', monospace;
      font-size: 12px;
      box-shadow: inset 0 2px 4px rgba(0,0,0,0.2);
    }
    .status-info {
      display: flex;
      align-items: center;
      gap: 10px;
    }
    .status-dot {
      width: 8px;
      height: 8px;
      border-radius: 50%;
      display: inline-block;
      transition: all 0.3s ease;
    }
    .status-dot.offline { background: #ef4444; box-shadow: 0 0 10px #ef4444; }
    .status-dot.online { background: #10b981; box-shadow: 0 0 10px #10b981; }
    .status-dot.testing { background: #f59e0b; box-shadow: 0 0 10px #f59e0b; animation: pulse 1s infinite alternate; }
    
    @keyframes pulse {
      0% { transform: scale(1); opacity: 0.6; }
      100% { transform: scale(1.3); opacity: 1; }
    }

    .btn-check {
      background: rgba(255, 255, 255, 0.05);
      border: 1px solid rgba(255, 255, 255, 0.08);
      color: var(--text-main);
      padding: 6px 12px;
      border-radius: 8px;
      cursor: pointer;
      font-size: 11px;
      font-weight: 600;
      transition: all 0.2s ease;
      font-family: 'JetBrains Mono', monospace;
    }
    .btn-check:hover {
      background: rgba(255, 255, 255, 0.1);
      border-color: rgba(255, 255, 255, 0.15);
    }
    .btn-check:active {
      transform: scale(0.97);
    }

    .tabs {
      display: flex;
      gap: 6px;
      margin-bottom: 20px;
      background: rgba(0, 0, 0, 0.2);
      padding: 4px;
      border-radius: 12px;
      border: 1px solid rgba(255, 255, 255, 0.02);
    }
    .tab {
      flex: 1;
      padding: 10px;
      border-radius: 8px;
      border: none;
      background: transparent;
      color: var(--text-muted);
      font-weight: 600;
      font-size: 13px;
      cursor: pointer;
      transition: all 0.2s cubic-bezier(0.4, 0, 0.2, 1);
      font-family: 'JetBrains Mono', monospace;
      text-align: center;
    }
    .tab:hover {
      color: var(--text-main);
      background: rgba(255, 255, 255, 0.02);
    }
    .tab.active {
      background: rgba(238, 58, 23, 0.08);
      color: #ff5235;
      border: 1px solid rgba(238, 58, 23, 0.25);
      box-shadow: 0 4px 12px rgba(238, 58, 23, 0.05);
    }
    .tab-content { display: none; }
    .tab-content.active {
      display: block;
      animation: fadeIn 0.4s ease;
    }
    
    @keyframes fadeIn {
      from { opacity: 0; transform: translateY(6px); }
      to { opacity: 1; transform: translateY(0); }
    }

    .grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 10px;
      margin-bottom: 24px;
    }
    .attack-btn {
      padding: 16px 12px;
      border-radius: 12px;
      border: 1px solid rgba(255, 255, 255, 0.04);
      background: rgba(255, 255, 255, 0.01);
      color: var(--text-main);
      cursor: pointer;
      transition: all 0.2s cubic-bezier(0.4, 0, 0.2, 1);
      text-align: center;
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 6px;
    }
    .attack-btn:hover {
      border-color: rgba(238, 58, 23, 0.3);
      background: rgba(238, 58, 23, 0.03);
      transform: translateY(-2px);
      box-shadow: 0 8px 20px rgba(0, 0, 0, 0.3);
    }
    .attack-btn.selected {
      border-color: #ee3a17;
      background: rgba(238, 58, 23, 0.08);
      box-shadow: 0 0 15px rgba(238, 58, 23, 0.15), inset 0 1px 0 rgba(255, 255, 255, 0.05);
    }
    .attack-btn .icon {
      font-size: 24px;
      filter: drop-shadow(0 2px 4px rgba(0,0,0,0.5));
    }
    .attack-btn .label {
      font-size: 13px;
      font-weight: 600;
      font-family: 'JetBrains Mono', monospace;
    }
    .action-row {
      display: flex;
      gap: 12px;
      margin-bottom: 20px;
    }
    .action-row button {
      flex: 1;
      padding: 16px;
      border: none;
      border-radius: 12px;
      font-size: 15px;
      font-weight: 700;
      cursor: pointer;
      transition: all 0.2s cubic-bezier(0.4, 0, 0.2, 1);
      font-family: 'JetBrains Mono', monospace;
      display: flex;
      justify-content: center;
      align-items: center;
      gap: 8px;
    }
    .btn-start {
      background: var(--primary-gradient);
      color: #fff;
      box-shadow: 0 8px 24px -6px var(--accent-glow);
    }
    .btn-start:hover {
      background: linear-gradient(135deg, #ff5235 0%, #f04726 100%);
      transform: translateY(-2px);
      box-shadow: 0 12px 28px -4px var(--accent-glow);
    }
    .btn-start:active {
      transform: translateY(0);
    }
    .btn-stop {
      background: rgba(255, 255, 255, 0.04);
      color: var(--text-main);
      border: 1px solid rgba(255, 255, 255, 0.06);
    }
    .btn-stop:hover {
      background: rgba(255, 255, 255, 0.08);
      border-color: rgba(255, 255, 255, 0.1);
    }
    .status {
      text-align: center;
      padding: 16px;
      background: rgba(0, 0, 0, 0.2);
      border-radius: 12px;
      border: 1px solid rgba(255, 255, 255, 0.02);
      font-family: 'JetBrains Mono', monospace;
      font-size: 13px;
      color: var(--text-muted);
      min-height: 52px;
      display: flex;
      justify-content: center;
      align-items: center;
      box-shadow: inset 0 2px 8px rgba(0,0,0,0.3);
    }
    .console-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin: 20px 0 8px;
      font-family: 'JetBrains Mono', monospace;
      font-size: 11px;
      color: var(--text-muted);
    }
    .console-box {
      background: #040405;
      border: 1px solid rgba(255,255,255,0.03);
      border-radius: 12px;
      padding: 16px;
      font-family: 'JetBrains Mono', monospace;
      font-size: 11px;
      line-height: 1.6;
      color: #34d399;
      height: 160px;
      overflow-y: auto;
      white-space: pre-wrap;
      box-shadow: inset 0 4px 12px rgba(0,0,0,0.6);
      text-align: left;
    }
    .hw-warning {
      display: none;
      margin-top: 12px;
      padding: 14px;
      background: rgba(239, 68, 68, 0.08);
      border: 1px solid rgba(239, 68, 68, 0.25);
      border-radius: 12px;
      color: #fca5a5;
      font-family: 'JetBrains Mono', monospace;
      font-size: 12px;
      text-align: center;
      line-height: 1.5;
      box-shadow: 0 4px 12px rgba(0,0,0,0.15);
    }
    .footer {
      text-align: center;
      margin-top: 24px;
      font-size: 11px;
      color: #4b5563;
      font-family: 'JetBrains Mono', monospace;
    }
    .footer a {
      color: #4b5563;
      text-decoration: none;
      transition: color 0.2s ease;
    }
    .footer a:hover {
      color: var(--text-main);
    }
    @media (max-width: 480px) {
      .grid { grid-template-columns: 1fr; }
      .tabs { flex-direction: column; }
      .action-row { flex-direction: column; }
    }
    .dashboard-section {
      background: rgba(255, 255, 255, 0.02);
      border: 1px solid var(--border-color);
      border-radius: 12px;
      padding: 16px;
      margin-top: 20px;
      box-shadow: inset 0 2px 8px rgba(0,0,0,0.3);
    }
    .dashboard-box {
      margin-top: 10px;
      max-height: 240px;
      overflow-y: auto;
    }
    .scan-table {
      width: 100%;
      border-collapse: collapse;
      font-family: 'JetBrains Mono', monospace;
      font-size: 11px;
    }
    .scan-table th, .scan-table td {
      padding: 8px 10px;
      text-align: left;
      border-bottom: 1px solid rgba(255, 255, 255, 0.04);
    }
    .scan-table th {
      color: var(--text-muted);
      font-weight: 600;
      text-transform: uppercase;
      font-size: 9px;
      letter-spacing: 0.5px;
    }
    .rssi-bar-container {
      width: 60px;
      height: 6px;
      background: rgba(255, 255, 255, 0.05);
      border-radius: 3px;
      display: inline-block;
      vertical-align: middle;
      margin-right: 8px;
      overflow: hidden;
    }
    .rssi-bar {
      height: 100%;
      border-radius: 3px;
    }
    .rssi-high { background: #10b981; }
    .rssi-medium { background: #f59e0b; }
    .rssi-low { background: #ef4444; }
    .client-card {
      background: rgba(255, 255, 255, 0.02);
      border: 1px solid rgba(255, 255, 255, 0.04);
      border-radius: 8px;
      padding: 10px 14px;
      margin-bottom: 6px;
      display: flex;
      justify-content: space-between;
      align-items: center;
    }
    .client-mac {
      font-family: 'JetBrains Mono', monospace;
      font-weight: 700;
      color: #34d399;
      font-size: 12px;
    }
    .client-status {
      font-size: 10px;
      padding: 2px 6px;
      border-radius: 4px;
      background: rgba(16, 185, 129, 0.1);
      color: #10b981;
      border: 1px solid rgba(16, 185, 129, 0.2);
      font-family: 'JetBrains Mono', monospace;
    }
  </style>
</head>
<body>
<div class="container">
  <h1>⚡ ESP<span>TOOL2</span></h1>
  <div class="sub">Multi-band Wireless Console</div>

  <div class="connection-status" id="conn-status">
    <div class="status-info">
      <span class="status-dot offline" id="status-dot"></span>
      <span id="status-text">Slave ESP32-S3: Офлайн</span>
    </div>
    <button class="btn-check" onclick="checkConnection()">🔄 Проверить</button>
  </div>

  <div class="tabs" id="tabs">
    <button class="tab active" data-tab="wifi">📡 Wi-Fi</button>
    <button class="tab" data-tab="ble">🔵 Bluetooth</button>
    <button class="tab" data-tab="ghz">📶 2.4GHz</button>
    <button class="tab" data-tab="subghz">📻 Sub-GHz</button>
    <button class="tab" data-tab="ir">📺 IR</button>
  </div>

  <div id="tab-wifi" class="tab-content active">
    <div class="grid">
      <button class="attack-btn selected" data-attack="beacon" onclick="selectAttack(this)"><span class="icon">📡</span><span class="label">Beacon Spam</span></button>
      <button class="attack-btn" data-attack="deauth" onclick="selectAttack(this)"><span class="icon">🔓</span><span class="label">Deauth</span></button>
      <button class="attack-btn" data-attack="probe" onclick="selectAttack(this)"><span class="icon">📶</span><span class="label">Probe Flood</span></button>
      <button class="attack-btn" data-attack="wifi_scan" onclick="selectAttack(this)"><span class="icon">🔍</span><span class="label">Wi-Fi Scan</span></button>
      <button class="attack-btn" data-attack="evil_twin" onclick="selectAttack(this)"><span class="icon">🎭</span><span class="label">Evil Twin</span></button>
    </div>
  </div>

  <div id="tab-ble" class="tab-content">
    <div class="grid">
      <button class="attack-btn" data-attack="ble_scan" onclick="selectAttack(this)"><span class="icon">🔍</span><span class="label">BLE Scan</span></button>
      <button class="attack-btn" data-attack="ble_spoofer" onclick="selectAttack(this)"><span class="icon">🔄</span><span class="label">BLE Spoofer</span></button>
      <button class="attack-btn" data-attack="ble_jammer" onclick="selectAttack(this)"><span class="icon">🔵</span><span class="label">BLE Jammer</span></button>
      <button class="attack-btn" data-attack="sour_apple" onclick="selectAttack(this)"><span class="icon">🍏</span><span class="label">Sour Apple</span></button>
    </div>
  </div>

  <div id="tab-ghz" class="tab-content">
    <div class="grid">
      <button class="attack-btn" data-attack="ghz_scan" onclick="selectAttack(this)"><span class="icon">📶</span><span class="label">2.4GHz Scanner</span></button>
      <button class="attack-btn" data-attack="protokill" onclick="selectAttack(this)"><span class="icon">⚡</span><span class="label">Protokill</span></button>
    </div>
  </div>

  <div id="tab-subghz" class="tab-content">
    <div class="grid">
      <button class="attack-btn" data-attack="subghz_replay" onclick="selectAttack(this)"><span class="icon">📻</span><span class="label">Replay</span></button>
      <button class="attack-btn" data-attack="subghz_jammer" onclick="selectAttack(this)"><span class="icon">📻</span><span class="label">Jammer</span></button>
    </div>
  </div>

  <div id="tab-ir" class="tab-content">
    <div class="grid">
      <button class="attack-btn" data-attack="ir_replay" onclick="selectAttack(this)"><span class="icon">📺</span><span class="label">IR Replay</span></button>
    </div>
  </div>

  <div class="action-row">
    <button class="btn-start" onclick="sendCmd('start')">▶️ Запустить</button>
    <button class="btn-stop" onclick="sendCmd('stop')">⏹️ Остановить</button>
  </div>

  <div class="status" id="status">🟢 Готов к работе</div>

  <div class="console-header">
    <span>📋 КОНСОЛЬ ОТЧЕТОВ И ЛОГОВ АТАК</span>
    <button class="btn-check" onclick="clearLogs()">Очистить</button>
  </div>
  <div class="console-box" id="console-box">Ожидание подключения Slave-платы...</div>

  <div class="dashboard-section" id="dashboard-section" style="display: none;">
    <div class="console-header" style="margin-top: 0;">
      <span id="dashboard-title">📊 РЕЗУЛЬТАТЫ СКАНИРОВАНИЯ</span>
    </div>
    <div class="dashboard-box" id="dashboard-box"></div>
  </div>

  <div class="hw-warning" id="hw-warning"></div>
  <div class="footer">ESPTOOL2 · <a href="#" onclick="location.reload()">⟳ обновить консоль</a></div>
</div>

<script>
  let selectedAttack = 'beacon';
  const warnings = {
    ghz_scan: '⚠️ Требуется внешний радиомодуль NRF24L01 для работы 2.4GHz Scanner!',
    protokill: '⚠️ Требуется внешний радиомодуль NRF24L01 для работы Protokill!',
    subghz_replay: '⚠️ Требуется внешний радиомодуль CC1101 для работы Sub-GHz Replay!',
    subghz_jammer: '⚠️ Требуется внешний радиомодуль CC1101 для работы Sub-GHz Jammer!',
    ir_replay: '⚠️ Требуется подключенный инфракрасный диод для работы IR Replay!'
  };

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
    document.getElementById('status').innerHTML = '🎯 Целевая атака: ' + btn.querySelector('.label').textContent;
    
    const warningDiv = document.getElementById('hw-warning');
    if (warnings[selectedAttack]) {
      warningDiv.innerHTML = warnings[selectedAttack];
      warningDiv.style.display = 'block';
    } else {
      warningDiv.style.display = 'none';
    }
  }

  let wifiNetworks = [];
  let bleDevices = [];
  let evilTwinClients = [];

  function parseLogs(text) {
    const lines = text.split('\n');
    let wifiTemp = [];
    let bleTemp = [];
    let evilTemp = [];
    
    lines.forEach(line => {
      if (line.includes('[WIFI_DEV]')) {
        const parts = line.split('[WIFI_DEV]')[1].split('|');
        if (parts.length >= 2) {
          wifiTemp.push({ ssid: parts[0], rssi: parseInt(parts[1]), enc: parts[2] || 'OPEN' });
        }
      }
      else if (line.includes('[BLE_DEV]')) {
        const parts = line.split('[BLE_DEV]')[1].split('|');
        if (parts.length >= 2) {
          bleTemp.push({ name: parts[0], rssi: parseInt(parts[1]) });
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
    
    if (wifiNetworks.length > 0) {
      dashSection.style.display = 'block';
      dashTitle.innerHTML = '📊 НАЙДЕННЫЕ WI-FI СЕТИ (' + wifiNetworks.length + ')';
      
      let html = '<table class="scan-table"><thead><tr><th>SSID</th><th>Сигнал</th><th>Защита</th></tr></thead><tbody>';
      wifiNetworks.forEach(net => {
        let rssiPercent = Math.min(Math.max(2 * (net.rssi + 100), 0), 100);
        let rssiClass = 'rssi-high';
        if (net.rssi < -80) rssiClass = 'rssi-low';
        else if (net.rssi < -67) rssiClass = 'rssi-medium';
        
        html += '<tr><td style="font-weight:600;color:#fff;">' + net.ssid + '</td><td><div class="rssi-bar-container"><div class="rssi-bar ' + rssiClass + '" style="width:' + rssiPercent + '%;"></div></div><span>' + net.rssi + ' dBm</span></td><td><span style="color:#ee3a17;font-weight:600;">' + net.enc + '</span></td></tr>';
      });
      html += '</tbody></table>';
      dashBox.innerHTML = html;
    } 
    else if (bleDevices.length > 0) {
      dashSection.style.display = 'block';
      dashTitle.innerHTML = '📊 БЛИЖАЙШИЕ BLE УСТРОЙСТВА (' + bleDevices.length + ')';
      
      let html = '<table class="scan-table"><thead><tr><th>Устройство / MAC</th><th>Сигнал (RSSI)</th></tr></thead><tbody>';
      bleDevices.forEach(dev => {
        let rssiPercent = Math.min(Math.max(2 * (dev.rssi + 100), 0), 100);
        let rssiClass = 'rssi-high';
        if (dev.rssi < -80) rssiClass = 'rssi-low';
        else if (dev.rssi < -67) rssiClass = 'rssi-medium';
        
        html += '<tr><td style="font-weight:600;color:#fff;">' + dev.name + '</td><td><div class="rssi-bar-container"><div class="rssi-bar ' + rssiClass + '" style="width:' + rssiPercent + '%;"></div></div><span>' + dev.rssi + ' dBm</span></td></tr>';
      });
      html += '</tbody></table>';
      dashBox.innerHTML = html;
    }
    else if (evilTwinClients.length > 0) {
      dashSection.style.display = 'block';
      dashTitle.innerHTML = '👥 ПОДКЛЮЧЕННЫЕ КЛИЕНТЫ EVIL TWIN (' + evilTwinClients.length + ')';
      
      let html = '<div style="display:grid;grid-template-columns:1fr;gap:6px;">';
      evilTwinClients.forEach(client => {
        html += '<div class="client-card"><span class="client-mac">' + client + '</span><span class="client-status">Подключен</span></div>';
      });
      html += '</div>';
      dashBox.innerHTML = html;
    }
    else {
      dashSection.style.display = 'none';
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
    status.innerHTML = '⏳ Отправка команды на Slave...';
    fetch('/cmd?q=' + cmd + '&attack=' + selectedAttack)
      .then(r => r.text())
      .then(data => { status.innerHTML = '✅ ' + data; })
      .catch(() => { status.innerHTML = '❌ Ошибка связи с Master ESP32'; });
  }

  function checkConnection() {
    const dot = document.getElementById('status-dot');
    const text = document.getElementById('status-text');
    dot.className = 'status-dot testing';
    text.innerHTML = 'Slave ESP32-S3: Запрос пинга...';
    
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
                text.innerHTML = 'Slave ESP32-S3: Онлайн' + (mac ? ' (' + mac + ')' : '');
              } else {
                dot.className = 'status-dot offline';
                text.innerHTML = 'Slave ESP32-S3: Офлайн';
              }
            })
            .catch(() => {
              dot.className = 'status-dot offline';
              text.innerHTML = 'Slave ESP32-S3: Офлайн';
            });
        }, 1200);
      })
      .catch(() => {
        dot.className = 'status-dot offline';
        text.innerHTML = 'Slave ESP32-S3: Офлайн';
      });
  }

  function updateConsole() {
    fetch('/logs')
      .then(r => r.text())
      .then(data => {
        parseLogs(data);
        
        const filteredLogs = data.split('\n')
          .filter(line => !line.includes('[WIFI_DEV]') && !line.includes('[BLE_DEV]') && !line.includes('[EVIL_CLIENT]'))
          .join('\n');

        const consoleBox = document.getElementById('console-box');
        if (filteredLogs.trim() === '') {
          consoleBox.innerHTML = 'Логи отсутствуют. Ожидание атак...';
        } else {
          consoleBox.innerHTML = filteredLogs;
          consoleBox.scrollTop = consoleBox.scrollHeight;
        }
      });
  }

  function clearLogs() {
    fetch('/clear_logs')
      .then(() => {
        document.getElementById('console-box').innerHTML = 'Лог консоли очищен.';
        wifiNetworks = [];
        bleDevices = [];
        evilTwinClients = [];
        updateDashboardUI();
      });
  }

  // Первая автопроверка
  setTimeout(checkConnection, 1000);
  
  // Постоянный опрос логов каждые 1.5 секунды
  setInterval(updateConsole, 1500);
</script>
</body>
</html>
)rawliteral";

// ===== ОБРАБОТЧИК ПРИЁМА ESP-NOW =====
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  const uint8_t *srcMac = info->src_addr;
#else
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
  const uint8_t *srcMac = mac_addr;
#endif
  if (len == sizeof(myData)) {
    struct_message incoming;
    memcpy(&incoming, incomingData, sizeof(incoming));
    incoming.command[15] = '\0';
    incoming.attack[31] = '\0';
    incoming.logMsg[63] = '\0';
    
    // Автосопряжение: при получении любого пакета от Slave переходим на Unicast
    if (!slavePaired || memcmp(slaveMac, srcMac, 6) != 0) {
      Serial.printf("🤝 Обнаружен Slave MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", 
                    srcMac[0], srcMac[1], srcMac[2], srcMac[3], srcMac[4], srcMac[5]);
      
      // Удаляем старый peer (если был)
      esp_now_del_peer(slaveMac);
      
      // Запоминаем новый MAC
      memcpy(slaveMac, srcMac, 6);
      
      memset(&peerInfo, 0, sizeof(peerInfo));
      memcpy(peerInfo.peer_addr, slaveMac, 6);
      peerInfo.channel = 1;
      peerInfo.encrypt = false;
      peerInfo.ifidx = WIFI_IF_AP;
      
      if (esp_now_add_peer(&peerInfo) == ESP_OK) {
        slavePaired = true;
        Serial.println("✅ Перешли на Unicast с обратным ACK-подтверждением!");
      }
    }

    if (strcmp(incoming.command, "pong") == 0) {
      lastPongTime = millis();
      Serial.println("📩 Получен PONG от SLAVE");
    } 
    else if (strcmp(incoming.command, "log") == 0) {
      addLog(incoming.logMsg);
      Serial.printf("📋 LOG от Slave: %s\n", incoming.logMsg);
    }
  }
}

void setup() {
  Serial.begin(115200);
  
  // Точка доступа на канале 1
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password, 1);
  WiFi.setSleep(false);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());

  // Инициализация ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW ошибка");
    return;
  }
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);
  
  // Добавляем широковещательный peer для первого соединения
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, slaveMac, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_AP;
  esp_now_add_peer(&peerInfo);

  // Маршруты
  server.on("/", []() {
    server.send(200, "text/html", webpage);
  });

  server.on("/logs", []() {
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
    server.send(200, "text/plain", attackLogs);
  });

  server.on("/clear_logs", []() {
    attackLogs = "";
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
    server.send(200, "text/plain", "Cleared");
  });

  server.on("/status", []() {
    String statusStr = "";
    if (lastPongTime > 0 && (millis() - lastPongTime < 5000)) {
      statusStr += "online";
    } else {
      statusStr += "offline";
    }
    if (slavePaired) {
      char macStr[20];
      sprintf(macStr, "|%02X:%02X:%02X:%02X:%02X:%02X", 
              slaveMac[0], slaveMac[1], slaveMac[2], slaveMac[3], slaveMac[4], slaveMac[5]);
      statusStr += macStr;
    }
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
    server.send(200, "text/plain", statusStr);
  });

  server.on("/cmd", []() {
    String cmd = server.arg("q");
    String attack = server.arg("attack");
    strncpy(myData.command, cmd.c_str(), sizeof(myData.command) - 1);
    myData.command[sizeof(myData.command) - 1] = '\0';
    strncpy(myData.attack, attack.c_str(), sizeof(myData.attack) - 1);
    myData.attack[sizeof(myData.attack) - 1] = '\0';
    memset(myData.logMsg, 0, sizeof(myData.logMsg));
    
    // Отправляем
    esp_now_send(slaveMac, (uint8_t *) &myData, sizeof(myData));

    String response = "Команда: " + cmd + " | Атака: " + attack;
    if (cmd == "start") {
      response = "▶️ Атака " + attack + " запущена!";
      addLog("Запуск атаки: " + attack);
    }
    else if (cmd == "stop") {
      response = "⏹️ Атака " + attack + " остановлена!";
      addLog("Команда STOP отправлена на Slave");
    }
    else if (cmd == "ping") response = "Отправлен PING!";
    
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
    server.send(200, "text/plain", response);
  });

  server.begin();
  Serial.println("Сервер запущен!");
}

void loop() {
  server.handleClient();
}
