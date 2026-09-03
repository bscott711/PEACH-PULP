#include <Arduino.h>
#include <stdio.h>
#include "rtos.h"
#include "HardwareConfig.h"
#include "controller.h"
#include "messaging.h"
#include "core/SystemState.h"
#include "core/StateSnapshot.h"
#include "core/SerialLink.h"
#include "core/Log.h"
#include "tasks/MotorNode.h"

// ---- global queue handles (declared extern in messaging.h) ----
QueueHandle_t pumpCmdQueue[NUM_PUMPS];
QueueHandle_t pumpTelQueue[NUM_PUMPS];
QueueHandle_t protoCmdQueue = NULL;
QueueHandle_t stateQueue = NULL;

// ---- pump nodes (heap-allocated in setup so SoftwareSerial members are
//      constructed after the Arduino core is initialised) ----
MotorNode *g_pumps[NUM_PUMPS] = {nullptr};

void setup() {
  Serial.begin(115200); // USB CDC — command + telemetry link to the Pi

  xUARTMutex = xSemaphoreCreateMutex();
  g_serialMutex = xSemaphoreCreateMutex();

  initSystemState(); // storage + systemState + systemStateMutex + controlEvents

  // Start one Active-Object task per pump.
  for (int i = 0; i < NUM_PUMPS; i++) {
    g_pumps[i] = new MotorNode(kPumpConfigs[i]);
    char name[10];
    snprintf(name, sizeof(name), "pump%d", i);
    g_pumps[i]->start(name, STACK_PUMP_NODE, PRIO_PUMP_NODE);
    pumpCmdQueue[i] = g_pumps[i]->getCmdQueue();
    pumpTelQueue[i] = g_pumps[i]->getTelQueue();
  }

  protoCmdQueue = xQueueCreate(16, sizeof(ProtoCommand));
  stateQueue = xQueueCreate(1, sizeof(StateSnapshot));

  xTaskCreate(controller_task, "controller", STACK_CONTROLLER, NULL,
              PRIO_CONTROLLER, NULL);
  xTaskCreate(serialLinkTask, "serial", STACK_SERIAL, NULL, PRIO_SERIAL, NULL);

  // STM32duino FreeRTOS: the scheduler is started explicitly here; loop() is
  // never used (unlike the ESP32 core where setup/loop run as a task).
  vTaskStartScheduler();

  for (;;) {
  } // unreachable
}

void loop() {}
