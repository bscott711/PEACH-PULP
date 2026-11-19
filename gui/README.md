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

Adds a **PEACH PULP** app-menu entry and a desktop icon (a peach; double-tap to open), and starts
the GUI fullscreen on the touchscreen at every boot. Without `--autostart` you just get the
icon + menu entry; `--off` removes the entries and autostart. No sudo. Safe to re-run any time: it
re-installs the icon (`~/.local/share/icons/hicolor/scalable/apps/peach-pulp.svg`) and rewrites
every entry. `deploy/launch.sh` starts the GUI by hand.

The icon opens straight away, with no "Execute / Execute in Terminal / Open / Cancel" dialog: each
entry is marked trusted (`gio`, retried on a private D-Bus session from a remote shell) and
`quick_exec=1` is set in `~/.config/libfm/libfm.conf`. If the dialog still appears, log out and in
once, or tick File Manager > Edit > Preferences > General > "Don't ask options on launch executable
file". `PEACHPULP_NO_QUICK_EXEC=1` skips the libfm.conf change.

**The icon keeps itself up to date.** Every launch (icon tap or boot autostart) first runs
`deploy/update-code.sh`, which fast-forwards the checkout to the newest code pushed to
`origin/gui/pi-pyside6`, then starts the GUI (the launcher restarts itself once after a sync, so
an updated `launch.sh` is picked up on that same launch). It is best-effort and never blocks or
fails the launch:

- **Offline / slow network** — the fetch is cut off after 8 s and the code is left as is.
- **Fast-forward only** — it never resets, force-checks-out or discards anything. It is skipped
  (and logged) when you have uncommitted changes to tracked files, when the checkout is on a
  different branch, or when the Pi has local commits that are not on `origin` (diverged / ahead).
  Untracked files (`.venv/`, notes) are ignored.
- **Dependencies** — `pip install -r requirements.txt` runs only when `requirements.txt` changed
  since the last successful install (and once on the first launch after this feature arrives).
  If pip fails (offline), it is retried on the next launch.
- **Log** — every launch appends a line to `~/.local/state/peachpulp/launch.log`
  (`tail ~/.local/state/peachpulp/launch.log` to see what the last launch did; rotated at ~200 KB).
- **Opt out** — `deploy/launch.sh --no-update`, or set `PEACHPULP_NO_UPDATE=1`.
- Overrides (rarely needed): `PEACHPULP_BRANCH` (default `gui/pi-pyside6`), `PEACHPULP_REMOTE`
  (default `origin`), `PEACHPULP_STATE_DIR` (log + stamp location).

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

Normally there is nothing to do: tapping the **PEACH PULP** icon (or rebooting, if autostart is
on) pulls the newest pushed code first (see above). To force it right now, repair a checkout that
the icon skipped (dirty or diverged), re-run the self-test and restart a running GUI, use the
update script. It **force-syncs** the checkout to `origin` (discarding local edits), refreshes the
venv, re-installs the launcher icon + entries (autostart is left as it is), re-runs the tests, and
restarts the service if it's installed:

```bash
curl -fsSL https://raw.githubusercontent.com/bscott711/PEACH-PULP/gui/pi-pyside6/gui/deploy/pi-update.sh | bash
```

or, once the repo is on the Pi: `~/PEACH-PULP/gui/deploy/pi-update.sh`
(handy as an alias: `alias peach-update='~/PEACH-PULP/gui/deploy/pi-update.sh'`).

Just want the new icon + self-updating launcher on a Pi that has the old launcher, without
touching anything else:

```bash
cd ~/PEACH-PULP && git pull --ff-only && gui/deploy/install-launcher.sh
```

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
| `peachpulp/widgets.py` | `SpeedControl` (bar + box), `PumpRow`, `MicrostepSelector`, `RunStatusBar` |
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

- a **speed bar** (tap anywhere to jump, then drag) + a numeric box (steps/s);
  the fill is anchored at zero — blue to the right for forward, amber to the
  left for reverse
- all motors on one screen (two columns), no scrolling
- **Hold / Free** toggle — switch to *Free* to drop holding torque and hand-turn the
  syringe during setup; back to *Hold* to lock it
- **Jog** — run this pump at the set speed while idle
- run indicator dot
- While an automated sequence runs, the rows become a read-only mirror of telemetry.

**Microstepping** — the **Microstepping** box in the Live tab's header row (next to
*PUMPS*) sets the TMC2209 microstepping for all pumps at once: Full, 1/2 … 1/256
(default 1/16). It needs firmware that knows the `MICROSTEP` command; on older firmware the
box is disabled ("Firmware too old: reflash to enable"). It is also disabled while a program
or any pump is running ("Stop all pumps first"). A pick shows at once (dashed border) and is
confirmed when the Octopus reports it back; if it doesn't (or refuses), the box reverts, turns
red and the log says why. **Speed numbers keep their meaning** at every setting (steps/s at
1/16), so changing it never makes a saved speed run faster or slower. The Octopus stores the
setting itself; it is not part of a workspace.

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
