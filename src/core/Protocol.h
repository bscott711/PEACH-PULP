#pragma once
#include <stdint.h>
#include "core/SystemState.h"

// The dosing protocol as a phase table. Each phase runs a fixed set of pumps
// (each at its configured pumpSpeedSteps) for phaseSeconds[phase], then the
// controller advances; after the last phase it returns to idle.
//
//   Phase 1 : Sample + Dye + Sheath
//   Phase 2 : Sheath + Wash
//   Phase 3 : Sheath + Antibody
//   Phase 4 : Sheath + Wash 2
//
// Sheath (P_SHEATH) runs in every phase. Replaces the hardcoded PROTO_PHASE1/2
// switch from the ESP32 controller.

#define PUMP_BIT(role) (1u << (role))

struct ProtocolPhase {
  uint8_t activeMask; // bit i set ⇒ pump i runs at pumpSpeedSteps[i]
};

static const ProtocolPhase kProtocol[NUM_PHASES] = {
    {PUMP_BIT(P_SAMPLE) | PUMP_BIT(P_DYE) | PUMP_BIT(P_SHEATH)},
    {PUMP_BIT(P_SHEATH) | PUMP_BIT(P_WASH)},
    {PUMP_BIT(P_SHEATH) | PUMP_BIT(P_ANTIBODY)},
    {PUMP_BIT(P_SHEATH) | PUMP_BIT(P_WASH2)},
};

// Fallback durations used until the operator sets + persists their own.
static const uint32_t kDefaultPhaseSeconds[NUM_PHASES] = {60, 30, 60, 30};

static const char *const kPhaseNames[NUM_PHASES] = {
    "P1 Sample+Dye+Sheath",
    "P2 Sheath+Wash",
    "P3 Sheath+Antibody",
    "P4 Sheath+Wash2",
};

static const char *const kPumpNames[NUM_PUMPS] = {
    "Sample", "Dye", "Sheath", "Wash", "Antibody", "Wash2", "Spare6", "Spare7",
};
