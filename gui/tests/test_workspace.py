"""Workspace model + on-disk store. Pure, no Qt."""
import pytest

from peachpulp import protocol as P


@pytest.fixture()
def W(tmp_path, monkeypatch):
    # workspace._root() reads PEACHPULP_HOME on every call, so this is enough
    monkeypatch.setenv("PEACHPULP_HOME", str(tmp_path))
    from peachpulp import workspace as mod

    return mod


def test_phasestep_roundtrip(W):
    s = W.PhaseStep(seconds=45, speeds={0: 1200, 2: -800}, label="Stain")
    d = s.to_dict()
    back = W.PhaseStep.from_dict(d)
    assert back.seconds == 45
    assert back.speeds == {0: 1200, 2: -800}
    assert back.label == "Stain"
    assert s.speed_list() == [1200, 0, -800, 0, 0, 0, 0, 0]


def test_phasestep_clamps_and_drops_bad_indices(W):
    s = W.PhaseStep.from_dict(
        {"seconds": 99999, "speeds": {"0": 99999, "9": 100, "x": 5}}
    )
    assert s.seconds == P.PHASE_SECONDS_MAX
    assert s.speeds == {0: P.SPEED_MAX}


def test_default_workspace_matches_seed_protocol(W):
    ws = W.default_workspace()
    assert len(ws.phases) == P.NUM_PHASES
    for phase, mask in zip(ws.phases, P.PHASE_MASKS):
        running = {i for i, v in phase.speeds.items() if v}
        assert running == {i for i in range(P.NUM_PUMPS) if mask & (1 << i)}


def test_workspace_roundtrip(W):
    ws = W.default_workspace()
    ws.name = "MyRun"
    ws.live_speeds[3] = 2500
    back = W.Workspace.from_dict(ws.to_dict())
    assert back.name == "MyRun"
    assert back.live_speeds[3] == 2500
    assert len(back.phases) == len(ws.phases)


def test_save_load_list_and_last_used(W):
    ws = W.default_workspace()
    ws.name = "Alpha"
    W.save(ws)
    ws2 = W.default_workspace()
    ws2.name = "Beta"
    W.save(ws2)

    assert W.list_workspaces() == ["Alpha", "Beta"]
    assert W.last_used() == "Beta"  # save() records it

    loaded = W.load("Alpha")
    assert loaded.name == "Alpha"
    assert W.last_used() == "Alpha"

    W.remember_last("Beta")
    assert W.load_last_or_default().name == "Beta"


def test_load_last_or_default_when_empty(W):
    assert W.last_used() is None
    assert W.load_last_or_default().name == "Default"


def test_name_is_sanitised_for_filename(W):
    ws = W.default_workspace()
    ws.name = "weird/../name?"
    path = W.save(ws)
    assert path.name == "weirdname.json"
    assert "weirdname" in W.list_workspaces()


def test_program_lines(W):
    ws = W.Workspace(
        name="x",
        phases=[
            W.PhaseStep(seconds=10, speeds={0: 1000}),
            W.PhaseStep(seconds=20, speeds={2: 500, 3: 500}),
        ],
    )
    lines = ws.program_lines()
    assert lines[0] == "PROGCLEAR"
    assert lines[1] == "PROGADD 10 1000 0 0 0 0 0 0 0"
    assert lines[2] == "PROGADD 20 0 0 500 500 0 0 0 0"
    assert lines[-1] == "PROGCOMMIT"
