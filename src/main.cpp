#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <SPI.h>
#include <TFT_eSPI.h>

#define TFT_BL 21
#define BACKLIGHT_CHANNEL 0

// Display Rotation Config:
// 0 = Portrait (Standard vertical), 1 = Landscape (CW), 2 = Portrait (180 deg), 3 = Landscape (CCW)
// Changed to 0 to enable native Portrait mode for the vertical enclosure window!
// (If it is upside down, simply change this value to 2!)
#define DISPLAY_ROTATION 0

// Initialize TFT
TFT_eSPI tft = TFT_eSPI();

// Premium Theme Colors (RGB565)
const uint16_t COLOR_BG = 0x10A2;          // Deep dark charcoal (16, 16, 20)
const uint16_t COLOR_PEACH = 0xFD67;       // Vibrant peach/orange (255, 110, 60)
const uint16_t COLOR_PEACH_LIGHT = 0xFEB2; // Soft peach highlight (255, 150, 100)
const uint16_t COLOR_TEXT_MUTED = 0x8C51;  // Muted steel gray (140, 140, 145)
const uint16_t COLOR_TEXT_WHITE = 0xF7BE;  // Soft white (245, 245, 250)
const uint16_t COLOR_BORDER = 0x39E7;      // Subtle border gray (56, 56, 60)

const char *ssid = "sdsmtopn";
const char *password = "";

// Forward Declarations
void initBacklight();
void fadeBacklightTo(int targetBrightness, int durationMs);
void drawSplashScreen();
void updateBootProgress(int percent, String message);
void drawMainInterface();

