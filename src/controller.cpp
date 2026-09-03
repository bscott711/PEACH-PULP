#include "controller.h"
#include "messaging.h"
#include <stdio.h>
#include <string.h>
#include "core/Protocol.h"
#include "core/StateSnapshot.h"
#include "core/StorageManager.h"
#include "core/SerialLink.h"
#include "core/Log.h"
#include "tasks/MotorNode.h"

// Pump nodes are heap-allocated in main.cpp (SoftwareSerial members must be
// constructed after the Arduino core init(), not at static-init time).
extern MotorNode *g_pumps[NUM_PUMPS];

SystemState systemState;
SemaphoreHandle_t systemStateMutex = NULL;
EventGroupHandle_t controlEvents = NULL;

static const char *TAG = "PROTO";

// Staging buffer for an in-progress program upload. Only touched by
// controller_task (via applyProtoCommand) so it needs no lock of its own.
static ProgramPhase s_staging[MAX_PHASES];
static uint8_t s_stagingCount = 0;
static bool s_stagingOpen = false;

// Program needs a (debounced, idle-only) flash write.
static bool s_programDirty = false;

// eeprom_buffer_flush() erases a 128 KiB F4 sector — it BLOCKS ~1-2 s and starves
// the FreeRTOS tick while it runs. Persistence is therefore debounced and only
// ever runs while the sequence is idle. The Pi re-uploads the program on connect,
// so if the stall is a nuisance during bring-up, build with -D PEACH_NO_PERSIST.
#ifdef PEACH_NO_PERSIST
static const bool kPersist = false;
#else
static const bool kPersist = true;
#endif
static const uint32_t SAVE_DEBOUNCE_MS = 3000;

static int clampSpeed(int v) {
  return constrain(v, -PUMP_SPEED_MAX_STEPS, PUMP_SPEED_MAX_STEPS);
}

// ============================================================================
// Init
// ============================================================================
void initSystemState() {
  systemStateMutex = xSemaphoreCreateMutex();
  controlEvents = xEventGroupCreate();

  StorageManager::init();

  if (xSemaphoreTake(systemStateMutex, portMAX_DELAY) == pdTRUE) {
    for (int i = 0; i < NUM_PUMPS; i++) {
      systemState.liveSpeedSteps[i] = StorageManager::loadLiveSpeed(i, 0);
      systemState.pumpManualRun[i] = false;
      systemState.pumpEnabled[i] = true;
    }
    systemState.nPhases = StorageManager::loadProgram(systemState.program);
    if (systemState.nPhases == 0) {
      systemState.nPhases = buildSeedProgram(systemState.program);
      PEACH_LOGI(TAG, "using seed program (%d phases)", systemState.nPhases);
    }
    systemState.currentPhase = -1;
    systemState.phaseEndTick = 0;
    systemState.phaseStartTick = 0;
    xSemaphoreGive(systemStateMutex);
  }
}

// ============================================================================
// Protocol commands from SerialLink
// ============================================================================
static void startSequence() {
  if (xSemaphoreTake(systemStateMutex, portMAX_DELAY) != pdTRUE) return;
  if (systemState.currentPhase < 0 && systemState.nPhases > 0) {
    TickType_t now = xTaskGetTickCount();
    systemState.currentPhase = 0;
    systemState.phaseStartTick = now;
    systemState.phaseEndTick =
        now + pdMS_TO_TICKS(systemState.program[0].seconds * 1000);
    for (int i = 0; i < NUM_PUMPS; i++) {
      systemState.pumpManualRun[i] = false;
      systemState.pumpEnabled[i] = true; // re-energise every driver for the run
    }
    xEventGroupSetBits(controlEvents, BIT_AUTO_RUNNING);
    PEACH_LOGI(TAG, "RUN (%d phases)", systemState.nPhases);
  }
  xSemaphoreGive(systemStateMutex);
}

