#include <WiFi.h>
#include <WebServer.h>
#include <esp_now.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include "webpage.h"
#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif
#include <mbedtls/aes.h>

// Forward declaration for Arduino IDE auto-prototype generation
typedef struct struct_message struct_message;

// ===== AES-128 ENCRYPTION (replaces XOR) =====
static const uint8_t AES_KEY[16] = {
  0x4D, 0x6F, 0x62, 0x69, 0x6C, 0x65, 0x54, 0x6F,
  0x6F, 0x6C, 0x53, 0x65, 0x63, 0x75, 0x72, 0x65
};

void encrypt_aes_blocks(uint8_t* data, size_t len) {
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, AES_KEY, 128);
  for (size_t i = 0; i + 16 <= len; i += 16)
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, data + i, data + i);
  mbedtls_aes_free(&aes);
}

void decrypt_aes_blocks(uint8_t* data, size_t len) {
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_dec(&aes, AES_KEY, 128);
  for (size_t i = 0; i + 16 <= len; i += 16)
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, data + i, data + i);
  mbedtls_aes_free(&aes);
}

// ===== CONSTANTS =====
#define WDT_TIMEOUT_SECONDS 30
#define STOP_INTERVAL_MS 80
#define MAX_STOP_ATTEMPTS 25

// ===== SETTINGS =====
const char* ap_ssid = "ESP-Admin";
const char* ap_password = "Esp_div_admin";
const char* adminUser = "admin";
const char* adminPass = "Esp_div_admin";
WebServer server(80);

// SLAVE MAC Address
uint8_t slaveMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
bool slavePaired = false;
unsigned long lastPongTime = 0;
String slaveTempStr = "44.8";

// Non-blocking background STOP variables
bool isStopping = false;
unsigned long lastStopSendTime = 0;
int stopSendCount = 0;

// Periodic heartbeat ping to the Slave (keeps online status & temperature fresh)
unsigned long lastPingSendTime = 0;
#define PING_INTERVAL 2000

// Circular Log Buffer
#define MAX_LOG_MESSAGES 200
String logBuffer[MAX_LOG_MESSAGES];
int logHead = 0;
int logCount = 0;
SemaphoreHandle_t logMutex = NULL;

// ===== ESP-NOW STRUCTURE =====
typedef struct struct_message {
  char command[16];
  char attack[32];
  char logMsg[180];
} struct_message;
struct_message myData;
struct_message stopData = {0};
esp_now_peer_info_t peerInfo;

