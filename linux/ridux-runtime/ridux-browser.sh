#!/bin/sh
set -eu

pick_and_exec() {
  _candidates="$1"
  shift
  for _cmd in $_candidates; do
    if command -v "$_cmd" >/dev/null 2>&1; then
      exec "$_cmd" "$@"
    fi
  done
  return 1
}

target="${1:-chrome}"
if [ "$#" -gt 0 ]; then
  shift
fi

case "$target" in
  chrome|google-chrome)
    pick_and_exec "google-chrome-stable google-chrome chromium chromium-browser firefox" "$@" || {
      echo "ridux-browser: no browser found (expected chrome/chromium/firefox)." >&2
      exit 1
    }
    ;;
  chromium)
    pick_and_exec "chromium chromium-browser google-chrome-stable google-chrome firefox" "$@" || {
      echo "ridux-browser: chromium not found." >&2
      exit 1
    }
    ;;
  firefox)
    pick_and_exec "firefox firefox-esr" "$@" || {
      echo "ridux-browser: firefox not found." >&2
      exit 1
    }
    ;;
  discord)
    if command -v discord >/dev/null 2>&1; then
      exec discord "$@"
    fi
    if command -v flatpak >/dev/null 2>&1 && flatpak info com.discordapp.Discord >/dev/null 2>&1; then
      exec flatpak run com.discordapp.Discord "$@"
    fi
    echo "ridux-browser: discord not found." >&2
    exit 1
    ;;
  *)
    exec "$target" "$@"
    ;;
esac
