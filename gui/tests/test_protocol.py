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
        '{"phase":1,"remaining":42,"nphases":4,"pumps":['
        '{"sp":2000,"run":1,"en":1},{"sp":0,"run":0,"en":1},'
        '{"sp":1500,"run":1,"en":1},{"sp":0,"run":0,"en":0},'
        '{"sp":0,"run":0,"en":1},{"sp":0,"run":0,"en":1},'
        '{"sp":0,"run":0,"en":1},{"sp":0,"run":0,"en":1}]}'
    )
    kind, t = P.parse_line(line)
    assert kind == "telemetry"
    assert t.phase == 1 and t.remaining_s == 42
    assert t.running is True
    assert t.phase_label.startswith("Phase 2")
    assert t.pumps[0] == P.PumpState(speed=2000, running=True, enabled=True)
    assert t.pumps[3] == P.PumpState(speed=0, running=False, enabled=False)
    assert len(t.pumps) == P.NUM_PUMPS


def test_parse_idle_telemetry():
    kind, t = P.parse_line('{"phase":-1,"remaining":0,"pumps":[]}')
    assert kind == "telemetry"
    assert t.running is False
    assert t.phase_label == "Idle"


def test_parse_bad_json():
    kind, msg = P.parse_line('{"phase":1,')
    assert kind == "error" and "bad JSON" in msg


def test_program_command_builders():
    assert P.cmd_prog_clear() == "PROGCLEAR"
    assert P.cmd_prog_commit() == "PROGCOMMIT"
    assert P.cmd_prog_add(30, {0: 1000, 2: -500}) == "PROGADD 30 1000 0 -500 0 0 0 0 0"
    assert P.cmd_prog_add(9999, [10] * 3) == "PROGADD 3600 10 10 10 0 0 0 0 0"


def test_cmd_program_from_pairs_and_objects():
    lines = P.cmd_program([(10, {1: 800}), (5, [0, 0, 300])])
    assert lines == [
        "PROGCLEAR",
        "PROGADD 10 0 800 0 0 0 0 0 0",
        "PROGADD 5 0 0 300 0 0 0 0 0",
        "PROGCOMMIT",
    ]

    class Step:
        seconds = 12
        speeds = {0: 100}

    assert P.cmd_program([Step()]) == [
        "PROGCLEAR",
        "PROGADD 12 100 0 0 0 0 0 0 0",
        "PROGCOMMIT",
    ]


def test_telemetry_nphases_and_label():
    kind, t = P.parse_line('{"phase":2,"nphases":5,"remaining":9,"pumps":[]}')
    assert kind == "telemetry"
    assert t.nphases == 5
    assert t.phase_label == "Phase 3 / 5"
