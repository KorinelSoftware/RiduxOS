#!/usr/bin/env bash
set -euo pipefail

DEST="${1:-third_party/upstream/freebsd-src}"
REF="${2:-stable/14}"
MODE="${3:-apply}"

REPO_URL="https://github.com/freebsd/freebsd-src.git"
MANIFEST_PATH="third_party/upstream/freebsd-manifest.txt"

to_host_path() {
  if command -v wslpath >/dev/null 2>&1; then
    wslpath -w "$1"
  else
    printf "%s" "$1"
  fi
}

clone_with_fallback() {
  local dest="$1"
  if git -c core.autocrlf=false clone --depth 1 --branch "$REF" "$REPO_URL" "$dest"; then
    return 0
  fi
  if ! command -v git.exe >/dev/null 2>&1; then
    return 1
  fi
  echo "[freebsd-bootstrap] retry clone with git.exe..."
  rm -rf "$dest"
  git.exe -c core.autocrlf=false clone --depth 1 --branch "$REF" "$REPO_URL" "$(to_host_path "$dest")"
}

fetch_checkout_with_fallback() {
  local dest="$1"
  if git -C "$dest" fetch --depth 1 origin "$REF" && git -C "$dest" checkout -f FETCH_HEAD; then
    return 0
  fi
  if ! command -v git.exe >/dev/null 2>&1; then
    return 1
  fi
  echo "[freebsd-bootstrap] retry fetch/checkout with git.exe..."
  git.exe -C "$(to_host_path "$dest")" -c core.autocrlf=false config core.autocrlf false
  git.exe -C "$(to_host_path "$dest")" fetch --depth 1 origin "$REF"
  git.exe -C "$(to_host_path "$dest")" checkout -f FETCH_HEAD
}

rev_parse_with_fallback() {
  local dest="$1"
  if git -C "$dest" rev-parse HEAD 2>/dev/null; then
    return 0
  fi
  if command -v git.exe >/dev/null 2>&1; then
    git.exe -C "$(to_host_path "$dest")" rev-parse HEAD
    return 0
  fi
  return 1
}

if [[ "$MODE" != "apply" && "$MODE" != "--dry-run" ]]; then
  echo "usage: $0 [dest] [ref] [apply|--dry-run]" >&2
  exit 2
fi

echo "[freebsd-bootstrap] repo: $REPO_URL"
echo "[freebsd-bootstrap] dest: $DEST"
echo "[freebsd-bootstrap] ref:  $REF"

if [[ "$MODE" == "--dry-run" ]]; then
  echo "[freebsd-bootstrap] dry-run: no changes made."
  exit 0
fi

if ! command -v git >/dev/null 2>&1; then
  echo "[freebsd-bootstrap] git not found in PATH." >&2
  exit 3
fi

if [[ -d "$DEST/.git" ]]; then
  echo "[freebsd-bootstrap] updating existing tree..."
  fetch_checkout_with_fallback "$DEST"
else
  echo "[freebsd-bootstrap] cloning source tree..."
  mkdir -p "$(dirname "$DEST")"
  clone_with_fallback "$DEST"
fi

mkdir -p "$(dirname "$MANIFEST_PATH")"
{
  echo "repo=$REPO_URL"
  echo "ref=$REF"
  echo "dest=$DEST"
  echo "timestamp_utc=$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
  rev_parse_with_fallback "$DEST" | sed 's/^/commit=/'
} > "$MANIFEST_PATH"

echo "[freebsd-bootstrap] done."
echo "[freebsd-bootstrap] manifest: $MANIFEST_PATH"
