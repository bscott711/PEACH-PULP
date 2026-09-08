"""Shared touch widgets: speed control, pump row, run-status bar."""
from __future__ import annotations

from PySide6.QtCore import Qt, QTimer, Signal
from PySide6.QtWidgets import (
    QFrame,
    QHBoxLayout,
    QLabel,
    QProgressBar,
    QPushButton,
    QSizePolicy,
    QSlider,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from . import protocol as P

_DEBOUNCE_MS = 250
_SETTLE_MS = 700
_HOLD_CONFIRM_MS = 1800  # wait this long for telemetry to confirm a Hold/Free tap
_DOT = "●"   # ●
_PLAY = "▶"  # ▶


class SpeedControl(QWidget):
    """A draggable bar + a numeric box, kept in sync. steps/s.

    Emits ``valueChanged`` (debounced) on user edits only — ``setValue`` is silent.
    """

    valueChanged = Signal(int)

    def __init__(
        self,
        lo: int = -P.SPEED_MAX,
        hi: int = P.SPEED_MAX,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self._slider = QSlider(Qt.Horizontal)
        self._slider.setRange(lo, hi)
        self._slider.setSingleStep(50)
        self._slider.setPageStep(500)
        self._slider.setMinimumWidth(110)
        if lo < 0 < hi:
            self._slider.setTickPosition(QSlider.TicksBelow)
            self._slider.setTickInterval(hi)  # a centre + ends marker

        self._box = QSpinBox()
        self._box.setRange(lo, hi)
        self._box.setSingleStep(50)
        self._box.setSuffix(" st/s")
        self._box.setAlignment(Qt.AlignRight)
        self._box.setFixedWidth(116)
        self._box.setMinimumHeight(40)

        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(8)
        lay.addWidget(self._slider, 1)
        lay.addWidget(self._box)

        # debounce user edits before emitting; then hold off telemetry writes
        # for a moment while the command round-trips (so it doesn't snap back)
        self._debounce = QTimer(self)
        self._debounce.setSingleShot(True)
        self._debounce.setInterval(_DEBOUNCE_MS)
        self._settle = QTimer(self)
        self._settle.setSingleShot(True)
        self._settle.setInterval(_SETTLE_MS)
        self._debounce.timeout.connect(self._commit)
        self._slider.valueChanged.connect(self._from_slider)
        self._box.valueChanged.connect(self._from_box)

    def _commit(self) -> None:
        self._settle.start()
        self.valueChanged.emit(self._box.value())

    def _from_slider(self, v: int) -> None:
        if v != self._box.value():
            self._box.blockSignals(True)
            self._box.setValue(v)
            self._box.blockSignals(False)
        self._debounce.start()

    def _from_box(self, v: int) -> None:
        if v != self._slider.value():
            self._slider.blockSignals(True)
            self._slider.setValue(v)
            self._slider.blockSignals(False)
        self._debounce.start()

    # ---- api ----------------------------------------------------------
    def value(self) -> int:
        return self._box.value()

    def setValue(self, v: int) -> None:
        v = P.clamp_speed(v)
        for w in (self._slider, self._box):
            w.blockSignals(True)
            w.setValue(v)
            w.blockSignals(False)

    def busy(self) -> bool:
        return (
            self._box.hasFocus()
            or self._slider.isSliderDown()
            or self._debounce.isActive()
            or self._settle.isActive()
        )

    def setReadOnly(self, ro: bool) -> None:
        self._slider.setEnabled(not ro)
        self._box.setReadOnly(ro)
        self._box.setButtonSymbols(
            QSpinBox.NoButtons if ro else QSpinBox.UpDownArrows
        )


class PumpRow(QFrame):
    """Live-tab pump: name, speed bar+box, run indicator, hold/free toggle, jog.

    The Hold toggle drives the driver's enable line. Switch it to **Free** to drop
    holding torque so the shaft can be hand-turned (loading a syringe at setup).
    """

    speedChanged = Signal(int, int)     # idx, steps/s
    enableToggled = Signal(int, bool)   # idx, holding torque on
    jogRequested = Signal(int, int)     # idx, steps/s
    jogStopped = Signal(int)            # idx

    def __init__(self, idx: int, name: str | None = None, parent=None) -> None:
        super().__init__(parent)
        self.idx = idx
        self.setObjectName("card")

        self._dot = QLabel(_DOT)
        self._dot.setFixedWidth(16)
        self._dot.setStyleSheet("color:#5b6472;")

        self._name = QLabel(name or P.PUMP_ROLES[idx])
        self._name.setStyleSheet("font-weight:600;")
        self._name.setMinimumWidth(78)

        self._speed = SpeedControl()

        self._hold = QPushButton("HOLD")
        self._hold.setObjectName("holdtoggle")
        self._hold.setCheckable(True)
        self._hold.setChecked(True)
        self._hold.setFixedWidth(96)
        self._hold.setMinimumHeight(44)
        self._hold.setToolTip(
            "HOLD = holding torque on.  FREE = torque off, hand-turn the syringe."
        )

        # Hold/Free is optimistic: the tap flips the button immediately and we
        # wait for telemetry to confirm. _want is the un-confirmed target (None
        # once telemetry agrees); until then apply_state() must not fight it.
        self._want: bool | None = None
        self._confirm = QTimer(self)
        self._confirm.setSingleShot(True)
        self._confirm.setInterval(_HOLD_CONFIRM_MS)
        self._confirm.timeout.connect(self._hold_unconfirmed)

        self._jog = QPushButton("Jog")
        self._jog.setCheckable(True)
        self._jog.setFixedWidth(72)
        self._jog.setMinimumHeight(40)
        self._jog.setToolTip("hold to run this pump at the set speed while idle")

        row = QHBoxLayout(self)
        row.setContentsMargins(12, 6, 12, 6)
        row.setSpacing(10)
        row.addWidget(self._dot)
        row.addWidget(self._name)
        row.addWidget(self._speed, 1)
        row.addWidget(self._hold)
        row.addWidget(self._jog)
        self.setMaximumHeight(60)

        self._speed.valueChanged.connect(
            lambda v: self.speedChanged.emit(self.idx, v)
        )
        self._hold.toggled.connect(self._on_hold)
        self._jog.toggled.connect(self._on_jog)
        self._paint_hold(ack="ok")  # seed the state/ack style properties

    def _on_hold(self, held: bool) -> None:
        # user tap: show it now, wait for telemetry to confirm
        self._want = held
        self._confirm.start()
        self._paint_hold(ack="pending")
        self.enableToggled.emit(self.idx, held)

    def _hold_unconfirmed(self) -> None:
        # telemetry never came back with our target — keep showing what the
        # operator asked for, flag that the firmware hasn't acknowledged, retry
        # the command once.
        if self._want is None:
            return
        self._paint_hold(ack="bad")
        self.enableToggled.emit(self.idx, self._want)

    def _paint_hold(self, ack: str = "ok") -> None:
        """Repaint the Hold/Free button: fill from the checked state, border
        from `ack` ('ok' | 'pending' | 'bad'), plus a text cue."""
        held = self._hold.isChecked()
        word = "HOLD" if held else "FREE"
        self._hold.setText(word + {"pending": " …", "bad": " ⚠"}.get(ack, ""))
        self._hold.setProperty("state", "hold" if held else "free")
        self._hold.setProperty("ack", ack)
        s = self._hold.style()
        s.unpolish(self._hold)
        s.polish(self._hold)

    def set_name(self, name: str) -> None:
        self._name.setText(name)

    def speed(self) -> int:
        return self._speed.value()

    def set_speed(self, v: int) -> None:
        self._speed.setValue(v)

    def _on_jog(self, on: bool) -> None:
        if on:
            self.jogRequested.emit(self.idx, self._speed.value())
        else:
            self.jogStopped.emit(self.idx)

    # ---- driven by telemetry ----------------------------------------
    def apply_state(self, st: P.PumpState, protocol_running: bool) -> None:
        self._dot.setStyleSheet("color:#2ecc71;" if st.running else "color:#5b6472;")

        # --- Hold/Free ------------------------------------------------
        if protocol_running:
            self._confirm.stop()   # the sequence owns the drivers now
            self._want = None
        if self._want is not None:
            if st.enabled == self._want:      # telemetry caught up — confirmed
                self._confirm.stop()
                self._want = None
                self._paint_hold(ack="ok")
            # else: still round-tripping — keep the operator's choice on screen
        elif self._hold.isChecked() != st.enabled:
            self._hold.blockSignals(True)
            self._hold.setChecked(st.enabled)
            self._hold.blockSignals(False)
            self._paint_hold(ack="ok")

        # while a sequence runs the row is a read-only mirror of telemetry
        self._speed.setReadOnly(protocol_running)
        self._hold.setEnabled(not protocol_running)
        self._jog.setEnabled(not protocol_running)
        if protocol_running:
            self._speed.setValue(st.speed)
            if self._jog.isChecked():
                self._jog.blockSignals(True)
                self._jog.setChecked(False)
                self._jog.blockSignals(False)
        elif not self._speed.busy() and self._speed.value() != st.speed:
            self._speed.setValue(st.speed)


class RunStatusBar(QFrame):
    """Compact 'Idle' / 'Phase 2 / 5 — Label' headline + progress bar."""

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setObjectName("card")
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(14, 10, 14, 10)
        lay.setSpacing(8)
        self._headline = QLabel("Idle")
        self._headline.setObjectName("phaseHeadline")
        self._headline.setWordWrap(True)
        self._bar = QProgressBar()
        self._bar.setMinimumHeight(24)
        self._bar.setVisible(False)
        lay.addWidget(self._headline)
        lay.addWidget(self._bar)

    def apply_telemetry(
        self, t: P.Telemetry, label: str = "", total_seconds: int = 0
    ) -> None:
        if t.phase < 0:
            self._headline.setText("Idle")
        else:
            head = t.phase_label
            if label:
                head += "  ·  " + label
            self._headline.setText(head)
        self._bar.setVisible(t.phase >= 0)
        if t.phase >= 0:
            total = max(1, total_seconds or t.remaining_s or 1)
            self._bar.setRange(0, total)
            self._bar.setValue(max(0, min(total, total - t.remaining_s)))
            self._bar.setFormat("{} s left".format(t.remaining_s))
