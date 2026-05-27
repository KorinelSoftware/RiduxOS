#!/usr/bin/env bash
set -euo pipefail

RIDUX_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_DIR="${RIDUXBSD_SRC_DIR:-${RIDUX_ROOT}/third_party/upstream/freebsd-src}"
KERNCONF="${RIDUXBSD_KERNCONF:-RIDUX}"
TARGET="${RIDUXBSD_TARGET:-amd64}"
TARGET_ARCH="${RIDUXBSD_TARGET_ARCH:-amd64}"
BUILDNAME="${RIDUXBSD_BUILDNAME:-RIDUXBSD}"
WORK_DIR="${RIDUXBSD_WORK_DIR:-${RIDUX_ROOT}/build/riduxbsd-source}"
OBJ_DIR="${RIDUXBSD_OBJ_DIR:-${WORK_DIR}/obj}"
OVERLAY_DIR="${RIDUXBSD_OVERLAY_DIR:-${WORK_DIR}/overlay}"
OUT_ISO="${RIDUXBSD_OUT_ISO:-${RIDUX_ROOT}/build/RiduxOS-RIDUXBSD-amd64.iso}"
JOBS="${RIDUXBSD_JOBS:-}"
MODE="iso"
DRY_RUN="no"
ALLOW_LINUX_CROSS="${RIDUX_ALLOW_LINUX_CROSS:-no}"
SKIP_WORLD="${RIDUXBSD_SKIP_WORLD:-no}"
SKIP_KERNEL="${RIDUXBSD_SKIP_KERNEL:-no}"

usage() {
  cat <<'EOF'
Usage:
  tools/build_riduxbsd_release.sh [options]

Build RiduxBSD from the FreeBSD source tree. This is the real path:
FreeBSD src -> RiduxBSD world/kernel -> RiduxBSD release ISO.

Options:
  --src <path>          FreeBSD source tree (default: third_party/upstream/freebsd-src)
  --kernconf <name>     Kernel config (default: RIDUX)
  --work-dir <path>     Build work directory (default: build/riduxbsd-source)
  --objdir <path>       MAKEOBJDIRPREFIX (default: build/riduxbsd-source/obj)
  --overlay <path>      Release overlay dir (default: build/riduxbsd-source/overlay)
  --output <path>       Output ISO (default: build/RiduxOS-RIDUXBSD-amd64.iso)
  --jobs <N>            Parallel build jobs
  --world-only          Stop after buildworld/buildkernel
  --iso                 Build release ISO (default)
  --skip-world          Reuse existing buildworld output
  --skip-kernel         Reuse existing buildkernel output
  --dry-run             Print commands without executing them
  -h, --help            Show this help

Notes:
  The ISO build is intentionally FreeBSD-native. On Linux/WSL, this script
  refuses the ISO step unless RIDUX_ALLOW_LINUX_CROSS=yes is set, because
  FreeBSD release media needs FreeBSD makefs/mkimg/etdump semantics.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --src)
      SRC_DIR="$2"
      shift 2
      ;;
    --kernconf)
      KERNCONF="$2"
      shift 2
      ;;
    --work-dir)
      WORK_DIR="$2"
      shift 2
      ;;
    --objdir)
      OBJ_DIR="$2"
      shift 2
      ;;
    --overlay)
      OVERLAY_DIR="$2"
      shift 2
      ;;
    --output)
      OUT_ISO="$2"
      shift 2
      ;;
    --jobs)
      JOBS="$2"
      shift 2
      ;;
    --world-only)
      MODE="world"
      shift
      ;;
    --iso)
      MODE="iso"
      shift
      ;;
    --skip-world)
      SKIP_WORLD="yes"
      shift
      ;;
    --skip-kernel)
      SKIP_KERNEL="yes"
      shift
      ;;
    --dry-run)
      DRY_RUN="yes"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "[riduxbsd] unknown option: $1" >&2
      usage
      exit 2
      ;;
  esac
done

HOST_OS="$(uname -s)"
if [[ -z "$JOBS" ]]; then
  if [[ "$HOST_OS" == "FreeBSD" ]]; then
    JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 2)"
  else
    JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"
  fi
fi

need_file() {
  local path="$1"
  if [[ ! -f "$path" ]]; then
    echo "[riduxbsd] required file not found: $path" >&2
    exit 4
  fi
}

