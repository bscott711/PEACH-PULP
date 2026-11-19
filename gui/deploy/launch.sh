#!/usr/bin/env bash
#
# PEACH PULP GUI launcher — used by the desktop icon and the autostart entry.
# Path-independent: finds the repo from this script's own location, so it works
# whatever user or directory the repo lives in.
#
#   deploy/launch.sh              # fullscreen on the touchscreen
#   deploy/launch.sh --windowed   # a normal window (for poking around)
#   deploy/launch.sh --no-update  # skip the code sync (or PEACHPULP_NO_UPDATE=1)
#
# Before starting, it fast-forwards the checkout to the newest pushed code
# (deploy/update-code.sh — best effort, never blocks or fails the launch; see
# ~/.local/state/peachpulp/launch.log), then re-execs itself once so a pull
# that replaced this very file is picked up.
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GUI_DIR="$(dirname "$HERE")"            # .../gui
cd "$GUI_DIR"

# Split our flags from the ones that go to run.py. (${ARGS[@]+...} keeps an
# empty array safe under `set -u` on bash 3.2.)
NO_UPDATE=0
WINDOWED=0
ARGS=()
for a in "$@"; do
    case "$a" in
        --no-update) NO_UPDATE=1 ;;
        --windowed)  WINDOWED=1 ;;
        *)           ARGS+=("$a") ;;
    esac
done
case "${PEACHPULP_NO_UPDATE:-}" in ""|0|false|no) ;; *) NO_UPDATE=1 ;; esac

# Sync the code first, then restart this script ONCE (PEACHPULP_SYNCED guards the
# loop) so what runs below is whatever launch.sh the sync left on disk.
if [ "$NO_UPDATE" = 0 ] && [ -z "${PEACHPULP_SYNCED:-}" ]; then
    bash "$HERE/update-code.sh" || true
    export PEACHPULP_SYNCED=1
    exec bash "$HERE/launch.sh" "$@"
fi
unset PEACHPULP_SYNCED

PY="$GUI_DIR/.venv/bin/python"
if [ ! -x "$PY" ]; then
    echo "no venv at $PY — run deploy/pi-bootstrap.sh first" >&2
    exit 1
fi

# When started from an SSH / Raspberry Pi Connect shell there's no display in the
# environment. Fall back to the local session (XWayland answers on :0 on Pi OS).
export DISPLAY="${DISPLAY:-:0}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

MODE=--fullscreen
[ "$WINDOWED" = 1 ] && MODE=

# Give the Octopus a few seconds to enumerate after a cold boot. The GUI retries
# on its own, so this only avoids a brief "disconnected" flash at startup.
WAITED=0
while [ "$WAITED" -lt "${PEACHPULP_SERIAL_WAIT:-10}" ]; do
    compgen -G "/dev/ttyACM*" >/dev/null && break
    sleep 1
    WAITED=$((WAITED + 1))
done

# shellcheck disable=SC2086   # $MODE is deliberately unquoted (empty = no flag)
exec "$PY" run.py $MODE ${ARGS[@]+"${ARGS[@]}"}
