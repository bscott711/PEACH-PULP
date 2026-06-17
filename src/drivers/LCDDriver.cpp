#include "drivers/LCDDriver.h"
#include "messaging.h"
#include "ota_sprites.h"
#include <TJpg_Decoder.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include <cstdio>
#include <cstring>
#include <esp_log.h>
#include <Preferences.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>

#define TOUCH_CS   33
#define TOUCH_IRQ  36
#define TOUCH_CLK  25
#define TOUCH_MISO 39
#define TOUCH_MOSI 32

// Premium Theme Colors (RGB565)
#define COLOR_PEACH       0xFD67 // Vibrant peach/orange
#define COLOR_PEACH_LIGHT 0xFEB2 // Soft peach highlight
#define COLOR_WHITE       0xF7BE // Soft white text
#define COLOR_MUTED       0x8C51 // Muted steel gray text
#define COLOR_BORDER      0x3186 // Subtle dark border gray
#define COLOR_GOLD        0xFED0 // Warm crescent moon gold
#define COLOR_MINT        0x2690 // Premium mint green for active state

static SPIClass touchSPI(VSPI);
static XPT2046_Touchscreen touchscreen(TOUCH_CS); // Polling mode (no IRQ pin) for extreme reliability

TFT_eSPI tft = TFT_eSPI();

static char lcdActionMessage[32] = "System Initialized";
static SemaphoreHandle_t lcdMutex = NULL;
static uint32_t lcdMessageTimestamp = 0;

// Permanent premium dark mode colors (values are bitwise inverted for hardware TFT_INVERSION_ON)
uint16_t getBgColor() {
  return 0xF7BE; // Renders as deep dark charcoal/black
}

uint16_t getTextColor() {
  return 0x18C3; // Renders as crisp soft white
}

uint16_t getMutedColor() {
  return 0x632C; // Renders as muted steel gray
}

uint16_t getBorderColor() {
  return 0xC618; // Renders as subtle border gray
}

// Theme icon removed as we default exclusively to premium dark mode
void drawThemeIcon() {}

// Draw a premium speed bar centered on zero
void drawSpeedBar(int centerX, int y, int width, int setpoint, bool isRunning) {
  int halfW = width / 2;
  int startX = centerX - halfW;
  int targetX = centerX + (setpoint * halfW) / 100;
  uint16_t bg = getBgColor();
  
  // Clear speed bar region cleanly
  tft.fillRect(startX - 2, y - 2, width + 5, 12, bg);
  
  // Draw subtle background track line
  tft.drawFastHLine(startX, y + 2, width, getBorderColor());
  
  // Draw center zero tick mark
  tft.drawFastVLine(centerX, y, 5, getMutedColor());
  
  if (isRunning) {
    // Fill from center (0) to setpoint coordinate
    int fillX = std::min(centerX, targetX);
    int fillW = std::abs(targetX - centerX);
    if (fillW > 0) {
      tft.fillRect(fillX, y + 1, fillW, 3, COLOR_PEACH);
    }
  }
  
  // Draw the setpoint tick indicator
  tft.drawFastVLine(targetX, y - 1, 7, COLOR_GOLD);
}

// Callback for TJpg_Decoder
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (y >= tft.height()) return 0;
  // bitmap contains standard RGB565 from JPEG
  // The pushImage function expects standard RGB565 and will handle swapping/inverting if setup correctly
  tft.pushImage(x, y, w, h, bitmap);
  return 1;
}

void LCDInit() {
  tft.begin();
  tft.setRotation(3); // 320x240 landscape (flipped)

  // Initialize touch SPI and Touchscreen with dedicated SPI bus
  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touchscreen.begin(touchSPI);
  touchscreen.setRotation(3); // Match display rotation

  tft.fillScreen(getBgColor());
  
  // Initialize JPEG decoder
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true); // Needed for TFT_eSPI
  TJpgDec.setCallback(tft_output);

  lcdMutex = xSemaphoreCreateMutex();
  if (lcdMutex == NULL) {
    ESP_LOGE("LCD", "Failed to create LCD string mutex");
  }
}

