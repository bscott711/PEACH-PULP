"""Live tab (home): direct manual control of each pump.

While an automated sequence runs this becomes a read-only mirror of telemetry
(the pump rows disable themselves).
"""
from __future__ import annotations

from typing import List

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import QFrame, QLabel, QScrollArea, QVBoxLayout, QWidget

from . import protocol as P
from .widgets import PumpRow, RunStatusBar


class LivePanel(QWidget):
    speedChanged = Signal(int, int)
    enableToggled = Signal(int, bool)
    jogRequested = Signal(int, int)
    jogStopped = Signal(int)

    def __init__(self, count: int, names: List[str] | None = None, parent=None):
        super().__init__(parent)
        names = names or list(P.PUMP_ROLES)

        self.status = RunStatusBar()

        hdr = QLabel("PUMPS")
        hdr.setObjectName("sectionHeader")

        self.rows: List[PumpRow] = []
        host = QWidget()
        rows_v = QVBoxLayout(host)
        rows_v.setContentsMargins(0, 0, 0, 0)
        rows_v.setSpacing(8)
        for i in range(count):
            row = PumpRow(i, names[i] if i < len(names) else None)
            row.speedChanged.connect(self.speedChanged)
            row.enableToggled.connect(self.enableToggled)
            row.jogRequested.connect(self.jogRequested)
            row.jogStopped.connect(self.jogStopped)
            self.rows.append(row)
            rows_v.addWidget(row)
        rows_v.addStretch(1)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(host)
        scroll.setFrameShape(QFrame.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)

        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(10)
        root.addWidget(self.status)
        root.addWidget(hdr)
        root.addWidget(scroll, 1)

    def set_pump_names(self, names: List[str]) -> None:
        for i, row in enumerate(self.rows):
            if i < len(names):
                row.set_name(names[i])

    def set_speeds(self, speeds: List[int]) -> None:
        for i, row in enumerate(self.rows):
            if i < len(speeds):
                row.set_speed(speeds[i])

    def speeds(self) -> List[int]:
        out = [0] * P.NUM_PUMPS
        for row in self.rows:
            out[row.idx] = row.speed()
        return out

    def apply_telemetry(self, t: P.Telemetry, label: str = "", total: int = 0) -> None:
        self.status.apply_telemetry(t, label, total)
        for row in self.rows:
            row.apply_state(t.pumps[row.idx], t.running)
