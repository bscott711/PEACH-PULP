#pragma once
#include "rtos.h"

// USB-CDC command + telemetry link to the Raspberry Pi. Replaces NetworkManager.
//
// In  (Pi → Octopus), line-based ASCII, \n-terminated:
//   PING | RUN | STOP | ESTOP | SKIP
//   SPEED <idx> <steps>   PHASETIME <phase> <sec>   ENABLE <idx> <0|1>
//   JOG <idx> <steps>     STATE
// Out (Octopus → Pi):
//   PONG
//   {"phase":..,"remaining":..,"estop":..,"pumps":[{"sp":..,"run":..,"en":..},...]}
//   !EVENT phase <n> | !EVENT done | !ERR <msg>
//   # <log line>
extern SemaphoreHandle_t g_serialMutex;

void serialLinkTask(void *pvParameters);
