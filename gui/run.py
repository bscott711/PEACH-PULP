#!/usr/bin/env python3
"""PEACH PULP operator GUI — entry point.

    python run.py --sim                 # no hardware, in-process firmware sim
    python run.py --port /dev/ttyACM0   # real Octopus over USB
    python run.py                       # real, autodetect the port
    python run.py --fullscreen          # kiosk on the Pi touchscreen
    python run.py --light               # light theme (default is dark)

In fullscreen: Esc leaves fullscreen, F11 toggles it, Ctrl+Q quits.
"""
from __future__ import annotations

import argparse
import sys


def main() -> int:
    ap = argparse.ArgumentParser(description="PEACH PULP operator GUI")
    ap.add_argument("--sim", action="store_true", help="run against the in-process simulator")
    ap.add_argument("--port", help="serial port (default: autodetect)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--fullscreen", action="store_true")
    ap.add_argument("--kiosk", action="store_true", help="fullscreen with no Exit button")
    ap.add_argument("--light", action="store_true", help="light theme (default: dark)")
    ap.add_argument("--spares", action="store_true", help="also show spare pump slots 6-7")
    ap.add_argument("--workspace", help="workspace to open (default: last used)")
    args = ap.parse_args()

    from PySide6.QtWidgets import QApplication

    from peachpulp.app import MainWindow
    from peachpulp.link import SerialLink, SimLink
    from peachpulp.theme import apply_theme

    app = QApplication(sys.argv)
    app.setApplicationName("PEACH PULP")
    apply_theme(app, dark=not args.light)

    link = SimLink() if args.sim else SerialLink(port=args.port, baud=args.baud)
    win = MainWindow(
        link,
        show_spares=args.spares,
        workspace_name=args.workspace,
        kiosk=args.kiosk,
    )

    if args.fullscreen or args.kiosk:
        win.showFullScreen()
    else:
        win.resize(1024, 640)
        win.show()

    link.start()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
