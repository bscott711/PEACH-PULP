#pragma once
#include <stdint.h>
#include "rtos.h"

// Octopus has 8 driver slots. 6 pump roles are used by the protocol today.
#define NUM_PUMPS 8
#define NUM_PHASES 4

// Firmware pump index → role → Octopus MOTORn (see HardwareConfig.h kPumpConfigs).
enum PumpRole : uint8_t {
  P_SAMPLE = 0, // MOTOR0
  P_DYE,        // MOTOR1
  P_SHEATH,     // MOTOR2  — runs in every phase
  P_WASH,       // MOTOR3
  P_ANTIBODY,   // MOTOR4
  P_WASH2,      // MOTOR5
  P_SPARE6,     // MOTOR6
  P_SPARE7,     // MOTOR7
};

struct SystemState {
  // Signed steps/s target for each pump (VACTUAL units). Operator sets these
  // via the Pi GUI; persisted to flash.
  int pumpSpeedSteps[NUM_PUMPS];

  // Manual per-pump run flag, honoured only while the protocol is idle.
  bool pumpManualRun[NUM_PUMPS];

  // Per-pump holding-torque enable. false = driver de-energised (EN high) so a
  // syringe can be hand-turned; true = normal hold/run.
  bool pumpEnabled[NUM_PUMPS];

  // Protocol phase durations (seconds), GUI-editable + flash-persisted.
  uint32_t phaseSeconds[NUM_PHASES];

  // Protocol state: -1 = idle, 0..NUM_PHASES-1 = running that phase.
  int currentPhase;
  TickType_t phaseEndTick;   // absolute tick the current phase expires
  TickType_t phaseStartTick; // absolute tick the current phase began
};
