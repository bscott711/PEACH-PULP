#pragma once
#include <Arduino.h>
#include "rtos.h"
#include "core/SystemState.h"

// --- Configuration ---
#define PUMP_SPEED_MAX_STEPS 5000 // UI/protocol ceiling (motorDriver hard-clamps at MOTOR_MAX_SAFE_STEPS)

// --- Event group bits ---
#define BIT_AUTO_RUNNING (1 << 1) // sequence currently running
#define BIT_STOP_REQUEST (1 << 3) // cooperative halt to idle (GUI STOP)
#define BIT_SKIP_REQUEST (1 << 2) // advance to next phase now

extern SystemState systemState;
extern SemaphoreHandle_t systemStateMutex;
extern EventGroupHandle_t controlEvents;

void initSystemState();
void controller_task(void *pvParameters);
