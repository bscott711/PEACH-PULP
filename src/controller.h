#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <stdint.h>

#include "core/SystemState.h"

// --- Configuration ---
#define MOTOR_SPEED_SCALE_FACTOR 16.6 // 0.4ml per minute per %

// --- Event Group Bits ---
#define BIT_AUTO_RUNNING (1 << 1)  // Milestone 6: two-phase protocol running
#define BIT_ESTOP_REQUEST (1 << 3) // Milestone 6: cooperative abort request

extern SystemState systemState;
extern SemaphoreHandle_t systemStateMutex;
extern SemaphoreHandle_t encoderStateMutex;
extern EventGroupHandle_t controlEvents;

// Queue handles declared in controller.cpp, extern here for access
extern QueueHandle_t samplePumpCmdQueue;
extern QueueHandle_t samplePumpTelQueue;
extern QueueHandle_t dyePumpCmdQueue;
extern QueueHandle_t dyePumpTelQueue;
extern QueueHandle_t washPumpCmdQueue;
extern QueueHandle_t washPumpTelQueue;

void initSystemState();

// FreeRTOS task entries
void controller_task(void *pvParameters);
