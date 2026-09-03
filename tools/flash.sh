#!/usr/bin/env bash
#
# Jumper-free firmware flash for the Octopus.
#
# Tells the running firmware to reboot into the ROM bootloader (the "DFU"
# command), waits for the DFU device, flashes 0x08000000, and lets it boot the
# new image. Same USB cable, no BOOT0 jumper, no reset button.
#
#   tools/flash.sh [firmware.bin] [/dev/ttyACM0]
#
# Defaults: .pio/build/octopus_f446/firmware.bin, first ttyACM*/usbmodem* found.
# If the board is already in DFU (or bricked — BOOT0 jumper + reset), just run it
# with no serial port and it flashes straight away.
#
set -euo pipefail

BIN="${1:-.pio/build/octopus_f446/firmware.bin}"
PORT="${2:-}"

[ -f "$BIN" ] || { echo "no firmware at: $BIN" >&2; exit 1; }

DFU_UTIL="$(command -v dfu-util || true)"
[ -z "$DFU_UTIL" ] && [ -x "$HOME/.platformio/packages/tool-dfuutil/bin/dfu-util" ] \
    && DFU_UTIL="$HOME/.platformio/packages/tool-dfuutil/bin/dfu-util"
[ -z "$DFU_UTIL" ] && { echo "dfu-util not found — 'sudo apt install dfu-util' or 'brew install dfu-util'" >&2; exit 1; }

if [ -z "$PORT" ]; then
    for p in /dev/ttyACM* /dev/cu.usbmodem* /dev/tty.usbmodem*; do
        [ -e "$p" ] && { PORT="$p"; break; }
    done
fi

if [ -n "$PORT" ] && [ -e "$PORT" ]; then
    echo "== $PORT: asking the firmware to enter DFU =="
    # raw mode, no hangup-on-close, then send the command
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
