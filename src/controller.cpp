#include "controller.h"
#include "messaging.h"
#include <string.h>
#include "core/Protocol.h"
#include "core/StateSnapshot.h"
#include "core/StorageManager.h"
#include "core/Log.h"
#include "tasks/MotorNode.h"

// Pump nodes are heap-allocated in main.cpp (SoftwareSerial members must be
// constructed after the Arduino core init(), not at static-init time).
extern MotorNode *g_pumps[NUM_PUMPS];

SystemState systemState;
SemaphoreHandle_t systemStateMutex = NULL;
EventGroupHandle_t controlEvents = NULL;
volatile bool g_estopFromISR = false;

static const char *TAG = "PROTO";

// ============================================================================
// Init
// ============================================================================
void initSystemState() {
  systemStateMutex = xSemaphoreCreateMutex();
  controlEvents = xEventGroupCreate();

  StorageManager::init();

  if (xSemaphoreTake(systemStateMutex, portMAX_DELAY) == pdTRUE) {
    for (int i = 0; i < NUM_PUMPS; i++) {
      systemState.pumpSpeedSteps[i] = StorageManager::loadPumpSpeed(i, 5);
      systemState.pumpManualRun[i] = false;
      systemState.pumpEnabled[i] = true;
    }
    for (int p = 0; p < NUM_PHASES; p++) {
      systemState.phaseSeconds[p] =
          StorageManager::loadPhaseTime(p, kDefaultPhaseSeconds[p]);
    }
    systemState.currentPhase = -1;
    systemState.phaseEndTick = 0;
    systemState.phaseStartTick = 0;
    xSemaphoreGive(systemStateMutex);
  }
}

// ============================================================================
// Protocol commands from SerialLink (replaces InputManager mutations)
// ============================================================================
static void startProtocol() {
  if (xSemaphoreTake(systemStateMutex, portMAX_DELAY) != pdTRUE) return;
  if (systemState.currentPhase < 0) {
    TickType_t now = xTaskGetTickCount();
    systemState.currentPhase = 0;
    systemState.phaseStartTick = now;
    systemState.phaseEndTick = now + pdMS_TO_TICKS(systemState.phaseSeconds[0] * 1000);
    for (int i = 0; i < NUM_PUMPS; i++) {
      systemState.pumpManualRun[i] = false;
      systemState.pumpEnabled[i] = true; // a pump left off for jogging must still run
    }
    xEventGroupSetBits(controlEvents, BIT_AUTO_RUNNING);
    PEACH_LOGI(TAG, "RUN → %s", kPhaseNames[0]);
  }
  xSemaphoreGive(systemStateMutex);
}

static void applyProtoCommand(const ProtoCommand &pc) {
  switch (pc.action) {
    case ProtoAction::RUN:
      startProtocol();
      break;

    case ProtoAction::STOP:
    case ProtoAction::ESTOP:
      xEventGroupSetBits(controlEvents, BIT_ESTOP_REQUEST);
      break;

    case ProtoAction::SKIP:
      xEventGroupSetBits(controlEvents, BIT_SKIP_REQUEST);
      break;

    case ProtoAction::SET_SPEED:
      if (pc.a >= 0 && pc.a < NUM_PUMPS &&
          xSemaphoreTake(systemStateMutex, portMAX_DELAY) == pdTRUE) {
        systemState.pumpSpeedSteps[pc.a] =
            constrain(pc.b, -PUMP_SPEED_MAX_STEPS, PUMP_SPEED_MAX_STEPS);
        xSemaphoreGive(systemStateMutex);
      }
      break;

    case ProtoAction::SET_PHASETIME:
      if (pc.a >= 0 && pc.a < NUM_PHASES &&
          xSemaphoreTake(systemStateMutex, portMAX_DELAY) == pdTRUE) {
        systemState.phaseSeconds[pc.a] = (uint32_t)constrain(pc.b, 1, 3600);
        StorageManager::savePhaseTime(pc.a, systemState.phaseSeconds[pc.a]);
        xSemaphoreGive(systemStateMutex);
      }
      break;

    case ProtoAction::SET_ENABLE:
      if (pc.a >= 0 && pc.a < NUM_PUMPS &&
          xSemaphoreTake(systemStateMutex, portMAX_DELAY) == pdTRUE) {
        systemState.pumpEnabled[pc.a] = (pc.b != 0);
        if (pc.b == 0) systemState.pumpManualRun[pc.a] = false;
        xSemaphoreGive(systemStateMutex);
      }
      break;

    case ProtoAction::JOG:
      if (pc.a >= 0 && pc.a < NUM_PUMPS &&
          xSemaphoreTake(systemStateMutex, portMAX_DELAY) == pdTRUE) {
        if (systemState.currentPhase < 0) { // manual jog only while idle
          systemState.pumpManualRun[pc.a] = (pc.b != 0);
          if (pc.b != 0) {
            systemState.pumpSpeedSteps[pc.a] =
                constrain(pc.b, -PUMP_SPEED_MAX_STEPS, PUMP_SPEED_MAX_STEPS);
          }
        }
        xSemaphoreGive(systemStateMutex);
      }
      break;
  }
}

// ============================================================================
// Main controller task — 50 Hz
// ============================================================================
static const uint32_t SPEED_SAVE_DEBOUNCE_MS = 2000;

