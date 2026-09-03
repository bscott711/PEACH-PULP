# PEACH PULP — Architecture (BigTreeTech Octopus / STM32 / custom FreeRTOS)

Status: **active migration**, branch `breaking/octopus-stm32-fw`.

## Why this change

PEACH PULP was ESP32 + FreeRTOS firmware: TMC2209 pumps in open-loop **VACTUAL velocity mode**
over a shared UART bus, an Active-Object task per pump (`src/tasks/MotorNode.*`), a two-phase
timed protocol (`src/controller.cpp`), and an SSD1309 OLED + 4 rotary encoders for local control.
WiFi did nothing at runtime — only OTA upload and a debug log stream.

The instrument is growing to **8 pumps** and needs a GUI. The firmware architecture and the pump
protocol are working code we want to keep, so instead of adopting a motion stack like Klipper we:

- **Port the firmware ESP32 → STM32** onto a **BigTreeTech Octopus v1.1 (STM32F446ZET6)**.
  FreeRTOS, the Active-Object `MotorNode`, VACTUAL-over-UART, and the protocol state machine all
  stay — see [the migration plan](~/.claude/plans/in-peach-pulp-i-tingly-globe.md).
- **Remove the on-device UI.** 4 encoders can't address 8 pumps and 8 rows don't fit a 128×64
  OLED. The Octopus runs **headless**.
- A **Raspberry Pi 5 + touchscreen** hosts a custom GUI and talks to the Octopus over **USB
  serial** (USB CDC).
- **Generalize 3 pumps → 8**, and replace the hardcoded two-phase `switch` with a **phase table**.

## Topology

```
  ┌───────────────────────── enclosure ─────────────────────────┐
  │                                                             │
  │  Raspberry Pi 5  ──── USB-C (USB CDC) ────  Octopus v1.1     │
  │  ├ custom GUI (PySide6, fullscreen touch)   STM32F446ZET6    │
  │  ├ pyserial ↔ /dev/ttyACM0                  ├ FreeRTOS fw    │
  │  └ systemd service, auto-reconnect          ├ 8× TMC2209     │
  │       │                                     │   (VACTUAL,    │
  │       │ WiFi (Pi onboard)                   │   per-driver   │
  │       ├── Raspberry Pi Connect              │   SW-serial)   │
  │       └── VNC / GUI on the LAN              └─ headless      │
  └─────────────────────────────────────────────────────────────┘
```

The Octopus has no WiFi. "Connect over WiFi like now" still holds — it terminates at the Pi
(Raspberry Pi Connect / VNC / the GUI on the LAN). Pi ↔ Octopus is a short wired USB-C cable.
Firmware updates: SD-card `firmware.bin` or `dfu-util` from the Pi — not OTA.

## The protocol

A sequence of **N phases** on one shared timeline. Each phase has a duration and a
**speed per motor** (0 = that motor is stopped for the phase). It runs the phase for its
duration, advances, and after the last phase → idle.

The **Pi GUI is the source of truth** for the program. The operator builds it on the
Automated tab (add/remove phases, add/remove motors per phase, set speed + time), and the
GUI uploads the whole thing whenever it changes. The firmware just runs what it's given —
there is no hardcoded phase table any more.

The seed "Default" workspace reproduces the original fixed protocol:

| Phase | Duration | Motor : speed |
|---|---|---|
| 1 | 60 s | Sample, Dye, Sheath |
| 2 | 30 s | Sheath, Wash |
| 3 | 60 s | Sheath, Antibody |
| 4 | 30 s | Sheath, Wash 2 |

Pump roles (firmware index → Octopus MOTORn):

| idx | role | idx | role |
|---|---|---|---|
| 0 | Sample | 4 | Antibody |
| 1 | Dye | 5 | Wash 2 |
| 2 | Sheath | 6 | spare |
| 3 | Wash | 7 | spare |

`NUM_PUMPS = 8` (Octopus capacity); 6 roles wired now. Names are cosmetic (GUI-side).

**Stopping**

