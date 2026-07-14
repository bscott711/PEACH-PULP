#include "controller.h"
#include "messaging.h"
#include "tasks/MotorNode.h"
#include "core/InputManager.h"
#include "core/StorageManager.h"
#include "core/UIData.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Global queue handles (declared extern in controller.h / messaging.h)
QueueHandle_t samplePumpCmdQueue;
QueueHandle_t samplePumpTelQueue;
QueueHandle_t dyePumpCmdQueue;
QueueHandle_t dyePumpTelQueue;
QueueHandle_t washPumpCmdQueue;
QueueHandle_t washPumpTelQueue;
QueueHandle_t lcdDataQueue;

// Global Node instances (defined in main.cpp, extern here)
extern MotorNode g_samplePumpNode;
extern MotorNode g_dyePumpNode;
extern MotorNode g_washPumpNode;

SemaphoreHandle_t systemStateMutex;
SemaphoreHandle_t encoderStateMutex;
EventGroupHandle_t controlEvents;

SystemState systemState;

static MotorNode* pumpNode(int idx) {
  switch (idx) {
    case PUMP_SAMPLE: return &g_samplePumpNode;
    case PUMP_DYE: return &g_dyePumpNode;
    default: return &g_washPumpNode;
  }
}

void initSystemState() {
  systemStateMutex = xSemaphoreCreateMutex();
  encoderStateMutex = xSemaphoreCreateMutex();
  controlEvents = xEventGroupCreate();

  StorageManager::init();

  if (xSemaphoreTake(systemStateMutex, portMAX_DELAY) == pdTRUE) {
    for (int i = 0; i < NUM_PUMPS; i++) {
      systemState.pumpSpeedPct[i] = StorageManager::loadPumpSpeed(i, 5);
      systemState.pumpManualRun[i] = false;
    }
    systemState.t1Seconds = StorageManager::loadT1(60);
    systemState.t2Seconds = StorageManager::loadT2(30);
    systemState.menuSel = MENU_T1;
    systemState.inEdit = false;
    xSemaphoreGive(systemStateMutex);
  }

  InputManager::init();
}

// ============================================================================
// Main Controller Task
// ============================================================================

// Debounced NVS writes for pump speeds — avoid flash wear on every detent.
static const uint32_t SPEED_SAVE_DEBOUNCE_MS = 2000;
static int lastSavedSpeed[NUM_PUMPS];
static uint32_t speedDirtySince[NUM_PUMPS] = {0, 0, 0};

void controller_task(void *pvParameters) {
  TickType_t lastWakeTime = xTaskGetTickCount();
  const TickType_t CONTROLLER_INTERVAL = pdMS_TO_TICKS(20);

  for (int i = 0; i < NUM_PUMPS; i++) {
    lastSavedSpeed[i] = systemState.pumpSpeedPct[i];
  }

  while (1) {
    InputManager::process();

    int speedPct[NUM_PUMPS];
    bool manualRun[NUM_PUMPS];
    if (xSemaphoreTake(systemStateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      for (int i = 0; i < NUM_PUMPS; i++) {
        speedPct[i] = systemState.pumpSpeedPct[i];
        manualRun[i] = systemState.pumpManualRun[i];
      }
      xSemaphoreGive(systemStateMutex);
    }

    // Apply speed commands to pumps (controller_task is the sole writer)
    // TODO(M6): replace manualRun-only gating with the two-phase protocol.
    for (int i = 0; i < NUM_PUMPS; i++) {
      int target = manualRun[i] ? (int)(speedPct[i] * MOTOR_SPEED_SCALE_FACTOR) : 0;
      pumpNode(i)->setSpeed(target);
    }

    // Debounced NVS persistence for pump speeds
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    for (int i = 0; i < NUM_PUMPS; i++) {
      if (speedPct[i] != lastSavedSpeed[i]) {
        if (speedDirtySince[i] == 0) {
          speedDirtySince[i] = now;
        } else if (now - speedDirtySince[i] >= SPEED_SAVE_DEBOUNCE_MS) {
          StorageManager::savePumpSpeed(i, speedPct[i]);
          lastSavedSpeed[i] = speedPct[i];
          speedDirtySince[i] = 0;
        }
      } else {
        speedDirtySince[i] = 0;
      }
    }

    // Publish UI snapshot
    UIData uiData;
    InputManager::populateUIData(uiData);
    if (lcdDataQueue != NULL) {
      xQueueOverwrite(lcdDataQueue, &uiData);
    }

    vTaskDelayUntil(&lastWakeTime, CONTROLLER_INTERVAL);
  }
}
