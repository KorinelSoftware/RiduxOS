#!/usr/bin/env bash
set -euo pipefail

RIDUX_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CACHE_DIR="${RIDUX_ROOT}/third_party/cache/freebsd-iso"
OUT_DIR="${RIDUX_ROOT}/build"
# User-visible name says only "Ridux".
OUT_ISO="${OUT_DIR}/RiduxOS-Live.iso"

RUNTIME_DIR="${RIDUX_ROOT}/freebsd/ridux-runtime"
RUNTIME_BROWSER="${RUNTIME_DIR}/ridux-browser.sh"
RUNTIME_APP="${RUNTIME_DIR}/ridux-app.sh"
RUNTIME_COMPAT="${RUNTIME_DIR}/ridux-compat.sh"
RUNTIME_SHELL="${RUNTIME_DIR}/ridux-shell.sh"
RUNTIME_DESKTOP="${RUNTIME_DIR}/ridux-desktop.sh"
FLUSH_RUNTIME_DIR="${RIDUX_ROOT}/linux/ridux-runtime"
RUNTIME_UI="${FLUSH_RUNTIME_DIR}/ridux-ui.c"
RUNTIME_FLUSH_C="${FLUSH_RUNTIME_DIR}/ridux-flush.c"
RUNTIME_FLUSH_H="${FLUSH_RUNTIME_DIR}/ridux-flush.h"
RUNTIME_ASSETS_H="${FLUSH_RUNTIME_DIR}/assets.h"

# Pre-baked package cache. When this directory exists with downloaded
# .pkg files, the live ISO ships every dependency Firefox+Xorg need
# inline and pkg(8) installs offline at boot. Without it, the ISO
# falls back to the slower online pkg path.
RIDUX_PKG_CACHE_ROOT="${RIDUX_ROOT}/third_party/cache/freebsd-pkgs"
RIDUX_PKG_ABI="${RIDUX_PKG_ABI:-FreeBSD:15:amd64}"
RIDUX_PKG_REPO_BRANCH="${RIDUX_PKG_REPO_BRANCH:-latest}"
RIDUX_PKG_CACHE_DIR="${RIDUX_PKG_CACHE_ROOT}/${RIDUX_PKG_ABI//:/_}/${RIDUX_PKG_REPO_BRANCH}"

FREEBSD_RELEASE="${FREEBSD_RELEASE:-auto}"
INSTALL_LINUX_CHROME="${INSTALL_LINUX_CHROME:-no}"
RIDUX_EXTRA_APPS="${RIDUX_EXTRA_APPS:-}"
RIDUX_FAST_START="${RIDUX_FAST_START:-no}"
RIDUX_KERNEL_PATH="${RIDUX_KERNEL_PATH:-auto}"
RIDUX_MINIMAL_BOOT="${RIDUX_MINIMAL_BOOT:-yes}"

usage() {
  cat <<'EOF'
Usage:
  tools/build_freebsd_live_iso.sh [options]

Options:
  --release <X.Y-RELEASE|auto>   FreeBSD release to use (default: auto)
  --output <path>                Output ISO path
  --ridux-kernel <path|auto|none>
                                 Inject custom RIDUX kernel into /boot/kernel/kernel.
                                 auto: detect common artifact paths (default)
                                 none: keep upstream FreeBSD kernel
  --orbit-fast-start             Boot fast: defer heavy browser provisioning
  --full-start                   Full provisioning during first boot (default)
  --minimal-boot                 Skip pkg provisioning; start RiduxShell quickly
  --full-provision               Run full pkg provisioning at first boot
  --with-linux-chrome            Install linux-chrome in live bootstrap
  --without-linux-chrome         Skip linux-chrome installation
  --extra-apps <csv>             Extra apps via ridux-app (default: none)
  --no-extra-apps                Disable extra apps
  -h, --help                     Show this help

Environment overrides:
  FREEBSD_RELEASE, INSTALL_LINUX_CHROME, RIDUX_EXTRA_APPS, RIDUX_FAST_START,
  RIDUX_KERNEL_PATH, RIDUX_MINIMAL_BOOT
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --release)
      FREEBSD_RELEASE="$2"
      shift 2
      ;;
    --output)
      OUT_ISO="$2"
      shift 2
      ;;
    --ridux-kernel)
      RIDUX_KERNEL_PATH="$2"
      shift 2
      ;;
    --orbit-fast-start)
      RIDUX_FAST_START="yes"
      shift
      ;;
    --full-start)
      RIDUX_FAST_START="no"
      shift
      ;;
    --minimal-boot)
      RIDUX_MINIMAL_BOOT="yes"
      shift
      ;;
    --full-provision)
      RIDUX_MINIMAL_BOOT="no"
      shift
      ;;
    --with-linux-chrome)
      INSTALL_LINUX_CHROME="yes"
      shift
      ;;
    --without-linux-chrome)
      INSTALL_LINUX_CHROME="no"
      shift
      ;;
    --extra-apps)
      RIDUX_EXTRA_APPS="$2"
      shift 2
      ;;
    --no-extra-apps)
      RIDUX_EXTRA_APPS=""
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "[freebsd-live-iso] unknown option: $1" >&2
      usage
      exit 2
      ;;
  esac
done

need_cmd() {
  local cmd="$1"
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "[freebsd-live-iso] required command not found: $cmd" >&2
    exit 3
  fi
}

need_file() {
  local path="$1"
  if [[ ! -f "$path" ]]; then
    echo "[freebsd-live-iso] required file not found: $path" >&2
    exit 4
  fi
}

need_cmd curl
need_cmd xz
need_cmd xorriso
need_cmd mktemp
need_cmd sed
need_cmd awk
need_cmd tr
need_cmd ar

need_file "$RUNTIME_BROWSER"
need_file "$RUNTIME_APP"
need_file "$RUNTIME_COMPAT"
need_file "$RUNTIME_SHELL"
need_file "$RUNTIME_DESKTOP"
need_file "$RUNTIME_UI"
need_file "$RUNTIME_FLUSH_C"
need_file "$RUNTIME_FLUSH_H"
need_file "$RUNTIME_ASSETS_H"

case "$INSTALL_LINUX_CHROME" in
  yes|YES|true|TRUE|1)
    INSTALL_LINUX_CHROME="YES"
    ;;
  no|NO|false|FALSE|0)
    INSTALL_LINUX_CHROME="NO"
    ;;
  *)
    echo "[freebsd-live-iso] INSTALL_LINUX_CHROME must be yes/no" >&2
    exit 4
    ;;
esac

case "$RIDUX_FAST_START" in
  yes|YES|true|TRUE|1)
    RIDUX_FAST_START="YES"
    ;;
  no|NO|false|FALSE|0)
    RIDUX_FAST_START="NO"
    ;;
  *)
    echo "[freebsd-live-iso] RIDUX_FAST_START must be yes/no" >&2
    exit 4
    ;;
esac

case "$RIDUX_MINIMAL_BOOT" in
  yes|YES|true|TRUE|1)
    RIDUX_MINIMAL_BOOT="YES"
    ;;
  no|NO|false|FALSE|0)
    RIDUX_MINIMAL_BOOT="NO"
    ;;
  *)
    echo "[freebsd-live-iso] RIDUX_MINIMAL_BOOT must be yes/no" >&2
    exit 4
    ;;
