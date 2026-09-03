# PEACH PULP — Raspberry Pi setup

Target: the existing **Raspberry Pi 5 + touchscreen** (OS installed, WiFi configured, reachable
via Raspberry Pi Connect). The Pi runs the operator GUI and connects to the Octopus over USB-C.

## 1. Flashing the Octopus

Build on any machine with PlatformIO (the dev Mac, or the Pi):

```bash
pio run -e octopus_f446
# -> .pio/build/octopus_f446/firmware.bin
```

**SD-card method (reliable, first flash + updates):**
1. Copy `firmware.bin` onto a FAT32 microSD.
2. Insert into the Octopus, power-cycle. The bootloader flashes it and renames to `FIRMWARE.CUR`.
3. The green LED settles; the board enumerates on the Pi as `/dev/ttyACM0`.

**DFU method (from the Pi, no card):**
```bash
sudo apt install dfu-util
# put the Octopus in DFU: BOOT0 jumper on, tap reset, then:
dfu-util -a 0 -s 0x08008000:leave -D firmware.bin
```

Confirm the link:
```bash
ls /dev/serial/by-id/*          # stable path for the GUI
screen /dev/ttyACM0 115200      # type: PING  -> expect: PONG   (Ctrl-A K to quit)
```

## 2. GUI (Phase 8)

The GUI lives in `../gui/` (PySide6). Once it exists:

```bash
cd gui
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
.venv/bin/python peach_pulp_gui.py --port /dev/serial/by-id/<octopus>
```

Install as a systemd service (auto-start on the touchscreen, auto-reconnect on USB drop) —
see `gui/README.md`.

## 3. Remote access

Unchanged — all via the Pi's own WiFi:
- **Raspberry Pi Connect** (`connect.raspberrypi.com`) — shell + screen share
- **VNC** or the GUI on the LAN

The Octopus is only ever reached *through* the Pi (USB-C cable).
