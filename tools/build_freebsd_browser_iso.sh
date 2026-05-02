#!/usr/bin/env bash
set -euo pipefail

RIDUX_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CACHE_DIR="${RIDUX_ROOT}/third_party/cache/freebsd-iso"
OUT_DIR="${RIDUX_ROOT}/build"
# User-visible artifact name says only "Ridux". Internally we still
# build on top of FreeBSD bits, but the product is Ridux.
OUT_ISO="${OUT_DIR}/RiduxOS-Browser.iso"

RUNTIME_DIR="${RIDUX_ROOT}/freebsd/ridux-runtime"
RUNTIME_BROWSER="${RUNTIME_DIR}/ridux-browser.sh"
RUNTIME_APP="${RUNTIME_DIR}/ridux-app.sh"
RUNTIME_COMPAT="${RUNTIME_DIR}/ridux-compat.sh"
FLUSH_RUNTIME_DIR="${RIDUX_ROOT}/linux/ridux-runtime"
RUNTIME_UI="${FLUSH_RUNTIME_DIR}/ridux-ui.c"
RUNTIME_FLUSH_C="${FLUSH_RUNTIME_DIR}/ridux-flush.c"
RUNTIME_FLUSH_H="${FLUSH_RUNTIME_DIR}/ridux-flush.h"
RUNTIME_ASSETS_H="${FLUSH_RUNTIME_DIR}/assets.h"

FREEBSD_RELEASE="${FREEBSD_RELEASE:-auto}"
RIDUX_HOSTNAME="${RIDUX_HOSTNAME:-ridux}"
RIDUX_USER="${RIDUX_USER:-ridux}"
INSTALL_LINUX_CHROME="${INSTALL_LINUX_CHROME:-yes}"
RIDUX_EXTRA_APPS="${RIDUX_EXTRA_APPS:-vlc,thunderbird,gimp,libreoffice}"
RIDUX_FAST_MODE="${RIDUX_FAST_MODE:-no}"

usage() {
  cat <<'EOF'
Usage:
  tools/build_freebsd_browser_iso.sh [options]

Options:
  --release <X.Y-RELEASE|auto>   FreeBSD release to use (default: auto)
  --output <path>                Output ISO path
  --hostname <name>              Hostname for installed system (default: ridux)
  --user <name>                  Default user (default: ridux)
  --with-linux-chrome            Install linux-chrome in first-boot provisioning
  --without-linux-chrome         Skip linux-chrome installation
  --extra-apps <csv>             Extra apps to install via ridux-app (default: vlc,thunderbird,gimp,libreoffice)
  --no-extra-apps                Skip extra app installation
  --fast                         Fast profile: no linux-chrome and no extra apps
  -h, --help                     Show this help

Environment overrides:
  FREEBSD_RELEASE, RIDUX_HOSTNAME, RIDUX_USER, INSTALL_LINUX_CHROME, RIDUX_EXTRA_APPS, RIDUX_FAST_MODE
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
    --hostname)
      RIDUX_HOSTNAME="$2"
      shift 2
      ;;
    --user)
      RIDUX_USER="$2"
      shift 2
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
    --fast)
      RIDUX_FAST_MODE="yes"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "[freebsd-browser-iso] unknown option: $1" >&2
      usage
      exit 2
      ;;
  esac
done

need_cmd() {
  local cmd="$1"
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "[freebsd-browser-iso] required command not found: $cmd" >&2
    exit 3
  fi
}

need_file() {
  local path="$1"
  if [[ ! -f "$path" ]]; then
    echo "[freebsd-browser-iso] required file not found: $path" >&2
    exit 5
  fi
}

escape_sed_repl() {
  printf '%s' "$1" | sed -e 's/[\/&]/\\&/g'
}

