#!/usr/bin/env bash
#
# Put a "PEACH PULP" launcher on the Pi: an app-menu entry + a desktop icon,
# and (with --autostart) start it automatically on the touchscreen at boot.
# Re-run any time — it just rewrites the entries. No sudo needed.
#
#   deploy/install-launcher.sh                # menu + desktop icon
#   deploy/install-launcher.sh --autostart    # + start on boot
#   deploy/install-launcher.sh --off          # remove autostart + icons
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LAUNCH="$HERE/launch.sh"
chmod +x "$LAUNCH" "$HERE"/*.sh 2>/dev/null || true

APPS_DIR="$HOME/.local/share/applications"
AUTOSTART_DIR="$HOME/.config/autostart"
DESKTOP_DIR="$(xdg-user-dir DESKTOP 2>/dev/null || echo "$HOME/Desktop")"
NAME=peach-pulp.desktop
LABWC_AUTOSTART="$HOME/.config/labwc/autostart"
MARK="# PEACH PULP launcher"

write_entry() {
    local dest="$1"
    mkdir -p "$(dirname "$dest")"
    sed "s#__LAUNCH__#$LAUNCH#" "$HERE/peach-pulp.desktop" > "$dest"
    chmod +x "$dest"
    gio set "$dest" metadata::trusted true 2>/dev/null || true
}

remove_all() {
    rm -f "$APPS_DIR/$NAME" "$AUTOSTART_DIR/$NAME" "$DESKTOP_DIR/$NAME"
    [ -f "$LABWC_AUTOSTART" ] && sed -i "\#$MARK#d" "$LABWC_AUTOSTART"
    echo "removed launcher entries + autostart."
}

case "${1:-}" in
  --off) remove_all; exit 0 ;;
esac

write_entry "$APPS_DIR/$NAME"
write_entry "$DESKTOP_DIR/$NAME"
echo "menu + desktop icon installed."

if [ "${1:-}" = "--autostart" ]; then
    write_entry "$AUTOSTART_DIR/$NAME"                 # XDG (wayfire, LXDE, GNOME…)
    if [ -d "$(dirname "$LABWC_AUTOSTART")" ]; then    # labwc (Pi OS Bookworm default)
        touch "$LABWC_AUTOSTART"
        sed -i "\#$MARK#d" "$LABWC_AUTOSTART"
        echo "$LAUNCH &  $MARK" >> "$LABWC_AUTOSTART"
    fi
    echo "autostart enabled — the GUI will open on the touchscreen at boot."
    echo "  disable:  $HERE/install-launcher.sh --off"
fi

echo
echo "start it now without rebooting:  $LAUNCH"
