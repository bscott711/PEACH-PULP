#include "drivers/LCDDriver.h"
#include "messaging.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include <cstdio>
#include <cstring>
#include <esp_log.h>
#include <Preferences.h>

TFT_eSPI tft = TFT_eSPI();

static char lcdActionMessage[32] = "System Initialized";
static SemaphoreHandle_t lcdMutex = NULL;
static uint32_t lcdMessageTimestamp = 0;

void LCDInit() {
  tft.begin();
  tft.setRotation(3); // 320x240 landscape (flipped)
  
  // Basic touch calibration if missing
  uint16_t calData[5];
  Preferences prefs;
  prefs.begin("peach_touch", false);
  if (prefs.getBytesLength("calData") == sizeof(calData)) {
    prefs.getBytes("calData", calData, sizeof(calData));
    tft.setTouch(calData);
  } else {
    // Run calibration
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.println("Touch corners to calibrate");
    tft.calibrateTouch(calData, TFT_WHITE, TFT_BLACK, 15);
    prefs.putBytes("calData", calData, sizeof(calData));
  }
  prefs.end();

  tft.fillScreen(TFT_BLACK);
  
  lcdMutex = xSemaphoreCreateMutex();
  if (lcdMutex == NULL) {
    ESP_LOGE("LCD", "Failed to create LCD string mutex");
  }
  
  // Static decorations
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("MOTOR 1", 80, 5, 2);
  tft.drawString("MOTOR 2", 240, 5, 2);
  tft.drawFastVLine(160, 0, 160, TFT_DARKGREY);
  tft.drawFastHLine(0, 155, 320, TFT_DARKGREY);
}

void LCD_setMessage(const char *msg) {
  if (lcdMutex != NULL && xSemaphoreTake(lcdMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    strncpy(lcdActionMessage, msg, sizeof(lcdActionMessage) - 1);
    lcdActionMessage[sizeof(lcdActionMessage) - 1] = '\0';
    lcdMessageTimestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
    xSemaphoreGive(lcdMutex);
  }
}

bool getTouchInput(uint16_t *x, uint16_t *y) {
  return tft.getTouch(x, y);
}

// Helper to draw a button without filling to prevent flicker
void drawButton(int x, int y, int w, int h, const char* label, bool isActive) {
  uint32_t color = isActive ? TFT_GREEN : TFT_BLUE;
  tft.drawRoundRect(x, y, w, h, 3, color);
  
  // Draw label with a padding string to clear old text cleanly
  char paddedLabel[16];
  snprintf(paddedLabel, sizeof(paddedLabel), " %-8s ", label);
  
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(paddedLabel, x + w/2, y + h/2, 2);
}

void draw_menu() {
  char localMsg[32] = "";
  uint32_t msgTime = 0;
  
  if (lcdMutex != NULL && xSemaphoreTake(lcdMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    strncpy(localMsg, lcdActionMessage, sizeof(localMsg) - 1);
    localMsg[sizeof(localMsg) - 1] = '\0';
    msgTime = lcdMessageTimestamp;
    xSemaphoreGive(lcdMutex);
  }
  
  // Draw Setpoints
  char buf[16];
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  
  snprintf(buf, sizeof(buf), "SPD: %-4d", systemState.motor1SpeedSetpoint);
  tft.drawString(buf, 80, 40, 2);
  
  snprintf(buf, sizeof(buf), "SPD: %-4d", systemState.motor2SpeedSetpoint);
  tft.drawString(buf, 240, 40, 2);

  // Buttons M1
  drawButton(10, 60, 60, 40, "-", false);
  drawButton(90, 60, 60, 40, "+", false);
  drawButton(10, 110, 140, 40, systemState.motor1Running ? "STOP M1" : "START M1", systemState.motor1Running);

  // Buttons M2
  drawButton(170, 60, 60, 40, "-", false);
  drawButton(250, 60, 60, 40, "+", false);
  drawButton(170, 110, 140, 40, systemState.motor2Running ? "STOP M2" : "START M2", systemState.motor2Running);

  // Global Buttons
  drawButton(10, 165, 145, 40, "START ALL", false);
  drawButton(165, 165, 145, 40, "STOP ALL", false);
  
  // Message Banner (at bottom)
  if (xTaskGetTickCount() * portTICK_PERIOD_MS - msgTime < 3000) {
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextDatum(ML_DATUM);
    char msgBuf[40];
    snprintf(msgBuf, sizeof(msgBuf), "> %-20s", localMsg);
    tft.drawString(msgBuf, 5, 227, 2);
  } else {
    // Clear banner if expired
    tft.drawString("                      ", 5, 227, 2);
  }
}