append_payload_file() {
  local installer_file="$1"
  local dst="$2"
  local src="$3"
  local mode="$4"
  local tag="$5"

  need_file "$src"

  {
    printf 'cat > "%s" <<'\''%s'\''\n' "$dst" "$tag"
    cat "$src"
    printf '\n%s\n' "$tag"
    printf 'chmod %s "%s"\n\n' "$mode" "$dst"
  } >> "$installer_file"
}

need_cmd curl
need_cmd xz
need_cmd xorriso
need_cmd mktemp
need_cmd sed
need_cmd awk
need_cmd tr

need_file "$RUNTIME_BROWSER"
need_file "$RUNTIME_APP"
need_file "$RUNTIME_COMPAT"
need_file "$RUNTIME_UI"
need_file "$RUNTIME_FLUSH_C"
need_file "$RUNTIME_FLUSH_H"
need_file "$RUNTIME_ASSETS_H"

if [[ ! "$RIDUX_HOSTNAME" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]]; then
  echo "[freebsd-browser-iso] invalid hostname: $RIDUX_HOSTNAME" >&2
  exit 4
fi
if [[ ! "$RIDUX_USER" =~ ^[A-Za-z_][A-Za-z0-9_-]*$ ]]; then
  echo "[freebsd-browser-iso] invalid username: $RIDUX_USER" >&2
  exit 4
fi
if [[ -n "$RIDUX_EXTRA_APPS" && ! "$RIDUX_EXTRA_APPS" =~ ^[A-Za-z0-9._,-]+$ ]]; then
  echo "[freebsd-browser-iso] invalid --extra-apps list: $RIDUX_EXTRA_APPS" >&2
  exit 4
fi

case "$RIDUX_FAST_MODE" in
  yes|YES|true|TRUE|1)
    RIDUX_FAST_MODE="YES"
    ;;
  no|NO|false|FALSE|0)
    RIDUX_FAST_MODE="NO"
    ;;
  *)
    echo "[freebsd-browser-iso] RIDUX_FAST_MODE must be yes/no" >&2
    exit 4
    ;;
esac

if [[ "$RIDUX_FAST_MODE" = "YES" ]]; then
  INSTALL_LINUX_CHROME="no"
  RIDUX_EXTRA_APPS=""
fi

case "$INSTALL_LINUX_CHROME" in
  yes|YES|true|TRUE|1)
    INSTALL_LINUX_CHROME="YES"
    ;;
  no|NO|false|FALSE|0)
    INSTALL_LINUX_CHROME="NO"
    ;;
  *)
    echo "[freebsd-browser-iso] INSTALL_LINUX_CHROME must be yes/no" >&2
    exit 4
    ;;
esac

RIDUX_EXTRA_APPS_WORDS="$(printf '%s' "$RIDUX_EXTRA_APPS" | tr ',' ' ' | awk '{$1=$1; print}')"

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
    echo "[freebsd-browser-iso] release not found on download.freebsd.org: $rel" >&2
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

  echo "[freebsd-browser-iso] unable to auto-detect a downloadable release" >&2
  return 1
}

resolved="$(resolve_release "$FREEBSD_RELEASE")"
FREEBSD_RELEASE="${resolved%%|*}"
ISO_URL="${resolved#*|}"
ISO_BASENAME="FreeBSD-${FREEBSD_RELEASE}-amd64-disc1.iso"
ISO_XZ_PATH="${CACHE_DIR}/${ISO_BASENAME}.xz"
ISO_PATH="${CACHE_DIR}/${ISO_BASENAME}"

mkdir -p "$CACHE_DIR" "$(dirname "$OUT_ISO")" "$OUT_DIR"

echo "[freebsd-browser-iso] release: $FREEBSD_RELEASE"
echo "[freebsd-browser-iso] source:  $ISO_URL"
echo "[freebsd-browser-iso] output:  $OUT_ISO"
echo "[freebsd-browser-iso] fast-mode: $RIDUX_FAST_MODE"
echo "[freebsd-browser-iso] extra-apps: ${RIDUX_EXTRA_APPS_WORDS:-<none>}"

