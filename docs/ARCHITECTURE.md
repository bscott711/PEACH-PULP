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
  │       └── VNC / GUI on the LAN              └ E-STOP button  │
  └─────────────────────────────────────────────────────────────┘
```

The Octopus has no WiFi. "Connect over WiFi like now" still holds — it terminates at the Pi
(Raspberry Pi Connect / VNC / the GUI on the LAN). Pi ↔ Octopus is a short wired USB-C cable.
Firmware updates: SD-card `firmware.bin` or `dfu-util` from the Pi — not OTA.

## The protocol

A 4-phase sequence on one shared timeline. Each phase runs a fixed set of pumps at their
configured speeds for a configurable duration, then advances; after the last phase → idle.

| Phase | Duration | Pumps running |
|---|---|---|
| 1 | T1 | Sample + Dye + Sheath |
| 2 | T2 | Sheath + Wash |
| 3 | T3 | Sheath + Antibody |
| 4 | T4 | Sheath + Wash 2 |

Sheath runs through **all four** phases. In firmware this is a `kProtocol[NUM_PHASES]` table of
active-pump bitmasks (`src/controller.cpp`), replacing the old hardcoded `PROTO_PHASE1/2` switch.
E-STOP and SKIP (advance to next phase now) work as before, via the `controlEvents` event group.

Pump roles (firmware index → Octopus MOTORn):

| idx | role | idx | role |
|---|---|---|---|
| 0 | Sample | 4 | Antibody |
| 1 | Dye | 5 | Wash 2 |
| 2 | Sheath | 6 | spare |
| 3 | Wash | 7 | spare |

`NUM_PUMPS = 8` (Octopus capacity); 6 roles used now.

## Pi ↔ Octopus serial protocol

Line-based ASCII over USB CDC, `\n`-terminated.

**Pi → Octopus:** `RUN`, `STOP`, `ESTOP`, `SKIP`, `SPEED <idx> <steps>`, `PHASETIME <phase> <s>`,
`ENABLE <idx> <0|1>`, `JOG <idx> <steps>`, `STATE`, `PING`.

**Octopus → Pi:** JSON telemetry ~5 Hz —
`{"phase":1,"remaining":42,"pumps":[{"sp":..,"run":..,"en":..}],"estop":false}`;
events `!EVENT phase 2` / `!EVENT done` / `!ERR <msg>`; logs `# ...`.

Implemented in `src/core/SerialLink.*`, which replaces `src/core/NetworkManager.*`. Parsed
commands post a `ProtoCommand` to `protoCmdQueue`, drained by `controller_task` (where
`InputManager::process()` used to be).

## What carries over unchanged

- `src/tasks/ActiveMotionNode.h` — the Active-Object template (already vanilla FreeRTOS)
- The protocol FSM shape, E-STOP/SKIP event-group model, 2 s debounced persistence
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
3. Exact Octopus pin map (driver EN pins, E-STOP GPIO, log UART) from
   `generic-bigtreetech-octopus-v1.1.cfg` + the BTT pinout PDF.
4. Pump motor spec → TMC `run_current` (≈ 1.2 A RMS practical on the Octopus).
5. Default T1–T4.
