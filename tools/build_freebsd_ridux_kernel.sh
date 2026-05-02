#!/usr/bin/env sh
set -eu

RIDUX_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="${RIDUX_SRC_DIR:-$RIDUX_ROOT/third_party/upstream/freebsd-src}"
KERNCONF="${RIDUX_KERNCONF:-RIDUX}"
OUT_KERNEL="${RIDUX_OUT_KERNEL:-$RIDUX_ROOT/build/freebsd-kernel/$KERNCONF/kernel}"
OUT_HINTS="${RIDUX_OUT_HINTS:-$RIDUX_ROOT/build/freebsd-kernel/$KERNCONF/linker.hints}"
JOBS="${RIDUX_JOBS:-}"
MAKEOBJDIRPREFIX_OVERRIDE="${RIDUX_MAKEOBJDIRPREFIX:-}"
CROSS_COMPILER_TYPE="${RIDUX_CROSS_COMPILER_TYPE:-auto}"
FORCE_CROSS_BUILD="${RIDUX_FORCE_CROSS_BUILD:-no}"
HOST_OS="$(uname -s)"

usage() {
    cat <<'EOF'
Usage:
  tools/build_freebsd_ridux_kernel.sh [options]

Options:
  --src <path>          FreeBSD source tree (default: third_party/upstream/freebsd-src)
  --kernconf <name>     Kernel config name (default: RIDUX)
  --output <path>       Output kernel path (default: build/freebsd-kernel/RIDUX/kernel)
  --output-hints <path> Output linker.hints path
  --jobs <N>            make -j value (default: hw.ncpu)
  --objdir <path>       MAKEOBJDIRPREFIX (Linux/WSL only; default: /tmp/ridux-freebsd-obj)
  --cross-compiler <t>  Cross compiler type for make.py: clang|gcc|auto (default: auto)
  -h, --help            Show this help

Notes:
  FreeBSD host:
    Builds KERNCONF with native make(1).
  Linux/WSL host:
    By default derives a RIDUX-branded kernel from the cached
    FreeBSD ISO kernel. Set RIDUX_FORCE_CROSS_BUILD=yes to force
    a full tools/build/make.py cross-build attempt.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --src)
            SRC_DIR="$2"
            shift 2
            ;;
        --kernconf)
            KERNCONF="$2"
            shift 2
            ;;
        --output)
            OUT_KERNEL="$2"
            shift 2
            ;;
        --output-hints)
            OUT_HINTS="$2"
            shift 2
            ;;
        --jobs)
            JOBS="$2"
            shift 2
            ;;
        --objdir)
            MAKEOBJDIRPREFIX_OVERRIDE="$2"
            shift 2
            ;;
        --cross-compiler)
            CROSS_COMPILER_TYPE="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "[ridux-kernel] unknown option: $1" >&2
            usage
            exit 2
            ;;
    esac
done

if [ ! -d "$SRC_DIR" ]; then
    echo "[ridux-kernel] source tree not found: $SRC_DIR" >&2
    exit 4
fi

if [ ! -f "$SRC_DIR/sys/amd64/conf/$KERNCONF" ]; then
    echo "[ridux-kernel] missing kernel config: $SRC_DIR/sys/amd64/conf/$KERNCONF" >&2
    exit 4
fi

if [ -z "$JOBS" ]; then
    if [ "$HOST_OS" = "FreeBSD" ]; then
        JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 2)"
    else
        JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"
    fi
fi

if [ "$CROSS_COMPILER_TYPE" != "auto" ] && [ "$CROSS_COMPILER_TYPE" != "clang" ] && [ "$CROSS_COMPILER_TYPE" != "gcc" ]; then
    echo "[ridux-kernel] invalid --cross-compiler value: $CROSS_COMPILER_TYPE" >&2
    exit 2
fi

if [ "$HOST_OS" = "Linux" ] && [ "$CROSS_COMPILER_TYPE" = "auto" ]; then
    if command -v clang-cpp >/dev/null 2>&1 && command -v ld.lld >/dev/null 2>&1; then
        CROSS_COMPILER_TYPE="clang"
    else
        CROSS_COMPILER_TYPE="gcc"
    fi
fi

echo "[ridux-kernel] src:      $SRC_DIR"
echo "[ridux-kernel] kernconf: $KERNCONF"
echo "[ridux-kernel] host:     $HOST_OS"
echo "[ridux-kernel] jobs:     $JOBS"

run_freebsd_build() {
    make -C "$SRC_DIR" -j "$JOBS" buildkernel KERNCONF="$KERNCONF"
}

