"""In-process fake of the Octopus firmware, for GUI development without hardware.

Consumes the same command lines and produces the same telemetry / event lines as
src/core/SerialLink.cpp + src/controller.cpp. Not cycle-accurate — just faithful
to the observable protocol.
"""
from __future__ import annotations

import json
import time
from typing import List

from . import protocol as P


class FakeFirmware:
    def __init__(self) -> None:
        self.speeds: List[int] = [5] * P.NUM_PUMPS
        self.enabled: List[bool] = [True] * P.NUM_PUMPS
        self.manual: List[bool] = [False] * P.NUM_PUMPS
        self.phase_seconds: List[int] = list(P.DEFAULT_PHASE_SECONDS)
        self.phase: int = -1
        self._phase_end: float = 0.0
        self.estop: bool = False
        self._skip: bool = False
        self._out: List[str] = []

    # ---- incoming command line -------------------------------------------
    def feed(self, line: str) -> None:
        parts = (line or "").strip().split()
        if not parts:
            return
        c, args = parts[0].upper(), parts[1:]

        if c == "PING":
            self._out.append("PONG")
        elif c == "RUN":
            if self.phase < 0:
                self.phase = 0
                self._phase_end = time.monotonic() + self.phase_seconds[0]
                self.manual = [False] * P.NUM_PUMPS
                self.enabled = [True] * P.NUM_PUMPS
                self._out.append("!EVENT phase 0")
        elif c in ("STOP", "ESTOP"):
            self.phase = -1
            self.manual = [False] * P.NUM_PUMPS
        elif c == "SKIP":
            self._skip = True
        elif c == "SPEED" and len(args) == 2:
            i, v = _int(args[0]), P.clamp_speed(_int(args[1]))
            if 0 <= i < P.NUM_PUMPS:
                self.speeds[i] = v
        elif c == "PHASETIME" and len(args) == 2:
            p, s = _int(args[0]), P.clamp_phase_seconds(_int(args[1]))
            if 0 <= p < P.NUM_PHASES:
                self.phase_seconds[p] = s
        elif c == "ENABLE" and len(args) == 2:
            i, e = _int(args[0]), args[1] != "0"
            if 0 <= i < P.NUM_PUMPS:
                self.enabled[i] = e
                if not e:
                    self.manual[i] = False
        elif c == "JOG" and len(args) == 2:
            i, v = _int(args[0]), P.clamp_speed(_int(args[1]))
            if 0 <= i < P.NUM_PUMPS and self.phase < 0:
                self.manual[i] = v != 0
                if v:
                    self.speeds[i] = v
        elif c == "STATE":
            pass  # telemetry is emitted continuously
        else:
            self._out.append("!ERR sim: bad `{}`".format(line.strip()))

    # ---- periodic advance (call ~20 Hz) --------------------------------
    def tick(self) -> None:
        if self.phase < 0:
            return
        now = time.monotonic()
        if not (self._skip or now >= self._phase_end):
            return
        self._skip = False
        nxt = self.phase + 1
        if nxt >= P.NUM_PHASES:
            self.phase = -1
            self._out.append("!EVENT done")
        else:
            self.phase = nxt
            self._phase_end = now + self.phase_seconds[nxt]
            self._out.append("!EVENT phase {}".format(nxt))

    # ---- telemetry JSON line (call ~5 Hz) -----------------------------
    def telemetry_line(self) -> str:
        now = time.monotonic()
        remaining = max(0, int(self._phase_end - now)) if self.phase >= 0 else 0
        mask = P.PHASE_MASKS[self.phase] if self.phase >= 0 else 0
        pumps = []
        for i in range(P.NUM_PUMPS):
            if self.phase >= 0:
                run = bool(mask & (1 << i)) and self.speeds[i] != 0
            else:
                run = self.manual[i] and self.speeds[i] != 0
            pumps.append(
                {"sp": self.speeds[i], "run": int(run), "en": int(self.enabled[i])}
            )
        return json.dumps(
            {
                "phase": self.phase,
                "remaining": remaining,
                "estop": self.estop,
                "pumps": pumps,
            }
        )

    # ---- pending async lines (PONG / !EVENT / !ERR) -------------------
    def drain(self) -> List[str]:
        out, self._out = self._out, []
        return out


def _int(s: str) -> int:
    try:
        return int(s)
    except (TypeError, ValueError):
        return 0
