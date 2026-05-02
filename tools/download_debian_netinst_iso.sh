#!/usr/bin/env bash
set -euo pipefail

RIDUX_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CACHE_DIR="${RIDUX_ROOT}/third_party/cache/debian-iso"
OUT_ISO="${RIDUX_ROOT}/build/RiduxOS-Debian-Netinst.iso"
BASE_URL="${BASE_URL:-https://cdimage.debian.org/debian-cd/current/amd64/iso-cd}"

usage() {
  cat <<'EOF'
Usage:
  tools/download_debian_netinst_iso.sh [options]

Options:
  --output <path>        Output ISO path
  --base-url <url>       Debian index URL (default: current amd64 netinst folder)
  -h, --help             Show this help

Environment overrides:
  BASE_URL
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --output)
      OUT_ISO="$2"
      shift 2
      ;;
    --base-url)
      BASE_URL="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "[debian-netinst] unknown option: $1" >&2
      exit 2
      ;;
  esac
done

need_cmd() {
  local cmd="$1"
  command -v "$cmd" >/dev/null 2>&1 || {
    echo "[debian-netinst] required command not found: $cmd" >&2
    exit 3
  }
}

need_cmd curl
need_cmd grep
need_cmd sort
need_cmd head

mkdir -p "$CACHE_DIR" "$(dirname "$OUT_ISO")"

echo "[debian-netinst] source index: $BASE_URL/"
index_html="$(curl -fsSL "${BASE_URL}/")"
iso_name="$(printf '%s' "$index_html" | grep -Eo 'debian-[0-9.]+-amd64-netinst\.iso' | sort -u | head -n 1)"

if [[ -z "${iso_name:-}" ]]; then
  echo "[debian-netinst] could not resolve netinst ISO name from index" >&2
  exit 4
fi

iso_url="${BASE_URL}/${iso_name}"
iso_cache_path="${CACHE_DIR}/${iso_name}"

echo "[debian-netinst] resolved ISO: $iso_name"
echo "[debian-netinst] download URL: $iso_url"

if [[ ! -s "$iso_cache_path" ]]; then
  echo "[debian-netinst] downloading..."
  curl -fL "$iso_url" -o "${iso_cache_path}.part"
  mv "${iso_cache_path}.part" "$iso_cache_path"
else
  echo "[debian-netinst] using cached ISO: $iso_cache_path"
fi

cp -f "$iso_cache_path" "$OUT_ISO"

echo "[debian-netinst] done"
echo "  ISO: $OUT_ISO"
