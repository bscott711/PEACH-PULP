#include "controller.h"
#include "messaging.h"
#include "tasks/MotorNode.h"
#include "core/InputManager.h"
#include "core/StorageManager.h"
#include "core/NetworkManager.h"
#include "core/UIData.h"
#include "drivers/LCDDriver.h"
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
    systemState.menuSel = MENU_START;
    systemState.inEdit = false;
    systemState.protocolPhase = PROTO_IDLE;
    systemState.phaseEndTick = 0;
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

    TickType_t now = xTaskGetTickCount();
    bool estopRequested = (xEventGroupGetBits(controlEvents) & BIT_ESTOP_REQUEST) != 0;

    UIData uiData = {};
    int targetPct[NUM_PUMPS] = {0, 0, 0};
    const char* phaseMessage = nullptr;

    if (xSemaphoreTake(systemStateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      // --- Two-phase protocol state machine ---
      if (estopRequested && systemState.protocolPhase != PROTO_IDLE) {
        systemState.protocolPhase = PROTO_IDLE;
        phaseMessage = "PROTOCOL STOPPED";
      } else if (systemState.protocolPhase == PROTO_PHASE1 && now >= systemState.phaseEndTick) {
        systemState.protocolPhase = PROTO_PHASE2;
        systemState.phaseEndTick = now + pdMS_TO_TICKS(systemState.t2Seconds * 1000);
        phaseMessage = "PHASE 2: DYE+WASH";
      } else if (systemState.protocolPhase == PROTO_PHASE2 && now >= systemState.phaseEndTick) {
        systemState.protocolPhase = PROTO_IDLE;
        phaseMessage = "PROTOCOL COMPLETE";
      }

      // --- Compute per-pump targets for this cycle ---
      switch (systemState.protocolPhase) {
        case PROTO_PHASE1:
          targetPct[PUMP_SAMPLE] = systemState.pumpSpeedPct[PUMP_SAMPLE];
          targetPct[PUMP_DYE] = systemState.pumpSpeedPct[PUMP_DYE];
          targetPct[PUMP_WASH] = 0;
          break;
        case PROTO_PHASE2:
          targetPct[PUMP_SAMPLE] = 0;
          targetPct[PUMP_DYE] = systemState.pumpSpeedPct[PUMP_DYE];
          targetPct[PUMP_WASH] = systemState.pumpSpeedPct[PUMP_WASH];
          break;
        default: // PROTO_IDLE — manual per-pump run
          for (int i = 0; i < NUM_PUMPS; i++) {
            targetPct[i] = systemState.pumpManualRun[i] ? systemState.pumpSpeedPct[i] : 0;
          }
          break;
      }

      if (systemState.protocolPhase == PROTO_IDLE) {
        xEventGroupClearBits(controlEvents, BIT_AUTO_RUNNING);
      }

      // --- Snapshot for the LCD ---
      for (int i = 0; i < NUM_PUMPS; i++) {
        uiData.pumpSpeedPct[i] = systemState.pumpSpeedPct[i];
        uiData.pumpRunning[i] = (targetPct[i] != 0);
      }
      uiData.t1S = systemState.t1Seconds;
      uiData.t2S = systemState.t2Seconds;
      uiData.menuSel = systemState.menuSel;
      uiData.inEdit = systemState.inEdit;
      uiData.phase = systemState.protocolPhase;
      uiData.phaseRemainingS = (systemState.protocolPhase != PROTO_IDLE && now < systemState.phaseEndTick)
                                   ? (uint32_t)((systemState.phaseEndTick - now) * portTICK_PERIOD_MS) / 1000
                                   : 0;

      xSemaphoreGive(systemStateMutex);
    }

    if (estopRequested) {
      xEventGroupClearBits(controlEvents, BIT_ESTOP_REQUEST);
    }
    if (phaseMessage != nullptr) {
      LCD_setMessage(phaseMessage);
    }

    // Apply speed commands to pumps (controller_task is the sole writer)
    for (int i = 0; i < NUM_PUMPS; i++) {
      pumpNode(i)->setSpeed((int)(targetPct[i] * MOTOR_SPEED_SCALE_FACTOR));
    }

    // Debounced NVS persistence for pump speeds
    uint32_t nowMs = xTaskGetTickCount() * portTICK_PERIOD_MS;
    for (int i = 0; i < NUM_PUMPS; i++) {
      if (uiData.pumpSpeedPct[i] != lastSavedSpeed[i]) {
        if (speedDirtySince[i] == 0) {
          speedDirtySince[i] = nowMs;
        } else if (nowMs - speedDirtySince[i] >= SPEED_SAVE_DEBOUNCE_MS) {
          StorageManager::savePumpSpeed(i, uiData.pumpSpeedPct[i]);
          lastSavedSpeed[i] = uiData.pumpSpeedPct[i];
          speedDirtySince[i] = 0;
        }
      } else {
        speedDirtySince[i] = 0;
      }
    }

    // Publish UI snapshot
    uiData.wifiConnected = NetworkManager::isConnected();
    if (lcdDataQueue != NULL) {
      xQueueOverwrite(lcdDataQueue, &uiData);
    }

    vTaskDelayUntil(&lastWakeTime, CONTROLLER_INTERVAL);
  }
}
