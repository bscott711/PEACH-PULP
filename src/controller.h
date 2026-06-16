#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <stdint.h>

// --- Configuration ---
#define MOTOR_SPEED_SCALE_FACTOR 166

// --- Event Group Bits ---
#define BIT_HOMING_REQUEST (1 << 0)
#define BIT_AUTO_RUNNING (1 << 1)
#define BIT_AUTO_RESUME (1 << 2)
#define BIT_ESTOP_REQUEST (1 << 3)

enum DeviceMode { IDLE, PICKUP_CELL, DROPOFF_CELL };

struct SystemState {
  DeviceMode mode;
  bool collisionDetected;
  uint32_t collisionTimestamp;
  
  // UI State
  int motor1SpeedSetpoint;
  int motor2SpeedSetpoint;
  bool motor1Running;
  bool motor2Running;

  // Timed Run State
  uint32_t motor1StopTick;
  uint32_t motor2StopTick;
};

extern SystemState systemState;
extern SemaphoreHandle_t systemStateMutex;
extern EventGroupHandle_t controlEvents;

// Queue handles declared in controller.cpp, extern here for access
extern QueueHandle_t motor1CmdQueue;
extern QueueHandle_t motor1TelQueue;
extern QueueHandle_t motor2CmdQueue;
extern QueueHandle_t motor2TelQueue;

void initSystemState();

// FreeRTOS task entries
void controller_task(void *pvParameters);