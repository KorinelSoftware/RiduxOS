#!/usr/bin/env bash
set -euo pipefail

RIDUX_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_DIR="${RIDUX_ROOT}/src"
OUT_ROOT="${RIDUX_ROOT}/freebsd/ridux-runtime/compat_snapshot"

SNAP_TAG="${1:-$(date -u +%Y%m%d-%H%M%S)}"
SNAP_DIR="${OUT_ROOT}/${SNAP_TAG}"

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "[compat-snapshot] missing required command: $1" >&2
    exit 2
  fi
}

need_cmd mkdir
need_cmd cp
need_cmd ls
need_cmd awk
need_cmd sed

if command -v sha256sum >/dev/null 2>&1; then
  SUM_CMD="sha256sum"
elif command -v shasum >/dev/null 2>&1; then
  SUM_CMD="shasum -a 256"
else
  echo "[compat-snapshot] missing sha256sum/shasum" >&2
  exit 2
fi

mkdir -p "$SNAP_DIR"

copy_file() {
  local src="$1"
  local dst="$2"
  if [[ ! -f "$src" ]]; then
    echo "[compat-snapshot] source file missing: $src" >&2
    exit 3
  fi
  cp "$src" "$dst"
}

copy_file "${SRC_DIR}/kernel.c" "${SNAP_DIR}/kernel.c"

mkdir -p "$SNAP_DIR/compat"
for f in "${SRC_DIR}"/compat/*.c "${SRC_DIR}"/compat/*.h; do
  [[ -f "$f" ]] || continue
  cp "$f" "$SNAP_DIR/compat/"
done
cp "${SRC_DIR}/flush.h" "$SNAP_DIR/"

manifest="${SNAP_DIR}/manifest.txt"
{
  echo "Ridux compat snapshot"
  echo "snapshot: ${SNAP_TAG}"
  echo "created_utc: $(date -u +"%Y-%m-%dT%H:%M:%SZ")"
  echo
  echo "files:"
  (cd "$SNAP_DIR" && find . -maxdepth 2 -type f \( -name '*.c' -o -name '*.h' \) | sort | sed 's#^\./#- #')
  echo
  echo "sha256:"
  (cd "$SNAP_DIR" && $SUM_CMD *.c *.h)
} > "$manifest"

latest_link="${OUT_ROOT}/latest"
if [[ -L "$latest_link" || -e "$latest_link" ]]; then
  rm -rf "$latest_link"
fi
ln -s "$SNAP_TAG" "$latest_link"

echo "[compat-snapshot] created: $SNAP_DIR"
echo "[compat-snapshot] latest -> $SNAP_TAG"
