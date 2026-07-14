#include "drivers/LCDDriver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <cstdio>
#include <cstring>
#include "core/NetworkManager.h"
#include "HardwareConfig.h"
#include <U8g2lib.h>
#include <SPI.h>
#include "ota_sprites.h"

/**
 * Mutex Lock Order Protocol (ALWAYS acquire in this order to prevent deadlock):
 * 1. lcdMutex          (LCD driver internal state message buffer)
 * 2. encoderStateMutex (g_encoderState - encoder input)
 * 3. systemStateMutex  (SystemState - UI-specific states ONLY)
 * Rule: Never hold a "lower" mutex while waiting for a "higher" one.
 * Rule: Keep critical sections short; copy data to local vars before releasing.
 * Note: Motion subsystem state is read lock-free via telemetry queues.
 */

// Instantiation for our LCD screen
U8G2_SSD1309_128X64_NONAME0_F_4W_HW_SPI u8g2(U8G2_R0, LCD_CS, LCD_DC, LCD_RESET);

// ============ Encapsulated LCD Globals ============
static char lcdActionMessage[32] = "";
static bool lcdMessagePending = false;
static uint32_t lcdMessageTimestamp = 0;
static uint32_t lcdBtnPressTime[4] = {0, 0, 0, 0};

// LCD-specific mutex for thread-safe message buffer access
static SemaphoreHandle_t lcdMutex = NULL;

