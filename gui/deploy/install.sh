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
echo "Autostart on the touchscreen:"
echo "  sudo cp deploy/peach-pulp-gui.service /etc/systemd/system/"
echo "  # edit paths/User in that file if the repo isn't at /home/pi/PEACH-PULP"
echo "  sudo systemctl enable --now peach-pulp-gui"
