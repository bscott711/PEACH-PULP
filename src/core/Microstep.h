#pragma once
#include <stdint.h>

// Runtime microstepping (MICROSTEP <n> verb). Pure helpers, no Arduino/RTOS
// includes, so they can be unit-checked on the host.
//
// Speed numbers on the wire (SPEED / JOG / PROGADD) keep one physical meaning:
// "steps/s at 1/16 microstepping". The TMC2209 VACTUAL register counts
// microsteps at whatever resolution is selected, so MotorNode scales the speed
// at send time with microstepsScaleSpeed(). At 16 that is the identity.

#define MICROSTEPS_DEFAULT 16

// Reply when n is missing / non-integer / not a power of two in 1..256.
#define MICROSTEP_USAGE_ERR "!ERR MICROSTEP 1|2|4|8|16|32|64|128|256"

// n in {1, 2, 4, 8, 16, 32, 64, 128, 256}.
inline bool microstepsValid(int n) {
  return n >= 1 && n <= 256 && (n & (n - 1)) == 0;
}

// Flash code: 0 = never stored, 1..9 = log2(n) + 1 (fits a byte; 256 would not).
inline uint8_t microstepsEncode(int n) {
  if (!microstepsValid(n)) return 0;
  uint8_t code = 1;
  while (n > 1) {
    n >>= 1;
    code++;
  }
  return code;
}

// Unknown / absent code -> the default, so an old or corrupt blob boots at 16.
inline uint16_t microstepsDecode(uint8_t code) {
  return (code >= 1 && code <= 9) ? (uint16_t)(1u << (code - 1))
                                  : (uint16_t)MICROSTEPS_DEFAULT;
}

// VACTUAL for a physical speed (steps/s at 1/16) at `ms` microsteps per step:
//   round(speed * ms / 16), half away from zero; non-zero speed -> at least +/-1;
//   clamped to +/-maxSteps; speed 0 -> 0. At ms == 16 this is clamp(speed).
// 64-bit intermediate so no input can overflow.
inline int microstepsScaleSpeed(int speed, int ms, int maxSteps) {
  if (speed == 0) return 0;
  const bool neg = speed < 0;
  const int64_t mag = neg ? -(int64_t)speed : (int64_t)speed;
  int64_t v = (mag * ms + 8) / 16;
  if (v < 1) v = 1;
  if (v > maxSteps) v = maxSteps;
  return neg ? -(int)v : (int)v;
}
