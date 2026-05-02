#!/usr/bin/env bash
set -euo pipefail

ROOTFS_DIR="${1:-rootfs}"
REQUESTED_BIN="${2:-}"

if [[ ! -d "$ROOTFS_DIR" ]]; then
  echo "[chromium-rootfs] rootfs dir not found: $ROOTFS_DIR" >&2
  exit 1
fi

resolve_elf_bin() {
  local cand="$1"
  local alt
  if [[ -x "$cand" ]] && readelf -h "$cand" >/dev/null 2>&1; then
    readlink -f "$cand"
    return 0
  fi
  for alt in \
    /usr/lib/chromium/chromium \
    /usr/lib/chromium-browser/chromium-browser \
    /usr/lib/chromium-browser/chromium \
    /opt/google/chrome/chrome
  do
    if [[ -x "$alt" ]] && readelf -h "$alt" >/dev/null 2>&1; then
      readlink -f "$alt"
      return 0
    fi
  done
  if [[ -r "$cand" ]]; then
    while IFS= read -r alt; do
      [[ -x "$alt" ]] || continue
      if readelf -h "$alt" >/dev/null 2>&1; then
        readlink -f "$alt"
        return 0
      fi
    done < <(grep -Eo '/[^[:space:]"'"'"']*chrom[^[:space:]"'"'"']*' "$cand" | sort -u)
  fi
  return 1
}

find_main_bin() {
  local cand resolved
  if [[ -n "$REQUESTED_BIN" ]]; then
    resolved="$(resolve_elf_bin "$REQUESTED_BIN" || true)"
    if [[ -n "$resolved" ]]; then
      echo "$resolved"
      return 0
    fi
    echo "[chromium-rootfs] requested binary is not a usable ELF: $REQUESTED_BIN" >&2
    return 1
  fi
  for cand in \
    /opt/google/chrome/chrome \
    /usr/bin/google-chrome \
    /usr/bin/chromium-browser \
    /usr/bin/chromium \
    /usr/lib/chromium/chromium \
    /snap/chromium/current/usr/lib/chromium-browser/chrome
  do
    resolved="$(resolve_elf_bin "$cand" || true)"
    if [[ -n "$resolved" ]]; then
      echo "$resolved"
      return 0
    fi
  done
  return 1
}

MAIN_BIN="$(find_main_bin || true)"
if [[ -z "$MAIN_BIN" ]]; then
  echo "[chromium-rootfs] chromium/google-chrome not found on host." >&2
  echo "[chromium-rootfs] install chromium in WSL and rerun." >&2
  exit 2
fi
MAIN_BIN_REAL="$(readlink -f "$MAIN_BIN")"

if ! command -v ldd >/dev/null 2>&1; then
  echo "[chromium-rootfs] ldd is required." >&2
  exit 3
fi

if ! command -v readelf >/dev/null 2>&1; then
  echo "[chromium-rootfs] readelf is required (binutils)." >&2
  exit 4
fi

declare -A SEEN
QUEUE=()
COPIED=()

enqueue_file() {
  local f="$1"
  if [[ -z "$f" ]]; then
    return 0
  fi
  if [[ ! -e "$f" ]]; then
    return 0
  fi
  f="$(readlink -f "$f")"
  if [[ -n "${SEEN[$f]:-}" ]]; then
    return 0
  fi
  QUEUE+=("$f")
}

copy_into_rootfs() {
  local src="$1"
  local dst="$ROOTFS_DIR$src"
  local soname=""
  local soname_dst=""
  mkdir -p "$(dirname "$dst")"
  cp -L "$src" "$dst"
  soname="$({ readelf -d "$src" 2>/dev/null || true; } | awk -F'[][]' '/SONAME/ {print $2; exit}')"
  if [[ -n "$soname" && "$soname" != "$(basename "$dst")" ]]; then
    soname_dst="$(dirname "$dst")/$soname"
    cp -L "$src" "$soname_dst"
  fi
  COPIED+=("$src")
}

enqueue_file "$MAIN_BIN"

