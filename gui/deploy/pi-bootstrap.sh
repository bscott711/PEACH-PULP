#!/usr/bin/env bash
#
# One-shot setup of the PEACH PULP GUI on a Raspberry Pi (OS Bookworm, 64-bit).
# Clones/updates the public repo, builds a venv, installs PySide6 + pyserial,
# runs the test suite, and checks that Qt can import.
#
# Run it directly:
#   curl -fsSL https://raw.githubusercontent.com/bscott711/PEACH-PULP/gui/pi-pyside6/gui/deploy/pi-bootstrap.sh | bash
# or with options:
#   REPO_DIR=~/dev/PEACH-PULP BRANCH=gui/pi-pyside6 bash pi-bootstrap.sh
#
set -euo pipefail

REPO_URL="${REPO_URL:-https://github.com/bscott711/PEACH-PULP.git}"
REPO_DIR="${REPO_DIR:-$HOME/PEACH-PULP}"
BRANCH="${BRANCH:-gui/pi-pyside6}"

say() { printf '\n\033[1;36m== %s\033[0m\n' "$*"; }

say "system packages"
sudo apt-get update -qq
# python venv + headers, git, and the xcb libs PySide6's Qt platform plugin needs
sudo apt-get install -y -qq \
    git python3-venv python3-dev \
    libxcb-cursor0 libxcb-xinerama0 libxcb-icccm4 libxcb-image0 \
    libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 libxcb-shape0 \
    libxkbcommon-x11-0 libegl1 libgl1

say "repo → $REPO_DIR ($BRANCH)"
if [ -d "$REPO_DIR/.git" ]; then
    git -C "$REPO_DIR" fetch --all --quiet
    git -C "$REPO_DIR" switch "$BRANCH" --quiet 2>/dev/null || git -C "$REPO_DIR" switch -c "$BRANCH" --track "origin/$BRANCH" --quiet
    git -C "$REPO_DIR" pull --ff-only --quiet
else
    git clone --quiet "$REPO_URL" "$REPO_DIR"
    git -C "$REPO_DIR" switch "$BRANCH" --quiet
fi
echo "  at $(git -C "$REPO_DIR" rev-parse --short HEAD)"

say "venv + deps"
cd "$REPO_DIR/gui"
python3 -m venv .venv
.venv/bin/pip install --upgrade pip --quiet
.venv/bin/pip install -r requirements.txt --quiet
echo "  installed: $(.venv/bin/python -c 'import PySide6, serial; print("PySide6", PySide6.__version__, "/ pyserial", serial.__version__)')"

say "serial permissions"
if ! id -nG "$USER" | grep -qw dialout; then
    sudo usermod -aG dialout "$USER"
    echo "  added $USER to 'dialout' — reboot (or re-login) before using real hardware"
else
    echo "  ok ($USER in dialout)"
fi

say "self-test (no hardware, no display)"
.venv/bin/python -m pytest -q
QT_QPA_PLATFORM=offscreen .venv/bin/python -c "import PySide6.QtWidgets as w; a=w.QApplication([]); print('  Qt import + QApplication OK (offscreen)')"

say "generating systemd unit → /tmp/peach-pulp-gui.service"
RUNTIME_DIR="/run/user/$(id -u)"
sed -e "s#^User=.*#User=$USER#" \
    -e "s#^WorkingDirectory=.*#WorkingDirectory=$REPO_DIR/gui#" \
    -e "s#^Environment=XDG_RUNTIME_DIR=.*#Environment=XDG_RUNTIME_DIR=$RUNTIME_DIR#" \
    -e "s#^ExecStart=.*#ExecStart=$REPO_DIR/gui/.venv/bin/python run.py --fullscreen#" \
    "$REPO_DIR/gui/deploy/peach-pulp-gui.service" > /tmp/peach-pulp-gui.service
echo "  review it, then: sudo cp /tmp/peach-pulp-gui.service /etc/systemd/system/"

say "done"
cat <<EOF
Try it now:
  cd $REPO_DIR/gui
  .venv/bin/python run.py --sim                 # windowed, simulated firmware
  .venv/bin/python run.py --sim --fullscreen    # kiosk on the touchscreen
  .venv/bin/python run.py --port /dev/ttyACM0   # real Octopus (once it's flashed)

Autostart on the touchscreen:
  sudo cp /tmp/peach-pulp-gui.service /etc/systemd/system/
  sudo systemctl enable --now peach-pulp-gui
EOF
