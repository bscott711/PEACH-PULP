#pragma once
#include <stdint.h>
#include <freertos/FreeRTOS.h>

#define NUM_PUMPS 3
#define PUMP_SAMPLE 0
#define PUMP_DYE 1
#define PUMP_WASH 2

// Flat menu items navigated by Enc3
enum MenuItem : uint8_t { MENU_START, MENU_MOTORS, MENU_T1, MENU_T2, MENU_COUNT };

// Two-phase protocol: Phase 1 runs Sample+Dye for t1Seconds, then Phase 2
// runs Dye+Wash (Sample stopped) for t2Seconds, then back to idle.
enum ProtocolPhase : uint8_t { PROTO_IDLE, PROTO_PHASE1, PROTO_PHASE2 };

struct SystemState {
  // Pump speeds, signed steps/s (bidirectional; ul/min conversion TBD)
  int pumpSpeedSteps[NUM_PUMPS];

  // Index into kPumpStepLevels (InputManager.cpp) — per-pump encoder step
  // size, cycled by that pump's own encoder long-press.
  int pumpStepSizeIdx[NUM_PUMPS];

  // Manual per-pump run flags, toggled by each pump's own encoder short-press.
  // Only applied while the protocol is idle.
  bool pumpManualRun[NUM_PUMPS];

  // Per-pump holding-torque enable. false = driver de-energized (toff=0) so
  // the syringe can be hand-turned into position; true = normal hold/run.
  bool pumpEnabled[NUM_PUMPS];

  // Protocol timing parameters (seconds), menu-editable + NVS-persisted
  uint32_t t1Seconds;
  uint32_t t2Seconds;

  // Flat menu navigation (Enc3)
  MenuItem menuSel;
  bool inEdit;

  // Motor power submenu (entered from MENU_MOTORS). motorMenuSel indexes
  // 0..NUM_PUMPS-1 for a pump, NUM_PUMPS itself means "BACK".
  bool inMotorMenu;
  int motorMenuSel;

  // Two-phase protocol state
  ProtocolPhase protocolPhase;
  TickType_t phaseEndTick;   // absolute tick when the current phase expires
  TickType_t phaseStartTick; // absolute tick when the current phase began
};
