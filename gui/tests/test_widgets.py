"""PumpRow Hold/Free toggle — optimistic flip, telemetry confirmation, and the
'firmware never acknowledged' fallback.  Headless (offscreen) Qt."""
import os

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

import pytest

from PySide6.QtCore import QEvent, QPointF, Qt
from PySide6.QtGui import QMouseEvent, QPixmap
from PySide6.QtWidgets import QApplication

from peachpulp import protocol as P
from peachpulp.widgets import PumpRow, _BarSlider


@pytest.fixture(scope="module")
def app():
    return QApplication.instance() or QApplication([])


@pytest.fixture
def row(app):
    r = PumpRow(2)
    seen: list = []
    r.enableToggled.connect(lambda i, on: seen.append((i, on)))
    r.seen = seen  # type: ignore[attr-defined]
    return r


def _state(row):
    return row._hold.property("state"), row._hold.property("ack")


def test_starts_held_and_styled(row):
    assert row._hold.isChecked()
    assert _state(row) == ("hold", "ok")


def test_tap_is_optimistic_and_pending(row):
    row._hold.click()  # Hold -> Free
    assert not row._hold.isChecked()
    assert row._want is False
    assert row.seen == [(2, False)]
    assert _state(row) == ("free", "pending")
    assert row._confirm.isActive()


def test_stale_telemetry_does_not_snap_back_while_pending(row):
    row._hold.click()  # -> Free, pending
    for _ in range(5):  # firmware still reporting the old state
        row.apply_state(P.PumpState(enabled=True), protocol_running=False)
    assert not row._hold.isChecked()      # operator's choice stays on screen
    assert row._want is False
    assert _state(row) == ("free", "pending")


def test_matching_telemetry_confirms(row):
    row._hold.click()  # -> Free, pending
    row.apply_state(P.PumpState(enabled=False), protocol_running=False)
    assert row._want is None
    assert not row._confirm.isActive()
    assert _state(row) == ("free", "ok")


def test_timeout_flags_bad_and_retries(row):
    row._hold.click()  # -> Free, pending
    row.seen.clear()
    row._hold_unconfirmed()               # simulate the confirm timer firing
    assert _state(row) == ("free", "bad")
    assert not row._hold.isChecked()      # still showing what was asked for
    assert row.seen == [(2, False)]       # command retried once

    row.apply_state(P.PumpState(enabled=False), protocol_running=False)  # late ack
    assert _state(row) == ("free", "ok")


def test_blind_driver_marks_the_dot(row):
    row.apply_state(P.PumpState(comm_ok=False), protocol_running=False)
    assert "blind" in row._dot.toolTip()
    row.apply_state(P.PumpState(comm_ok=True), protocol_running=False)
    assert row._dot.toolTip() == ""


def test_running_sequence_clears_pending_and_mirrors(row):
    row._hold.click()  # -> Free, pending
    row.apply_state(P.PumpState(enabled=True, running=True), protocol_running=True)
    assert row._want is None
    assert not row._confirm.isActive()
    assert row._hold.isChecked()          # sequence re-energises every driver
    assert not row._hold.isEnabled()


# --- _BarSlider: tap-to-seek + centre-anchored fill --------------------

@pytest.fixture
def bar(app):
    s = _BarSlider()
    s.setRange(-P.SPEED_MAX, P.SPEED_MAX)
    s.resize(200, 40)
    s.show()
    app.processEvents()
    return s


def _press(s, x):
    ev = QMouseEvent(
        QEvent.MouseButtonPress, QPointF(x, 20),
        Qt.LeftButton, Qt.LeftButton, Qt.NoModifier,
    )
    s.mousePressEvent(ev)


def _release(s, x):
    ev = QMouseEvent(
        QEvent.MouseButtonRelease, QPointF(x, 20),
        Qt.LeftButton, Qt.LeftButton, Qt.NoModifier,
    )
    s.mouseReleaseEvent(ev)


def test_tap_on_track_jumps_there_and_starts_a_drag(bar):
    g, h = bar._groove_handle()
    _press(bar, g.x() + g.width() - h.width() / 2 - 1)   # far right
    assert bar.value() > P.SPEED_MAX * 0.9
    assert bar.isSliderDown()                            # drag continues
    _release(bar, g.x() + g.width())
    assert not bar.isSliderDown()


def test_drag_follows_the_finger_without_grabbing_the_handle(bar):
    g, h = bar._groove_handle()
    _press(bar, bar.width() / 2)                         # near centre -> ~0
    mid = bar.value()
    move = QMouseEvent(
        QEvent.MouseMove, QPointF(g.x() + h.width() / 2 + 1, 20),
        Qt.NoButton, Qt.LeftButton, Qt.NoModifier,
    )
    bar.mouseMoveEvent(move)
    assert bar.value() < mid                             # tracked left to min-ish
    _release(bar, g.x())


def test_fill_paints_at_the_extremes(bar):
    for v in (-P.SPEED_MAX, 0, 1234):
        bar.setValue(v)
        bar.render(QPixmap(bar.size()))                  # no crash, any sign
