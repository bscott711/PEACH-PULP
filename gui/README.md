# PEACH PULP — Pi GUI (Phase 8)

Placeholder. The operator GUI runs on the **Raspberry Pi 5 touchscreen** and talks to the
Octopus over **USB serial** (the protocol in [../docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md)).

## Plan

- **PySide6** (Qt), fullscreen, touch-first. `pyserial` to `/dev/serial/by-id/<octopus>`.
- Screens:
  - per-pump speed control with role labels (Sample / Dye / Sheath / Wash / Antibody / Wash 2)
  - phase durations T1–T4
  - big **RUN** / **SKIP** / **E-STOP**
  - phase progress bar + live telemetry (`!STATE` lines), connection indicator
- systemd service: auto-start on boot, auto-reconnect when the USB cable is replugged.

## Protocol (quick ref)

Send: `RUN` `STOP` `ESTOP` `SKIP` `SPEED <idx> <steps>` `PHASETIME <phase> <s>`
`ENABLE <idx> <0|1>` `JOG <idx> <steps>` `STATE` `PING`

Receive: JSON telemetry lines, `!EVENT ...`, `!ERR ...`, `# ...` logs.