INTERP="$(readelf -l "$MAIN_BIN" | awk '/Requesting program interpreter/ {gsub("\\[","",$NF); gsub("\\]","",$NF); print $NF; exit}')"
INTERP_REAL=""
if [[ -n "$INTERP" && -e "$INTERP" ]]; then
  INTERP_REAL="$(readlink -f "$INTERP")"
  enqueue_file "$INTERP_REAL"
fi

while [[ "${#QUEUE[@]}" -gt 0 ]]; do
  f="${QUEUE[0]}"
  QUEUE=("${QUEUE[@]:1}")
  if [[ -n "${SEEN[$f]:-}" ]]; then
    continue
  fi
  SEEN["$f"]=1
  if [[ "$f" != "$MAIN_BIN_REAL" ]]; then
    copy_into_rootfs "$f"
  fi

  while IFS= read -r dep; do
    [[ -z "$dep" ]] && continue
    enqueue_file "$dep"
  done < <(ldd "$f" 2>/dev/null | awk '
    /=> \// {print $3}
    /^[[:space:]]*\/lib/ {print $1}
    /^[[:space:]]*\/usr\/lib/ {print $1}
  ')
done

BIN_DIR="$(dirname "$MAIN_BIN")"

# Ensure PT_INTERP exact path exists inside rootfs even when host uses symlinked
# multiarch locations (e.g. /lib64 -> /usr/lib/x86_64-linux-gnu).
if [[ -n "$INTERP" && -n "$INTERP_REAL" && -e "$ROOTFS_DIR$INTERP_REAL" ]]; then
  mkdir -p "$ROOTFS_DIR$(dirname "$INTERP")"
  cp -L "$INTERP_REAL" "$ROOTFS_DIR$INTERP"
fi

# Provide a stable, predictable run path inside RiduxOS.
mkdir -p "$ROOTFS_DIR/opt/chromium"
cp -L "$MAIN_BIN" "$ROOTFS_DIR/opt/chromium/chrome"
if [[ -d "$BIN_DIR" ]]; then
  for extra in \
    chrome-sandbox \
    chrome_crashpad_handler \
    icudtl.dat \
    snapshot_blob.bin \
    v8_context_snapshot.bin \
    *.pak \
    *.bin \
    locales \
    resources
  do
    for path in "$BIN_DIR"/$extra; do
      local_name=""
      [[ -e "$path" ]] || continue
      rel="$(readlink -f "$path")"
      local_name="$(basename "$rel")"
      if [[ -d "$rel" ]]; then
        mkdir -p "$ROOTFS_DIR/opt/chromium/$local_name"
        cp -r "$rel"/. "$ROOTFS_DIR/opt/chromium/$local_name"/
      else
        cp -L "$rel" "$ROOTFS_DIR/opt/chromium/$local_name"
      fi
    done
  done
fi

if [[ -r /etc/ssl/certs/ca-certificates.crt ]]; then
  mkdir -p "$ROOTFS_DIR/etc/ssl/certs"
  cp -L /etc/ssl/certs/ca-certificates.crt "$ROOTFS_DIR/etc/ssl/certs/ca-certificates.crt"
elif [[ -d /etc/ssl/certs ]]; then
  mkdir -p "$ROOTFS_DIR/etc/ssl/certs"
  cp -r /etc/ssl/certs/. "$ROOTFS_DIR/etc/ssl/certs/"
fi

mkdir -p "$ROOTFS_DIR/opt/chromium"
MANIFEST="$ROOTFS_DIR/opt/chromium/MANIFEST.txt"
{
  echo "chromium_main=$MAIN_BIN"
  echo "ridux_run_primary=/opt/chromium/chrome"
  echo "interpreter=${INTERP:-none}"
  echo "interpreter_real=${INTERP_REAL:-none}"
  echo "copied_count=${#COPIED[@]}"
  printf '%s\n' "${COPIED[@]}" | sort -u
} > "$MANIFEST"

echo "[chromium-rootfs] done"
echo "  main binary: $MAIN_BIN"
echo "  files copied: ${#COPIED[@]}"
echo "  manifest: $MANIFEST"