esac

if [[ -n "$RIDUX_EXTRA_APPS" && ! "$RIDUX_EXTRA_APPS" =~ ^[A-Za-z0-9._,-]+$ ]]; then
  echo "[freebsd-live-iso] invalid --extra-apps list: $RIDUX_EXTRA_APPS" >&2
  exit 4
fi

RIDUX_EXTRA_APPS_WORDS="$(printf '%s' "$RIDUX_EXTRA_APPS" | tr ',' ' ' | awk '{$1=$1; print}')"

resolve_ridux_kernel() {
  case "$RIDUX_KERNEL_PATH" in
    ""|none|NONE)
      printf '%s' ""
      return 0
      ;;
    auto|AUTO)
      local candidate
      local candidates=(
        "${RIDUX_ROOT}/build/freebsd-kernel/RIDUX/kernel"
        "${RIDUX_ROOT}/build/kernel.RIDUX"
        "${RIDUX_ROOT}/build/RIDUX/kernel"
      )

      for candidate in "${candidates[@]}"; do
        if [[ -f "$candidate" ]]; then
          printf '%s' "$candidate"
          return 0
        fi
      done

      local found
      found="$(find "${RIDUX_ROOT}/third_party/upstream/freebsd-src/obj" /usr/obj \
        -type f -path '*/sys/RIDUX/kernel' 2>/dev/null | head -1 || true)"
      if [[ -n "$found" && -f "$found" ]]; then
        printf '%s' "$found"
        return 0
      fi

      printf '%s' ""
      return 0
      ;;
    *)
      if [[ -f "$RIDUX_KERNEL_PATH" ]]; then
        printf '%s' "$RIDUX_KERNEL_PATH"
        return 0
      fi
      echo "[freebsd-live-iso] custom kernel path not found: $RIDUX_KERNEL_PATH" >&2
      exit 4
      ;;
  esac
}

RIDUX_KERNEL_RESOLVED="$(resolve_ridux_kernel)"
RIDUX_KERNEL_MAP_ARGS=()
if [[ -n "$RIDUX_KERNEL_RESOLVED" ]]; then
  RIDUX_KERNEL_MAP_ARGS=( -map "$RIDUX_KERNEL_RESOLVED" /boot/kernel/kernel )
  if [[ -f "$(dirname "$RIDUX_KERNEL_RESOLVED")/linker.hints" ]]; then
    RIDUX_KERNEL_MAP_ARGS+=( -map "$(dirname "$RIDUX_KERNEL_RESOLVED")/linker.hints" /boot/kernel/linker.hints )
  fi
fi

ISO_PREFIX="https://download.freebsd.org/releases/amd64/amd64/ISO-IMAGES"

probe_release_url() {
  local rel="$1"
  local major_minor="${rel%%-RELEASE}"
  local url="${ISO_PREFIX}/${major_minor}/FreeBSD-${rel}-amd64-disc1.iso.xz"
  if curl -fsIL "$url" >/dev/null 2>&1; then
    printf '%s' "$url"
    return 0
  fi
  return 1
}

resolve_release() {
  local rel="$1"
  if [[ "$rel" != "auto" ]]; then
    if url="$(probe_release_url "$rel")"; then
      printf '%s|%s' "$rel" "$url"
      return 0
    fi
    echo "[freebsd-live-iso] release not found on download.freebsd.org: $rel" >&2
    return 1
  fi

  local candidates=(
    "15.0-RELEASE"
    "14.3-RELEASE"
    "14.2-RELEASE"
    "14.1-RELEASE"
    "13.5-RELEASE"
  )
  local candidate
  for candidate in "${candidates[@]}"; do
    if url="$(probe_release_url "$candidate")"; then
      printf '%s|%s' "$candidate" "$url"
      return 0
    fi
  done

  echo "[freebsd-live-iso] unable to auto-detect a downloadable release" >&2
  return 1
}

resolved="$(resolve_release "$FREEBSD_RELEASE")"
FREEBSD_RELEASE="${resolved%%|*}"
ISO_URL="${resolved#*|}"
ISO_BASENAME="FreeBSD-${FREEBSD_RELEASE}-amd64-disc1.iso"
ISO_XZ_PATH="${CACHE_DIR}/${ISO_BASENAME}.xz"
ISO_PATH="${CACHE_DIR}/${ISO_BASENAME}"

mkdir -p "$CACHE_DIR" "$(dirname "$OUT_ISO")" "$OUT_DIR"

echo "[freebsd-live-iso] release: $FREEBSD_RELEASE"
echo "[freebsd-live-iso] source:  $ISO_URL"
echo "[freebsd-live-iso] output:  $OUT_ISO"
echo "[freebsd-live-iso] orbit-fast-start: $RIDUX_FAST_START"
echo "[freebsd-live-iso] minimal-boot: $RIDUX_MINIMAL_BOOT"
echo "[freebsd-live-iso] extra-apps: ${RIDUX_EXTRA_APPS_WORDS:-<none>}"
if [[ -n "$RIDUX_KERNEL_RESOLVED" ]]; then
  echo "[freebsd-live-iso] custom kernel: $RIDUX_KERNEL_RESOLVED"
else
  echo "[freebsd-live-iso] custom kernel: <none> (using upstream FreeBSD kernel)"
fi

if [[ ! -s "$ISO_XZ_PATH" ]]; then
  echo "[freebsd-live-iso] downloading base ISO archive..."
  curl -fL "$ISO_URL" -o "${ISO_XZ_PATH}.part"
  mv "${ISO_XZ_PATH}.part" "$ISO_XZ_PATH"
else
  echo "[freebsd-live-iso] using cached archive: $ISO_XZ_PATH"
fi

if [[ ! -s "$ISO_PATH" || "$ISO_XZ_PATH" -nt "$ISO_PATH" ]]; then
  echo "[freebsd-live-iso] decompressing ISO..."
  xz -dkc "$ISO_XZ_PATH" > "${ISO_PATH}.part"
  mv "${ISO_PATH}.part" "$ISO_PATH"
else
  echo "[freebsd-live-iso] using cached ISO: $ISO_PATH"
fi

tmp_dir="$(mktemp -d)"
cleanup() {
  rm -rf "$tmp_dir"
}
trap cleanup EXIT

rc_local="${tmp_dir}/rc.local"
live_init="${tmp_dir}/ridux-live-init"
live_notes="${tmp_dir}/RIDUX_LIVE.txt"
loader_conf="${tmp_dir}/loader.conf"
libc_nonshared_a="${tmp_dir}/libc_nonshared.a"
libgcc_a="${tmp_dir}/libgcc.a"