if [[ ! -s "$ISO_XZ_PATH" ]]; then
  echo "[freebsd-browser-iso] downloading base ISO archive..."
  curl -fL "$ISO_URL" -o "${ISO_XZ_PATH}.part"
  mv "${ISO_XZ_PATH}.part" "$ISO_XZ_PATH"
else
  echo "[freebsd-browser-iso] using cached archive: $ISO_XZ_PATH"
fi

if [[ ! -s "$ISO_PATH" || "$ISO_XZ_PATH" -nt "$ISO_PATH" ]]; then
  echo "[freebsd-browser-iso] decompressing ISO..."
  xz -dkc "$ISO_XZ_PATH" > "${ISO_PATH}.part"
  mv "${ISO_PATH}.part" "$ISO_PATH"
else
  echo "[freebsd-browser-iso] using cached ISO: $ISO_PATH"
fi

tmp_dir="$(mktemp -d)"
cleanup() {
  rm -rf "$tmp_dir"
}
trap cleanup EXIT

installer_cfg="${tmp_dir}/installerconfig"

cat > "$installer_cfg" <<'EOF'
PARTITIONS=DEFAULT
DISTRIBUTIONS="kernel.txz base.txz"
export nonInteractive="YES"
ROOTPASS_PLAIN="ridux"

#!/bin/sh
set -eux

# This setup script runs inside the target system chroot.

echo "hostname=__RIDUX_HOSTNAME__" >> /etc/rc.conf
echo "ifconfig_DEFAULT=DHCP" >> /etc/rc.conf
echo "sshd_enable=YES" >> /etc/rc.conf
echo "moused_enable=YES" >> /etc/rc.conf
echo "dbus_enable=YES" >> /etc/rc.conf
echo "linux_enable=YES" >> /etc/rc.conf
echo "ridux_firstboot_enable=YES" >> /etc/rc.conf
echo "vboxguest_enable=YES" >> /etc/rc.conf
echo "vboxservice_enable=YES" >> /etc/rc.conf

if ! pw usershow "__RIDUX_USER__" >/dev/null 2>&1; then
  pw useradd "__RIDUX_USER__" -m -G wheel,operator -s /bin/sh -w none || true
fi
if pw groupshow video >/dev/null 2>&1; then
  pw groupmod video -m "__RIDUX_USER__" || true
fi

mkdir -p /usr/local/bin /usr/local/etc/rc.d /usr/local/share/ridux "/home/__RIDUX_USER__"

EOF

append_payload_file "$installer_cfg" "/usr/local/bin/ridux-browser" "$RUNTIME_BROWSER" "0755" "RIDUX_BROWSER_EOF"
append_payload_file "$installer_cfg" "/usr/local/bin/ridux-app" "$RUNTIME_APP" "0755" "RIDUX_APP_EOF"
append_payload_file "$installer_cfg" "/usr/local/bin/ridux-compat" "$RUNTIME_COMPAT" "0755" "RIDUX_COMPAT_EOF"
append_payload_file "$installer_cfg" "/usr/local/share/ridux/ridux-ui.c" "$RUNTIME_UI" "0644" "RIDUX_UI_SRC_EOF"
append_payload_file "$installer_cfg" "/usr/local/share/ridux/ridux-flush.c" "$RUNTIME_FLUSH_C" "0644" "RIDUX_FLUSH_C_EOF"
append_payload_file "$installer_cfg" "/usr/local/share/ridux/ridux-flush.h" "$RUNTIME_FLUSH_H" "0644" "RIDUX_FLUSH_H_EOF"
append_payload_file "$installer_cfg" "/usr/local/share/ridux/assets.h" "$RUNTIME_ASSETS_H" "0644" "RIDUX_ASSETS_H_EOF"

cat >> "$installer_cfg" <<'EOF'
for compat_cmd in \
  abi6 pidfd io_uring statx sysplus \
  lsblk route mount lsdev catproc syscalls \
  threads ss elf64 pmm tasks paging shm timers fds \
  realsys mmaps procfs dynlink libc dlsym heap bsd b64 rng browser