void addLog(const String& logMsg) {
  if (logMutex && xSemaphoreTake(logMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    char timeBuf[20];
    snprintf(timeBuf, sizeof(timeBuf), "[%lus] ", millis() / 1000);
    logBuffer[logHead] = String(timeBuf) + logMsg;
    logHead = (logHead + 1) % MAX_LOG_MESSAGES;
    if (logCount < MAX_LOG_MESSAGES) logCount++;
    xSemaphoreGive(logMutex);
  }
}

String getLogs() {
  String allLogs = "";
  if (logMutex && xSemaphoreTake(logMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    int startIndex = (logCount == MAX_LOG_MESSAGES) ? logHead : 0;
    for (int i = 0; i < logCount; i++) {
      int index = (startIndex + i) % MAX_LOG_MESSAGES;
      allLogs += logBuffer[index] + "\n";
    }
    xSemaphoreGive(logMutex);
  }
  return allLogs;
}

void clearLogs() {
  if (logMutex && xSemaphoreTake(logMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    logHead = 0;
    logCount = 0;
    for (int i = 0; i < MAX_LOG_MESSAGES; i++) logBuffer[i] = "";
    xSemaphoreGive(logMutex);
  }
}

float getBoardTemp() {
  float t = 0.0;
  #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 2
  t = temperatureRead();
  #endif
  if (isnan(t) || t < -50 || t > 150) t = 43.5;
  return t;
}

// ===== SIMPLE JSON EXTRACTOR =====
String jsonExtract(const String& json, const String& key) {
  String search = "\"" + key + "\"";
  int idx = json.indexOf(search);
  if (idx < 0) return "";
  idx = json.indexOf(':', idx);
  if (idx < 0) return "";
  idx++;
  while (idx < (int)json.length() && (json[idx] == ' ' || json[idx] == '\t')) idx++;
  if (idx >= (int)json.length()) return "";
  if (json[idx] == '"') {
    int end = json.indexOf('"', idx + 1);
    if (end < 0) return "";
    // Skip escaped quotes (\") so values containing them aren't truncated
    while (end > 0 && json[end - 1] == '\\') {
      end = json.indexOf('"', end + 1);
      if (end < 0) return "";
    }
    return json.substring(idx + 1, end);
  }
  int end = idx;
  while (end < (int)json.length() && json[end] != ',' && json[end] != '}' && json[end] != ' ') end++;
  return json.substring(idx, end);
}

// ===== HELPER: Send ESP-NOW message =====
void sendToSlave(struct_message& msg) {
  if (!slavePaired) {
    addLog("ERROR: Slave not paired, send skipped");
    return;
  }
  struct_message encrypted;
  memset(&encrypted, 0, sizeof(encrypted));
  memcpy(&encrypted, &msg, sizeof(msg));
  encrypt_aes_blocks((uint8_t*)&encrypted, sizeof(encrypted));
  esp_err_t result = esp_now_send(slaveMac, (uint8_t*)&encrypted, sizeof(encrypted));
  if (result != ESP_OK) addLog("ESP-NOW send failed: " + String(result));
}

// ===== ESP-NOW SEND CALLBACK =====
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void OnDataSent(const esp_now_send_info_t *info, esp_now_send_status_t status) {
#else
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
#endif
  Serial.print("ESP-NOW Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Error");
}

// ===== ESP-NOW RECEIVE CALLBACK =====
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  const uint8_t *srcMac = info->src_addr;
#else
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
  const uint8_t *srcMac = mac_addr;
#endif
  if (len == (int)sizeof(myData)) {
    struct_message incoming;
    memcpy(&incoming, incomingData, sizeof(incoming));
    decrypt_aes_blocks((uint8_t*)&incoming, sizeof(incoming));
    incoming.command[15] = '\0';
    incoming.attack[31] = '\0';
    incoming.logMsg[sizeof(incoming.logMsg)-1] = '\0';
    
    // Auto-pairing with MAC validation
    if (!slavePaired || memcmp(slaveMac, srcMac, 6) != 0) {
      // Reject broadcast/multicast MACs
      if (srcMac[0] == 0xFF && srcMac[1] == 0xFF) return;
      if (srcMac[0] & 0x01) { addLog("Warning: Multicast MAC rejected"); return; }

      Serial.printf("Slave MAC detected: %02X:%02X:%02X:%02X:%02X:%02X\n", 
                    srcMac[0], srcMac[1], srcMac[2], srcMac[3], srcMac[4], srcMac[5]);
      esp_now_del_peer(slaveMac);
      memcpy(slaveMac, srcMac, 6);
      memset(&peerInfo, 0, sizeof(peerInfo));
      memcpy(peerInfo.peer_addr, slaveMac, 6);
      peerInfo.channel = 1;
      peerInfo.encrypt = false;
      peerInfo.ifidx = WIFI_IF_AP;
      if (esp_now_add_peer(&peerInfo) == ESP_OK) {
        slavePaired = true;
        Serial.println("Switched to Unicast with ACK confirmation!");
        Preferences prefs;
        prefs.begin("esptool2", false);
        prefs.putBytes("slaveMac", slaveMac, 6);
        prefs.putBool("slavePaired", true);
        prefs.end();
      }
    }

    if (strcmp(incoming.command, "pong") == 0) {
      lastPongTime = millis();
      slaveTempStr = String(incoming.logMsg);
      Serial.println("Received PONG from SLAVE, temp: " + slaveTempStr + " C");
    } 
    else if (strcmp(incoming.command, "stopped") == 0) {
      isStopping = false;
      stopSendCount = 0;
      lastStopSendTime = 0;
      if (incoming.logMsg[0] != '\0') addLog(incoming.logMsg);
      Serial.println("Slave confirmed: Stopped");
    }
    else if (strcmp(incoming.command, "log") == 0) {
      addLog(incoming.logMsg);
      Serial.printf("LOG from Slave: %s\n", incoming.logMsg);
    }
  }
}

void setup() {
  Serial.begin(115200);
  logMutex = xSemaphoreCreateMutex();

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t wdt_config = { .timeout_ms = WDT_TIMEOUT_SECONDS * 1000, .idle_core_mask = (1 << 0) | (1 << 1), .trigger_panic = true };
  esp_task_wdt_init(&wdt_config);
#else
  esp_task_wdt_init(WDT_TIMEOUT_SECONDS, true);
#endif
  esp_task_wdt_add(NULL);
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password, 1);
  WiFi.setSleep(false);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init Error");
    return;
  }
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);
  
  // Load paired Slave MAC
  Preferences prefs;
  prefs.begin("esptool2", false);
  if (prefs.getBool("slavePaired", false)) {
    prefs.getBytes("slaveMac", slaveMac, 6);
    slavePaired = true;
    Serial.printf("Loaded Slave MAC from Flash: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  slaveMac[0], slaveMac[1], slaveMac[2], slaveMac[3], slaveMac[4], slaveMac[5]);
  }
  prefs.end();

  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, slaveMac, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_AP;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) Serial.println("Failed to add peer");

  // ===== WEB ROUTES (all protected with HTTP Basic Auth) =====
  server.on("/", []() {
    if (!server.authenticate(adminUser, adminPass)) { return server.requestAuthentication(); }
    server.send(200, "text/html", webpage);
  });

  server.on("/logs", []() {
    if (!server.authenticate(adminUser, adminPass)) { return server.requestAuthentication(); }
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
    server.send(200, "text/plain", getLogs());
  });

  server.on("/clear_logs", []() {
    if (!server.authenticate(adminUser, adminPass)) { return server.requestAuthentication(); }
    clearLogs();
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
    server.send(200, "text/plain", "Cleared");
  });

  server.on("/status", []() {
    if (!server.authenticate(adminUser, adminPass)) { return server.requestAuthentication(); }
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
    if (!server.authenticate(adminUser, adminPass)) { return server.requestAuthentication(); }
    String cmd = server.arg("q");
    String attack = server.arg("attack");

    // Command validation whitelist
    const char* validCommands[] = {"start", "stop", "ping"};
    bool validCmd = false;
    for (const auto& vc : validCommands) {
      if (cmd == vc) { validCmd = true; break; }
    }
    if (!validCmd) {
      server.send(400, "text/plain", "Invalid command");
      return;
    }

    strncpy(myData.command, cmd.c_str(), sizeof(myData.command) - 1);
    myData.command[sizeof(myData.command) - 1] = '\0';
    strncpy(myData.attack, attack.c_str(), sizeof(myData.attack) - 1);
    myData.attack[sizeof(myData.attack) - 1] = '\0';
    memset(myData.logMsg, 0, sizeof(myData.logMsg));
    
    char response[256];
    
    if (cmd == "stop") {
      isStopping = true;
      stopSendCount = 0;
      lastStopSendTime = 0;
      strncpy(stopData.command, "stop", sizeof(stopData.command) - 1);
      stopData.command[sizeof(stopData.command) - 1] = '\0';
      strncpy(stopData.attack, attack.c_str(), sizeof(stopData.attack) - 1);
      stopData.attack[sizeof(stopData.attack) - 1] = '\0';
      memset(stopData.logMsg, 0, sizeof(stopData.logMsg));
      snprintf(response, sizeof(response), "Attack %s stopping...", attack.c_str());
      addLog("STOP command triggered");
    } else {
      sendToSlave(myData);
      if (cmd == "start") {
        snprintf(response, sizeof(response), "Attack %s started!", attack.c_str());
        addLog("Attack started: " + attack);
      } else if (cmd == "ping") {
        snprintf(response, sizeof(response), "PING sent!");
      } else {
        snprintf(response, sizeof(response), "Command: %s | Target: %s", cmd.c_str(), attack.c_str());
      }
    }
    
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
    server.send(200, "text/plain", String(response));
  });

  // ===== NEW v1.2 ROUTES =====

  // POST /select_target — send target info to Slave
  server.on("/select_target", HTTP_POST, []() {
    if (!server.authenticate(adminUser, adminPass)) { return server.requestAuthentication(); }
    if (!server.hasArg("plain")) { server.send(400, "text/plain", "No body"); return; }
    String body = server.arg("plain");
    String type = jsonExtract(body, "type");
    struct_message msg = {0};
    strlcpy(msg.command, "select_target", sizeof(msg.command));
    strlcpy(msg.attack, type.c_str(), sizeof(msg.attack));
    String packed = "";
    if (type == "wifi") {
      packed = jsonExtract(body,"ssid") + "|" + jsonExtract(body,"bssid") + "|" + jsonExtract(body,"rssi") + "|" + jsonExtract(body,"channel") + "|" + jsonExtract(body,"security") + "|" + jsonExtract(body,"wps");
    } else if (type == "ble") {
      packed = jsonExtract(body,"name") + "|" + jsonExtract(body,"addr") + "|" + jsonExtract(body,"rssi");
    } else if (type == "subghz") {
      packed = jsonExtract(body,"freq") + "|" + jsonExtract(body,"rssi") + "|" + jsonExtract(body,"mod");
    } else if (type == "ir") {
      packed = jsonExtract(body,"proto") + "|" + jsonExtract(body,"addr") + "|" + jsonExtract(body,"cmd") + "|" + jsonExtract(body,"len");
    }
    strlcpy(msg.logMsg, packed.c_str(), sizeof(msg.logMsg));
    sendToSlave(msg);
    addLog("Target selected: " + type + " -> " + packed);
    server.send(200, "text/plain", "Target selected: " + type);
  });

  // GET /clear_target — clear target on Slave
  server.on("/clear_target", []() {
    if (!server.authenticate(adminUser, adminPass)) { return server.requestAuthentication(); }
    struct_message msg = {0};
    strlcpy(msg.command, "clear_target", sizeof(msg.command));
    sendToSlave(msg);
    addLog("Target cleared");
    server.send(200, "text/plain", "Target cleared");
  });

  // POST /multi_attack — send multi-attack command to Slave
  server.on("/multi_attack", HTTP_POST, []() {
    if (!server.authenticate(adminUser, adminPass)) { return server.requestAuthentication(); }
    if (!server.hasArg("plain")) { server.send(400, "text/plain", "No body"); return; }
    String body = server.arg("plain");
    int arrStart = body.indexOf('[');
    int arrEnd = body.indexOf(']');
    String attacksList = "";
    if (arrStart >= 0 && arrEnd > arrStart) {
      String arr = body.substring(arrStart + 1, arrEnd);
      arr.replace("\"", "");
      arr.replace(" ", "");
      attacksList = arr;
    }
    String mode = jsonExtract(body, "mode");
    struct_message msg = {0};
    strlcpy(msg.command, "multi_attack", sizeof(msg.command));
    strlcpy(msg.attack, attacksList.c_str(), sizeof(msg.attack));
    strlcpy(msg.logMsg, mode.c_str(), sizeof(msg.logMsg));
    sendToSlave(msg);
    addLog("Multi-attack: " + attacksList + " [" + mode + "]");
    server.send(200, "text/plain", "Multi-attack started: " + attacksList);
  });

  // GET /stop_all — stop all attacks on Slave
  server.on("/stop_all", []() {
    if (!server.authenticate(adminUser, adminPass)) { return server.requestAuthentication(); }
    isStopping = true;
    stopSendCount = 0;
    lastStopSendTime = 0;
    strlcpy(stopData.command, "stop", sizeof(stopData.command));
    memset(stopData.attack, 0, sizeof(stopData.attack));
    memset(stopData.logMsg, 0, sizeof(stopData.logMsg));
    addLog("STOP ALL triggered");
    server.sendHeader("Cache-Control", "no-cache");
    server.send(200, "text/plain", "Stopping all attacks...");
  });

  server.begin();
  Serial.println("Server started!");
}

