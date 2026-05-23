#include "controller.h"
#include "messaging.h"
#include "tasks/LCD_task.h"
#include "tasks/MotorNode.h"
#include "drivers/LCDDriver.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>

#define TFT_BL 21
#define BACKLIGHT_CHANNEL 0

// Premium Theme Colors (RGB565)
const uint16_t COLOR_BG = 0xF7BE;          // Deep dark charcoal under inversion
const uint16_t COLOR_PEACH = 0xFD67;       // Vibrant peach/orange (255, 110, 60)
const uint16_t COLOR_PEACH_LIGHT = 0xFEB2; // Soft peach highlight (255, 150, 100)
const uint16_t COLOR_TEXT_MUTED = 0x632C;  // Muted steel gray under inversion
const uint16_t COLOR_TEXT_WHITE = 0x18C3;  // Soft white under inversion
const uint16_t COLOR_BORDER = 0xC618;      // Subtle border gray under inversion

// Forward declarations
void initBacklight();
void fadeBacklightTo(int targetBrightness, int durationMs);
void drawSplashScreen();
void updateBootProgress(int percent, String message);

extern TaskHandle_t lcdTaskHandle;

// Shared UART pins for TMC2209 (Changed to avoid CYD Touch CS conflict on GPIO 33)
#define UART_RX 22
#define UART_TX 27

// Configure Motor 1 (Address 0)
const MotorConfig motor1Config = {
    .serial = &Serial1,
    .address = TMC2209::SERIAL_ADDRESS_0,
    .rxPin = UART_RX,
    .txPin = UART_TX,
    .nvsNamespace = "peach_m1"
};

// Configure Motor 2 (Address 1)
const MotorConfig motor2Config = {
    .serial = &Serial1,
    .address = TMC2209::SERIAL_ADDRESS_1,
    .rxPin = UART_RX,
    .txPin = UART_TX,
    .nvsNamespace = "peach_m2"
};

// Global Node instances (extern in controller.cpp)
MotorNode g_motor1Node(motor1Config);
MotorNode g_motor2Node(motor2Config);

volatile bool isOTA = false;
volatile int otaProgress = 0;

void setup() {
  // Begin USB serial for debugging/monitoring
  Serial.begin(115200);

  // Initialize shared Motor UART on pins 22, 27
  Serial1.begin(115200, SERIAL_8N1, UART_RX, UART_TX);

  // Initialize System State from NVS
  initSystemState();

  // Inits
  LCDInit();
  initBacklight();
  drawSplashScreen();
  fadeBacklightTo(255, 800);

  updateBootProgress(10, "Connecting to WiFi...");
  
  WiFi.mode(WIFI_STA);
  Preferences prefs;
  prefs.begin("wifi_pref", true); // Read-only mode
  String lastSSID = prefs.getString("last_ssid", "");
  prefs.end();

  bool connected = false;

  if (lastSSID.length() > 0) {
    const char* pass = (lastSSID == "Chaos Capital") ? "bccbtscott" : "";
    updateBootProgress(20, "Connecting to saved AP...");
    WiFi.begin(lastSSID.c_str(), pass);
    
    // Quick 2.5-second wait
    for (int i = 0; i < 25; i++) {
      if (WiFi.status() == WL_CONNECTED) {
        connected = true;
        break;
      }
      updateBootProgress(20 + i, "Connecting to saved AP...");
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }

  if (!connected) {
    updateBootProgress(45, "Scanning networks...");
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
      updateBootProgress(55, "Connecting to " + bestSSID + "...");
      WiFi.begin(bestSSID.c_str(), bestPass);
      
      // Wait up to 5 seconds for connection
      for (int i = 0; i < 50; i++) {
        if (WiFi.status() == WL_CONNECTED) {
          connected = true;
          
          // Save to preferences
          prefs.begin("wifi_pref", false); // Read-write
          prefs.putString("last_ssid", bestSSID);
          prefs.end();
          break;
        }
        updateBootProgress(55 + (i / 5), "Connecting to " + bestSSID + "...");
        vTaskDelay(pdMS_TO_TICKS(100));
      }
    }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    updateBootProgress(60, "WiFi Connected!");
    if (MDNS.begin("peachpulp")) {
      ESP_LOGI("MAIN", "mDNS responder started: peachpulp.local");
      updateBootProgress(70, "mDNS Active");
    }
    ArduinoOTA.setHostname("peachpulp");
    
    // Custom OTA Callbacks
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
    updateBootProgress(90, "OTA Listener Ready");
  } else {
    updateBootProgress(60, "WiFi Failed. Offline.");
  }
  
  updateBootProgress(100, "System Starting...");
  vTaskDelay(pdMS_TO_TICKS(500));
  
  // Clear screen before launching LCD Task UI
  tft.fillScreen(COLOR_BG);

  // Task Update Intervals
  static int lcd_interval = TASK_REFRESH_LCD;

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
  xTaskCreate(LCD_task, "LCD", 8192, &lcd_interval, 2, &lcdTaskHandle);

  // Restore setup() to Priority 1
  vTaskPrioritySet(NULL, 1);
}