do
  ln -sf /usr/local/bin/ridux-compat "/usr/local/bin/${compat_cmd}"
done

cat > "/home/__RIDUX_USER__/.xinitrc" <<'XINIT_EOF'
#!/bin/sh
service dbus onestart >/dev/null 2>&1 || true
xset -dpms s off s noblank || true
xsetroot -solid "#101826" || true
if command -v dbus-launch >/dev/null 2>&1; then
  exec dbus-launch --exit-with-session /usr/local/bin/ridux-ui
fi
exec /usr/local/bin/ridux-ui
XINIT_EOF

cat > "/home/__RIDUX_USER__/.profile" <<'PROFILE_EOF'
if [ -z "${DISPLAY:-}" ] && [ "$(tty)" = "/dev/ttyv0" ]; then
  exec startx
fi
PROFILE_EOF

chown "__RIDUX_USER__:__RIDUX_USER__" "/home/__RIDUX_USER__/.xinitrc" "/home/__RIDUX_USER__/.profile"
chmod 0755 "/home/__RIDUX_USER__/.xinitrc"
chmod 0644 "/home/__RIDUX_USER__/.profile"

if grep -q '^ttyv0' /etc/ttys; then
  sed -i '' 's#^ttyv0.*#ttyv0   "/usr/libexec/getty autologin __RIDUX_USER__"   xterm   onifexists secure#' /etc/ttys || true
fi

cat > /usr/local/etc/rc.d/ridux_firstboot <<'FIRSTBOOT_EOF'
#!/bin/sh
# PROVIDE: ridux_firstboot
# REQUIRE: NETWORKING LOGIN
# KEYWORD: nojail

. /etc/rc.subr

name="ridux_firstboot"
rcvar="${name}_enable"
start_cmd="${name}_start"
stop_cmd=":"

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

ridux_firstboot_start() {
  marker="/var/db/ridux/firstboot.done"
  mkdir -p /var/db/ridux
  if [ -f "$marker" ]; then
    sysrc "${name}_enable=NO" >/dev/null 2>&1 || true
    return 0
  fi

  env ASSUME_ALWAYS_YES=yes pkg bootstrap -f
  retry_cmd 4 pkg update -f || true

  pkg install -y \
    xorg \
    xinit \
    xauth \
    dbus \
    xterm \
    pkgconf \
    libX11 \
    binutils \
    virtualbox-ose-additions || true

  /usr/local/bin/ridux-app install firefox || true
  /usr/local/bin/ridux-app install chromium || true

  if [ "__INSTALL_LINUX_CHROME__" = "YES" ]; then
    /usr/local/bin/ridux-app ensure-linux || true
    /usr/local/bin/ridux-app install chrome || true
  fi

  for app in __RIDUX_EXTRA_APPS__; do
    [ -n "$app" ] || continue
    /usr/local/bin/ridux-app install "$app" || true
  done

  echo "[ridux-firstboot] building Ridux Flush/X11 shell..."
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

  for candidate in cc clang /usr/bin/cc; do
    if try_build_ui "$candidate" -O2; then
      RIDUX_UI_BUILT=YES
      break
    fi
  done

  if [ "$RIDUX_UI_BUILT" != "YES" ]; then
    pkg install -y llvm || pkg install -y llvm19 || pkg install -y llvm18 || true
    pkg install -y gcc || pkg install -y gcc14 || pkg install -y gcc13 || true
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

  if [ ! -x /usr/local/bin/ridux-ui ]; then
    cat > /usr/local/bin/ridux-ui <<'RIDUX_UI_FALLBACK_EOF'
#!/bin/sh
exec /usr/local/bin/ridux-browser firefox
RIDUX_UI_FALLBACK_EOF
    chmod +x /usr/local/bin/ridux-ui
  fi

  /usr/local/bin/ridux-compat abi6 >/var/log/ridux-abi6.log 2>&1 || true
  /usr/local/bin/ridux-app doctor >/var/log/ridux-doctor.log 2>&1 || true

  touch "$marker"
  sysrc "${name}_enable=NO" >/dev/null 2>&1 || true
  shutdown -r now || true
}

