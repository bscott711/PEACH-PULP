#!/usr/bin/env python3
"""PEACH PULP operator GUI — entry point.

    python run.py --sim                 # no hardware, in-process firmware sim
    python run.py --port /dev/ttyACM0   # real Octopus over USB
    python run.py                       # real, autodetect the port
    python run.py --fullscreen          # kiosk mode (the Pi touchscreen)
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
    ap.add_argument("--spares", action="store_true", help="also show spare pump slots 6-7")
    args = ap.parse_args()

    from PySide6.QtWidgets import QApplication

    from peachpulp.app import MainWindow
    from peachpulp.link import SerialLink, SimLink

    app = QApplication(sys.argv)
    link = SimLink() if args.sim else SerialLink(port=args.port, baud=args.baud)
    win = MainWindow(link, show_spares=args.spares)

    if args.fullscreen:
        win.showFullScreen()
    else:
        win.resize(900, 680)
        win.show()

    link.start()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
