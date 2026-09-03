#!/usr/bin/env python3
"""Tiny serial console for the Octopus firmware — bring-up helper.

Run with PlatformIO's bundled Python (it has pyserial):

    ~/.platformio/penv/bin/python tools/octo.py                 # interactive
    ~/.platformio/penv/bin/python tools/octo.py "JOG 0 800"     # one-shot
    ~/.platformio/penv/bin/python tools/octo.py --quiet PING    # hide telemetry

Interactive mode: type a command + Enter to send (newline added for you).
Telemetry JSON is dimmed; events (!EVENT / !ERR) and PONG stand out.
Ctrl-C / Ctrl-D to quit.
"""
from __future__ import annotations

import argparse
import sys
import threading
import time

import serial
from serial.tools import list_ports


def autodetect() -> str | None:
    for p in list_ports.comports():
        if p.vid == 0x0483 or "usbmodem" in (p.device or "") or "ACM" in (p.device or ""):
            return p.device
    return None


def reader(ser: serial.Serial, quiet: bool, stop: threading.Event) -> None:
    buf = b""
    while not stop.is_set():
        try:
            buf += ser.read(256)
        except serial.SerialException:
            print("\n[port closed]")
            stop.set()
            return
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            line = raw.decode("utf-8", "replace").rstrip("\r")
            if not line:
                continue
            if quiet and line.startswith('{"phase"'):
                continue
            if line.startswith('{"phase"'):
                sys.stdout.write("\033[2m" + line + "\033[0m\n")
            elif line.startswith("!") or line == "PONG":
                sys.stdout.write("\033[1;36m" + line + "\033[0m\n")
            else:
                sys.stdout.write(line + "\n")
            sys.stdout.flush()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("command", nargs="*", help="send this command and exit")
    ap.add_argument("--port", help="serial port (default: autodetect)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--quiet", action="store_true", help="suppress telemetry lines")
    args = ap.parse_args()

    port = args.port or autodetect()
    if not port:
        print("no USB serial port found — is the Octopus plugged in?", file=sys.stderr)
        return 1

    ser = serial.Serial(port, args.baud, timeout=0.2)
    print(f"[{port} @ {args.baud}]  Ctrl-C to quit")
    stop = threading.Event()
    t = threading.Thread(target=reader, args=(ser, args.quiet, stop), daemon=True)
    t.start()

    try:
        if args.command:
            time.sleep(0.3)
            ser.write((" ".join(args.command) + "\n").encode())
            time.sleep(1.5)
        else:
            for raw in sys.stdin:
                cmd = raw.strip()
                if cmd:
                    ser.write((cmd + "\n").encode())
    except (KeyboardInterrupt, EOFError):
        pass
    finally:
        stop.set()
        ser.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
