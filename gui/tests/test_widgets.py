"""PumpRow Hold/Free toggle — optimistic flip, telemetry confirmation, and the
'firmware never acknowledged' fallback; the Live-tab MicrostepSelector (same
pattern).  Headless (offscreen) Qt."""
import os

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

import pytest

from PySide6.QtCore import QEvent, QPointF, Qt
from PySide6.QtGui import QMouseEvent, QPixmap
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication

from peachpulp import protocol as P
from peachpulp.link import SimLink
from peachpulp.live import LivePanel
from peachpulp.sim import FakeFirmware
from peachpulp.widgets import MicrostepSelector, PumpRow, _BarSlider


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


def test_jog_tooltip_says_it_latches(row):
    tip = row._jog.toolTip()
    assert tip == "tap to start, tap again to stop (at the set speed, while idle)"
    assert "tap again to stop" in tip and "hold" not in tip.lower()


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


# --- MicrostepSelector: Live-tab "Microstepping" combo ----------------------

def _telem(ms=16, phase=-1, running=()):
    """Telemetry as the GUI sees it; ms=None = firmware too old to report it."""
    t = P.Telemetry(phase=phase, microsteps=ms)
    for i in running:
        t.pumps[i] = P.PumpState(speed=500, running=True)
    return t


@pytest.fixture
def sel(app):
    w = MicrostepSelector()
    w.sent = []        # type: ignore[attr-defined]
    w.msgs = []        # type: ignore[attr-defined]
    w.microstepRequested.connect(w.sent.append)
    w.message.connect(w.msgs.append)
    w.apply_telemetry(_telem(16))          # a connected, idle, current firmware
    return w


def _pick(w, n):
    w.combo.setCurrentIndex(w.combo.findData(n))


def _ack(w):
    return w.combo.property("ack")


def test_microstep_combo_items_and_data(sel):
    c = sel.combo
    assert c.count() == 9
    assert [c.itemData(i) for i in range(9)] == list(P.MICROSTEP_CHOICES)
    assert [c.itemText(i) for i in range(9)] == [
        "Full", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64", "1/128", "1/256",
    ]
    assert c.currentData() == P.DEFAULT_MICROSTEPS
    assert "Microstepping" in sel._label.text()


def test_microstep_combo_is_touch_sized(sel):
    assert sel.combo.minimumHeight() >= 44
    sh = sel.combo.itemData(0, Qt.SizeHintRole)
    assert sh.height() >= 44              # large popup rows


def test_microstep_disabled_until_first_telemetry(app):
    w = MicrostepSelector()
    assert not w.combo.isEnabled()
    assert w.combo.toolTip()


def test_microstep_follows_telemetry(sel):
    sel.apply_telemetry(_telem(64))
    assert sel.combo.currentData() == 64 and sel.sent == []   # no command echoed back
    assert _ack(sel) == "ok" and sel.combo.isEnabled()


def test_microstep_pick_sends_and_is_pending(sel):
    _pick(sel, 32)
    assert sel.sent == [32]
    assert sel._want == 32 and sel._confirm.isActive()
    assert _ack(sel) == "pending"
    assert sel.combo.currentData() == 32          # shown at once (optimistic)


def test_microstep_stale_telemetry_does_not_snap_back(sel):
    _pick(sel, 32)
    for _ in range(5):                    # firmware still reporting the old value
        sel.apply_telemetry(_telem(16))
    assert sel.combo.currentData() == 32
    assert sel._want == 32 and _ack(sel) == "pending"
    assert sel.msgs == []


def test_microstep_matching_telemetry_confirms(sel):
    _pick(sel, 32)
    sel.apply_telemetry(_telem(32))
    assert sel._want is None and not sel._confirm.isActive()
    assert _ack(sel) == "ok" and sel.combo.currentData() == 32
    assert sel.sent == [32] and sel.msgs == []


def test_microstep_timeout_reverts_and_reports(sel):
    _pick(sel, 32)
    sel._unconfirmed()                    # simulate the confirm timer firing
    assert sel.combo.currentData() == 16  # back to the telemetry value
    assert sel._want is None and _ack(sel) == "bad"
    assert len(sel.msgs) == 1 and "1/32" in sel.msgs[0] and "1/16" in sel.msgs[0]
    assert sel.sent == [32]               # reported, not retried

    _pick(sel, 8)                         # next pick starts clean
    assert _ack(sel) == "pending"
    sel.apply_telemetry(_telem(8))
    assert _ack(sel) == "ok"


def test_microstep_confirm_timer_really_fires(app):
    w = MicrostepSelector(confirm_ms=5)   # injectable interval, no time.sleep
    msgs = []
    w.message.connect(msgs.append)
    w.apply_telemetry(_telem(16))
    _pick(w, 4)
    QTest.qWait(80)
    assert w.combo.currentData() == 16 and _ack(w) == "bad"
    assert len(msgs) == 1