cat > "$rc_local" <<'EOF'
#!/bin/sh
# Ridux rc.local. Runs at the end of /etc/rc, BEFORE the installer
# would have grabbed control of ttyv0.
echo "[rc.local] starting at $(date)" >> /var/log/rc.local.log
# Wait for /usr/local to be mounted (tmpfs overlay) before running Ridux launcher
for i in 1 2 3 4 5 6 7 8 9 10; do
  echo "[rc.local] checking ridux-live-init-tty (attempt $i)..." >> /var/log/rc.local.log
  ls -la /usr/local/bin/ridux-live-init-tty 2>&1 >> /var/log/rc.local.log || true
  if [ -x /usr/local/bin/ridux-live-init-tty ]; then
    if ! pgrep -f "/usr/local/bin/ridux-live-init-tty" >/dev/null 2>&1; then
      echo "[rc.local] launching ridux-live-init-tty on /dev/ttyv0..." >> /var/log/rc.local.log
      /usr/local/bin/ridux-live-init-tty </dev/ttyv0 >/dev/ttyv0 2>&1 &
    else
      echo "[rc.local] ridux-live-init-tty already running." >> /var/log/rc.local.log
    fi
    exit 0
  fi
  sleep 1
done
echo "[rc.local] ERROR: /usr/local/bin/ridux-live-init-tty not found after 10 seconds" >> /var/log/rc.local.log
exit 0
EOF

# Override of bsdinstall trigger: replace the installer's launch hook
# with a no-op so the user is NEVER dropped into the bsdinstall(8)
# main menu. The Ridux live runtime takes its place via rc.local +
# the ttyv0 autologin we install below.
bsdinstall_noop="${tmp_dir}/bsdinstall-noop"
cat > "$bsdinstall_noop" <<'EOF'
#!/bin/sh
# Ridux: bsdinstall is intentionally disabled on this image.
# This stub keeps any code path that exec(8)s /usr/sbin/bsdinstall
# from accidentally bringing the menu back. Boot the system with
# Ridux Live; if you want to install to disk, use the dedicated
# Ridux installer ISO.
exit 0
EOF
chmod 0755 "$bsdinstall_noop"

# Override of /etc/ttys so ttyv0 autologins as root with our launcher
# instead of running getty -> login -> bsdinstall. Since tmp_dir gets
# evaluated at iso build time, we use a synthesized file map.
ttys_override="${tmp_dir}/ttys"
cat > "$ttys_override" <<'EOF'
# Ridux: ttyv0 autologin straight into the Ridux live runtime.
console none                    unknown off secure
ttyv0   "/usr/local/bin/ridux-live-init-tty" xterm on secure
ttyv1   "/usr/libexec/getty Pc" xterm on secure
ttyv2   "/usr/libexec/getty Pc" xterm off secure
ttyv3   "/usr/libexec/getty Pc" xterm off secure
ttyv8   "/usr/local/bin/xdm -nodaemon" xterm off secure
ttyu0   "/usr/libexec/getty 3wire"  vt100 off secure
ttyu1   "/usr/libexec/getty 3wire"  vt100 off secure
dcons   "/usr/libexec/getty std.9600" vt100 off secure
EOF

# Tiny wrapper that ridux-live-init runs from ttyv0. Stays in the
# foreground so init does not respawn it; loops if Ridux UI exits.
live_init_tty="${tmp_dir}/ridux-live-init-tty"
cat > "$live_init_tty" <<'EOF'
#!/bin/sh
set -eu

TTY_LOG="/tmp/ridux-tty.log"
TTY_DEV="$(tty 2>/dev/null || echo /dev/ttyv0)"

echo "[ridux-tty] starting at $(date)" >"$TTY_LOG"

print_boot_status() {
    {
        printf '\033[2J\033[H'
        echo "Ridux Live - Inicializando entorno"
        echo ""
        echo "Kernel/boot base: FreeBSD + Ridux runtime"
        echo "Marca de listo: /var/db/ridux/live-ready"
        echo ""
        if [ -f /var/log/ridux-live-init.log ]; then
            echo "Ultimas lineas de ridux-live-init.log:"
            tail -n 14 /var/log/ridux-live-init.log
        else
            echo "Preparando log inicial..."
        fi
        echo ""
        echo "Atajos: Ctrl+Alt+F2 (rescate), Ctrl+Alt+F9 (X11)"
    } >"$TTY_DEV" 2>/dev/null || true
}

if [ ! -f /var/db/ridux/live-ready ]; then
    if [ -x /usr/local/bin/ridux-live-init ] && [ ! -f /var/run/ridux-live.lock ]; then
        echo "[ridux-tty] launching ridux-live-init in background" >>"$TTY_LOG"
        /usr/local/bin/ridux-live-init >/var/log/ridux-live-init.log 2>&1 &
    fi

    i=0
    while [ "$i" -lt 900 ]; do
        if [ -f /var/db/ridux/live-ready ]; then
            break
        fi
        print_boot_status
        i=$((i + 1))
        sleep 1
    done
fi

if [ ! -f /var/db/ridux/live-ready ]; then
    print_boot_status
    echo "[ridux-tty] live init timeout, opening recovery shell" >>"$TTY_LOG"
    echo "" >"$TTY_DEV" 2>/dev/null || true
    echo "Ridux init timeout. Abriendo shell de recuperacion..." >"$TTY_DEV" 2>/dev/null || true
    exec /bin/sh
fi

while :; do
    launched="NO"
    if [ -x /usr/local/bin/startx ] && [ -x /usr/local/bin/ridux-live-session ]; then
        echo "[ridux-tty] launching ridux-live-session..." >>"$TTY_LOG"
        env HOME=/tmp/ridux-root USER=root SHELL=/bin/sh TERM=xterm \
            /usr/local/bin/startx /usr/local/bin/ridux-live-session -- :0 vt9 -nolisten tcp \
            >/var/log/ridux-startx.log 2>&1 || true
        launched="YES"
    fi

    if [ "$launched" != "YES" ] && [ -x /usr/local/bin/ridux-desktop ]; then
        echo "[ridux-tty] launching ridux-desktop fallback..." >>"$TTY_LOG"
        env TERM=xterm /usr/local/bin/ridux-desktop || true
        launched="YES"
    fi

    if [ "$launched" != "YES" ] && [ -x /usr/local/bin/ridux-shell ]; then
        echo "[ridux-tty] launching ridux-shell fallback..." >>"$TTY_LOG"
        env TERM=xterm /usr/local/bin/ridux-shell || true
        launched="YES"
    fi

    if [ "$launched" != "YES" ] && [ -x /usr/local/bin/ridux-browser ]; then
        echo "[ridux-tty] launching ridux-browser..." >>"$TTY_LOG"
        /usr/local/bin/ridux-browser firefox || true
        launched="YES"
    fi

    if [ "$launched" != "YES" ]; then
        echo "[ridux-tty] no ridux session binaries, falling to shell" >>"$TTY_LOG"
        /bin/sh
    fi
    sleep 2
done
EOF
chmod 0755 "$live_init_tty"

# Override of /etc/rc.conf to disable services we do not want and
# enable Ridux-friendly defaults. This file is concatenated to the
# default rc.conf at runtime by /etc/rc.
rc_conf_local="${tmp_dir}/rc.conf.local"
cat > "$rc_conf_local" <<'EOF'
# Ridux runtime defaults baked into the live ISO.
hostname="ridux"
bsdinstall_enable="NO"
linux_enable="YES"
dbus_enable="YES"
moused_enable="YES"
ifconfig_DEFAULT="DHCP"
vboxguest_enable="YES"
vboxservice_enable="YES"
EOF
chmod 0755 "$rc_local"

