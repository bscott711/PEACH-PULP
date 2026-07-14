#include "core/StorageManager.h"
#include <Preferences.h>
#include "core/NetworkManager.h"
#include <cstdio>

static const char* TAG = "STORAGE";
static Preferences prefs;

void StorageManager::init() {
    if (!prefs.begin("peach", false)) {
        PEACH_LOGE(TAG, "Failed to open NVS namespace on init");
    }
}

void StorageManager::savePumpSpeed(int idx, int pct) {
    char key[8];
    snprintf(key, sizeof(key), "spd%d", idx);
    prefs.putInt(key, pct);
}

int StorageManager::loadPumpSpeed(int idx, int defaultPct) {
    char key[8];
    snprintf(key, sizeof(key), "spd%d", idx);
    return prefs.getInt(key, defaultPct);
}

void StorageManager::saveT1(uint32_t seconds) {
    prefs.putUInt("t1", seconds);
}

uint32_t StorageManager::loadT1(uint32_t defaultSeconds) {
    return prefs.getUInt("t1", defaultSeconds);
}

void StorageManager::saveT2(uint32_t seconds) {
    prefs.putUInt("t2", seconds);
}

uint32_t StorageManager::loadT2(uint32_t defaultSeconds) {
    return prefs.getUInt("t2", defaultSeconds);
}