def test_microstep_default_confirm_timeout_is_1800ms(sel):
    assert sel._confirm.interval() == 1800


def test_microstep_busy_error_reply_reverts_and_reports(sel):
    _pick(sel, 32)
    sel.on_error("MICROSTEP busy")
    assert sel.combo.currentData() == 16
    assert sel._want is None and not sel._confirm.isActive() and _ack(sel) == "bad"
    assert len(sel.msgs) == 1 and "a program is running" in sel.msgs[0]
    assert "pumps are running" not in sel.msgs[0]


def test_microstep_usage_error_reply_reverts_and_reports(sel):
    _pick(sel, 32)
    sel.on_error("MICROSTEP 1|2|4|8|16|32|64|128|256")
    assert sel.combo.currentData() == 16 and _ack(sel) == "bad"
    assert "1|2|4|8|16|32|64|128|256" in sel.msgs[0]


def test_microstep_error_without_a_pick_in_flight_is_ignored(sel):
    sel.on_error("MICROSTEP busy")
    assert sel.msgs == [] and _ack(sel) == "ok"


def test_microstep_disabled_while_a_program_runs(sel):
    sel.apply_telemetry(_telem(16, phase=1))
    assert not sel.combo.isEnabled()
    assert sel.combo.toolTip() == "Stop the program first"
    sel.apply_telemetry(_telem(16))
    assert sel.combo.isEnabled()


def test_microstep_stays_enabled_while_a_pump_jogs(sel):
    sel.apply_telemetry(_telem(16, running=[3]))    # manual run / JOG, no program
    assert sel.combo.isEnabled()
    assert "while a pump jogs" in sel.combo.toolTip()
    assert "Stop" not in sel.combo.toolTip()
    sel.apply_telemetry(_telem(16, phase=2, running=[3]))   # a program does block it
    assert not sel.combo.isEnabled()
    sel.apply_telemetry(_telem(16, running=[0, 1, 2, 3]))
    assert sel.combo.isEnabled()


def test_microstep_pick_while_a_pump_jogs_is_sent_and_confirmed(sel):
    sel.apply_telemetry(_telem(16, running=[3]))
    _pick(sel, 32)
    assert sel.sent == [32] and _ack(sel) == "pending"
    sel.apply_telemetry(_telem(32, running=[3]))    # resumed at the new resolution
    assert sel._want is None and _ack(sel) == "ok" and sel.combo.isEnabled()
    assert sel.msgs == []


def test_microstep_disabled_with_tooltip_when_firmware_too_old(sel):
    sel.apply_telemetry(_telem(None))
    assert not sel.combo.isEnabled()
    assert sel.combo.toolTip() == "Firmware too old: reflash to enable"
    sel.apply_telemetry(_telem(16))       # reflashed
    assert sel.combo.isEnabled() and "too old" not in sel.combo.toolTip()


def _why(w):
    """(text, shown) of the visible 'why is this greyed out' caption."""
    return w._why.text(), not w._why.isHidden()


def test_microstep_caption_while_waiting_for_first_telemetry(app):
    w = MicrostepSelector()
    assert _why(w) == ("waiting for Octopus…", True)
    assert w.combo.toolTip()                      # tooltip kept


def test_microstep_caption_when_firmware_too_old(sel):
    sel.apply_telemetry(_telem(None))
    assert _why(sel) == ("firmware too old — reflash", True)
    assert sel.combo.toolTip() == "Firmware too old: reflash to enable"


def test_microstep_caption_while_a_program_runs_not_a_pump(sel):
    sel.apply_telemetry(_telem(16, phase=1))
    assert _why(sel) == ("program running", True)
    assert sel.combo.toolTip() == "Stop the program first"
    sel.apply_telemetry(_telem(16))
    assert _why(sel)[1] is False
    sel.apply_telemetry(_telem(16, running=[3]))      # a jogging pump is fine
    assert _why(sel) == ("", False)
    assert sel.combo.isEnabled()


def test_microstep_caption_hidden_when_enabled(sel):
    assert sel.combo.isEnabled()
    assert _why(sel) == ("", False)
    sel.apply_telemetry(_telem(None))             # shown again when it greys out
    assert _why(sel)[1] is True
    sel.apply_telemetry(_telem(16))               # reflashed
    assert _why(sel) == ("", False)


def test_microstep_caption_is_short_and_unwrapped(sel):
    assert not sel._why.wordWrap()
    for t, text in ((_telem(None), "firmware too old — reflash"),
                    (_telem(16, phase=1), "program running")):
        sel.apply_telemetry(t)
        assert len(sel._why.text()) <= 28 and sel._why.text() == text


