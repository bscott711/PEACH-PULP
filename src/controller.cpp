#include "controller.h"
#include "messaging.h"
#include "tasks/MotorNode.h"
#include "drivers/LCDDriver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>

// Global queue handles (declared extern in controller.h)
QueueHandle_t samplePumpCmdQueue;
QueueHandle_t samplePumpTelQueue;
QueueHandle_t dyePumpCmdQueue;
QueueHandle_t dyePumpTelQueue;
QueueHandle_t washPumpCmdQueue;
QueueHandle_t washPumpTelQueue;

// Global Node instances (defined in main.cpp, extern here)
extern MotorNode g_samplePumpNode;
extern MotorNode g_dyePumpNode;
extern MotorNode g_washPumpNode;

SemaphoreHandle_t systemStateMutex;
SemaphoreHandle_t encoderStateMutex;
EventGroupHandle_t controlEvents;

SystemState systemState = {.mode = IDLE,
                           .collisionDetected = false,
                           .collisionTimestamp = 0,
                           .motor1SpeedSetpoint = 5,
                           .motor2SpeedSetpoint = 5,
                           .motor1Running = false,
                           .motor2Running = false,
                           .motor1Enabled = true,
                           .motor2Enabled = true};

void initSystemState() {
  systemStateMutex = xSemaphoreCreateMutex();
  encoderStateMutex = xSemaphoreCreateMutex();
  controlEvents = xEventGroupCreate();

  if (xSemaphoreTake(systemStateMutex, portMAX_DELAY) == pdTRUE) {
    systemState.mode = IDLE;
    systemState.collisionDetected = false;
    systemState.motor1StopTick = 0;
    systemState.motor2StopTick = 0;
    systemState.motor1Enabled = true;
    systemState.motor2Enabled = true;
    xSemaphoreGive(systemStateMutex);
  }
}

// ============================================================================
// Main Controller Task
// ============================================================================



void controller_task(void *pvParameters) {
  TickType_t lastWakeTime = xTaskGetTickCount();
  const TickType_t CONTROLLER_INTERVAL = pdMS_TO_TICKS(50);

  while (1) {
    // Check timers for timed runs
    if (systemState.motor1Running && systemState.motor1StopTick != 0) {
        if (xTaskGetTickCount() >= systemState.motor1StopTick) {
            systemState.motor1Running = false;
            systemState.motor1StopTick = 0;
        }
    }
    if (systemState.motor2Running && systemState.motor2StopTick != 0) {
        if (xTaskGetTickCount() >= systemState.motor2StopTick) {
            systemState.motor2Running = false;
            systemState.motor2StopTick = 0;
        }
    }

    // Apply speed commands to pumps
    // TODO(M5): replace with the pumpSpeedPct[3] / two-phase protocol model.
    int sampleTarget = systemState.motor1Running ? systemState.motor1SpeedSetpoint * MOTOR_SPEED_SCALE_FACTOR : 0;
    int dyeTarget = systemState.motor2Running ? systemState.motor2SpeedSetpoint * MOTOR_SPEED_SCALE_FACTOR : 0;

    g_samplePumpNode.setSpeed(sampleTarget);
    g_dyePumpNode.setSpeed(dyeTarget);
    g_washPumpNode.setSpeed(0);

    vTaskDelayUntil(&lastWakeTime, CONTROLLER_INTERVAL);
  }
}
