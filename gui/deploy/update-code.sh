#!/usr/bin/env bash
#
# Best-effort code sync, run by launch.sh on every start so the desktop icon
# always opens the newest pushed GUI. It NEVER blocks or fails the launch: every
# problem is logged and the script still exits 0.
#
#   deploy/update-code.sh               # fetch + fast-forward + (maybe) pip install
#   deploy/update-code.sh --no-update   # do nothing (also: PEACHPULP_NO_UPDATE=1)
#
# What it does:
#   1. `git fetch` the tracked branch, hard-bounded in time (offline -> skipped).
#   2. Fast-forwards ONLY: never reset --hard, never checkout -f, never discards
#      local edits. Skipped (and logged) when the working tree has uncommitted
#      changes to tracked files, when the checkout is on another branch, or when
#      local has commits that are not on the remote. Untracked files (.venv/ ...)
#      are ignored.
#   3. Re-runs `.venv/bin/pip install -r requirements.txt` only when
#      requirements.txt changed since the last successful install (hash stamp).
#      A failed pip keeps the stamp stale so it is retried next launch.
#
# Log:   ~/.local/state/peachpulp/launch.log   (rotated at ~200 KB)
# Env:   PEACHPULP_BRANCH   branch to follow           (default gui/pi-pyside6)
#        PEACHPULP_REMOTE   remote name or URL         (default origin)
#        PEACHPULP_STATE_DIR  log + stamp directory    (default ~/.local/state/peachpulp)
#        PEACHPULP_FETCH_TIMEOUT / PEACHPULP_PIP_TIMEOUT  seconds (default 8 / 300)
#
# Path-independent: the repo is found from this script's own location.
# Written for bash 3.2 (macOS) as well as the Pi's bash 5.
#
set -u
trap 'exit 0' EXIT      # whatever happens below, the launch path sees success

BRANCH="${PEACHPULP_BRANCH:-gui/pi-pyside6}"
REMOTE="${PEACHPULP_REMOTE:-origin}"
STATE_DIR="${PEACHPULP_STATE_DIR:-$HOME/.local/state/peachpulp}"
FETCH_TIMEOUT="${PEACHPULP_FETCH_TIMEOUT:-8}"
PIP_TIMEOUT="${PEACHPULP_PIP_TIMEOUT:-300}"
LOG_MAX_BYTES=204800
case "$FETCH_TIMEOUT" in ''|*[!0-9]*) FETCH_TIMEOUT=8 ;; esac
case "$PIP_TIMEOUT" in ''|*[!0-9]*) PIP_TIMEOUT=300 ;; esac

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GUI_DIR="$(dirname "$HERE")"                    # .../gui
REPO=""

# ---- logging -----------------------------------------------------------------

LOG="$STATE_DIR/launch.log"
mkdir -p "$STATE_DIR" 2>/dev/null || LOG=/dev/null

rotate_log() {
    [ "$LOG" = /dev/null ] && return 0
    [ -f "$LOG" ] || return 0
    local size
    size="$(wc -c < "$LOG" 2>/dev/null | tr -d ' ')"
    if [ "${size:-0}" -gt "$LOG_MAX_BYTES" ] 2>/dev/null; then
        mv -f "$LOG" "$LOG.1" 2>/dev/null || : > "$LOG"
    fi
    return 0
}

log() {
    printf '%s update-code: %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*" >> "$LOG" 2>/dev/null
    if [ -t 2 ]; then echo "peachpulp: $*" >&2; fi
    return 0
}

# ---- opt-out -----------------------------------------------------------------

DISABLED=0
for arg in "$@"; do
    [ "$arg" = "--no-update" ] && DISABLED=1
done
case "${PEACHPULP_NO_UPDATE:-}" in ""|0|false|no) ;; *) DISABLED=1 ;; esac
rotate_log
if [ "$DISABLED" = 1 ]; then
    log "update disabled"
    exit 0
fi

# ---- helpers -----------------------------------------------------------------

# Prefer GNU timeout / gtimeout; else a small portable watchdog. Never fails
# just because a timeout tool is missing.
TIMEOUT_BIN=""
if command -v timeout >/dev/null 2>&1; then TIMEOUT_BIN=timeout
elif command -v gtimeout >/dev/null 2>&1; then TIMEOUT_BIN=gtimeout
fi

# bounded SECS CMD...   run CMD, kill it after SECS seconds (exit 124 then)
bounded() {
    local secs="$1"; shift
    if [ -n "$TIMEOUT_BIN" ]; then
        "$TIMEOUT_BIN" -k 2 "$secs" "$@"
        return $?
    fi
    "$@" &
    local pid=$! rc=0 wd
    (   # watchdog: polls once a second so it ends soon after the command does
        i=0
        while [ "$i" -lt "$secs" ] && kill -0 "$pid" 2>/dev/null; do
            sleep 1; i=$((i + 1))
        done
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null; sleep 1; kill -9 "$pid" 2>/dev/null
        fi
    ) >/dev/null 2>&1 &
    wd=$!
    wait "$pid" 2>/dev/null || rc=$?
    kill "$wd" 2>/dev/null
    wait "$wd" 2>/dev/null        # reap it quietly (no "Terminated" job notice)
    [ "$rc" -ge 128 ] && rc=124
    return "$rc"
}

