#include "controller.h"
#include "messaging.h"
#include "tasks/MotorNode.h"
#include "tasks/LCD_task.h"
#include "drivers/LCDDriver.h"
#include "core/NetworkManager.h"
#include "HardwareConfig.h"
#include <ArduinoOTA.h>

extern TaskHandle_t lcdTaskHandle;

// Configure Motor 1 (Address 0)
const MotorConfig motor1Config = {
    .serial = &Serial1, // Use Serial1 (UART1)
    .address = TMC2209::SERIAL_ADDRESS_0,
    .rxPin = RXD1,
    .txPin = TXD1,
    .nvsNamespace = "peach_m1"
};

// Configure Motor 2 (Address 1)
const MotorConfig motor2Config = {
    .serial = &Serial1, // Use Serial1 (UART1)
    .address = TMC2209::SERIAL_ADDRESS_1,
    .rxPin = RXD1,
    .txPin = TXD1,
    .nvsNamespace = "peach_m2"
};

// Global Node instances (extern in controller.cpp)
MotorNode g_motor1Node(motor1Config);
MotorNode g_motor2Node(motor2Config);

void setup() {
  // Create shared UART mutex before starting motor tasks
  xUARTMutex = xSemaphoreCreateMutex();

  // USB serial for debug logging — motors use Serial1 on the TMC UART pins
  Serial.begin(115200);

  // Global wake up for TMC2209s on the shared bus
  pinMode(TXD1, OUTPUT);
  digitalWrite(TXD1, HIGH);
  delay(50);

  // Pre-initialize Serial1 in setup() context
  Serial1.begin(SERIAL_BAUD_RATE, SERIAL_8N1, RXD1, TXD1);
  delay(50);

  // Initialize System State from NVS
  initSystemState();

  LCDInit();              // OLED splash screen
  NetworkManager::init(); // WiFi (NVS/scan), mDNS, OTA, log bridge

  // Elevate setup() to Priority 5 (higher than our tasks)
  vTaskPrioritySet(NULL, 5);

  // 1. Start Active Motion Nodes
  if (!g_motor1Node.start("Motor1Node", 4096, 2))
    ESP_LOGE("MAIN", "Failed Motor1Node");
  if (!g_motor2Node.start("Motor2Node", 4096, 2))
    ESP_LOGE("MAIN", "Failed Motor2Node");

  // 2. Link the global messaging queues
  motor1CmdQueue = g_motor1Node.getCmdQueue();
  motor1TelQueue = g_motor1Node.getTelQueue();
  motor2CmdQueue = g_motor2Node.getCmdQueue();
  motor2TelQueue = g_motor2Node.getTelQueue();

  // 3. Create Dependent Tasks
  static int lcd_interval = TASK_REFRESH_LCD;
  xTaskCreate(controller_task, "Controller", 4096, NULL, 3, NULL);
  xTaskCreate(LCD_task, "LCD", 8192, &lcd_interval, 2, &lcdTaskHandle);

  // Restore setup() to Priority 1
  vTaskPrioritySet(NULL, 1);
}

void loop() {
  NetworkManager::handle();
  vTaskDelay(pdMS_TO_TICKS(50));
}