# Ridux's own pkg(8) repository definition. Points at the embedded
# offline cache so the live system can install Firefox, Xorg, and the
# Ridux UI dependencies without any internet access.
pkg_repos_ridux_conf="${tmp_dir}/Ridux.conf"
cat > "$pkg_repos_ridux_conf" <<'EOF'
# Ridux: pre-baked offline package repository (file://).
# This repo carries the full transitive closure of Firefox + Xorg +
# the Ridux UI runtime, fetched at build time so first boot does not
# require a network round trip.
Ridux: {
    # IMPORTANT: stored at /ridux-pkgs (top level), NOT /usr/local/ridux-pkgs.
    # The live runtime mounts tmpfs over /usr/local to make it writable
    # for pkg, which would hide everything that the ISO baked under there.
    # /ridux-pkgs sits outside that overlay so the cache stays visible.
    url: "file:///ridux-pkgs",
    mirror_type: "none",
    signature_type: "none",
    enabled: yes,
    priority: 100,
}
EOF

# FreeBSD upstream repo definition: explicitly DISABLED. The Ridux
# offline repo above provides everything the live system needs, and
# leaving the upstream repo enabled would make pkg unnecessarily try
# to update from the internet.
pkg_repos_freebsd_disable="${tmp_dir}/FreeBSD.conf"
cat > "$pkg_repos_freebsd_disable" <<'EOF'
# Ridux disables the FreeBSD upstream pkg repo on the live image so
# the system stays self-contained and offline-first. Re-enable this
# manually if you ever want to pull additional packages over the
# network from a running Ridux session.
FreeBSD: {
    enabled: no,
}
EOF

cat > "$loader_conf" <<'EOF'
# Ridux loader configuration.
#
# Hide everything that would otherwise display "FreeBSD" or the daemon
# (beastie) logo during boot. The user should see only the Ridux
# splash and progress.
autoboot_delay="0"
beastie_disable="YES"
loader_logo="none"
loader_menu_timeout="0"
loader_menu_frame="none"
boot_verbose="NO"
boot_mute="NO"
boot_serial="NO"
hw.vga.textmode="0"
kern.consmsgbuf_size="0"
kern.module_verbose="0"
verbose_loading="NO"
# Suppress the "login:" banner before our autologin grabs ttyv0.
kern.hostname="ridux"
EOF

ar cr "$libc_nonshared_a"
ar cr "$libgcc_a"

cat > "$live_init" <<'EOF'
#!/bin/sh
set -eu

LOG="/var/log/ridux-live-init.log"
exec >"$LOG" 2>&1

echo "[ridux-live] starting first-boot provisioning..."
echo "[ridux-live] timestamp: $(date)"
echo "[ridux-live] uname -a: $(uname -a)"

MARKER="/var/db/ridux/live-ready"
LOCK="/var/run/ridux-live.lock"
SEED="/var/db/ridux/live-seed"
ETC_SEED="/var/db/ridux/etc-seed"

export ASSUME_ALWAYS_YES=yes

ridux_write_offline_pkg_repo() {
  mkdir -p /usr/local/etc/pkg/repos /etc/pkg
  cat > /usr/local/etc/pkg/repos/Ridux.conf <<'RIDUX_REPO_EOF'
Ridux: {
    url: "file:///ridux-pkgs",
    mirror_type: "none",
    signature_type: "none",
    enabled: yes,
    priority: 100,
}
RIDUX_REPO_EOF
  cat > /etc/pkg/FreeBSD.conf <<'FREEBSD_REPO_EOF'
FreeBSD: {
    enabled: no,
}
FREEBSD_REPO_EOF
}

ridux_prepare_xinit() {
  mkdir -p /tmp/ridux-root
  cat > /tmp/ridux-root/.xinitrc <<'XINIT_EOF'
#!/bin/sh
exec /usr/local/bin/ridux-live-session
XINIT_EOF
  chmod 0755 /tmp/ridux-root/.xinitrc || true
}

ridux_start_ui_once() {
  if [ -x /usr/local/bin/startx ] && [ -x /usr/local/bin/ridux-live-session ] && ! pgrep -f "Xorg.*:0" >/dev/null 2>&1; then
    ridux_prepare_xinit
    env HOME=/tmp/ridux-root USER=root SHELL=/bin/sh TERM=xterm /usr/local/bin/startx /usr/local/bin/ridux-live-session -- :0 vt9 -nolisten tcp >/var/log/ridux-startx.log 2>&1 &
    sleep 2
    vidcontrol -s 9 >/dev/null 2>&1 || true
  fi
}

ridux_start_ui_retry_bg() {
  cat > /usr/local/bin/ridux-live-autostart-loop <<'RIDUX_AUTOSTART_EOF'
#!/bin/sh
set -eu
i=0
while [ "$i" -lt 180 ]; do
  if pgrep -f "Xorg.*:0" >/dev/null 2>&1; then
    exit 0
  fi
  if [ -x /usr/local/bin/startx ] && [ -x /usr/local/bin/ridux-live-session ]; then
    mkdir -p /tmp/ridux-root
    if [ ! -f /tmp/ridux-root/.xinitrc ]; then
      cat > /tmp/ridux-root/.xinitrc <<'XINIT_EOF'
#!/bin/sh
exec /usr/local/bin/ridux-live-session
XINIT_EOF
      chmod 0755 /tmp/ridux-root/.xinitrc || true
    fi
    env HOME=/tmp/ridux-root USER=root SHELL=/bin/sh TERM=xterm /usr/local/bin/startx /usr/local/bin/ridux-live-session -- :0 vt9 -nolisten tcp >/var/log/ridux-startx.log 2>&1 &
    sleep 4
    vidcontrol -s 9 >/dev/null 2>&1 || true
    if pgrep -f "Xorg.*:0" >/dev/null 2>&1; then
      exit 0
    fi
  fi
  i=$((i + 1))
  sleep 2
done
exit 0
RIDUX_AUTOSTART_EOF
  chmod 0755 /usr/local/bin/ridux-live-autostart-loop || true
  nohup /usr/local/bin/ridux-live-autostart-loop >/var/log/ridux-live-autostart.log 2>&1 &
}

ridux_start_postinstall_bg() {
  if [ -x /usr/local/bin/ridux-live-postinstall ] && [ ! -f /var/db/ridux/live-postinstall.started ]; then
    touch /var/db/ridux/live-postinstall.started
    nohup /usr/local/bin/ridux-live-postinstall >/var/log/ridux-live-postinstall.log 2>&1 &
  fi
}

