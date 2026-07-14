#pragma once
#include "core/SystemState.h"

struct UIData {
  int pumpSpeedSteps[NUM_PUMPS];
  bool pumpRunning[NUM_PUMPS]; // true if actually commanded to run this cycle
  bool pumpEnabled[NUM_PUMPS]; // true = holding torque on, false = de-energized for jogging

  uint32_t t1S;
  uint32_t t2S;

  MenuItem menuSel;
  bool inEdit;

  bool inMotorMenu;
  int motorMenuSel;

  ProtocolPhase phase;
  uint32_t phaseRemainingS;

  bool wifiConnected;
};
