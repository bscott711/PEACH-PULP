#include "drivers/LCDDriver.h"
#include <esp_log.h>

// Milestone-1 placeholder: just logs the message. Milestone 2 replaces this
// file with the full OLED driver (see header comment).

void LCD_setMessage(const char *msg) {
  ESP_LOGI("LCD", "%s", msg);
}

void LCD_notifyButtonPress(int index) {
  (void)index;
}
