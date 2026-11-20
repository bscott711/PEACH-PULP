#pragma once
#include <stdint.h>

// Runtime microstepping (MICROSTEP <n> verb). Pure helpers, no Arduino/RTOS
// includes, so they can be unit-checked on the host.
//
// Speed numbers on the wire (SPEED / JOG / PROGADD) are microsteps/s at the
// selected resolution, sent to the TMC2209 VACTUAL register as is (clamped by
// motorDriver::setVelocity), never scaled by ms: the same number turns the shaft
// 16/ms times as fast as it does at 1/16 (1/32 is half speed, 1/8 is double). At
// 16 behaviour is unchanged.

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
