# PEACH PULP — Pi GUI

Operator GUI for the **Raspberry Pi 5 touchscreen**. Talks to the Octopus firmware
over USB serial (protocol in [../docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md)).
PySide6 (Qt). Branch: `gui/pi-pyside6`.

## Install on the Pi (one line)

```bash
curl -fsSL https://raw.githubusercontent.com/bscott711/PEACH-PULP/gui/pi-pyside6/gui/deploy/pi-bootstrap.sh | bash
```

Clones the repo to `~/PEACH-PULP`, builds `gui/.venv`, installs PySide6 + pyserial,
runs the tests, and checks Qt imports. Then `cd ~/PEACH-PULP/gui && .venv/bin/python run.py --sim`.

### Launcher + autostart on the touchscreen

```bash
~/PEACH-PULP/gui/deploy/install-launcher.sh --autostart
```

Adds a **PEACH PULP** app-menu entry and desktop icon (double-tap to open), and starts
the GUI fullscreen on the touchscreen at every boot. Without `--autostart` you just get
the icons; `--off` removes everything. No sudo. `deploy/launch.sh` starts it by hand.

### Octopus firmware updates — jumper-free

The firmware (`breaking/octopus-stm32-fw`) has a `DFU` serial command that reboots it into
the STM32 ROM bootloader — no BOOT0 jumper, no reset button. From the Pi:

```bash
sudo apt install -y dfu-util   # once
~/PEACH-PULP/gui/deploy/flash-firmware.sh --latest
```

Downloads the newest `firmware.bin` published on a GitHub release of the repo and flashes it
over the same USB cable. Pass a local path instead of `--latest` to flash a specific build. If
the board is bricked (predates the `DFU` command, or won't boot): BOOT0 jumper (`J75`) + hold
`RST`, then run the same command with no serial port connected.

## Pull the latest onto the Pi

Run this whenever you want the newest code on the Pi — it force-syncs the checkout
to `origin`, refreshes the venv, re-runs the tests, and restarts the service if it's
installed:

```bash
curl -fsSL https://raw.githubusercontent.com/bscott711/PEACH-PULP/gui/pi-pyside6/gui/deploy/pi-update.sh | bash
```

or, once the repo is on the Pi: `~/PEACH-PULP/gui/deploy/pi-update.sh`
(handy as an alias: `alias peach-update='~/PEACH-PULP/gui/deploy/pi-update.sh'`).

## Run (dev, any machine)

```bash
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt

.venv/bin/python run.py --sim          # in-process firmware simulator, no hardware
.venv/bin/python run.py                # real Octopus, autodetect the port
.venv/bin/python run.py --port /dev/ttyACM0 --fullscreen
.venv/bin/python run.py --sim --light  # light theme (default is dark)
```

The **simulator** (`--sim`) runs `peachpulp.sim.FakeFirmware`, which speaks the exact
same command + telemetry protocol as the firmware, so the whole GUI is developable
and demoable without an Octopus.

## Layout

| Path | What |
|---|---|
| `peachpulp/protocol.py` | command builders + telemetry/event parser — pure, no Qt/serial |
| `peachpulp/workspace.py` | `Workspace` / `PhaseStep` model + JSON save/load — pure |
| `peachpulp/sim.py` | `FakeFirmware` — in-process protocol-faithful fake |
| `peachpulp/link.py` | `SerialLink` (pyserial + QThread, auto-reconnect) and `SimLink` |
| `peachpulp/widgets.py` | `SpeedControl` (bar + box), `PumpRow`, `RunStatusBar` |
| `peachpulp/live.py` | `LivePanel` — the Live tab |
| `peachpulp/automated.py` | phase-program editor + workspace bar — the Automated tab |
| `peachpulp/theme.py` | dark/light theme — Fusion + palette + QSS, no third-party dep |
| `peachpulp/app.py` | `MainWindow` — tabs + RUN/SKIP/STOP footer + workspace lifecycle |
| `run.py` | entry point / CLI |
| `tests/` | pytest — pure modules only (no Qt); `test_wire_contract.py` guards the firmware protocol |
| `deploy/` | `pi-bootstrap.sh`, `pi-update.sh`, `install-launcher.sh` + `launch.sh` (icon/autostart), `flash-firmware.sh` (jumper-free Octopus flashing), `octo.py` (serial console), `install.sh`, `peach-pulp-gui.service` |

## Screen

Dark theme by default (`--light` for light), tuned for a fingertip on the Pi
touchscreen. In fullscreen: **Esc** leaves fullscreen, **F11** toggles it, **Ctrl+Q** quits.

**Live tab** (home) — direct manual control, one row per pump:

- a draggable **speed bar** + a numeric box (steps/s, negative = reverse)
- **Hold / Free** toggle — switch to *Free* to drop holding torque and hand-turn the
  syringe during setup; back to *Hold* to lock it
- **Jog** — run this pump at the set speed while idle
- run indicator dot
- While an automated sequence runs, the rows become a read-only mirror of telemetry.

**Automated tab** — build the sequence:

- **Workspace bar** — pick / Load / Save / Save As / New. The last-used workspace loads
  on startup; unsaved edits show a `*`.
- A card per **phase**: a label, a duration, and a list of **motor : speed** rows
  (add/remove motors, add/remove phases freely). The active phase highlights while running.
- Edits upload to the firmware automatically (debounced) and on connect.

**Footer** (both tabs) — **RUN** (start the sequence) · **SKIP** (advance a phase now) ·
**STOP** (halt; recoverable with RUN). Plus a log pane.

Workspaces are JSON under `~/.local/share/peachpulp/` (override the root with
`$PEACHPULP_HOME`).

## Status

Protocol + workspace + simulator unit-tested (`pytest`), including a wire-contract
test against the firmware grammar. The matching firmware is on
`breaking/octopus-stm32-fw` (commit `bbab060`) — builds, not yet run on hardware.

## TODO

- Reconnect/health UI polish; port picker in the GUI
- Confirm-dialog on RUN
- Volume calibration (steps ↔ mL) per pump, once pumps are characterised
- Kiosk niceties (hide cursor, screen-blank inhibit)
- Rename pumps from the GUI (model already supports per-workspace names)
