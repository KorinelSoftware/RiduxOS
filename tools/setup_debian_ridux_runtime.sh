#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -d "${SCRIPT_DIR}/../linux/ridux-runtime" ]]; then
  RUNTIME_DIR="${SCRIPT_DIR}/../linux/ridux-runtime"
elif [[ -d "${SCRIPT_DIR}/ridux-runtime" ]]; then
  RUNTIME_DIR="${SCRIPT_DIR}/ridux-runtime"
else
  RUNTIME_DIR="${SCRIPT_DIR}/../linux/ridux-runtime"
fi
RIDUX_USER="${RIDUX_USER:-ridux}"
RIDUX_FAST_MODE="${RIDUX_FAST_MODE:-no}"
INSTALL_CHROME="${INSTALL_CHROME:-no}"
INSTALL_DISCORD="${INSTALL_DISCORD:-no}"
SKIP_PACKAGES="${SKIP_PACKAGES:-no}"

usage() {
  cat <<'EOF'
Usage:
  tools/setup_debian_ridux_runtime.sh [options]

Options:
  --user <name>             Username for tty autologin session (default: ridux)
  --runtime-dir <path>      Runtime source directory (default: linux/ridux-runtime)
  --fast                    Install only core UI/runtime packages
  --with-chrome             Install google-chrome-stable via ridux-app
  --with-discord            Install discord via ridux-app
  --skip-packages           Skip apt package installation
  -h, --help                Show this help

Environment overrides:
  RIDUX_USER, RIDUX_FAST_MODE, INSTALL_CHROME, INSTALL_DISCORD, SKIP_PACKAGES
EOF
}

log() {
  printf '[ridux-debian-setup] %s\n' "$*"
}

die() {
  printf '[ridux-debian-setup] error: %s\n' "$*" >&2
  exit 1
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --user)
      RIDUX_USER="$2"
      shift 2
      ;;
    --runtime-dir)
      RUNTIME_DIR="$2"
      shift 2
      ;;
    --fast)
      RIDUX_FAST_MODE="yes"
      shift
      ;;
    --with-chrome)
      INSTALL_CHROME="yes"
      shift
      ;;
    --with-discord)
      INSTALL_DISCORD="yes"
      shift
      ;;
    --skip-packages)
      SKIP_PACKAGES="yes"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "unknown option: $1"
      ;;
  esac
done

if [[ "$(id -u)" -ne 0 ]]; then
  die "run as root (sudo)."
fi

if [[ ! -d "$RUNTIME_DIR" ]]; then
  die "runtime directory not found: $RUNTIME_DIR"
fi

for f in ridux-browser.sh ridux-app.sh ridux-compat.sh ridux-ui.c ridux-flush.c ridux-flush.h; do
  [[ -f "$RUNTIME_DIR/$f" ]] || die "missing runtime file: $RUNTIME_DIR/$f"
done

if [[ ! "$RIDUX_USER" =~ ^[a-z_][a-z0-9_-]*$ ]]; then
  die "invalid user name: $RIDUX_USER"
fi

case "$RIDUX_FAST_MODE" in
  yes|YES|true|TRUE|1) RIDUX_FAST_MODE="yes" ;;
  no|NO|false|FALSE|0) RIDUX_FAST_MODE="no" ;;
  *) die "RIDUX_FAST_MODE must be yes/no" ;;
esac

case "$INSTALL_CHROME" in
  yes|YES|true|TRUE|1) INSTALL_CHROME="yes" ;;
  no|NO|false|FALSE|0) INSTALL_CHROME="no" ;;
  *) die "INSTALL_CHROME must be yes/no" ;;
esac

case "$INSTALL_DISCORD" in
  yes|YES|true|TRUE|1) INSTALL_DISCORD="yes" ;;
  no|NO|false|FALSE|0) INSTALL_DISCORD="no" ;;
  *) die "INSTALL_DISCORD must be yes/no" ;;
esac

case "$SKIP_PACKAGES" in
  yes|YES|true|TRUE|1) SKIP_PACKAGES="yes" ;;
  no|NO|false|FALSE|0) SKIP_PACKAGES="no" ;;
  *) die "SKIP_PACKAGES must be yes/no" ;;
esac

need_cmd apt-get
need_cmd install

export DEBIAN_FRONTEND=noninteractive

