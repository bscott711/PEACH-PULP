"""deploy/install-launcher.sh: menu entry + desktop icon that open without pcmanfm's
"Execute / Execute in Terminal / Open / Cancel" dialog (trusted entry + libfm quick_exec=1).

The script is copied into tmp_path and run with a sandboxed HOME/XDG_CONFIG_HOME and a
PATH that holds only stub gio / dbus-run-session / xdg-user-dir plus symlinks to the
handful of coreutils it needs. No network, no Qt, no Pi, no real desktop. Needs only bash.
"""
import os
import shutil
import subprocess
from pathlib import Path

import pytest

DEPLOY = Path(__file__).resolve().parent.parent / "deploy"
NAME = "peach-pulp.desktop"
TOOLS = ("dirname", "chmod", "mkdir", "cp", "sed", "rm", "mktemp", "awk", "cmp",
         "mv", "touch", "head", "cat")


class Sandbox:
    def __init__(self, tmp_path):
        self.tmp = tmp_path
        self.home = tmp_path / "home dir"           # space on purpose: paths must be quoted
        self.cfg = tmp_path / "xdg config"
        self.desktop = tmp_path / "home dir" / "Desk top"
        self.deploy = tmp_path / "pi clone" / "gui" / "deploy"
        self.bin = tmp_path / "stubbin"
        self.calls = tmp_path / "calls.log"
        self.gio_fail = tmp_path / "gio.fail"
        self.home.mkdir()
        self.deploy.mkdir(parents=True)
        self.bin.mkdir()
        for f in ("install-launcher.sh", "launch.sh", NAME, "peach-pulp.svg"):
            shutil.copy(DEPLOY / f, self.deploy / f)
        for tool in TOOLS:
            real = shutil.which(tool)
            assert real, f"need {tool} on PATH"
            (self.bin / tool).symlink_to(real)
        self.stub("xdg-user-dir", f"echo '{self.desktop}'\n")
        self.stub("gio",
                  f"echo \"gio $*\" >> '{self.calls}'\n"
                  f"[ -e '{self.gio_fail}' ] && {{ echo 'gio: no D-Bus' >&2; exit 1; }}\n"
                  "exit 0\n")
        self.stub("dbus-run-session",
                  f"echo \"dbus-run-session $*\" >> '{self.calls}'\n"
                  "shift\n"                          # drop the --
                  'exec "$@"\n')
        self.libfm = self.cfg / "libfm" / "libfm.conf"

    def stub(self, name, body):
        p = self.bin / name
        p.write_text("#!/bin/bash\n" + body)
        p.chmod(0o755)

    def env(self, **extra):
        env = {
            "PATH": str(self.bin),
            "HOME": str(self.home),
            "XDG_CONFIG_HOME": str(self.cfg),
            "DBUS_SESSION_BUS_ADDRESS": "unix:path=/nonexistent/bus",
        }
        env.update(extra)
        return {k: v for k, v in env.items() if v is not None}

    def run(self, *args, **extra_env):
        return subprocess.run(
            [shutil.which("bash"), str(self.deploy / "install-launcher.sh"), *args],
            cwd=str(self.tmp), env=self.env(**extra_env),
            capture_output=True, text=True, timeout=60,
        )

    def entries(self):
        return [self.home / ".local/share/applications" / NAME, self.desktop / NAME]

    def gio_calls(self):
        text = self.calls.read_text() if self.calls.exists() else ""
        return [ln for ln in text.splitlines() if ln.startswith("gio ")]

    def dbus_calls(self):
        text = self.calls.read_text() if self.calls.exists() else ""
        return [ln for ln in text.splitlines() if ln.startswith("dbus-run-session ")]

    def conf(self):
        return self.libfm.read_text()

    def write_conf(self, text):
        self.libfm.parent.mkdir(parents=True, exist_ok=True)
        self.libfm.write_text(text)


@pytest.fixture()
def sb(tmp_path):
    return Sandbox(tmp_path)


def ok(proc):
    assert proc.returncode == 0, proc.stdout + proc.stderr
    return proc.stdout


def quick_exec_lines(text):
    return [ln for ln in text.splitlines() if ln.strip().startswith("quick_exec")]


