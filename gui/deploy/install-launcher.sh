#!/usr/bin/env bash
#
# Put a "PEACH PULP" launcher on the Pi: an app-menu entry + a desktop icon
# (with the peach icon), and (with --autostart) start it automatically on the
# touchscreen at boot. Idempotent: re-run any time, it rewrites the icon and the
# entries. No sudo needed. The launcher itself pulls the newest pushed code on
# every start (deploy/update-code.sh), so this only needs re-running when the
# icon/entry changes.
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
ICON_THEME_DIR="$HOME/.local/share/icons/hicolor"
ICON_DIR="$ICON_THEME_DIR/scalable/apps"
AUTOSTART_DIR="$HOME/.config/autostart"
DESKTOP_DIR="$(xdg-user-dir DESKTOP 2>/dev/null || echo "$HOME/Desktop")"
NAME=peach-pulp.desktop
LABWC_AUTOSTART="$HOME/.config/labwc/autostart"
MARK="# PEACH PULP launcher"

install_icon() {
    mkdir -p "$ICON_DIR"
    cp -f "$HERE/peach-pulp.svg" "$ICON_DIR/peach-pulp.svg"
    echo "icon installed: $ICON_DIR/peach-pulp.svg"
}

refresh_caches() {
    command -v gtk-update-icon-cache >/dev/null 2>&1 \
        && { gtk-update-icon-cache -f -t "$ICON_THEME_DIR" >/dev/null 2>&1 || true; }
    command -v update-desktop-database >/dev/null 2>&1 \
        && { update-desktop-database "$APPS_DIR" >/dev/null 2>&1 || true; }
    return 0
}

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

install_icon
write_entry "$APPS_DIR/$NAME"
echo "app-menu entry written: $APPS_DIR/$NAME"
write_entry "$DESKTOP_DIR/$NAME"
echo "desktop icon written:   $DESKTOP_DIR/$NAME"

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

refresh_caches

echo
echo "the icon updates the code itself on every launch (log: ~/.local/state/peachpulp/launch.log)"
echo "start it now without rebooting:  $LAUNCH"