static void draw_splashScreen() {
  const uint8_t* boot_sprites[6] = {
      ota_sprite_boot_0, ota_sprite_boot_1, ota_sprite_boot_2,
      ota_sprite_boot_3, ota_sprite_boot_4, ota_sprite_boot_5
  };

  const char* messages[6] = {
      "Initializing...",
      "Loading drivers...",
      "Starting modules...",
      "Configuring system...",
      "Almost there...",
      "Ready!"
  };
  for (int i = 0; i < 6; i++) {
    u8g2.clearBuffer();

    // Draw the image centered at the top
    u8g2.drawXBMP(24, 0, BOOT_SPRITE_W, BOOT_SPRITE_H, boot_sprites[i]);

    // Draw the corresponding status text underneath
    u8g2.setFont(u8g2_font_tiny5_tf);
    u8g2.drawStr(24, 55, messages[i]);

    u8g2.sendBuffer();

    // Wait 500ms (2 FPS)
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void draw_wifiStatus(const char* status, const char* ssid, int attempt, bool failed) {
  u8g2.clearBuffer();
  // Draw the final, static boot sprite centered at the top
  u8g2.drawXBMP(24, 0, BOOT_SPRITE_W, BOOT_SPRITE_H, ota_sprite_boot_5);
  // Stack the text beautifully below the sprite
  u8g2.setFont(u8g2_font_tiny5_tf);

  char ssidBuf[32];
  snprintf(ssidBuf, sizeof(ssidBuf), "SSID: %s", ssid);
  u8g2.drawStr(24, 50, ssidBuf);

  if (failed) {
    u8g2.drawStr(24, 60, "Rebooting in 5s...");
  } else {
    char animBuf[32];
    char dots[5] = "";
    int dotCount = (attempt % 4);
    for (int j = 0; j < dotCount; j++) {
      strcat(dots, ".");
    }
    snprintf(animBuf, sizeof(animBuf), "%s%s", status, dots);
    u8g2.drawStr(24, 60, animBuf);
  }

  u8g2.sendBuffer();
}

void draw_otaScreen() {
  int downloadProgressPercent = NetworkManager::getOTAProgress();
  static int lastFrame = -1;
  // Clamp percentage
  if (downloadProgressPercent < 0) downloadProgressPercent = 0;
  if (downloadProgressPercent > 100) downloadProgressPercent = 100;
  // Map 0-100% to frame indexes 0-11
  int frame = (downloadProgressPercent >= 100) ?
              (OTA_SPRITE_COUNT - 1) :
              (downloadProgressPercent * (OTA_SPRITE_COUNT - 1)) / 100;
  // Render ONLY if the animation frame has advanced
  if (frame != lastFrame) {
      u8g2.clearBuffer();
      // Fetch the array pointer from flash memory
      const uint8_t* spritePtr = (const uint8_t*)pgm_read_ptr(&ota_sprites[frame]);

      // Draw the full-screen frame
      u8g2.drawXBMP(0, 0, OTA_SPRITE_W, OTA_SPRITE_H, spritePtr);

      u8g2.sendBuffer();
      lastFrame = frame;
  }
}

void LCDInit() {
  SPI.begin(LCD_SCK, -1, LCD_MOSI, -1); // Prevent SPI from taking over pin 19 (MISO) which we use for CS
  u8g2.begin();
  u8g2.setBusClock(4000000); // Lower SPI speed to 4MHz to prevent screen tearing
  u8g2.sendF("ca", 0xd5, 0xf0); // Maximize internal oscillator freq to fix camera flicker
  u8g2.setFont(u8g2_font_tiny5_tf);

  lcdMutex = xSemaphoreCreateMutex();
  if (lcdMutex == NULL) {
    PEACH_LOGE("LCD", "Failed to create LCD string mutex");
  }

  // Show splash screen for 2.5 seconds
  draw_splashScreen();
  vTaskDelay(pdMS_TO_TICKS(2500));

  // Restore the small UI font
  u8g2.setFont(u8g2_font_tiny5_tf);
}

void LCD_setMessage(const char *msg) {
  if (lcdMutex != NULL &&
      xSemaphoreTake(lcdMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    strncpy(lcdActionMessage, msg, sizeof(lcdActionMessage) - 1);
    lcdActionMessage[sizeof(lcdActionMessage) - 1] = '\0';
    lcdMessageTimestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
    lcdMessagePending = true;
    xSemaphoreGive(lcdMutex);
  } else {
    PEACH_LOGW("LCD", "Mutex timeout setting message");
  }
}

void LCD_notifyButtonPress(int index) {
  if (index >= 0 && index < 4) {
    if (lcdMutex != NULL &&
        xSemaphoreTake(lcdMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      lcdBtnPressTime[index] = xTaskGetTickCount() * portTICK_PERIOD_MS;
      xSemaphoreGive(lcdMutex);
    }
  }
}

// ============ Drawing Helpers ============

static void draw_displayTimer() {
  uint32_t t = (xTaskGetTickCount() * portTICK_PERIOD_MS) / 1000;
  uint32_t s = t % 60;
  uint32_t total_m = t / 60;
  uint32_t m = total_m % 60;
  uint32_t h = total_m / 60;
  uint32_t d = h / 24;
  h = h % 24;

  char timerBuffer[32];
  if (d > 0) {
      snprintf(timerBuffer, sizeof(timerBuffer), "RUN:%lu-%02lu:%02lu:%02lu", (unsigned long)d, (unsigned long)h, (unsigned long)m, (unsigned long)s);
  } else if (h > 0) {
      snprintf(timerBuffer, sizeof(timerBuffer), "RUN:%lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m, (unsigned long)s);
  } else {
      snprintf(timerBuffer, sizeof(timerBuffer), "RUN:%02lu:%02lu", (unsigned long)m, (unsigned long)s);
  }
  u8g2.drawStr(0, 6, timerBuffer);
}

static void draw_buttonStatus() {
  uint32_t localBtnTime[4] = {0};

  if (lcdMutex != NULL &&
      xSemaphoreTake(lcdMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    for (int i = 0; i < 4; i++) {
      localBtnTime[i] = lcdBtnPressTime[i];
    }
    xSemaphoreGive(lcdMutex);
  }

  uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

  for (int i = 0; i < 4; i++) {
    bool recentlyPressed = (now - localBtnTime[i] < 200);

    if (recentlyPressed) {
      u8g2.drawDisc(100 + (i * 6), 4, 2); // filled = pressed
    } else {
      u8g2.drawCircle(100 + (i * 6), 4, 2); // outline = unpressed
    }
  }
}

static void draw_actionMessage() {
  bool pending = false;
  char localMsg[32] = "";
  uint32_t timestamp = 0;

  if (lcdMutex != NULL &&
      xSemaphoreTake(lcdMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    pending = lcdMessagePending;
    timestamp = lcdMessageTimestamp;
    if (pending) {
      strncpy(localMsg, lcdActionMessage, sizeof(localMsg) - 1);
      localMsg[sizeof(localMsg) - 1] = '\0';
    }
    xSemaphoreGive(lcdMutex);
  }

  uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

  // Show message for 2.5 seconds after being set
  if (pending && (now - timestamp < 2500)) {
    char actionBuffer[48];
    snprintf(actionBuffer, sizeof(actionBuffer), "> %s", localMsg);
    u8g2.drawStr(0, 60, actionBuffer);
  } else if (pending) {
    // Auto-expire the flag
    if (lcdMutex != NULL &&
        xSemaphoreTake(lcdMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      lcdMessagePending = false;
      xSemaphoreGive(lcdMutex);
    }
  }
}

static void draw_wifiIndicator(bool connected) {
  if (connected) {
    u8g2.drawStr(123, 6, "W");
  }
}

static const char* kPumpLabels[NUM_PUMPS] = {"SMP", "DYE", "WSH"};

static void draw_pumpRow(int y, int idx, const UIData& data) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%s %3d%% %s", kPumpLabels[idx], data.pumpSpeedPct[idx],
           data.pumpRunning[idx] ? "RUN" : "---");
  u8g2.drawStr(0, y, buf);
}

static void draw_menuRow(const UIData& data) {
  char buf[32];
  char t1Buf[14];
  char t2Buf[14];
  snprintf(t1Buf, sizeof(t1Buf), "T1:%lus", (unsigned long)data.t1S);
  snprintf(t2Buf, sizeof(t2Buf), "T2:%lus", (unsigned long)data.t2S);

  const char* startMarker = (data.menuSel == MENU_START) ? ">" : " ";
  const char* t1Marker = (data.menuSel == MENU_T1) ? (data.inEdit ? "*" : ">") : " ";
  const char* t2Marker = (data.menuSel == MENU_T2) ? (data.inEdit ? "*" : ">") : " ";

  snprintf(buf, sizeof(buf), "%sSTART %s%s %s%s", startMarker, t1Marker, t1Buf, t2Marker, t2Buf);
  u8g2.drawStr(0, 47, buf);
}

// Progress bar + mm:ss countdown for the currently running protocol phase.
static void draw_protocolRow(const UIData& data) {
  uint32_t total = (data.phase == PROTO_PHASE1) ? data.t1S : data.t2S;
  uint32_t remaining = data.phaseRemainingS;
  uint32_t elapsed = (total > remaining) ? (total - remaining) : 0;
  int segs = (total > 0) ? (int)((elapsed * 10) / total) : 0;
  segs = constrain(segs, 0, 10);

  char bar[11];
  for (int i = 0; i < 10; i++) bar[i] = (i < segs) ? '#' : '-';
  bar[10] = '\0';

  char buf[32];
  snprintf(buf, sizeof(buf), "P%d %lu:%02lu [%s]",
           (data.phase == PROTO_PHASE1) ? 1 : 2,
           (unsigned long)(remaining / 60), (unsigned long)(remaining % 60), bar);
  u8g2.drawStr(0, 47, buf);
}

// ============ Main Draw Function ============

void draw_menu(const UIData& data) {
  u8g2.clearBuffer();

  draw_displayTimer();
  draw_buttonStatus();
  draw_wifiIndicator(data.wifiConnected);
  u8g2.drawHLine(0, 9, 128);

  draw_pumpRow(18, PUMP_SAMPLE, data);
  draw_pumpRow(26, PUMP_DYE, data);
  draw_pumpRow(34, PUMP_WASH, data);
  u8g2.drawHLine(0, 38, 128);

  if (data.phase != PROTO_IDLE) {
    draw_protocolRow(data);
  } else {
    draw_menuRow(data);
  }
  u8g2.drawHLine(0, 51, 128);

  draw_actionMessage();

  u8g2.sendBuffer();
}