def _effect(w):
    """(text, shown) of the visible 'what does this setting do to speed' caption."""
    return w._effect.text(), not w._effect.isHidden()


def test_microstep_speed_effect_caption_for_the_current_value(sel):
    assert _effect(sel) == ("speed x1", True)         # 1/16: unchanged
    sel.apply_telemetry(_telem(32))
    assert _effect(sel) == ("speed x0.5 vs 1/16", True)
    sel.apply_telemetry(_telem(8))
    assert _effect(sel) == ("speed x2 vs 1/16", True)
    sel.apply_telemetry(_telem(1))
    assert _effect(sel) == ("speed x16 vs 1/16", True)
    sel.apply_telemetry(_telem(256))
    assert _effect(sel) == ("speed x0.0625 vs 1/16", True)


def test_microstep_speed_effect_caption_follows_a_pending_pick_and_revert(sel):
    _pick(sel, 32)                                    # shown at once, before telemetry
    assert _effect(sel) == ("speed x0.5 vs 1/16", True)
    _pick(sel, 8)
    assert _effect(sel) == ("speed x2 vs 1/16", True)
    sel._unconfirmed()                                # reverts to the telemetry value
    assert _effect(sel) == ("speed x1", True)
    _pick(sel, 4)
    sel.on_error("MICROSTEP busy")                    # refused: back to 1/16
    assert _effect(sel) == ("speed x1", True)


def test_microstep_speed_effect_caption_hidden_while_disabled(sel):
    for t in (_telem(None), _telem(32, phase=1)):
        sel.apply_telemetry(t)
        assert not sel.combo.isEnabled()
        assert _effect(sel)[1] is False
        assert _why(sel)[1] is True                   # the why-caption takes over
    sel.apply_telemetry(_telem(32))
    assert _effect(sel) == ("speed x0.5 vs 1/16", True) and _why(sel)[1] is False


def test_microstep_speed_effect_caption_hidden_before_first_telemetry(app):
    w = MicrostepSelector()
    assert _effect(w)[1] is False and _why(w)[1] is True


def test_microstep_speed_effect_caption_is_muted_and_unwrapped(sel):
    assert not sel._effect.wordWrap()
    assert sel._effect.objectName() == "microstepEffect"
    assert sel._effect.styleSheet() == sel._why.styleSheet()
    for n in P.MICROSTEP_CHOICES:
        sel.apply_telemetry(_telem(n))
        assert sel._effect.text().startswith("speed x")
        assert len(sel._effect.text()) <= 24


def test_microstep_hint_describes_raw_microsteps_per_second(sel):
    tip = sel.combo.toolTip()
    assert "microsteps/s" in tip and "finer setting is slower" in tip
    assert "keep their meaning" not in tip and "steps/s at 1/16" not in tip


_NO_MS_MSG = (
    "firmware has no MICROSTEP support — "
    "flash breaking/octopus-stm32-fw (fw effb898 or newer)"
)


def test_microstep_old_firmware_message_emitted_once(sel):
    assert sel.msgs == []                         # current firmware: silent
    for _ in range(6):                            # ~5 Hz telemetry without ms
        sel.apply_telemetry(_telem(None))
    assert sel.msgs == [_NO_MS_MSG]


def test_microstep_old_firmware_message_again_only_after_ms_was_seen(sel):
    for _ in range(3):
        sel.apply_telemetry(_telem(None))
    assert sel.msgs == [_NO_MS_MSG]
    sel.apply_telemetry(_telem(16))               # ms seen (e.g. reflashed)
    assert sel.msgs == [_NO_MS_MSG]
    for _ in range(3):                            # ...and gone again
        sel.apply_telemetry(_telem(None))
    assert sel.msgs == [_NO_MS_MSG, _NO_MS_MSG]


def test_microstep_old_firmware_message_on_first_telemetry(app):
    w = MicrostepSelector()                       # never saw ms at all
    msgs = []
    w.message.connect(msgs.append)
    w.apply_telemetry(_telem(None))
    w.apply_telemetry(_telem(None))
    assert msgs == [_NO_MS_MSG]


def test_microstep_not_stored_in_workspaces():
    from peachpulp.workspace import default_workspace
    assert "micro" not in " ".join(default_workspace().to_dict()).lower()


# --- wired through LivePanel + the simulator ---------------------------------

def _fw_roundtrip(panel, fw):
    kind, t = P.parse_line(fw.telemetry_line())
    assert kind == "telemetry"
    panel.apply_telemetry(t)


def test_live_panel_header_row_has_the_selector(app):
    panel = LivePanel(P.ACTIVE_PUMPS)
    assert panel.microstep.combo.count() == 9
    assert panel.microstep.parentWidget() is panel


