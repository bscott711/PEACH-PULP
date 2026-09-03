"""Automated tab: build a phase program (add/remove phases and per-phase motors),
plus workspace save / load.
"""
from __future__ import annotations

from typing import List

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QComboBox,
    QFrame,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QScrollArea,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from . import protocol as P
from .widgets import RunStatusBar, SpeedControl
from .workspace import PhaseStep

_X = "✕"  # ✕


class PhaseMotorRow(QWidget):
    """One motor line inside a phase: motor picker + speed + remove."""

    changed = Signal()
    removeRequested = Signal()

    def __init__(self, names: List[str], idx: int = 0, speed: int = 0, parent=None):
        super().__init__(parent)
        self._combo = QComboBox()
        self._combo.addItems(names)
        self._combo.setCurrentIndex(max(0, min(idx, len(names) - 1)))
        self._combo.setMinimumWidth(120)

        self._speed = SpeedControl(0, P.SPEED_MAX)
        self._speed.setValue(speed or P.DEFAULT_STEP_SPEED)

        self._rm = QPushButton(_X)
        self._rm.setObjectName("iconbtn")
        self._rm.setFixedSize(34, 34)
        self._rm.setToolTip("remove this motor from the phase")

        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(8)
        lay.addWidget(self._combo)
        lay.addWidget(self._speed, 1)
        lay.addWidget(self._rm)

        self._combo.currentIndexChanged.connect(lambda _i: self.changed.emit())
        self._speed.valueChanged.connect(lambda _v: self.changed.emit())
        self._rm.clicked.connect(self.removeRequested)

    def set_names(self, names: List[str]) -> None:
        keep = self._combo.currentIndex()
        self._combo.blockSignals(True)
        self._combo.clear()
        self._combo.addItems(names)
        self._combo.setCurrentIndex(max(0, min(keep, len(names) - 1)))
        self._combo.blockSignals(False)

    def motor(self) -> int:
        return self._combo.currentIndex()

    def speed(self) -> int:
        return self._speed.value()

    def set_editable(self, on: bool) -> None:
        self._combo.setEnabled(on)
        self._speed.setReadOnly(not on)
        self._rm.setEnabled(on)


