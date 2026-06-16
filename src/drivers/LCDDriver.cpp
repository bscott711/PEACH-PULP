#include "drivers/LCDDriver.h"
#include "messaging.h"
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
  int targetX = centerX + (setpoint * halfW) / 10;
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



void LCDInit() {
  tft.begin();
  tft.setRotation(3); // 320x240 landscape (flipped)

  // Initialize touch SPI and Touchscreen with dedicated SPI bus
  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touchscreen.begin(touchSPI);
  touchscreen.setRotation(3); // Match display rotation

  tft.fillScreen(getBgColor());
  
  lcdMutex = xSemaphoreCreateMutex();
  if (lcdMutex == NULL) {
    ESP_LOGE("LCD", "Failed to create LCD string mutex");
  }
}



float constrainFloat(float val, float min_val, float max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

void drawOTAScreen(int percent) {
    static int lastPercent = -1;
    if (percent == lastPercent) return;
    
    static float last_p = -1.0;
    static int last_peach_y = 80;
    static int last_peach_x = 190;

    float p = (float)percent / 100.0f;
    p = constrainFloat(p, 0.0, 1.0);

    // RGB565 Colors (Standard, no inversion)
    uint16_t TREE_COLOR_SKY = tft.color565(135, 206, 235);
    uint16_t TREE_COLOR_GRASS = tft.color565(34, 139, 34);
    uint16_t TREE_COLOR_TRUNK = tft.color565(101, 67, 33);
    uint16_t TREE_COLOR_LEAF = tft.color565(34, 139, 34);
    uint16_t TREE_COLOR_FLOWER = tft.color565(255, 182, 193);
    uint16_t TREE_COLOR_PEACH = tft.color565(255, 140, 0);

    // Initial background generation
    if (last_p < 0.0 || p < last_p) {
        tft.fillScreen(TREE_COLOR_SKY);
        tft.fillRect(0, 200, 320, 40, TREE_COLOR_GRASS);
        last_p = 0.0;
        last_peach_y = 80;
        last_peach_x = 190;
    }

    // 1. Trunk (0% - 15%)
    float trunk_p = constrainFloat((p - 0.0) / 0.15, 0.0, 1.0);
    if (trunk_p > 0) {
        int h = trunk_p * 80;
        tft.fillRect(150, 200 - h, 20, h, TREE_COLOR_TRUNK);
    }

    // 2. Limbs (15% - 30%)
    float limb_p = constrainFloat((p - 0.15) / 0.15, 0.0, 1.0);
    if (limb_p > 0) {
        int endX1 = 160 - (limb_p * 40);
        int endY1 = 150 - (limb_p * 50);
        tft.drawLine(160, 150, endX1, endY1, TREE_COLOR_TRUNK);
        tft.drawLine(159, 150, endX1 - 1, endY1, TREE_COLOR_TRUNK);

        int endX2 = 160 + (limb_p * 50);
        int endY2 = 140 - (limb_p * 50);
        tft.drawLine(160, 140, endX2, endY2, TREE_COLOR_TRUNK);
        tft.drawLine(161, 140, endX2 + 1, endY2, TREE_COLOR_TRUNK);

        int endY3 = 120 - (limb_p * 60);
        tft.drawLine(160, 120, 160, endY3, TREE_COLOR_TRUNK);
        tft.drawLine(159, 120, 159, endY3, TREE_COLOR_TRUNK);
        tft.drawLine(161, 120, 161, endY3, TREE_COLOR_TRUNK);
    }

    // 3. Leaves (30% - 50%)
    float leaf_p = constrainFloat((p - 0.30) / 0.20, 0.0, 1.0);
    if (leaf_p > 0) {
        int r = leaf_p * 25;
        tft.fillCircle(120, 100, r, TREE_COLOR_LEAF);
        tft.fillCircle(210, 90, r, TREE_COLOR_LEAF);
        tft.fillCircle(160, 60, r, TREE_COLOR_LEAF);
        tft.fillCircle(160, 100, r + 5, TREE_COLOR_LEAF);
    }

    // 4. Flowers (50% - 70%)
    float flower_p = constrainFloat((p - 0.50) / 0.20, 0.0, 1.0);
    if (flower_p > 0) {
        int r = flower_p * 4;
        tft.fillCircle(110, 95, r, TREE_COLOR_FLOWER);
        tft.fillCircle(130, 110, r, TREE_COLOR_FLOWER);
        tft.fillCircle(200, 85, r, TREE_COLOR_FLOWER);
        tft.fillCircle(220, 100, r, TREE_COLOR_FLOWER);
        tft.fillCircle(150, 55, r, TREE_COLOR_FLOWER);
        tft.fillCircle(170, 70, r, TREE_COLOR_FLOWER);
        tft.fillCircle(160, 110, r, TREE_COLOR_FLOWER);
    }

    // 5. Peaches (70% - 85%)
    float peach_p = constrainFloat((p - 0.70) / 0.15, 0.0, 1.0);
    if (peach_p > 0) {
        int r = peach_p * 8;
        tft.fillCircle(110, 110, r, TREE_COLOR_PEACH);
        tft.fillCircle(200, 100, r, TREE_COLOR_PEACH);
        if (p <= 0.85) {
            tft.fillCircle(190, 80, r, TREE_COLOR_PEACH); // Static prior to falling
        }
    }

    // 6. Falling Peach (85% - 100%)
    float fall_p = constrainFloat((p - 0.85) / 0.15, 0.0, 1.0);
    if (fall_p > 0) {
        int current_y = 80;
        int current_x = 190;

        // Bounce physics
        if (fall_p <= 0.5) {
            float t = fall_p / 0.5;
            current_y = 80 + (192 - 80) * (t * t);
        } else if (fall_p <= 0.75) {
            float t = (fall_p - 0.5) / 0.25;
            current_y = 168 * ((t - 0.5) * (t - 0.5)) + 150;
            current_x = 190 + (t * 15);
        } else {
            float t = (fall_p - 0.75) / 0.25;
            current_y = 88 * ((t - 0.5) * (t - 0.5)) + 170;
            current_x = 205 + (t * 10);
        }

        int last_x = last_peach_x;
        int last_y = last_peach_y;

        if (current_y != last_y || current_x != last_x) {
            // Erase previous bounding box
            tft.fillRect(last_x - 8, last_y - 8, 17, 17, TREE_COLOR_SKY);
            
            // Redraw grass and leaf overlaps if necessary
            if (last_y + 8 >= 200) {
                tft.fillRect(last_x - 8, 200, 17, (last_y + 8) - 199, TREE_COLOR_GRASS);
            }
            if (last_y < 115 && last_x > 180) {
                tft.fillCircle(210, 90, 25, TREE_COLOR_LEAF);
                tft.fillCircle(200, 85, 4, TREE_COLOR_FLOWER);
                tft.fillCircle(220, 100, 4, TREE_COLOR_FLOWER);
                tft.fillCircle(200, 100, 8, TREE_COLOR_PEACH);
            }

            // Draw at new position
            tft.fillCircle(current_x, current_y, 8, TREE_COLOR_PEACH);
            last_peach_y = current_y;
            last_peach_x = current_x;
        }
    }

    last_p = p;
    if (percent >= 100) {
        // Draw graceful completion text so user knows it is rebooting
        tft.fillRoundRect(20, 20, 280, 80, 8, getBgColor());
        tft.drawRoundRect(20, 20, 280, 80, 8, COLOR_PEACH);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(COLOR_PEACH);
        tft.drawString("UPDATE COMPLETE", 160, 45, 4);
        tft.setTextColor(getTextColor());
        tft.drawString("Rebooting...", 160, 75, 2);
        
        last_p = -1.0; // Reset state mapping
    }
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
    systemState.motor1SpeedSetpoint = std::max(-10, systemState.motor1SpeedSetpoint - 1);
    LCD_setMessage("M1 Speed -");
  }
  else if (buttonId == 2) {
    systemState.motor1SpeedSetpoint = std::min(10, systemState.motor1SpeedSetpoint + 1);
    LCD_setMessage("M1 Speed +");
  }
  else if (buttonId == 3) {
    systemState.motor1Running = !systemState.motor1Running;
    LCD_setMessage(systemState.motor1Running ? "M1 Started" : "M1 Stopped");
  }
  else if (buttonId == 4) {
    systemState.motor2SpeedSetpoint = std::max(-10, systemState.motor2SpeedSetpoint - 1);
    LCD_setMessage("M2 Speed -");
  }
  else if (buttonId == 5) {
    systemState.motor2SpeedSetpoint = std::min(10, systemState.motor2SpeedSetpoint + 1);
    LCD_setMessage("M2 Speed +");
  }
  else if (buttonId == 6) {
    systemState.motor2Running = !systemState.motor2Running;
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
    LCD_setMessage("All Stopped");
  }
  // Theme Toggling removed
}

