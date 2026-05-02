#!/usr/bin/env bash
set -euo pipefail

RIDUX_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_ISO="${RIDUX_ROOT}/build/RiduxOS-Debian-Live-Ridux.iso"
RUNTIME_DIR="${RIDUX_ROOT}/linux/ridux-runtime"
DIST="${DIST:-stable}"
WITH_CHROME="${WITH_CHROME:-no}"
INCREMENTAL="${INCREMENTAL:-yes}"
SKIP_HOST_DEPS="${SKIP_HOST_DEPS:-no}"
BOOT_SPLASH="${RIDUX_ROOT}/RiduxIcons/Wallpaper/DefaultWallpaper.png"
ORIG_ARGS=("$@")

if [[ "${RIDUX_ROOT}" == /mnt/* ]]; then
  WORK_DIR="${WORK_DIR:-/var/tmp/ridux-debian-live-work}"
else
  WORK_DIR="${WORK_DIR:-${RIDUX_ROOT}/build/debian-live-work}"
fi

usage() {
  cat <<'EOF'
Usage:
  tools/build_debian_live_ridux_iso.sh [options]

Options:
  --output <path>       Output ISO path
  --work-dir <path>     Working directory (default: build/debian-live-work)
  --dist <name>         Debian suite (default: stable)
  --incremental         Reuse existing work dir/chroot cache for faster rebuilds (default)
  --fresh               Rebuild from scratch and discard cached live-build state
  --skip-host-deps      Skip apt update/install of host build dependencies
  --with-chrome         Attempt to install google-chrome-stable in the live image
  -h, --help            Show this help

Notes:
- Run inside Linux/WSL with root privileges.
- Produces a live ISO (iso-hybrid) intended for USB boot on another PC.
- Boots directly into Ridux UI via tty1 autologin + startx.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --output)
      OUT_ISO="$2"
      shift 2
      ;;
    --work-dir)
      WORK_DIR="$2"
      shift 2
      ;;
    --dist)
      DIST="$2"
      shift 2
      ;;
    --incremental)
      INCREMENTAL="yes"
      shift
      ;;
    --fresh)
      INCREMENTAL="no"
      shift
      ;;
    --skip-host-deps)
      SKIP_HOST_DEPS="yes"
      shift
      ;;
    --with-chrome)
      WITH_CHROME="yes"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "[debian-live-ridux] unknown option: $1" >&2
      exit 2
      ;;
  esac
done

if [[ "$OUT_ISO" != /* ]]; then
  OUT_ISO="${RIDUX_ROOT}/${OUT_ISO#./}"
fi

if [[ "$WORK_DIR" != /* ]]; then
  WORK_DIR="${RIDUX_ROOT}/${WORK_DIR#./}"
fi

if [[ "$(id -u)" -ne 0 ]]; then
  echo "[debian-live-ridux] this script needs root. Re-running with sudo..." >&2
  exec sudo -E bash "$0" "${ORIG_ARGS[@]}"
fi

normalize_bool() {
  case "$1" in
    yes|YES|true|TRUE|1) echo "yes" ;;
    no|NO|false|FALSE|0) echo "no" ;;
    *)
      echo "[debian-live-ridux] invalid boolean value: $1" >&2
      exit 6
      ;;
  esac
}

INCREMENTAL="$(normalize_bool "$INCREMENTAL")"
SKIP_HOST_DEPS="$(normalize_bool "$SKIP_HOST_DEPS")"
WITH_CHROME="$(normalize_bool "$WITH_CHROME")"

need_cmd() {
  local cmd="$1"
  command -v "$cmd" >/dev/null 2>&1 || {
    echo "[debian-live-ridux] missing command: $cmd" >&2
    exit 3
  }
}

need_file() {
  local path="$1"
  [[ -f "$path" ]] || {
    echo "[debian-live-ridux] missing required file: $path" >&2
    exit 4
  }
}

for f in ridux-browser.sh ridux-app.sh ridux-compat.sh ridux-ui.c ridux-flush.c ridux-flush.h; do
  need_file "${RUNTIME_DIR}/${f}"
done
need_file "$BOOT_SPLASH"

need_cmd apt-get

if [[ "$SKIP_HOST_DEPS" = "no" ]]; then
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -y
  apt-get install -y \
    live-build \
    debootstrap \
    squashfs-tools \
    xorriso \
    dctrl-tools \
    ca-certificates \
    curl \
    gnupg \
    dosfstools \
    mtools
fi

need_cmd lb

mkdir -p "$(dirname "$OUT_ISO")"
if [[ "$INCREMENTAL" = "no" ]]; then
  rm -rf "$WORK_DIR"
fi
mkdir -p "$WORK_DIR"
cd "$WORK_DIR"

if [[ "$INCREMENTAL" = "yes" && -f "config/common" ]]; then
  echo "[debian-live-ridux] reusing existing live-build config in: $WORK_DIR"
else
  echo "[debian-live-ridux] configuring live-build (dist=${DIST})..."
  lb config \
    --mode debian \
    --distribution "$DIST" \
    --architectures amd64 \
    --binary-images iso-hybrid \
    --build-with-chroot true \
    --debian-installer false \
    --archive-areas "main contrib non-free non-free-firmware" \
    --bootappend-live "boot=live components quiet loglevel=3 noprompt noeject"
fi

mkdir -p config/package-lists
cat > config/package-lists/ridux.list.chroot <<'EOF'
live-boot
live-config
systemd-sysv
xserver-xorg
xinit
xterm
dbus-x11
build-essential
pkg-config
libx11-6
libx11-dev
file
iproute2
net-tools
procps
psmisc
lsof
strace
firefox-esr
chromium
ca-certificates
curl
gnupg
EOF

mkdir -p config/includes.chroot/usr/local/bin
mkdir -p config/includes.chroot/usr/local/share/ridux
mkdir -p config/includes.binary/isolinux
mkdir -p config/includes.binary/boot/grub

install -m 0755 "${RUNTIME_DIR}/ridux-browser.sh" config/includes.chroot/usr/local/bin/ridux-browser
install -m 0755 "${RUNTIME_DIR}/ridux-app.sh" config/includes.chroot/usr/local/bin/ridux-app
install -m 0755 "${RUNTIME_DIR}/ridux-compat.sh" config/includes.chroot/usr/local/bin/ridux-compat
install -m 0644 "${RUNTIME_DIR}/ridux-ui.c" config/includes.chroot/usr/local/share/ridux/ridux-ui.c
install -m 0644 "${RUNTIME_DIR}/ridux-flush.c" config/includes.chroot/usr/local/share/ridux/ridux-flush.c
install -m 0644 "${RUNTIME_DIR}/ridux-flush.h" config/includes.chroot/usr/local/share/ridux/ridux-flush.h
install -m 0644 "$BOOT_SPLASH" config/includes.binary/isolinux/splash.png
install -m 0644 "$BOOT_SPLASH" config/includes.binary/boot/grub/splash.png

cat > config/includes.binary/isolinux/isolinux.cfg <<'EOF'
UI vesamenu.c32
PROMPT 0
TIMEOUT 10
DEFAULT live
ONTIMEOUT live
MENU HIDDEN
INCLUDE menu.cfg
EOF

cat > config/includes.binary/isolinux/stdmenu.cfg <<'EOF'
menu background splash.png
menu color title   * #FFFFFFFF *
menu color border  * #00000000 #00000000 none
menu color sel     * #FFFFFFFF #2E6CDEFF *
menu color hotsel  1;7;37;40 #FFFFFFFF #2E6CDEFF *
menu color tabmsg  * #DCEBFFFF #00000000 *
menu color help    37;40 #BFD2ECFF #00000000 none
menu vshift 10
menu rows 8
menu helpmsgrow 14
menu cmdlinerow 15
menu timeoutrow 15
menu tabmsgrow 17
menu tabmsg Press ENTER to boot RiduxOS
EOF

cat > config/includes.binary/isolinux/menu.cfg <<'EOF'
menu hshift 0
menu width 82
menu title RiduxOS Live
include stdmenu.cfg
include live.cfg
menu begin utilities
	menu label Utilities
	menu title RiduxOS Utilities
	include stdmenu.cfg
	label mainmenu
		menu label Back
		menu exit
	include utilities.cfg
menu end
menu clear
EOF

cat > config/includes.binary/boot/grub/theme.cfg <<'EOF'
set color_normal=light-gray/black
set color_highlight=white/blue
if [ -e /boot/grub/splash.png ]; then
    terminal_output gfxterm
    insmod png
    background_image /boot/grub/splash.png
    set menu_color_normal=white/black
    set menu_color_highlight=white/blue
fi
EOF

cat > config/includes.binary/boot/grub/grub.cfg <<'EOF'
source /boot/grub/config.cfg
set default=0
set timeout=1
set timeout_style=hidden
source /boot/grub/theme.cfg

menuentry "RiduxOS Live" --hotkey=l {
	linux	/live/vmlinuz boot=live components quiet loglevel=3 noprompt noeject findiso=${iso_path}
	initrd	/live/initrd.img
}
menuentry "RiduxOS Live (safe graphics)" {
	linux	/live/vmlinuz boot=live components nomodeset noprompt noeject findiso=${iso_path}
	initrd	/live/initrd.img
}

submenu 'Utilities...' --hotkey=u {
	source /boot/grub/theme.cfg
	menuentry "Verify integrity of the boot medium" --hotkey=v {
		linux	/live/vmlinuz boot=live components noprompt noeject findiso=${iso_path} verify-checksums
		initrd	/live/initrd.img
	}
}
EOF

mkdir -p config/hooks/live
cat > config/hooks/live/9900-ridux-runtime.chroot <<'EOF'
#!/bin/bash
set -euo pipefail

for compat_cmd in \
  abi6 pidfd io_uring statx sysplus \
  lsblk route mount lsdev catproc syscalls \
  threads ss elf64 pmm tasks paging shm timers fds \
  realsys mmaps procfs dynlink libc dlsym heap bsd b64 rng browser
do
  ln -sf /usr/local/bin/ridux-compat "/usr/local/bin/${compat_cmd}"
done

if command -v cc >/dev/null 2>&1; then
  if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists x11 >/dev/null 2>&1; then
    cc -O2 \
      /usr/local/share/ridux/ridux-ui.c \
      /usr/local/share/ridux/ridux-flush.c \
      -o /usr/local/bin/ridux-ui \
      $(pkg-config --cflags --libs x11) || true
  else
    cc -O2 \
      /usr/local/share/ridux/ridux-ui.c \
      /usr/local/share/ridux/ridux-flush.c \
      -o /usr/local/bin/ridux-ui \
      -lX11 || true
  fi
fi

if [ ! -x /usr/local/bin/ridux-ui ]; then
  cat > /usr/local/bin/ridux-ui <<'RIDUX_UI_FALLBACK_EOF'
#!/bin/sh
exec /usr/local/bin/ridux-browser chrome --start-maximized
RIDUX_UI_FALLBACK_EOF
  chmod +x /usr/local/bin/ridux-ui
fi

cat > /usr/local/bin/ridux <<'RIDUX_CMD_EOF'
#!/bin/sh
exec /usr/local/bin/ridux-ui "$@"
RIDUX_CMD_EOF
chmod +x /usr/local/bin/ridux

cat > /usr/local/bin/ridux-session <<'RIDUX_SESSION_EOF'
#!/bin/sh
xset -dpms s off s noblank || true
xsetroot -solid "#101826" || true
if command -v dbus-launch >/dev/null 2>&1; then
  exec dbus-launch --exit-with-session /usr/local/bin/ridux-ui
fi
exec /usr/local/bin/ridux-ui
RIDUX_SESSION_EOF
chmod +x /usr/local/bin/ridux-session

mkdir -p /etc/systemd/system/getty@tty1.service.d
cat > /etc/systemd/system/getty@tty1.service.d/autologin.conf <<'GETTY_EOF'
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin user --noclear %I $TERM
Type=idle
GETTY_EOF

cat > /etc/profile.d/ridux-autostart.sh <<'AUTOSTART_EOF'
if [ -z "${DISPLAY:-}" ] && [ "$(tty 2>/dev/null)" = "/dev/tty1" ] && command -v startx >/dev/null 2>&1; then
  exec startx /usr/local/bin/ridux-session --
fi
AUTOSTART_EOF
chmod 0644 /etc/profile.d/ridux-autostart.sh

mkdir -p /etc/skel
cat > /etc/skel/.xinitrc <<'SKEL_XINIT_EOF'
#!/bin/sh
exec /usr/local/bin/ridux-session
SKEL_XINIT_EOF
chmod 0755 /etc/skel/.xinitrc

cat > /etc/skel/.profile <<'SKEL_PROFILE_EOF'
if [ -z "${DISPLAY:-}" ] && [ "$(tty 2>/dev/null)" = "/dev/tty1" ] && command -v startx >/dev/null 2>&1; then
  exec startx /usr/local/bin/ridux-session --
fi
SKEL_PROFILE_EOF
chmod 0644 /etc/skel/.profile

install -d -m 0755 /home/user
cp -f /etc/skel/.xinitrc /home/user/.xinitrc
cp -f /etc/skel/.profile /home/user/.profile
chown -R 1000:1000 /home/user || true

echo "Ridux Live image ready" > /usr/local/share/ridux/LIVE_INFO.txt
EOF
chmod +x config/hooks/live/9900-ridux-runtime.chroot

if [[ "$WITH_CHROME" = "yes" ]]; then
  cat > config/hooks/live/9950-google-chrome.chroot <<'EOF'
#!/bin/bash
set -euo pipefail
apt-get update -y
apt-get install -y ca-certificates curl gnupg
install -d -m 0755 /etc/apt/keyrings
if [ ! -f /etc/apt/keyrings/google-chrome.gpg ]; then
  curl -fsSL https://dl.google.com/linux/linux_signing_key.pub | gpg --dearmor -o /etc/apt/keyrings/google-chrome.gpg
  chmod 0644 /etc/apt/keyrings/google-chrome.gpg
fi
cat > /etc/apt/sources.list.d/google-chrome.list <<'CHROME_REPO_EOF'
deb [arch=amd64 signed-by=/etc/apt/keyrings/google-chrome.gpg] https://dl.google.com/linux/chrome/deb/ stable main
CHROME_REPO_EOF
apt-get update -y
apt-get install -y google-chrome-stable || true
EOF
  chmod +x config/hooks/live/9950-google-chrome.chroot
else
  rm -f config/hooks/live/9950-google-chrome.chroot
fi

if [[ "$INCREMENTAL" = "yes" && -d "chroot" ]]; then
  echo "[debian-live-ridux] incremental mode: keeping bootstrap/chroot cache"
  echo "[debian-live-ridux] cleaning previous binary artifacts..."
  lb clean --binary || true
else
  echo "[debian-live-ridux] bootstrapping..."
  lb bootstrap
fi

echo "[debian-live-ridux] running chroot stage..."
lb chroot

echo "[debian-live-ridux] running binary stage..."
lb binary

ISO_SRC="$(find . -maxdepth 1 -type f -name 'live-image-*.hybrid.iso' | head -n 1)"
if [[ -z "${ISO_SRC:-}" ]]; then
  echo "[debian-live-ridux] could not find resulting live ISO in work directory." >&2
  exit 5
fi

cp -f "$ISO_SRC" "$OUT_ISO"

echo "[debian-live-ridux] done"
echo "  ISO: $OUT_ISO"
echo "  dist: $DIST"
echo "  with-chrome: $WITH_CHROME"
echo "  incremental: $INCREMENTAL"
echo "  skip-host-deps: $SKIP_HOST_DEPS"
