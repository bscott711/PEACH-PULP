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
  
  // Apply standard touch calibration values to bypass the manual setup screen
  uint16_t calData[5] = {300, 3600, 300, 3600, 1};
  tft.setTouch(calData);

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

void drawOTAScreen(int percent) {
  static int lastPercent = -1;
  if (percent == lastPercent) return;
  lastPercent = percent;

  if (percent == 0) {
      tft.fillScreen(0x10A2); // COLOR_BG
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(0xFD67); // COLOR_PEACH
      tft.drawString("OTA UPDATE IN PROGRESS", tft.width()/2, tft.height()*0.33, 4);
      tft.setTextColor(0x8C51); // COLOR_TEXT_MUTED
      tft.drawString("Receiving new firmware...", tft.width()/2, tft.height()*0.33 + 35, 2);
  } else if (percent == 100) {
      tft.fillScreen(0x10A2);
      tft.setTextColor(0xFD67);
      tft.drawString("UPDATE COMPLETE", tft.width()/2, tft.height()/2, 4);
      return;
  }
  
  tft.fillRect(0, tft.height() - 72, tft.width(), 40, 0x10A2);
  tft.setTextColor(0xF7BE); // COLOR_TEXT_WHITE
  tft.drawString(String(percent) + "% Completed", tft.width()/2, tft.height() - 62, 2);
}

void LCD_setMessage(const char *msg) {
  if (lcdMutex != NULL && xSemaphoreTake(lcdMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    strncpy(lcdActionMessage, msg, sizeof(lcdActionMessage) - 1);
    lcdActionMessage[sizeof(lcdActionMessage) - 1] = '\0';
    lcdMessageTimestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
    xSemaphoreGive(lcdMutex);
  }
}

bool isPointInRect(uint16_t x, uint16_t y, uint16_t rx, uint16_t ry, uint16_t rw, uint16_t rh) {
  return (x >= rx && x <= rx + rw && y >= ry && y <= ry + rh);
}

void process_touch() {
  static bool wasTouched = false;
  static uint32_t lastTouchTime = 0;
  uint16_t t_x = 0, t_y = 0;
  
  bool isTouched = tft.getTouch(&t_x, &t_y);
  uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

  if (isTouched && !wasTouched && (now - lastTouchTime > 200)) {
    lastTouchTime = now;
    wasTouched = true;
    
    // M1 -
    if (isPointInRect(t_x, t_y, 10, 60, 60, 40)) {
      systemState.motor1SpeedSetpoint = std::max(0, systemState.motor1SpeedSetpoint - 10);
      LCD_setMessage("M1 Speed -");
    }
    // M1 +
    else if (isPointInRect(t_x, t_y, 90, 60, 60, 40)) {
      systemState.motor1SpeedSetpoint = std::min(100, systemState.motor1SpeedSetpoint + 10);
      LCD_setMessage("M1 Speed +");
    }
    // M1 Toggle
    else if (isPointInRect(t_x, t_y, 10, 110, 140, 40)) {
      systemState.motor1Running = !systemState.motor1Running;
      LCD_setMessage(systemState.motor1Running ? "M1 Started" : "M1 Stopped");
    }
    
    // M2 -
    else if (isPointInRect(t_x, t_y, 170, 60, 60, 40)) {
      systemState.motor2SpeedSetpoint = std::max(0, systemState.motor2SpeedSetpoint - 10);
      LCD_setMessage("M2 Speed -");
    }
    // M2 +
    else if (isPointInRect(t_x, t_y, 250, 60, 60, 40)) {
      systemState.motor2SpeedSetpoint = std::min(100, systemState.motor2SpeedSetpoint + 10);
      LCD_setMessage("M2 Speed +");
    }
    // M2 Toggle
    else if (isPointInRect(t_x, t_y, 170, 110, 140, 40)) {
      systemState.motor2Running = !systemState.motor2Running;
      LCD_setMessage(systemState.motor2Running ? "M2 Started" : "M2 Stopped");
    }
    
    // START ALL
    else if (isPointInRect(t_x, t_y, 10, 165, 145, 40)) {
      systemState.motor1Running = true;
      systemState.motor2Running = true;
      LCD_setMessage("All Started");
    }
    // STOP ALL
    else if (isPointInRect(t_x, t_y, 165, 165, 145, 40)) {
      systemState.motor1Running = false;
      systemState.motor2Running = false;
      LCD_setMessage("All Stopped");
    }
  } else if (!isTouched) {
    wasTouched = false;
  }
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