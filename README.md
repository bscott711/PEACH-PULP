# PEACH PULP

Multi-pump liquid-handling instrument for the PEACH bench system.

## This branch (`breaking/octopus-stm32-fw`)

Porting the ESP32 firmware onto a **BigTreeTech Octopus v1.1 (STM32F446ZET6)** and scaling from
3 pumps to 8, keeping the existing FreeRTOS / Active-Object / VACTUAL architecture.

- **Octopus** — 8× TMC2209 in VACTUAL velocity mode, per-driver software UART. Headless.
  MOTOR0..5 wired (Sample, Dye, Sheath, Wash, Antibody, Wash2).
- **Raspberry Pi 5 + touchscreen** — custom GUI (PySide6), talks to the Octopus over **USB serial**.
- **No Klipper, no gcode.** The sequence is a variable list of phases (duration + speed per motor)
  built on the Pi and uploaded to the firmware, which just runs it.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and the plan at
`~/.claude/plans/in-peach-pulp-i-tingly-globe.md`.

### Layout

| Path | What |
|---|---|
| `src/` | firmware (being ported ESP32 → STM32) |
| `platformio.ini` | `env:octopus_f446` is the target; `env:esp32dev` kept temporarily for reference |
| `gui/` | custom Pi GUI (PySide6) — Phase 8 |
| `pi/` | Pi setup: GUI install, flashing the Octopus |
| `docs/` | architecture notes |

### Build & flash

```
pio run -e octopus_f446
# -> .pio/build/octopus_f446/firmware.bin
```
Flash over USB-C via DFU: fit the BOOT0 jumper (`J75`, centre of the board by the BTT logo),
hold `RST` ~4 s, then
```
dfu-util -a 0 -d 0483:df11 -s 0x08000000:leave -D .pio/build/octopus_f446/firmware.bin
```
Remove the jumper and power-cycle. (`pio run -e octopus_f446 -t upload` does the same once the
board is in DFU.) See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Status

Migration in progress. Phase 0 (branch/docs) and Phase 1 (STM32 build target) underway.

## The old ESP32 firmware

Preserved on `main` and `breaking/stem-hw-3pump`: ESP32 + FreeRTOS, 3 pumps on TMC2209 (shared
UART), SSD1309 OLED + 4 rotary encoders, two-phase timed protocol, WiFi for OTA + a TCP-6666
debug log stream.
