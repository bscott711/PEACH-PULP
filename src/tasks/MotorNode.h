#pragma once
#include "tasks/ActiveMotionNode.h"
#include "messaging.h"
#include "drivers/MotorDriver.h"
#include <Preferences.h>
#include <freertos/semphr.h>

extern SemaphoreHandle_t xUARTMutex;

struct MotorConfig {
    HardwareSerial* serial;
    TMC2209::SerialAddress address;
    int rxPin;
    int txPin;
    const char* nvsNamespace;
};

/**
 * @brief Stepper motor control node using Active Object pattern.
 * 
 * Encapsulates TMC2209 driver, limit management, SG4 homing state,
 * and float-based position tracking with lock-free message passing.
 */
class MotorNode : public ActiveMotionNode<MotorCommand, MotorTelemetry> {
private:
    MotorConfig config;

    // TMC2209 driver instance
    motorDriver driver;
    
    // Position tracking (float units)
    float currentPosition;
    int targetSpeed;
    
    // Speed optimization state tracking
    int lastSentSpeed;
    bool lastSentLocked;
    
    // Homing and collision state
    bool isHomed;
    bool isHoming;
    bool collisionDetected;
    bool motorLocked;
    
    // StallGuard threshold
    int sgThreshold;
    
    // NVS storage
    Preferences preferences;
    
    // Homing state machine
    enum HomingState { H_IDLE, H_MOVING, H_BLIND_WAIT, H_POLLING };
    HomingState homingState;
    TickType_t homingStartTime;
    
public:
    MotorNode(const MotorConfig& conf);
    virtual ~MotorNode();
    
    // Override base class pure virtuals
    void hwInit() override;
    void processCommand(const MotorCommand& cmd) override;
    void hwUpdate() override;
    MotorTelemetry generateTelemetry() override;
    
    // Convenience methods for sending commands
    bool setSpeed(int speed);
    bool startHoming();
    bool setSGThreshold(int threshold);
    
    // Getters for state
    float getPosition() const { return currentPosition; }
    bool getIsHomed() const { return isHomed; }
    int getSGThreshold() const { return sgThreshold; }
};
