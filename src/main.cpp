#include "controller.h"
#include "messaging.h"
#include "tasks/LCD_task.h"
#include "tasks/MotorNode.h"
#include "drivers/LCDDriver.h"

// Shared UART pins for TMC2209 (Changed to avoid CYD Touch CS conflict on GPIO 33)
#define UART_RX 22
#define UART_TX 27

// DIAG pins for Steppers
#define MOTOR1_DIAG 4
#define MOTOR2_DIAG 27

// Configure Motor 1 (Address 0)
const MotorConfig motor1Config = {
    .serial = &Serial1,
    .address = TMC2209::SERIAL_ADDRESS_0,
    .rxPin = UART_RX,
    .txPin = UART_TX,
    .diagPin = MOTOR1_DIAG,
    .nvsNamespace = "peach_m1"
};

// Configure Motor 2 (Address 1)
const MotorConfig motor2Config = {
    .serial = &Serial1,
    .address = TMC2209::SERIAL_ADDRESS_1,
    .rxPin = UART_RX,
    .txPin = UART_TX,
    .diagPin = MOTOR2_DIAG,
    .nvsNamespace = "peach_m2"
};

// Global Node instances (extern in controller.cpp)
MotorNode g_motor1Node(motor1Config);
MotorNode g_motor2Node(motor2Config);

void setup() {
  // Begin USB serial for debugging/monitoring
  Serial.begin(115200);

  // Initialize System State from NVS
  initSystemState();

  // Inits
  LCDInit();

  // Task Update Intervals
  static int lcd_interval = TASK_REFRESH_LCD;

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
  xTaskCreate(controller_task, "Controller", 4096, NULL, 3, NULL);
  xTaskCreate(LCD_task, "LCD", 8192, &lcd_interval, 2, NULL);

  // Restore setup() to Priority 1
  vTaskPrioritySet(NULL, 1);
}

void loop() {
  // Delete the default Arduino loop task to reclaim its memory stack
  vTaskDelete(NULL);
}