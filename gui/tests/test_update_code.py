"""deploy/update-code.sh + deploy/launch.sh: the self-updating desktop launcher.

Everything runs against throwaway git repos in tmp_path (a bare "remote", a "dev"
clone that pushes, and a "pi" clone with the real scripts at <clone>/gui/deploy/).
No network, no Qt, no Pi. Needs only bash + git.
"""
import os
import shutil
import subprocess
import time
import xml.etree.ElementTree as ET
from pathlib import Path

import pytest

DEPLOY = Path(__file__).resolve().parent.parent / "deploy"
BRANCH = "gui/pi-pyside6"
LOG_NAME = "launch.log"


# ---- helpers -----------------------------------------------------------------


def base_env(tmp_path):
    env = {k: v for k, v in os.environ.items() if not k.startswith("PEACHPULP_")}
    for k in ("GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE"):
        env.pop(k, None)
    env.update(
        HOME=str(tmp_path / "home"),
        GIT_CONFIG_GLOBAL=os.devnull,   # ignore the developer's own git config
        GIT_CONFIG_NOSYSTEM="1",
        PEACHPULP_STATE_DIR=str(tmp_path / "state"),
    )
    return env


class World:
    def __init__(self, tmp_path):
        self.tmp = tmp_path
        self.env = base_env(tmp_path)
        self.state = tmp_path / "state"
        self.remote = tmp_path / "remote.git"
        self.dev = tmp_path / "dev"
        self.pi = tmp_path / "pi clone"        # space on purpose: paths must be quoted
        (tmp_path / "home").mkdir()

        self.git(tmp_path, "init", "--bare", "-q", str(self.remote))
        self.git(self.remote, "symbolic-ref", "HEAD", f"refs/heads/{BRANCH}")
        self.git(tmp_path, "init", "-q", str(self.dev))
        self.git(self.dev, "symbolic-ref", "HEAD", f"refs/heads/{BRANCH}")
        self._identity(self.dev)
        self.git(self.dev, "remote", "add", "origin", str(self.remote))

        (self.dev / "gui" / "deploy").mkdir(parents=True)
        for name in ("update-code.sh", "launch.sh"):
            shutil.copy(DEPLOY / name, self.dev / "gui" / "deploy" / name)
        (self.dev / "gui" / "requirements.txt").write_text("pyserial>=3.5\n")
        (self.dev / "gui" / "notes.txt").write_text("v1\n")
        self.commit_push("initial")

        self.git(tmp_path, "clone", "-q", str(self.remote), str(self.pi))
        self._identity(self.pi)
        assert self.branch(self.pi) == BRANCH

    # -- git plumbing
    def git(self, cwd, *args):
        r = subprocess.run(
            ["git", *args], cwd=str(cwd), env=self.env,
            capture_output=True, text=True, check=True,
        )
        return r.stdout.strip()

    def _identity(self, repo):
        self.git(repo, "config", "user.name", "Test")
        self.git(repo, "config", "user.email", "test@example.invalid")

    def head(self, repo):
        return self.git(repo, "rev-parse", "HEAD")

    def branch(self, repo):
        return self.git(repo, "symbolic-ref", "--short", "HEAD")

    def commit(self, repo, rel, text, msg):
        p = repo / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)
        self.git(repo, "add", "-A")
        self.git(repo, "commit", "-q", "-m", msg)

    def commit_push(self, msg, rel=None, text=None):
        if rel:
            self.commit(self.dev, rel, text, msg)
        else:
            self.git(self.dev, "add", "-A")
            self.git(self.dev, "commit", "-q", "-m", msg)
        self.git(self.dev, "push", "-q", "origin", BRANCH)
        return self.head(self.dev)

    # -- the scripts under test (run from the pi clone, like the real thing)
    def update(self, *args, env=None, path_prefix=None):
        e = dict(self.env, **(env or {}))
        if path_prefix:
            e["PATH"] = f"{path_prefix}{os.pathsep}{e['PATH']}"
        return subprocess.run(
            ["bash", str(self.pi / "gui" / "deploy" / "update-code.sh"), *args],
            cwd=str(self.tmp), env=e, capture_output=True, text=True, timeout=90,
        )

    def launch(self, *args, env=None):
        e = dict(self.env, PEACHPULP_SERIAL_WAIT="0", **(env or {}))
        return subprocess.run(
            ["bash", str(self.pi / "gui" / "deploy" / "launch.sh"), *args],
            cwd=str(self.tmp), env=e, capture_output=True, text=True, timeout=90,
        )

    def log(self):
        p = self.state / LOG_NAME
        return p.read_text() if p.exists() else ""

    # -- fake venv: python records argv, pip records its calls (and can fail)
    def fake_venv(self):
        self.pip_log = self.tmp / "pip.calls"
        self.pip_fail = self.tmp / "pip.fail"
        self.py_args = self.tmp / "python.args"
        bindir = self.pi / "gui" / ".venv" / "bin"
        bindir.mkdir(parents=True)
        pip = bindir / "pip"
        pip.write_text(
            "#!/usr/bin/env bash\n"
            f"echo \"$@\" >> '{self.pip_log}'\n"
            f"[ -e '{self.pip_fail}' ] && exit 1\n"
            "exit 0\n"
        )
        py = bindir / "python"
        py.write_text(
            "#!/usr/bin/env bash\n"
            f"printf '%s\\n' \"$@\" > '{self.py_args}'\n"
        )
        pip.chmod(0o755)
        py.chmod(0o755)

    def pip_calls(self):
        return self.pip_log.read_text().splitlines() if self.pip_log.exists() else []

    def python_args(self):
        return self.py_args.read_text().splitlines()

    def stamp(self):
        p = self.state / "requirements.sha"
        return p.read_text().strip() if p.exists() else None