def test_live_panel_pick_round_trips_through_the_sim(app):
    fw, panel = FakeFirmware(), LivePanel(P.ACTIVE_PUMPS)
    panel.microstepRequested.connect(lambda n: fw.feed(P.cmd_microstep(n)))
    _fw_roundtrip(panel, fw)
    assert panel.microstep.combo.isEnabled()

    _pick(panel.microstep, 128)
    assert panel.microstep._want == 128
    _fw_roundtrip(panel, fw)              # firmware applied it, telemetry echoes
    assert panel.microstep._want is None and _ack(panel.microstep) == "ok"
    assert panel.microstep.combo.currentData() == 128


def test_live_panel_header_row_fits_800px_with_the_longest_caption(app):
    """The selector + its widest speed caption must not push the Live tab past an
    800 px touchscreen (checked with the real theme, which is what the Pi runs)."""
    from peachpulp import theme

    style, pal, qss = app.style().objectName(), app.palette(), app.styleSheet()
    theme.apply_theme(app)
    try:
        panel = LivePanel(P.ACTIVE_PUMPS)
        widths = {}
        for n in P.MICROSTEP_CHOICES:
            panel.apply_telemetry(_telem(n))
            assert not panel.microstep._effect.isHidden()
            widths[n] = panel.microstep._effect.sizeHint().width()
        assert max(widths, key=widths.get) == 256        # "speed x0.0625 vs 1/16"
        panel.apply_telemetry(_telem(256))
        assert panel.microstep._effect.text() == "speed x0.0625 vs 1/16"
        panel.resize(800, 480)
        panel.show()
        app.processEvents()
        assert panel.microstep.minimumSizeHint().width() < 500
        assert panel.minimumSizeHint().width() <= 800
        assert panel.microstep.geometry().right() <= 800   # not clipped off-screen
        panel.hide()
    finally:
        app.setStyleSheet(qss)
        app.setPalette(pal)
        app.setStyle(style)


def test_live_panel_microstep_stays_usable_while_jogging_but_not_in_a_program(app):
    fw, panel = FakeFirmware(), LivePanel(P.ACTIVE_PUMPS)
    panel.microstepRequested.connect(lambda n: fw.feed(P.cmd_microstep(n)))
    fw.feed("JOG 1 600")
    _fw_roundtrip(panel, fw)
    assert panel.microstep.combo.isEnabled()          # a jog does not block it
    _pick(panel.microstep, 32)
    assert fw.drain() == []                           # firmware accepted silently
    _fw_roundtrip(panel, fw)
    assert panel.microstep._want is None and _ack(panel.microstep) == "ok"
    assert panel.microstep.combo.currentData() == 32

    fw.feed("JOG 1 0")
    fw.feed(P.cmd_phasetime(0, 3600))
    fw.feed("RUN")
    _fw_roundtrip(panel, fw)
    assert not panel.microstep.combo.isEnabled()      # a running program does
    assert panel.microstep._why.text() == "program running"
    fw.feed("STOP")
    _fw_roundtrip(panel, fw)
    assert panel.microstep.combo.isEnabled()


def test_live_panel_legacy_firmware_disables_the_selector(app):
    panel = LivePanel(P.ACTIVE_PUMPS)
    old = FakeFirmware(legacy_firmware=True)
    _fw_roundtrip(panel, old)
    assert not panel.microstep.combo.isEnabled()
    assert "too old" in panel.microstep.combo.toolTip()


def test_main_window_sends_and_routes_the_busy_error(app, tmp_path, monkeypatch):
    monkeypatch.setenv("PEACHPULP_HOME", str(tmp_path))
    from peachpulp.app import MainWindow

    link = SimLink()
    win = MainWindow(link)
    sel = win._live.microstep
    link._on_telem()
    assert sel.combo.isEnabled()

    _pick(sel, 32)                        # -> "MICROSTEP 32" through the link
    assert link.fw.microsteps == 32
    link._on_telem()
    assert sel._want is None and _ack(sel) == "ok"

    # a jog is no obstacle: the firmware takes the new value, the log stays quiet
    link.fw.feed("JOG 0 500")
    _pick(sel, 8)
    assert link.fw.microsteps == 8
    link._on_telem()
    assert sel.combo.currentData() == 8 and _ack(sel) == "ok"
    assert "MICROSTEP busy" not in win._log.toPlainText()

    # a program starts just before the GUI has seen it: the firmware says busy
    link.fw.feed("JOG 0 0")
    link.fw.feed("RUN")
    _pick(sel, 4)
    assert link.fw.microsteps == 8
    assert sel.combo.currentData() == 8 and _ack(sel) == "bad"
    log = win._log.toPlainText()
    assert "MICROSTEP busy" in log and "staying at 1/8" in log
    assert "a program is running" in log
