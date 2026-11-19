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
# So the desktop icon opens the app straight away (no pcmanfm "Execute / Execute in
# Terminal / Open / Cancel" dialog) each entry is marked trusted (gio metadata::trusted,
# retried on a private D-Bus session when there is none, e.g. a remote shell), and
# pcmanfm's "Don't ask options on launch executable file" (quick_exec=1 in
# ~/.config/libfm/libfm.conf) is switched on. PEACHPULP_NO_QUICK_EXEC=1 skips the latter.
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
LIBFM_CONF="${XDG_CONFIG_HOME:-$HOME/.config}/libfm/libfm.conf"

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

# Mark a .desktop file trusted so pcmanfm launches it without asking. gio needs the
# session D-Bus (gvfs metadata daemon); from a remote shell there is none, so retry on a
# private one. Never fatal, never silent.
trust_entry() {
    local f="$1" ok=0 err=""
    if ! command -v gio >/dev/null 2>&1; then
        echo "  NOT marked trusted (gio not found): $f"
        return 0
    fi
    if err="$(gio set "$f" metadata::trusted true 2>&1)"; then ok=1; fi
    if { [ "$ok" = 0 ] || [ -z "${DBUS_SESSION_BUS_ADDRESS:-}" ]; } \
       && command -v dbus-run-session >/dev/null 2>&1; then
        if err="$(dbus-run-session -- gio set "$f" metadata::trusted true 2>&1)"; then ok=1; fi
    fi
    if [ "$ok" = 1 ]; then
        echo "  marked trusted: $f"
    else
        echo "  NOT marked trusted (gio set failed: $(printf '%s' "$err" | head -n 1)): $f"
    fi
    return 0
}

# $1 = target path, $2 = optional line printed once it is written.
write_entry() {
    local dest="$1"
    mkdir -p "$(dirname "$dest")"
    sed "s#__LAUNCH__#$LAUNCH#" "$HERE/peach-pulp.desktop" > "$dest"
    chmod +x "$dest"
    [ -z "${2:-}" ] || echo "$2"
    trust_entry "$dest"
}

# pcmanfm's "Don't ask options on launch executable file" is quick_exec=1 under [config]
# in libfm.conf. Add or replace just that line (everything else is kept as is), exactly
# one copy however often this runs. Temp file + mv; plain awk, no `sed -i` (BSD vs GNU).
set_quick_exec() {
    local conf="$LIBFM_CONF" dir tmp existed=0
    if [ "${PEACHPULP_NO_QUICK_EXEC:-}" = 1 ]; then
        echo "quick_exec: skipped (PEACHPULP_NO_QUICK_EXEC=1), $conf untouched"
        return 0
    fi
    dir="$(dirname "$conf")"
    if ! mkdir -p "$dir" || ! tmp="$(mktemp "$dir/libfm.conf.XXXXXX")"; then
        echo "quick_exec: could not write in $dir, skipped"
        return 0
    fi
    if [ -s "$conf" ]; then
        if ! awk '
            function is_hdr(s) { return s ~ /^[ \t]*\[/ }
            function is_cfg(s) { return s ~ /^[ \t]*\[config\][ \t\r]*$/ }
            function is_qe(s)  { return s ~ /^[ \t]*quick_exec[ \t]*=/ }
            NR == FNR {                      # pass 1: any quick_exec key in [config]?
                if (is_hdr($0)) { g = is_cfg($0); if (g) hasgrp = 1 }
                else if (g && is_qe($0)) have = 1
                next
            }
            {                                # pass 2: copy, fixing only that key
                n++; last = $0
                if (is_hdr($0)) {
                    g = is_cfg($0)
                    print
                    if (g && !have && !done) { print "quick_exec=1"; done = 1 }
                    next
                }
                if (g && is_qe($0)) {
                    if (!done) { print "quick_exec=1"; done = 1 }
                    next
                }
                print
            }
            END {
                if (!hasgrp) {
                    if (n > 0 && last !~ /^[ \t\r]*$/) print ""
                    print "[config]"
                    print "quick_exec=1"
                }
            }' "$conf" "$conf" > "$tmp"; then
            rm -f "$tmp"
            echo "quick_exec: could not update $conf, skipped"
            return 0
        fi
    else
        printf '[config]\nquick_exec=1\n' > "$tmp"
    fi
    if [ -f "$conf" ] && cmp -s "$tmp" "$conf"; then
        rm -f "$tmp"
        echo "quick_exec=1 already set in $conf"
        return 0
    fi
    if [ -e "$conf" ]; then existed=1; fi
    chmod 644 "$tmp"
    if mv -f "$tmp" "$conf"; then
        if [ "$existed" = 1 ]; then
            echo "quick_exec=1 set in $conf (pcmanfm will not ask how to open the icon)"
        else
            echo "quick_exec=1 written to new $conf (pcmanfm will not ask how to open the icon)"
        fi
    else
        rm -f "$tmp"
        echo "quick_exec: could not replace $conf, skipped"
    fi
    return 0
}

# --off removes only our entries/autostart; libfm.conf (a desktop-wide preference that
# other launchers benefit from too) is deliberately left alone.
remove_all() {
    rm -f "$APPS_DIR/$NAME" "$AUTOSTART_DIR/$NAME" "$DESKTOP_DIR/$NAME"
    [ -f "$LABWC_AUTOSTART" ] && sed -i "\#$MARK#d" "$LABWC_AUTOSTART"
    echo "removed launcher entries + autostart."
}

case "${1:-}" in
  --off) remove_all; exit 0 ;;
esac

install_icon
write_entry "$APPS_DIR/$NAME" "app-menu entry written: $APPS_DIR/$NAME"
write_entry "$DESKTOP_DIR/$NAME" "desktop icon written:   $DESKTOP_DIR/$NAME"
set_quick_exec

if [ "${1:-}" = "--autostart" ]; then
    write_entry "$AUTOSTART_DIR/$NAME" "autostart entry written: $AUTOSTART_DIR/$NAME"  # XDG (wayfire, LXDE, GNOME…)
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
echo
echo "if the desktop icon still asks how to open it: log out and in (or reboot) once so the"
echo "desktop re-reads its config, or tick File Manager > Edit > Preferences > General >"
echo "\"Don't ask options on launch executable file\"."