@pytest.fixture()
def w(tmp_path):
    return World(tmp_path)


def ok(proc):
    assert proc.returncode == 0, proc.stdout + proc.stderr
    assert proc.stderr == ""            # no TTY -> nothing echoed, only the log


# ---- update-code.sh: git sync -------------------------------------------------


def test_fast_forwards_when_remote_is_ahead(w):
    new = w.commit_push("c2", "gui/newfile.txt", "hi\n")
    before = w.head(w.pi)
    assert before != new
    ok(w.update())
    assert w.head(w.pi) == new
    assert (w.pi / "gui" / "newfile.txt").read_text() == "hi\n"
    assert w.git(w.pi, "status", "--porcelain") == ""
    assert "updated" in w.log()


def test_noop_when_up_to_date(w):
    before = w.head(w.pi)
    ok(w.update())
    assert w.head(w.pi) == before
    assert "up to date" in w.log()


def test_untracked_files_do_not_block_the_update(w):
    (w.pi / "gui" / ".venv").mkdir()
    (w.pi / "gui" / ".venv" / "junk").write_text("x")
    (w.pi / ".claude").mkdir()
    (w.pi / ".claude" / "scratch.md").write_text("x")
    new = w.commit_push("c2", "gui/newfile.txt", "hi\n")
    ok(w.update())
    assert w.head(w.pi) == new
    assert (w.pi / ".claude" / "scratch.md").exists()


@pytest.mark.parametrize("how", ["remote-moved-away", "bad-url"])
def test_offline_is_skipped_not_fatal(w, how):
    w.commit_push("c2", "gui/newfile.txt", "hi\n")
    before = w.head(w.pi)
    env = {}
    if how == "remote-moved-away":
        w.remote.rename(w.tmp / "remote.gone")
    else:
        env["PEACHPULP_REMOTE"] = "file:///nonexistent/peach-pulp.git"
    ok(w.update(env=env))
    assert w.head(w.pi) == before
    assert "offline, skipped" in w.log()


def test_hanging_fetch_is_really_bounded(w, tmp_path):
    # a `git` that hangs forever on fetch (blackholed network) must be cut off
    real_git = shutil.which("git")
    stub = tmp_path / "stubbin"
    stub.mkdir()
    (stub / "git").write_text(
        "#!/usr/bin/env bash\n"
        'for a in "$@"; do [ "$a" = fetch ] && exec sleep 120; done\n'
        f"exec '{real_git}' \"$@\"\n"
    )
    (stub / "git").chmod(0o755)
    w.commit_push("c2", "gui/newfile.txt", "hi\n")
    before = w.head(w.pi)
    t0 = time.time()
    ok(w.update(env={"PEACHPULP_FETCH_TIMEOUT": "2"}, path_prefix=str(stub)))
    assert time.time() - t0 < 30
    assert w.head(w.pi) == before
    assert "offline, skipped" in w.log()


