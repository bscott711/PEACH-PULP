#pragma once
#include <stdint.h>
#include "core/SystemState.h" // ProgramPhase, NUM_PUMPS, MAX_PHASES

// Persists operator settings to the STM32 emulated-EEPROM flash page.
// Blob v3: per-pump live/jog speeds + the whole automated program.
class StorageManager {
public:
  static void init();

  static void saveLiveSpeed(int idx, int steps);
  static int loadLiveSpeed(int idx, int defaultSteps);

  // The program is persisted as a whole. loadProgram fills dst[0..nPhases) and
  // returns the phase count (0 ⇒ nothing valid stored; caller uses the seed).
  static void saveProgram(const ProgramPhase *program, uint8_t nPhases);
  static uint8_t loadProgram(ProgramPhase *dst);
};
