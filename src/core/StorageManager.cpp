#include "core/StorageManager.h"
#include "core/Log.h"
#include "core/Microstep.h"
#include <string.h>
#include <EEPROM.h> // STM32 core: emulated EEPROM in flash + eeprom_buffer_* API

static const char *TAG = "STORE";
static const uint16_t MAGIC = 0x9E03; // v3 layout

struct PersistBlob {
  uint16_t magic;
  uint8_t nPhases; // 0 ⇒ caller uses the seed program
  // Microsteps code (core/Microstep.h): 0 = never stored → 16, 1..9 = log2(n)+1.
  // Reuses the old pad byte (always written as 0), so the v3 layout and MAGIC are
  // unchanged: an existing blob keeps its program and boots at 16 microsteps.
  uint8_t msCode;
  int32_t liveSpeed[NUM_PUMPS];
  ProgramPhase program[MAX_PHASES];
};

static PersistBlob blob;

static void blobRead() {
  eeprom_buffer_fill(); // flash → RAM buffer
  uint8_t *p = (uint8_t *)&blob;
  for (size_t i = 0; i < sizeof(blob); i++) p[i] = eeprom_buffered_read_byte(i);
}

static void blobWrite() {
  uint8_t *p = (uint8_t *)&blob;
  for (size_t i = 0; i < sizeof(blob); i++) eeprom_buffered_write_byte(i, p[i]);
  eeprom_buffer_flush(); // RAM buffer → flash — BLOCKS ~1 s on an F4 sector erase
}

void StorageManager::init() {
  blobRead();
  if (blob.magic != MAGIC || blob.nPhases > MAX_PHASES) {
    memset(&blob, 0, sizeof(blob));
    blob.magic = MAGIC;
    blob.nPhases = 0;
    blobWrite();
    PEACH_LOGI(TAG, "flash blob initialised (v3, %u bytes)",
               (unsigned)sizeof(blob));
  }
}

void StorageManager::saveLiveSpeed(int idx, int steps) {
  if (idx < 0 || idx >= NUM_PUMPS) return;
  blob.liveSpeed[idx] = steps;
  blobWrite();
}

int StorageManager::loadLiveSpeed(int idx, int defaultSteps) {
  if (idx < 0 || idx >= NUM_PUMPS) return defaultSteps;
  return blob.liveSpeed[idx];
}

void StorageManager::saveProgram(const ProgramPhase *program, uint8_t nPhases) {
  if (nPhases > MAX_PHASES) nPhases = MAX_PHASES;
  blob.nPhases = nPhases;
  memset(blob.program, 0, sizeof(blob.program));
  memcpy(blob.program, program, sizeof(ProgramPhase) * nPhases);
  blobWrite();
}

void StorageManager::saveMicrosteps(int microsteps) {
  uint8_t code = microstepsEncode(microsteps);
  if (code == 0 || code == blob.msCode) return; // invalid, or unchanged: spare the flash
  blob.msCode = code;
  blobWrite();
}

int StorageManager::loadMicrosteps() { return microstepsDecode(blob.msCode); }

uint8_t StorageManager::loadProgram(ProgramPhase *dst) {
  if (blob.nPhases == 0 || blob.nPhases > MAX_PHASES) return 0;
  memcpy(dst, blob.program, sizeof(ProgramPhase) * blob.nPhases);
  return blob.nPhases;
}
