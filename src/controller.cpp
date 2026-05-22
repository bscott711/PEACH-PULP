#include "controller.h"
#include "messaging.h"
#include "tasks/MotorNode.h"
#include "drivers/LCDDriver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>

// Global queue handles (declared extern in controller.h)
QueueHandle_t motor1CmdQueue;
QueueHandle_t motor1TelQueue;
QueueHandle_t motor2CmdQueue;
QueueHandle_t motor2TelQueue;

// Global Node instances (defined in main.cpp, extern here)
extern MotorNode g_motor1Node;
extern MotorNode g_motor2Node;

SemaphoreHandle_t systemStateMutex;
EventGroupHandle_t controlEvents;

SystemState systemState = {.mode = IDLE,
                           .collisionDetected = false,
                           .collisionTimestamp = 0,
                           .motor1SpeedSetpoint = 50,
                           .motor2SpeedSetpoint = 50,
                           .motor1Running = false,
                           .motor2Running = false};

void initSystemState() {
  systemStateMutex = xSemaphoreCreateMutex();
  controlEvents = xEventGroupCreate();

  if (xSemaphoreTake(systemStateMutex, portMAX_DELAY) == pdTRUE) {
    systemState.mode = IDLE;
    systemState.collisionDetected = false;
    xSemaphoreGive(systemStateMutex);
  }
}

// ============================================================================
// Main Controller Task
// ============================================================================

bool isPointInRect(uint16_t x, uint16_t y, uint16_t rx, uint16_t ry, uint16_t rw, uint16_t rh) {
  return (x >= rx && x <= rx + rw && y >= ry && y <= ry + rh);
}

void controller_task(void *pvParameters) {
  TickType_t lastWakeTime = xTaskGetTickCount();
  const TickType_t CONTROLLER_INTERVAL = pdMS_TO_TICKS(50);
  
  bool wasTouched = false;
  uint32_t lastTouchTime = 0;

  while (1) {
    uint16_t t_x = 0, t_y = 0;
    bool isTouched = getTouchInput(&t_x, &t_y);
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    if (isTouched && !wasTouched && (now - lastTouchTime > 200)) {
      lastTouchTime = now;
      wasTouched = true;
      
      // M1 -
      if (isPointInRect(t_x, t_y, 10, 60, 60, 40)) {
        systemState.motor1SpeedSetpoint = std::max(0, systemState.motor1SpeedSetpoint - 10);
        LCD_setMessage("M1 Speed -");
      }
      // M1 +
      else if (isPointInRect(t_x, t_y, 90, 60, 60, 40)) {
        systemState.motor1SpeedSetpoint = std::min(100, systemState.motor1SpeedSetpoint + 10);
        LCD_setMessage("M1 Speed +");
      }
      // M1 Toggle
      else if (isPointInRect(t_x, t_y, 10, 110, 140, 40)) {
        systemState.motor1Running = !systemState.motor1Running;
        LCD_setMessage(systemState.motor1Running ? "M1 Started" : "M1 Stopped");
      }
      
      // M2 -
      else if (isPointInRect(t_x, t_y, 170, 60, 60, 40)) {
        systemState.motor2SpeedSetpoint = std::max(0, systemState.motor2SpeedSetpoint - 10);
        LCD_setMessage("M2 Speed -");
      }
      // M2 +
      else if (isPointInRect(t_x, t_y, 250, 60, 60, 40)) {
        systemState.motor2SpeedSetpoint = std::min(100, systemState.motor2SpeedSetpoint + 10);
        LCD_setMessage("M2 Speed +");
      }
      // M2 Toggle
      else if (isPointInRect(t_x, t_y, 170, 110, 140, 40)) {
        systemState.motor2Running = !systemState.motor2Running;
        LCD_setMessage(systemState.motor2Running ? "M2 Started" : "M2 Stopped");
      }
      
      // START ALL
      else if (isPointInRect(t_x, t_y, 10, 165, 145, 40)) {
        systemState.motor1Running = true;
        systemState.motor2Running = true;
        LCD_setMessage("All Started");
      }
      // STOP ALL
      else if (isPointInRect(t_x, t_y, 165, 165, 145, 40)) {
        systemState.motor1Running = false;
        systemState.motor2Running = false;
        LCD_setMessage("All Stopped");
      }
    } else if (!isTouched) {
      wasTouched = false;
    }
    
    // Apply speed commands to motors
    int m1Target = systemState.motor1Running ? systemState.motor1SpeedSetpoint * MOTOR_SPEED_SCALE_FACTOR : 0;
    int m2Target = systemState.motor2Running ? systemState.motor2SpeedSetpoint * MOTOR_SPEED_SCALE_FACTOR : 0;
    
    g_motor1Node.setSpeed(m1Target);
    g_motor2Node.setSpeed(m2Target);

    vTaskDelayUntil(&lastWakeTime, CONTROLLER_INTERVAL);
  }
}
