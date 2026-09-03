#include "tasks/MotorNode.h"
#include "core/Log.h"

SemaphoreHandle_t xUARTMutex = NULL;

static const char *TAG = "PUMP";

MotorNode::MotorNode(const MotorConfig &conf)
    : config(conf),
      swSerial(conf.uartPin, conf.uartPin), // one-wire, write-only
      targetSpeed(0),
      lastSentSpeed(-999999),
      isEnabled(true) {}

void MotorNode::hwInit() {
  if (xUARTMutex != NULL && xSemaphoreTake(xUARTMutex, portMAX_DELAY) == pdTRUE) {
    // Blind bring-up: driver.setup() + all config are unidirectional writes.
    // There is no comm read-back on a one-wire write-only link.
    driver.begin(swSerial, config.enPin);
    xSemaphoreGive(xUARTMutex);
  }
  vTaskDelay(pdMS_TO_TICKS(50)); // let the driver settle before VACTUAL (mutex released)
  PEACH_LOGI(TAG, "%s configured", config.name);
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

void MotorNode::hwUpdate() {
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
