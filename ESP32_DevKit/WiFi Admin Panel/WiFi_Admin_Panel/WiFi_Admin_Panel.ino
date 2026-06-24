#include <WiFi.h>
#include <WebServer.h>
#include <esp_now.h>
#include "webpage.h"
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

// webpage is defined in webpage.h

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
  esp_now_register_send_cb((esp_now_send_cb_t)OnDataSent);
  esp_now_register_recv_cb((esp_now_recv_cb_t)OnDataRecv);
  
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
