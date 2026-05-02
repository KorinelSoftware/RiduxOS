#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TP_DIR="$ROOT_DIR/third_party/upstream"
MANIFEST="$ROOT_DIR/third_party/upstream-manifest.txt"

usage() {
  cat <<'EOF'
Usage:
  bash tools/vendor_upstream.sh [network|all]

Profiles:
  network -> lwIP + mbedTLS (recomendado para empezar Chrome real)
  all     -> actualmente igual a network
EOF
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "[vendor] missing command: $1" >&2
    exit 1
  }
}

download_extract() {
  local name="$1"
  local url="$2"
  local license="$3"
  local archive tmp extracted_dir
  archive="$TP_DIR/${name}.tar.gz"
  tmp="$TP_DIR/.tmp_${name}"

  rm -rf "$tmp"
  mkdir -p "$tmp"

  echo "[vendor] download $name"
  curl -L --fail --retry 3 --retry-delay 2 -o "$archive" "$url"

  echo "[vendor] extract $name"
  # -m avoids utime failures on some Windows/WSL mounts.
  tar -xzmf "$archive" -C "$tmp"
  extracted_dir="$(find "$tmp" -mindepth 1 -maxdepth 1 -type d | head -n1 || true)"
  if [[ -z "$extracted_dir" ]]; then
    echo "[vendor] failed to extract $name from $url" >&2
    exit 1
  fi

  rm -rf "$TP_DIR/$name"
  mv "$extracted_dir" "$TP_DIR/$name"
  rm -rf "$tmp"

  {
    printf '%s\t%s\t%s\t%s\n' \
      "$name" "$url" "$(basename "$archive")" "$license"
  } >> "$MANIFEST"
}

profile="${1:-all}"
case "$profile" in
  network|all) ;;
  -h|--help|help) usage; exit 0 ;;
  *)
    echo "[vendor] unknown profile: $profile" >&2
    usage
    exit 2
    ;;
esac

need_cmd curl
need_cmd tar
mkdir -p "$TP_DIR"

: > "$MANIFEST"
{
  printf "# RiduxOS upstream third_party manifest\n"
  printf "# generated_at_utc=%s\n" "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  printf "# columns: name archive_url local_archive license\n"
} >> "$MANIFEST"

download_extract \
  "lwip" \
  "https://github.com/lwip-tcpip/lwip/archive/refs/tags/STABLE-2_2_1_RELEASE.tar.gz" \
  "BSD-3-Clause"

download_extract \
  "mbedtls" \
  "https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/v3.6.2.tar.gz" \
  "Apache-2.0"

echo "[vendor] done"
echo "[vendor] manifest: $MANIFEST"
