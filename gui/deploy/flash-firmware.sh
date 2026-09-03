#!/usr/bin/env bash
#
# Flash Octopus firmware from the Pi — jumper-free.
#
# Sends the "DFU" command to the running firmware (it reboots into the STM32 ROM
# bootloader), waits for the DFU device, flashes 0x08000000, and boots the new
# image. Same USB cable, no BOOT0 jumper, no reset button.
#
#   deploy/flash-firmware.sh [firmware.bin] [/dev/ttyACM0]
#
# Get firmware.bin onto the Pi first (built on the dev machine with
# `pio run -e octopus_f446`): scp it over, or download a GitHub release asset.
# Needs dfu-util:  sudo apt install -y dfu-util
#
# If the board is bricked: BOOT0 jumper (J75) + hold RST, then run this with no
# serial port — it skips the DFU command and flashes straight away.
#
set -euo pipefail

BIN="${1:-$HOME/firmware.bin}"
PORT="${2:-}"

[ -f "$BIN" ] || { echo "no firmware at: $BIN  (pass the path as arg 1)" >&2; exit 1; }

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
