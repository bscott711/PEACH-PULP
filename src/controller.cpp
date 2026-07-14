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
      systemState.pumpSpeedSteps[i] = StorageManager::loadPumpSpeed(i, 5);
      systemState.pumpManualRun[i] = false;
      systemState.pumpEnabled[i] = true;
      systemState.pumpStepSizeIdx[i] = 0;
    }
    systemState.t1Seconds = StorageManager::loadT1(60);
    systemState.t2Seconds = StorageManager::loadT2(30);
    systemState.menuSel = MENU_START;
    systemState.inEdit = false;
    systemState.inMotorMenu = false;
    systemState.motorMenuSel = 0;
    systemState.protocolPhase = PROTO_IDLE;
    systemState.phaseEndTick = 0;
    systemState.phaseStartTick = 0;
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
static bool lastAppliedEnabled[NUM_PUMPS] = {true, true, true};

static const char* kPhaseLogNames[3] = {"IDLE", "PHASE1(Sample+Dye)", "PHASE2(Dye+Wash)"};

void controller_task(void *pvParameters) {
  TickType_t lastWakeTime = xTaskGetTickCount();
  const TickType_t CONTROLLER_INTERVAL = pdMS_TO_TICKS(20);

  for (int i = 0; i < NUM_PUMPS; i++) {
    lastSavedSpeed[i] = systemState.pumpSpeedSteps[i];
  }

  while (1) {
    InputManager::process();

    TickType_t now = xTaskGetTickCount();
    bool estopRequested = (xEventGroupGetBits(controlEvents) & BIT_ESTOP_REQUEST) != 0;
    bool skipRequested = (xEventGroupGetBits(controlEvents) & BIT_SKIP_REQUEST) != 0;

    UIData uiData = {};
    int targetSteps[NUM_PUMPS] = {0, 0, 0};
    char phaseMsgBuf[32];
    const char* phaseMessage = nullptr;

    if (xSemaphoreTake(systemStateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      // --- Two-phase protocol state machine ---
      bool phaseExpired = (now >= systemState.phaseEndTick) || skipRequested;
      if (estopRequested && systemState.protocolPhase != PROTO_IDLE) {
        uint32_t elapsedS = (uint32_t)((now - systemState.phaseStartTick) * portTICK_PERIOD_MS) / 1000;
        PEACH_LOGI("PROTO", "%s stopped (E-STOP) after %us", kPhaseLogNames[systemState.protocolPhase], (unsigned)elapsedS);
        snprintf(phaseMsgBuf, sizeof(phaseMsgBuf), "STOPPED (%us)", (unsigned)elapsedS);
        phaseMessage = phaseMsgBuf;
        systemState.protocolPhase = PROTO_IDLE;
      } else if (systemState.protocolPhase == PROTO_PHASE1 && phaseExpired) {
        uint32_t elapsedS = (uint32_t)((now - systemState.phaseStartTick) * portTICK_PERIOD_MS) / 1000;
        PEACH_LOGI("PROTO", "%s ran %us", kPhaseLogNames[PROTO_PHASE1], (unsigned)elapsedS);
        systemState.protocolPhase = PROTO_PHASE2;
        systemState.phaseStartTick = now;
        systemState.phaseEndTick = now + pdMS_TO_TICKS(systemState.t2Seconds * 1000);
        snprintf(phaseMsgBuf, sizeof(phaseMsgBuf), "PHASE 2 (P1:%us)", (unsigned)elapsedS);
        phaseMessage = phaseMsgBuf;
      } else if (systemState.protocolPhase == PROTO_PHASE2 && phaseExpired) {
        uint32_t elapsedS = (uint32_t)((now - systemState.phaseStartTick) * portTICK_PERIOD_MS) / 1000;
        PEACH_LOGI("PROTO", "%s ran %us", kPhaseLogNames[PROTO_PHASE2], (unsigned)elapsedS);
        systemState.protocolPhase = PROTO_IDLE;
        snprintf(phaseMsgBuf, sizeof(phaseMsgBuf), "DONE (P2:%us)", (unsigned)elapsedS);
        phaseMessage = phaseMsgBuf;
      }

      // --- Compute per-pump targets for this cycle ---
      switch (systemState.protocolPhase) {
        case PROTO_PHASE1:
          targetSteps[PUMP_SAMPLE] = systemState.pumpSpeedSteps[PUMP_SAMPLE];
          targetSteps[PUMP_DYE] = systemState.pumpSpeedSteps[PUMP_DYE];
          targetSteps[PUMP_WASH] = 0;
          break;
        case PROTO_PHASE2:
          targetSteps[PUMP_SAMPLE] = 0;
          targetSteps[PUMP_DYE] = systemState.pumpSpeedSteps[PUMP_DYE];
          targetSteps[PUMP_WASH] = systemState.pumpSpeedSteps[PUMP_WASH];
          break;
        default: // PROTO_IDLE — manual per-pump run
          for (int i = 0; i < NUM_PUMPS; i++) {
            targetSteps[i] = systemState.pumpManualRun[i] ? systemState.pumpSpeedSteps[i] : 0;
          }
          break;
      }

      if (systemState.protocolPhase == PROTO_IDLE) {
        xEventGroupClearBits(controlEvents, BIT_AUTO_RUNNING);
      }

      // --- Snapshot for the LCD ---
      for (int i = 0; i < NUM_PUMPS; i++) {
        uiData.pumpSpeedSteps[i] = systemState.pumpSpeedSteps[i];
        uiData.pumpRunning[i] = (targetSteps[i] != 0);
        uiData.pumpEnabled[i] = systemState.pumpEnabled[i];
      }
      uiData.t1S = systemState.t1Seconds;
      uiData.t2S = systemState.t2Seconds;
      uiData.menuSel = systemState.menuSel;
      uiData.inEdit = systemState.inEdit;
      uiData.inMotorMenu = systemState.inMotorMenu;
      uiData.motorMenuSel = systemState.motorMenuSel;
      uiData.phase = systemState.protocolPhase;
      uiData.phaseRemainingS = (systemState.protocolPhase != PROTO_IDLE && now < systemState.phaseEndTick)
                                   ? (uint32_t)((systemState.phaseEndTick - now) * portTICK_PERIOD_MS) / 1000
                                   : 0;

      xSemaphoreGive(systemStateMutex);
    }

    if (estopRequested) {
      xEventGroupClearBits(controlEvents, BIT_ESTOP_REQUEST);
    }
    if (skipRequested) {
      xEventGroupClearBits(controlEvents, BIT_SKIP_REQUEST);
    }
    if (phaseMessage != nullptr) {
      LCD_setMessage(phaseMessage);
    }

    // Apply speed + enable commands to pumps (controller_task is the sole writer)
    for (int i = 0; i < NUM_PUMPS; i++) {
      pumpNode(i)->setSpeed(targetSteps[i]);
      if (uiData.pumpEnabled[i] != lastAppliedEnabled[i]) {
        pumpNode(i)->setEnabled(uiData.pumpEnabled[i]);
        lastAppliedEnabled[i] = uiData.pumpEnabled[i];
      }
    }

    // Debounced NVS persistence for pump speeds
    uint32_t nowMs = xTaskGetTickCount() * portTICK_PERIOD_MS;
    for (int i = 0; i < NUM_PUMPS; i++) {
      if (uiData.pumpSpeedSteps[i] != lastSavedSpeed[i]) {
        if (speedDirtySince[i] == 0) {
          speedDirtySince[i] = nowMs;
        } else if (nowMs - speedDirtySince[i] >= SPEED_SAVE_DEBOUNCE_MS) {
          StorageManager::savePumpSpeed(i, uiData.pumpSpeedSteps[i]);
          lastSavedSpeed[i] = uiData.pumpSpeedSteps[i];
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
