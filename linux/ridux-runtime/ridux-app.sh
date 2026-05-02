#!/bin/sh
set -eu

SELF="$(basename "$0")"
APT_PREPARED="0"

usage() {
  cat <<'EOF'
Ridux App Manager (Debian/Linux fast compatibility track)

Usage:
  ridux-app list
  ridux-app status [app]
  ridux-app install <app|all>
  ridux-app run <app> [args...]
  ridux-app doctor

Apps:
  firefox      Firefox browser
  chromium     Chromium browser
  chrome       Google Chrome stable (repo setup)
  discord      Discord (apt if available, otherwise Flatpak)
  telegram     Telegram Desktop
  vlc          Video player
  thunderbird  Mail client
  gimp         Image editor
  libreoffice  Office suite
  obs          OBS Studio
EOF
}

log() {
  printf '%s\n' "$*"
}

warn() {
  printf 'warning: %s\n' "$*" >&2
}

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

need_root() {
  if [ "$(id -u)" -ne 0 ]; then
    die "this action requires root. Use sudo or run as root."
  fi
}

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    die "required command not found: $1"
  fi
}

prepare_apt() {
  if [ "$APT_PREPARED" = "1" ]; then
    return 0
  fi
  need_cmd apt-get
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -y
  APT_PREPARED="1"
}

app_known() {
  case "$1" in
    firefox|chromium|chrome|discord|telegram|vlc|thunderbird|gimp|libreoffice|obs)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

all_apps() {
  printf '%s\n' \
    firefox \
    chromium \
    chrome \
    discord \
    telegram \
    vlc \
    thunderbird \
    gimp \
    libreoffice \
    obs
}

app_package_candidates() {
  case "$1" in
    firefox)      printf '%s\n' "firefox firefox-esr" ;;
    chromium)     printf '%s\n' "chromium chromium-browser" ;;
    telegram)     printf '%s\n' "telegram-desktop" ;;
    vlc)          printf '%s\n' "vlc" ;;
    thunderbird)  printf '%s\n' "thunderbird" ;;
    gimp)         printf '%s\n' "gimp" ;;
    libreoffice)  printf '%s\n' "libreoffice" ;;
    obs)          printf '%s\n' "obs-studio obs" ;;
    discord)      printf '%s\n' "discord" ;;
    chrome)       printf '%s\n' "" ;;
    *)
      return 1
      ;;
  esac
}

app_command_candidates() {
  case "$1" in
    firefox)      printf '%s\n' "firefox firefox-esr" ;;
    chromium)     printf '%s\n' "chromium chromium-browser" ;;
    chrome)       printf '%s\n' "google-chrome-stable google-chrome chromium chromium-browser firefox firefox-esr" ;;
    discord)      printf '%s\n' "discord" ;;
    telegram)     printf '%s\n' "telegram-desktop telegram" ;;
    vlc)          printf '%s\n' "vlc" ;;
    thunderbird)  printf '%s\n' "thunderbird" ;;
    gimp)         printf '%s\n' "gimp" ;;
    libreoffice)  printf '%s\n' "libreoffice libreoffice7.6 libreoffice7.5" ;;
    obs)          printf '%s\n' "obs obs-studio" ;;
    *)
      return 1
      ;;
  esac
}

is_pkg_installed() {
  dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q "install ok installed"
}

install_first_available_pkg() {
  _candidate_list="$1"
  [ -n "$_candidate_list" ] || return 1
  for _pkg in $_candidate_list; do
    if is_pkg_installed "$_pkg"; then
      return 0
    fi
    if apt-get install -y "$_pkg"; then
      return 0
    fi
  done
  return 1
}

install_google_chrome() {
  prepare_apt
  apt-get install -y ca-certificates curl gnupg

  install -d -m 0755 /etc/apt/keyrings
  if [ ! -f /etc/apt/keyrings/google-chrome.gpg ]; then
    curl -fsSL https://dl.google.com/linux/linux_signing_key.pub \
      | gpg --dearmor -o /etc/apt/keyrings/google-chrome.gpg
    chmod 0644 /etc/apt/keyrings/google-chrome.gpg
  fi

  cat > /etc/apt/sources.list.d/google-chrome.list <<'EOF'
deb [arch=amd64 signed-by=/etc/apt/keyrings/google-chrome.gpg] https://dl.google.com/linux/chrome/deb/ stable main
EOF

  apt-get update -y
  apt-get install -y google-chrome-stable
}

ensure_flatpak() {
  prepare_apt
  apt-get install -y flatpak
  if ! flatpak remote-list --columns=name 2>/dev/null | grep -q '^flathub$'; then
    flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
  fi
}

install_discord() {
  prepare_apt
  if install_first_available_pkg "discord"; then
    return 0
  fi

  ensure_flatpak
  if flatpak install -y flathub com.discordapp.Discord; then
    return 0
  fi

  return 1
}

first_command_found() {
  _candidates="$1"
  for _cmd in $_candidates; do
    if command -v "$_cmd" >/dev/null 2>&1; then
      printf '%s\n' "$_cmd"
      return 0
    fi
  done
  return 1
}

discord_installed() {
  if command -v discord >/dev/null 2>&1; then
    return 0
  fi
  if command -v flatpak >/dev/null 2>&1 && flatpak info com.discordapp.Discord >/dev/null 2>&1; then
    return 0
  fi
  return 1
}

