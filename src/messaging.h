#pragma once
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// ============================================================================
// PUMP (MOTOR) MESSAGING
// ============================================================================

enum class MotorCmdAction {
    SET_SPEED,   // Set velocity (int steps/s)
    GET_STATE,   // Request state (for telemetry response)
    SET_ENABLED  // Enable/disable motor output
};

struct MotorCommand {
    MotorCmdAction action;
    float value;     // Speed value
};

struct MotorTelemetry {
    int targetSpeed;  // Current target speed
    bool isEnabled;   // True if motor driver output is enabled
};

// Sample Pump Queues (TMC2209 address 0)
extern QueueHandle_t samplePumpCmdQueue;
extern QueueHandle_t samplePumpTelQueue;

// Dye Pump Queues (TMC2209 address 1)
extern QueueHandle_t dyePumpCmdQueue;
extern QueueHandle_t dyePumpTelQueue;

// Wash Pump Queues (TMC2209 address 2)
extern QueueHandle_t washPumpCmdQueue;
extern QueueHandle_t washPumpTelQueue;
