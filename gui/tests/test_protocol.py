"""Protocol builders + parser. No Qt / serial deps — plain pytest."""
from peachpulp import protocol as P


def test_command_builders():
    assert P.cmd_ping() == "PING"
    assert P.cmd_run() == "RUN"
    assert P.cmd_skip() == "SKIP"
    assert P.cmd_speed(0, 2000) == "SPEED 0 2000"
    assert P.cmd_speed(2, -1234) == "SPEED 2 -1234"
    assert P.cmd_phasetime(1, 45) == "PHASETIME 1 45"
    assert P.cmd_enable(3, False) == "ENABLE 3 0"
    assert P.cmd_enable(3, True) == "ENABLE 3 1"
    assert P.cmd_jog(4, 800) == "JOG 4 800"


def test_speed_and_time_clamping():
    assert P.cmd_speed(0, 999999) == "SPEED 0 5000"
    assert P.cmd_speed(0, -999999) == "SPEED 0 -5000"
    assert P.cmd_phasetime(0, 0) == "PHASETIME 0 1"
    assert P.cmd_phasetime(0, 10000) == "PHASETIME 0 3600"


def test_phase_table_matches_firmware():
    # kProtocol[] in src/core/Protocol.h
    assert P.PHASE_MASKS[0] == (1 << 0) | (1 << 1) | (1 << 2)  # Sample+Dye+Sheath
    assert P.PHASE_MASKS[1] == (1 << 2) | (1 << 3)             # Sheath+Wash
    assert P.PHASE_MASKS[2] == (1 << 2) | (1 << 4)             # Sheath+Antibody
    assert P.PHASE_MASKS[3] == (1 << 2) | (1 << 5)             # Sheath+Wash2
    # Sheath (idx 2) runs in every phase
    assert all(m & (1 << 2) for m in P.PHASE_MASKS)


def test_parse_pong_log_event_error():
    assert P.parse_line("PONG") == ("pong", None)
    assert P.parse_line("# I PROTO: hello") == ("log", "I PROTO: hello")
    assert P.parse_line("!EVENT phase 2") == ("event", "phase 2")
    assert P.parse_line("!EVENT done") == ("event", "done")
    assert P.parse_line("!ERR bad thing") == ("error", "bad thing")
    assert P.parse_line("") is None
    assert P.parse_line("   ") is None
    assert P.parse_line("random noise") is None


def test_parse_telemetry():
    line = (
        '{"phase":1,"remaining":42,"estop":false,"pumps":['
        '{"sp":2000,"run":1,"en":1},{"sp":0,"run":0,"en":1},'
        '{"sp":1500,"run":1,"en":1},{"sp":0,"run":0,"en":0},'
        '{"sp":0,"run":0,"en":1},{"sp":0,"run":0,"en":1},'
        '{"sp":0,"run":0,"en":1},{"sp":0,"run":0,"en":1}]}'
    )
    kind, t = P.parse_line(line)
    assert kind == "telemetry"
    assert t.phase == 1 and t.remaining_s == 42 and t.estop is False
    assert t.running is True
    assert t.phase_label.startswith("Phase 2")
    assert t.pumps[0] == P.PumpState(speed=2000, running=True, enabled=True)
    assert t.pumps[3] == P.PumpState(speed=0, running=False, enabled=False)
    assert len(t.pumps) == P.NUM_PUMPS


def test_parse_idle_telemetry():
    kind, t = P.parse_line('{"phase":-1,"remaining":0,"estop":false,"pumps":[]}')
    assert kind == "telemetry"
    assert t.running is False
    assert t.phase_label == "Idle"


def test_parse_bad_json():
    kind, msg = P.parse_line('{"phase":1,')
    assert kind == "error" and "bad JSON" in msg