def test_dirty_tracked_file_is_skipped_and_intact(w):
    (w.pi / "gui" / "notes.txt").write_text("my local edit\n")
    w.commit_push("c2", "gui/newfile.txt", "hi\n")
    before = w.head(w.pi)
    ok(w.update())
    assert w.head(w.pi) == before
    assert (w.pi / "gui" / "notes.txt").read_text() == "my local edit\n"
    assert not (w.pi / "gui" / "newfile.txt").exists()
    assert "local changes" in w.log()


@pytest.mark.parametrize("remote_also_ahead", [True, False])
def test_diverged_or_ahead_is_skipped_without_reset(w, remote_also_ahead):
    w.commit(w.pi, "gui/local.txt", "mine\n", "local commit")
    local = w.head(w.pi)
    if remote_also_ahead:
        w.commit_push("c2", "gui/newfile.txt", "hi\n")
    ok(w.update())
    assert w.head(w.pi) == local
    assert (w.pi / "gui" / "local.txt").read_text() == "mine\n"
    assert "skipped" in w.log() and "diverged or ahead" in w.log()


def test_other_branch_is_left_alone(w):
    w.git(w.pi, "checkout", "-q", "-b", "scratch")
    before = w.head(w.pi)
    w.commit_push("c2", "gui/newfile.txt", "hi\n")
    ok(w.update())
    assert w.branch(w.pi) == "scratch"
    assert w.head(w.pi) == before
    assert "not %s" % BRANCH in w.log()


@pytest.mark.parametrize("how", ["flag", "env"])
def test_update_can_be_disabled(w, how):
    w.commit_push("c2", "gui/newfile.txt", "hi\n")
    before = w.head(w.pi)
    if how == "flag":
        proc = w.update("--no-update")
    else:
        proc = w.update(env={"PEACHPULP_NO_UPDATE": "1"})
    ok(proc)
    assert w.head(w.pi) == before
    assert "update disabled" in w.log()


def test_exits_zero_outside_a_git_checkout(tmp_path):
    plain = tmp_path / "plain" / "gui" / "deploy"
    plain.mkdir(parents=True)
    shutil.copy(DEPLOY / "update-code.sh", plain / "update-code.sh")
    env = base_env(tmp_path)
    proc = subprocess.run(["bash", str(plain / "update-code.sh")], env=env,
                          capture_output=True, text=True, timeout=60)
    ok(proc)
    assert "not inside a git checkout" in (tmp_path / "state" / LOG_NAME).read_text()


def test_log_is_rotated_when_large(w):
    w.state.mkdir()
    (w.state / LOG_NAME).write_text("x" * 250_000 + "\n")
    ok(w.update())
    assert (w.state / (LOG_NAME + ".1")).exists()
    assert len(w.log()) < 5_000


# ---- update-code.sh: requirements stamp --------------------------------------


def test_requirements_change_triggers_pip_once(w):
    w.fake_venv()

    ok(w.update())                                   # first run: stamp missing -> install
    assert len(w.pip_calls()) == 1
    assert "install" in w.pip_calls()[0] and "requirements.txt" in w.pip_calls()[0]
    first_stamp = w.stamp()
    assert first_stamp

    ok(w.update())                                   # nothing changed
    assert len(w.pip_calls()) == 1

    w.commit_push("c2", "gui/notes.txt", "v2\n")     # update without a requirements change
    ok(w.update())
    assert (w.pi / "gui" / "notes.txt").read_text() == "v2\n"
    assert len(w.pip_calls()) == 1

    w.commit_push("c3", "gui/requirements.txt", "pyserial>=3.5\npytest>=7\n")
    ok(w.update())                                   # requirements changed -> once
    assert len(w.pip_calls()) == 2
    assert w.stamp() != first_stamp

    ok(w.update())                                   # ...and not again
    assert len(w.pip_calls()) == 2


def test_pip_failure_keeps_the_stamp_stale_and_retries(w):
    w.fake_venv()
    ok(w.update())
    good_stamp = w.stamp()
    assert len(w.pip_calls()) == 1

    w.pip_fail.write_text("")
    w.commit_push("c2", "gui/requirements.txt", "pyserial>=3.5\npytest>=7\n")
    ok(w.update())
    assert len(w.pip_calls()) == 2
    assert w.stamp() == good_stamp                   # stale on purpose
    assert "pip install failed" in w.log()

    ok(w.update())                                   # retried next launch
    assert len(w.pip_calls()) == 3
    assert w.stamp() == good_stamp

    w.pip_fail.unlink()
    ok(w.update())                                   # network back: succeeds, stamp moves
    assert len(w.pip_calls()) == 4
    assert w.stamp() != good_stamp
    ok(w.update())
    assert len(w.pip_calls()) == 4


