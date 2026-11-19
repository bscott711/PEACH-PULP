"""Shared touch widgets: speed control, pump row, microstep selector, run-status bar."""
from __future__ import annotations

from PySide6.QtCore import QPointF, QRectF, QSize, Qt, QTimer, Signal
from PySide6.QtGui import QColor, QPainter
from PySide6.QtWidgets import (
    QComboBox,
    QFrame,
    QHBoxLayout,
    QLabel,
    QListView,
    QProgressBar,
    QPushButton,
    QSizePolicy,
    QSlider,
    QSpinBox,
    QStyle,
    QStyleOptionSlider,
    QVBoxLayout,
    QWidget,
)

from . import protocol as P

_DEBOUNCE_MS = 250
_SETTLE_MS = 700
_HOLD_CONFIRM_MS = 1800  # wait this long for telemetry to confirm a Hold/Free tap
_MICROSTEP_CONFIRM_MS = 1800  # ...and for telemetry "ms" to confirm a microstep pick
_TOUCH_H = 44  # minimum touch-target height (combo box and its popup rows)
_DOT = "●"   # ●
_PLAY = "▶"  # ▶


class _BarSlider(QSlider):
    """Horizontal slider tuned for the touchscreen.

    * Tap anywhere on the track to jump there, then keep dragging — no need to
      land on the handle first (stock ``QSlider`` only pages toward a groove tap).
    * The fill is anchored at **zero**: run positive and a bar grows to the right
      in one colour, negative and it grows left in another. There is always a
      bar, and its length reads as distance from zero.
    """

    _TRACK = "#2c3440"
    _POS = "#4aa3ff"
    _NEG = "#f39c12"
    _HANDLE = "#e7ebf2"
    _HANDLE_OFF = "#6c7688"

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(Qt.Horizontal, parent)
        self.setMinimumHeight(30)

    # ---- geometry helpers --------------------------------------------
    def _groove_handle(self):
        opt = QStyleOptionSlider()
        self.initStyleOption(opt)
        st = self.style()
        groove = st.subControlRect(
            QStyle.CC_Slider, opt, QStyle.SC_SliderGroove, self
        )
        handle = st.subControlRect(
            QStyle.CC_Slider, opt, QStyle.SC_SliderHandle, self
        )
        return groove, handle

    def _x_for(self, value: int, groove, handle) -> float:
        span = groove.width() - handle.width()
        pos = QStyle.sliderPositionFromValue(
            self.minimum(), self.maximum(), value, span
        )
        return groove.x() + handle.width() / 2 + pos

    def _value_at(self, x: float, groove, handle) -> int:
        span = groove.width() - handle.width()
        pos = round(x - groove.x() - handle.width() / 2)
        return QStyle.sliderValueFromPosition(
            self.minimum(), self.maximum(), pos, span
        )

    # ---- touch: tap-to-seek, then drag -----------------------------
    def _seek(self, x: float) -> None:
        groove, handle = self._groove_handle()
        self.setValue(self._value_at(x, groove, handle))

    def mousePressEvent(self, ev) -> None:  # noqa: N802 (Qt override)
        if self.isEnabled() and ev.button() == Qt.LeftButton:
            self.setSliderDown(True)
            self._seek(ev.position().x())
            ev.accept()
            return
        super().mousePressEvent(ev)

    def mouseMoveEvent(self, ev) -> None:  # noqa: N802 (Qt override)
        if self.isSliderDown():
            self._seek(ev.position().x())
            ev.accept()
            return
        super().mouseMoveEvent(ev)

    def mouseReleaseEvent(self, ev) -> None:  # noqa: N802 (Qt override)
        if self.isSliderDown() and ev.button() == Qt.LeftButton:
            self._seek(ev.position().x())
            self.setSliderDown(False)
            ev.accept()
            return
        super().mouseReleaseEvent(ev)

    # ---- centre-anchored two-colour fill --------------------------
    def paintEvent(self, ev) -> None:  # noqa: N802 (Qt override)
        groove, handle = self._groove_handle()
        cy = self.rect().center().y()
        h = 6.0
        left = groove.x() + handle.width() / 2
        right = groove.x() + groove.width() - handle.width() / 2

        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        p.setPen(Qt.NoPen)

        p.setBrush(QColor(self._TRACK))
        p.drawRoundedRect(QRectF(left, cy - h / 2, right - left, h), h / 2, h / 2)

        v = self.value()
        zero = max(self.minimum(), min(self.maximum(), 0))
        zx = self._x_for(zero, groove, handle)
        hx = self._x_for(v, groove, handle)
        a, b = sorted((zx, hx))
        if b - a > 0.5:
            p.setBrush(QColor(self._POS if v >= 0 else self._NEG))
            p.drawRoundedRect(QRectF(a, cy - h / 2, b - a, h), h / 2, h / 2)

        r = 10.0
        p.setBrush(QColor(self._HANDLE if self.isEnabled() else self._HANDLE_OFF))
        p.drawEllipse(QPointF(hx, cy), r, r)


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
        self._slider = _BarSlider()
        self._slider.setRange(lo, hi)
        self._slider.setSingleStep(50)
        self._slider.setPageStep(500)
        self._slider.setMinimumWidth(90)

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
        self._name.setMinimumWidth(64)

        self._speed = SpeedControl()

        self._hold = QPushButton("HOLD")
        self._hold.setObjectName("holdtoggle")
        self._hold.setCheckable(True)
        self._hold.setChecked(True)
        self._hold.setFixedWidth(84)
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
        self._jog.setFixedWidth(60)
        self._jog.setMinimumHeight(40)
        self._jog.setToolTip("hold to run this pump at the set speed while idle")

        row = QHBoxLayout(self)
        row.setContentsMargins(10, 6, 10, 6)
        row.setSpacing(8)
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
        if not st.comm_ok:
            self._dot.setStyleSheet("color:#e74c3c;")   # driver not answering on UART
            self._dot.setToolTip(
                "no UART read-back — this driver is running blind on defaults"
            )
        else:
            self._dot.setStyleSheet(
                "color:#2ecc71;" if st.running else "color:#5b6472;"
            )
            self._dot.setToolTip("")

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


