#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <stdint.h>

#include "core/SystemState.h"

// --- Configuration ---
#define PUMP_SPEED_MAX_STEPS 5000   // practical UI ceiling (motorDriver::setVelocity still hard-clamps at +/-MOTOR_MAX_SAFE_STEPS)
#define PUMP_SPEED_DETENT 10        // steps/s per encoder detent
#define PUMP_SPEED_DETENT_ACCEL 50  // steps/s per detent when spun fast

// --- Event Group Bits ---
#define BIT_AUTO_RUNNING (1 << 1)  // Two-phase protocol currently running
#define BIT_ESTOP_REQUEST (1 << 3) // Cooperative abort request (menu or OTA interlock)
#define BIT_SKIP_REQUEST (1 << 2)  // Manual skip to next phase (Enc3 short-press while running)

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
