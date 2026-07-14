#include "core/NetworkManager.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include "messaging.h"
#include "drivers/LCDDriver.h" // For draw_wifiStatus
#include <esp_log.h>

// WiFi Serial Bridge
static WiFiServer wifiSerialServer(6666);
static WiFiClient wifiSerialClient;

static QueueHandle_t logQueue = NULL;

void NetworkManager::logToWiFi(const char* level, const char* tag, const char* format, ...) {
    char buf[128];
    // Format timestamp, level, and tag
    int prefixLen = snprintf(buf, sizeof(buf), "%s (%lu) %s: ", level, millis(), tag);

    if (prefixLen > 0 && prefixLen < sizeof(buf)) {
        va_list args;
        va_start(args, format);
        int msgLen = vsnprintf(buf + prefixLen, sizeof(buf) - prefixLen - 1, format, args);
        va_end(args);

        if (msgLen > 0) {
            int totalLen = prefixLen + msgLen;
            if (totalLen >= sizeof(buf) - 1) totalLen = sizeof(buf) - 2;
            buf[totalLen] = '\n';
            buf[totalLen + 1] = '\0';

            // Output to standard USB Serial
            Serial.print(buf);

            // Output to WiFi Queue
            if (logQueue != NULL) {
                if (xPortInIsrContext()) {
                    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
                    xQueueSendToBackFromISR(logQueue, buf, &xHigherPriorityTaskWoken);
                } else {
                    xQueueSendToBack(logQueue, buf, 0);
                }
            }
        }
    }
}

static volatile bool g_otaActive = false;
static volatile int g_otaProgress = 0;
static const char* g_otaStatus = "";
static bool g_wifiConnected = false;

// Motor safety interlock helper — stops all pumps immediately on OTA start.
static void stopAllPumps() {
    MotorCommand stop = {MotorCmdAction::SET_SPEED, 0.0f};
    if (samplePumpCmdQueue != NULL) xQueueSend(samplePumpCmdQueue, &stop, 0);
    if (dyePumpCmdQueue != NULL) xQueueSend(dyePumpCmdQueue, &stop, 0);
    if (washPumpCmdQueue != NULL) xQueueSend(washPumpCmdQueue, &stop, 0);
}

void NetworkManager::init() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // Disable power save for reliable OTA (UDP/mDNS)

  Preferences prefs;
  prefs.begin("wifi_pref", true); // Read-only
  String lastSSID = prefs.getString("last_ssid", "");
  prefs.end();

  bool connected = false;
  String triedSSID = lastSSID;

  if (lastSSID.length() > 0) {
    const char* pass = (lastSSID == "Chaos Capital") ? "bccbtscott" : "";
    WiFi.begin(lastSSID.c_str(), pass);

    for (int i = 0; i < 25; i++) {
      if (WiFi.status() == WL_CONNECTED) {
        connected = true;
        break;
      }
      draw_wifiStatus("Connecting (saved)", lastSSID.c_str(), i, false);
      delay(100);
    }
  }

  if (!connected) {
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);

    int n = WiFi.scanNetworks();
    String bestSSID = "";
    const char* bestPass = "";
    int bestRSSI = -999;

    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      int rssi = WiFi.RSSI(i);
      if (ssid == "sdsmtopn") {
        if (rssi > bestRSSI) {
          bestSSID = "sdsmtopn";
          bestPass = "";
          bestRSSI = rssi;
        }
      } else if (ssid == "Chaos Capital") {
        if (rssi > bestRSSI) {
          bestSSID = "Chaos Capital";
          bestPass = "bccbtscott";
          bestRSSI = rssi;
        }
      }
    }

    if (bestSSID.length() > 0) {
      triedSSID = bestSSID;
      WiFi.begin(bestSSID.c_str(), bestPass);

      for (int i = 0; i < 50; i++) {
        if (WiFi.status() == WL_CONNECTED) {
          connected = true;
          prefs.begin("wifi_pref", false); // Read-write
          prefs.putString("last_ssid", bestSSID);
          prefs.end();
          break;
        }
        draw_wifiStatus("Connecting", bestSSID.c_str(), i, false);
        delay(100);
      }
    }
  }

  if (!connected) {
    // Preserve PULP's existing behavior: continue offline, never reboot.
    draw_wifiStatus("Offline (no WiFi)", triedSSID.c_str(), 0, false);
    Serial.println("WiFi Failed. Continuing offline.");
    delay(1500);
    return;
  }

  g_wifiConnected = true;
  Serial.println("WiFi Connected!");
  draw_wifiStatus("peachpulp.local", triedSSID.c_str(), 0, false);
  delay(1000);

  ArduinoOTA.setHostname("peachpulp");

  ArduinoOTA
    .onStart([]() {
      g_otaActive = true;
      g_otaProgress = 0;

      if (ArduinoOTA.getCommand() == U_FLASH) {
        g_otaStatus = "Updating Sketch";
      } else { // U_SPIFFS
        g_otaStatus = "Updating Filesystem";
      }
      Serial.println("Start updating");

      stopAllPumps();
    })
    .onEnd([]() {
      g_otaProgress = 100;
      g_otaStatus = "Success! Rebooting";
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      g_otaProgress = (progress * 100) / total;
      Serial.printf("Progress: %u%%\r", g_otaProgress);
    })
    .onError([](ota_error_t error) {
      g_otaActive = false;
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) g_otaStatus = "Auth Failed";
      else if (error == OTA_BEGIN_ERROR) g_otaStatus = "Begin Failed";
      else if (error == OTA_CONNECT_ERROR) g_otaStatus = "Connect Failed";
      else if (error == OTA_RECEIVE_ERROR) g_otaStatus = "Receive Failed";
      else if (error == OTA_END_ERROR) g_otaStatus = "End Failed";
    });

  if (MDNS.begin("peachpulp")) {
    Serial.println("mDNS responder started: peachpulp.local");
  }
  ArduinoOTA.begin();
  Serial.println("OTA Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Start WiFi Serial Bridge
  wifiSerialServer.begin();
  wifiSerialServer.setNoDelay(true);
  if (logQueue == NULL) {
      logQueue = xQueueCreate(20, 128); // 20 messages, 128 bytes each
  }

  Serial.println("\n--- WiFi Serial Bridge Ready on Port 6666 ---");
}

