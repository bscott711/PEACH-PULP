"""Pure PEACH PULP serial-protocol helpers — no Qt, no pyserial, unit-testable.

Mirrors the firmware:
  src/core/SystemState.h  NUM_PUMPS, roles
  src/core/SerialLink.cpp command grammar + JSON telemetry

The automated sequence is a *variable* list of phases, each with its own
duration and its own speed per motor. The GUI is the source of truth for it and
uploads it with PROGCLEAR / PROGADD… / PROGCOMMIT; the firmware just runs what
it's given. The constants below (PHASE_NAMES / PHASE_MASKS / DEFAULT_PHASE_SECONDS)
are only the seed for a fresh "Default" workspace — the original fixed 4-phase
protocol.
"""
from __future__ import annotations

import json
from dataclasses import dataclass, field
from typing import Iterable, List, Optional, Tuple

NUM_PUMPS = 8
NUM_PHASES = 4  # length of the seed program only; live programs vary
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

# bit i set => pump i runs during that seed phase
_P = lambda *idx: sum(1 << i for i in idx)  # noqa: E731
PHASE_MASKS = [_P(0, 1, 2), _P(2, 3), _P(2, 4), _P(2, 5)]

DEFAULT_PHASE_SECONDS = [60, 30, 60, 30]

SPEED_MAX = 5000  # PUMP_SPEED_MAX_STEPS
DEFAULT_STEP_SPEED = 1000  # seed speed for a motor added to a phase
PHASE_SECONDS_MIN = 1
PHASE_SECONDS_MAX = 3600
MAX_PHASES = 32  # firmware staging-buffer cap


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


def cmd_skip() -> str:
    return "SKIP"


def cmd_state() -> str:
    return "STATE"


def cmd_dfu() -> str:
    """Reboot the firmware into the ROM bootloader for a jumper-free flash."""
    return "DFU"


def cmd_speed(idx: int, steps: int) -> str:
    return "SPEED {} {}".format(int(idx), clamp_speed(steps))


def cmd_phasetime(phase: int, seconds: int) -> str:
    return "PHASETIME {} {}".format(int(phase), clamp_phase_seconds(seconds))


def cmd_enable(idx: int, on: bool) -> str:
    return "ENABLE {} {}".format(int(idx), 1 if on else 0)


def cmd_jog(idx: int, steps: int) -> str:
    return "JOG {} {}".format(int(idx), clamp_speed(steps))


# ---- automated-program upload (Pi -> Octopus) ------------------------------
# The GUI sends the whole program each time it changes:
#   PROGCLEAR
#   PROGADD <seconds> <s0> <s1> ... <s7>     (once per phase, in order)
#   PROGCOMMIT
# The firmware stages the phases and swaps them in atomically on PROGCOMMIT.
def cmd_prog_clear() -> str:
    return "PROGCLEAR"


def cmd_prog_commit() -> str:
    return "PROGCOMMIT"


def _speed_list(speeds) -> List[int]:
    if hasattr(speeds, "get"):  # dict-like: idx -> steps
        return [clamp_speed(speeds.get(i, 0)) for i in range(NUM_PUMPS)]
    out = [clamp_speed(s) for s in list(speeds)[:NUM_PUMPS]]
    return out + [0] * (NUM_PUMPS - len(out))


def cmd_prog_add(seconds: int, speeds) -> str:
    vals = " ".join(str(s) for s in _speed_list(speeds))
    return "PROGADD {} {}".format(clamp_phase_seconds(seconds), vals)


def cmd_program(phases: Iterable) -> List[str]:
    """Full upload for a list of phases. Each item is either a
    ``(seconds, speeds)`` pair or an object with ``.seconds`` and either
    ``.speed_list()`` or ``.speeds``."""
    lines = [cmd_prog_clear()]
    for ph in phases:
        if hasattr(ph, "seconds"):
            secs = ph.seconds
            speeds = ph.speed_list() if hasattr(ph, "speed_list") else ph.speeds
        else:
            secs, speeds = ph
        lines.append(cmd_prog_add(secs, speeds))
    lines.append(cmd_prog_commit())
    return lines


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
    phase: int = -1  # -1 idle, else 0-based index into the running program
    remaining_s: int = 0
    nphases: int = 0  # length of the running program (0 if unknown)
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
        if self.nphases:
            return "Phase {} / {}".format(self.phase + 1, self.nphases)
        return "Phase {}".format(self.phase + 1)


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
            nphases=int(d.get("nphases", 0)),
        )
        for i, p in enumerate(d.get("pumps", [])[:NUM_PUMPS]):
            t.pumps[i] = PumpState(
                speed=int(p.get("sp", 0)),
                running=bool(p.get("run", 0)),
                enabled=bool(p.get("en", 0)),
            )
        return ("telemetry", t)
    return None
