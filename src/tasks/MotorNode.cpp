#include "tasks/MotorNode.h"
#include "drivers/LCDDriver.h"
#include <esp_log.h>

SemaphoreHandle_t xUARTMutex = NULL;

static const char* TAG = "MOTOR_NODE";

MotorNode::MotorNode(const MotorConfig& conf)
    : config(conf)
    , targetSpeed(0)
    , lastSentSpeed(-999999)
    , isEnabled(true) {
}

void MotorNode::hwInit() {
    if (xUARTMutex != NULL && xSemaphoreTake(xUARTMutex, portMAX_DELAY) == pdTRUE) {
        vTaskDelay(pdMS_TO_TICKS(50)); // Wait for shared serial to be ready

        // NOTE: Do NOT call config.serial->end() here. The TMC2209 library's ESP32
        // setup() intentionally skips end() because it causes GPIO matrix issues.
        // The library's begin() inside setup() handles re-initialization correctly.

        driver.begin(*(config.serial), config.address, config.rxPin, config.txPin);

        bool isComm = driver.isSetupAndCommunicating();
        if (!isComm) {
            ESP_LOGE(TAG, "%s UART COMM FAILED at address %d!", config.name, (int)config.address);

            // Read raw chip diagnostics over the UART bus to display exactly what is happening
            uint8_t ver = driver.getVersion();
            char buf[32];
            if (ver == 0x21) {
                snprintf(buf, sizeof(buf), "%s ERR: NOT SETUP (V:0x21)", config.name);
            } else if (ver == 0x00 || ver == 0xFF) {
                snprintf(buf, sizeof(buf), "%s ERR: DEAD BUS (V:0x%02X)", config.name, ver);
            } else {
                snprintf(buf, sizeof(buf), "%s ERR: BAD VER (V:0x%02X)", config.name, ver);
            }
            LCD_setMessage(buf);
        } else {
            ESP_LOGI(TAG, "%s UART COMM OK at address %d", config.name, (int)config.address);
            char buf[32];
            snprintf(buf, sizeof(buf), "%s UART COMM OK", config.name);
            LCD_setMessage(buf);
        }
        xSemaphoreGive(xUARTMutex);
    }
}

void MotorNode::processCommand(const MotorCommand& cmd) {
    switch (cmd.action) {
        case MotorCmdAction::SET_SPEED:
            targetSpeed = (int)cmd.value;
            ESP_LOGD(TAG, "%s set speed: %d", config.name, targetSpeed);
            break;

        case MotorCmdAction::GET_STATE:
            // Telemetry will include state automatically
            break;

        case MotorCmdAction::SET_ENABLED:
            isEnabled = (cmd.value != 0.0f);
            if (xUARTMutex != NULL && xSemaphoreTake(xUARTMutex, portMAX_DELAY) == pdTRUE) {
                if (isEnabled) {
                    driver.enable();
                    ESP_LOGI(TAG, "%s output ENABLED", config.name);
                } else {
                    driver.disable();
                    ESP_LOGI(TAG, "%s output DISABLED", config.name);
                }
                xSemaphoreGive(xUARTMutex);
            }
            break;
    }
}

void MotorNode::hwUpdate() {
    // Apply speed command to driver ONLY on change to avoid flooding the shared half-duplex UART bus
    if (targetSpeed != lastSentSpeed) {
        if (xUARTMutex != NULL && xSemaphoreTake(xUARTMutex, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "%s sending speed %d", config.name, targetSpeed);
            driver.setVelocity(targetSpeed);
            xSemaphoreGive(xUARTMutex);
        }
        lastSentSpeed = targetSpeed;
    }
}

MotorTelemetry MotorNode::generateTelemetry() {
    MotorTelemetry tel;
    tel.targetSpeed = targetSpeed;
    tel.isEnabled = isEnabled;
    return tel;
}

bool MotorNode::setSpeed(int speed) {
    MotorCommand cmd;
    cmd.action = MotorCmdAction::SET_SPEED;
    cmd.value = (float)speed;
    return sendCommand(cmd);
}

bool MotorNode::setEnabled(bool enable) {
    MotorCommand cmd;
    cmd.action = MotorCmdAction::SET_ENABLED;
    cmd.value = enable ? 1.0f : 0.0f;
    return sendCommand(cmd);
}