- `STOP` — halt the sequence, all pump speeds → 0. Drivers stay energised (holding torque
  per each pump's *Hold* flag). Recoverable: `RUN` starts again.
- There is **no E-STOP** (the rig has no physical E-STOP button, and a software
  "emergency" button that isn't wired to power adds no safety). `STOP` is the halt.
- Per-motor **Hold** toggle (`ENABLE <idx> <0|1>`) drops/raises holding torque on one
  driver so its shaft can be hand-turned — used to load syringes at setup.

## Pi ↔ Octopus serial protocol

Line-based ASCII over USB CDC, `\n`-terminated.

**Pi → Octopus**

| command | meaning |
|---|---|
| `PING` | → `PONG` |
| `RUN` / `STOP` / `SKIP` | sequence control (see "Stopping" above) |
| `SPEED <idx> <steps>` | live/manual speed for a pump (used by `JOG`; not the phase speed) |
| `JOG <idx> <steps>` | run pump `idx` at `steps` while idle; `0` stops it |
| `ENABLE <idx> <0\|1>` | holding torque on/off for a pump |
| `PROGCLEAR` | begin staging a new program |
| `PROGADD <sec> <s0> <s1> … <s7>` | append one phase: duration + 8 motor speeds |
| `PROGCOMMIT` | swap the staged program in atomically (rejects an empty program) |
| `PHASETIME <phase> <s>` | quick edit of one committed phase's duration |
| `STATE` | (no-op; telemetry streams continuously) |

`MAX_PHASES = 32`. The GUI re-uploads the full `PROGCLEAR … PROGCOMMIT` block (debounced)
on every edit and once on connect.

**Octopus → Pi:** JSON telemetry ~5 Hz —
`{"phase":1,"nphases":5,"remaining":42,"pumps":[{"sp":..,"run":..,"en":..}]}`;
events `!EVENT phase 2` / `!EVENT done` / `!EVENT prog <n>` / `!ERR <msg>`; logs `# ...`.

Implemented in `src/core/SerialLink.*`, which replaces `src/core/NetworkManager.*`. Parsed
commands post a `ProtoCommand` to `protoCmdQueue`, drained by `controller_task` (where
`InputManager::process()` used to be).

### Firmware work for this (not yet done — bench task)

The GUI + simulator implement the above now. The firmware on this branch still has the
fixed `kProtocol[4]`; bring-up needs:

1. `struct Phase { uint32_t seconds; int16_t speed[NUM_PUMPS]; }` and
   `Phase g_program[MAX_PHASES]; uint8_t g_nphases;` replacing `kProtocol[]` +
   `SystemState.phaseSeconds[]`.
2. `SerialLink`: a `Phase g_staging[MAX_PHASES]` buffer; `PROGADD` appends, `PROGCOMMIT`
   copies staging→program under `systemStateMutex` (reject if 0 phases), resets `currentPhase`
   if it's now out of range. `PROGCLEAR` zeroes the staging count.
3. `controller_task`: `targetSteps[i] = g_program[phase].speed[i]` directly (drop the
   bitmask lookup + the global `pumpSpeedSteps[]` for sequenced motion; keep `pumpSpeedSteps`
   for `JOG`).
4. Drop the hardware-E-STOP path: remove `ESTOP_BTN_PIN`, `attachInterrupt(estopISR)`,
   `g_estopFromISR`, `BIT_ESTOP_REQUEST`, and the `ESTOP` command. `STOP` is the halt
   (speeds → 0, phase → idle; drivers keep their per-pump enable state).
5. EEPROM blob v2: `{ uint16_t magic; uint8_t nphases; Phase program[MAX_PHASES]; int32_t liveSpeed[8]; }`
   — bump `MAGIC`, keep the debounced-write path. ~32×20 B ≈ 640 B; fine for emulated EEPROM.
6. Telemetry: add `"nphases"`, emit `!EVENT prog <n>` on commit.

## What carries over unchanged

- `src/tasks/ActiveMotionNode.h` — the Active-Object template (already vanilla FreeRTOS)
- The protocol FSM shape, SKIP event-group model, 2 s debounced persistence
- `MotorDriver` VACTUAL config: microsteps 16, StealthChop on, CoolStep off, run/hold current
  (currents re-tuned for the real pump motors)
- `MotorNode::hwUpdate` send-on-change (now limits SW-serial writes instead of shared-bus traffic)

## What's ESP32-specific and gets replaced

| ESP32 | STM32 |
|---|---|
| `esp_log.h` / `ESP_LOGx` | trimmed `PEACH_LOG*` → `Serial` `#` lines (`src/core/Log.h`) |
| `Preferences` / NVS (`StorageManager`) | STM32 emulated-EEPROM flash page (`<EEPROM.h>` `eeprom_buffer_*`), same `save*/load*` API |
| `NetworkManager` (WiFi/mDNS/OTA/6666 bridge) | `SerialLink` (USB CDC command + telemetry) |
| `Serial1.begin(baud, cfg, rx, tx)` 4-arg, shared TMC bus | per-driver one-wire `SoftwareSerial` (write-only) |
| `SPI.begin(sck,-1,mosi,-1)`, `IRAM_ATTR`, seesaw, U8g2 | deleted with the on-device UI |
| `xTaskCreate` stack in **bytes**; scheduler already running in `setup()` | vanilla FreeRTOS stack in **words**; `setup()` ends with `vTaskStartScheduler()`, `loop()` unused |
| `freertos/xxx.h` includes | `src/rtos.h` → `<STM32FreeRTOS.h>` |

## Building

```
pio run -e octopus_f446          # → .pio/build/octopus_f446/firmware.bin
```

- Board def: `boards/octopus_f446.json` (custom — ststm32 has no Octopus entry). It pins the
  F446ZET6 + the generic 144-pin variant via `build.arduino.board = GENERIC_F446ZETX`.
- `platformio.ini` sets `board_build.offset = 0x8000` for the 32 KiB DFU bootloader, and
  `-DHSE_VALUE=12000000L` + USB-CDC flags.
- Libs: `janelia-arduino/TMC2209`, `stm32duino/STM32duino FreeRTOS`, plus the core's bundled
  `SoftwareSerial` and `EEPROM`.
- Confirmed compiling; ~56 KB flash / ~15 KB static RAM. Not yet run on hardware.

## Open items (hardware bring-up)

1. Per-driver `SoftwareSerial` VACTUAL timing under FreeRTOS — validated early; fallbacks are the
   `TMCStepper` library or hardware-UART half-duplex buses.
2. STM32duino + STM32FreeRTOS SysTick coexistence — 2-task smoke test first.
3. Exact Octopus pin map (driver EN pins, log UART) from
   `generic-bigtreetech-octopus-v1.1.cfg` + the BTT pinout PDF.
4. Pump motor spec → TMC `run_current` (≈ 1.2 A RMS practical on the Octopus).
5. The variable-phase-program firmware changes (see "Firmware work for this" above).
