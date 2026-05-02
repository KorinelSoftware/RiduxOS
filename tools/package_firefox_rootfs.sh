#!/usr/bin/env bash
set -euo pipefail

ROOTFS_DIR="${1:-rootfs}"
REQUESTED_BIN="${2:-}"

if [[ ! -d "$ROOTFS_DIR" ]]; then
  echo "[firefox-rootfs] rootfs dir not found: $ROOTFS_DIR" >&2
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
    third_party/browser/firefox/extracted/firefox/firefox \
    ./third_party/browser/firefox/extracted/firefox/firefox \
    /usr/lib/firefox-esr/firefox-esr \
    /usr/lib/firefox/firefox \
    /usr/lib64/firefox/firefox \
    /usr/bin/firefox-esr \
    /usr/bin/firefox
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
    done < <(grep -Eo '/[^[:space:]"\x27]*firefox[^[:space:]"\x27]*' "$cand" | sort -u)
  fi
  return 1
}

enqueue_firefox_bundle_elfs() {
  local dir="$1"
  local f
  [[ -d "$dir" ]] || return 0
  while IFS= read -r -d '' f; do
    readelf -h "$f" >/dev/null 2>&1 || continue
    enqueue_file "$f"
  done < <(find "$dir" -maxdepth 3 -type f -print0 2>/dev/null)
}

find_main_bin() {
  local cand resolved
  if [[ -n "$REQUESTED_BIN" ]]; then
    resolved="$(resolve_elf_bin "$REQUESTED_BIN" || true)"
    if [[ -n "$resolved" ]]; then
      echo "$resolved"
      return 0
    fi
    echo "[firefox-rootfs] requested binary is not a usable ELF: $REQUESTED_BIN" >&2
    return 1
  fi
  for cand in \
    third_party/browser/firefox/extracted/firefox/firefox \
    ./third_party/browser/firefox/extracted/firefox/firefox \
    /usr/lib/firefox-esr/firefox-esr \
    /usr/lib/firefox/firefox \
    /usr/lib64/firefox/firefox \
    /usr/bin/firefox-esr \
    /usr/bin/firefox
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
  echo "[firefox-rootfs] firefox/firefox-esr not found on host." >&2
  echo "[firefox-rootfs] install firefox-esr in WSL and rerun." >&2
  exit 2
fi
MAIN_BIN_REAL="$(readlink -f "$MAIN_BIN")"

if ! command -v ldd >/dev/null 2>&1; then
  echo "[firefox-rootfs] ldd is required." >&2
  exit 3
fi

if ! command -v readelf >/dev/null 2>&1; then
  echo "[firefox-rootfs] readelf is required (binutils)." >&2
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

BIN_DIR="$(dirname "$MAIN_BIN")"
enqueue_firefox_bundle_elfs "$BIN_DIR"

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
if [[ -d "$BIN_DIR" ]]; then
  case "$BIN_DIR" in
    /usr/*|/lib/*|/opt/*)
      mkdir -p "$ROOTFS_DIR$BIN_DIR"
      cp -rL "$BIN_DIR"/. "$ROOTFS_DIR$BIN_DIR"/
      ;;
  esac
fi

# Provide a stable run path inside RiduxOS.
mkdir -p "$ROOTFS_DIR/opt/firefox"
if [[ -d "$BIN_DIR" ]]; then
  cp -rL "$BIN_DIR"/. "$ROOTFS_DIR/opt/firefox"/
fi
cp -L "$MAIN_BIN" "$ROOTFS_DIR/opt/firefox/firefox"

# Ensure PT_INTERP exact path exists inside rootfs.
if [[ -n "$INTERP" && -n "$INTERP_REAL" && -e "$ROOTFS_DIR$INTERP_REAL" ]]; then
  mkdir -p "$ROOTFS_DIR$(dirname "$INTERP")"
  cp -L "$INTERP_REAL" "$ROOTFS_DIR$INTERP"
fi

if [[ -r /etc/ssl/certs/ca-certificates.crt ]]; then
  mkdir -p "$ROOTFS_DIR/etc/ssl/certs"
  cp -L /etc/ssl/certs/ca-certificates.crt "$ROOTFS_DIR/etc/ssl/certs/ca-certificates.crt"
elif [[ -d /etc/ssl/certs ]]; then
  mkdir -p "$ROOTFS_DIR/etc/ssl/certs"
  cp -r /etc/ssl/certs/. "$ROOTFS_DIR/etc/ssl/certs/"
fi

mkdir -p "$ROOTFS_DIR/opt/firefox"
MANIFEST="$ROOTFS_DIR/opt/firefox/MANIFEST.txt"
{
  echo "firefox_main=$MAIN_BIN"
  echo "ridux_run_primary=/opt/firefox/firefox"
  echo "interpreter=${INTERP:-none}"
  echo "interpreter_real=${INTERP_REAL:-none}"
  echo "copied_count=${#COPIED[@]}"
  printf '%s\n' "${COPIED[@]}" | sort -u
} > "$MANIFEST"

echo "[firefox-rootfs] done"
echo "  main binary: $MAIN_BIN"
echo "  files copied: ${#COPIED[@]}"
echo "  manifest: $MANIFEST"
