#!/usr/bin/env bash
# Set up the PEACH PULP GUI on a Raspberry Pi. Run from the gui/ directory.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."   # -> gui/

echo "== venv =="
python3 -m venv .venv
.venv/bin/pip install --upgrade pip
.venv/bin/pip install -r requirements.txt

echo
echo "== serial permissions =="
if ! id -nG "$USER" | grep -qw dialout; then
    sudo usermod -aG dialout "$USER"
    echo "added $USER to 'dialout' — log out/in (or reboot) for it to take effect"
fi

echo
echo "Test it:   .venv/bin/python run.py --sim"
echo "Real HW:   .venv/bin/python run.py            (autodetects /dev/ttyACM*)"
echo
echo "Launcher icon + autostart on the touchscreen (no sudo, path-independent):"
echo "  deploy/install-launcher.sh              # app-menu + desktop icon"
echo "  deploy/install-launcher.sh --autostart  # + open on boot"
echo "  deploy/install-launcher.sh --off        # undo"
