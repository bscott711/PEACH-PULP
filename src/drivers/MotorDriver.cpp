#include "drivers/MotorDriver.h"
#include "controller.h"

void motorDriver::begin(HardwareSerial &serial,
                        TMC2209::SerialAddress address, int rxPin, int txPin) {
  // Replicate the exact steps from the successful diagnostic scan:
  // 1. Force TX high to wake up the TMC2209 from standby
  pinMode(txPin, OUTPUT);
  digitalWrite(txPin, HIGH);
  vTaskDelay(pdMS_TO_TICKS(50));

  // 2. End and restart the serial to ensure a perfectly clean driver state
  serial.end();
  vTaskDelay(pdMS_TO_TICKS(10));
  serial.begin(SERIAL_BAUD_RATE, SERIAL_8N1, rxPin, txPin);
  vTaskDelay(pdMS_TO_TICKS(20));

  // 3. Setup TMC2209 passing -1, -1 so the library doesn't mess with the GPIO matrix we just set up
  driver.setup(serial, SERIAL_BAUD_RATE, address, -1, -1);

  // IMPORTANT: Do NOT call pinMode() here — Serial1.begin() configures the matrix.
  // We removed setReplyDelay(2) because PEACH_PIT and the diagnostic script don't use it,
  // and it could be interfering with the driver's default communication timing.

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