hash_file() {   # print a content hash of $1, or nothing on failure
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" 2>/dev/null | cut -d' ' -f1
    elif command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" 2>/dev/null | cut -d' ' -f1
    else cksum "$1" 2>/dev/null | cut -d' ' -f1,2 | tr ' ' '-'
    fi
}

# ---- git: fetch + fast-forward ----------------------------------------------

sync_code() {
    if ! command -v git >/dev/null 2>&1; then log "git not found, skipped"; return 0; fi
    REPO="$(git -C "$HERE" rev-parse --show-toplevel 2>/dev/null)"
    if [ -z "$REPO" ]; then log "$HERE is not inside a git checkout, skipped"; return 0; fi

    # never let git prompt (a credential dialog on the touchscreen would hang the launch)
    export GIT_TERMINAL_PROMPT=0 GIT_ASKPASS=true GCM_INTERACTIVE=never
    unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE
    local ssh_base="${GIT_SSH_COMMAND:-$(git -C "$REPO" config --get core.sshCommand 2>/dev/null)}"
    export GIT_SSH_COMMAND="${ssh_base:-ssh} -o BatchMode=yes -o ConnectTimeout=$FETCH_TIMEOUT"

    local cur head tip n
    cur="$(git -C "$REPO" symbolic-ref -q --short HEAD 2>/dev/null)"

    if ! bounded "$FETCH_TIMEOUT" git -C "$REPO" -c http.lowSpeedLimit=1000 \
            -c http.lowSpeedTime="$FETCH_TIMEOUT" fetch --quiet --no-tags \
            "$REMOTE" "$BRANCH" </dev/null >/dev/null 2>&1; then
        log "offline, skipped (fetch $REMOTE $BRANCH failed or timed out after ${FETCH_TIMEOUT}s)"
        return 0
    fi
    tip="$(git -C "$REPO" rev-parse -q --verify 'FETCH_HEAD^{commit}' 2>/dev/null)"
    head="$(git -C "$REPO" rev-parse -q --verify 'HEAD^{commit}' 2>/dev/null)"
    if [ -z "$tip" ] || [ -z "$head" ]; then log "could not resolve HEAD / fetched tip, skipped"; return 0; fi

    if [ "$head" = "$tip" ]; then
        log "up to date at $(echo "$head" | cut -c1-7)"
        return 0
    fi
    if [ "$cur" != "$BRANCH" ]; then
        log "on '${cur:-detached HEAD}', not $BRANCH, skipped"
        return 0
    fi
    if [ -n "$(git -C "$REPO" status --porcelain --untracked-files=no 2>/dev/null)" ]; then
        log "local changes to tracked files, skipped (remote is at $(echo "$tip" | cut -c1-7))"
        return 0
    fi
    if ! git -C "$REPO" merge-base --is-ancestor "$head" "$tip" 2>/dev/null; then
        log "local commits not on $REMOTE/$BRANCH (diverged or ahead), skipped"
        return 0
    fi

    n="$(git -C "$REPO" rev-list --count "$head..$tip" 2>/dev/null)"
    if git -C "$REPO" merge --ff-only --quiet "$tip" </dev/null >/dev/null 2>&1; then
        log "updated $(echo "$head" | cut -c1-7) -> $(echo "$tip" | cut -c1-7) (${n:-?} commits)"
    else
        log "fast-forward to $(echo "$tip" | cut -c1-7) failed, left as is"
    fi
    return 0
}

# ---- deps: pip install only when requirements.txt changed --------------------

sync_deps() {
    local req="$GUI_DIR/requirements.txt" pip="$GUI_DIR/.venv/bin/pip"
    local stamp="$STATE_DIR/requirements.sha" want have
    [ -f "$req" ] || return 0
    if [ ! -x "$pip" ]; then log "no venv pip at $pip, deps not checked"; return 0; fi
    want="$(hash_file "$req")"
    if [ -z "$want" ]; then log "could not hash requirements.txt, deps not checked"; return 0; fi
    have="$(cat "$stamp" 2>/dev/null)"
    [ "$have" = "$want" ] && return 0

    log "requirements.txt changed (or first run), installing deps"
    if bounded "$PIP_TIMEOUT" "$pip" install --quiet --disable-pip-version-check \
            --retries 1 --timeout 15 -r "$req" </dev/null >/dev/null 2>&1; then
        echo "$want" > "$stamp" 2>/dev/null
        log "deps installed"
    else
        log "pip install failed (offline?), will retry next launch"
    fi
    return 0
}

main() {
    sync_code
    sync_deps
}

main "$@"; exit 0
