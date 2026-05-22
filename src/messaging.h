#pragma once
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// ============================================================================
// MOTOR MESSAGING
// ============================================================================

enum class MotorCmdAction {
    SET_SPEED,       // Set velocity (int steps/s)
    START_HOMING,    // Initiate homing sequence
    SET_SG_THRESHOLD,// Set StallGuard threshold
    GET_STATE        // Request state (for telemetry response)
};

struct MotorCommand {
    MotorCmdAction action;
    float value;     // Speed or position value
};

struct MotorTelemetry {
    float currentPosition; // Current Z-axis position
    int targetSpeed;       // Current target speed
    bool isHomed;          // Homing complete flag
    bool isHoming;         // Currently homing flag
    int sgThreshold;       // Current StallGuard threshold
    bool collisionDetected;// Collision flag
};

// Motor 1 Queues
extern QueueHandle_t motor1CmdQueue;
extern QueueHandle_t motor1TelQueue;

// Motor 2 Queues
extern QueueHandle_t motor2CmdQueue;
extern QueueHandle_t motor2TelQueue;
