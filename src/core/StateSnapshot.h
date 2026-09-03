#pragma once
#include <stdint.h>
#include "core/SystemState.h"

// Immutable per-cycle snapshot published by controller_task into stateQueue
// (depth-1 mailbox) and consumed by SerialLink to build the telemetry line.
struct StateSnapshot {
  int currentPhase;      // -1 idle, else 0..nPhases-1
  uint8_t nPhases;       // length of the committed program
  uint32_t phaseRemainingS;

  // telemetry "sp": the running phase's speed for this pump while a sequence
  // runs, else the live/jog speed. Matches gui/peachpulp/sim.py.
  int pumpSpeedSteps[NUM_PUMPS];
  bool pumpRunning[NUM_PUMPS]; // commanded non-zero this cycle
  bool pumpEnabled[NUM_PUMPS];
};
