#!/usr/bin/env bash
#
# Flash Octopus firmware from the Pi — jumper-free.
#
# Sends the "DFU" command to the running firmware (it reboots into the STM32 ROM
# bootloader), waits for the DFU device, flashes 0x08000000, and boots the new
# image. Same USB cable, no BOOT0 jumper, no reset button.
#
#   deploy/flash-firmware.sh --latest [/dev/ttyACM0]   # download + flash the newest release
#   deploy/flash-firmware.sh [firmware.bin] [/dev/ttyACM0]
#
# --latest pulls the firmware.bin asset off the newest GitHub release of this
# repo (built + published from the dev machine: `pio run -e octopus_f446` then
# `gh release create fw-<sha> .pio/build/octopus_f446/firmware.bin --target
# breaking/octopus-stm32-fw`). Otherwise pass a local path (scp'd over, etc).
# Needs dfu-util:  sudo apt install -y dfu-util
#
# If the board is bricked: BOOT0 jumper (J75) + hold RST, then run this with no
# serial port — it skips the DFU command and flashes straight away.
#
set -euo pipefail

LATEST_URL="https://github.com/bscott711/PEACH-PULP/releases/latest/download/firmware.bin"

BIN="$HOME/firmware.bin"
PORT=""

if [ "${1:-}" = "--latest" ]; then
    shift
    BIN="${TMPDIR:-/tmp}/peach-firmware-latest.bin"
    echo "== downloading latest firmware release =="
    curl -fsSL -o "$BIN" "$LATEST_URL"
    echo "  -> $BIN ($(wc -c < "$BIN") bytes)"
elif [ -n "${1:-}" ]; then
    BIN="$1"; shift
fi
[ -n "${1:-}" ] && PORT="$1"

[ -f "$BIN" ] || { echo "no firmware at: $BIN  (pass a path, or --latest)" >&2; exit 1; }

DFU_UTIL="$(command -v dfu-util || true)"
[ -z "$DFU_UTIL" ] && { echo "dfu-util not found:  sudo apt install -y dfu-util" >&2; exit 1; }

if [ -z "$PORT" ]; then
    for p in /dev/ttyACM* /dev/cu.usbmodem* /dev/tty.usbmodem*; do
        [ -e "$p" ] && { PORT="$p"; break; }
    done
fi

if [ -n "$PORT" ] && [ -e "$PORT" ]; then
    echo "== $PORT: asking the firmware to enter DFU =="
    stty -F "$PORT" 115200 cs8 -cstopb -parenb -hupcl -echo raw 2>/dev/null \
        || stty -f "$PORT" 115200 cs8 -cstopb -parenb -hupcl -echo raw
    printf 'DFU\r\n' > "$PORT"
else
    echo "== no serial port — assuming the board is already in DFU =="
fi

echo "== waiting for the ROM bootloader (0483:df11) =="
for _ in $(seq 1 30); do
    "$DFU_UTIL" -l 2>/dev/null | grep -q "0483:df11" && { found=1; break; }
    sleep 0.5
done
[ "${found:-}" = 1 ] || { echo "DFU device never appeared. BOOT0 jumper (J75) + reset, then re-run." >&2; exit 1; }

echo "== flashing $BIN =="
"$DFU_UTIL" -a 0 -d 0483:df11 -s 0x08000000:leave -D "$BIN"
echo "== done — the board is booting the new firmware =="