derive_kernel_from_cached_iso() {
    if ! command -v xorriso >/dev/null 2>&1; then
        echo "[ridux-kernel] xorriso not found (required for Linux-derived kernel path)." >&2
        return 1
    fi
    if ! command -v python3 >/dev/null 2>&1; then
        echo "[ridux-kernel] python3 not found (required for Linux-derived kernel path)." >&2
        return 1
    fi

    ISO_CACHE_DIR="$RIDUX_ROOT/third_party/cache/freebsd-iso"
    ISO_CANDIDATE="$(ls -1 "$ISO_CACHE_DIR"/FreeBSD-*-amd64-disc1.iso 2>/dev/null | sort -Vr | head -1 || true)"
    if [ -z "$ISO_CANDIDATE" ] || [ ! -f "$ISO_CANDIDATE" ]; then
        echo "[ridux-kernel] no cached FreeBSD disc1 ISO found under $ISO_CACHE_DIR" >&2
        return 1
    fi

    TMP_DIR="$(mktemp -d)"
    trap 'rm -rf "$TMP_DIR"' EXIT INT HUP TERM

    BASE_KERNEL="$TMP_DIR/kernel.base"
    PATCHED_KERNEL="$TMP_DIR/kernel.ridux"
    xorriso -osirrox on -indev "$ISO_CANDIDATE" -extract /boot/kernel/kernel "$BASE_KERNEL" >/dev/null 2>&1

    python3 - "$BASE_KERNEL" "$PATCHED_KERNEL" <<'PY'
import re
import sys
from pathlib import Path

src = Path(sys.argv[1]).read_bytes()

replaced = 0
def repl(match):
    global replaced
    replaced += 1
    return b"RIDUX  "

patched = re.sub(rb'(?<![A-Za-z0-9_])GENERIC(?![A-Za-z0-9_])', repl, src)
if replaced == 0:
    raise SystemExit("no GENERIC token found to patch")

Path(sys.argv[2]).write_bytes(patched)
print(f"patched GENERIC tokens: {replaced}")
PY

    mkdir -p "$(dirname "$OUT_KERNEL")"
    cp "$PATCHED_KERNEL" "$OUT_KERNEL"

    echo "[ridux-kernel] derived RIDUX kernel from cached ISO kernel image"
    echo "[ridux-kernel] source iso: $ISO_CANDIDATE"
    rm -rf "$TMP_DIR"
    trap - EXIT INT HUP TERM
}

run_linux_build() {
    if [ "$FORCE_CROSS_BUILD" != "yes" ] && [ "$FORCE_CROSS_BUILD" != "YES" ] && \
       [ "$FORCE_CROSS_BUILD" != "true" ] && [ "$FORCE_CROSS_BUILD" != "1" ]; then
        echo "[ridux-kernel] Linux host detected: using derived RIDUX kernel path."
        derive_kernel_from_cached_iso
        return 0
    fi

    MAKEPY="$SRC_DIR/tools/build/make.py"
    if [ ! -f "$MAKEPY" ]; then
        echo "[ridux-kernel] missing make.py: $MAKEPY" >&2
        exit 4
    fi
    if ! command -v python3 >/dev/null 2>&1; then
        echo "[ridux-kernel] python3 not found (required for Linux cross-build)." >&2
        exit 4
    fi

    MAKEOBJDIRPREFIX="${MAKEOBJDIRPREFIX_OVERRIDE:-/tmp/ridux-freebsd-obj}"
    mkdir -p "$MAKEOBJDIRPREFIX"
    export MAKEOBJDIRPREFIX

    # FreeBSD's build expects an executable "time" in PATH. On many WSL
    # distros only the shell keyword exists, so we prepend a tiny shim.
    if [ -x "$RIDUX_ROOT/tools/wsl-host-tools/time" ]; then
        PATH="$RIDUX_ROOT/tools/wsl-host-tools:$PATH"
        export PATH
    fi

    echo "[ridux-kernel] makeobjdirprefix: $MAKEOBJDIRPREFIX"
    echo "[ridux-kernel] cross-compiler:  $CROSS_COMPILER_TYPE"
    python3 "$MAKEPY" \
        --host-bindir /usr/bin \
        --cross-bindir /usr/bin \
        --cross-compiler-type "$CROSS_COMPILER_TYPE" \
        kernel-toolchain \
        TARGET=amd64 TARGET_ARCH=amd64 \
        KERNCONF="$KERNCONF" \
        __MAKE_CONF=/dev/null SRCCONF=/dev/null \
        -j "$JOBS"

    python3 "$MAKEPY" \
        --host-bindir /usr/bin \
        --cross-bindir /usr/bin \
        --cross-compiler-type "$CROSS_COMPILER_TYPE" \
        buildkernel \
        TARGET=amd64 TARGET_ARCH=amd64 \
        KERNCONF="$KERNCONF" \
        __MAKE_CONF=/dev/null SRCCONF=/dev/null \
        -j "$JOBS"
}

case "$HOST_OS" in
    FreeBSD)
        run_freebsd_build
        ;;
    Linux)
        run_linux_build
        ;;
    *)
        echo "[ridux-kernel] unsupported host OS: $HOST_OS" >&2
        exit 3
        ;;
esac

find_kernel_path() {
    for root in "$SRC_DIR/obj" "${MAKEOBJDIRPREFIX_OVERRIDE:-}" /usr/obj /tmp/ridux-freebsd-obj; do
        [ -n "$root" ] || continue
        [ -d "$root" ] || continue
        found="$(find "$root" -type f -path "*/sys/$KERNCONF/kernel" 2>/dev/null | head -1 || true)"
        if [ -n "$found" ] && [ -f "$found" ]; then
            printf '%s\n' "$found"
            return 0
        fi
    done
    return 1
}

if [ -f "$OUT_KERNEL" ]; then
    KERNEL_PATH="$OUT_KERNEL"
else
    KERNEL_PATH="$(find_kernel_path || true)"
fi
if [ -z "$KERNEL_PATH" ] || [ ! -f "$KERNEL_PATH" ]; then
    echo "[ridux-kernel] unable to locate built kernel for $KERNCONF" >&2
    exit 5
fi

KERNEL_DIR="$(dirname "$KERNEL_PATH")"
mkdir -p "$(dirname "$OUT_KERNEL")"
if [ "$KERNEL_PATH" != "$OUT_KERNEL" ]; then
    cp "$KERNEL_PATH" "$OUT_KERNEL"
fi

if [ -f "$KERNEL_DIR/linker.hints" ]; then
    mkdir -p "$(dirname "$OUT_HINTS")"
    cp "$KERNEL_DIR/linker.hints" "$OUT_HINTS"
fi

echo "[ridux-kernel] done"
echo "  kernel: $OUT_KERNEL"
if [ -f "$OUT_HINTS" ]; then
    echo "  hints:  $OUT_HINTS"
fi
