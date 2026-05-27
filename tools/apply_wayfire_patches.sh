#!/usr/bin/env bash
set -euo pipefail

WAYFIRE_DIR="${1:-third_party/wayfire}"
PATCH_DIR="${2:-patches/wayfire}"
SRC_DIR="$WAYFIRE_DIR/src"

if [[ ! -d "$SRC_DIR" ]]; then
  echo "[wayfire-patches] source dir not found: $SRC_DIR" >&2
  echo "[wayfire-patches] Run: make wayfire-source" >&2
  exit 2
fi

if [[ ! -d "$PATCH_DIR" ]]; then
  echo "[wayfire-patches] no patch directory yet: $PATCH_DIR"
  exit 0
fi

apply_patch_file() {
  local repo="$1"
  local patch="$2"
  local repo_dir="$SRC_DIR/$repo"
  patch="$(cd "$(dirname "$patch")" && pwd -P)/$(basename "$patch")"

  if [[ ! -d "$repo_dir/.git" ]]; then
    echo "[wayfire-patches] skip $repo: source checkout missing"
    return 0
  fi

  if git -C "$repo_dir" apply --reverse --check "$patch" >/dev/null 2>&1; then
    echo "[wayfire-patches] already applied: $repo/$(basename "$patch")"
    return 0
  fi

  echo "[wayfire-patches] apply: $repo/$(basename "$patch")"
  git -C "$repo_dir" apply --whitespace=nowarn "$patch"
}

found=0
while IFS= read -r -d '' patch; do
  found=1
  repo="$(basename "$(dirname "$patch")")"
  apply_patch_file "$repo" "$patch"
done < <(find "$PATCH_DIR" -mindepth 2 -maxdepth 2 -type f -name '*.patch' -print0 | sort -z)

if [[ "$found" -eq 0 ]]; then
  echo "[wayfire-patches] patch directory exists but has no module patches."
fi
