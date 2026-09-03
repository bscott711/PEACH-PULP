"""Wire contract between the GUI and the Octopus firmware.

The firmware (branch breaking/octopus-stm32-fw, src/core/SerialLink.cpp) and this
GUI are separate codebases that must agree on the serial protocol. This test
transcribes the firmware's accepted grammar + emitted telemetry and checks the
GUI produces / parses exactly that. If either side changes the wire format, this
test should fail until both are updated.

Keep in sync with docs/ARCHITECTURE.md "Pi ↔ Octopus serial protocol".
"""
import json
import re

from peachpulp import protocol as P
from peachpulp.workspace import PhaseStep

# --- verbs SerialLink.cpp parseLine() dispatches on (strcasecmp) --------------
FIRMWARE_VERBS = {
    "PING", "RUN", "STOP", "SKIP", "STATE",
    "SPEED", "PHASETIME", "ENABLE", "JOG",
    "PROGCLEAR", "PROGADD", "PROGCOMMIT",
    "DFU",  # reboot into the ROM bootloader (src/core/DfuReboot.*)
}


def _verb(line: str) -> str:
    return line.split()[0]


def test_every_gui_command_is_a_firmware_verb():
    lines = [
        P.cmd_ping(), P.cmd_run(), P.cmd_stop(), P.cmd_skip(), P.cmd_state(),
        P.cmd_speed(0, 100), P.cmd_phasetime(0, 30), P.cmd_enable(0, True),
        P.cmd_jog(0, 100), P.cmd_prog_clear(), P.cmd_prog_commit(),
        P.cmd_prog_add(30, {0: 100}), P.cmd_dfu(),
    ]
    for ln in lines:
        assert _verb(ln) in FIRMWARE_VERBS, ln


def test_no_estop_anywhere():
    assert "ESTOP" not in FIRMWARE_VERBS
    assert not hasattr(P, "cmd_estop")


def test_progadd_is_seconds_plus_eight_speeds():
    # firmware parseProgAdd(): nextInt() seconds, then NUM_PUMPS ints,
    # whitespace-delimited, missing trailing values -> 0.
    line = P.cmd_prog_add(9999, {1: 99999, 6: -99999})
    m = re.fullmatch(r"PROGADD (-?\d+)((?: -?\d+){8})", line)
    assert m, line
    secs = int(m.group(1))
    speeds = [int(x) for x in m.group(2).split()]
    assert 1 <= secs <= 3600           # clamp_phase_seconds
    assert speeds[1] == P.SPEED_MAX    # clamp_speed
    assert speeds[6] == -P.SPEED_MAX
    assert speeds[0] == 0 and len(speeds) == P.NUM_PUMPS


def test_cmd_program_upload_sequence():
    lines = P.cmd_program([
        PhaseStep(seconds=10, speeds={0: 1000}),
        PhaseStep(seconds=20, speeds={2: 500, 3: 500}),
    ])
    assert lines[0] == "PROGCLEAR"
    assert lines[-1] == "PROGCOMMIT"
    assert lines[1] == "PROGADD 10 1000 0 0 0 0 0 0 0"
    assert lines[2] == "PROGADD 20 0 0 500 500 0 0 0 0"


# --- firmware telemetry (SerialLink.cpp emitTelemetry) -----------------------
def _fw_telemetry(phase, nphases, remaining, pumps):
    body = ",".join(
        '{"sp":%d,"run":%d,"en":%d}' % (sp, r, e) for sp, r, e in pumps
    )
    return (
        '{"phase":%d,"nphases":%d,"remaining":%d,"pumps":[%s]}'
        % (phase, nphases, remaining, body)
    )


def test_gui_parses_firmware_telemetry():
    pumps = [(1000, 1, 1)] * 3 + [(0, 0, 1)] * 4 + [(0, 0, 0)]
    raw = _fw_telemetry(1, 4, 42, pumps)
    json.loads(raw)  # must be valid JSON

    kind, t = P.parse_line(raw)
    assert kind == "telemetry"
    assert (t.phase, t.nphases, t.remaining_s) == (1, 4, 42)
    assert t.phase_label == "Phase 2 / 4"
    assert len(t.pumps) == P.NUM_PUMPS
    assert t.pumps[0].running and not t.pumps[7].enabled
    assert not hasattr(t, "estop")


def test_gui_parses_firmware_idle_telemetry():
    kind, t = P.parse_line(_fw_telemetry(-1, 4, 0, [(0, 0, 1)] * 8))
    assert kind == "telemetry" and not t.running
    assert t.phase_label == "Idle"


def test_gui_parses_firmware_events():
    for ev in ("!EVENT phase 0", "!EVENT phase 3", "!EVENT done", "!EVENT prog 5"):
        assert P.parse_line(ev)[0] == "event"
    for er in ("!ERR empty program", "!ERR unknown command"):
        assert P.parse_line(er)[0] == "error"
    assert P.parse_line("PONG") == ("pong", None)
    assert P.parse_line("# STORE: flash blob initialised (v3)")[0] == "log"
