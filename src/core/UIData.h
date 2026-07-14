#pragma once
#include "core/SystemState.h"

struct UIData {
  int pumpSpeedPct[NUM_PUMPS];
  bool pumpRunning[NUM_PUMPS]; // true if actually commanded to run this cycle

  uint32_t t1S;
  uint32_t t2S;

  MenuItem menuSel;
  bool inEdit;

  ProtocolPhase phase;
  uint32_t phaseRemainingS;

  bool wifiConnected;
};
