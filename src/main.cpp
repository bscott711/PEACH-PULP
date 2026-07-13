#include "controller.h"
#include "messaging.h"
#include "tasks/MotorNode.h"
#include "HardwareConfig.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>

// Configure Motor 1 (Address 0)
const MotorConfig motor1Config = {
    .serial = &Serial1, // Use Serial1 (UART1)
    .address = TMC2209::SERIAL_ADDRESS_0,
    .rxPin = RXD1,
    .txPin = TXD1,
    .nvsNamespace = "peach_m1"
};

// Configure Motor 2 (Address 1)
const MotorConfig motor2Config = {
    .serial = &Serial1, // Use Serial1 (UART1)
    .address = TMC2209::SERIAL_ADDRESS_1,
    .rxPin = RXD1,
    .txPin = TXD1,
    .nvsNamespace = "peach_m2"
};

// Global Node instances (extern in controller.cpp)
MotorNode g_motor1Node(motor1Config);
MotorNode g_motor2Node(motor2Config);

volatile bool isOTA = false;
volatile int otaProgress = 0;

void setup() {
  // Create shared UART mutex before starting motor tasks
  xUARTMutex = xSemaphoreCreateMutex();

  // USB serial for debug logging — motors use Serial1 on the TMC UART pins
  Serial.begin(115200);

  // Global wake up for TMC2209s on the shared bus
  pinMode(TXD1, OUTPUT);
  digitalWrite(TXD1, HIGH);
  delay(50);

  // Pre-initialize Serial1 in setup() context
  Serial1.begin(SERIAL_BAUD_RATE, SERIAL_8N1, RXD1, TXD1);
  delay(50);

  // Initialize System State from NVS
  initSystemState();

  // --- WiFi (temporary inline block; moves to NetworkManager in Milestone 2) ---
  WiFi.mode(WIFI_STA);
  Preferences prefs;
  prefs.begin("wifi_pref", true); // Read-only mode
  String lastSSID = prefs.getString("last_ssid", "");
  prefs.end();

  bool connected = false;

  if (lastSSID.length() > 0) {
    const char* pass = (lastSSID == "Chaos Capital") ? "bccbtscott" : "";
    WiFi.begin(lastSSID.c_str(), pass);

    for (int i = 0; i < 25; i++) {
      if (WiFi.status() == WL_CONNECTED) {
        connected = true;
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }

  if (!connected) {
    WiFi.disconnect(true);
    vTaskDelay(pdMS_TO_TICKS(100));
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
      WiFi.begin(bestSSID.c_str(), bestPass);

      for (int i = 0; i < 50; i++) {
        if (WiFi.status() == WL_CONNECTED) {
          connected = true;
          prefs.begin("wifi_pref", false); // Read-write
          prefs.putString("last_ssid", bestSSID);
          prefs.end();
          break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
      }
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    ESP_LOGI("MAIN", "WiFi Connected!");
    if (MDNS.begin("peachpulp")) {
      ESP_LOGI("MAIN", "mDNS responder started: peachpulp.local");
    }
    ArduinoOTA.setHostname("peachpulp");

    ArduinoOTA.onStart([&]() {
      isOTA = true;
      otaProgress = 0;
    });

    ArduinoOTA.onProgress([&](unsigned int progress, unsigned int total) {
      otaProgress = (progress / (total / 100));
    });

    ArduinoOTA.onEnd([&]() {
      otaProgress = 100;
    });

    ArduinoOTA.begin();
    ESP_LOGI("MAIN", "OTA Listener Ready");
  } else {
    ESP_LOGW("MAIN", "WiFi Failed. Offline.");
  }

  // Elevate setup() to Priority 5 (higher than our tasks)
  vTaskPrioritySet(NULL, 5);

  // 1. Start Active Motion Nodes
  if (!g_motor1Node.start("Motor1Node", 4096, 2))
    ESP_LOGE("MAIN", "Failed Motor1Node");
  if (!g_motor2Node.start("Motor2Node", 4096, 2))
    ESP_LOGE("MAIN", "Failed Motor2Node");

  // 2. Link the global messaging queues
  motor1CmdQueue = g_motor1Node.getCmdQueue();
  motor1TelQueue = g_motor1Node.getTelQueue();
  motor2CmdQueue = g_motor2Node.getCmdQueue();
  motor2TelQueue = g_motor2Node.getTelQueue();

  // 3. Create Dependent Tasks
  xTaskCreate(controller_task, "Controller", 4096, NULL, 3, NULL);

  // Restore setup() to Priority 1
  vTaskPrioritySet(NULL, 1);
}

void loop() {
  ArduinoOTA.handle();
  vTaskDelay(pdMS_TO_TICKS(50));
}
