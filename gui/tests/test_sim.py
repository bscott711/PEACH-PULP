"""FakeFirmware behaviour — mirrors the firmware's observable protocol."""
import time

from peachpulp import protocol as P
from peachpulp.sim import FakeFirmware


def _telem(fw):
    kind, t = P.parse_line(fw.telemetry_line())
    assert kind == "telemetry"
    return t


def test_ping():
    fw = FakeFirmware()
    fw.feed("PING")
    assert fw.drain() == ["PONG"]


def test_idle_then_run_advances_phases_and_stops():
    fw = FakeFirmware()
    for p in range(P.NUM_PHASES):
        fw.feed(P.cmd_phasetime(p, 1))
    fw.feed("SPEED 0 2000")
    fw.feed("SPEED 2 1000")  # sheath

    assert _telem(fw).phase == -1
    fw.feed("RUN")
    assert "!EVENT phase 0" in fw.drain()
    assert _telem(fw).phase == 0

    events = []
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        fw.tick()
        events += fw.drain()
        if _telem(fw).phase == -1 and "!EVENT done" in events:
            break
        time.sleep(0.05)

    assert events.count("!EVENT phase 1") == 1
    assert events.count("!EVENT phase 2") == 1
    assert events.count("!EVENT phase 3") == 1
    assert "!EVENT done" in events
    assert _telem(fw).phase == -1


def test_run_marks_correct_pumps_per_phase():
    fw = FakeFirmware()
    for i in range(P.ACTIVE_PUMPS):
        fw.feed("SPEED {} 1000".format(i))
    for p in range(P.NUM_PHASES):
        fw.feed(P.cmd_phasetime(p, 3600))
    fw.feed("RUN")

    running = {i for i, p in enumerate(_telem(fw).pumps) if p.running}
    assert running == {0, 1, 2}  # Sample, Dye, Sheath

    fw.feed("SKIP")
    fw.tick()
    running = {i for i, p in enumerate(_telem(fw).pumps) if p.running}
    assert running == {2, 3}  # Sheath, Wash


def test_stop_halts_the_sequence():
    fw = FakeFirmware()
    fw.feed(P.cmd_phasetime(0, 3600))
    fw.feed("RUN")
    assert _telem(fw).phase == 0
    fw.feed("STOP")
    t = _telem(fw)
    assert t.phase == -1
    assert not any(p.running for p in t.pumps)
    assert all(p.enabled for p in t.pumps)  # drivers stay energized
    assert all(p.comm_ok for p in t.pumps)  # sim drivers always answer
    fw.feed("RUN")  # recoverable
    assert _telem(fw).phase == 0


def test_jog_only_when_idle():
    fw = FakeFirmware()
    fw.feed("JOG 4 800")
    assert _telem(fw).pumps[4].running is True

    fw.feed("STOP")
    fw.feed(P.cmd_phasetime(0, 3600))
    fw.feed("RUN")
    fw.feed("JOG 5 800")  # ignored while running
    assert _telem(fw).pumps[5].running is False


def test_enable_off_clears_manual_run():
    fw = FakeFirmware()
    fw.feed("JOG 3 500")
    assert _telem(fw).pumps[3].running is True
    fw.feed("ENABLE 3 0")
    t = _telem(fw)
    assert t.pumps[3].enabled is False
    assert t.pumps[3].running is False


def test_bad_command():
    fw = FakeFirmware()
    fw.feed("FLOOB 1 2")
    assert any("!ERR" in line for line in fw.drain())


def test_upload_program_then_run():
    fw = FakeFirmware()
    for line in P.cmd_program(
        [(1, {0: 1500}), (1, {2: 900, 3: 900})]
    ):
        fw.feed(line)
    assert any(l == "!EVENT prog 2" for l in fw.drain())
    assert _telem(fw).nphases == 2

    fw.feed("RUN")
    assert "!EVENT phase 0" in fw.drain()
    running = {i for i, p in enumerate(_telem(fw).pumps) if p.running}
    assert running == {0}
    assert _telem(fw).pumps[0].speed == 1500

    fw.feed("SKIP")
    fw.tick()
    running = {i for i, p in enumerate(_telem(fw).pumps) if p.running}
    assert running == {2, 3}

    events = []
    deadline = time.monotonic() + 4
    while time.monotonic() < deadline:
        fw.tick()
        events += fw.drain()
        if "!EVENT done" in events:
            break
        time.sleep(0.05)
    assert "!EVENT done" in events
    assert _telem(fw).phase == -1


def test_empty_program_commit_is_rejected():
    fw = FakeFirmware()
    fw.feed("PROGCLEAR")
    fw.feed("PROGCOMMIT")
    assert any("!ERR" in l for l in fw.drain())
    # seed program still intact
    assert _telem(fw).nphases == P.NUM_PHASES


