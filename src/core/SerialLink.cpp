#include "core/SerialLink.h"
#include "core/StateSnapshot.h"
#include "messaging.h"
#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

SemaphoreHandle_t g_serialMutex = nullptr;

void serialEmit(const char *s) {
  bool locked = (g_serialMutex != nullptr &&
                 xSemaphoreTake(g_serialMutex, pdMS_TO_TICKS(20)) == pdTRUE);
  Serial.println(s);
  if (locked) xSemaphoreGive(g_serialMutex);
}

// Returns next whitespace-delimited token as int; *ok=false if absent.
static int nextInt(bool *ok) {
  char *t = strtok(nullptr, " \t\r\n");
  if (!t) { *ok = false; return 0; }
  *ok = true;
  return atoi(t);
}

static void post(ProtoAction action, int a = 0, int b = 0) {
  ProtoCommand pc{};
  pc.action = action;
  pc.a = a;
  pc.b = b;
  if (protoCmdQueue) xQueueSend(protoCmdQueue, &pc, 0);
}

static void parseProgAdd() {
  ProtoCommand pc{};
  pc.action = ProtoAction::PROG_ADD;
  bool ok;
  pc.a = nextInt(&ok); // seconds
  if (!ok) {
    serialEmit("!ERR PROGADD <sec> <s0..s7>");
    return;
  }
  for (int i = 0; i < NUM_PUMPS; i++) {
    bool got;
    int v = nextInt(&got);
    pc.speeds[i] = got ? (int16_t)v : 0; // missing trailing speeds default to 0
  }
  if (protoCmdQueue) xQueueSend(protoCmdQueue, &pc, 0);
}

static void parseLine(char *line) {
  char *cmd = strtok(line, " \t\r\n");
  if (!cmd) return;

  if (!strcasecmp(cmd, "PING")) {
    serialEmit("PONG");
  } else if (!strcasecmp(cmd, "RUN")) {
    post(ProtoAction::RUN);
  } else if (!strcasecmp(cmd, "STOP")) {
    post(ProtoAction::STOP);
  } else if (!strcasecmp(cmd, "SKIP")) {
    post(ProtoAction::SKIP);
  } else if (!strcasecmp(cmd, "STATE")) {
    // telemetry is emitted continuously; nothing to do
  } else if (!strcasecmp(cmd, "SPEED")) {
    bool o1, o2;
    int idx = nextInt(&o1), v = nextInt(&o2);
    if (o1 && o2) post(ProtoAction::SET_SPEED, idx, v);
    else serialEmit("!ERR SPEED <idx> <steps>");
  } else if (!strcasecmp(cmd, "PHASETIME")) {
    bool o1, o2;
    int ph = nextInt(&o1), s = nextInt(&o2);
    if (o1 && o2) post(ProtoAction::SET_PHASETIME, ph, s);
    else serialEmit("!ERR PHASETIME <phase> <sec>");
  } else if (!strcasecmp(cmd, "ENABLE")) {
    bool o1, o2;
    int idx = nextInt(&o1), en = nextInt(&o2);
    if (o1 && o2) post(ProtoAction::SET_ENABLE, idx, en);
    else serialEmit("!ERR ENABLE <idx> <0|1>");
  } else if (!strcasecmp(cmd, "JOG")) {
    bool o1, o2;
    int idx = nextInt(&o1), v = nextInt(&o2);
    if (o1 && o2) post(ProtoAction::JOG, idx, v);
    else serialEmit("!ERR JOG <idx> <steps>");
  } else if (!strcasecmp(cmd, "PROGCLEAR")) {
    post(ProtoAction::PROG_CLEAR);
  } else if (!strcasecmp(cmd, "PROGADD")) {
    parseProgAdd();
  } else if (!strcasecmp(cmd, "PROGCOMMIT")) {
    post(ProtoAction::PROG_COMMIT);
  } else {
    serialEmit("!ERR unknown command");
  }
}

static void emitTelemetry(const StateSnapshot &s) {
  char j[420];
  int n = snprintf(j, sizeof(j),
                   "{\"phase\":%d,\"nphases\":%u,\"remaining\":%lu,\"pumps\":[",
                   s.currentPhase, (unsigned)s.nPhases,
                   (unsigned long)s.phaseRemainingS);
  for (int i = 0; i < NUM_PUMPS && n < (int)sizeof(j); i++) {
    n += snprintf(j + n, sizeof(j) - n, "%s{\"sp\":%d,\"run\":%d,\"en\":%d}",
                  i ? "," : "", s.pumpSpeedSteps[i], s.pumpRunning[i] ? 1 : 0,
                  s.pumpEnabled[i] ? 1 : 0);
  }
  if (n < (int)sizeof(j)) snprintf(j + n, sizeof(j) - n, "]}");
  serialEmit(j);
}

void serialLinkTask(void *pvParameters) {
  (void)pvParameters;
  char buf[96];
  size_t len = 0;
  int lastPhase = -2;
  TickType_t lastTelem = xTaskGetTickCount();

  for (;;) {
    while (Serial.available() > 0) {
      char c = (char)Serial.read();
      if (c == '\n' || c == '\r') {
        if (len > 0) {
          buf[len] = '\0';
          parseLine(buf);
          len = 0;
        }
      } else if (len < sizeof(buf) - 1) {
        buf[len++] = c;
      } else {
        len = 0; // overflow — drop the line
      }
    }

    if (xTaskGetTickCount() - lastTelem >= pdMS_TO_TICKS(200)) {
      lastTelem = xTaskGetTickCount();
      StateSnapshot s;
      if (stateQueue && xQueuePeek(stateQueue, &s, 0) == pdTRUE) {
        if (s.currentPhase != lastPhase) {
          if (s.currentPhase < 0 && lastPhase >= 0)
            serialEmit("!EVENT done");
          else if (s.currentPhase >= 0) {
            char ev[24];
            snprintf(ev, sizeof(ev), "!EVENT phase %d", s.currentPhase);
            serialEmit(ev);
          }
          lastPhase = s.currentPhase;
        }
        emitTelemetry(s);
      }
    }

    // 5 ms keeps the USB-CDC RX drained during a bulk PROG* upload
    // (~35 lines back-to-back for a full 32-phase program).
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
