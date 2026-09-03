#pragma once
#include <stdint.h>

// Persists operator settings to the STM32 emulated-EEPROM flash page.
// Replaces the ESP32 Preferences/NVS layer; the call surface is unchanged
// apart from T1/T2 → per-phase durations.
class StorageManager {
public:
  static void init();

  static void savePumpSpeed(int idx, int steps);
  static int loadPumpSpeed(int idx, int defaultSteps);

  static void savePhaseTime(int phase, uint32_t seconds);
  static uint32_t loadPhaseTime(int phase, uint32_t defaultSeconds);
};