static void applyProtoCommand(const ProtoCommand &pc) {
  switch (pc.action) {
    case ProtoAction::RUN:
      startSequence();
      break;

    case ProtoAction::STOP:
      xEventGroupSetBits(controlEvents, BIT_STOP_REQUEST);
      break;

    case ProtoAction::SKIP:
      xEventGroupSetBits(controlEvents, BIT_SKIP_REQUEST);
      break;

    case ProtoAction::SET_SPEED:
      if (pc.a >= 0 && pc.a < NUM_PUMPS &&
          xSemaphoreTake(systemStateMutex, portMAX_DELAY) == pdTRUE) {
        systemState.liveSpeedSteps[pc.a] = clampSpeed(pc.b);
        xSemaphoreGive(systemStateMutex);
      }
      break;

    case ProtoAction::SET_PHASETIME:
      if (xSemaphoreTake(systemStateMutex, portMAX_DELAY) == pdTRUE) {
        if (pc.a >= 0 && pc.a < systemState.nPhases) {
          systemState.program[pc.a].seconds = (uint32_t)constrain(pc.b, 1, 3600);
          s_programDirty = true;
        }
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
          if (pc.b != 0) systemState.liveSpeedSteps[pc.a] = clampSpeed(pc.b);
        }
        xSemaphoreGive(systemStateMutex);
      }
      break;

    case ProtoAction::PROG_CLEAR:
      s_stagingCount = 0;
      s_stagingOpen = true;
      break;

    case ProtoAction::PROG_ADD:
      if (!s_stagingOpen) { // bare ADD after a COMMIT starts a fresh program
        s_stagingCount = 0;
        s_stagingOpen = true;
      }
      if (s_stagingCount < MAX_PHASES) {
        ProgramPhase &ph = s_staging[s_stagingCount];
        ph.seconds = (uint32_t)constrain(pc.a, 1, 3600);
        for (int i = 0; i < NUM_PUMPS; i++)
          ph.speed[i] = (int16_t)clampSpeed(pc.speeds[i]);
        s_stagingCount++;
      }
      break;

    case ProtoAction::PROG_COMMIT:
      if (s_stagingCount == 0) {
        serialEmit("!ERR empty program");
      } else if (xSemaphoreTake(systemStateMutex, portMAX_DELAY) == pdTRUE) {
        memset(systemState.program, 0, sizeof(systemState.program));
        memcpy(systemState.program, s_staging,
               sizeof(ProgramPhase) * s_stagingCount);
        systemState.nPhases = s_stagingCount;
        if (systemState.currentPhase >= (int)systemState.nPhases)
          systemState.currentPhase = -1; // running phase fell off the end
        s_programDirty = true;
        xSemaphoreGive(systemStateMutex);
        char m[24];
        snprintf(m, sizeof(m), "!EVENT prog %u", (unsigned)s_stagingCount);
        serialEmit(m);
      }
      s_stagingOpen = false;
      break;
  }
}

