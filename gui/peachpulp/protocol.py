"""Pure PEACH PULP serial-protocol helpers — no Qt, no pyserial, unit-testable.

Mirrors the firmware:
  src/core/Protocol.h     phase table / names
  src/core/SystemState.h  NUM_PUMPS, NUM_PHASES, roles
  src/core/SerialLink.cpp command grammar + JSON telemetry
"""
from __future__ import annotations

import json
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

NUM_PUMPS = 8
NUM_PHASES = 4
ACTIVE_PUMPS = 6  # indices 0..5 are wired roles; 6, 7 are spare driver slots

PUMP_ROLES = [
    "Sample", "Dye", "Sheath", "Wash", "Antibody", "Wash2", "Spare 6", "Spare 7",
]

PHASE_NAMES = [
    "Sample + Dye + Sheath",
    "Sheath + Wash",
    "Sheath + Antibody",
    "Sheath + Wash 2",
]

# bit i set => pump i runs during that phase (mirrors kProtocol[] in Protocol.h)
_P = lambda *idx: sum(1 << i for i in idx)  # noqa: E731
PHASE_MASKS = [_P(0, 1, 2), _P(2, 3), _P(2, 4), _P(2, 5)]

DEFAULT_PHASE_SECONDS = [60, 30, 60, 30]

SPEED_MAX = 5000  # PUMP_SPEED_MAX_STEPS
PHASE_SECONDS_MIN = 1
PHASE_SECONDS_MAX = 3600


def clamp_speed(steps) -> int:
    return max(-SPEED_MAX, min(SPEED_MAX, int(steps)))


def clamp_phase_seconds(seconds) -> int:
    return max(PHASE_SECONDS_MIN, min(PHASE_SECONDS_MAX, int(seconds)))


# --------------------------------------------------------------------------
# Command builders  (Pi -> Octopus)
# --------------------------------------------------------------------------
def cmd_ping() -> str:
    return "PING"


def cmd_run() -> str:
    return "RUN"


def cmd_stop() -> str:
    return "STOP"


def cmd_estop() -> str:
    return "ESTOP"


def cmd_skip() -> str:
    return "SKIP"


def cmd_state() -> str:
    return "STATE"


def cmd_speed(idx: int, steps: int) -> str:
    return "SPEED {} {}".format(int(idx), clamp_speed(steps))


def cmd_phasetime(phase: int, seconds: int) -> str:
    return "PHASETIME {} {}".format(int(phase), clamp_phase_seconds(seconds))


def cmd_enable(idx: int, on: bool) -> str:
    return "ENABLE {} {}".format(int(idx), 1 if on else 0)


def cmd_jog(idx: int, steps: int) -> str:
    return "JOG {} {}".format(int(idx), clamp_speed(steps))


# --------------------------------------------------------------------------
# Telemetry / event parsing  (Octopus -> Pi)
# --------------------------------------------------------------------------
@dataclass
class PumpState:
    speed: int = 0
    running: bool = False
    enabled: bool = True


@dataclass
class Telemetry:
    phase: int = -1  # -1 idle, 0..NUM_PHASES-1
    remaining_s: int = 0
    estop: bool = False
    pumps: List[PumpState] = field(
        default_factory=lambda: [PumpState() for _ in range(NUM_PUMPS)]
    )

    @property
    def running(self) -> bool:
        return self.phase >= 0

    @property
    def phase_label(self) -> str:
        if self.phase < 0:
            return "Idle"
        return "Phase {} — {}".format(self.phase + 1, PHASE_NAMES[self.phase])


# parse_line returns (kind, payload):
#   ("telemetry", Telemetry) | ("event", str) | ("error", str)
#   ("log", str) | ("pong", None) | None  (unrecognised / blank)
ParsedLine = Optional[Tuple[str, object]]


def parse_line(line: str) -> ParsedLine:
    line = (line or "").strip()
    if not line:
        return None
    if line == "PONG":
        return ("pong", None)
    if line.startswith("#"):
        return ("log", line[1:].strip())
    if line.startswith("!EVENT"):
        return ("event", line[len("!EVENT"):].strip())
    if line.startswith("!ERR"):
        return ("error", line[len("!ERR"):].strip())
    if line.startswith("{"):
        try:
            d = json.loads(line)
        except ValueError:
            return ("error", "bad JSON: " + line[:60])
        t = Telemetry(
            phase=int(d.get("phase", -1)),
            remaining_s=int(d.get("remaining", 0)),
            estop=bool(d.get("estop", False)),
        )
        for i, p in enumerate(d.get("pumps", [])[:NUM_PUMPS]):
            t.pumps[i] = PumpState(
                speed=int(p.get("sp", 0)),
                running=bool(p.get("run", 0)),
                enabled=bool(p.get("en", 0)),
            )
        return ("telemetry", t)
    return None
