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
    vTaskDelay(pdMS_TO_TICKS(50)); // Wait for shared serial to be ready
    
    // Force a clean termination of the serial port before re-opening to bypass the library ESP32 end() bug!
    config.serial->end();
    vTaskDelay(pdMS_TO_TICKS(15));
    
    // Try the configured address first
    driver.begin(*(config.serial), config.address, config.rxPin, config.txPin);
    
    bool isComm = driver.isSetupAndCommunicating();
    if (!isComm) {
        ESP_LOGE(TAG, "Motor %d failed at default address %d. Scanning alternate addresses...", (int)config.address + 1, (int)config.address);
        
        // Scan other addresses cleanly by closing and re-opening serial
        int foundAddr = -1;
        for (int addr = 0; addr < 4; addr++) {
            if (addr == (int)config.address) continue;
            
            config.serial->end();
            vTaskDelay(pdMS_TO_TICKS(20));
            driver.begin(*(config.serial), (TMC2209::SerialAddress)addr, config.rxPin, config.txPin);
            
            if (driver.isSetupAndCommunicating()) {
                foundAddr = addr;
                break;
            }
        }
        
        if (foundAddr != -1) {
            ESP_LOGI(TAG, "Motor %d responded at Address %d!", (int)config.address + 1, foundAddr);
            char buf[32];
            snprintf(buf, sizeof(buf), "M%d FOUND AT ADDR %d", (int)config.address + 1, foundAddr);
            LCD_setMessage(buf);
        } else {
            ESP_LOGE(TAG, "Motor %d UART COMM FAILED on all addresses!", (int)config.address + 1);
            char buf[32];
            snprintf(buf, sizeof(buf), "M%d UART COMM ERROR", (int)config.address + 1);
            LCD_setMessage(buf);
        }
    } else {
        ESP_LOGI(TAG, "Motor %d UART COMM OK at address %d", (int)config.address + 1, (int)config.address);
        char buf[32];
        snprintf(buf, sizeof(buf), "M%d UART COMM OK", (int)config.address + 1);
        LCD_setMessage(buf);
    }
    
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
                // Homing disabled as we have no DIAG pins
                if (xTaskGetTickCount() - homingStartTime > pdMS_TO_TICKS(5000)) {
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
