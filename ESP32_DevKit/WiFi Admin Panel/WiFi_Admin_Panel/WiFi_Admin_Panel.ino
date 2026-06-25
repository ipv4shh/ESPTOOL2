#include <WiFi.h>
#include <WebServer.h>
#include <esp_now.h>
#include <Preferences.h>
#include "webpage.h"
#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

// ===== XOR CRYPT HELPER =====
void XOR_crypt(uint8_t* data, size_t len) {
  const uint8_t key[] = { 0xDE, 0xAD, 0xBE, 0xEF };
  for (size_t i = 0; i < len; i++) {
    data[i] ^= key[i % 4];
  }
}

// ===== SETTINGS =====
const char* ap_ssid = "ESP-Admin";
const char* ap_password = "Esp_div_admin";
WebServer server(80);

// SLAVE MAC Address (Broadcast FF:FF:FF:FF:FF:FF for auto-pairing initially)
uint8_t slaveMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
bool slavePaired = false;
unsigned long lastPongTime = 0;
String slaveTempStr = "44.8";

// Non-blocking background STOP variables
bool isStopping = false;
unsigned long lastStopSendTime = 0;
int stopSendCount = 0;

// Circular Log Buffer
#define MAX_LOG_MESSAGES 60
String logBuffer[MAX_LOG_MESSAGES];
int logHead = 0;
int logCount = 0;

// ===== ESP-NOW STRUCTURE =====
typedef struct struct_message {
  char command[16];
  char attack[32];
  char logMsg[200]; // Increased to 200 bytes to prevent truncated device scan data (fits ESP-NOW limit)
} struct_message;
struct_message myData;
struct_message stopData; // Defined globally to avoid conflict
esp_now_peer_info_t peerInfo;

void addLog(const String& logMsg) {
  String timeStr = "[" + String(millis() / 1000) + "s] ";
  logBuffer[logHead] = timeStr + logMsg;
  logHead = (logHead + 1) % MAX_LOG_MESSAGES;
  if (logCount < MAX_LOG_MESSAGES) {
    logCount++;
  }
}

String getLogs() {
  String allLogs = "";
  int startIndex = (logCount == MAX_LOG_MESSAGES) ? logHead : 0;
  for (int i = 0; i < logCount; i++) {
    int index = (startIndex + i) % MAX_LOG_MESSAGES;
    allLogs += logBuffer[index] + "\n";
  }
  return allLogs;
}

void clearLogs() {
  logHead = 0;
  logCount = 0;
  for (int i = 0; i < MAX_LOG_MESSAGES; i++) {
    logBuffer[i] = "";
  }
}

// Get board internal temperature safely
float getBoardTemp() {
  float t = 0.0;
  #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 2
  t = temperatureRead();
  #endif
  if (isnan(t) || t < -50 || t > 150) {
    t = 43.5; // realistic fallback temperature in Celsius
  }
  return t;
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
  if (len == sizeof(myData)) {
    struct_message incoming;
    memcpy(&incoming, incomingData, sizeof(incoming));
    XOR_crypt((uint8_t*)&incoming, sizeof(incoming)); // Decrypt
    incoming.command[15] = '\0';
    incoming.attack[31] = '\0';
    incoming.logMsg[199] = '\0';
    
    // Auto-pairing: switch to Unicast on first received packet from Slave
    if (!slavePaired || memcmp(slaveMac, srcMac, 6) != 0) {
      Serial.printf("Slave MAC detected: %02X:%02X:%02X:%02X:%02X:%02X\n", 
                    srcMac[0], srcMac[1], srcMac[2], srcMac[3], srcMac[4], srcMac[5]);
      
      // Delete old peer
      esp_now_del_peer(slaveMac);
      
      // Store new MAC
      memcpy(slaveMac, srcMac, 6);
      
      memset(&peerInfo, 0, sizeof(peerInfo));
      memcpy(peerInfo.peer_addr, slaveMac, 6);
      peerInfo.channel = 1;
      peerInfo.encrypt = false;
      peerInfo.ifidx = WIFI_IF_AP;
      
      if (esp_now_add_peer(&peerInfo) == ESP_OK) {
        slavePaired = true;
        Serial.println("Switched to Unicast with ACK confirmation!");
        
        // Save to Preferences
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
      addLog(incoming.logMsg);
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
  
  // Access Point on Channel 1
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password, 1);
  WiFi.setSleep(false);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init Error");
    return;
  }
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb((esp_now_recv_cb_t)OnDataRecv);
  
  // Load paired Slave MAC from Preferences
  Preferences prefs;
  prefs.begin("esptool2", true);
  if (prefs.getBool("slavePaired", false)) {
    prefs.getBytes("slaveMac", slaveMac, 6);
    slavePaired = true;
    Serial.printf("Loaded Slave MAC from Flash: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  slaveMac[0], slaveMac[1], slaveMac[2], slaveMac[3], slaveMac[4], slaveMac[5]);
  }
  prefs.end();

  // Add peer
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, slaveMac, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_AP;
  esp_now_add_peer(&peerInfo);

  // Web routes
  server.on("/", []() {
    server.send(200, "text/html", webpage);
  });

  server.on("/logs", []() {
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
    server.send(200, "text/plain", getLogs());
  });

  server.on("/clear_logs", []() {
    clearLogs();
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
    
    String response = "";
    
    // For stop command: trigger background send loop
    if (cmd == "stop") {
      isStopping = true;
      stopSendCount = 0;
      lastStopSendTime = 0;
      strncpy(stopData.command, "stop", sizeof(stopData.command) - 1);
      stopData.command[sizeof(stopData.command) - 1] = '\0';
      strncpy(stopData.attack, attack.c_str(), sizeof(stopData.attack) - 1);
      stopData.attack[sizeof(stopData.attack) - 1] = '\0';
      memset(stopData.logMsg, 0, sizeof(stopData.logMsg));
      
      response = "Attack " + attack + " stopping...";
      addLog("STOP command triggered");
    } else {
      struct_message encryptedMyData = myData;
      XOR_crypt((uint8_t*)&encryptedMyData, sizeof(encryptedMyData));
      esp_now_send(slaveMac, (uint8_t *) &encryptedMyData, sizeof(encryptedMyData));
      response = "Command: " + cmd + " | Target: " + attack;
      if (cmd == "start") {
        response = "Attack " + attack + " started!";
        addLog("Attack started: " + attack);
      }
      else if (cmd == "ping") response = "PING sent!";
    }
    
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
    server.send(200, "text/plain", response);
  });

  server.begin();
  Serial.println("Server started!");
}

void loop() {
  server.handleClient();
  
  if (isStopping) {
    if (millis() - lastStopSendTime >= 80) {
      lastStopSendTime = millis();
      struct_message encryptedStopData = stopData;
      XOR_crypt((uint8_t*)&encryptedStopData, sizeof(encryptedStopData));
      esp_now_send(slaveMac, (uint8_t *) &encryptedStopData, sizeof(encryptedStopData));
      stopSendCount++;
      if (stopSendCount >= 25) { // Try up to 25 times (2 seconds)
        isStopping = false;
        addLog("Stop timeout reached (Slave did not acknowledge)");
        Serial.println("Finished sending stop commands (timeout)");
      }
    }
  }
}
