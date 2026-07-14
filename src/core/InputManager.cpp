#include "core/InputManager.h"
#include "controller.h"
#include "drivers/EncoderDriver.h"
#include "drivers/LCDDriver.h"
#include "core/StorageManager.h"
#include <cstdio>
#include <cstdlib>

static const char* kPumpNames[NUM_PUMPS] = {"Sample", "Dye", "Wash"};

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

  if (xSemaphoreTake(encoderStateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    delta = g_encoderState.position[idx] - lastPos[idx];
    lastPos[idx] = g_encoderState.position[idx];

    if (g_encoderState.buttonPressed[idx]) {
      shortPress = true;
      g_encoderState.buttonPressed[idx] = false;
    }
    g_encoderState.buttonLongPressed[idx] = false;
    g_encoderState.buttonDoublePressed[idx] = false;
    xSemaphoreGive(encoderStateMutex);
  }

  if (delta != 0) {
    int32_t step = (abs((int)delta) >= 4) ? delta * 5 : delta;
    if (xSemaphoreTake(systemStateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      int pct = systemState.pumpSpeedPct[idx] + step;
      systemState.pumpSpeedPct[idx] = constrain(pct, 0, 100);
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
}

// ============================================================================
// Enc3 — menu navigation / value edit (T1, T2) / protocol start-stop / E-STOP
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
  const char* message = nullptr;

  // Long press: global E-STOP, regardless of menu/edit state.
  if (longPress) {
    requestEstop = true;
    notify = true;
    message = "E-STOP";
  }

  if (xSemaphoreTake(systemStateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    bool protocolRunning = (systemState.protocolPhase != PROTO_IDLE);

    if (systemState.inEdit) {
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
    } else {
      if (delta != 0) {
        int sel = ((int)systemState.menuSel + (int)delta) % MENU_COUNT;
        if (sel < 0) sel += MENU_COUNT;
        systemState.menuSel = (MenuItem)sel;
      }
      if (shortPress) {
        if (systemState.menuSel == MENU_START) {
          if (protocolRunning) {
            requestEstop = true;
            notify = true;
            message = "STOPPING...";
          } else {
            requestStart = true;
            notify = true;
            message = "PHASE 1: SAMPLE+DYE";
          }
        } else {
          systemState.inEdit = true;
          notify = true;
          message = "Editing...";
        }
      }
    }

    if (requestStart) {
      systemState.protocolPhase = PROTO_PHASE1;
      systemState.phaseEndTick = xTaskGetTickCount() + pdMS_TO_TICKS(systemState.t1Seconds * 1000);
      for (int i = 0; i < NUM_PUMPS; i++) {
        systemState.pumpManualRun[i] = false;
      }
    }

    if (doublePress) {
      for (int i = 0; i < NUM_PUMPS; i++) {
        StorageManager::savePumpSpeed(i, systemState.pumpSpeedPct[i]);
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

  if (shortPress || doublePress || longPress) {
    LCD_notifyButtonPress(3);
  }
  if (notify && message != nullptr) {
    LCD_setMessage(message);
  }
}