install_app() {
  _app="$1"
  app_known "$_app" || die "unknown app: $_app"

  need_root

  case "$_app" in
    chrome)
      install_google_chrome || die "failed to install google-chrome-stable"
      ;;
    discord)
      install_discord || die "failed to install discord (apt/flatpak)"
      ;;
    *)
      prepare_apt
      _pkgs="$(app_package_candidates "$_app")"
      install_first_available_pkg "$_pkgs" || die "package installation failed for app '$_app' (candidates: $_pkgs)"
      ;;
  esac

  log "[ok] installed: $_app"
}

show_status_for_app() {
  _app="$1"
  app_known "$_app" || die "unknown app: $_app"

  if [ "$_app" = "discord" ]; then
    if command -v discord >/dev/null 2>&1; then
      printf '%-12s installed (%s)\n' "$_app" "discord"
      return 0
    fi
    if command -v flatpak >/dev/null 2>&1 && flatpak info com.discordapp.Discord >/dev/null 2>&1; then
      printf '%-12s installed (%s)\n' "$_app" "flatpak:com.discordapp.Discord"
      return 0
    fi
    printf '%-12s missing\n' "$_app"
    return 0
  fi

  _cmds="$(app_command_candidates "$_app")"
  _found="$(first_command_found "$_cmds" || true)"
  if [ -n "$_found" ]; then
    printf '%-12s installed (%s)\n' "$_app" "$_found"
  else
    printf '%-12s missing\n' "$_app"
  fi
}

show_status() {
  _one="${1:-}"
  printf 'Ridux app status on %s\n' "$(uname -sr)"
  printf 'display=%s\n' "${DISPLAY:-<none>}"
  echo

  if [ -n "$_one" ]; then
    show_status_for_app "$_one"
    return 0
  fi

  for _app in $(all_apps); do
    show_status_for_app "$_app"
  done
}

run_app() {
  _app="$1"
  shift

  case "$_app" in
    chrome|chromium|firefox)
      exec /usr/local/bin/ridux-browser "$_app" "$@"
      ;;
    discord)
      if command -v discord >/dev/null 2>&1; then
        exec discord "$@"
      fi
      if command -v flatpak >/dev/null 2>&1 && flatpak info com.discordapp.Discord >/dev/null 2>&1; then
        exec flatpak run com.discordapp.Discord "$@"
      fi
      die "discord is not installed. Run: sudo ridux-app install discord"
      ;;
  esac

  if app_known "$_app"; then
    _cmds="$(app_command_candidates "$_app")"
    _cmd="$(first_command_found "$_cmds" || true)"
    if [ -z "${_cmd:-}" ]; then
      die "app '$_app' is not installed. Run: sudo ridux-app install $_app"
    fi
    exec "$_cmd" "$@"
  fi

  exec "$_app" "$@"
}

doctor() {
  echo "=== Ridux App Doctor ==="
  echo "host: $(uname -a)"
  echo
  show_status "${1:-}"
  echo

  if [ -z "${DISPLAY:-}" ]; then
    echo "hint: DISPLAY is not set. Start an X session (startx) before GUI apps."
  fi
  if ! command -v apt-get >/dev/null 2>&1; then
    echo "hint: apt-get is missing. This script expects a Debian/Ubuntu style base."
  fi
  if ! command -v firefox >/dev/null 2>&1 && ! command -v firefox-esr >/dev/null 2>&1 && ! command -v chromium >/dev/null 2>&1; then
    echo "hint: install at least one browser:"
    echo "  sudo ridux-app install firefox"
    echo "  sudo ridux-app install chromium"
  fi
  if ! command -v google-chrome-stable >/dev/null 2>&1; then
    echo "hint: install Google Chrome:"
    echo "  sudo ridux-app install chrome"
  fi
  if ! discord_installed; then
    echo "hint: install Discord:"
    echo "  sudo ridux-app install discord"
  fi
}

list_apps_cmd() {
  cat <<'EOF'
firefox      browser (firefox/firefox-esr)
chromium     browser (chromium/chromium-browser)
chrome       google-chrome-stable
discord      discord (apt or flatpak)
telegram     messaging
vlc          media player
thunderbird  mail client
gimp         image editor
libreoffice  office suite
obs          streaming/recording
EOF
}

cmd="${1:-help}"

case "$cmd" in
  help|-h|--help)
    usage
    ;;
  list)
    list_apps_cmd
    ;;
  status)
    shift || true
    show_status "${1:-}"
    ;;
  install)
    shift || true
    [ $# -ge 1 ] || die "install requires <app|all>"
    if [ "$1" = "all" ]; then
      _failed=0
      for _app in $(all_apps); do
        if ! install_app "$_app"; then
          warn "failed installing $_app"
          _failed=1
        fi
      done
      [ "$_failed" -eq 0 ] || exit 1
    else
      install_app "$1"
    fi
    ;;
  run)
    shift || true
    [ $# -ge 1 ] || die "run requires <app>"
    _app="$1"
    shift || true
    run_app "$_app" "$@"
    ;;
  doctor)
    shift || true
    doctor "${1:-}"
    ;;
  *)
    die "unknown command: $cmd (run '$SELF help')"
    ;;
esac
