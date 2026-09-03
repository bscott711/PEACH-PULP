# PEACH PULP

Multi-pump liquid-handling instrument for the PEACH bench system.

## This branch (`breaking/octopus-stm32-fw`)

Porting the ESP32 firmware onto a **BigTreeTech Octopus v1.1 (STM32F446ZET6)** and scaling from
3 pumps to 8, keeping the existing FreeRTOS / Active-Object / VACTUAL architecture.

- **Octopus** — 8× TMC2209 in VACTUAL velocity mode, per-driver software UART. Headless.
- **Raspberry Pi 5 + touchscreen** — custom GUI (PySide6), talks to the Octopus over **USB serial**.
- **No Klipper, no gcode.** The two-phase protocol (now a 4-phase table) stays as firmware.

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
Copy `firmware.bin` to the Octopus microSD and power-cycle, or `dfu-util` from the Pi. See
[pi/README.md](pi/README.md).

## Status

Migration in progress. Phase 0 (branch/docs) and Phase 1 (STM32 build target) underway.

## The old ESP32 firmware

Preserved on `main` and `breaking/stem-hw-3pump`: ESP32 + FreeRTOS, 3 pumps on TMC2209 (shared
UART), SSD1309 OLED + 4 rotary encoders, two-phase timed protocol, WiFi for OTA + a TCP-6666
debug log stream.
