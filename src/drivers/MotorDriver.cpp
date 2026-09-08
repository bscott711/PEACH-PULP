#include "drivers/MotorDriver.h"
#include "rtos.h"

void motorDriver::applyConfig() {
  driver.setRMSCurrent(RUN_CURRENT_MA, SENSE_RESISTOR_OHMS, HOLD_CURRENT_MULTIPLIER);
  driver.disableAnalogCurrentScaling();
  driver.disableCoolStep();
  driver.enableStealthChop();
  driver.setMicrostepsPerStep(16);
  driver.setCoolStepDurationThreshold(0);
}

void motorDriver::begin(SoftwareSerial &serial, uint32_t enPin) {
  // SoftwareSerial overload — address 0 (each driver has its own one-wire link).
  // Any internal read attempts during setup() time out harmlessly; the VACTUAL
  // and config writes are what matter.
  driver.setup(serial, SERIAL_BAUD_RATE);

  driver.setHardwareEnablePin(enPin);

  applyConfig();

  driver.enable();
  driver.moveAtVelocity(0);
}

void motorDriver::reassertConfig() { applyConfig(); }

TMC2209::Settings motorDriver::readSettings(SoftwareSerial &serial) {
  // Half-duplex: claim the RX listener slot for this one-wire pin. The janelia
  // write path flips the pin back to RX after each datagram (see
  // SoftwareSerial::setRXTX), so the read replies land in the buffer.
  serial.listen();
  TMC2209::Settings s = driver.getSettings();
  serial.stopListening();
  return s;
}

bool motorDriver::verifyComms(SoftwareSerial &serial) {
  serial.listen();
  bool ok = driver.isSetupAndCommunicating(); // one GCONF read
  serial.stopListening();
  return ok;
}

void motorDriver::setVelocity(int newSpeed) {
  newSpeed = constrain(newSpeed, -MOTOR_MAX_SAFE_STEPS, MOTOR_MAX_SAFE_STEPS);

  // Sign selects direction via the shaft bit (mapping fixed in commit 3979330).
  if (newSpeed > 0) {
    driver.enableInverseMotorDirection();
  } else {
    driver.disableInverseMotorDirection();
  }
  driver.moveAtVelocity(abs(newSpeed));
}

void motorDriver::stop() { driver.moveAtVelocity(0); }
void motorDriver::enable() { driver.enable(); }
void motorDriver::disable() { driver.disable(); }