def microstep_label(n: int) -> str:
    return "Full" if n == 1 else "1/{}".format(n)


class MicrostepSelector(QWidget):
    """Live-tab "Microstepping" picker. One setting for all drivers (``MICROSTEP <n>``).

    Speed numbers keep their meaning at every setting (steps/s at 1/16), so this
    changes smoothness/torque, not how fast a given number runs.

    The firmware is the source of truth and the combo follows telemetry ``ms``.
    A pick is optimistic, like Hold/Free: it shows at once (dashed border) until
    telemetry echoes it. If that doesn't happen within ``confirm_ms``, or the
    firmware answers ``!ERR MICROSTEP …``, the combo reverts to the telemetry
    value (red border) and ``message`` says why.

    Disabled while a program or any pump runs (the firmware rejects it) and on
    firmware that doesn't report ``ms`` yet. A short caption beside the combo says
    which (a touchscreen never shows tooltips); it is hidden while enabled.
    """

    microstepRequested = Signal(int)   # microsteps, one of P.MICROSTEP_CHOICES
    message = Signal(str)              # a failure the operator should see (-> log)

    _HINT = (
        "Microstepping for all pumps. Speed numbers keep their meaning "
        "(steps/s at 1/16)."
    )
    _NO_MS_MSG = (
        "firmware has no MICROSTEP support — "
        "flash breaking/octopus-stm32-fw (fw effb898 or newer)"
    )

    def __init__(self, confirm_ms: int = _MICROSTEP_CONFIRM_MS, parent=None) -> None:
        super().__init__(parent)
        self._label = QLabel("Microstepping")
        self._label.setObjectName("sectionHeader")

        self.combo = QComboBox()
        self.combo.setObjectName("microstep")
        self.combo.setView(QListView())          # lets QSS size the popup rows
        self.combo.setMinimumHeight(_TOUCH_H)
        self.combo.setMinimumWidth(120)
        for n in P.MICROSTEP_CHOICES:
            self.combo.addItem(microstep_label(n), n)
            self.combo.setItemData(
                self.combo.count() - 1, QSize(0, _TOUCH_H), Qt.SizeHintRole
            )
        self.combo.setCurrentIndex(self.combo.findData(P.DEFAULT_MICROSTEPS))

        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(8)
        lay.addWidget(self._label)
        lay.addWidget(self.combo)

        # Why the combo is greyed out, as visible text (muted; short, no wrapping).
        self._why = QLabel()
        self._why.setObjectName("microstepWhy")
        self._why.setStyleSheet("color: palette(placeholder-text); font-size: 12px;")
        self._why.setWordWrap(False)
        self._why.setVisible(False)
        lay.addWidget(self._why)

        # _current = last value telemetry reported (None until/unless it does);
        # _want = the un-confirmed pick (None once telemetry agrees).
        self._current: int | None = None
        self._want: int | None = None
        self._warned_no_ms = False   # one log line per stretch of ms-less telemetry
        self._confirm = QTimer(self)
        self._confirm.setSingleShot(True)
        self._confirm.setInterval(confirm_ms)
        self._confirm.timeout.connect(self._unconfirmed)

        self.combo.currentIndexChanged.connect(self._on_pick)
        self._paint("ok")
        self._set_enabled(False, "Waiting for the Octopus…", "waiting for Octopus…")

    # ---- user pick -----------------------------------------------------
    def _on_pick(self, index: int) -> None:
        n = self.combo.itemData(index)
        if n is None:
            return
        self._want = n
        self._confirm.start()
        self._paint("pending")
        self.microstepRequested.emit(n)

    def _unconfirmed(self) -> None:
        # telemetry never echoed the pick (timer fired): revert and say so
        if self._want is None:
            return
        self._fail(
            "microstepping {} not confirmed by the Octopus".format(
                microstep_label(self._want)
            )
        )

    def on_error(self, text: str) -> None:
        """The firmware answered ``!ERR MICROSTEP …`` (payload as parse_line gives it)."""
        if self._want is None:
            return  # nothing of ours in flight
        why = "pumps are running" if text.rstrip().endswith("busy") else text
        self._fail("microstepping change rejected ({})".format(why))

    def _fail(self, what: str) -> None:
        self._confirm.stop()
        self._want = None
        self._show(self._current if self._current is not None else P.DEFAULT_MICROSTEPS)
        self._paint("bad")
        self.message.emit("{} — staying at {}".format(
            what, microstep_label(self.combo.currentData())
        ))

    # ---- driven by telemetry --------------------------------------------
    def apply_telemetry(self, t: P.Telemetry) -> None:
        ms = t.microsteps
        if ms is None:  # firmware predates MICROSTEP: nothing to follow or send to
            self._current = None
            self._confirm.stop()
            self._want = None
            self._paint("ok")
            self._set_enabled(
                False, "Firmware too old: reflash to enable", "firmware too old — reflash"
            )
            if not self._warned_no_ms:           # telemetry arrives at ~5 Hz: say it once
                self._warned_no_ms = True
                self.message.emit(self._NO_MS_MSG)
            return
        self._warned_no_ms = False
        self._current = ms
        if self._want is not None:
            if ms == self._want:                 # telemetry caught up — confirmed
                self._confirm.stop()
                self._want = None
                self._show(ms)
                self._paint("ok")
            # else: still round-tripping — keep the operator's pick on screen
        elif self.combo.currentData() != ms:     # follow the firmware
            self._show(ms)
            self._paint("ok")

        busy = t.running or any(p.running for p in t.pumps)
        if busy:
            self._set_enabled(False, "Stop all pumps first", "stop pumps first")
        else:
            self._set_enabled(True, self._HINT)

    # ---- helpers ---------------------------------------------------------
    def _show(self, n: int) -> None:
        i = self.combo.findData(n)
        if i >= 0 and i != self.combo.currentIndex():
            self.combo.blockSignals(True)
            self.combo.setCurrentIndex(i)
            self.combo.blockSignals(False)

    def _set_enabled(self, on: bool, tip: str, why: str = "") -> None:
        self.combo.setEnabled(on)
        self.combo.setToolTip(tip)
        self._label.setToolTip(tip)
        self._why.setText("" if on else why)
        self._why.setToolTip(tip)
        self._why.setVisible(not on)

    def _paint(self, ack: str) -> None:
        """ack = 'ok' | 'pending' | 'bad' → border (see theme.py) + a text cue."""
        self._label.setText(
            "Microstepping" + {"pending": " …", "bad": " ⚠"}.get(ack, "")
        )
        self.combo.setProperty("ack", ack)
        s = self.combo.style()
        s.unpolish(self.combo)
        s.polish(self.combo)


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