ridux_install_tty_ui_launcher() {
  cat > /usr/local/bin/ridux-tty-launch <<'RIDUX_TTY_LAUNCH_EOF'
#!/bin/sh
set -eu

prepare_xinit() {
  mkdir -p /tmp/ridux-root
  if [ ! -f /tmp/ridux-root/.xinitrc ]; then
    cat > /tmp/ridux-root/.xinitrc <<'XINIT_EOF'
#!/bin/sh
exec /usr/local/bin/ridux-live-session
XINIT_EOF
    chmod 0755 /tmp/ridux-root/.xinitrc || true
  fi
}

while :; do
  if pgrep -f "Xorg.*:0" >/dev/null 2>&1; then
    sleep 2
    continue
  fi
  if [ -x /usr/local/bin/startx ] && [ -x /usr/local/bin/ridux-live-session ]; then
    prepare_xinit
    env HOME=/tmp/ridux-root USER=root SHELL=/bin/sh TERM=xterm /usr/local/bin/startx /usr/local/bin/ridux-live-session -- :0 vt9 -nolisten tcp >>/var/log/ridux-startx.log 2>&1 || true
    sleep 2
    vidcontrol -s 9 >/dev/null 2>&1 || true
    continue
  fi
  if [ -x /usr/local/bin/ridux-desktop ]; then
    env TERM=xterm /usr/local/bin/ridux-desktop || true
    continue
  fi
  if [ -x /usr/local/bin/ridux-shell ]; then
    env TERM=xterm /usr/local/bin/ridux-shell || true
    continue
  fi
  sleep 2
done
RIDUX_TTY_LAUNCH_EOF
  chmod 0755 /usr/local/bin/ridux-tty-launch || true

  if grep -q '^ttyv0[[:space:]]' /etc/ttys; then
    sed -i '' 's#^ttyv0[[:space:]].*#ttyv0   "/usr/local/bin/ridux-tty-launch"   xterm   on   secure#' /etc/ttys || true
    kill -HUP 1 >/dev/null 2>&1 || true
  fi
}

if [ -f "$MARKER" ]; then
  ridux_install_tty_ui_launcher
  ridux_start_ui_once
  ridux_start_ui_retry_bg
  ridux_start_postinstall_bg
  exit 0
fi

if [ -e "$LOCK" ]; then
  exit 0
fi
touch "$LOCK"
mkdir -p /var/db/ridux /var/log

echo "[ridux-live] creating large writable /tmp..."
mount -t tmpfs -o size=1024m tmpfs /tmp || mdmfs -s 1024m md /tmp || true
chmod 1777 /tmp || true

if [ "__RIDUX_MINIMAL_BOOT__" = "YES" ]; then
  echo "[ridux-live] minimal boot mode (early path): skipping overlay/pkg provisioning."
  touch "$MARKER"
  ridux_install_tty_ui_launcher
  ridux_start_ui_once
  ridux_start_ui_retry_bg
  exit 0
fi

if [ ! -d "$ETC_SEED" ]; then
  mkdir -p "$ETC_SEED"
  (cd /etc && tar cf - .) | (cd "$ETC_SEED" && tar xpf -) || true
fi

if touch /etc/.ridux-write-test 2>/dev/null; then
  rm -f /etc/.ridux-write-test
else
  echo "[ridux-live] creating writable /etc overlay..."
  mount -t tmpfs -o size=64m tmpfs /etc || mdmfs -s 64m md /etc
  (cd "$ETC_SEED" && tar cf - .) | (cd /etc && tar xpf -) || true
fi

rm -f /etc/resolv.conf
cat > /etc/resolv.conf <<'RESOLV_EOF'
nameserver 10.0.2.3
nameserver 1.1.1.1
nameserver 8.8.8.8
RESOLV_EOF

if [ ! -d "$SEED" ]; then
  mkdir -p "$SEED/bin" "$SEED/share"
  cp /usr/local/bin/ridux-browser "$SEED/bin/" || true
  cp /usr/local/bin/ridux-app "$SEED/bin/" || true
  cp /usr/local/bin/ridux-compat "$SEED/bin/" || true
  cp /usr/local/bin/ridux-shell "$SEED/bin/" || true
  cp /usr/local/bin/ridux-desktop "$SEED/bin/" || true
  cp /usr/local/bin/ridux-live-init "$SEED/bin/" || true
  cp /usr/local/share/ridux/ridux-ui.c "$SEED/share/" || true
  cp /usr/local/share/ridux/ridux-flush.c "$SEED/share/" || true
  cp /usr/local/share/ridux/ridux-flush.h "$SEED/share/" || true
  cp /usr/local/share/ridux/assets.h "$SEED/share/" || true
  cp /usr/local/share/ridux/RIDUX_LIVE.txt "$SEED/share/" || true
fi

if ! mount | grep -q ' on /usr/local '; then
  echo "[ridux-live] creating writable /usr/local overlay..."
  mount -t tmpfs -o size=6144m tmpfs /usr/local || mdmfs -s 6144m md /usr/local
fi

mkdir -p /usr/local/bin /usr/local/share/ridux
cp "$SEED/bin/"* /usr/local/bin/ 2>/dev/null || true
cp "$SEED/share/"* /usr/local/share/ridux/ 2>/dev/null || true
chmod 0755 /usr/local/bin/ridux-browser /usr/local/bin/ridux-app /usr/local/bin/ridux-compat /usr/local/bin/ridux-live-init 2>/dev/null || true
chmod 0755 /usr/local/bin/ridux-shell 2>/dev/null || true
chmod 0755 /usr/local/bin/ridux-desktop 2>/dev/null || true

if [ "__RIDUX_MINIMAL_BOOT__" = "YES" ]; then
  echo "[ridux-live] minimal boot mode: skipping pkg provisioning."
  touch "$MARKER"
  ridux_install_tty_ui_launcher
  ridux_start_ui_once
  ridux_start_ui_retry_bg
  exit 0
fi

mkdir -p /var/db/pkg /var/cache/pkg
if ! mount | grep -q ' on /var/db/pkg '; then
  echo "[ridux-live] creating writable pkg database/cache..."
  mount -t tmpfs -o size=512m tmpfs /var/db/pkg || mdmfs -s 512m md /var/db/pkg
fi
if ! mount | grep -q ' on /var/cache/pkg '; then
  mount -t tmpfs -o size=2048m tmpfs /var/cache/pkg || mdmfs -s 2048m md /var/cache/pkg
fi

retry_cmd() {
  _max="$1"
  shift
  _try=1
  while ! "$@"; do
    if [ "$_try" -ge "$_max" ]; then
      return 1
    fi
    _try=$((_try + 1))
    sleep 5
  done
  return 0
}

# Top-level path on purpose: /usr/local gets a tmpfs overlay below,
# which would otherwise hide the baked-in cache.
RIDUX_PKG_CACHE="/ridux-pkgs"
RIDUX_PKG_OFFLINE="NO"
RIDUX_PKG_SELF=""
PKG_BIN="pkg"

