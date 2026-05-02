#!/bin/sh
#
# ridux-browser: Ridux browser launcher.
#
# Usage:
#   ridux-browser                   # default: firefox
#   ridux-browser firefox
#   ridux-browser chromium
#   ridux-browser chrome            # Linuxulator linux-chrome (if installed)
#   ridux-browser <url>             # opens URL in default browser
#
# This script lives at /usr/local/bin/ridux-browser on the installed
# system and is invoked by the Ridux UI ("Browser" tile / Firefox tile),
# by the autostart sequence, and by the user's shell as a CLI shortcut.
#
# All variants are real native binaries: firefox/chromium are FreeBSD
# native pkg installs, linux-chrome runs through the Linuxulator that
# the Ridux kernel ships enabled by default.

set -eu

target="${1:-firefox}"
shift 2>/dev/null || true

is_url() {
    case "$1" in
        http://*|https://*|file://*|ftp://*) return 0 ;;
        *)                                   return 1 ;;
    esac
}

run_firefox() {
    if command -v firefox >/dev/null 2>&1; then
        exec firefox "$@"
    fi
    return 1
}

run_chromium() {
    if command -v chromium >/dev/null 2>&1; then
        exec chromium --no-sandbox "$@"
    fi
    return 1
}

run_linux_chrome() {
    for cand in \
        /compat/linux/usr/bin/google-chrome-stable \
        /compat/linux/usr/bin/google-chrome \
        /compat/linux/opt/google/chrome/chrome \
        /usr/local/share/linux-chrome/chrome \
        /usr/local/bin/google-chrome
    do
        if [ -x "$cand" ]; then
            exec "$cand" --no-sandbox "$@"
        fi
    done
    return 1
}

if is_url "$target"; then
    set -- "$target" "$@"
    target="firefox"
fi

case "$target" in
    firefox)
        run_firefox "$@" || true
        run_chromium "$@" || true
        run_linux_chrome "$@" || true
        ;;
    chromium)
        run_chromium "$@" || true
        run_firefox "$@" || true
        ;;
    chrome|google-chrome|linux-chrome)
        run_linux_chrome "$@" || true
        run_chromium "$@" || true
        run_firefox "$@" || true
        ;;
    *)
        echo "ridux-browser: unknown target '$target'" >&2
        echo "ridux-browser: try 'firefox', 'chromium', 'chrome', or a URL" >&2
        exit 2
        ;;
esac

echo "ridux-browser: no usable browser found." >&2
echo "ridux-browser: try 'ridux-app install firefox' or 'ridux-app install chromium'." >&2
exit 1
