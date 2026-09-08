#pragma once
#include <Arduino.h>
#include "core/SystemState.h"

/**
 * @file HardwareConfig.h
 * @brief Pinout for PEACH PULP on a BigTreeTech Octopus v1.1 (STM32F446ZET6).
 *
 * Pin names are the STM32duino "PXn" identifiers. Driver pins match Klipper's
 * config/generic-bigtreetech-octopus-v1.1.cfg (MOTOR0..MOTOR7).
 * >>> If this is an Octopus Pro, cross-check against the -pro- config. <<<
 */

// ==========================================
// TMC2209 — VACTUAL velocity mode, per-driver one-wire software UART
// ==========================================
// 19200, not 115200: the TMC2209 auto-detects baud from the sync byte, and a
// slower bit period (52 us vs 8.7 us) gives the bit-banged SoftwareSerial ~6x
// more timing margin against FreeRTOS ISR/scheduler jitter — which is what makes
// the half-duplex *read-back* (DRIVER_VERIFY) unreliable. Writes (VACTUAL) are
// tiny and infrequent, so the lower rate costs nothing there.
#define SERIAL_BAUD_RATE 19200

// Driver current is set deterministically from the sense resistor (BTT
// TMC2209 stepstick = 0.11 Ω). Tune RUN_CURRENT_MA for the real pump motors.
#define SENSE_RESISTOR_OHMS 0.11f
#define RUN_CURRENT_MA 600           // conservative start; TMC2209 ~1.2 A RMS practical max
#define HOLD_CURRENT_MULTIPLIER 0.5f // hold = 0.5 × run (lets a syringe be hand-turned)

#define MOTOR_MAX_SAFE_STEPS 100000  // hard clamp in motorDriver::setVelocity

// Every driver's full config (StealthChop / run current / microsteps / CoolStep)
// is blindly re-pushed this often. A TMC2209 that momentarily loses VMOT — a
// loose motor-power terminal — reloads its (loud, SpreadCycle) OTP defaults;
// this heals it without the operator having to toggle Hold/Free in the GUI.
#define DRIVER_REASSERT_MS 5000

// Read-back over the one-wire link (GCONF/CHOPCONF) for diagnostics: boot log
// lines + a per-pump "ok" flag in telemetry. OFF by default — bit-banged
// half-duplex RX proved unreliable under FreeRTOS on this hardware at both
// 115200 and 19200 baud (every driver read as "not communicating" even though
// the blind config writes clearly land — motors run quiet). TX is fine, so
// motion and the 5 s self-heal below are unaffected. Set to 1 only if you move
// the TMC UART onto a hardware half-duplex peripheral.
#define DRIVER_VERIFY 0

struct MotorConfig {
  uint32_t uartPin;  // TMC2209 PDN_UART — one-wire SoftwareSerial (write-only)
  uint32_t enPin;    // TMC2209 EN (active low), via setHardwareEnablePin
  const char *name;
};

// MOTOR0..MOTOR5 are wired to the 6 pumps; MOTOR6/7 are spare (no stepstick).
//                       UART   EN     role
static const MotorConfig kPumpConfigs[NUM_PUMPS] = {
    {PC4, PF14, "Sample"},   // MOTOR0  P_SAMPLE
    {PD11, PF15, "Dye"},     // MOTOR1  P_DYE
    {PC6, PG5, "Sheath"},    // MOTOR2  P_SHEATH
    {PC7, PA0, "Wash"},      // MOTOR3  P_WASH
    {PF2, PG2, "Antibody"},  // MOTOR4  P_ANTIBODY
    {PE4, PF1, "Wash2"},     // MOTOR5  P_WASH2
    {PE1, PD4, "Spare6"},    // MOTOR6  (unpopulated)
    {PD3, PE0, "Spare7"},    // MOTOR7  (unpopulated)
};

// ==========================================
// FreeRTOS task stacks (WORDS — vanilla FreeRTOS, unlike the ESP32 byte counts)
// and priorities. Tune stacks with uxTaskGetStackHighWaterMark during bring-up.
// ==========================================
#define STACK_PUMP_NODE 640
#define STACK_CONTROLLER 768
#define STACK_SERIAL 768
#define PRIO_PUMP_NODE 2
#define PRIO_CONTROLLER 3
#define PRIO_SERIAL 3
