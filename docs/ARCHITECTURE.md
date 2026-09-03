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
- **Generalize 3 pumps → 8** (6 wired: MOTOR0..5), and replace the hardcoded two-phase `switch`
  with a **program the Pi uploads** — a variable list of phases, each a duration + a speed per
  motor. The firmware runs `g_program[]`; the GUI owns and edits it.

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

## The sequence

A **variable list of phases** on one shared timeline (`SystemState.program[]`, up to
`MAX_PHASES = 32`). Each `ProgramPhase` is a duration plus a signed steps/s target for **every
motor** (`speed[i] == 0` ⇒ that motor is stopped for the phase). `controller_task` runs the
current phase for `program[phase].seconds`, advances, and after the last phase → idle.

The **Pi GUI is the source of truth.** It builds the program (add/remove phases, add/remove
motors per phase, set each speed + duration) and uploads the whole thing with `PROGCLEAR` /
`PROGADD` / `PROGCOMMIT` on every edit and once on connect. The firmware just runs what it's
given — there is no built-in phase table.

`src/core/Protocol.h` keeps only a **seed program** (the original fixed protocol: Phase 1
Sample+Dye+Sheath 60 s, 2 Sheath+Wash 30 s, 3 Sheath+Antibody 60 s, 4 Sheath+Wash2 30 s, all at
1000 steps/s) as a fallback for a fresh device — the Pi re-uploads the real one on connect. It
matches the GUI's default workspace and simulator seed.

Pump roles (firmware index → Octopus MOTORn):

| idx | role | idx | role |
|---|---|---|---|
| 0 | Sample | 4 | Antibody |
| 1 | Dye | 5 | Wash 2 |
| 2 | Sheath | 6 | spare (unpopulated) |
| 3 | Wash | 7 | spare (unpopulated) |

`NUM_PUMPS = 8` (wire protocol width); MOTOR0..5 wired. Names are cosmetic (GUI-side).

**Stopping.** `STOP` halts the sequence (all speeds → 0, phase → idle); drivers keep their
per-pump enable state; recoverable with `RUN`. There is **no E-STOP** — the rig has no physical
button and a software "emergency" button that can't cut power adds nothing. The per-pump `ENABLE`
toggle drops holding torque on one driver so its shaft can be hand-turned (loading a syringe).

## Pi ↔ Octopus serial protocol

Line-based ASCII over USB CDC, `\n`-terminated. Grammar mirrors `gui/peachpulp/protocol.py` —
keep the two in sync (a host-side conformance check lives with the GUI tests).

**Pi → Octopus**

| command | meaning |
|---|---|
| `PING` | → `PONG` |
| `RUN` / `STOP` / `SKIP` | sequence control |
| `SPEED <idx> <steps>` | live/jog speed for a pump — used by `JOG`, reported as `sp` while idle; **not** the phase speed |
| `JOG <idx> <steps>` | run pump `idx` at `steps` while idle; `0` stops it |
| `ENABLE <idx> <0\|1>` | holding torque on/off for one pump |
| `PHASETIME <phase> <s>` | edit one committed phase's duration |
| `PROGCLEAR` | begin staging a new program |
| `PROGADD <sec> <s0> <s1> … <s7>` | append one phase: duration + 8 motor speeds |
| `PROGCOMMIT` | swap the staged program in atomically (rejects an empty program) |
| `STATE` | no-op; telemetry streams continuously |

**Octopus → Pi:** JSON telemetry ~5 Hz —
`{"phase":1,"nphases":5,"remaining":42,"pumps":[{"sp":..,"run":..,"en":..}, …]}`
(`sp` = the running phase's speed while a sequence runs, else the live/jog speed);
events `!EVENT phase <n>` / `!EVENT done` / `!EVENT prog <n>` / `!ERR <msg>`; logs `# ...`.

Implemented in `src/core/SerialLink.*` (replaces `NetworkManager`). Parsed commands post a
`ProtoCommand` to `protoCmdQueue`, drained by `controller_task`. The program staging buffer lives
in `controller.cpp` and is only touched from that task, so `controller_task` stays the sole
mutator of `systemState`.

## What carries over unchanged

- `src/tasks/ActiveMotionNode.h` — the Active-Object template (already vanilla FreeRTOS)
- The `controller_task` FSM shape, STOP/SKIP event-group model, debounced flash persistence
- `MotorDriver` VACTUAL config: microsteps 16, StealthChop on, CoolStep off, run/hold current
  (currents re-tuned for the real pump motors)
- `MotorNode::hwUpdate` send-on-change (now limits SW-serial writes instead of shared-bus traffic)

## What's ESP32-specific and gets replaced

| ESP32 | STM32 |
|---|---|
| `esp_log.h` / `ESP_LOGx` | trimmed `PEACH_LOG*` → `Serial` `#` lines (`src/core/Log.h`) |
| `Preferences` / NVS (`StorageManager`) | STM32 emulated-EEPROM flash page (`<EEPROM.h>` `eeprom_buffer_*`); blob v3 = live speeds + the whole program (~680 B) |
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
- Confirmed compiling; ~55 KB flash / ~16 KB static RAM. **Not yet run on hardware.**
- The GUI (`gui/` on branch `gui/pi-pyside6`) + its `FakeFirmware` simulator implement the same
  protocol; a host-side conformance check confirms the two match.

## Open items (hardware bring-up)

1. Per-driver `SoftwareSerial` VACTUAL timing under FreeRTOS — validated early; fallbacks are the
   `TMCStepper` library or hardware-UART half-duplex buses.
2. STM32duino + STM32FreeRTOS SysTick coexistence — 2-task smoke test first.
3. Exact Octopus pin map (driver EN pins, log UART) from
   `generic-bigtreetech-octopus-v1.1.cfg` + the BTT pinout PDF.
4. Pump motor spec → TMC `run_current` (≈ 1.2 A RMS practical on the Octopus).
5. **`eeprom_buffer_flush()` blocks ~1 s on an F4 sector erase** — persistence is debounced (3 s)
   and idle-only, but the stall still starves the FreeRTOS tick while it runs. If it's a problem,
   move to a HAL async flash write or drop program persistence (the Pi re-uploads on connect).
6. Bulk `PROG*` upload: a full 32-phase program is ~35 lines back-to-back. The serial task drains
   RX every 5 ms; if the USB-CDC RX ring still overflows, pace the writes GUI-side.