load_rc_config "$name"
: "${ridux_firstboot_enable:=NO}"
run_rc_command "$1"
FIRSTBOOT_EOF
chmod +x /usr/local/etc/rc.d/ridux_firstboot

cat > /usr/local/share/ridux/FIRST_BOOT.txt <<'README_EOF'
Ridux: direct user session profile.

- No login manager
- ttyv0 autologin -> startx -> Ridux UI
- Ridux UI uses the Flush/X11 runtime with Ridux assets
- Browser launcher: /usr/local/bin/ridux-browser
- App manager:     /usr/local/bin/ridux-app
- Compat bridge:   /usr/local/bin/ridux-compat (+ aliases: abi6, dynlink, mmaps...)

Kernel notes:
- This is the Ridux kernel (production track). Real Linux apps run via
  the Ridux Linux ABI runtime, including Firefox, Chromium, and Linux
  Chrome.
- The experimental from-scratch Ridux kernel lives under src/ in the
  source tree and is built/maintained as a separate "lab" track.
README_EOF

cat > /usr/local/share/ridux/INSTALL_NOTES.txt <<'NOTES_EOF'
Ridux installation notes
========================

Default credentials:
- root password: ridux
- user:          __RIDUX_USER__ (autologin on ttyv0 -> startx -> Ridux UI)

After first boot the system runs Ridux UI directly. From the UI or
shell you can launch:
- firefox, chromium                      (native Ridux apps)
- linux-chrome / google-chrome           (real Linux binary via Ridux
                                          Linux ABI runtime)

Compat / introspection helpers:
- abi6, statx, dynlink, mmaps, fds, ss
- ridux-compat browser check | run <name>
- ridux-app    install | remove | doctor | ensure-linux
NOTES_EOF
EOF

RIDUX_HOSTNAME_ESC="$(escape_sed_repl "$RIDUX_HOSTNAME")"
RIDUX_USER_ESC="$(escape_sed_repl "$RIDUX_USER")"
INSTALL_LINUX_CHROME_ESC="$(escape_sed_repl "$INSTALL_LINUX_CHROME")"
RIDUX_EXTRA_APPS_WORDS_ESC="$(escape_sed_repl "$RIDUX_EXTRA_APPS_WORDS")"

sed -i \
  -e "s/__RIDUX_HOSTNAME__/${RIDUX_HOSTNAME_ESC}/g" \
  -e "s/__RIDUX_USER__/${RIDUX_USER_ESC}/g" \
  -e "s/__INSTALL_LINUX_CHROME__/${INSTALL_LINUX_CHROME_ESC}/g" \
  -e "s/__RIDUX_EXTRA_APPS__/${RIDUX_EXTRA_APPS_WORDS_ESC}/g" \
  "$installer_cfg"

echo "[freebsd-browser-iso] building customized ISO..."
rm -f "${OUT_ISO}.part"
xorriso \
  -indev "$ISO_PATH" \
  -outdev "${OUT_ISO}.part" \
  -boot_image any keep \
  -map "$installer_cfg" /etc/installerconfig \
  -return_with SORRY 0 \
  -commit \
  -end >/dev/null

mv "${OUT_ISO}.part" "$OUT_ISO"

echo "[freebsd-browser-iso] done"
echo "  ISO: $OUT_ISO"
echo "  release: $FREEBSD_RELEASE"
echo "  fast-mode: $RIDUX_FAST_MODE"
echo "  linux-chrome: $INSTALL_LINUX_CHROME"
echo "  extra-apps: ${RIDUX_EXTRA_APPS_WORDS:-<none>}"