need_dir() {
  local path="$1"
  if [[ ! -d "$path" ]]; then
    echo "[riduxbsd] required directory not found: $path" >&2
    exit 4
  fi
}

run_cmd() {
  printf '[riduxbsd] +'
  printf ' %q' "$@"
  printf '\n'
  if [[ "$DRY_RUN" != "yes" ]]; then
    "$@"
  fi
}

run_env_cmd() {
  local env_spec="$1"
  shift
  printf '[riduxbsd] + %s' "$env_spec"
  printf ' %q' "$@"
  printf '\n'
  if [[ "$DRY_RUN" != "yes" ]]; then
    env "$env_spec" "$@"
  fi
}

prepare_source_tree() {
  need_dir "$SRC_DIR"
  need_file "$SRC_DIR/Makefile"
  run_cmd bash "$RIDUX_ROOT/tools/setup_freebsd_integration.sh" "$SRC_DIR"
  need_file "$SRC_DIR/sys/amd64/conf/$KERNCONF"
  need_file "$SRC_DIR/sys/conf/newvers.sh"
}

prepare_overlay() {
  if [[ "$DRY_RUN" == "yes" ]]; then
    echo "[riduxbsd] would prepare overlay: $OVERLAY_DIR"
    return 0
  fi

  rm -rf "$OVERLAY_DIR"
  mkdir -p \
    "$OVERLAY_DIR/usr/local/bin" \
    "$OVERLAY_DIR/usr/local/share/ridux" \
    "$OVERLAY_DIR/etc" \
    "$OVERLAY_DIR/boot"

  cp "$RIDUX_ROOT/freebsd/ridux-runtime/ridux-browser.sh" "$OVERLAY_DIR/usr/local/bin/ridux-browser"
  cp "$RIDUX_ROOT/freebsd/ridux-runtime/ridux-app.sh" "$OVERLAY_DIR/usr/local/bin/ridux-app"
  cp "$RIDUX_ROOT/freebsd/ridux-runtime/ridux-compat.sh" "$OVERLAY_DIR/usr/local/bin/ridux-compat"
  cp "$RIDUX_ROOT/freebsd/ridux-runtime/ridux-shell.sh" "$OVERLAY_DIR/usr/local/bin/ridux-shell"
  cp "$RIDUX_ROOT/freebsd/ridux-runtime/ridux-desktop.sh" "$OVERLAY_DIR/usr/local/bin/ridux-desktop"
  cp "$RIDUX_ROOT/freebsd/ridux-runtime/ridux-wayfire-session.sh" "$OVERLAY_DIR/usr/local/bin/ridux-wayfire-session"
  chmod 0755 "$OVERLAY_DIR/usr/local/bin/"ridux-*

  cat > "$OVERLAY_DIR/etc/rc.conf.local" <<'EOF_RC'
# RiduxBSD defaults. This file is part of the source-built release overlay.
hostname="ridux"
linux_enable="YES"
dbus_enable="YES"
seatd_enable="YES"
moused_enable="YES"
ifconfig_DEFAULT="DHCP"
EOF_RC

  cat > "$OVERLAY_DIR/boot/loader.conf" <<'EOF_LOADER'
# RiduxBSD loader defaults.
autoboot_delay="1"
beastie_disable="YES"
loader_logo="none"
kern.ridux.source_build="1"
EOF_LOADER

  cat > "$OVERLAY_DIR/usr/local/share/ridux/RIDUXBSD_SOURCE_BUILD.txt" <<EOF_NOTE
RiduxBSD source-built release

This image is built from third_party/upstream/freebsd-src with:
  KERNCONF=$KERNCONF
  TYPE=RiduxBSD
  BRANCH=RIDUX

Desktop target:
  /usr/local/bin/ridux-wayfire-session

This is not the old patched live ISO flow.
EOF_NOTE
}

freebsd_make() {
  run_env_cmd "MAKEOBJDIRPREFIX=$OBJ_DIR" \
    make -C "$SRC_DIR" \
    TARGET="$TARGET" TARGET_ARCH="$TARGET_ARCH" \
    KERNCONF="$KERNCONF" \
    __MAKE_CONF=/dev/null SRCCONF=/dev/null \
    "$@"
}

