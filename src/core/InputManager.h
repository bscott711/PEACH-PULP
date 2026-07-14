#pragma once
#include "core/UIData.h"

class InputManager {
public:
    static void init();
    static void process();
    static void populateUIData(UIData& data);

private:
    static void handlePumpEncoder(int idx);
    static void handleMenuEncoder();
};
