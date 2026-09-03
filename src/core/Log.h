#pragma once
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include "rtos.h"

// Lightweight logging → USB CDC (`Serial`), one line per call, prefixed with
// "# " so the Pi GUI can separate logs from the JSON telemetry / `!EVENT`
// lines on the same stream. Replaces the ESP32 NetworkManager PEACH_LOG /
// esp_log path.
//
// g_serialMutex is created in main.cpp before the tasks start and shared with
// SerialLink so log lines and telemetry never interleave. It may be null very
// early in boot; the lock is simply skipped then.
extern SemaphoreHandle_t g_serialMutex;

inline void peachLog(const char *level, const char *tag, const char *fmt, ...) {
  char buf[192];
  int n = snprintf(buf, sizeof(buf), "# %s %s: ", level, tag);
  if (n < 0 || n >= (int)sizeof(buf)) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
  va_end(ap);

  bool locked = (g_serialMutex != nullptr &&
                 xSemaphoreTake(g_serialMutex, pdMS_TO_TICKS(20)) == pdTRUE);
  Serial.println(buf);
  if (locked) xSemaphoreGive(g_serialMutex);
}

#define PEACH_LOGI(tag, fmt, ...) peachLog("I", tag, fmt, ##__VA_ARGS__)
#define PEACH_LOGE(tag, fmt, ...) peachLog("E", tag, fmt, ##__VA_ARGS__)
#define PEACH_LOGW(tag, fmt, ...) peachLog("W", tag, fmt, ##__VA_ARGS__)
#define PEACH_LOGD(tag, fmt, ...) peachLog("D", tag, fmt, ##__VA_ARGS__)
