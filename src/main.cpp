#include "controller.h"
#include "messaging.h"
#include "tasks/MotorNode.h"
#include "tasks/LCD_task.h"
#include "tasks/encoder_task.h"
#include "drivers/LCDDriver.h"
#include "core/NetworkManager.h"
#include "HardwareConfig.h"
#include <ArduinoOTA.h>
#include <Wire.h>

extern TaskHandle_t lcdTaskHandle;

// Sample Pump (TMC2209 address 0)
const MotorConfig samplePumpConfig = {
    .serial = &Serial1, // Use Serial1 (UART1)
    .address = TMC2209::SERIAL_ADDRESS_0,
    .rxPin = RXD1,
    .txPin = TXD1,
    .name = "SMP"
};

// Dye Pump (TMC2209 address 1)
const MotorConfig dyePumpConfig = {
    .serial = &Serial1, // Use Serial1 (UART1)
    .address = TMC2209::SERIAL_ADDRESS_1,
    .rxPin = RXD1,
    .txPin = TXD1,
    .name = "DYE"
};

// Wash Pump (TMC2209 address 2)
const MotorConfig washPumpConfig = {
    .serial = &Serial1, // Use Serial1 (UART1)
    .address = TMC2209::SERIAL_ADDRESS_2,
    .rxPin = RXD1,
    .txPin = TXD1,
    .name = "WSH"
};

// Global Node instances (extern in controller.cpp)
MotorNode g_samplePumpNode(samplePumpConfig);
MotorNode g_dyePumpNode(dyePumpConfig);
MotorNode g_washPumpNode(washPumpConfig);

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

  // Start I2C line (used by the rotary encoders)
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  encoderInit();
  LCDInit();              // OLED splash screen
  NetworkManager::init(); // WiFi (NVS/scan), mDNS, OTA, log bridge

  // Elevate setup() to Priority 5 (higher than our tasks)
  vTaskPrioritySet(NULL, 5);

  // 1. Start Active Motion Nodes
  if (!g_samplePumpNode.start("SamplePump", 4096, 2))
    ESP_LOGE("MAIN", "Failed SamplePump");
  if (!g_dyePumpNode.start("DyePump", 4096, 2))
    ESP_LOGE("MAIN", "Failed DyePump");
  if (!g_washPumpNode.start("WashPump", 4096, 2))
    ESP_LOGE("MAIN", "Failed WashPump");

  // 2. Link the global messaging queues
  samplePumpCmdQueue = g_samplePumpNode.getCmdQueue();
  samplePumpTelQueue = g_samplePumpNode.getTelQueue();
  dyePumpCmdQueue = g_dyePumpNode.getCmdQueue();
  dyePumpTelQueue = g_dyePumpNode.getTelQueue();
  washPumpCmdQueue = g_washPumpNode.getCmdQueue();
  washPumpTelQueue = g_washPumpNode.getTelQueue();

  // 3. Create Dependent Tasks
  static int lcd_interval = TASK_REFRESH_LCD;
  xTaskCreate(encoderTask, "EncoderTask", 4096, NULL, 3, NULL);
  xTaskCreate(controller_task, "Controller", 4096, NULL, 3, NULL);
  xTaskCreate(LCD_task, "LCD", 8192, &lcd_interval, 2, &lcdTaskHandle);

  // Restore setup() to Priority 1
  vTaskPrioritySet(NULL, 1);
}

void loop() {
  NetworkManager::handle();
  vTaskDelay(pdMS_TO_TICKS(50));
}
