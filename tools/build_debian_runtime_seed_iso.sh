#!/usr/bin/env bash
set -euo pipefail

RIDUX_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_ISO="${RIDUX_ROOT}/build/RiduxOS-Debian-Seed.iso"
RUNTIME_DIR="${RIDUX_ROOT}/linux/ridux-runtime"
SETUP_SCRIPT="${RIDUX_ROOT}/tools/setup_debian_ridux_runtime.sh"

usage() {
  cat <<'EOF'
Usage:
  tools/build_debian_runtime_seed_iso.sh [options]

Options:
  --output <path>        Output ISO path
  --runtime-dir <path>   Runtime source directory
  -h, --help             Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --output)
      OUT_ISO="$2"
      shift 2
      ;;
    --runtime-dir)
      RUNTIME_DIR="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "[debian-seed-iso] unknown option: $1" >&2
      exit 2
      ;;
  esac
done

need_cmd() {
  local cmd="$1"
  command -v "$cmd" >/dev/null 2>&1 || {
    echo "[debian-seed-iso] required command not found: $cmd" >&2
    exit 3
  }
}

need_file() {
  local path="$1"
  [[ -f "$path" ]] || {
    echo "[debian-seed-iso] required file not found: $path" >&2
    exit 4
  }
}

need_cmd xorriso
need_file "$SETUP_SCRIPT"
need_file "$RUNTIME_DIR/ridux-ui.c"
need_file "$RUNTIME_DIR/ridux-flush.c"
need_file "$RUNTIME_DIR/ridux-flush.h"
need_file "$RUNTIME_DIR/ridux-app.sh"
need_file "$RUNTIME_DIR/ridux-browser.sh"
need_file "$RUNTIME_DIR/ridux-compat.sh"

tmp_dir="$(mktemp -d)"
cleanup() {
  rm -rf "$tmp_dir"
}
trap cleanup EXIT

mkdir -p "$tmp_dir/ridux-runtime"
cp "$SETUP_SCRIPT" "$tmp_dir/setup_debian_ridux_runtime.sh"
cp "$RUNTIME_DIR"/ridux-* "$tmp_dir/ridux-runtime/"
chmod 0755 "$tmp_dir/setup_debian_ridux_runtime.sh"
chmod 0755 "$tmp_dir/ridux-runtime/ridux-app.sh" "$tmp_dir/ridux-runtime/ridux-browser.sh" "$tmp_dir/ridux-runtime/ridux-compat.sh"

cat > "$tmp_dir/README.txt" <<'EOF'
Ridux Debian runtime seed ISO

Inside guest Debian:

  sudo bash /media/cdrom/setup_debian_ridux_runtime.sh --user ridux --fast

If the seed ISO mounts as another device:

  ls /media
  ls /media/$USER
  sudo bash /media/<mountpoint>/setup_debian_ridux_runtime.sh --user ridux --fast

This installs:
- /usr/local/bin/ridux-browser
- /usr/local/bin/ridux-app
- /usr/local/bin/ridux-compat (+ aliases)
- /usr/local/bin/ridux-ui
- tty1 autologin -> startx -> Ridux UI
EOF

mkdir -p "$(dirname "$OUT_ISO")"
rm -f "${OUT_ISO}.part"

xorriso -as mkisofs \
  -V RIDUX_DEBIAN_SEED \
  -o "${OUT_ISO}.part" \
  "$tmp_dir" >/dev/null

mv "${OUT_ISO}.part" "$OUT_ISO"

echo "[debian-seed-iso] done"
echo "  ISO: $OUT_ISO"
