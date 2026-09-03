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
| `peachpulp/sim.py` | `FakeFirmware` — in-process protocol-faithful fake |
| `peachpulp/link.py` | `SerialLink` (pyserial + QThread, auto-reconnect) and `SimLink` |
| `peachpulp/widgets.py` | `PumpRow`, `PhasePanel` |
| `peachpulp/theme.py` | dark/light theme — Fusion + palette + QSS, no third-party dep |
| `peachpulp/app.py` | `MainWindow` |
| `run.py` | entry point / CLI |
| `tests/` | pytest — `protocol.py` + `sim.py` (no Qt needed): `cd gui && python -m pytest` |
| `deploy/` | `pi-bootstrap.sh` (one-line Pi setup), `install.sh`, `peach-pulp-gui.service` |

## Screen

Dark theme by default (`--light` for light), tuned for a fingertip on the Pi
touchscreen. Scales from 800×480 up; the pump list scrolls if it doesn't fit.

- **Pumps** — one row per wired role (Sample, Dye, Sheath, Wash, Antibody, Wash2):
  speed (steps/s), run indicator, hold-torque toggle, jog.
- **Phase durations** — T1–T4 editable; live progress bar + remaining time for the
  running phase.
- **RUN / SKIP / STOP / E-STOP** buttons.
- Log pane (firmware `#` lines, `!EVENT`, `!ERR`).

## Status

First cut. Protocol + simulator unit-tested (`pytest`, 14 tests). Not yet run against
real firmware (pending Octopus bring-up on the `breaking/octopus-stm32-fw` branch).

## TODO

- Reconnect/health UI polish; port picker in the GUI
- Confirm-dialog on RUN; disable edits sensibly while running
- Volume calibration (steps ↔ mL) per pump, once pumps are characterised
- Kiosk niceties (hide cursor, screen-blank inhibit)
