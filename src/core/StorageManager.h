#pragma once
#include <stdint.h>

class StorageManager {
public:
    static void init();

    static void savePumpSpeed(int idx, int steps);
    static int loadPumpSpeed(int idx, int defaultSteps);

    static void saveT1(uint32_t seconds);
    static uint32_t loadT1(uint32_t defaultSeconds);

    static void saveT2(uint32_t seconds);
    static uint32_t loadT2(uint32_t defaultSeconds);
};
