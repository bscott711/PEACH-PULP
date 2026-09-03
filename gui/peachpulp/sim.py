"""In-process fake of the Octopus firmware, for GUI development without hardware.

Consumes the same command lines and produces the same telemetry / event lines as
src/core/SerialLink.cpp + src/controller.cpp. Not cycle-accurate — just faithful
to the observable protocol.

The automated sequence is an uploaded program (PROGCLEAR / PROGADD… / PROGCOMMIT):
a list of ``(seconds, [speed per motor])`` phases. Until something is uploaded it
runs the seed 4-phase program so the GUI has something to show.
"""
from __future__ import annotations

import json
import time
from typing import List, Optional, Tuple

from . import protocol as P

Phase = Tuple[int, List[int]]  # (seconds, speed per motor)


def _seed_program() -> List[Phase]:
    prog: List[Phase] = []
    for mask, secs in zip(P.PHASE_MASKS, P.DEFAULT_PHASE_SECONDS):
        speeds = [
            P.DEFAULT_STEP_SPEED if mask & (1 << i) else 0
            for i in range(P.NUM_PUMPS)
        ]
        prog.append((secs, speeds))
    return prog


class FakeFirmware:
    def __init__(self) -> None:
        self.live_speeds: List[int] = [0] * P.NUM_PUMPS
        self.enabled: List[bool] = [True] * P.NUM_PUMPS
        self.manual: List[bool] = [False] * P.NUM_PUMPS
        self.program: List[Phase] = _seed_program()
        self._staging: Optional[List[Phase]] = None
        self.phase: int = -1
        self._phase_end: float = 0.0
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
            if self.phase < 0 and self.program:
                self.phase = 0
                self._phase_end = time.monotonic() + self.program[0][0]
                self.manual = [False] * P.NUM_PUMPS
                self.enabled = [True] * P.NUM_PUMPS  # re-energise every driver
                self._out.append("!EVENT phase 0")
        elif c == "STOP":
            self.phase = -1
            self.manual = [False] * P.NUM_PUMPS
        elif c == "SKIP":
            self._skip = True
        elif c == "SPEED" and len(args) == 2:
            i, v = _int(args[0]), P.clamp_speed(_int(args[1]))
            if 0 <= i < P.NUM_PUMPS:
                self.live_speeds[i] = v
        elif c == "PHASETIME" and len(args) == 2:
            p, s = _int(args[0]), P.clamp_phase_seconds(_int(args[1]))
            if 0 <= p < len(self.program):
                self.program[p] = (s, self.program[p][1])
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
                    self.live_speeds[i] = v
        elif c == "PROGCLEAR":
            self._staging = []
        elif c == "PROGADD":
            if self._staging is None:
                self._staging = []
            secs = P.clamp_phase_seconds(_int(args[0])) if args else 30
            sp = [P.clamp_speed(_int(x)) for x in args[1 : 1 + P.NUM_PUMPS]]
            sp += [0] * (P.NUM_PUMPS - len(sp))
            if len(self._staging) < P.MAX_PHASES:
                self._staging.append((secs, sp))
        elif c == "PROGCOMMIT":
            if self._staging:
                self.program = self._staging
                if self.phase >= len(self.program):
                    self.phase = -1
                self._out.append("!EVENT prog {}".format(len(self.program)))
            else:
                self._out.append("!ERR sim: empty program")
            self._staging = None
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
        if nxt >= len(self.program):
            self.phase = -1
            self._out.append("!EVENT done")
        else:
            self.phase = nxt
            self._phase_end = now + self.program[nxt][0]
            self._out.append("!EVENT phase {}".format(nxt))

    # ---- telemetry JSON line (call ~5 Hz) -----------------------------
    def telemetry_line(self) -> str:
        now = time.monotonic()
        running = self.phase >= 0
        remaining = max(0, int(self._phase_end - now)) if running else 0
        ph_speeds = self.program[self.phase][1] if running else None
        pumps = []
        for i in range(P.NUM_PUMPS):
            if running:
                sp = ph_speeds[i]
                run = sp != 0
            else:
                sp = self.live_speeds[i]
                run = self.manual[i] and sp != 0
            pumps.append({"sp": sp, "run": int(run), "en": int(self.enabled[i])})
        return json.dumps(
            {
                "phase": self.phase,
                "nphases": len(self.program),
                "remaining": remaining,
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
