#include "core/InputManager.h"
#include "controller.h"
#include "drivers/EncoderDriver.h"
#include "drivers/LCDDriver.h"
#include "core/StorageManager.h"
#include <cstdio>
#include <cstdlib>

static const char* kPumpNames[NUM_PUMPS] = {"Sample", "Dye", "Wash"};

// Per-pump encoder step size, cycled by that pump's own encoder long-press.
static const int kPumpStepLevels[] = {10, 100, 1000};
static const int kPumpStepLevelCount = sizeof(kPumpStepLevels) / sizeof(kPumpStepLevels[0]);

void InputManager::init() {
  if (xSemaphoreTake(encoderStateMutex, portMAX_DELAY) == pdTRUE) {
    for (int i = 0; i < 4; i++) {
      g_encoderState.position[i] = 0;
    }
    xSemaphoreGive(encoderStateMutex);
  }
}

void InputManager::process() {
  handlePumpEncoder(PUMP_SAMPLE);
  handlePumpEncoder(PUMP_DYE);
  handlePumpEncoder(PUMP_WASH);
  handleMenuEncoder();
}

// ============================================================================
// Enc0/1/2 — pump speed (turn) + manual run toggle (short press)
// ============================================================================
void InputManager::handlePumpEncoder(int idx) {
  static int32_t lastPos[NUM_PUMPS] = {0, 0, 0};
  int32_t delta = 0;
  bool shortPress = false;
  bool longPress = false;

  if (xSemaphoreTake(encoderStateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    delta = g_encoderState.position[idx] - lastPos[idx];
    lastPos[idx] = g_encoderState.position[idx];

    if (g_encoderState.buttonPressed[idx]) {
      shortPress = true;
      g_encoderState.buttonPressed[idx] = false;
    }
    if (g_encoderState.buttonLongPressed[idx]) {
      longPress = true;
      g_encoderState.buttonLongPressed[idx] = false;
    }
    g_encoderState.buttonDoublePressed[idx] = false;
    xSemaphoreGive(encoderStateMutex);
  }

  if (delta != 0) {
    int32_t detents = (abs((int)delta) >= 4) ? delta * (PUMP_SPEED_DETENT_ACCEL / PUMP_SPEED_DETENT) : delta;
    if (xSemaphoreTake(systemStateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      int stepSize = kPumpStepLevels[systemState.pumpStepSizeIdx[idx]];
      int steps = systemState.pumpSpeedSteps[idx] + detents * stepSize;
      systemState.pumpSpeedSteps[idx] = constrain(steps, -PUMP_SPEED_MAX_STEPS, PUMP_SPEED_MAX_STEPS);
      xSemaphoreGive(systemStateMutex);
    }
  }

  if (shortPress) {
    LCD_notifyButtonPress(idx);
    bool running = false;
    if (xSemaphoreTake(systemStateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      systemState.pumpManualRun[idx] = !systemState.pumpManualRun[idx];
      running = systemState.pumpManualRun[idx];
      xSemaphoreGive(systemStateMutex);
    }
    char msg[32];
    snprintf(msg, sizeof(msg), "%s %s", kPumpNames[idx], running ? "RUN" : "STOP");
    LCD_setMessage(msg);
  }

  if (longPress) {
    LCD_notifyButtonPress(idx);
    int newStepSize = 0;
    if (xSemaphoreTake(systemStateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      systemState.pumpStepSizeIdx[idx] = (systemState.pumpStepSizeIdx[idx] + 1) % kPumpStepLevelCount;
      newStepSize = kPumpStepLevels[systemState.pumpStepSizeIdx[idx]];
      xSemaphoreGive(systemStateMutex);
    }
    char msg[32];
    snprintf(msg, sizeof(msg), "%s step: %d", kPumpNames[idx], newStepSize);
    LCD_setMessage(msg);
  }
}

// ============================================================================
// Enc3 — menu navigation / value edit (T1, T2) / motor power submenu /
// protocol start / manual phase-skip / E-STOP
// ============================================================================
void InputManager::handleMenuEncoder() {
  static int32_t lastPos = 0;
  int32_t delta = 0;
  bool shortPress = false;
  bool longPress = false;
  bool doublePress = false;

  if (xSemaphoreTake(encoderStateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    delta = g_encoderState.position[3] - lastPos;
    lastPos = g_encoderState.position[3];

    if (g_encoderState.buttonPressed[3]) {
      shortPress = true;
      g_encoderState.buttonPressed[3] = false;
    }
    if (g_encoderState.buttonLongPressed[3]) {
      longPress = true;
      g_encoderState.buttonLongPressed[3] = false;
    }
    if (g_encoderState.buttonDoublePressed[3]) {
      doublePress = true;
      g_encoderState.buttonDoublePressed[3] = false;
    }
    xSemaphoreGive(encoderStateMutex);
  }

  if (delta == 0 && !shortPress && !doublePress && !longPress) return;

  bool notify = false;
  bool requestStart = false;
  bool requestEstop = false;
  bool requestSkip = false;
  char msgBuf[32];
  const char* message = nullptr;

  // Long press: global E-STOP, regardless of menu/edit/run state.
  if (longPress) {
    requestEstop = true;
    notify = true;
    message = "E-STOP";
  }

  if (xSemaphoreTake(systemStateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    bool protocolRunning = (systemState.protocolPhase != PROTO_IDLE);

    if (protocolRunning) {
      // The menu row isn't shown while running, so rotation is ignored and a
      // short press means "skip to next phase now" instead of navigating.
      if (shortPress) {
        requestSkip = true;
        notify = true;
        message = "SKIPPING...";
      }
    } else if (systemState.inEdit) {
      if (delta != 0) {
        int32_t step = (abs((int)delta) >= 4) ? delta * 5 : delta;
        uint32_t* target = (systemState.menuSel == MENU_T1) ? &systemState.t1Seconds : &systemState.t2Seconds;
        int32_t newVal = (int32_t)*target + step;
        *target = (uint32_t)constrain(newVal, 1, 3600);
      }
      if (shortPress) {
        if (systemState.menuSel == MENU_T1) {
          StorageManager::saveT1(systemState.t1Seconds);
        } else {
          StorageManager::saveT2(systemState.t2Seconds);
        }
        systemState.inEdit = false;
        notify = true;
        message = "Saved";
      }
    } else if (systemState.inMotorMenu) {
      if (delta != 0) {
        int sel = ((int)systemState.motorMenuSel + (int)delta) % (NUM_PUMPS + 1);
        if (sel < 0) sel += (NUM_PUMPS + 1);
        systemState.motorMenuSel = sel;
      }
      if (shortPress) {
        if (systemState.motorMenuSel == NUM_PUMPS) {
          systemState.inMotorMenu = false;
          notify = true;
          message = "Back";
        } else {
          int idx = systemState.motorMenuSel;
          systemState.pumpEnabled[idx] = !systemState.pumpEnabled[idx];
          if (!systemState.pumpEnabled[idx]) {
            systemState.pumpManualRun[idx] = false;
          }
          notify = true;
          snprintf(msgBuf, sizeof(msgBuf), "%s: %s", kPumpNames[idx], systemState.pumpEnabled[idx] ? "ON" : "OFF");
          message = msgBuf;
        }
      }
    } else {
      if (delta != 0) {
        int sel = ((int)systemState.menuSel + (int)delta) % MENU_COUNT;
        if (sel < 0) sel += MENU_COUNT;
        systemState.menuSel = (MenuItem)sel;
      }
      if (shortPress) {
        if (systemState.menuSel == MENU_START) {
          requestStart = true;
          notify = true;
          message = "PHASE 1: SAMPLE+DYE";
        } else if (systemState.menuSel == MENU_MOTORS) {
          systemState.inMotorMenu = true;
          systemState.motorMenuSel = 0;
          notify = true;
          message = "Motors...";
        } else {
          systemState.inEdit = true;
          notify = true;
          message = "Editing...";
        }
      }
    }

    if (requestStart) {
      TickType_t nowTick = xTaskGetTickCount();
      systemState.protocolPhase = PROTO_PHASE1;
      systemState.phaseStartTick = nowTick;
      systemState.phaseEndTick = nowTick + pdMS_TO_TICKS(systemState.t1Seconds * 1000);
      systemState.inMotorMenu = false;
      systemState.inEdit = false;
      for (int i = 0; i < NUM_PUMPS; i++) {
        systemState.pumpManualRun[i] = false;
        // A pump left off for manual jogging must not silently fail to run.
        systemState.pumpEnabled[i] = true;
      }
    }

    if (doublePress) {
      for (int i = 0; i < NUM_PUMPS; i++) {
        StorageManager::savePumpSpeed(i, systemState.pumpSpeedSteps[i]);
      }
      StorageManager::saveT1(systemState.t1Seconds);
      StorageManager::saveT2(systemState.t2Seconds);
      notify = true;
      message = "All Saved";
    }
    xSemaphoreGive(systemStateMutex);
  }

  if (requestStart) {
    xEventGroupSetBits(controlEvents, BIT_AUTO_RUNNING);
  }
  if (requestEstop) {
    xEventGroupSetBits(controlEvents, BIT_ESTOP_REQUEST);
  }
  if (requestSkip) {
    xEventGroupSetBits(controlEvents, BIT_SKIP_REQUEST);
  }

  if (shortPress || doublePress || longPress) {
    LCD_notifyButtonPress(3);
  }
  if (notify && message != nullptr) {
    LCD_setMessage(message);
  }
}
