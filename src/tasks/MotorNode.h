#pragma once
#include "tasks/ActiveMotionNode.h"
#include "messaging.h"
#include "drivers/MotorDriver.h"
#include <freertos/semphr.h>

extern SemaphoreHandle_t xUARTMutex;

struct MotorConfig {
    HardwareSerial* serial;
    TMC2209::SerialAddress address;
    int rxPin;
    int txPin;
    const char* name; // Label for logs/UI, e.g. "SAMPLE", "DYE", "WASH"
};

/**
 * @brief Pump (stepper) control node using the Active Object pattern.
 *
 * Runs the TMC2209 in UART velocity mode. Pumps are open-loop — no homing,
 * no position tracking, no collision detection (no DIAG pins wired).
 */
class MotorNode : public ActiveMotionNode<MotorCommand, MotorTelemetry> {
private:
    MotorConfig config;

    // TMC2209 driver instance
    motorDriver driver;

    int targetSpeed;

    // Speed optimization state tracking (avoid flooding the shared half-duplex UART)
    int lastSentSpeed;

    bool isEnabled; // Output enabled state

public:
    MotorNode(const MotorConfig& conf);

    // Override base class pure virtuals
    void hwInit() override;
    void processCommand(const MotorCommand& cmd) override;
    void hwUpdate() override;
    MotorTelemetry generateTelemetry() override;

    // Convenience methods for sending commands
    bool setSpeed(int speed);
    bool setEnabled(bool enable);
};
