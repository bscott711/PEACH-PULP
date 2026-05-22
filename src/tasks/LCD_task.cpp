#include "tasks/LCD_task.h"

TaskHandle_t lcdTaskHandle = NULL;

void LCD_task(void *parameter) {
  int interval = *(int *)parameter;
  TickType_t lastWakeTime = xTaskGetTickCount();
  uint32_t loopCount = 0;

  Serial.println("[LCD_TASK] Started! Touch processing active.");

  while (1) {
    if (isOTA) {
      drawOTAScreen(otaProgress);
    } else {
      process_touch(); // Process touch events safely on the LCD thread
      draw_menu(); // Draw Screen
    }

    loopCount++;
    if (loopCount % 50 == 0) {
      Serial.printf("[LCD_TASK] heartbeat #%lu\n", loopCount);
    }

    // Wait until next interval mark
    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(interval));
  }
}