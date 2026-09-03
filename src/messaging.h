#pragma once
#include <stdint.h>
#include "rtos.h"
#include "core/SystemState.h"

// ============================================================================
// Pump command  (controller_task → MotorNode, one queue per pump)
// ============================================================================
enum class MotorCmdAction {
  SET_SPEED,   // value = signed steps/s (VACTUAL)
  SET_ENABLED, // value != 0 → holding torque on
};

struct MotorCommand {
  MotorCmdAction action;
  float value;
};

struct MotorTelemetry {
  int targetSpeed;
  bool isEnabled;
};

// ============================================================================
// Protocol command  (SerialLink → controller_task)
// Replaces the encoder-driven InputManager mutations.
// ============================================================================
enum class ProtoAction {
  RUN,           // start the protocol from phase 0
  STOP,          // abort to idle (motion stops; holding torque unchanged)
  ESTOP,         // same as STOP for now
  SKIP,          // advance to the next phase immediately
  SET_SPEED,     // a = pump idx, b = signed steps/s
  SET_PHASETIME, // a = phase idx, b = seconds (persisted)
  SET_ENABLE,    // a = pump idx, b = 0/1 holding torque
  JOG,           // a = pump idx, b = signed steps/s (0 = stop); idle only
};

struct ProtoCommand {
  ProtoAction action;
  int a;
  int b;
};

// ============================================================================
// Global queue handles (defined in main.cpp)
// ============================================================================
extern QueueHandle_t pumpCmdQueue[NUM_PUMPS];
extern QueueHandle_t pumpTelQueue[NUM_PUMPS];
extern QueueHandle_t protoCmdQueue; // SerialLink → controller
extern QueueHandle_t stateQueue;    // controller → SerialLink (depth-1 mailbox)
