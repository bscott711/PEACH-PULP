#pragma once
#include <stdint.h>
#include "core/SystemState.h"

// The dosing sequence is defined on the Pi GUI (add/remove phases, and per phase
// add/remove motors with a speed + a duration) and uploaded whole via
// PROGCLEAR / PROGADD / PROGCOMMIT. The firmware just runs g_program[]. There is
// no built-in phase table any more.
//
// The seed below is only a fallback for a fresh device / bad flash blob. The Pi
// re-uploads the operator's real program on connect. It reproduces the original
// fixed protocol and must match the GUI's default workspace
// (gui/peachpulp/workspace.py default_workspace) and the simulator's
// _seed_program() (gui/peachpulp/sim.py).

#define PUMP_BIT(role) (1u << (role))

// Cosmetic names for log lines.
static const char *const kPumpNames[NUM_PUMPS] = {
    "Sample", "Dye", "Sheath", "Wash", "Antibody", "Wash2", "Spare6", "Spare7",
};

#define SEED_PHASES 4
#define SEED_STEP_SPEED 1000

static const uint8_t kSeedMask[SEED_PHASES] = {
    PUMP_BIT(P_SAMPLE) | PUMP_BIT(P_DYE) | PUMP_BIT(P_SHEATH),
    PUMP_BIT(P_SHEATH) | PUMP_BIT(P_WASH),
    PUMP_BIT(P_SHEATH) | PUMP_BIT(P_ANTIBODY),
    PUMP_BIT(P_SHEATH) | PUMP_BIT(P_WASH2),
};
static const uint32_t kSeedSeconds[SEED_PHASES] = {60, 30, 60, 30};

// Fill dst[0..SEED_PHASES) with the seed program; returns the phase count.
static inline uint8_t buildSeedProgram(ProgramPhase *dst) {
  for (int p = 0; p < SEED_PHASES; p++) {
    dst[p].seconds = kSeedSeconds[p];
    for (int i = 0; i < NUM_PUMPS; i++)
      dst[p].speed[i] = (kSeedMask[p] & PUMP_BIT(i)) ? SEED_STEP_SPEED : 0;
  }
  return SEED_PHASES;
}