def test_no_venv_means_no_pip_and_no_failure(w):
    w.commit_push("c2", "gui/requirements.txt", "pyserial>=3.5\npytest>=7\n")
    ok(w.update())
    assert w.stamp() is None
    assert "no venv pip" in w.log()


# ---- launch.sh ---------------------------------------------------------------


def test_launch_syncs_then_starts_fullscreen(w):
    w.fake_venv()
    new = w.commit_push("c2", "gui/newfile.txt", "hi\n")
    proc = w.launch()
    assert proc.returncode == 0, proc.stderr
    assert w.head(w.pi) == new
    assert w.python_args() == ["run.py", "--fullscreen"]


def test_launch_passes_args_and_strips_no_update(w):
    w.fake_venv()
    w.commit_push("c2", "gui/newfile.txt", "hi\n")
    before = w.head(w.pi)
    proc = w.launch("--no-update", "--windowed", "--sim", "--port", "/dev/x")
    assert proc.returncode == 0, proc.stderr
    assert w.head(w.pi) == before                    # --no-update: no sync
    assert w.python_args() == ["run.py", "--sim", "--port", "/dev/x"]


def test_launch_env_opt_out(w):
    w.fake_venv()
    w.commit_push("c2", "gui/newfile.txt", "hi\n")
    before = w.head(w.pi)
    proc = w.launch(env={"PEACHPULP_NO_UPDATE": "1"})
    assert proc.returncode == 0, proc.stderr
    assert w.head(w.pi) == before
    assert w.python_args() == ["run.py", "--fullscreen"]


def test_launch_reexecs_once_into_the_pulled_launch_sh(w, tmp_path):
    w.fake_venv()
    mark = tmp_path / "mark"
    old = (w.dev / "gui" / "deploy" / "launch.sh").read_text()
    new = old.replace("MODE=--fullscreen\n", 'echo new-launch >> "$PEACHPULP_TEST_MARK"\nMODE=--fullscreen\n')
    assert new != old
    tip = w.commit_push("new launcher", "gui/deploy/launch.sh", new)
    proc = w.launch(env={"PEACHPULP_TEST_MARK": str(mark)})
    assert proc.returncode == 0, proc.stderr
    assert w.head(w.pi) == tip
    assert mark.read_text().splitlines() == ["new-launch"]   # new file ran, exactly once
    assert w.python_args() == ["run.py", "--fullscreen"]
    assert w.log().count("updated") == 1                     # no sync loop


def test_launch_survives_a_broken_updater(w):
    w.fake_venv()
    (w.pi / "gui" / "deploy" / "update-code.sh").write_text("#!/usr/bin/env bash\nexit 7\n")
    proc = w.launch()
    assert proc.returncode == 0, proc.stderr
    assert w.python_args() == ["run.py", "--fullscreen"]


def test_launch_without_venv_says_so(w):
    proc = w.launch("--no-update")
    assert proc.returncode == 1
    assert "no venv" in proc.stderr


# ---- static checks -----------------------------------------------------------


@pytest.mark.parametrize("name", ["launch.sh", "update-code.sh", "install-launcher.sh", "pi-update.sh"])
def test_scripts_parse(name):
    r = subprocess.run(["bash", "-n", str(DEPLOY / name)], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr


def test_icon_is_valid_self_contained_svg():
    root = ET.parse(DEPLOY / "peach-pulp.svg").getroot()
    assert root.tag.endswith("}svg") or root.tag == "svg"
    assert root.get("viewBox") == "0 0 256 256"
    for el in root.iter():
        for attr, val in el.attrib.items():
            assert not attr.endswith("href"), "no external refs"
            assert "http" not in val
    assert any(el.tag.endswith("Gradient") for el in root.iter())


def test_desktop_entry_uses_the_theme_icon_and_launch_placeholder():
    text = (DEPLOY / "peach-pulp.desktop").read_text()
    assert "Icon=peach-pulp\n" in text
    assert "Exec=__LAUNCH__" in text