def config_section(text):
    """Lines of the first [config] group."""
    out, inside = [], False
    for ln in text.splitlines():
        if ln.strip().startswith("["):
            if inside:
                break
            inside = ln.strip() == "[config]"
            continue
        if inside:
            out.append(ln)
    return out


# ---- entries ------------------------------------------------------------------


def test_entries_are_written_executable_with_launch_path(sb):
    out = ok(sb.run())
    launch = str(sb.deploy / "launch.sh")
    for entry in sb.entries():
        assert entry.is_file(), entry
        assert os.access(entry, os.X_OK)
        text = entry.read_text()
        assert "__LAUNCH__" not in text
        assert f"Exec={launch}\n" in text
    assert (sb.home / ".local/share/icons/hicolor/scalable/apps/peach-pulp.svg").is_file()
    assert "desktop icon written" in out and "app-menu entry written" in out


def test_rerun_is_idempotent(sb):
    ok(sb.run())
    before = [e.read_text() for e in sb.entries()]
    ok(sb.run())
    assert [e.read_text() for e in sb.entries()] == before


def test_autostart_entry(sb):
    out = ok(sb.run("--autostart"))
    auto = sb.home / ".config" / "autostart" / NAME
    assert auto.is_file() and os.access(auto, os.X_OK)
    assert "autostart enabled" in out


# ---- trust --------------------------------------------------------------------


def test_each_entry_is_marked_trusted_and_says_so(sb):
    out = ok(sb.run())
    calls = sb.gio_calls()
    for entry in sb.entries():
        assert f"gio set {entry} metadata::trusted true" in calls
    assert out.count("marked trusted") == 2
    assert "NOT marked trusted" not in out
    assert sb.dbus_calls() == []                     # session bus present: no private one


def test_no_session_bus_also_tries_a_private_one(sb):
    out = ok(sb.run(DBUS_SESSION_BUS_ADDRESS=None))
    assert len(sb.dbus_calls()) == 2
    assert "marked trusted" in out and "NOT marked trusted" not in out


def test_gio_failure_retries_on_private_session_and_install_still_succeeds(sb):
    sb.gio_fail.write_text("")
    out = ok(sb.run())
    # direct attempt + dbus-run-session attempt, per entry
    assert len(sb.dbus_calls()) == 2
    assert len(sb.gio_calls()) == 4
    assert out.count("NOT marked trusted") == 2      # never silent
    assert "gio: no D-Bus" in out
    for entry in sb.entries():
        assert entry.is_file() and os.access(entry, os.X_OK)
    assert quick_exec_lines(sb.conf()) == ["quick_exec=1"]   # rest of the install still ran


def test_gio_failure_without_dbus_run_session_does_not_fail_install(sb):
    (sb.bin / "dbus-run-session").unlink()
    sb.gio_fail.write_text("")
    out = ok(sb.run())
    assert out.count("NOT marked trusted") == 2
    assert sb.dbus_calls() == []
    assert all(e.is_file() for e in sb.entries())


def test_missing_gio_is_reported_not_fatal(sb):
    (sb.bin / "gio").unlink()
    out = ok(sb.run())
    assert out.count("NOT marked trusted (gio not found)") == 2
    assert all(e.is_file() for e in sb.entries())


# ---- libfm.conf quick_exec ----------------------------------------------------


def test_missing_libfm_conf_is_created(sb):
    assert not sb.libfm.exists()
    out = ok(sb.run())
    assert sb.conf() == "[config]\nquick_exec=1\n"
    assert "quick_exec=1" in out
    assert not list(sb.libfm.parent.glob("libfm.conf.*"))    # no temp litter


def test_xdg_config_home_defaults_to_dot_config(sb):
    ok(sb.run(XDG_CONFIG_HOME=None))
    assert (sb.home / ".config" / "libfm" / "libfm.conf").read_text() == "[config]\nquick_exec=1\n"
    assert not sb.libfm.exists()


def test_other_groups_only_gets_a_config_group_appended(sb):
    original = "[ui]\nbig_icon_size=48\n# keep me\n"
    sb.write_conf(original)
    ok(sb.run())
    text = sb.conf()
    assert text.startswith(original)
    assert config_section(text) == ["quick_exec=1"]
    assert quick_exec_lines(text) == ["quick_exec=1"]


