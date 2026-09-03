#pragma once
#include <SoftwareSerial.h>
#include <TMC2209.h>
#include "HardwareConfig.h"

// Thin wrapper over the janelia TMC2209 library, driving the chip in UART
// VACTUAL velocity mode over a per-driver one-wire SoftwareSerial (write-only:
// VACTUAL and all config are unidirectional writes).
class motorDriver {
public:
  void begin(SoftwareSerial &serial, uint32_t enPin);
  void setVelocity(int newSpeed); // signed steps/s
  void stop();
  void enable();
  void disable();

private:
  TMC2209 driver;
};
