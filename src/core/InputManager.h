#pragma once

class InputManager {
public:
    static void init();
    static void process();

private:
    static void handlePumpEncoder(int idx);
    static void handleMenuEncoder();
};