# Detection accepts either layout that the build pipeline might emit:
#   /ridux-pkgs/All/pkg-*.pkg            (flat)
#   /ridux-pkgs/All/Hashed/pkg-*.pkg     (upstream FreeBSD pkg layout)
# Plus a packagesite.pkg next to All/ which is the canonical metadata
# bundle. Using find lets us tolerate xorriso layout drift without
# failing closed.
if [ -f "$RIDUX_PKG_CACHE/packagesite.pkg" ] && [ -d "$RIDUX_PKG_CACHE/All" ]; then
    RIDUX_PKG_SELF="$(find "$RIDUX_PKG_CACHE/All" -type f -name 'pkg-[0-9]*.pkg' 2>/dev/null | head -1)"
    if [ -n "$RIDUX_PKG_SELF" ]; then
        RIDUX_PKG_OFFLINE="YES"
        echo "[ridux-live] offline pkg cache detected at $RIDUX_PKG_CACHE"
        echo "[ridux-live]   pkg bundle:  $RIDUX_PKG_SELF"
        pkg_count="$(find "$RIDUX_PKG_CACHE/All" -type f -name '*.pkg' 2>/dev/null | wc -l | awk '{print $1}')"
        echo "[ridux-live]   pkg files:   ${pkg_count} packages staged"
    fi
fi

if [ "$RIDUX_PKG_OFFLINE" != "YES" ]; then
    # Loud diagnostic: tells us at boot time exactly what the live
    # runtime sees, so we can iterate on the build without re-rolling.
    echo "[ridux-live] no offline cache detected. Layout snapshot:"
    ls -la /ridux-pkgs 2>&1 | head -10 || true
    ls -la /ridux-pkgs/All 2>&1 | head -10 || true
fi

if [ "$RIDUX_PKG_OFFLINE" = "YES" ]; then
    echo "[ridux-live] bootstrapping pkg from offline cache..."
    # Extract pkg-static manually from the cached pkg-*.pkg.
    mkdir -p /usr/local/sbin /usr/local/bin /usr/local/lib
    pkg_self="$RIDUX_PKG_SELF"
    if [ -n "$pkg_self" ]; then
        echo "[ridux-live] extracting $pkg_self to bootstrap pkg-static..."
        tar -xpf "$pkg_self" -C / 2>/dev/null || true
    fi
    if [ -x /usr/local/sbin/pkg-static ]; then
        PKG_BIN="/usr/local/sbin/pkg-static"
    elif [ -x /usr/local/sbin/pkg ]; then
        PKG_BIN="/usr/local/sbin/pkg"
    else
        PKG_BIN="pkg"
    fi
    echo "[ridux-live] using pkg binary: $PKG_BIN"

    ridux_write_offline_pkg_repo
    echo 'ABI: "FreeBSD:15:amd64"' > /usr/local/etc/pkg/pkg.conf
    "$PKG_BIN" update -f >/dev/null 2>&1 || true

    # Strategy: `pkg add` installs from explicit .pkg files and resolves
    # transitive deps from co-located files in the same directory.
    # Skip the Firefox/Chromium subtree entirely; we only need Xorg +
    # ffmpeg + dbus + xinit/xauth + libX11 + pkgconf to render the
    # Ridux UI and play the boot video.
    echo "[ridux-live] installing minimal Ridux graphical runtime..."
    PKG_DIR="$RIDUX_PKG_CACHE/All/Hashed"
    if [ ! -d "$PKG_DIR" ]; then
        PKG_DIR="$RIDUX_PKG_CACHE/All"
    fi

    RIDUX_RUNTIME_UNPACKED="NO"
    if [ -d "$PKG_DIR" ]; then
        echo "[ridux-live] unpacking graphical runtime directly from offline packages..."
        unpacked_count=0
        for f in "$PKG_DIR"/*.pkg; do
            [ -f "$f" ] || continue
            base="${f##*/}"
            case "$base" in
                firefox-*|chromium-*|llvm*|python*|py311-*|perl5-*|boost-libs-*|openblas-*|gtk3-*|ffmpeg-*)
                    continue
                    ;;
            esac
            tar -xpf "$f" -C / >/dev/null 2>&1 || true
            unpacked_count=$((unpacked_count + 1))
        done
        echo "[ridux-live] unpacked ${unpacked_count} graphical/runtime packages."
        ldconfig -m /usr/local/lib /usr/local/lib/xorg /usr/local/lib/gcc14 /usr/local/lib/compat 2>/dev/null || true
        RIDUX_RUNTIME_UNPACKED="YES"
        if [ -x /usr/local/bin/Xorg ] && [ -x /usr/local/bin/startx ] && [ -r /usr/local/include/X11/Xlib.h ]; then
            echo "[ridux-live] graphical runtime unpack verified."
        else
            echo "[ridux-live] WARNING: direct unpack did not verify every X11 path; continuing without pkg install fallback."
        fi
    fi

    install_pkg_glob() {
        # Install a glob-matched .pkg file using pkg add, which pulls
        # dependencies from neighboring .pkg files automatically. Quiet
        # output so the boot screen is not flooded.
        for f in $1; do
            [ -f "$f" ] || continue
            "$PKG_BIN" add -f "$f" >/dev/null 2>&1 || true
        done
    }

    install_core_graphics() {
        if command -v timeout >/dev/null 2>&1; then
            timeout 300 "$PKG_BIN" install -y "$@" >/dev/null 2>&1
        else
            "$PKG_BIN" install -y "$@" >/dev/null 2>&1
        fi
    }

    if [ "$RIDUX_RUNTIME_UNPACKED" != "YES" ]; then
        echo "[ridux-live] direct package unpack was unavailable; falling back to selected pkg add."
        install_pkg_glob "$PKG_DIR/xorg-minimal-*.pkg"
        install_pkg_glob "$PKG_DIR/xinit-*.pkg"
        install_pkg_glob "$PKG_DIR/xauth-*.pkg"
        install_pkg_glob "$PKG_DIR/dbus-[0-9]*.pkg"
        install_pkg_glob "$PKG_DIR/libX11-*.pkg"
        install_pkg_glob "$PKG_DIR/pkgconf-*.pkg"
        install_pkg_glob "$PKG_DIR/xterm-*.pkg"
        install_pkg_glob "$PKG_DIR/xf86-video-vesa-*.pkg"
        install_pkg_glob "$PKG_DIR/xf86-input-libinput-*.pkg"
    fi
else
    echo "[ridux-live] no offline cache found, falling back to online pkg..."
    pkg bootstrap -f -y || true
    retry_cmd 4 pkg update -f || true

    pkg install -y xorg-minimal xinit xauth dbus ffmpeg libX11 pkgconf \
      || pkg install -y xorg xinit xauth dbus ffmpeg libX11 pkgconf \
      || true
fi

mkdir -p /usr/local/etc/X11/xorg.conf.d
cat > /usr/local/etc/X11/xorg.conf.d/10-ridux-video.conf <<'RIDUX_XORG_VIDEO_EOF'
Section "Device"
    Identifier "Ridux Video"
    Driver "vesa"
EndSection

Section "Screen"
    Identifier "Ridux Screen"
    Device "Ridux Video"
    DefaultDepth 24
EndSection
RIDUX_XORG_VIDEO_EOF

if [ "__RIDUX_FAST_START__" = "YES" ]; then
  echo "[ridux-live] graphical fast start enabled: deferring browsers until Ridux UI is up."
  cat > /usr/local/bin/ridux-live-postinstall <<'RIDUX_POST_EOF'
#!/bin/sh
set -eu
export ASSUME_ALWAYS_YES=yes
if [ -f /var/db/ridux/live-postinstall.done ]; then
  exit 0
