"""Workspaces: the operator's saved setup — pump names, the automated phase
program, and the last-used live speeds. Pure Python (no Qt), JSON on disk.

Files live under a single root:
  $PEACHPULP_HOME                     if set (tests use this)
  else $XDG_DATA_HOME/peachpulp       if XDG_DATA_HOME is set
  else ~/.local/share/peachpulp

    <root>/workspaces/<name>.json     one file per workspace
    <root>/settings.json              { "last_workspace": "<name>" }
"""
from __future__ import annotations

import json
import os
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional

from . import protocol as P

MAX_PHASES = 32
MAX_MOTOR_ROWS = P.NUM_PUMPS


# --------------------------------------------------------------------------
# model
# --------------------------------------------------------------------------
@dataclass
class PhaseStep:
    """One phase: a duration and a speed for each motor that runs in it.

    `speeds` maps motor index -> steps/s. A motor absent from the map is
    stopped (0) for the phase.
    """

    seconds: int = 30
    speeds: Dict[int, int] = field(default_factory=dict)
    label: str = ""

    def speed_list(self) -> List[int]:
        return [int(self.speeds.get(i, 0)) for i in range(P.NUM_PUMPS)]

    def to_dict(self) -> dict:
        return {
            "seconds": int(self.seconds),
            "label": self.label,
            "speeds": {str(k): int(v) for k, v in sorted(self.speeds.items())},
        }

    @classmethod
    def from_dict(cls, d: dict) -> "PhaseStep":
        raw = d.get("speeds", {}) or {}
        speeds: Dict[int, int] = {}
        for k, v in raw.items():
            try:
                idx = int(k)
            except (TypeError, ValueError):
                continue
            if 0 <= idx < P.NUM_PUMPS:
                speeds[idx] = P.clamp_speed(v)
        return cls(
            seconds=P.clamp_phase_seconds(d.get("seconds", 30)),
            speeds=speeds,
            label=str(d.get("label", "")),
        )


@dataclass
class Workspace:
    name: str = "Untitled"
    pump_names: List[str] = field(default_factory=lambda: list(P.PUMP_ROLES))
    phases: List[PhaseStep] = field(default_factory=list)
    live_speeds: List[int] = field(default_factory=lambda: [0] * P.NUM_PUMPS)

    def to_dict(self) -> dict:
        return {
            "version": 1,
            "name": self.name,
            "pump_names": list(self.pump_names),
            "live_speeds": [int(s) for s in self.live_speeds],
            "phases": [p.to_dict() for p in self.phases],
        }

    @classmethod
    def from_dict(cls, d: dict) -> "Workspace":
        names = list(d.get("pump_names") or P.PUMP_ROLES)
        names = (names + list(P.PUMP_ROLES))[: P.NUM_PUMPS]
        live = [P.clamp_speed(s) for s in (d.get("live_speeds") or [])]
        live = (live + [0] * P.NUM_PUMPS)[: P.NUM_PUMPS]
        phases = [PhaseStep.from_dict(p) for p in (d.get("phases") or [])][:MAX_PHASES]
        return cls(
            name=str(d.get("name", "Untitled")),
            pump_names=names,
            phases=phases,
            live_speeds=live,
        )

    def program_lines(self) -> List[str]:
        """The PROGCLEAR / PROGADD… / PROGCOMMIT lines for this workspace."""
        return P.cmd_program(self.phases)


def default_workspace() -> Workspace:
    """Seeded from the original fixed 4-phase protocol."""
    phases: List[PhaseStep] = []
    for mask, secs, name in zip(
        P.PHASE_MASKS, P.DEFAULT_PHASE_SECONDS, P.PHASE_NAMES
    ):
        speeds = {
            i: P.DEFAULT_STEP_SPEED for i in range(P.NUM_PUMPS) if mask & (1 << i)
        }
        phases.append(PhaseStep(seconds=secs, speeds=speeds, label=name))
    return Workspace(name="Default", phases=phases)


# --------------------------------------------------------------------------
# storage
# --------------------------------------------------------------------------
def _root() -> Path:
    env = os.environ.get("PEACHPULP_HOME")
    if env:
        return Path(env).expanduser()
    xdg = os.environ.get("XDG_DATA_HOME")
    base = Path(xdg).expanduser() if xdg else Path.home() / ".local" / "share"
    return base / "peachpulp"


def _workspaces_dir() -> Path:
    d = _root() / "workspaces"
    d.mkdir(parents=True, exist_ok=True)
    return d


def _settings_path() -> Path:
    _root().mkdir(parents=True, exist_ok=True)
    return _root() / "settings.json"


def _safe_stem(name: str) -> str:
    stem = re.sub(r"[^A-Za-z0-9 _-]", "", name or "").strip()
    return stem or "workspace"


def workspace_path(name: str) -> Path:
    return _workspaces_dir() / (_safe_stem(name) + ".json")


def list_workspaces() -> List[str]:
    return sorted(p.stem for p in _workspaces_dir().glob("*.json"))


def save(ws: Workspace) -> Path:
    path = workspace_path(ws.name)
    tmp = path.with_suffix(".json.tmp")
    tmp.write_text(json.dumps(ws.to_dict(), indent=2))
    tmp.replace(path)
    remember_last(ws.name)
    return path


def load(name: str) -> Workspace:
    path = workspace_path(name)
    ws = Workspace.from_dict(json.loads(path.read_text()))
    ws.name = path.stem
    remember_last(ws.name)  # opening a workspace makes it the "last used"
    return ws


def delete(name: str) -> None:
    workspace_path(name).unlink(missing_ok=True)


def remember_last(name: str) -> None:
    try:
        _settings_path().write_text(json.dumps({"last_workspace": _safe_stem(name)}))
    except OSError:
        pass


def last_used() -> Optional[str]:
    try:
        data = json.loads(_settings_path().read_text())
    except (OSError, ValueError):
        return None
    name = data.get("last_workspace")
    return name if name and workspace_path(name).exists() else None


def load_last_or_default() -> Workspace:
    name = last_used()
    if name:
        try:
            return load(name)
        except (OSError, ValueError):
            pass
    return default_workspace()