linux_makepy() {
  local makepy="$SRC_DIR/tools/build/make.py"
  need_file "$makepy"
  run_env_cmd "MAKEOBJDIRPREFIX=$OBJ_DIR" \
    python3 "$makepy" \
    --host-bindir /usr/bin \
    --cross-bindir /usr/bin \
    --cross-compiler-type clang \
    "$@" \
    TARGET="$TARGET" TARGET_ARCH="$TARGET_ARCH" \
    KERNCONF="$KERNCONF" \
    __MAKE_CONF=/dev/null SRCCONF=/dev/null
}

build_world_kernel() {
  mkdir -p "$OBJ_DIR"
  if [[ "$DRY_RUN" == "yes" ]]; then
    if [[ "$SKIP_WORLD" != "yes" ]]; then
      freebsd_make -j "$JOBS" buildworld
    fi
    if [[ "$SKIP_KERNEL" != "yes" ]]; then
      freebsd_make -j "$JOBS" buildkernel
    fi
    return 0
  fi

  if [[ "$HOST_OS" == "FreeBSD" ]]; then
    if [[ "$SKIP_WORLD" != "yes" ]]; then
      freebsd_make -j "$JOBS" buildworld
    fi
    if [[ "$SKIP_KERNEL" != "yes" ]]; then
      freebsd_make -j "$JOBS" buildkernel
    fi
    return 0
  fi

  if [[ "$HOST_OS" == "Linux" && "$ALLOW_LINUX_CROSS" == "yes" ]]; then
    if [[ "$SKIP_WORLD" != "yes" ]]; then
      linux_makepy -j "$JOBS" buildworld
    fi
    if [[ "$SKIP_KERNEL" != "yes" ]]; then
      linux_makepy -j "$JOBS" buildkernel
    fi
    return 0
  fi

  echo "[riduxbsd] host $HOST_OS is not enabled for full source builds." >&2
  echo "[riduxbsd] Use a FreeBSD builder and run: make riduxbsd-iso" >&2
  echo "[riduxbsd] Experimental Linux cross-build: RIDUX_ALLOW_LINUX_CROSS=yes make riduxbsd-world" >&2
  exit 6
}

build_release_iso() {
  if [[ "$HOST_OS" != "FreeBSD" && "$ALLOW_LINUX_CROSS" != "yes" && "$DRY_RUN" != "yes" ]]; then
    echo "[riduxbsd] release ISO generation is FreeBSD-native by design." >&2
    echo "[riduxbsd] Boot a FreeBSD builder, mount this repo, then run:" >&2
    echo "[riduxbsd]   make riduxbsd-iso" >&2
    exit 6
  fi

  mkdir -p "$WORK_DIR" "$(dirname "$OUT_ISO")"
  prepare_overlay

  local release_dir="$SRC_DIR/release"
  need_dir "$release_dir"

  run_env_cmd "MAKEOBJDIRPREFIX=$OBJ_DIR" \
    make -C "$release_dir" clean cdrom \
    WORLDDIR="$SRC_DIR" \
    TARGET="$TARGET" TARGET_ARCH="$TARGET_ARCH" \
    KERNCONF="$KERNCONF" \
    BUILDNAME="$BUILDNAME" \
    VOLUME_LABEL="RIDUXBSD_AMD64" \
    NOPORTS=1 NOSRC=1 NOPKG=1 NO_ROOT=1 \
    XTRADIR="$OVERLAY_DIR" \
    __MAKE_CONF=/dev/null SRCCONF=/dev/null

  if [[ "$DRY_RUN" == "yes" ]]; then
    return 0
  fi

  local iso_path
  iso_path="$(find "$OBJ_DIR" "$release_dir" -type f -name 'disc1.iso' 2>/dev/null | sort -Vr | head -1 || true)"
  if [[ -z "$iso_path" || ! -f "$iso_path" ]]; then
    echo "[riduxbsd] unable to locate generated disc1.iso" >&2
    exit 7
  fi
  cp "$iso_path" "$OUT_ISO"
  echo "[riduxbsd] ISO ready: $OUT_ISO"
}

echo "[riduxbsd] src:       $SRC_DIR"
echo "[riduxbsd] kernconf:  $KERNCONF"
echo "[riduxbsd] host:      $HOST_OS"
echo "[riduxbsd] jobs:      $JOBS"
echo "[riduxbsd] mode:      $MODE"
echo "[riduxbsd] objdir:    $OBJ_DIR"
echo "[riduxbsd] output:    $OUT_ISO"

prepare_source_tree
build_world_kernel

if [[ "$MODE" == "iso" ]]; then
  build_release_iso
else
  echo "[riduxbsd] world/kernel build step complete."
fi
