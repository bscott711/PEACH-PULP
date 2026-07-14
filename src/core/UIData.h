#pragma once
#include "core/SystemState.h"

struct UIData {
  int pumpSpeedPct[NUM_PUMPS];
  bool pumpRunning[NUM_PUMPS];

  uint32_t t1S;
  uint32_t t2S;

  MenuItem menuSel;
  bool inEdit;

  bool wifiConnected;
};