class PhaseCard(QFrame):
    """A phase: label + duration + a list of motor rows."""

    changed = Signal()
    removeRequested = Signal()

    def __init__(self, names: List[str], step: PhaseStep | None = None, parent=None):
        super().__init__(parent)
        self.setObjectName("card")
        self._names = list(names)
        self._rows: List[PhaseMotorRow] = []

        self._num = QLabel("Phase 1")
        self._num.setStyleSheet("font-weight:800; font-size:15px;")

        self._label = QLineEdit()
        self._label.setPlaceholderText("label (optional)")
        self._label.setMaximumWidth(220)

        self._secs = QSpinBox()
        self._secs.setRange(P.PHASE_SECONDS_MIN, P.PHASE_SECONDS_MAX)
        self._secs.setSuffix(" s")
        self._secs.setAlignment(Qt.AlignRight)
        self._secs.setFixedWidth(92)
        self._secs.setMinimumHeight(38)
        self._secs.setValue(30)

        self._del = QPushButton(_X)
        self._del.setObjectName("iconbtn")
        self._del.setFixedSize(34, 34)
        self._del.setToolTip("delete this phase")

        head = QHBoxLayout()
        head.setSpacing(10)
        head.addWidget(self._num)
        head.addWidget(self._label, 1)
        head.addWidget(QLabel("for"))
        head.addWidget(self._secs)
        head.addWidget(self._del)

        self._rows_box = QVBoxLayout()
        self._rows_box.setSpacing(6)

        self._add_motor = QPushButton("+ Add motor")
        self._add_motor.setObjectName("ghostbtn")
        self._add_motor.clicked.connect(lambda: (self._new_row(), self.changed.emit()))

        outer = QVBoxLayout(self)
        outer.setContentsMargins(14, 12, 14, 12)
        outer.setSpacing(10)
        outer.addLayout(head)
        outer.addLayout(self._rows_box)
        outer.addWidget(self._add_motor, 0, Qt.AlignLeft)

        self._label.textChanged.connect(lambda _t: self.changed.emit())
        self._secs.valueChanged.connect(lambda _v: self.changed.emit())
        self._del.clicked.connect(self.removeRequested)

        if step is not None:
            self.load_step(step)

    # ---- rows -------------------------------------------------------
    def _new_row(self, idx: int = 0, speed: int = 0) -> PhaseMotorRow:
        row = PhaseMotorRow(self._names, idx, speed)
        row.changed.connect(self.changed)
        row.removeRequested.connect(lambda r=row: self._remove_row(r))
        self._rows.append(row)
        self._rows_box.addWidget(row)
        return row

    def _remove_row(self, row: PhaseMotorRow) -> None:
        if row in self._rows:
            self._rows.remove(row)
            row.setParent(None)
            row.deleteLater()
            self.changed.emit()

    def set_names(self, names: List[str]) -> None:
        self._names = list(names)
        for r in self._rows:
            r.set_names(names)

    def set_index(self, i: int) -> None:
        self._num.setText("Phase {}".format(i + 1))

    def set_editable(self, on: bool) -> None:
        self._label.setEnabled(on)
        self._secs.setEnabled(on)
        self._del.setEnabled(on)
        self._add_motor.setEnabled(on)
        for r in self._rows:
            r.set_editable(on)

    def set_active(self, active: bool) -> None:
        self.setProperty("active", "true" if active else "false")
        self.style().unpolish(self)
        self.style().polish(self)

    # ---- model ----------------------------------------------------
    def load_step(self, step: PhaseStep) -> None:
        for r in list(self._rows):
            r.setParent(None)
            r.deleteLater()
        self._rows.clear()
        self._label.blockSignals(True)
        self._secs.blockSignals(True)
        self._label.setText(step.label)
        self._secs.setValue(P.clamp_phase_seconds(step.seconds))
        self._label.blockSignals(False)
        self._secs.blockSignals(False)
        for idx, spd in sorted(step.speeds.items()):
            self._new_row(idx, spd)
        if not self._rows:
            self._new_row()

    def to_step(self) -> PhaseStep:
        speeds = {}
        for r in self._rows:
            speeds[r.motor()] = r.speed()  # last row wins on duplicate motor
        return PhaseStep(
            seconds=self._secs.value(),
            speeds=speeds,
            label=self._label.text().strip(),
        )


class WorkspaceBar(QWidget):
    loadRequested = Signal(str)
    saveRequested = Signal()
    saveAsRequested = Signal()
    newRequested = Signal()

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self._combo = QComboBox()
        self._combo.setMinimumWidth(180)
        self._name = QLabel("—")
        self._name.setStyleSheet("font-weight:700;")

        load = QPushButton("Load")
        save = QPushButton("Save")
        save_as = QPushButton("Save As…")
        new = QPushButton("New")
        for b in (load, save, save_as, new):
            b.setMinimumHeight(38)

        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(8)
        lay.addWidget(QLabel("Workspace"))
        lay.addWidget(self._name)
        lay.addStretch(1)
        lay.addWidget(self._combo)
        lay.addWidget(load)
        lay.addWidget(save)
        lay.addWidget(save_as)
        lay.addWidget(new)

        load.clicked.connect(
            lambda: self.loadRequested.emit(self._combo.currentText())
        )
        save.clicked.connect(self.saveRequested)
        save_as.clicked.connect(self.saveAsRequested)
        new.clicked.connect(self.newRequested)

    def refresh(self, names: List[str], current: str, dirty: bool) -> None:
        self._combo.blockSignals(True)
        self._combo.clear()
        self._combo.addItems(names)
        if current in names:
            self._combo.setCurrentText(current)
        self._combo.blockSignals(False)
        self._name.setText(current + (" *" if dirty else ""))