void setup() {
  Serial.begin(115200);
  Serial.println("Booting...");

  // 1. Initialize Backlight PWM (Keep it dark initially to prevent screen flash)
  initBacklight();

  // 2. Initialize display hardware
  tft.init();
  tft.setRotation(DISPLAY_ROTATION); // Apply custom rotation

  // 3. Render the initial splash screen
  drawSplashScreen();
  updateBootProgress(5, "Initializing display...");

  // 4. Fade in the backlight beautifully
  fadeBacklightTo(255, 500); // Smooth 500ms fade-in
  delay(200);

  updateBootProgress(15, "Connecting to WiFi...");

  // 5. Connect to the WiFi network
  Serial.printf("Connecting to WiFi SSID: %s...\n", ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // Wait for connection (update progress bar dynamically)
  int attempt = 0;
  int basePercent = 15;
  while (WiFi.status() != WL_CONNECTED && attempt < 30) {
    attempt++;
    int currentPercent = basePercent + (attempt * 2); // 15% -> 75%
    String loadingMsg = "Connecting to WiFi... (" + String(attempt) + "/30)";
    updateBootProgress(currentPercent, loadingMsg);
    delay(500);
  }
  Serial.println("");

  if (WiFi.status() == WL_CONNECTED) {
    updateBootProgress(80, "WiFi connected! Starting services...");
    Serial.println("WiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // Start mDNS
    updateBootProgress(85, "Starting mDNS responder...");
    if (!MDNS.begin("peachpulp")) {
      Serial.println("Error setting up MDNS responder!");
      updateBootProgress(88, "mDNS error! Continuing...");
      delay(500);
    } else {
      Serial.println("mDNS responder started: peachpulp.local");
      updateBootProgress(90, "mDNS responder active.");
      delay(200);
    }
  } else {
    Serial.println("WiFi connection failed!");
    updateBootProgress(80, "WiFi failed! Offline Mode.");
    delay(1000);
  }

  // Configure OTA Hostname
  updateBootProgress(92, "Configuring OTA updates...");
  ArduinoOTA.setHostname("peachpulp");

  // Configure OTA Callbacks with elegant custom on-screen progress
  ArduinoOTA
      .onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH) {
          type = "sketch";
        } else { // U_SPIFFS
          type = "filesystem";
        }
        Serial.println("Start updating " + type);

        int w = tft.width();
        int h = tft.height();
        int cx = w / 2;
        int cy = h * 0.33;

        // Render OTA Start screen
        tft.fillScreen(COLOR_BG);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(COLOR_PEACH);
        tft.drawString("OTA UPDATE IN PROGRESS", cx, cy, 4);
        
        tft.setTextColor(COLOR_TEXT_MUTED);
        tft.drawString("Receiving new firmware...", cx, cy + 35, 2);
        tft.drawString("Do not power off the device", cx, cy + 55, 2);
        
        // Progress outline
        int barMaxWidth = (w > 240) ? 224 : 180;
        int barX = (w - barMaxWidth) / 2;
        tft.drawRoundRect(barX - 2, h - 42, barMaxWidth + 4, 12, 4, COLOR_BORDER);
      })
      .onEnd([]() {
        Serial.println("\nEnd");
        int w = tft.width();
        int h = tft.height();
        int cx = w / 2;
        int cy = h / 2;

        tft.fillScreen(COLOR_BG);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(COLOR_PEACH);
        tft.drawString("UPDATE COMPLETE", cx, cy - 20, 4);
        tft.setTextColor(COLOR_TEXT_WHITE);
        tft.drawString("Rebooting system...", cx, cy + 20, 2);
      })
      .onProgress([](unsigned int progress, unsigned int total) {
        unsigned int progressPercent = progress / (total / 100);
        Serial.printf("Progress: %u%%\r", progressPercent);

        int w = tft.width();
        int h = tft.height();
        int cx = w / 2;

        // Clear dynamic percentage area
        tft.fillRect(cx - 70, h - 72, 140, 20, COLOR_BG);
        tft.setTextColor(COLOR_TEXT_WHITE);
        tft.drawString(String(progressPercent) + "% Completed", cx, h - 62, 2);

        // Draw inner progress bar
        int barMaxWidth = (w > 240) ? 224 : 180;
        int barX = (w - barMaxWidth) / 2;
        int barWidth = (barMaxWidth * progressPercent) / 100;
        if (barWidth > 0) {
          tft.fillRoundRect(barX, h - 40, barWidth, 8, 3, COLOR_PEACH);
        }
      })
      .onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        String errStr = "OTA Error!";
        if (error == OTA_AUTH_ERROR) errStr = "Auth Failed";
        else if (error == OTA_BEGIN_ERROR) errStr = "Begin Failed";
        else if (error == OTA_CONNECT_ERROR) errStr = "Connect Failed";
        else if (error == OTA_RECEIVE_ERROR) errStr = "Receive Failed";
        else if (error == OTA_END_ERROR) errStr = "End Failed";

        int w = tft.width();
        int h = tft.height();
        int cx = w / 2;
        int cy = h / 2;

        tft.fillScreen(COLOR_BG);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_RED);
        tft.drawString("UPDATE FAILED", cx, cy - 20, 4);
        tft.setTextColor(COLOR_TEXT_WHITE);
        tft.drawString(errStr, cx, cy + 20, 2);
        delay(2000);
      });

  // Start OTA
  updateBootProgress(96, "Starting OTA listener...");
  ArduinoOTA.begin();

  // Completion
  updateBootProgress(100, "System Ready!");
  delay(800);

  // Transition to main visual dashboard
  drawMainInterface();
}

void loop() {
  ArduinoOTA.handle();
}

// --- Helper Functions ---

void initBacklight() {
  ledcSetup(BACKLIGHT_CHANNEL, 5000, 8);
  ledcAttachPin(TFT_BL, BACKLIGHT_CHANNEL);
  ledcWrite(BACKLIGHT_CHANNEL, 0); // Keep screen dark initially
}

void fadeBacklightTo(int targetBrightness, int durationMs) {
  int steps = 30;
  int stepDelay = durationMs / steps;
  for (int i = 0; i <= steps; i++) {
    int val = (targetBrightness * i) / steps;
    ledcWrite(BACKLIGHT_CHANNEL, val);
    delay(stepDelay);
  }
}

