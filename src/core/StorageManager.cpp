#include "core/StorageManager.h"
#include "core/SystemState.h"
#include "core/Log.h"
#include <string.h>
#include <EEPROM.h> // STM32 core: emulated EEPROM in flash + eeprom_buffer_* API

static const char *TAG = "STORE";
static const uint16_t MAGIC = 0x9E02;

struct PersistBlob {
  uint16_t magic;
  int32_t pumpSpeed[NUM_PUMPS];
  uint32_t phaseSeconds[NUM_PHASES]; // 0 ⇒ use the caller's default
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
  eeprom_buffer_flush(); // RAM buffer → flash (page erase+write, ~tens of ms)
}

void StorageManager::init() {
  blobRead();
  if (blob.magic != MAGIC) {
    memset(&blob, 0, sizeof(blob));
    blob.magic = MAGIC;
    for (int i = 0; i < NUM_PUMPS; i++) blob.pumpSpeed[i] = 5;
    blobWrite();
    PEACH_LOGI(TAG, "flash blob initialised");
  }
}

void StorageManager::savePumpSpeed(int idx, int steps) {
  if (idx < 0 || idx >= NUM_PUMPS) return;
  blob.pumpSpeed[idx] = steps;
  blobWrite();
}

int StorageManager::loadPumpSpeed(int idx, int defaultSteps) {
  if (idx < 0 || idx >= NUM_PUMPS) return defaultSteps;
  return blob.pumpSpeed[idx];
}

void StorageManager::savePhaseTime(int phase, uint32_t seconds) {
  if (phase < 0 || phase >= NUM_PHASES) return;
  blob.phaseSeconds[phase] = seconds;
  blobWrite();
}

uint32_t StorageManager::loadPhaseTime(int phase, uint32_t defaultSeconds) {
  if (phase < 0 || phase >= NUM_PHASES) return defaultSeconds;
  return blob.phaseSeconds[phase] ? blob.phaseSeconds[phase] : defaultSeconds;
}
