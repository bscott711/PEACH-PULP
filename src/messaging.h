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
// Mirrors the command grammar in gui/peachpulp/protocol.py.
// ============================================================================
enum class ProtoAction {
  RUN,           // start the sequence from phase 0
  STOP,          // halt to idle (motion stops; holding torque unchanged)
  SKIP,          // advance to the next phase immediately
  SET_SPEED,     // a = pump idx, b = signed steps/s (live/jog speed)
  SET_PHASETIME, // a = phase idx, b = seconds
  SET_ENABLE,    // a = pump idx, b = 0/1 holding torque
  JOG,           // a = pump idx, b = signed steps/s (0 = stop); idle only
  PROG_CLEAR,    // begin staging a new program
  PROG_ADD,      // a = seconds, speeds[] = per-motor; append one staged phase
  PROG_COMMIT,   // swap the staged program into effect
};

struct ProtoCommand {
  ProtoAction action;
  int a;
  int b;
  int16_t speeds[NUM_PUMPS]; // PROG_ADD payload only; ignored otherwise
};

// ============================================================================
// Global queue handles (defined in main.cpp)
// ============================================================================
extern QueueHandle_t pumpCmdQueue[NUM_PUMPS];
extern QueueHandle_t pumpTelQueue[NUM_PUMPS];
extern QueueHandle_t protoCmdQueue; // SerialLink → controller
extern QueueHandle_t stateQueue;    // controller → SerialLink (depth-1 mailbox)