fi
pkg bootstrap -f -y || true
pkg update -f || true
pkg install -y firefox || true
pkg install -y chromium || true
if [ "__INSTALL_LINUX_CHROME__" = "YES" ]; then
  /usr/local/bin/ridux-app ensure-linux || true
  /usr/local/bin/ridux-app install chrome || true
fi
for app in __RIDUX_EXTRA_APPS__; do
  [ -n "$app" ] || continue
  /usr/local/bin/ridux-app install "$app" || true
done
touch /var/db/ridux/live-postinstall.done
RIDUX_POST_EOF
  chmod 0755 /usr/local/bin/ridux-live-postinstall
else
  "$PKG_BIN" install -y firefox || pkg install -y firefox || true
  "$PKG_BIN" install -y chromium || pkg install -y chromium || true
  if [ "__INSTALL_LINUX_CHROME__" = "YES" ]; then
    /usr/local/bin/ridux-app ensure-linux || true
    /usr/local/bin/ridux-app install chrome || true
  fi
  for app in __RIDUX_EXTRA_APPS__; do
    [ -n "$app" ] || continue
    /usr/local/bin/ridux-app install "$app" || true
  done
fi

echo "[ridux-live] building Ridux Flush/X11 shell..."
RIDUX_UI_BUILT=NO

try_build_ui() {
  _cc="$1"
  shift
  [ -n "$_cc" ] || return 1
  if ! command -v "$_cc" >/dev/null 2>&1 && [ ! -x "$_cc" ]; then
    return 1
  fi
  "$_cc" "$@" \
    /usr/local/share/ridux/ridux-ui.c \
    /usr/local/share/ridux/ridux-flush.c \
    -I/usr/local/share/ridux \
    -I/usr/local/include \
    -L/usr/local/lib \
    -o /usr/local/bin/ridux-ui \
    -lX11
}

for candidate in cc clang /usr/local/bin/clang /usr/local/llvm*/bin/clang; do
  if try_build_ui "$candidate" -O2; then
    RIDUX_UI_BUILT=YES
    break
  fi
done

if [ "$RIDUX_UI_BUILT" != "YES" ]; then
  echo "[ridux-live] trying already-available compiler toolchain fallback..."
  for candidate in /usr/local/bin/gcc /usr/local/bin/gcc15 /usr/local/bin/gcc14 /usr/local/bin/gcc13 /usr/local/llvm*/bin/clang /usr/local/bin/clang clang cc; do
    if try_build_ui "$candidate" -O2 -fuse-ld=lld -rtlib=compiler-rt -unwindlib=libunwind; then
      RIDUX_UI_BUILT=YES
      break
    fi
    if try_build_ui "$candidate" -O2; then
      RIDUX_UI_BUILT=YES
      break
    fi
  done
fi

if [ "$RIDUX_UI_BUILT" != "YES" ]; then
  echo "[ridux-live] C build failed; falling back to browser launcher."
fi
if [ ! -x /usr/local/bin/ridux-ui ]; then
  cat > /usr/local/bin/ridux-ui <<'RIDUX_UI_FALLBACK_EOF'
#!/bin/sh
if command -v xterm >/dev/null 2>&1; then
  exec xterm -geometry 110x34 -bg "#101826" -fg "#F5FAFF" -title "Ridux UI Recovery" \
    -e sh -lc 'clear; echo "Ridux UI no pudo compilarse o abrirse."; echo; echo "ridux-live-init.log:"; tail -n 90 /var/log/ridux-live-init.log 2>/dev/null; echo; echo "ridux-startx.log:"; tail -n 90 /var/log/ridux-startx.log 2>/dev/null; echo; exec sh'
fi
if [ -x /usr/local/bin/ridux-desktop ]; then
  exec /usr/local/bin/ridux-desktop
fi
exec /bin/sh
RIDUX_UI_FALLBACK_EOF
  chmod +x /usr/local/bin/ridux-ui
fi

if [ ! -x /usr/local/bin/ridux-shell ]; then
  cat > /usr/local/bin/ridux-shell <<'RIDUX_SHELL_FALLBACK_EOF'
#!/bin/sh
echo "RiduxShell fallback (runtime)"
echo "Use: /usr/local/bin/ridux-browser firefox"
exec /bin/sh
RIDUX_SHELL_FALLBACK_EOF
  chmod +x /usr/local/bin/ridux-shell
fi

cat > /usr/local/bin/ridux-live-session <<'LIVE_SESSION_EOF'
#!/bin/sh
# ridux-live-session: the X11 session that the live image runs as soon
# as Xorg is up. Plays a looping splash video while the Ridux UI is
# warming up, plays a one-shot transition video, then hands off to the
# Ridux UI binary.
set -eu

service dbus onestart >/dev/null 2>&1 || true
xset -dpms s off s noblank      || true
xsetroot -solid "#000000"       || true
# Hide the cursor while the video splash is on screen.
( unclutter -idle 0 -root >/dev/null 2>&1 & ) || true

LOOP_VIDEO="/ridux/videos/loop.mp4"
END_VIDEO="/ridux/videos/end.mp4"
UI_BIN="/usr/local/bin/ridux-ui"

ffplay_args() {
    # ffplay options used everywhere: fullscreen, no audio, quiet,
    # exit on completion, no window decorations.
    echo "-fs -an -nostats -hide_banner -loglevel quiet -autoexit -alwaysontop"
}

play_loop() {
    if [ -x /usr/local/bin/ffplay ] && [ -f "$LOOP_VIDEO" ]; then
        ffplay $(ffplay_args) -loop 0 "$LOOP_VIDEO" >/dev/null 2>&1 &
        echo $! > /tmp/ridux-loop.pid
    fi
}

stop_loop() {
    if [ -f /tmp/ridux-loop.pid ]; then
        loop_pid="$(cat /tmp/ridux-loop.pid 2>/dev/null)"
        [ -n "$loop_pid" ] && kill -TERM "$loop_pid" >/dev/null 2>&1 || true
        rm -f /tmp/ridux-loop.pid
    fi
    pkill -f "ffplay.*$LOOP_VIDEO" >/dev/null 2>&1 || true
}

play_end() {
    if [ -x /usr/local/bin/ffplay ] && [ -f "$END_VIDEO" ]; then
        ffplay $(ffplay_args) "$END_VIDEO" >/dev/null 2>&1 || true
    fi
}

launch_ui() {
    if [ -x "$UI_BIN" ]; then
        "$UI_BIN" >/var/log/ridux-ui.log 2>&1 || true
    fi

    # Fallback: show diagnostics instead of leaving the VM on a black X screen.
    xterm -fa "DejaVu Sans Mono" -fs 12 -geometry 110x34 -bg "#101826" -fg "#FFFFFF" \
          -title "Ridux Recovery Shell" \
          -e sh -lc 'clear; echo "Ridux UI salio o no pudo arrancar."; echo; echo "ridux-ui.log:"; tail -n 80 /var/log/ridux-ui.log 2>/dev/null; echo; echo "ridux-live-init.log:"; tail -n 80 /var/log/ridux-live-init.log 2>/dev/null; echo; exec sh'
}

