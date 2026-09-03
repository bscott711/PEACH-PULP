#pragma once
#include "rtos.h"

// USB-CDC command + telemetry link to the Raspberry Pi. Replaces NetworkManager.
// The grammar mirrors gui/peachpulp/protocol.py — keep the two in sync.
//
// In  (Pi → Octopus), line-based ASCII, \n-terminated:
//   PING | RUN | STOP | SKIP | STATE
//   SPEED <idx> <steps>        (live/jog speed, not the phase speed)
//   PHASETIME <phase> <sec>    ENABLE <idx> <0|1>    JOG <idx> <steps>
//   PROGCLEAR
//   PROGADD <sec> <s0> <s1> ... <s7>     (one per phase, in order)
//   PROGCOMMIT
// Out (Octopus → Pi):
//   PONG
//   {"phase":..,"nphases":..,"remaining":..,"pumps":[{"sp":..,"run":..,"en":..},...]}
//   !EVENT phase <n> | !EVENT done | !EVENT prog <n> | !ERR <msg>
//   # <log line>
extern SemaphoreHandle_t g_serialMutex;

void serialLinkTask(void *pvParameters);

// Thread-safe single-line output (takes g_serialMutex). Used by the controller
// to emit !EVENT / !ERR lines from its own task context.
void serialEmit(const char *s);