// ============================================================================
// Main controller task — 50 Hz
// ============================================================================
void controller_task(void *pvParameters) {
  (void)pvParameters;
  TickType_t lastWakeTime = xTaskGetTickCount();
  const TickType_t INTERVAL = pdMS_TO_TICKS(20);

  int lastSavedLive[NUM_PUMPS];
  uint32_t liveDirtySince[NUM_PUMPS];
  bool lastAppliedEnabled[NUM_PUMPS];
  for (int i = 0; i < NUM_PUMPS; i++) {
    lastSavedLive[i] = systemState.liveSpeedSteps[i];
    liveDirtySince[i] = 0;
    lastAppliedEnabled[i] = systemState.pumpEnabled[i];
  }
  uint32_t programDirtySince = 0;

  for (;;) {
    // 1. protocol commands from SerialLink
    ProtoCommand pc;
    while (xQueueReceive(protoCmdQueue, &pc, 0) == pdTRUE) applyProtoCommand(pc);

    TickType_t now = xTaskGetTickCount();
    bool stop = (xEventGroupGetBits(controlEvents) & BIT_STOP_REQUEST) != 0;
    bool skip = (xEventGroupGetBits(controlEvents) & BIT_SKIP_REQUEST) != 0;

    int targetSteps[NUM_PUMPS];
    for (int i = 0; i < NUM_PUMPS; i++) targetSteps[i] = 0;
    StateSnapshot snap;
    memset(&snap, 0, sizeof(snap));
    snap.currentPhase = -1; // safe default if the state lock times out

    if (xSemaphoreTake(systemStateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      // --- sequence state machine ---
      if (stop) {
        if (systemState.currentPhase >= 0)
          PEACH_LOGI(TAG, "STOP (phase %d)", systemState.currentPhase);
        systemState.currentPhase = -1;
        for (int i = 0; i < NUM_PUMPS; i++) systemState.pumpManualRun[i] = false;
      } else if (systemState.currentPhase >= 0 &&
                 (now >= systemState.phaseEndTick || skip)) {
        int next = systemState.currentPhase + 1;
        if (next >= (int)systemState.nPhases) {
          PEACH_LOGI(TAG, "sequence complete");
          systemState.currentPhase = -1;
        } else {
          systemState.currentPhase = next;
          systemState.phaseStartTick = now;
          systemState.phaseEndTick =
              now + pdMS_TO_TICKS(systemState.program[next].seconds * 1000);
          PEACH_LOGI(TAG, "→ phase %d", next);
        }
      }

      // --- per-pump targets for this cycle ---
      if (systemState.currentPhase >= 0) {
        const ProgramPhase &ph = systemState.program[systemState.currentPhase];
        for (int i = 0; i < NUM_PUMPS; i++) targetSteps[i] = ph.speed[i];
      } else {
        xEventGroupClearBits(controlEvents, BIT_AUTO_RUNNING);
        for (int i = 0; i < NUM_PUMPS; i++)
          targetSteps[i] = systemState.pumpManualRun[i]
                               ? systemState.liveSpeedSteps[i]
                               : 0;
      }

      // --- snapshot ---
      snap.currentPhase = systemState.currentPhase;
      snap.nPhases = systemState.nPhases;
      snap.phaseRemainingS =
          (systemState.currentPhase >= 0 && now < systemState.phaseEndTick)
              ? (uint32_t)((systemState.phaseEndTick - now) * portTICK_PERIOD_MS) / 1000
              : 0;
      for (int i = 0; i < NUM_PUMPS; i++) {
        snap.pumpSpeedSteps[i] =
            (systemState.currentPhase >= 0)
                ? systemState.program[systemState.currentPhase].speed[i]
                : systemState.liveSpeedSteps[i];
        snap.pumpRunning[i] = (targetSteps[i] != 0);
        snap.pumpEnabled[i] = systemState.pumpEnabled[i];
      }
      xSemaphoreGive(systemStateMutex);
    }

    if (stop) xEventGroupClearBits(controlEvents, BIT_STOP_REQUEST);
    if (skip) xEventGroupClearBits(controlEvents, BIT_SKIP_REQUEST);

    // 2. apply to pumps (controller_task is the sole speed writer)
    for (int i = 0; i < NUM_PUMPS; i++) {
      g_pumps[i]->setSpeed(targetSteps[i]);
      if (snap.pumpEnabled[i] != lastAppliedEnabled[i]) {
        g_pumps[i]->setEnabled(snap.pumpEnabled[i]);
        lastAppliedEnabled[i] = snap.pumpEnabled[i];
      }
    }

    // 3. debounced flash persistence — idle only, never mid-run
    uint32_t nowMs = now * portTICK_PERIOD_MS;
    bool canSave = kPersist && (snap.currentPhase < 0);

    for (int i = 0; i < NUM_PUMPS; i++) {
      if (canSave && snap.pumpSpeedSteps[i] != lastSavedLive[i]) {
        if (liveDirtySince[i] == 0) {
          liveDirtySince[i] = nowMs;
        } else if (nowMs - liveDirtySince[i] >= SAVE_DEBOUNCE_MS) {
          StorageManager::saveLiveSpeed(i, snap.pumpSpeedSteps[i]);
          lastSavedLive[i] = snap.pumpSpeedSteps[i];
          liveDirtySince[i] = 0;
        }
      } else if (snap.pumpSpeedSteps[i] == lastSavedLive[i]) {
        liveDirtySince[i] = 0;
      }
    }

    if (canSave && s_programDirty) {
      if (programDirtySince == 0) {
        programDirtySince = nowMs;
      } else if (nowMs - programDirtySince >= SAVE_DEBOUNCE_MS) {
        if (xSemaphoreTake(systemStateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
          StorageManager::saveProgram(systemState.program, systemState.nPhases);
          s_programDirty = false;
          xSemaphoreGive(systemStateMutex);
        }
        programDirtySince = 0;
      }
    } else if (!s_programDirty) {
      programDirtySince = 0;
    }

    // 4. publish snapshot for SerialLink
    if (stateQueue != NULL) xQueueOverwrite(stateQueue, &snap);

    vTaskDelayUntil(&lastWakeTime, INTERVAL);
  }
}
