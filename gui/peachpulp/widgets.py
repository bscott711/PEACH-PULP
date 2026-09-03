"""Touch-friendly widgets for the PEACH PULP operator GUI."""
from __future__ import annotations

from PySide6.QtCore import Qt, QTimer, Signal
from PySide6.QtWidgets import (
    QCheckBox,
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QProgressBar,
    QPushButton,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from . import protocol as P

_SPEED_STEP = 100
_DEBOUNCE_MS = 300


class PumpRow(QFrame):
    """One pump: role name, speed, run indicator, hold-torque toggle, jog."""

    speedChanged = Signal(int, int)     # idx, steps/s
    enableToggled = Signal(int, bool)   # idx, hold-torque on
    jogRequested = Signal(int, int)     # idx, steps/s
    jogStopped = Signal(int)            # idx

    def __init__(self, idx: int, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.idx = idx
        self.setFrameShape(QFrame.StyledPanel)

        self._name = QLabel(P.PUMP_ROLES[idx])
        self._name.setStyleSheet("font-weight: 600;")

        self._dot = QLabel("●")  # ●
        self._dot.setStyleSheet("color: #888;")
        self._dot.setToolTip("running indicator")

        self._speed = QSpinBox()
        self._speed.setRange(-P.SPEED_MAX, P.SPEED_MAX)
        self._speed.setSingleStep(_SPEED_STEP)
        self._speed.setSuffix(" st/s")
        self._speed.setAlignment(Qt.AlignRight)
        self._speed.setMinimumHeight(40)

        self._hold = QCheckBox("Hold")
        self._hold.setChecked(True)
        self._hold.setToolTip("holding torque — uncheck to hand-turn the syringe")

        self._jog = QPushButton("Jog")
        self._jog.setCheckable(True)
        self._jog.setMinimumHeight(40)

        row = QHBoxLayout(self)
        row.addWidget(self._dot)
        row.addWidget(self._name, 2)
        row.addWidget(self._speed, 3)
        row.addWidget(self._hold, 1)
        row.addWidget(self._jog, 1)

        self._debounce = QTimer(self)
        self._debounce.setSingleShot(True)
        self._debounce.setInterval(_DEBOUNCE_MS)
        self._debounce.timeout.connect(
            lambda: self.speedChanged.emit(self.idx, self._speed.value())
        )
        self._speed.valueChanged.connect(lambda _v: self._debounce.start())
        self._hold.toggled.connect(lambda on: self.enableToggled.emit(self.idx, on))
        self._jog.toggled.connect(self._on_jog)

    def _on_jog(self, on: bool) -> None:
        if on:
            self.jogRequested.emit(self.idx, self._speed.value())
        else:
            self.jogStopped.emit(self.idx)

    # ---- driven by telemetry --------------------------------------------
    def apply_state(self, st: P.PumpState, protocol_running: bool) -> None:
        self._dot.setStyleSheet("color: #2ecc71;" if st.running else "color: #888;")
        if self._hold.isChecked() != st.enabled:
            self._hold.blockSignals(True)
            self._hold.setChecked(st.enabled)
            self._hold.blockSignals(False)
        # only pull the spinbox from telemetry when the operator isn't touching it
        if not self._speed.hasFocus() and not self._debounce.isActive():
            if self._speed.value() != st.speed:
                self._speed.blockSignals(True)
                self._speed.setValue(st.speed)
                self._speed.blockSignals(False)
        self._jog.setEnabled(not protocol_running)
        if protocol_running and self._jog.isChecked():
            self._jog.blockSignals(True)
            self._jog.setChecked(False)
            self._jog.blockSignals(False)


class PhasePanel(QFrame):
    """Current-phase progress + editable per-phase durations."""

    phaseTimeChanged = Signal(int, int)  # phase, seconds

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setFrameShape(QFrame.StyledPanel)
        outer = QVBoxLayout(self)

        self._headline = QLabel("Idle")
        self._headline.setStyleSheet("font-size: 18px; font-weight: 700;")
        self._bar = QProgressBar()
        self._bar.setTextVisible(True)
        self._bar.setMinimumHeight(28)
        outer.addWidget(self._headline)
        outer.addWidget(self._bar)

        grid = QGridLayout()
        grid.addWidget(QLabel("<b>Phase durations</b>"), 0, 0, 1, 3)
        self._secs: list[QSpinBox] = []
        self._marks: list[QLabel] = []
        for p in range(P.NUM_PHASES):
            mark = QLabel(" ")
            name = QLabel("P{}  {}".format(p + 1, P.PHASE_NAMES[p]))
            box = QSpinBox()
            box.setRange(P.PHASE_SECONDS_MIN, P.PHASE_SECONDS_MAX)
            box.setValue(P.DEFAULT_PHASE_SECONDS[p])
            box.setSuffix(" s")
            box.setMinimumHeight(36)
            box.editingFinished.connect(
                lambda p=p, box=box: self.phaseTimeChanged.emit(p, box.value())
            )
            grid.addWidget(mark, p + 1, 0)
            grid.addWidget(name, p + 1, 1)
            grid.addWidget(box, p + 1, 2)
            self._secs.append(box)
            self._marks.append(mark)
        outer.addLayout(grid)
        outer.addStretch(1)

    def apply_telemetry(self, t: P.Telemetry) -> None:
        self._headline.setText(t.phase_label + ("  —  E-STOP" if t.estop else ""))
        for p, mark in enumerate(self._marks):
            mark.setText("▶" if p == t.phase else " ")  # ▶
        if t.phase < 0:
            self._bar.setRange(0, 1)
            self._bar.setValue(0)
            self._bar.setFormat("")
        else:
            total = max(1, self._secs[t.phase].value())
            done = max(0, total - t.remaining_s)
            self._bar.setRange(0, total)
            self._bar.setValue(done)
            self._bar.setFormat("{} s left".format(t.remaining_s))

    def set_phase_seconds(self, phase: int, seconds: int) -> None:
        if 0 <= phase < P.NUM_PHASES:
            box = self._secs[phase]
            if not box.hasFocus() and box.value() != seconds:
                box.blockSignals(True)
                box.setValue(seconds)
                box.blockSignals(False)