# --- MICROSTEP -------------------------------------------------------------
_USAGE = "!ERR MICROSTEP 1|2|4|8|16|32|64|128|256"


def test_microstep_default_is_16_and_in_telemetry():
    fw = FakeFirmware()
    assert _telem(fw).microsteps == 16


def test_microstep_ok_is_silent_and_shows_in_telemetry():
    fw = FakeFirmware()
    for n in P.MICROSTEP_CHOICES:
        fw.feed(P.cmd_microstep(n))
        assert fw.drain() == []          # success: no reply, like every other verb
        assert _telem(fw).microsteps == n


def test_microstep_is_case_insensitive():
    fw = FakeFirmware()
    fw.feed("microstep 32")
    assert fw.drain() == [] and _telem(fw).microsteps == 32
    fw.feed("MicroStep 4")
    assert fw.drain() == [] and _telem(fw).microsteps == 4


def test_microstep_bad_n_is_rejected_and_keeps_the_old_value():
    fw = FakeFirmware()
    fw.feed("MICROSTEP 32")
    for line in ("MICROSTEP 3", "MICROSTEP 0", "MICROSTEP -16", "MICROSTEP 512",
                 "MICROSTEP", "MICROSTEP abc", "MICROSTEP 16.5"):
        fw.drain()
        fw.feed(line)
        assert fw.drain() == [_USAGE], line
        assert _telem(fw).microsteps == 32, line


def test_microstep_busy_while_a_program_runs():
    fw = FakeFirmware()
    fw.feed(P.cmd_phasetime(0, 3600))
    fw.feed("RUN")
    fw.drain()
    fw.feed("MICROSTEP 32")
    assert fw.drain() == ["!ERR MICROSTEP busy"]
    assert _telem(fw).microsteps == 16
    fw.feed("STOP")
    fw.feed("MICROSTEP 32")
    assert fw.drain() == [] and _telem(fw).microsteps == 32


def test_microstep_allowed_while_a_pump_jogs():
    fw = FakeFirmware()
    fw.feed("JOG 4 800")
    fw.drain()
    assert _telem(fw).pumps[4].running
    fw.feed("MICROSTEP 8")
    assert fw.drain() == []               # a jog does not block it, no reply
    t = _telem(fw)
    assert t.microsteps == 8
    assert t.pumps[4].running and t.pumps[4].speed == 800   # resumes at the same number
    fw.feed("MICROSTEP 256")              # ...and again, still jogging
    assert fw.drain() == [] and _telem(fw).microsteps == 256


def test_microstep_allowed_while_a_manual_speed_is_set():
    fw = FakeFirmware()
    fw.feed("SPEED 0 1200")
    fw.feed("JOG 0 1200")
    fw.feed("MICROSTEP 64")
    assert fw.drain() == [] and _telem(fw).microsteps == 64


def test_microstep_busy_only_while_a_program_runs_even_with_a_jog():
    fw = FakeFirmware()
    fw.feed("JOG 2 500")
    fw.feed(P.cmd_phasetime(0, 3600))
    fw.feed("RUN")                        # a program takes over from the jog
    fw.drain()
    fw.feed("MICROSTEP 8")
    assert fw.drain() == ["!ERR MICROSTEP busy"]
    assert _telem(fw).microsteps == 16
    fw.feed("STOP")
    fw.feed("MICROSTEP 8")
    assert fw.drain() == [] and _telem(fw).microsteps == 8


def test_microstep_validates_n_before_busy():
    fw = FakeFirmware()
    fw.feed(P.cmd_phasetime(0, 3600))
    fw.feed("RUN")
    fw.drain()
    fw.feed("MICROSTEP 3")
    assert fw.drain() == [_USAGE]         # bad n wins over busy


def test_microstep_does_not_change_reported_speeds():
    # speed numbers are raw microsteps/s: the number the GUI sent is the number
    # reported, whatever the setting (only the real shaft speed would differ)
    fw = FakeFirmware()
    fw.feed("SPEED 0 1000")
    fw.feed("JOG 0 1000")
    before = _telem(fw).pumps[0]
    fw.feed("MICROSTEP 256")              # while jogging
    assert _telem(fw).pumps[0].speed == before.speed == 1000
    fw.feed("STOP")
    fw.feed("JOG 0 1000")
    assert _telem(fw).pumps[0].speed == 1000


def test_legacy_firmware_has_no_ms_and_unknown_verb():
    fw = FakeFirmware(legacy_firmware=True)
    assert _telem(fw).microsteps is None
    assert '"ms"' not in fw.telemetry_line()
    fw.feed("MICROSTEP 32")
    errs = fw.drain()
    assert len(errs) == 1 and errs[0].startswith("!ERR") and "MICROSTEP busy" not in errs[0]
    assert _telem(fw).microsteps is None