def test_existing_config_group_keeps_other_keys_and_comments(sb):
    original = (
        "# libfm settings, edited by hand\n"
        "[config]\n"
        "single_click=0\n"
        "# a comment inside the group\n"
        "use_trash=1\n"
        "\n"
        "[ui]\n"
        "big_icon_size=48\n"
    )
    sb.write_conf(original)
    ok(sb.run())
    text = sb.conf()
    assert quick_exec_lines(text) == ["quick_exec=1"]
    assert "quick_exec=1" in config_section(text)
    # nothing else touched: removing our line gives the original back byte for byte
    assert text.replace("quick_exec=1\n", "", 1) == original


@pytest.mark.parametrize("line", ["quick_exec=0", "quick_exec = 0", "quick_exec=1", "  quick_exec=false"])
def test_existing_quick_exec_is_replaced_in_place(sb, line):
    sb.write_conf(f"[config]\nsingle_click=0\n{line}\nuse_trash=1\n[ui]\nbig_icon_size=48\n")
    ok(sb.run())
    assert sb.conf() == "[config]\nsingle_click=0\nquick_exec=1\nuse_trash=1\n[ui]\nbig_icon_size=48\n"


def test_duplicate_quick_exec_lines_collapse_to_one(sb):
    sb.write_conf("[config]\nquick_exec=0\nuse_trash=1\nquick_exec=0\n")
    ok(sb.run())
    assert sb.conf() == "[config]\nquick_exec=1\nuse_trash=1\n"


def test_quick_exec_in_another_group_is_not_ours(sb):
    sb.write_conf("[other]\nquick_exec=0\n")
    ok(sb.run())
    text = sb.conf()
    assert text.startswith("[other]\nquick_exec=0\n")
    assert config_section(text) == ["quick_exec=1"]


def test_file_without_trailing_newline(sb):
    sb.write_conf("[config]\nuse_trash=1")
    ok(sb.run())
    assert sb.conf() == "[config]\nquick_exec=1\nuse_trash=1\n"


@pytest.mark.parametrize("start", [None, "", "[ui]\nx=1\n", "[config]\nuse_trash=1\n# c\n",
                                   "[config]\nquick_exec=0\n[ui]\nx=1\n"])
def test_second_run_changes_nothing(sb, start):
    if start is not None:
        sb.write_conf(start)
    ok(sb.run())
    first = sb.conf()
    mtime = sb.libfm.stat().st_mtime_ns
    out = ok(sb.run())
    third = ok(sb.run())
    assert sb.conf() == first
    assert sb.libfm.stat().st_mtime_ns == mtime      # not even rewritten
    assert quick_exec_lines(first) == ["quick_exec=1"]
    assert "already set" in out and "already set" in third


def test_opt_out_leaves_libfm_conf_untouched(sb):
    ok(sb.run(PEACHPULP_NO_QUICK_EXEC="1"))
    assert not sb.libfm.exists() and not sb.libfm.parent.exists()
    original = "[config]\nquick_exec=0\n# mine\n"
    sb.write_conf(original)
    out = ok(sb.run(PEACHPULP_NO_QUICK_EXEC="1"))
    assert sb.conf() == original
    assert "skipped" in out


def test_unwritable_config_dir_does_not_fail_install(sb):
    sb.cfg.write_text("not a directory")             # mkdir -p libfm/ under it fails
    out = ok(sb.run())
    assert "skipped" in out
    assert all(e.is_file() for e in sb.entries())


# ---- output + --off -----------------------------------------------------------


def test_final_hint_is_printed(sb):
    out = ok(sb.run())
    assert "log out" in out
    assert "Don't ask options on launch executable file" in out


def test_off_removes_entries_and_leaves_libfm_conf(sb):
    ok(sb.run("--autostart"))
    conf = sb.conf()
    assert all(e.exists() for e in sb.entries())
    out = ok(sb.run("--off"))
    assert not any(e.exists() for e in sb.entries())
    assert not (sb.home / ".config" / "autostart" / NAME).exists()
    assert "removed launcher entries" in out
    assert sb.conf() == conf


def test_off_without_anything_installed_is_fine(sb):
    ok(sb.run("--off"))
    assert not sb.libfm.exists()
