#!/usr/bin/env bash
set -euo pipefail

WAYFIRE_DIR="${1:-third_party/wayfire}"
REF="${RIDUX_WAYFIRE_REF:-${2:-master}}"
MODE="${3:-apply}"

SRC_DIR="$WAYFIRE_DIR/src"
MANIFEST="$WAYFIRE_DIR/source-manifest.txt"

REPOS=(
  "wf-config"
  "wayfire"
  "wayfire-plugins-extra"
  "wf-shell"
  "wcm"
)

if [[ "$MODE" != "apply" && "$MODE" != "--dry-run" ]]; then
  echo "usage: $0 [wayfire-dir] [ref] [apply|--dry-run]" >&2
  exit 2
fi

if ! command -v git >/dev/null 2>&1; then
  echo "[wayfire-source] git not found in PATH." >&2
  exit 3
fi

repo_url() {
  local repo="$1"
  printf "https://github.com/WayfireWM/%s.git" "$repo"
}

repo_dirty() {
  local dir="$1"
  [[ -n "$(git -C "$dir" status --porcelain 2>/dev/null)" ]]
}

windows_git_exe() {
  local win_path
  if ! command -v cmd.exe >/dev/null 2>&1 || ! command -v wslpath >/dev/null 2>&1; then
    return 1
  fi
  win_path="$(cmd.exe /c where git 2>/dev/null | tr -d '\r' | head -n 1)"
  [[ -n "$win_path" ]] || return 1
  wslpath -u "$win_path"
}

clone_with_windows_git() {
  local url="$1"
  local dest="$2"
  local git_exe win_dest
  git_exe="$(windows_git_exe)" || return 1
  win_dest="$(wslpath -w "$dest")"
  echo "[wayfire-source] WSL git checkout failed; retrying with Windows Git."
  if "$git_exe" -c core.autocrlf=false clone --depth 1 --branch "$REF" "$url" "$win_dest"; then
    return 0
  fi
  rm -rf "$dest"
  "$git_exe" -c core.autocrlf=false clone --depth 1 "$url" "$win_dest"
}

update_with_windows_git() {
  local dest="$1"
  local git_exe win_dest
  git_exe="$(windows_git_exe)" || return 1
  win_dest="$(wslpath -w "$dest")"
  echo "[wayfire-source] retrying update with Windows Git."
  "$git_exe" -C "$win_dest" fetch --depth 1 origin "$REF" >/dev/null 2>&1 ||
    "$git_exe" -C "$win_dest" fetch --depth 1 origin HEAD >/dev/null
  "$git_exe" -C "$win_dest" checkout --detach FETCH_HEAD >/dev/null
}

clone_repo() {
  local repo="$1"
  local url dest
  url="$(repo_url "$repo")"
  dest="$SRC_DIR/$repo"

  echo "[wayfire-source] WayfireWM/$repo -> $dest"
  if [[ "$MODE" == "--dry-run" ]]; then
    return 0
  fi

  mkdir -p "$SRC_DIR"
  if [[ -d "$dest/.git" ]]; then
    if repo_dirty "$dest"; then
      echo "[wayfire-source] local edits detected in $dest; leaving checkout untouched."
      return 0
    fi
    if git -C "$dest" fetch --depth 1 origin "$REF" >/dev/null 2>&1 &&
       git -C "$dest" checkout --detach FETCH_HEAD >/dev/null; then
      return 0
    fi
    if git -C "$dest" fetch --depth 1 origin HEAD >/dev/null &&
       git -C "$dest" checkout --detach FETCH_HEAD >/dev/null; then
      return 0
    fi
    update_with_windows_git "$dest"
    return 0
  fi

  if ! git -c core.autocrlf=false clone --filter=blob:none --depth 1 --branch "$REF" "$url" "$dest"; then
    rm -rf "$dest"
    if ! git -c core.autocrlf=false clone --filter=blob:none --depth 1 "$url" "$dest"; then
      rm -rf "$dest"
      clone_with_windows_git "$url" "$dest"
    fi
  fi
}

write_manifest() {
  local repo dest url
  mkdir -p "$WAYFIRE_DIR"
  {
    echo "source=WayfireWM GitHub"
    echo "ref=$REF"
    echo "src_dir=$SRC_DIR"
    echo "timestamp_utc=$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
    for repo in "${REPOS[@]}"; do
      dest="$SRC_DIR/$repo"
      url="$(repo_url "$repo")"
      if [[ -d "$dest/.git" ]]; then
        printf "%s %s %s\n" "$repo" "$(git -C "$dest" rev-parse HEAD 2>/dev/null || printf unknown)" "$url"
      else
        printf "%s missing %s\n" "$repo" "$url"
      fi
    done
  } > "$MANIFEST"
}

echo "[wayfire-source] dir: $WAYFIRE_DIR"
echo "[wayfire-source] ref: $REF"
for repo in "${REPOS[@]}"; do
  clone_repo "$repo"
done

if [[ "$MODE" != "--dry-run" ]]; then
  write_manifest
  echo "[wayfire-source] manifest: $MANIFEST"
fi
echo "[wayfire-source] done."