void controller_task(void *pvParameters) {
  (void)pvParameters;
  TickType_t lastWakeTime = xTaskGetTickCount();
  const TickType_t INTERVAL = pdMS_TO_TICKS(20);

  int lastSavedSpeed[NUM_PUMPS];
  uint32_t speedDirtySince[NUM_PUMPS];
  bool lastAppliedEnabled[NUM_PUMPS];
  for (int i = 0; i < NUM_PUMPS; i++) {
    lastSavedSpeed[i] = systemState.pumpSpeedSteps[i];
    speedDirtySince[i] = 0;
    lastAppliedEnabled[i] = systemState.pumpEnabled[i];
  }

  for (;;) {
    // 1. protocol commands from SerialLink
    ProtoCommand pc;
    while (xQueueReceive(protoCmdQueue, &pc, 0) == pdTRUE) applyProtoCommand(pc);

    // 2. hardware E-STOP button
    if (g_estopFromISR) {
      g_estopFromISR = false;
      xEventGroupSetBits(controlEvents, BIT_ESTOP_REQUEST);
    }

    TickType_t now = xTaskGetTickCount();
    bool estop = (xEventGroupGetBits(controlEvents) & BIT_ESTOP_REQUEST) != 0;
    bool skip = (xEventGroupGetBits(controlEvents) & BIT_SKIP_REQUEST) != 0;

    int targetSteps[NUM_PUMPS];
    for (int i = 0; i < NUM_PUMPS; i++) targetSteps[i] = 0;
    StateSnapshot snap;
    memset(&snap, 0, sizeof(snap));
    snap.currentPhase = -1; // safe default if the state lock times out

    if (xSemaphoreTake(systemStateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      // --- phase state machine ---
      if (estop && systemState.currentPhase >= 0) {
        PEACH_LOGI(TAG, "E-STOP during %s", kPhaseNames[systemState.currentPhase]);
        systemState.currentPhase = -1;
        for (int i = 0; i < NUM_PUMPS; i++) systemState.pumpManualRun[i] = false;
      } else if (systemState.currentPhase >= 0 &&
                 (now >= systemState.phaseEndTick || skip)) {
        int next = systemState.currentPhase + 1;
        if (next >= NUM_PHASES) {
          PEACH_LOGI(TAG, "protocol complete");
          systemState.currentPhase = -1;
        } else {
          systemState.currentPhase = next;
          systemState.phaseStartTick = now;
          systemState.phaseEndTick =
              now + pdMS_TO_TICKS(systemState.phaseSeconds[next] * 1000);
          PEACH_LOGI(TAG, "→ %s", kPhaseNames[next]);
        }
      }

      // --- per-pump targets for this cycle ---
      if (systemState.currentPhase >= 0) {
        uint8_t mask = kProtocol[systemState.currentPhase].activeMask;
        for (int i = 0; i < NUM_PUMPS; i++) {
          targetSteps[i] = (mask & PUMP_BIT(i)) ? systemState.pumpSpeedSteps[i] : 0;
        }
      } else {
        xEventGroupClearBits(controlEvents, BIT_AUTO_RUNNING);
        for (int i = 0; i < NUM_PUMPS; i++) {
          targetSteps[i] = systemState.pumpManualRun[i] ? systemState.pumpSpeedSteps[i] : 0;
        }
      }

      // --- snapshot ---
      snap.currentPhase = systemState.currentPhase;
      snap.phaseRemainingS =
          (systemState.currentPhase >= 0 && now < systemState.phaseEndTick)
              ? (uint32_t)((systemState.phaseEndTick - now) * portTICK_PERIOD_MS) / 1000
              : 0;
      for (int i = 0; i < NUM_PUMPS; i++) {
        snap.pumpSpeedSteps[i] = systemState.pumpSpeedSteps[i];
        snap.pumpRunning[i] = (targetSteps[i] != 0);
        snap.pumpEnabled[i] = systemState.pumpEnabled[i];
      }
      for (int p = 0; p < NUM_PHASES; p++) snap.phaseSeconds[p] = systemState.phaseSeconds[p];
      xSemaphoreGive(systemStateMutex);
    }

    if (estop) xEventGroupClearBits(controlEvents, BIT_ESTOP_REQUEST);
    if (skip) xEventGroupClearBits(controlEvents, BIT_SKIP_REQUEST);
    snap.estopLatched = estop;

    // 3. apply to pumps (controller_task is the sole speed writer)
    for (int i = 0; i < NUM_PUMPS; i++) {
      g_pumps[i]->setSpeed(targetSteps[i]);
      if (snap.pumpEnabled[i] != lastAppliedEnabled[i]) {
        g_pumps[i]->setEnabled(snap.pumpEnabled[i]);
        lastAppliedEnabled[i] = snap.pumpEnabled[i];
      }
    }

    // 4. debounced persistence of pump speeds
    uint32_t nowMs = xTaskGetTickCount() * portTICK_PERIOD_MS;
    for (int i = 0; i < NUM_PUMPS; i++) {
      if (snap.pumpSpeedSteps[i] != lastSavedSpeed[i]) {
        if (speedDirtySince[i] == 0) {
          speedDirtySince[i] = nowMs;
        } else if (nowMs - speedDirtySince[i] >= SPEED_SAVE_DEBOUNCE_MS) {
          StorageManager::savePumpSpeed(i, snap.pumpSpeedSteps[i]);
          lastSavedSpeed[i] = snap.pumpSpeedSteps[i];
          speedDirtySince[i] = 0;
        }
      } else {
        speedDirtySince[i] = 0;
      }
    }

    // 5. publish snapshot for SerialLink
    if (stateQueue != NULL) xQueueOverwrite(stateQueue, &snap);

    vTaskDelayUntil(&lastWakeTime, INTERVAL);
  }
}
