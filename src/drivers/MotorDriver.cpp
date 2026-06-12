#include "drivers/MotorDriver.h"
#include "controller.h"

void motorDriver::begin(HardwareSerial &serial,
                        TMC2209::SerialAddress address, int rxPin, int txPin) {
  // Pass -1, -1 for pins to prevent TMC2209 library from repeatedly re-configuring the 
  // GPIO matrix (which it does via Serial1.begin(baud, SERIAL_8N1, rx, tx)).
  // We already cleanly initialized Serial1 with the correct pins in main.cpp setup().
  driver.setup(serial, SERIAL_BAUD_RATE, address, -1, -1);

  // IMPORTANT: Do NOT call pinMode() here — Serial2.begin() inside driver.setup()
  // already configures the GPIO matrix to connect rxPin/txPin to the UART2 peripheral.
  // Calling pinMode() would override that mapping and disconnect the pins from UART2,
  // causing a "dead bus" (version reads return 0x00/0xFF).

  // Required when multiple TMC2209 drivers share a single-wire UART bus
  driver.setReplyDelay(2);

  driver.setRunCurrent(RUN_CURRENT_PERCENT);

  driver.disableCoolStep();
  driver.enableStealthChop();
  driver.setMicrostepsPerStep(16);
  driver.setCoolStepDurationThreshold(0);

  driver.setStallGuardThreshold(16);

  driver.enable();

  driver.moveAtVelocity(0);
  vTaskDelay(pdMS_TO_TICKS(200));
}

void motorDriver::setVelocity(int newSpeed) {
  newSpeed = constrain(newSpeed, -MOTOR_MAX_SAFE_STEPS, MOTOR_MAX_SAFE_STEPS);

  if (newSpeed > 0) {
    driver.disableInverseMotorDirection();
  } else {
    driver.enableInverseMotorDirection();
  }
  driver.moveAtVelocity(abs(newSpeed));
}

void motorDriver::stop() { driver.moveAtVelocity(0); }

void motorDriver::setupHoming() {
  // Serial.println("Starting Hardware Sensorless Homing...");

  driver.setRunCurrent(70);
  driver.enableStealthChop();
  driver.setCoolStepDurationThreshold(1048575);
  driver.setStallGuardThreshold(15);
}

void motorDriver::finishHoming(int restoreThreshold) {
  driver.setRunCurrent(RUN_CURRENT_PERCENT);
  driver.enableStealthChop();
  driver.setCoolStepDurationThreshold(0);
  updateSGThreshold(restoreThreshold);
}

void motorDriver::updateSGThreshold(int newThreshold) {
  newThreshold = constrain(newThreshold, 0, 255);
  driver.setStallGuardThreshold(newThreshold);
}

bool motorDriver::isSetupAndCommunicating() {
  return driver.isSetupAndCommunicating();
}

bool motorDriver::isCommunicating() {
  return driver.isCommunicating();
}

uint8_t motorDriver::getVersion() {
  return driver.getVersion();
}