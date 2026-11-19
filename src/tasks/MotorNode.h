#pragma once
#include <SoftwareSerial.h>
#include "tasks/ActiveMotionNode.h"
#include "messaging.h"
#include "drivers/MotorDriver.h"
#include "core/Microstep.h"
#include "HardwareConfig.h"
#include "rtos.h"

// Serializes all TMC2209 software-UART writes so the per-driver one-wire TX
// windows never overlap. Created in main.cpp before the pump tasks start.
extern SemaphoreHandle_t xUARTMutex;

/**
 * Pump control node (Active Object). One per Octopus driver slot.
 * TMC2209 in UART VACTUAL velocity mode, open-loop — no homing, no position.
 */
class MotorNode : public ActiveMotionNode<MotorCommand, MotorTelemetry> {
private:
  MotorConfig config;
  SoftwareSerial swSerial; // one-wire: rx == tx == config.uartPin
  motorDriver driver;

  int targetSpeed;   // physical speed, steps/s at 1/16 microstep (scaled on send)
  int lastSentSpeed; // send-on-change guard for the software UART
  bool isEnabled;
  uint16_t microsteps; // current microsteps/step; sole owner, passed to every config push

  bool commOk;              // last UART read-back succeeded (diagnostic only)
  uint8_t commFail;         // consecutive failed read-backs (debounces the warning)
  uint32_t reassertAccumMs; // ms accumulated toward the next full-config re-push

  void reassertDriver(); // blind full-config re-push + debounced health re-check

public:
  // microsteps: boot value (flash, default 16) so the driver is configured once, correctly.
  explicit MotorNode(const MotorConfig &conf, uint16_t initialMicrosteps = MICROSTEPS_DEFAULT);

  void hwInit() override;
  void processCommand(const MotorCommand &cmd) override;
  void hwUpdate() override;
  MotorTelemetry generateTelemetry() override;

  bool setSpeed(int speed);
  bool setEnabled(bool enable);
  bool setMicrosteps(uint16_t n);
};