void loop() {
  ArduinoOTA.handle();
  vTaskDelay(pdMS_TO_TICKS(50));
}

// --- Boot UI Helper Implementations ---

void initBacklight() {
  ledcSetup(BACKLIGHT_CHANNEL, 5000, 8);
  ledcAttachPin(TFT_BL, BACKLIGHT_CHANNEL);
  ledcWrite(BACKLIGHT_CHANNEL, 0); 
}

void fadeBacklightTo(int targetBrightness, int durationMs) {
  int steps = 30;
  int stepDelay = durationMs / steps;
  for (int i = 0; i <= steps; i++) {
    int val = (targetBrightness * i) / steps;
    ledcWrite(BACKLIGHT_CHANNEL, val);
    vTaskDelay(pdMS_TO_TICKS(stepDelay));
  }
}

void drawSplashScreen() {
  tft.fillScreen(COLOR_BG);
  int w = tft.width();
  int h = tft.height();
  int cx = w / 2;
  int cy = h * 0.33;

  tft.drawCircle(cx, cy, 32, COLOR_PEACH);
  tft.drawCircle(cx, cy, 33, COLOR_PEACH); 
  tft.fillCircle(cx, cy, 24, COLOR_PEACH_LIGHT); 
  tft.fillCircle(cx + 12, cy, 14, COLOR_BG); 

  int textY = cy + 55;
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(0xFFFF); 
  tft.drawString("PEACH PULP", cx + 2, textY + 2, 4);
  tft.setTextColor(COLOR_PEACH); 
  tft.drawString("PEACH PULP", cx, textY, 4);

  int subY = textY + 25;
  tft.setTextColor(COLOR_TEXT_MUTED);
  tft.drawString("SYSTEM BOOTING", cx, subY, 2);

  int barMaxWidth = (w > 240) ? 224 : 180;
  int barX = (w - barMaxWidth) / 2;
  int barY = h - 35;
  tft.drawRoundRect(barX - 2, barY - 2, barMaxWidth + 4, 12, 4, COLOR_BORDER);
}

void updateBootProgress(int percent, String message) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;

  int w = tft.width();
  int h = tft.height();
  int cx = w / 2;
  int barY = h - 35;

  tft.fillRect(10, barY - 25, w - 20, 18, COLOR_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_TEXT_WHITE, COLOR_BG);
  tft.drawString(message, cx, barY - 15, 1);

  int barMaxWidth = (w > 240) ? 224 : 180;
  int barX = (w - barMaxWidth) / 2;
  int barWidth = (barMaxWidth * percent) / 100;
  if (barWidth > 0) {
    tft.fillRoundRect(barX, barY, barWidth, 8, 3, COLOR_PEACH);
  }
}