class AutomatedPanel(QWidget):
    programChanged = Signal()
    loadRequested = Signal(str)
    saveRequested = Signal()
    saveAsRequested = Signal()
    newRequested = Signal()

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self._names = list(P.PUMP_ROLES)
        self._cards: List[PhaseCard] = []
        self._running = False

        self.bar = WorkspaceBar()
        self.bar.loadRequested.connect(self.loadRequested)
        self.bar.saveRequested.connect(self.saveRequested)
        self.bar.saveAsRequested.connect(self.saveAsRequested)
        self.bar.newRequested.connect(self.newRequested)

        self.status = RunStatusBar()
        self.status.setVisible(False)  # only while a sequence runs

        self._cards_box = QVBoxLayout()
        self._cards_box.setSpacing(10)
        self._cards_box.addStretch(1)

        host = QWidget()
        host.setLayout(self._cards_box)
        self._scroll = QScrollArea()
        self._scroll.setWidgetResizable(True)
        self._scroll.setWidget(host)
        self._scroll.setFrameShape(QFrame.NoFrame)
        self._scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)

        self._add_phase = QPushButton("+ Add phase")
        self._add_phase.setObjectName("ghostbtn")
        self._add_phase.setMinimumHeight(40)
        self._add_phase.clicked.connect(self._on_add_phase)

        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(10)
        root.addWidget(self.bar)
        root.addWidget(self.status)
        root.addWidget(self._scroll, 1)
        root.addWidget(self._add_phase, 0, Qt.AlignLeft)

        self._set_running(False)

    # ---- editing --------------------------------------------------
    def _on_add_phase(self) -> None:
        self._add_card(PhaseStep(seconds=30, speeds={}))
        self._renumber()
        self.programChanged.emit()

    def _add_card(self, step: PhaseStep) -> PhaseCard:
        card = PhaseCard(self._names, step)
        card.changed.connect(self.programChanged)
        card.removeRequested.connect(lambda c=card: self._remove_card(c))
        self._cards.append(card)
        self._cards_box.insertWidget(self._cards_box.count() - 1, card)
        return card

    def _remove_card(self, card: PhaseCard) -> None:
        if card in self._cards:
            self._cards.remove(card)
            card.setParent(None)
            card.deleteLater()
            self._renumber()
            self.programChanged.emit()

    def _renumber(self) -> None:
        for i, c in enumerate(self._cards):
            c.set_index(i)

    # ---- model ---------------------------------------------------
    def set_pump_names(self, names: List[str]) -> None:
        self._names = list(names)
        for c in self._cards:
            c.set_names(self._names)

    def load_program(self, phases: List[PhaseStep], names: List[str]) -> None:
        self._names = list(names)
        for c in list(self._cards):
            c.setParent(None)
            c.deleteLater()
        self._cards.clear()
        for step in phases:
            self._add_card(step)
        if not self._cards:
            self._add_card(PhaseStep(seconds=30, speeds={}))
        self._renumber()

    def program(self) -> List[PhaseStep]:
        return [c.to_step() for c in self._cards]

    def refresh_bar(self, names: List[str], current: str, dirty: bool) -> None:
        self.bar.refresh(names, current, dirty)

    # ---- telemetry ---------------------------------------------
    def _set_running(self, running: bool) -> None:
        self._running = running
        self._add_phase.setEnabled(not running)
        for c in self._cards:
            c.set_editable(not running)

    def apply_telemetry(self, t: P.Telemetry, label: str = "", total: int = 0) -> None:
        if t.running != self._running:
            self._set_running(t.running)
        for i, c in enumerate(self._cards):
            c.set_active(t.running and i == t.phase)
        self.status.setVisible(t.running)
        self.status.apply_telemetry(t, label, total)
