#pragma once
#include <stdint.h>
#include "core/SystemState.h"

// Immutable per-cycle snapshot published by controller_task into stateQueue
// (depth-1 mailbox) and consumed by SerialLink to build the telemetry line.
// Replaces the ESP32 UIData that fed the OLED.
struct StateSnapshot {
  int currentPhase;          // -1 idle, 0..NUM_PHASES-1 running
  uint32_t phaseRemainingS;
  int pumpSpeedSteps[NUM_PUMPS];
  bool pumpRunning[NUM_PUMPS]; // commanded non-zero this cycle
  bool pumpEnabled[NUM_PUMPS];
  uint32_t phaseSeconds[NUM_PHASES];
  bool estopLatched;
};
