"""Serial link to the Octopus, as a QObject emitting parsed protocol events.

Two interchangeable implementations:
  SerialLink  — real, pyserial in a QThread, auto-reconnect
  SimLink     — drives sim.FakeFirmware with QTimers (no hardware)

Both emit the same signals; the UI does not care which it has.
"""
from __future__ import annotations

import queue
import time
from typing import Optional

from PySide6.QtCore import QObject, QThread, QTimer, Signal

from . import protocol as P
from .sim import FakeFirmware


class LinkBase(QObject):
    telemetry = Signal(object)   # protocol.Telemetry
    event = Signal(str)          # "phase 2", "done", ...
    error = Signal(str)
    log = Signal(str)
    connected = Signal(bool)

    def start(self) -> None:  # pragma: no cover - interface
        raise NotImplementedError

    def stop(self) -> None:  # pragma: no cover - interface
        raise NotImplementedError

    def send(self, line: str) -> None:  # pragma: no cover - interface
        raise NotImplementedError

    def _dispatch(self, line: str) -> None:
        parsed = P.parse_line(line)
        if not parsed:
            return
        kind, payload = parsed
        if kind == "telemetry":
            self.telemetry.emit(payload)
        elif kind == "event":
            self.event.emit(payload)
        elif kind == "error":
            self.error.emit(payload)
        elif kind == "log":
            self.log.emit(payload)
        # "pong" is ignored — connection liveness is handled by telemetry flow


# --------------------------------------------------------------------------
class SimLink(LinkBase):
    def __init__(self, parent: Optional[QObject] = None) -> None:
        super().__init__(parent)
        self.fw = FakeFirmware()
        self._tick = QTimer(self)
        self._tick.timeout.connect(self._on_tick)
        self._telem = QTimer(self)
        self._telem.timeout.connect(self._on_telem)

    def start(self) -> None:
        self._tick.start(50)
        self._telem.start(200)
        self.connected.emit(True)
        self.log.emit("simulator running (no hardware)")

    def stop(self) -> None:
        self._tick.stop()
        self._telem.stop()
        self.connected.emit(False)

    def send(self, line: str) -> None:
        self.fw.feed(line)
        self._flush()

    def _on_tick(self) -> None:
        self.fw.tick()
        self._flush()

    def _on_telem(self) -> None:
        self._dispatch(self.fw.telemetry_line())

    def _flush(self) -> None:
        for line in self.fw.drain():
            self._dispatch(line)


# --------------------------------------------------------------------------
class SerialLink(LinkBase):
    def __init__(
        self,
        port: Optional[str] = None,
        baud: int = 115200,
        parent: Optional[QObject] = None,
    ) -> None:
        super().__init__(parent)
        self._port = port
        self._baud = baud
        self._reader = _ReaderThread(self)

    def start(self) -> None:
        self._reader.start()

    def stop(self) -> None:
        self._reader.request_stop()
        self._reader.wait(3000)

    def send(self, line: str) -> None:
        self._reader.enqueue(line)


class _ReaderThread(QThread):
    def __init__(self, link: SerialLink) -> None:
        super().__init__()
        self._link = link
        self._stop = False
        self._wq: "queue.Queue[str]" = queue.Queue()

    def request_stop(self) -> None:
        self._stop = True

    def enqueue(self, line: str) -> None:
        self._wq.put(line)

    def run(self) -> None:  # runs in the worker thread
        import serial  # local import so the module is importable without pyserial

        while not self._stop:
            port = self._link._port or _autodetect(serial)
            if not port:
                self._link.error.emit("no USB serial port found")
                self._sleep(2.0)
                continue
            try:
                ser = serial.Serial(port, self._link._baud, timeout=0.2)
            except serial.SerialException as exc:
                self._link.error.emit("open {}: {}".format(port, exc))
                self._sleep(2.0)
                continue

            self._link.connected.emit(True)
            self._link.log.emit("connected {}".format(port))
            buf = b""
            try:
                ser.write(b"PING\n")
                while not self._stop:
                    self._drain_writes(ser)
                    data = ser.read(256)
                    if data:
                        buf += data
                        while b"\n" in buf:
                            raw, buf = buf.split(b"\n", 1)
                            self._link._dispatch(raw.decode("utf-8", "replace"))
            except serial.SerialException as exc:
                self._link.error.emit("{}: {}".format(port, exc))
            finally:
                try:
                    ser.close()
                except Exception:
                    pass
                self._link.connected.emit(False)
            self._sleep(1.0)

    def _drain_writes(self, ser) -> None:
        try:
            while True:
                line = self._wq.get_nowait()
                ser.write((line + "\n").encode("ascii", "ignore"))
        except queue.Empty:
            pass

    def _sleep(self, secs: float) -> None:
        end = time.monotonic() + secs
        while time.monotonic() < end and not self._stop:
            time.sleep(0.05)


def _autodetect(serial_mod) -> Optional[str]:
    """First CDC-ACM / STMicro device we can find."""
    try:
        from serial.tools import list_ports
    except Exception:  # pragma: no cover
        return None
    candidates = []
    for p in list_ports.comports():
        score = 0
        if getattr(p, "vid", None) == 0x0483:  # STMicroelectronics
            score += 2
        if p.device and ("ACM" in p.device or "usbmodem" in p.device):
            score += 1
        if score:
            candidates.append((score, p.device))
    candidates.sort(reverse=True)
    return candidates[0][1] if candidates else None
