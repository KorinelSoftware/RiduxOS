#!/bin/sh
#
# ridux-app: Ridux app manager.
#
# A small CLI that wraps pkg(8) plus a few Linuxulator helpers so that
# the rest of the Ridux UI / first-boot scripts have a single contract
# for "install, query, diagnose" — independent of whether the package
# happens to be a native FreeBSD port or a Linux binary running under
# the Ridux Linux ABI runtime.
#
# Usage:
#   ridux-app install <pkg> [<pkg>...]
#   ridux-app remove  <pkg> [<pkg>...]
#   ridux-app list
#   ridux-app info <pkg>
#   ridux-app ensure-linux        # load Linux ABI runtime + linux_base
#   ridux-app doctor              # full health snapshot to stdout
#   ridux-app help

set -eu

cmd="${1:-help}"
shift 2>/dev/null || true

pkg_install() {
    [ "$#" -ge 1 ] || { echo "ridux-app install: need a package name" >&2; return 2; }
    env ASSUME_ALWAYS_YES=yes pkg install -y "$@"
}

pkg_remove() {
    [ "$#" -ge 1 ] || { echo "ridux-app remove: need a package name" >&2; return 2; }
    env ASSUME_ALWAYS_YES=yes pkg delete -y "$@"
}

ensure_linux() {
    # Load the Linuxulator if not already loaded.
    for mod in linux64 linsysfs linprocfs; do
        kldstat -q -m "$mod" 2>/dev/null || kldload "$mod" 2>/dev/null || true
    done
    # Make sure Linux base userland is present (Rocky 9 first, CentOS 7 fallback).
    if ! pkg info -e linux_base-rl9 >/dev/null 2>&1 \
       && ! pkg info -e linux_base-c7 >/dev/null 2>&1; then
        env ASSUME_ALWAYS_YES=yes pkg install -y linux_base-rl9 \
            || env ASSUME_ALWAYS_YES=yes pkg install -y linux_base-c7 \
            || true
    fi
    # Persist across reboots.
    sysrc linux_enable=YES >/dev/null 2>&1 || true
    service linux start  >/dev/null 2>&1 || true
}

doctor() {
    echo "=== Ridux doctor ==="
    echo "kernel    : $(uname -mrs)"
    echo "hostname  : $(hostname)"
    echo "uptime    : $(uptime)"
    echo
    echo "-- Linuxulator --"
    if kldstat | grep -qi linux; then
        kldstat | grep -i linux
    else
        echo "  no linux modules loaded"
    fi
    echo "  linux_enable: $(sysrc -n linux_enable 2>/dev/null || echo unset)"
    echo "  /compat/linux: $(test -d /compat/linux && echo present || echo missing)"
    echo
    echo "-- Browsers --"
    for b in firefox chromium google-chrome chrome; do
        if command -v "$b" >/dev/null 2>&1; then
            echo "  $b: $(command -v "$b")"
        fi
    done
    for cand in \
        /compat/linux/usr/bin/google-chrome-stable \
        /compat/linux/opt/google/chrome/chrome \
        /usr/local/share/linux-chrome/chrome
    do
        [ -x "$cand" ] && echo "  linux-chrome: $cand"
    done
    echo
    echo "-- Display stack --"
    echo "  Xorg     : $(command -v Xorg || command -v X || echo missing)"
    echo "  startx   : $(command -v startx || echo missing)"
    echo "  dbus     : $(service dbus status 2>&1 | head -1 || echo unknown)"
    echo
    echo "-- Ridux UI --"
    if [ -x /usr/local/bin/ridux-ui ]; then
        echo "  /usr/local/bin/ridux-ui : present"
    else
        echo "  /usr/local/bin/ridux-ui : MISSING (will fall back to ridux-browser)"
    fi
}

case "$cmd" in
    install)        pkg_install "$@" ;;
    remove|delete)  pkg_remove  "$@" ;;
    list)           pkg info -a ;;
    info)
        [ "$#" -ge 1 ] || { echo "ridux-app info: need a package name" >&2; exit 2; }
        pkg info "$@"
        ;;
    ensure-linux)   ensure_linux ;;
    doctor)         doctor ;;
    help|-h|--help|"")
        cat <<EOF
ridux-app: Ridux app manager

Subcommands:
  install <pkg> [<pkg>...]   install via pkg
  remove  <pkg> [<pkg>...]   remove via pkg
  list                        list installed packages
  info <pkg>                  show package info
  ensure-linux                load Linuxulator + Linux base userland
  doctor                      print full system health snapshot
  help                        this message
EOF
        ;;
    *)
        echo "ridux-app: unknown subcommand '$cmd'" >&2
        exit 2
        ;;
esac
