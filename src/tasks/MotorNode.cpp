#include "tasks/MotorNode.h"
#include "core/Log.h"

SemaphoreHandle_t xUARTMutex = NULL;

static const char *TAG = "PUMP";

// Sentinel for lastSentSpeed: outside the range of any real speed, so the first
// hwUpdate() (and the one after each config re-push) always resends VACTUAL.
static constexpr int SPEED_UNSENT = -1000000;

// Consecutive failed read-backs before we call a driver "dark" (debounce, so a
// single flaky software-UART read doesn't cry wolf). At DRIVER_REASSERT_MS = 5 s
// this is ~10 s of steady failure.
static constexpr uint8_t COMM_FAIL_WARN = 2;

// Construction order (0..NUM_PUMPS-1) — used only to stagger the periodic
// reassert bursts so all eight drivers don't hit the UART mutex on one cycle.
static uint8_t s_nodeSeq = 0;

MotorNode::MotorNode(const MotorConfig &conf)
    : config(conf),
      swSerial(conf.uartPin, conf.uartPin), // one-wire, half-duplex
      targetSpeed(0),
      lastSentSpeed(SPEED_UNSENT),
      isEnabled(true),
      commOk(false),
      commFail(0),
      reassertAccumMs((s_nodeSeq++ * DRIVER_REASSERT_MS) / NUM_PUMPS) {}

void MotorNode::hwInit() {
#if DRIVER_VERIFY
  TMC2209::Settings s = {};
#endif
  if (xUARTMutex != NULL && xSemaphoreTake(xUARTMutex, portMAX_DELAY) == pdTRUE) {
    driver.begin(swSerial, config.enPin);
#if DRIVER_VERIFY
    s = driver.readSettings(swSerial);
#endif
    xSemaphoreGive(xUARTMutex);
  }
  vTaskDelay(pdMS_TO_TICKS(50)); // let the driver settle before VACTUAL (mutex released)

#if DRIVER_VERIFY
  commOk = s.is_communicating && s.is_setup;
  commFail = commOk ? 0 : COMM_FAIL_WARN; // already "dark" — don't warn again on the first tick
  if (!s.is_communicating) {
    // One driver → suspect that slot (stepstick seating, MSx address jumpers,
    // VMOT to the slot). All eight → the software-UART RX path, not the drivers.
    PEACH_LOGW(TAG, "%s: no UART read-back - running blind on defaults", config.name);
  } else if (!s.is_setup) {
    PEACH_LOGW(TAG, "%s: TMC2209 answered but the serial-mode config did not stick",
               config.name);
  } else {
    PEACH_LOGI(TAG, "%s: UART ok - stealthchop=%d usteps=%u irun=%u%%", config.name,
               s.stealth_chop_enabled ? 1 : 0, s.microsteps_per_step, s.irun_percent);
  }
#else
  commOk = true;
  PEACH_LOGI(TAG, "%s configured (blind)", config.name);
#endif
}

void MotorNode::processCommand(const MotorCommand &cmd) {
  switch (cmd.action) {
    case MotorCmdAction::SET_SPEED:
      targetSpeed = (int)cmd.value;
      break;

    case MotorCmdAction::SET_ENABLED:
      isEnabled = (cmd.value != 0.0f);
      if (xUARTMutex != NULL && xSemaphoreTake(xUARTMutex, portMAX_DELAY) == pdTRUE) {
        if (isEnabled) {
          driver.enable();
        } else {
          driver.disable();
        }
        xSemaphoreGive(xUARTMutex);
      }
      break;
  }
}

void MotorNode::reassertDriver() {
  // Blindly re-push the full config so a TMC2209 that briefly lost VMOT and
  // reloaded its loud OTP defaults heals itself — the automatic version of the
  // operator toggling Hold/Free. In the healthy case every write is idempotent
  // and motion is undisturbed (VACTUAL is not touched here).
  if (xUARTMutex == NULL || xSemaphoreTake(xUARTMutex, portMAX_DELAY) != pdTRUE) return;

  driver.reassertConfig();
  if (isEnabled) {
    driver.enable();
  } else {
    driver.disable();
  }

#if DRIVER_VERIFY
  bool ok = driver.verifyComms(swSerial);
#endif
  xSemaphoreGive(xUARTMutex);

  lastSentSpeed = SPEED_UNSENT; // resend VACTUAL + direction on the next hwUpdate

#if DRIVER_VERIFY
  if (ok) {
    if (commFail >= COMM_FAIL_WARN) {
      PEACH_LOGI(TAG, "%s: UART recovered", config.name);
    }
    commFail = 0;
  } else if (commFail < 255) {
    if (++commFail == COMM_FAIL_WARN) {
      PEACH_LOGW(TAG, "%s: UART went dark - now running blind on defaults", config.name);
    }
  }
  commOk = (commFail < COMM_FAIL_WARN);
#endif
}

void MotorNode::hwUpdate() {
  reassertAccumMs += TASK_UPDATE_INTERVAL_MS;
  if (reassertAccumMs >= DRIVER_REASSERT_MS) {
    reassertAccumMs = 0;
    reassertDriver();
  }

  // Write VACTUAL only on change — limits software-UART traffic / IRQ windows.
  if (targetSpeed != lastSentSpeed) {
    if (xUARTMutex != NULL && xSemaphoreTake(xUARTMutex, portMAX_DELAY) == pdTRUE) {
      driver.setVelocity(targetSpeed);
      xSemaphoreGive(xUARTMutex);
    }
    lastSentSpeed = targetSpeed;
  }
}

MotorTelemetry MotorNode::generateTelemetry() {
  MotorTelemetry tel;
  tel.targetSpeed = targetSpeed;
  tel.isEnabled = isEnabled;
  tel.commOk = commOk;
  return tel;
}

bool MotorNode::setSpeed(int speed) {
  MotorCommand cmd{MotorCmdAction::SET_SPEED, (float)speed};
  return sendCommand(cmd);
}

bool MotorNode::setEnabled(bool enable) {
  MotorCommand cmd{MotorCmdAction::SET_ENABLED, enable ? 1.0f : 0.0f};
  return sendCommand(cmd);
}