void NetworkManager::handle() {
    if (!g_wifiConnected) return;

    ArduinoOTA.handle();

    // --- WiFi Serial Bridge Handling ---

    // 1. Handle new connections from PlatformIO
    if (wifiSerialServer.hasClient()) {
        if (!wifiSerialClient || !wifiSerialClient.connected()) {
            if (wifiSerialClient) wifiSerialClient.stop(); // Kick old client
            wifiSerialClient = wifiSerialServer.available();
            wifiSerialClient.println("\n\n--- CONNECTED TO PEACHPULP WIFI SERIAL ---");
        } else {
            wifiSerialServer.available().stop(); // Reject if already connected
        }
    }

    // 2. Forward ESP32 Serial output -> PlatformIO (Mac)
    if (logQueue != NULL) {
        char logBuf[128];
        while (xQueueReceive(logQueue, logBuf, 0) == pdPASS) {
            if (wifiSerialClient && wifiSerialClient.connected()) {
                wifiSerialClient.write((uint8_t*)logBuf, strlen(logBuf));
            }
        }
    }

    // Also forward any typed USB console input to the WiFi client
    if (Serial.available()) {
        uint8_t txBuf[128];
        size_t available = Serial.available();
        size_t toRead = available > sizeof(txBuf) ? sizeof(txBuf) : available;
        size_t len = Serial.readBytes(txBuf, toRead);
        if (wifiSerialClient && wifiSerialClient.connected()) {
            wifiSerialClient.write(txBuf, len);
        }
    }

    // 3. Forward PlatformIO (Mac) keyboard input -> ESP32 Serial (TX)
    if (wifiSerialClient && wifiSerialClient.connected() && wifiSerialClient.available()) {
        uint8_t rxBuf[128];
        size_t available = wifiSerialClient.available();
        size_t toRead = available > sizeof(rxBuf) ? sizeof(rxBuf) : available;
        size_t len = wifiSerialClient.readBytes(rxBuf, toRead);
        Serial.write(rxBuf, len);
    }
}

bool NetworkManager::isOTAActive() {
    return g_otaActive;
}

int NetworkManager::getOTAProgress() {
    return g_otaProgress;
}

const char* NetworkManager::getOTAStatus() {
    return g_otaStatus;
}