void loop() {
  server.handleClient();
  esp_task_wdt_reset();

  // Heartbeat: ping the Slave every 2s so the web UI's online/offline status
  // and temperature reading stay current even when no attack is running.
  if (slavePaired && !isStopping && (millis() - lastPingSendTime >= PING_INTERVAL)) {
    lastPingSendTime = millis();
    struct_message ping = {0};
    strlcpy(ping.command, "ping", sizeof(ping.command));
    sendToSlave(ping);
  }

  if (isStopping) {
    if (millis() - lastStopSendTime >= STOP_INTERVAL_MS) {
      lastStopSendTime = millis();
      esp_task_wdt_reset();
      // Full zeroing before encryption
      struct_message encryptedStopData;
      memset(&encryptedStopData, 0, sizeof(encryptedStopData));
      memcpy(&encryptedStopData, &stopData, sizeof(stopData));
      encrypt_aes_blocks((uint8_t*)&encryptedStopData, sizeof(encryptedStopData));
      esp_err_t result = esp_now_send(slaveMac, (uint8_t *)&encryptedStopData, sizeof(encryptedStopData));
      if (result != ESP_OK) addLog("ESP-NOW stop send failed: " + String(result));
      stopSendCount++;
      if (stopSendCount >= MAX_STOP_ATTEMPTS) {
        isStopping = false;
        addLog("Stop timeout reached (Slave did not acknowledge)");
        Serial.println("Finished sending stop commands (timeout)");
      }
    }
  }
}