void drawOTAScreen(int percent) {
    static int lastPercent = -1;
    static int lastFrame = -1;
    if (percent == lastPercent) return;

    // Clamp percentage
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    // Map progress to sprite frame index (0-10 for 0-99%, 11 for 100%)
    int frame;
    if (percent >= 100) {
        frame = OTA_SPRITE_COUNT - 1; // Final frame (ripe tree with fallen fruit)
    } else {
        frame = (percent * (OTA_SPRITE_COUNT - 1)) / 100;
    }

    // Display dimensions
    const int screenW = 320;
    const int screenH = 240;
    // Sprite is 320x232. Center it vertically.
    const int spriteX = (screenW - OTA_SPRITE_W) / 2;
    const int spriteY = (screenH - OTA_SPRITE_H) / 2;

    // Only redraw sprite when the frame actually changes
    if (frame != lastFrame) {
        // Clear screen on first draw or frame change
        if (lastFrame < 0) {
            tft.fillScreen(getBgColor());
        }

        // Decode JPEG and push to display
        const uint8_t* spritePtr = (const uint8_t*)pgm_read_ptr(&ota_sprites[frame]);
        uint32_t spriteLen = ota_sprite_lens[frame];
        TJpgDec.drawJpg(spriteX, spriteY, spritePtr, spriteLen);

        lastFrame = frame;
    }

    // Removed the state reset block so the screen doesn't tear by continuously redrawing 100%

    lastPercent = percent;
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

// Track which button is currently pressed (0 = none)
static int pressedButtonId = 0;

void executeButtonAction(int buttonId) {
  if (buttonId == 1) {
    if (systemState.motor1SpeedSetpoint > -100) systemState.motor1SpeedSetpoint--;
  } else if (buttonId == 2) {
    if (systemState.motor1SpeedSetpoint < 100) systemState.motor1SpeedSetpoint++;
  } else if (buttonId == 3) {
    systemState.motor1Running = !systemState.motor1Running;
    if (!systemState.motor1Running) systemState.motor1StopTick = 0; // Clear timer if stopped
    LCD_setMessage(systemState.motor1Running ? "M1 Started" : "M1 Stopped");
  }
  else if (buttonId == 4) {
    systemState.motor2SpeedSetpoint = std::max(-100, systemState.motor2SpeedSetpoint - 1);
    LCD_setMessage("M2 Speed -");
  }
  else if (buttonId == 5) {
    systemState.motor2SpeedSetpoint = std::min(100, systemState.motor2SpeedSetpoint + 1);
    LCD_setMessage("M2 Speed +");
  }
  else if (buttonId == 6) {
    systemState.motor2Running = !systemState.motor2Running;
    if (!systemState.motor2Running) systemState.motor2StopTick = 0; // Clear timer if stopped
    LCD_setMessage(systemState.motor2Running ? "M2 Started" : "M2 Stopped");
  }
  else if (buttonId == 7) {
    systemState.motor1Running = true;
    systemState.motor2Running = true;
    LCD_setMessage("All Started");
  }
  else if (buttonId == 8) {
    systemState.motor1Running = false;
    systemState.motor2Running = false;
    systemState.motor1StopTick = 0;
    systemState.motor2StopTick = 0;
    LCD_setMessage("All Stopped");
  }
  else if (buttonId == 12) {
    systemState.motor1Running = true;
    systemState.motor1StopTick = xTaskGetTickCount() + pdMS_TO_TICKS(60000);
    LCD_setMessage("M1 Run 1m");
  }
  else if (buttonId == 13) {
    systemState.motor2Running = true;
    systemState.motor2StopTick = xTaskGetTickCount() + pdMS_TO_TICKS(60000);
    LCD_setMessage("M2 Run 1m");
  }
  // Theme Toggling removed
}

void handleSliderTouch(int sliderId, int t_x) {
  if (sliderId == 10) {
    int dx = t_x - 80;
    int rawSetpoint = (dx * 100) / 70;
    int setpoint = std::max(-100, std::min(100, rawSetpoint));
    if (systemState.motor1SpeedSetpoint != setpoint) {
      systemState.motor1SpeedSetpoint = setpoint;
      LCD_setMessage("M1 Speed Set");
    }
  } else if (sliderId == 11) {
    int dx = t_x - 240;
    int rawSetpoint = (dx * 100) / 70;
    int setpoint = std::max(-100, std::min(100, rawSetpoint));
    if (systemState.motor2SpeedSetpoint != setpoint) {
      systemState.motor2SpeedSetpoint = setpoint;
      LCD_setMessage("M2 Speed Set");
    }
  }
}

void process_touch() {
  static int heldButtonId = 0;
  static uint32_t touchStartTime = 0;
  static uint32_t lastRepeatTime = 0;
  static uint32_t lastValidTouchTime = 0;
  static int lastValidButtonId = 0;

  uint16_t t_x = 0, t_y = 0;
  bool isTouched = touchscreen.touched();
  uint16_t raw_x = 0, raw_y = 0;
  int currentButtonId = 0;
  uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
  static uint16_t last_t_x = 0;

  if (isTouched) {
    TS_Point p = touchscreen.getPoint();
    raw_x = p.x;
    raw_y = p.y;
    
    // Map raw touch coordinates (approx 200 - 3800) to screen pixel dimensions (320 x 240)
    t_x = map(raw_x, 200, 3800, 0, 320);
    t_y = map(raw_y, 200, 3800, 0, 240);
    
    // Clamp to screen boundaries
    t_x = std::max((uint16_t)0, std::min((uint16_t)320, t_x));
    t_y = std::max((uint16_t)0, std::min((uint16_t)240, t_y));
    last_t_x = t_x;

    // Touch capturing: if dragging a slider, lock focus on it
    if (heldButtonId == 10 || heldButtonId == 11) {
      currentButtonId = heldButtonId;
    } else {
      // Determine if we are touching a button
      if (isPointInRect(t_x, t_y, 10, 60, 45, 40))         currentButtonId = 1; // M1 -
      else if (isPointInRect(t_x, t_y, 60, 60, 45, 40))    currentButtonId = 2; // M1 +
      else if (isPointInRect(t_x, t_y, 110, 60, 45, 40))   currentButtonId = 12; // M1 1m
      else if (isPointInRect(t_x, t_y, 10, 110, 140, 40))  currentButtonId = 3; // M1 Toggle
      else if (isPointInRect(t_x, t_y, 170, 60, 45, 40))   currentButtonId = 4; // M2 -
      else if (isPointInRect(t_x, t_y, 220, 60, 45, 40))   currentButtonId = 5; // M2 +
      else if (isPointInRect(t_x, t_y, 270, 60, 45, 40))   currentButtonId = 13; // M2 1m
      else if (isPointInRect(t_x, t_y, 170, 110, 140, 40)) currentButtonId = 6; // M2 Toggle
      else if (isPointInRect(t_x, t_y, 10, 165, 145, 40))  currentButtonId = 7; // START ALL
      else if (isPointInRect(t_x, t_y, 165, 165, 145, 40)) currentButtonId = 8; // STOP ALL
      else if (isPointInRect(t_x, t_y, 10, 25, 140, 33))   currentButtonId = 10; // M1 Slider (y = 25 to 58)
      else if (isPointInRect(t_x, t_y, 170, 25, 140, 33))  currentButtonId = 11; // M2 Slider (y = 25 to 58)
    }

    if (currentButtonId != 0) {
      lastValidButtonId = currentButtonId;
      lastValidTouchTime = now;
    }
  }

  // Apply debounce filtering: if we had a valid button press recently, maintain it
  int activeButtonId = 0;
  if (now - lastValidTouchTime < 150) {
    activeButtonId = lastValidButtonId;
  }

  // Update pressedButtonId for visual feedback (glow effect, hide on sliders)
  pressedButtonId = (activeButtonId != 10 && activeButtonId != 11) ? activeButtonId : 0;

  if (activeButtonId != 0) {
    if (activeButtonId != heldButtonId) {
      heldButtonId = activeButtonId;
      touchStartTime = now;
      lastRepeatTime = now;
      if (activeButtonId == 10 || activeButtonId == 11) {
        handleSliderTouch(activeButtonId, last_t_x);
      } else {
        executeButtonAction(activeButtonId);
      }
    } else {
      // Continuous holding for speed adjustments or sliding
      if (activeButtonId == 1 || activeButtonId == 2 || activeButtonId == 4 || activeButtonId == 5) {
        if (now - touchStartTime > 450) { // Initial repeat delay of 450ms
          if (now - lastRepeatTime > 120) { // Repeat speed increment every 120ms
            executeButtonAction(activeButtonId);
            lastRepeatTime = now;
          }
        }
      } else if (activeButtonId == 10 || activeButtonId == 11) {
        handleSliderTouch(activeButtonId, last_t_x);
      }
    }
  } else {
    heldButtonId = 0;
  }
}

// Darken a RGB565 color by shifting each channel right
static uint16_t darkenColor(uint16_t c) {
  uint16_t r = (c >> 11) & 0x1F;
  uint16_t g = (c >> 5)  & 0x3F;
  uint16_t b =  c        & 0x1F;
  return ((r >> 1) << 11) | ((g >> 1) << 5) | (b >> 1);
}

void drawButton(int x, int y, int w, int h, const char* label, bool isActive, bool pressed) {
  uint16_t borderColor = getBorderColor();
  uint16_t textColor = getTextColor();
  uint16_t fillColor = getBgColor();

  if (pressed) {
    // Button is pressed: glow orange/peach!
    borderColor = COLOR_PEACH;
    fillColor = COLOR_PEACH;
    textColor = TFT_BLACK; // Dark text on glowing peach background
  } else if (isActive) {
    // Button is active (e.g. Stop button when running): draw in vibrant mint green
    borderColor = COLOR_MINT;
    fillColor = 0xEE7D; // Swapped to account for TFT inversion
    textColor = COLOR_MINT;
  } else {
    // Default inactive button
    borderColor = getBorderColor();
    fillColor = getBgColor();
    textColor = getTextColor();
  }

  tft.fillRoundRect(x, y, w, h, 4, fillColor);
  tft.drawRoundRect(x, y, w, h, 4, borderColor);

  tft.setTextColor(textColor, fillColor);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(label, x + w/2, y + h/2, 2);
}

static void drawButtonIfChanged(int buttonId, int x, int y, int w, int h, const char* label, bool isActive, bool lastActive, bool firstDraw, int pressedButtonId, int lastPressedButtonId) {
  bool currentPressed = (pressedButtonId == buttonId);
  bool lastPressed = (lastPressedButtonId == buttonId);
  if (firstDraw || currentPressed != lastPressed || isActive != lastActive) {
    drawButton(x, y, w, h, label, isActive, currentPressed);
  }
}

void draw_menu() {
  static int lastMotor1Setpoint = -999;
  static int lastMotor2Setpoint = -999;
  static bool lastMotor1Running = false;
  static bool lastMotor2Running = false;
  static int lastPressedButtonId = -1;
  static char lastLcdMsg[32] = "";
  static bool lastMsgVisible = false;
  static bool firstDraw = true;

  // Speed bar state tracking
  static int lastBar1Setpoint = -999;
  static bool lastBar1Running = false;
  static int lastBar2Setpoint = -999;
  static bool lastBar2Running = false;

  char localMsg[32] = "";
  uint32_t msgTime = 0;
  
  if (lcdMutex != NULL && xSemaphoreTake(lcdMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    strncpy(localMsg, lcdActionMessage, sizeof(localMsg) - 1);
    localMsg[sizeof(localMsg) - 1] = '\0';
    msgTime = lcdMessageTimestamp;
    xSemaphoreGive(lcdMutex);
  }

  bool isMsgVisible = (xTaskGetTickCount() * portTICK_PERIOD_MS - msgTime < 3000);

  // Redraw static decorations and theme header if firstDraw or theme changes
  if (firstDraw) {
    tft.fillScreen(getBgColor()); // Clear screen with active theme color on initial draw (fixes startup color inversion)
    
    tft.setTextColor(getMutedColor(), getBgColor());
    tft.setTextDatum(TC_DATUM);
    tft.drawString("Sample", 80, 5, 2);
    tft.drawString("Sheath", 240, 5, 2);
    tft.drawFastVLine(160, 30, 125, getBorderColor()); // Starts below title, goes down to horizontal line
    tft.drawFastHLine(0, 155, 320, getBorderColor());
    
    tft.setTextColor(COLOR_PEACH, getBgColor());
    tft.setTextDatum(TC_DATUM);
    tft.drawString("PEACH PULP v5.0", 160, 5, 1);
  }

  // 1. Redraw setpoints if they changed
  if (firstDraw || systemState.motor1SpeedSetpoint != lastMotor1Setpoint) {
    char buf[16];
    tft.setTextColor(getTextColor(), getBgColor());
    tft.setTextDatum(MC_DATUM);
    snprintf(buf, sizeof(buf), "SPD: %-4d", systemState.motor1SpeedSetpoint);
    tft.drawString(buf, 80, 35, 2); // Raised slightly to fit speed bar below
    lastMotor1Setpoint = systemState.motor1SpeedSetpoint;
  }

  if (firstDraw || systemState.motor2SpeedSetpoint != lastMotor2Setpoint) {
    char buf[16];
    tft.setTextColor(getTextColor(), getBgColor());
    tft.setTextDatum(MC_DATUM);
    snprintf(buf, sizeof(buf), "SPD: %-4d", systemState.motor2SpeedSetpoint);
    tft.drawString(buf, 240, 35, 2); // Raised slightly to fit speed bar below
    lastMotor2Setpoint = systemState.motor2SpeedSetpoint;
  }

  // 2. Redraw Speed Bars if setpoint or running state changed (state-aware)
  if (firstDraw || systemState.motor1SpeedSetpoint != lastBar1Setpoint || systemState.motor1Running != lastBar1Running) {
    drawSpeedBar(80, 48, 140, systemState.motor1SpeedSetpoint, systemState.motor1Running);
    lastBar1Setpoint = systemState.motor1SpeedSetpoint;
    lastBar1Running = systemState.motor1Running;
  }

  if (firstDraw || systemState.motor2SpeedSetpoint != lastBar2Setpoint || systemState.motor2Running != lastBar2Running) {
    drawSpeedBar(240, 48, 140, systemState.motor2SpeedSetpoint, systemState.motor2Running);
    lastBar2Setpoint = systemState.motor2SpeedSetpoint;
    lastBar2Running = systemState.motor2Running;
  }

  // 3. Buttons M1
  static bool lastM1Timed = false;
  bool m1Timed = systemState.motor1Running && (systemState.motor1StopTick != 0);
  drawButtonIfChanged(1, 10, 60, 45, 40, "-", false, false, firstDraw, pressedButtonId, lastPressedButtonId);
  drawButtonIfChanged(2, 60, 60, 45, 40, "+", false, false, firstDraw, pressedButtonId, lastPressedButtonId);
  drawButtonIfChanged(12, 110, 60, 45, 40, "1m", m1Timed, lastM1Timed, firstDraw, pressedButtonId, lastPressedButtonId);
  drawButtonIfChanged(3, 10, 110, 140, 40, systemState.motor1Running ? "STOP SAMPLE" : "START SAMPLE", systemState.motor1Running, lastMotor1Running, firstDraw, pressedButtonId, lastPressedButtonId);

  // 4. Buttons M2
  static bool lastM2Timed = false;
  bool m2Timed = systemState.motor2Running && (systemState.motor2StopTick != 0);
  drawButtonIfChanged(4, 170, 60, 45, 40, "-", false, false, firstDraw, pressedButtonId, lastPressedButtonId);
  drawButtonIfChanged(5, 220, 60, 45, 40, "+", false, false, firstDraw, pressedButtonId, lastPressedButtonId);
  drawButtonIfChanged(13, 270, 60, 45, 40, "1m", m2Timed, lastM2Timed, firstDraw, pressedButtonId, lastPressedButtonId);
  drawButtonIfChanged(6, 170, 110, 140, 40, systemState.motor2Running ? "STOP SHEATH" : "START SHEATH", systemState.motor2Running, lastMotor2Running, firstDraw, pressedButtonId, lastPressedButtonId);

  // 5. Global Buttons
  drawButtonIfChanged(7, 10, 165, 145, 40, "START ALL", false, false, firstDraw, pressedButtonId, lastPressedButtonId);
  drawButtonIfChanged(8, 165, 165, 145, 40, "STOP ALL", false, false, firstDraw, pressedButtonId, lastPressedButtonId);

  // Update cached states
  lastMotor1Running = systemState.motor1Running;
  lastMotor2Running = systemState.motor2Running;
  lastM1Timed = m1Timed;
  lastM2Timed = m2Timed;
  lastPressedButtonId = pressedButtonId;

  // 6. Message Banner & Timer (at bottom)
  static char lastTimerStr[32] = "";
  char timerStr[32] = "";
  uint32_t now_ticks = xTaskGetTickCount();
  
  if (m1Timed || m2Timed) {
    int t1 = 0, t2 = 0;
    if (m1Timed && systemState.motor1StopTick > now_ticks) {
        t1 = (systemState.motor1StopTick - now_ticks) * portTICK_PERIOD_MS / 1000;
        t1 = std::max(0, 60 - t1);
    } else if (m1Timed) {
        t1 = 60;
    }
    
    if (m2Timed && systemState.motor2StopTick > now_ticks) {
        t2 = (systemState.motor2StopTick - now_ticks) * portTICK_PERIOD_MS / 1000;
        t2 = std::max(0, 60 - t2);
    } else if (m2Timed) {
        t2 = 60;
    }

    if (m1Timed && m2Timed) {
        snprintf(timerStr, sizeof(timerStr), "M1:%ds M2:%ds", t1, t2);
    } else if (m1Timed) {
        snprintf(timerStr, sizeof(timerStr), "Timer: %ds / 60s", t1);
    } else {
        snprintf(timerStr, sizeof(timerStr), "Timer: %ds / 60s", t2);
    }
  }

  uint16_t bg = getBgColor();

  // Left half: Message Banner
  if (firstDraw || isMsgVisible != lastMsgVisible || (isMsgVisible && strcmp(localMsg, lastLcdMsg) != 0)) {
    tft.fillRect(0, 215, 160, 25, bg); // Clear left half
    if (isMsgVisible) {
      tft.setTextColor(COLOR_PEACH, bg);
      tft.setTextDatum(ML_DATUM);
      char msgBuf[40];
      snprintf(msgBuf, sizeof(msgBuf), "> %s", localMsg);
      tft.drawString(msgBuf, 5, 227, 2);
      strcpy(lastLcdMsg, localMsg);
    } else {
      lastLcdMsg[0] = '\0';
    }
    lastMsgVisible = isMsgVisible;
  }

  // Right half: Timer Display
  if (firstDraw || strcmp(timerStr, lastTimerStr) != 0) {
    tft.fillRect(160, 215, 160, 25, bg); // Clear right half
    if (timerStr[0] != '\0') {
      tft.setTextColor(COLOR_GOLD, bg);
      tft.setTextDatum(MR_DATUM);
      tft.drawString(timerStr, 315, 227, 2);
    }
    strcpy(lastTimerStr, timerStr);
  }

  firstDraw = false;
}