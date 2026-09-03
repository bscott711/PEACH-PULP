#pragma once
#include <SoftwareSerial.h>
#include "tasks/ActiveMotionNode.h"
#include "messaging.h"
#include "drivers/MotorDriver.h"
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

  int targetSpeed;
  int lastSentSpeed; // send-on-change guard for the software UART
  bool isEnabled;

public:
  explicit MotorNode(const MotorConfig &conf);

  void hwInit() override;
  void processCommand(const MotorCommand &cmd) override;
  void hwUpdate() override;
  MotorTelemetry generateTelemetry() override;

  bool setSpeed(int speed);
  bool setEnabled(bool enable);
};