void drawSplashScreen() {
  tft.fillScreen(COLOR_BG);

  int w = tft.width();
  int h = tft.height();
  int cx = w / 2;
  int cy = h * 0.33; // Centered at 33% vertical height

  // 1. Sleek Modern Peach Logo (Circular crescent design)
  tft.drawCircle(cx, cy, 32, COLOR_PEACH);
  tft.drawCircle(cx, cy, 33, COLOR_PEACH); // Bold outer ring
  tft.fillCircle(cx, cy, 24, COLOR_PEACH_LIGHT); // Soft inner peach circle
  tft.fillCircle(cx + 12, cy, 14, COLOR_BG); // Dark offset crescent cutout

  // 2. Brand Name Shadow & Highlight
  int textY = cy + 55;
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(0x1842); // Soft dark shadow
  tft.drawString("PEACH PULP", cx + 2, textY + 2, 4);
  tft.setTextColor(COLOR_PEACH); // Main heading
  tft.drawString("PEACH PULP", cx, textY, 4);

  // 3. Subtitle
  int subY = textY + 25;
  tft.setTextColor(COLOR_TEXT_MUTED);
  tft.drawString("SYSTEM BOOTING", cx, subY, 2);

  // 4. Progress bar outer boundary
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

  // Clear previous message box to avoid overlap ghosting
  tft.fillRect(10, barY - 25, w - 20, 18, COLOR_BG);

  // Draw updated status
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_TEXT_WHITE);
  tft.drawString(message, cx, barY - 15, 1);

  // Grow the progress bar
  int barMaxWidth = (w > 240) ? 224 : 180;
  int barX = (w - barMaxWidth) / 2;
  int barWidth = (barMaxWidth * percent) / 100;
  if (barWidth > 0) {
    tft.fillRoundRect(barX, barY, barWidth, 8, 3, COLOR_PEACH);
  }

  // Serial logging for diagnostics
  Serial.printf("[BOOT] %d%% - %s\n", percent, message.c_str());
}

void drawMainInterface() {
  tft.fillScreen(COLOR_BG);

  int w = tft.width();
  int h = tft.height();

  // 1. Top Header Banner
  tft.fillRect(0, 0, w, 40, COLOR_BORDER);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_PEACH);
  tft.drawString("PEACH PULP", 15, 20, 2);

  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(COLOR_TEXT_MUTED);
  tft.drawString("v1.0.0", w - 15, 20, 2);

  // 2. Central Panel Frame
  tft.drawRoundRect(15, 55, w - 30, h - 80, 8, COLOR_BORDER);

  // 3. Status Information
  tft.setTextDatum(TL_DATUM);
  if (WiFi.status() == WL_CONNECTED) {
    tft.setTextColor(COLOR_PEACH_LIGHT);
    tft.drawString("Device Status: ONLINE", 30, 70, 2);

    tft.setTextColor(COLOR_TEXT_MUTED);
    tft.drawString("Network Settings:", 30, 100, 2);

    tft.setTextColor(COLOR_TEXT_WHITE);
    tft.drawString("SSID : " + String(ssid), 45, 125, 2);
    tft.drawString("IP   : " + WiFi.localIP().toString(), 45, 145, 2);
    tft.drawString("OTA  : peachpulp.local", 45, 165, 2);
  } else {
    tft.setTextColor(TFT_RED);
    tft.drawString("Device Status: OFFLINE", 30, 70, 2);

    tft.setTextColor(COLOR_TEXT_MUTED);
    tft.drawString("Network Settings:", 30, 100, 2);

    tft.setTextColor(COLOR_TEXT_WHITE);
    tft.drawString("SSID : " + String(ssid) + " (Failed)", 45, 125, 2);
    tft.drawString("IP   : ---.---.---.---", 45, 145, 2);
    tft.drawString("OTA  : Disabled", 45, 165, 2);
  }

  // 4. Accent line
  tft.fillRect(15, h - 25, w - 30, 3, COLOR_PEACH);
}