# PEACH PULP — Pi GUI

The operator GUI lives on branch **`gui/pi-pyside6`** (PySide6, runs on the Raspberry Pi 5
touchscreen, talks to the Octopus over USB serial). It is developed in parallel with this
firmware branch against an in-process `FakeFirmware` simulator.

This branch only carries the **firmware** side of the protocol
(`src/core/SerialLink.*`, `src/controller.cpp`). The two are kept in lock-step — see
[../docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md) "Pi ↔ Octopus serial protocol".

```bash
git switch gui/pi-pyside6      # the GUI, deploy scripts, tests
```