if [[ "$SKIP_PACKAGES" = "no" ]]; then
  log "updating apt metadata"
  apt-get update -y

  base_pkgs=(
    sudo
    xorg
    xinit
    xterm
    dbus-user-session
    ca-certificates
    curl
    gnupg
    build-essential
    pkg-config
    libx11-dev
    file
    iproute2
    net-tools
    procps
    psmisc
    lsof
    strace
    lsb-release
    pciutils
    usbutils
  )

  log "installing base packages"
  apt-get install -y "${base_pkgs[@]}"

  if [[ "$RIDUX_FAST_MODE" = "yes" ]]; then
    log "fast mode enabled: installing only firefox"
    apt-get install -y firefox-esr || true
  else
    log "standard mode: installing firefox + chromium"
    apt-get install -y firefox-esr || true
    apt-get install -y chromium || apt-get install -y chromium-browser || true
  fi
fi

if ! id -u "$RIDUX_USER" >/dev/null 2>&1; then
  log "creating user $RIDUX_USER"
  useradd -m -s /bin/bash "$RIDUX_USER"
fi

for grp in sudo audio video render input netdev; do
  if getent group "$grp" >/dev/null 2>&1; then
    usermod -aG "$grp" "$RIDUX_USER" || true
  fi
done

log "installing ridux runtime commands to /usr/local/bin"
install -d -m 0755 /usr/local/bin
install -m 0755 "$RUNTIME_DIR/ridux-browser.sh" /usr/local/bin/ridux-browser
install -m 0755 "$RUNTIME_DIR/ridux-app.sh" /usr/local/bin/ridux-app
install -m 0755 "$RUNTIME_DIR/ridux-compat.sh" /usr/local/bin/ridux-compat

for compat_cmd in \
  abi6 pidfd io_uring statx sysplus \
  lsblk route mount lsdev catproc syscalls \
  threads ss elf64 pmm tasks paging shm timers fds \
  realsys mmaps procfs dynlink libc dlsym heap bsd b64 rng browser
do
  ln -sf /usr/local/bin/ridux-compat "/usr/local/bin/${compat_cmd}"
done

log "compiling ridux-ui"
if command -v cc >/dev/null 2>&1; then
  if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists x11 >/dev/null 2>&1; then
    cc -O2 \
      "$RUNTIME_DIR/ridux-ui.c" \
      "$RUNTIME_DIR/ridux-flush.c" \
      -o /usr/local/bin/ridux-ui \
      $(pkg-config --cflags --libs x11) || true
  else
    cc -O2 \
      "$RUNTIME_DIR/ridux-ui.c" \
      "$RUNTIME_DIR/ridux-flush.c" \
      -o /usr/local/bin/ridux-ui \
      -lX11 || true
  fi
fi

if [[ ! -x /usr/local/bin/ridux-ui ]]; then
  log "ridux-ui binary not available, installing fallback launcher"
  cat > /usr/local/bin/ridux-ui <<'EOF'
#!/bin/sh
exec /usr/local/bin/ridux-browser chrome --start-maximized
EOF
  chmod +x /usr/local/bin/ridux-ui
fi

home_dir="$(getent passwd "$RIDUX_USER" | cut -d: -f6)"
[[ -n "$home_dir" ]] || die "unable to resolve home dir for user $RIDUX_USER"

log "configuring X session for $RIDUX_USER"
cat > "${home_dir}/.xinitrc" <<'EOF'
#!/bin/sh
xset -dpms s off s noblank || true
xsetroot -solid "#101826" || true
exec /usr/local/bin/ridux-ui
EOF

cat > "${home_dir}/.profile" <<EOF
if [ -z "\${DISPLAY:-}" ] && [ "\$(tty)" = "/dev/tty1" ]; then
  exec startx
fi
EOF

chown "$RIDUX_USER:$RIDUX_USER" "${home_dir}/.xinitrc" "${home_dir}/.profile"
chmod 0755 "${home_dir}/.xinitrc"
chmod 0644 "${home_dir}/.profile"

log "configuring tty1 autologin"
install -d -m 0755 /etc/systemd/system/getty@tty1.service.d
cat > /etc/systemd/system/getty@tty1.service.d/autologin.conf <<EOF
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin ${RIDUX_USER} --noclear %I \$TERM
Type=idle
EOF

if command -v systemctl >/dev/null 2>&1; then
  systemctl daemon-reload || true
  systemctl restart getty@tty1.service || true
fi

if [[ "$INSTALL_CHROME" = "yes" ]]; then
  log "installing Chrome via ridux-app"
  /usr/local/bin/ridux-app install chrome || true
fi

if [[ "$INSTALL_DISCORD" = "yes" ]]; then
  log "installing Discord via ridux-app"
  /usr/local/bin/ridux-app install discord || true
fi

log "done"
log "next: reboot and tty1 will autologin into Ridux UI."
