#!/usr/bin/env bash
#
# PEACH PULP GUI launcher — used by the desktop icon and the autostart entry.
# Path-independent: finds the repo from this script's own location, so it works
# whatever user or directory the repo lives in.
#
#   deploy/launch.sh              # fullscreen on the touchscreen
#   deploy/launch.sh --windowed   # a normal window (for poking around)
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GUI_DIR="$(dirname "$HERE")"            # .../gui
cd "$GUI_DIR"

PY="$GUI_DIR/.venv/bin/python"
if [ ! -x "$PY" ]; then
    echo "no venv at $PY — run deploy/pi-bootstrap.sh first" >&2
    exit 1
fi

MODE=--fullscreen
[ "${1:-}" = "--windowed" ] && { MODE=; shift; }

# Give the Octopus a few seconds to enumerate after a cold boot. The GUI retries
# on its own, so this only avoids a brief "disconnected" flash at startup.
for _ in $(seq 1 10); do
    compgen -G "/dev/ttyACM*" >/dev/null && break
    sleep 1
done

exec "$PY" run.py $MODE "$@"
