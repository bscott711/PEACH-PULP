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
