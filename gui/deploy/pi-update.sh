#!/usr/bin/env bash
#
# Pull the latest PEACH PULP GUI onto the Pi and restart it.
# Safe to run any time — it force-syncs the deploy checkout to origin,
# refreshes the venv, and (if installed) restarts the systemd service.
#
# One-liner (run this whenever you want the newest code):
#   curl -fsSL https://raw.githubusercontent.com/bscott711/PEACH-PULP/gui/pi-pyside6/gui/deploy/pi-update.sh | bash
#
# or, once cloned:
#   ~/PEACH-PULP/gui/deploy/pi-update.sh
#
set -euo pipefail

REPO_DIR="${REPO_DIR:-$HOME/PEACH-PULP}"
BRANCH="${BRANCH:-gui/pi-pyside6}"

if [ ! -d "$REPO_DIR/.git" ]; then
    echo "no repo at $REPO_DIR — run pi-bootstrap.sh first" >&2
    exit 1
fi

echo "== syncing $REPO_DIR to origin/$BRANCH =="
git -C "$REPO_DIR" fetch --all --quiet --prune
git -C "$REPO_DIR" switch "$BRANCH" --quiet 2>/dev/null \
    || git -C "$REPO_DIR" switch -c "$BRANCH" --track "origin/$BRANCH" --quiet
git -C "$REPO_DIR" reset --hard "origin/$BRANCH" --quiet
echo "  now at $(git -C "$REPO_DIR" log -1 --format='%h  %s')"

echo "== refreshing venv =="
cd "$REPO_DIR/gui"
if [ ! -x .venv/bin/python ]; then
    python3 -m venv .venv
    .venv/bin/pip install --upgrade pip --quiet
fi
.venv/bin/pip install -r requirements.txt --quiet
echo "  $(.venv/bin/python -c 'import PySide6, serial; print("PySide6", PySide6.__version__, "/ pyserial", serial.__version__)')"

echo "== self-test =="
.venv/bin/python -m pytest -q

if systemctl list-unit-files 2>/dev/null | grep -q '^peach-pulp-gui\.service'; then
    echo "== restarting peach-pulp-gui.service =="
    sudo systemctl restart peach-pulp-gui
    echo "  $(systemctl is-active peach-pulp-gui)"
else
    echo "== service not installed — start it yourself: =="
    echo "  cd $REPO_DIR/gui && .venv/bin/python run.py --sim --fullscreen"
fi

echo "done."
