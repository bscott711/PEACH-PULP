#include "tasks/MotorNode.h"
#include "drivers/LCDDriver.h"
#include <esp_log.h>

static const char* TAG = "MOTOR_NODE";

MotorNode::MotorNode(const MotorConfig& conf)
    : config(conf)
    , currentPosition(0.0f)
    , targetSpeed(0)
    , isHomed(false)
    , isHoming(false)
    , collisionDetected(false)
    , motorLocked(false)
    , sgThreshold(16)
    , homingStartTime(0) {
}

MotorNode::~MotorNode() {
    preferences.end();
}

void MotorNode::hwInit() {
    // Initialize hardware pins and TMC2209 driver
    pinMode(config.diagPin, INPUT_PULLDOWN);
    vTaskDelay(pdMS_TO_TICKS(50)); // Wait for shared serial to be ready
    driver.begin(*(config.serial), config.address, config.rxPin, config.txPin);
    
    // Always require re-homing on boot (clears stale NVS homing data)
    isHomed = false;
    currentPosition = 0.0f;
}

void MotorNode::processCommand(const MotorCommand& cmd) {
    switch (cmd.action) {
        case MotorCmdAction::SET_SPEED:
            targetSpeed = (int)cmd.value;
            ESP_LOGD(TAG, "Set speed: %d", targetSpeed);
            break;
            
        case MotorCmdAction::START_HOMING:
            if (homingState == H_IDLE && !motorLocked) {
                homingState = H_MOVING;
                isHoming = true;
                ESP_LOGI(TAG, "Homing sequence initiated");
            }
            break;
            
        case MotorCmdAction::SET_SG_THRESHOLD:
            sgThreshold = (int)cmd.value;
            driver.updateSGThreshold(sgThreshold);
            ESP_LOGI(TAG, "SG threshold updated to %d", sgThreshold);
            break;
            
        case MotorCmdAction::GET_STATE:
            // Telemetry will include state automatically
            break;
    }
}

void MotorNode::hwUpdate() {
    
    // Unlock motor if collision was cleared
    if (motorLocked && targetSpeed == 0) {
        motorLocked = false;
        collisionDetected = false;
        LCD_setMessage("MOTOR UNLOCKED");
        ESP_LOGI(TAG, "Motor unlocked after collision clear");
    }
    
    // --- HOMING STATE MACHINE ---
    if (homingState != H_IDLE) {
        switch (homingState) {
            case H_MOVING:
                driver.setupHoming();
                driver.setVelocity(-20000);
                homingStartTime = xTaskGetTickCount();
                homingState = H_BLIND_WAIT;
                break;
                
            case H_BLIND_WAIT:
                // Wait 1 second before listening to avoid static friction spike
                if (xTaskGetTickCount() - homingStartTime >= pdMS_TO_TICKS(1000)) {
                    ESP_LOGI(TAG, "Listening to DIAG pin");
                    homingState = H_POLLING;
                }
                break;
                
            case H_POLLING:
                // Check for collision OR timeout (5 seconds)
                if (digitalRead(config.diagPin) == HIGH) {
                    driver.setVelocity(0);
                    ESP_LOGI(TAG, "Homing complete!");
                    
                    driver.finishHoming(sgThreshold);
                    
                    currentPosition = 0.0f;
                    isHomed = true;
                    isHoming = false;
                    targetSpeed = 0;
                    homingState = H_IDLE;
                    
                    // Save homing state to NVS
                    if (preferences.begin(config.nvsNamespace, false)) {
                        preferences.putBool("isHomed", true);
                        preferences.putFloat("pos", 0.0f);
                        preferences.end();
                    }
                } else if (xTaskGetTickCount() - homingStartTime > pdMS_TO_TICKS(5000)) {
                    ESP_LOGE(TAG, "Homing timeout - aborting");
                    LCD_setMessage("Homing: TIMEOUT");
                    
                    driver.setVelocity(0);
                    driver.finishHoming(sgThreshold);
                    
                    isHoming = false;
                    targetSpeed = 0;
                    homingState = H_IDLE;
                }
                break;
                
            default:
                break;
        }
        return;  // Skip normal operation during homing
    }
    
    // --- LIVE POSITION TRACKING & LIMITS ---
    if (!motorLocked && targetSpeed != 0) {
        // Update position based on velocity
        float deltaPos = (1.372e-6f * (float)targetSpeed * (float)TASK_UPDATE_INTERVAL_MS);
        currentPosition += deltaPos;
        
        // Home position hard stop
        if (isHomed && currentPosition <= 0.0f && targetSpeed < 0) {
            targetSpeed = 0;
        }
    }
    
    // Apply speed command to driver
    if (motorLocked) {
        driver.stop();
    } else {
        driver.setVelocity(targetSpeed);
    }
    
    // Save state when stopped and homed
    if (targetSpeed == 0 && isHomed) {
        if (preferences.begin(config.nvsNamespace, false)) {
            preferences.putBool("isHomed", isHomed);
            preferences.putFloat("pos", currentPosition);
            preferences.end();
        }
    }
}

MotorTelemetry MotorNode::generateTelemetry() {
    MotorTelemetry tel;
    tel.currentPosition = currentPosition;
    tel.targetSpeed = targetSpeed;
    tel.isHomed = isHomed;
    tel.isHoming = isHoming;
    tel.sgThreshold = sgThreshold;
    tel.collisionDetected = collisionDetected || motorLocked;
    return tel;
}

bool MotorNode::setSpeed(int speed) {
    MotorCommand cmd;
    cmd.action = MotorCmdAction::SET_SPEED;
    cmd.value = (float)speed;
    return sendCommand(cmd);
}

bool MotorNode::startHoming() {
    MotorCommand cmd;
    cmd.action = MotorCmdAction::START_HOMING;
    cmd.value = 0;
    return sendCommand(cmd);
}

bool MotorNode::setSGThreshold(int threshold) {
    MotorCommand cmd;
    cmd.action = MotorCmdAction::SET_SG_THRESHOLD;
    cmd.value = (float)threshold;
    return sendCommand(cmd);
}
