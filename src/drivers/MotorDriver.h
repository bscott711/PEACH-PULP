#pragma once
#include <SoftwareSerial.h>
#include <TMC2209.h>
#include "HardwareConfig.h"

// Thin wrapper over the janelia TMC2209 library, driving the chip in UART
// VACTUAL velocity mode over a per-driver one-wire SoftwareSerial. Motion and
// config are unidirectional writes; readSettings() is the one place we turn the
// one-wire link around to read back (half-duplex), used only for diagnostics.
class motorDriver {
public:
  void begin(SoftwareSerial &serial, uint32_t enPin);

  // Blindly re-push every config register (StealthChop / current / microsteps /
  // CoolStep). A TMC2209 that briefly loses VMOT reloads its OTP defaults and
  // goes loud (SpreadCycle, analog current); calling this on a timer is the
  // automatic version of the manual "toggle Hold/Free" recovery. Leaves VACTUAL
  // and the EN pin untouched — the caller re-asserts those.
  void reassertConfig();

  // Half-duplex read-back of the live register state (listens on the one-wire
  // pin, then reads GCONF/CHOPCONF/PWMCONF — ~4 UART reads, ~25 ms). Boot-time
  // diagnostics only.
  TMC2209::Settings readSettings(SoftwareSerial &serial);

  // Cheap "is this driver still answering with serial-mode config?" — one GCONF
  // read (~6-14 ms). For the periodic runtime health check. Diagnostics only —
  // never gate motion on this; software-UART RX timing is not guaranteed.
  bool verifyComms(SoftwareSerial &serial);

  void setVelocity(int newSpeed); // signed steps/s
  void stop();
  void enable();
  void disable();

private:
  void applyConfig(); // the register writes shared by begin() and reassertConfig()
  TMC2209 driver;
};