void handleSliderTouch(int sliderId, int t_x) {
  if (sliderId == 10) {
    int dx = t_x - 80;
    int rawSetpoint = (dx * 10) / 70;
    int setpoint = std::max(-10, std::min(10, rawSetpoint));
    if (systemState.motor1SpeedSetpoint != setpoint) {
      systemState.motor1SpeedSetpoint = setpoint;
      LCD_setMessage("M1 Speed Set");
    }
  } else if (sliderId == 11) {
    int dx = t_x - 240;
    int rawSetpoint = (dx * 10) / 70;
    int setpoint = std::max(-10, std::min(10, rawSetpoint));
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

    // Touch capturing: if dragging a slider, lock focus on it
    if (heldButtonId == 10 || heldButtonId == 11) {
      currentButtonId = heldButtonId;
    } else {
      // Determine if we are touching a button
      if (isPointInRect(t_x, t_y, 10, 60, 60, 40))         currentButtonId = 1; // M1 -
      else if (isPointInRect(t_x, t_y, 90, 60, 60, 40))    currentButtonId = 2; // M1 +
      else if (isPointInRect(t_x, t_y, 10, 110, 140, 40))  currentButtonId = 3; // M1 Toggle
      else if (isPointInRect(t_x, t_y, 170, 60, 60, 40))   currentButtonId = 4; // M2 -
      else if (isPointInRect(t_x, t_y, 250, 60, 60, 40))   currentButtonId = 5; // M2 +
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
  pressedButtonId = (activeButtonId < 10) ? activeButtonId : 0;

  if (activeButtonId != 0) {
    if (activeButtonId != heldButtonId) {
      heldButtonId = activeButtonId;
      touchStartTime = now;
      lastRepeatTime = now;
      if (activeButtonId >= 10) {
        handleSliderTouch(activeButtonId, t_x);
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
      } else if (activeButtonId >= 10) {
        handleSliderTouch(activeButtonId, t_x);
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

  // Draw label with a padding string to clear old text cleanly
  char paddedLabel[16];
  snprintf(paddedLabel, sizeof(paddedLabel), " %-8s ", label);

  tft.setTextColor(textColor, fillColor);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(paddedLabel, x + w/2, y + h/2, 2);
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
    tft.drawString("MOTOR 1", 80, 5, 2);
    tft.drawString("MOTOR 2", 240, 5, 2);
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
  drawButtonIfChanged(1, 10, 60, 60, 40, "-", false, false, firstDraw, pressedButtonId, lastPressedButtonId);
  drawButtonIfChanged(2, 90, 60, 60, 40, "+", false, false, firstDraw, pressedButtonId, lastPressedButtonId);
  drawButtonIfChanged(3, 10, 110, 140, 40, systemState.motor1Running ? "STOP M1" : "START M1", systemState.motor1Running, lastMotor1Running, firstDraw, pressedButtonId, lastPressedButtonId);

  // 4. Buttons M2
  drawButtonIfChanged(4, 170, 60, 60, 40, "-", false, false, firstDraw, pressedButtonId, lastPressedButtonId);
  drawButtonIfChanged(5, 250, 60, 60, 40, "+", false, false, firstDraw, pressedButtonId, lastPressedButtonId);
  drawButtonIfChanged(6, 170, 110, 140, 40, systemState.motor2Running ? "STOP M2" : "START M2", systemState.motor2Running, lastMotor2Running, firstDraw, pressedButtonId, lastPressedButtonId);

  // 5. Global Buttons
  drawButtonIfChanged(7, 10, 165, 145, 40, "START ALL", false, false, firstDraw, pressedButtonId, lastPressedButtonId);
  drawButtonIfChanged(8, 165, 165, 145, 40, "STOP ALL", false, false, firstDraw, pressedButtonId, lastPressedButtonId);

  // Update cached states
  lastMotor1Running = systemState.motor1Running;
  lastMotor2Running = systemState.motor2Running;
  lastPressedButtonId = pressedButtonId;

  // 6. Message Banner (at bottom)
  if (firstDraw || isMsgVisible != lastMsgVisible || (isMsgVisible && strcmp(localMsg, lastLcdMsg) != 0)) {
    uint16_t bg = getBgColor();
    if (isMsgVisible) {
      tft.setTextColor(COLOR_PEACH, bg);
      tft.setTextDatum(ML_DATUM);
      char msgBuf[40];
      snprintf(msgBuf, sizeof(msgBuf), "> %-20s", localMsg);
      tft.drawString(msgBuf, 5, 227, 2);
      strcpy(lastLcdMsg, localMsg);
    } else {
      // Clear banner if expired
      tft.fillRect(0, 215, 320, 25, bg);
      lastLcdMsg[0] = '\0';
    }
    lastMsgVisible = isMsgVisible;
  }

  firstDraw = false;
}