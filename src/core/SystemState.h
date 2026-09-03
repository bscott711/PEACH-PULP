#pragma once
#include <stdint.h>
#include "rtos.h"

// The Octopus has 8 driver slots. MOTOR0..MOTOR5 are wired (Sample, Dye, Sheath,
// Wash, Antibody, Wash2); MOTOR6/7 are spare. The wire protocol to the Pi is
// 8-wide throughout so the GUI's pump arrays line up 1:1.
#define NUM_PUMPS 8

// The automated sequence is a *variable* list of phases uploaded from the Pi
// GUI (PROGCLEAR / PROGADD / PROGCOMMIT). This caps the committed + staging
// buffers and the flash blob. Mirrors gui/peachpulp/protocol.py MAX_PHASES.
#define MAX_PHASES 32

// Firmware pump index → role → Octopus MOTORn (see HardwareConfig.h kPumpConfigs).
enum PumpRole : uint8_t {
  P_SAMPLE = 0, // MOTOR0
  P_DYE,        // MOTOR1
  P_SHEATH,     // MOTOR2
  P_WASH,       // MOTOR3
  P_ANTIBODY,   // MOTOR4
  P_WASH2,      // MOTOR5
  P_SPARE6,     // MOTOR6
  P_SPARE7,     // MOTOR7
};

// One phase of the automated program: a duration and a signed steps/s target
// for every motor. speed[i] == 0 ⇒ that motor is stopped for the phase.
struct ProgramPhase {
  uint32_t seconds;
  int16_t speed[NUM_PUMPS]; // ±PUMP_SPEED_MAX_STEPS fits int16
};

struct SystemState {
  // Manual / jog speed per pump (VACTUAL steps/s). Set by SPEED/JOG, reported as
  // the live "sp" in telemetry while idle. NOT the sequenced speed.
  int liveSpeedSteps[NUM_PUMPS];

  // Manual per-pump run flag, honoured only while the sequence is idle.
  bool pumpManualRun[NUM_PUMPS];

  // Per-pump holding-torque enable. false = driver de-energised (EN high) so a
  // syringe can be hand-turned; true = normal hold/run.
  bool pumpEnabled[NUM_PUMPS];

  // The committed automated program.
  ProgramPhase program[MAX_PHASES];
  uint8_t nPhases; // 0..MAX_PHASES

  // Sequence state: -1 = idle, 0..nPhases-1 = running that phase.
  int currentPhase;
  TickType_t phaseEndTick;   // absolute tick the current phase expires
  TickType_t phaseStartTick; // absolute tick the current phase began
};
