#pragma once
// FreeRTOS include shim.
//
// The ESP32 build used ESP-IDF's "freertos/xxx.h" include paths. On the STM32
// (BigTreeTech Octopus) we use the STM32duino FreeRTOS library, whose single
// umbrella header pulls in FreeRTOS.h / task.h / queue.h / semphr.h /
// event_groups.h with no path prefix.
#include <STM32FreeRTOS.h>