play_loop

# Wait up to 5 minutes for ridux-ui to be ready. ridux-live-init
# normally finishes the build in well under a minute once pkg add
# completes, so the loop video usually plays for ~30-60s.
i=0
while [ "$i" -lt 300 ]; do
    if [ -x "$UI_BIN" ]; then break; fi
    i=$((i + 1))
    sleep 1
done

stop_loop
play_end
launch_ui
LIVE_SESSION_EOF
chmod +x /usr/local/bin/ridux-live-session

touch "$MARKER"
echo "[ridux-live] ready."

ridux_install_tty_ui_launcher
ridux_start_ui_once
ridux_start_ui_retry_bg
ridux_start_postinstall_bg
EOF

sed -i \
  -e "s/__INSTALL_LINUX_CHROME__/${INSTALL_LINUX_CHROME}/g" \
  -e "s/__RIDUX_FAST_START__/${RIDUX_FAST_START}/g" \
  -e "s/__RIDUX_MINIMAL_BOOT__/${RIDUX_MINIMAL_BOOT}/g" \
  -e "s/__RIDUX_EXTRA_APPS__/${RIDUX_EXTRA_APPS_WORDS}/g" \
  "$live_init"
chmod 0755 "$live_init"

cat > "$live_notes" <<'EOF'
Ridux Live ISO (no disk installation required)

- Boots directly from ISO into Ridux. No installer prompt, no
  FreeBSD-branded menu, no manual login.
- Optional custom kernel injection: pass --ridux-kernel <path> when
  generating the ISO to replace /boot/kernel/kernel.
- ttyv0 autologin runs /usr/local/bin/ridux-live-init-tty which:
    * on first boot, provisions Xorg + DBus + Firefox + Chromium and
      builds the Ridux UI (Flush/X11 shell)
      (unless built with --minimal-boot, which jumps directly to Ridux Desktop)
    * prints boot progress live on ttyv0 so startup is visible
    * then hands off to /usr/local/bin/ridux-live-session which runs
      the Ridux UI on display :0 / vt9
    * if X11 is unavailable, falls back to /usr/local/bin/ridux-desktop
      and then /usr/local/bin/ridux-shell as rescue shell
- bsdinstall is disabled at runtime via /etc/rc.conf.local and via a
  noop replacement of /usr/sbin/bsdinstall.

Diagnostics if the GUI does not auto-open:
  Ctrl+Alt+F2          switch to a tty
  tail /var/log/ridux-live-init.log
  tail /var/log/ridux-startx.log
  /usr/local/bin/ridux-app doctor
EOF

# Decide whether the prebuilt offline pkg cache is available. If so we
# embed the entire ~700 MB tree into the ISO so first boot can install
# Firefox + Xorg + the Ridux UI in a couple of minutes with no network.
RIDUX_PKG_EMBED_ARGS=""
if [[ -d "$RIDUX_PKG_CACHE_DIR" ]] && [[ -f "$RIDUX_PKG_CACHE_DIR/packagesite.pkg" ]]; then
    cache_size_bytes="$(du -sb "$RIDUX_PKG_CACHE_DIR" 2>/dev/null | awk '{print $1}')"
    cache_size_mb="$(( ${cache_size_bytes:-0} / 1024 / 1024 ))"
    echo "[ridux-live-iso] embedding offline pkg cache: $RIDUX_PKG_CACHE_DIR (${cache_size_mb} MB)"
    # Bake the cache at top-level /ridux-pkgs so it survives the tmpfs
# overlay that ridux-live-init mounts over /usr/local at boot.
RIDUX_PKG_EMBED_ARGS=( -map "$RIDUX_PKG_CACHE_DIR" /ridux-pkgs )
else
    echo "[ridux-live-iso] no offline pkg cache at $RIDUX_PKG_CACHE_DIR"
    echo "[ridux-live-iso] -> ISO will fall back to online pkg at first boot."
    echo "[ridux-live-iso] -> to populate, run: python3 tools/prefetch_ridux_pkgs.py"
    RIDUX_PKG_EMBED_ARGS=()
fi

echo "[ridux-live-iso] building customized live ISO..."
rm -f "${OUT_ISO}.part"
xorriso \
  -indev "$ISO_PATH" \
  -outdev "${OUT_ISO}.part" \
  -boot_image any keep \
  "${RIDUX_KERNEL_MAP_ARGS[@]}" \
  -map "$loader_conf" /boot/loader.conf \
  -map "$libc_nonshared_a" /usr/lib/libc_nonshared.a \
  -map "$libgcc_a" /usr/lib/libgcc.a \
  -map "$rc_local" /etc/rc.local \
  -map "$ttys_override" /etc/ttys \
  -map "$rc_conf_local" /etc/rc.conf.local \
  -map "$bsdinstall_noop" /usr/sbin/bsdinstall \
  -map "$pkg_repos_ridux_conf" /usr/local/etc/pkg/repos/Ridux.conf \
  -map "$pkg_repos_freebsd_disable" /etc/pkg/FreeBSD.conf \
  -map "$live_init" /usr/local/bin/ridux-live-init \
  -map "$live_init_tty" /usr/local/bin/ridux-live-init-tty \
  -map "${RIDUX_ROOT}/LoadingScreenLoopVideo.mp4" /ridux/videos/loop.mp4 \
  -map "${RIDUX_ROOT}/LoadingScreenEnd.mp4" /ridux/videos/end.mp4 \
  -map "$RUNTIME_BROWSER" /usr/local/bin/ridux-browser \
  -map "$RUNTIME_APP" /usr/local/bin/ridux-app \
  -map "$RUNTIME_COMPAT" /usr/local/bin/ridux-compat \
  -map "$RUNTIME_SHELL" /usr/local/bin/ridux-shell \
  -map "$RUNTIME_DESKTOP" /usr/local/bin/ridux-desktop \
  -map "$RUNTIME_UI" /usr/local/share/ridux/ridux-ui.c \
  -map "$RUNTIME_FLUSH_C" /usr/local/share/ridux/ridux-flush.c \
  -map "$RUNTIME_FLUSH_H" /usr/local/share/ridux/ridux-flush.h \
  -map "$RUNTIME_ASSETS_H" /usr/local/share/ridux/assets.h \
  -map "$live_notes" /usr/local/share/ridux/RIDUX_LIVE.txt \
  "${RIDUX_PKG_EMBED_ARGS[@]}" \
  -return_with SORRY 0 \
  -commit \
  -end >/dev/null

mv "${OUT_ISO}.part" "$OUT_ISO"

echo "[ridux-live-iso] done"
echo "  ISO: $OUT_ISO"
echo "  release: $FREEBSD_RELEASE"
if [[ -n "$RIDUX_KERNEL_RESOLVED" ]]; then
  echo "  ridux-kernel: $RIDUX_KERNEL_RESOLVED"
else
  echo "  ridux-kernel: <none>"
fi
echo "  linux-chrome: $INSTALL_LINUX_CHROME"
echo "  extra-apps: ${RIDUX_EXTRA_APPS_WORDS:-<none>}"
