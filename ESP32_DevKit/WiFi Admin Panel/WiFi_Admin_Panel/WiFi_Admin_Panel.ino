#include <WiFi.h>
#include <WebServer.h>
#include <esp_now.h>

// ===== НАСТРОЙКИ =====
const char* ap_ssid = "ESP-Admin";
const char* ap_password = "Esp_div_admin";
WebServer server(80);

// MAC-адрес SLAVE (Используем широковещательный адрес FF:FF:FF:FF:FF:FF для автосопряжения)
uint8_t slaveMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ===== СТРУКТУРА ДЛЯ ESP-NOW =====
typedef struct struct_message {
  char command[16];
  char attack[16];
} struct_message;
struct_message myData;
esp_now_peer_info_t peerInfo;

// ===== КОЛБЭК ESP-NOW =====
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void OnDataSent(const esp_now_send_info_t *info, esp_now_send_status_t status) {
#else
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
#endif
  Serial.print("ESP-NOW статус: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "✅ Успешно" : "❌ Ошибка");
}

// ===== ВЕБ-СТРАНИЦА =====
String webpage = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESPTOOL2</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; font-family: 'Segoe UI', monospace; }
    body { background: #0a0a0b; color: #f4f4f5; display: flex; justify-content: center; align-items: center; min-height: 100vh; padding: 16px; }
    .container { background: #101012; padding: 28px 22px; border-radius: 16px; max-width: 700px; width: 100%; border: 1px solid rgba(255,255,255,0.06); }
    h1 { text-align: center; font-size: 26px; font-weight: 800; }
    h1 span { color: #ee3a17; }
    .sub { text-align: center; color: #8e8e96; font-size: 13px; margin-bottom: 24px; font-family: monospace; }
    .tabs { display: flex; gap: 6px; margin-bottom: 18px; flex-wrap: wrap; }
    .tab { padding: 8px 16px; border-radius: 10px; border: 1px solid rgba(255,255,255,0.06); background: transparent; color: #8e8e96; font-weight: 600; font-size: 14px; cursor: pointer; transition: 0.2s; font-family: monospace; }
    .tab:hover { border-color: rgba(238,58,23,0.4); color: #fff; }
    .tab.active { border-color: #ee3a17; background: rgba(238,58,23,0.1); color: #fff; }
    .tab-content { display: none; }
    .tab-content.active { display: block; }
    .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; margin-bottom: 20px; }
    .attack-btn { padding: 12px 10px; border-radius: 10px; border: 1px solid rgba(255,255,255,0.06); background: rgba(255,255,255,0.02); color: #f4f4f5; font-size: 13px; font-weight: 600; cursor: pointer; transition: 0.2s; text-align: center; font-family: monospace; }
    .attack-btn:hover { border-color: #ee3a17; background: rgba(238,58,23,0.06); transform: translateY(-2px); }
    .attack-btn .icon { font-size: 22px; display: block; margin-bottom: 2px; }
    .attack-btn .label { font-size: 11px; color: #8e8e96; }
    .attack-btn img { width: 28px; height: 28px; display: none; margin: 0 auto 4px; }
    .action-row { display: flex; gap: 10px; margin: 16px 0 14px; }
    .action-row button { flex: 1; padding: 14px; border: none; border-radius: 10px; font-size: 16px; font-weight: 700; cursor: pointer; transition: 0.2s; font-family: monospace; }
    .btn-start { background: #ee3a17; color: #fff; box-shadow: 0 6px 22px -8px rgba(238,58,23,0.6); }
    .btn-start:hover { background: #ff4d22; transform: translateY(-2px); }
    .btn-stop { background: rgba(255,255,255,0.06); color: #f4f4f5; border: 1px solid rgba(255,255,255,0.08); }
    .btn-stop:hover { background: rgba(255,255,255,0.1); }
    .status { text-align: center; padding: 12px; background: rgba(255,255,255,0.02); border-radius: 10px; border: 1px solid rgba(255,255,255,0.04); font-family: monospace; font-size: 13px; color: #8e8e96; min-height: 44px; }
    .footer { text-align: center; margin-top: 18px; font-size: 11px; color: #444; font-family: monospace; }
    .connection-status { display: flex; align-items: center; justify-content: center; gap: 10px; background: rgba(255, 255, 255, 0.02); border: 1px solid rgba(255, 255, 255, 0.05); padding: 8px 14px; border-radius: 10px; margin-bottom: 20px; font-family: monospace; font-size: 12px; }
    .status-dot { width: 8px; height: 8px; border-radius: 50%; display: inline-block; }
    .status-dot.offline { background: #ee3a17; box-shadow: 0 0 8px #ee3a17; }
    .status-dot.online { background: #10b981; box-shadow: 0 0 8px #10b981; }
    .status-dot.testing { background: #fbbf24; box-shadow: 0 0 8px #fbbf24; }
    .btn-check { background: rgba(255, 255, 255, 0.06); border: none; color: #8e8e96; padding: 4px 8px; border-radius: 6px; cursor: pointer; font-size: 11px; transition: 0.2s; font-family: monospace; }
    .btn-check:hover { background: rgba(255, 255, 255, 0.1); color: #fff; }
    .hw-warning { display: none; margin-top: 10px; padding: 12px; background: rgba(238,58,23,0.08); border: 1px solid rgba(238,58,23,0.3); border-radius: 10px; color: #ff6040; font-family: monospace; font-size: 12px; text-align: center; line-height: 1.4; }
    @media (max-width: 480px) { .grid { grid-template-columns: 1fr 1fr; gap: 6px; } .attack-btn { padding: 8px 6px; font-size: 12px; } .action-row { flex-direction: column; } }
  </style>
</head>
<body>
<div class="container">
  <h1>⚡ ESP<span>TOOL2</span></h1>
  <div class="sub">Multi-band Wireless Toolkit</div>

  <div class="connection-status" id="conn-status">
    <span class="status-dot offline" id="status-dot"></span>
    <span id="status-text">Slave ESP32-S3: Офлайн</span>
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
      <button class="attack-btn" data-attack="beacon"><span class="icon">📡</span><span class="label">Beacon Spam</span></button>
      <button class="attack-btn" data-attack="deauth"><span class="icon">🔓</span><span class="label">Deauth</span></button>
      <button class="attack-btn" data-attack="probe"><span class="icon">📶</span><span class="label">Probe Flood</span></button>
      <button class="attack-btn" data-attack="wifi_scan"><span class="icon">🔍</span><span class="label">Wi-Fi Scan</span></button>
      <button class="attack-btn" data-attack="evil_twin"><span class="icon">🎭</span><span class="label">Evil Twin</span></button>
    </div>
  </div>

  <div id="tab-ble" class="tab-content">
    <div class="grid">
      <button class="attack-btn" data-attack="ble_scan"><span class="icon">🔍</span><span class="label">BLE Scan</span></button>
      <button class="attack-btn" data-attack="ble_spoofer"><span class="icon">🔄</span><span class="label">BLE Spoofer</span></button>
      <button class="attack-btn" data-attack="ble_jammer"><span class="icon">🔵</span><span class="label">BLE Jammer</span></button>
      <button class="attack-btn" data-attack="sour_apple"><span class="icon">🍏</span><span class="label">Sour Apple</span></button>
    </div>
  </div>

  <div id="tab-ghz" class="tab-content">
    <div class="grid">
      <button class="attack-btn" data-attack="ghz_scan"><span class="icon">📶</span><span class="label">2.4GHz Scanner</span></button>
      <button class="attack-btn" data-attack="protokill"><span class="icon">⚡</span><span class="label">Protokill</span></button>
    </div>
  </div>

  <div id="tab-subghz" class="tab-content">
    <div class="grid">
      <button class="attack-btn" data-attack="subghz_replay"><span class="icon">📻</span><span class="label">Replay</span></button>
      <button class="attack-btn" data-attack="subghz_jammer"><span class="icon">📻</span><span class="label">Jammer</span></button>
    </div>
  </div>

  <div id="tab-ir" class="tab-content">
    <div class="grid">
      <button class="attack-btn" data-attack="ir_replay"><span class="icon">📺</span><span class="label">IR Replay</span></button>
    </div>
  </div>

  <div class="action-row">
    <button class="btn-start" onclick="sendCmd('start')">▶️ Запустить</button>
    <button class="btn-stop" onclick="sendCmd('stop')">⏹️ Остановить</button>
  </div>

  <div class="status" id="status">🟢 Готов к работе</div>
  <div class="hw-warning" id="hw-warning"></div>
  <div class="footer">ESPTOOL2 · <a href="#" onclick="location.reload()">⟳ обновить</a></div>
</div>

<script>
  let selectedAttack = 'beacon';
  const warnings = {
    ghz_scan: '⚠️ Для работы "2.4GHz Scanner" требуется подключить модуль NRF24L01!',
    protokill: '⚠️ Для работы "Protokill" требуется подключить модуль NRF24L01!',
    subghz_replay: '⚠️ Для работы "Sub-GHz Replay" требуется подключить модуль CC1101!',
    subghz_jammer: '⚠️ Для работы "Sub-GHz Jammer" требуется подключить модуль CC1101!',
    ir_replay: '⚠️ Для работы "IR Replay" требуется подключить внешний инфракрасный светодиод!'
  };

  document.querySelectorAll('.tab').forEach(tab => {
    tab.addEventListener('click', function() {
      document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
      document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
      this.classList.add('active');
      document.getElementById('tab-' + this.dataset.tab).classList.add('active');
    });
  });

  document.querySelectorAll('.attack-btn').forEach(btn => {
    btn.addEventListener('click', function() {
      selectedAttack = this.dataset.attack;
      document.getElementById('status').innerHTML = '🎯 Выбрано: ' + this.querySelector('.label').textContent;
      
      const warningDiv = document.getElementById('hw-warning');
      if (warnings[selectedAttack]) {
        warningDiv.innerHTML = warnings[selectedAttack];
        warningDiv.style.display = 'block';
      } else {
        warningDiv.style.display = 'none';
      }
    });
  });

  function sendCmd(cmd) {
    const status = document.getElementById('status');
    status.innerHTML = '⏳ Отправка...';
    fetch('/cmd?q=' + cmd + '&attack=' + selectedAttack)
      .then(r => r.text())
      .then(data => { status.innerHTML = '✅ ' + data; })
      .catch(() => { status.innerHTML = '❌ Ошибка связи'; });
  }

  function checkConnection() {
    const dot = document.getElementById('status-dot');
    const text = document.getElementById('status-text');
    dot.className = 'status-dot testing';
    text.innerHTML = 'Slave ESP32-S3: Проверка...';
    
    fetch('/cmd?q=ping&attack=')
      .then(() => {
        setTimeout(() => {
          fetch('/status')
            .then(r => r.text())
            .then(status => {
              if (status.trim() === 'online') {
                dot.className = 'status-dot online';
                text.innerHTML = 'Slave ESP32-S3: Онлайн';
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

  // Запуск проверки при загрузке страницы
  setTimeout(checkConnection, 1000);
</script>
</body>
</html>
)rawliteral";

unsigned long lastPongTime = 0;

// ===== ОБРАБОТЧИК ПРИЁМА ESP-NOW =====
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
#else
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
#endif
  if (len == sizeof(myData)) {
    struct_message incoming;
    memcpy(&incoming, incomingData, sizeof(incoming));
    incoming.command[15] = '\0';
    incoming.attack[15] = '\0';
    if (strcmp(incoming.command, "pong") == 0) {
      lastPongTime = millis();
      Serial.println("📩 Получен PONG от SLAVE");
    }
  }
}

void setup() {
  Serial.begin(115200);
  
  // Инициализация Wi-Fi AP на канале 1
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password, 1);
  WiFi.setSleep(false); // Отключаем спящий режим для надежности
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());

  // ESP-NOW инициализация
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW ошибка");
    return;
  }
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);
  
  memcpy(peerInfo.peer_addr, slaveMac, 6);
  peerInfo.channel = 1; // Указываем канал 1
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_AP; // Явно указываем AP-интерфейс для отправки с Master
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Ошибка добавления SLAVE");
    return;
  }

  // Веб-сервер
  server.on("/", []() {
    server.send(200, "text/html", webpage);
  });

  server.on("/status", []() {
    if (lastPongTime > 0 && (millis() - lastPongTime < 5000)) {
      server.send(200, "text/plain", "online");
    } else {
      server.send(200, "text/plain", "offline");
    }
  });

  server.on("/cmd", []() {
    String cmd = server.arg("q");
    String attack = server.arg("attack");
    strncpy(myData.command, cmd.c_str(), sizeof(myData.command) - 1);
    myData.command[sizeof(myData.command) - 1] = '\0';
    strncpy(myData.attack, attack.c_str(), sizeof(myData.attack) - 1);
    myData.attack[sizeof(myData.attack) - 1] = '\0';
    esp_now_send(slaveMac, (uint8_t *) &myData, sizeof(myData));

    String response = "Команда: " + cmd + " | Атака: " + attack;
    if (cmd == "start") response = "▶️ Атака " + attack + " запущена!";
    else if (cmd == "stop") response = "⏹️ Атака " + attack + " остановлена!";
    else if (cmd == "ping") response = "Отправлен PING!";
    server.send(200, "text/plain", response);
  });

  server.begin();
  Serial.println("Сервер запущен! Подключись к Wi-Fi 'ESP-Admin' и открой 192.168.4.1");
}

void loop() {
  server.handleClient();
}
