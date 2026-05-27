#!/usr/bin/env bash
set -euo pipefail

HYPRLAND_DIR="${1:-third_party/hyprland}"
HYPRLAND_REF="${RIDUX_HYPRLAND_REF:-${2:-main}}"
NWG_DOCK_REF="${RIDUX_NWG_DOCK_HYPRLAND_REF:-${3:-master}}"
HYPR_DEPS_REF="${RIDUX_HYPR_DEPS_REF:-main}"
HYPRWIRE_REF="${RIDUX_HYPRWIRE_REF:-main}"
GLSLANG_REF="${RIDUX_GLSLANG_REF:-main}"
THIRD_PARTY_DEPS_REF="${RIDUX_THIRD_PARTY_DEPS_REF:-main}"
WAYLAND_PROTOCOLS_REF="${RIDUX_WAYLAND_PROTOCOLS_REF:-1.47}"
XKBCOMMON_REF="${RIDUX_XKBCOMMON_REF:-master}"
LIBINPUT_REF="${RIDUX_LIBINPUT_REF:-main}"
MODE="${4:-apply}"

SRC_DIR="$HYPRLAND_DIR/src"
MANIFEST="$HYPRLAND_DIR/source-manifest.txt"

if [[ "$MODE" != "apply" && "$MODE" != "--dry-run" ]]; then
  echo "usage: $0 [hyprland-dir] [hyprland-ref] [nwg-dock-ref] [apply|--dry-run]" >&2
  exit 2
fi

if ! command -v git >/dev/null 2>&1; then
  echo "[hyprland-source] git not found in PATH." >&2
  exit 3
fi

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
  local ref="$3"
  local recursive="$4"
  local git_exe win_dest rec_args=()
  git_exe="$(windows_git_exe)" || return 1
  win_dest="$(wslpath -w "$dest")"
  [[ "$recursive" == "1" ]] && rec_args+=(--recursive)
  echo "[hyprland-source] WSL git checkout failed; retrying with Windows Git."
  if "$git_exe" -c core.autocrlf=false clone "${rec_args[@]}" --depth 1 --branch "$ref" "$url" "$win_dest"; then
    return 0
  fi
  rm -rf "$dest"
  "$git_exe" -c core.autocrlf=false clone "${rec_args[@]}" --depth 1 "$url" "$win_dest"
}

update_with_windows_git() {
  local dest="$1"
  local ref="$2"
  local git_exe win_dest
  git_exe="$(windows_git_exe)" || return 1
  win_dest="$(wslpath -w "$dest")"
  echo "[hyprland-source] retrying update with Windows Git."
  "$git_exe" -C "$win_dest" fetch --depth 1 origin "$ref" >/dev/null 2>&1 ||
    "$git_exe" -C "$win_dest" fetch --depth 1 origin HEAD >/dev/null
  "$git_exe" -C "$win_dest" checkout --detach FETCH_HEAD >/dev/null
  "$git_exe" -C "$win_dest" submodule update --init --recursive --depth 1 >/dev/null 2>&1 || true
}

clone_repo() {
  local name="$1"
  local url="$2"
  local ref="$3"
  local recursive="$4"
  local dest="$SRC_DIR/$name"
  local rec_args=()

  echo "[hyprland-source] $url -> $dest"
  if [[ "$MODE" == "--dry-run" ]]; then
    return 0
  fi

  mkdir -p "$SRC_DIR"
  if [[ -d "$dest/.git" ]]; then
    if repo_dirty "$dest"; then
      echo "[hyprland-source] local edits detected in $dest; leaving checkout untouched."
      return 0
    fi
    if git -C "$dest" fetch --depth 1 origin "$ref" >/dev/null 2>&1 &&
       git -C "$dest" checkout --detach FETCH_HEAD >/dev/null; then
      git -C "$dest" submodule update --init --recursive --depth 1 >/dev/null 2>&1 || true
      return 0
    fi
    if git -C "$dest" fetch --depth 1 origin HEAD >/dev/null &&
       git -C "$dest" checkout --detach FETCH_HEAD >/dev/null; then
      git -C "$dest" submodule update --init --recursive --depth 1 >/dev/null 2>&1 || true
      return 0
    fi
    update_with_windows_git "$dest" "$ref"
    return 0
  fi

  [[ "$recursive" == "1" ]] && rec_args+=(--recursive)
  if ! git -c core.autocrlf=false clone "${rec_args[@]}" --filter=blob:none --depth 1 --branch "$ref" "$url" "$dest"; then
    rm -rf "$dest"
    if ! git -c core.autocrlf=false clone "${rec_args[@]}" --filter=blob:none --depth 1 "$url" "$dest"; then
      rm -rf "$dest"
      clone_with_windows_git "$url" "$dest" "$ref" "$recursive"
    fi
  fi
  git -C "$dest" submodule update --init --recursive --depth 1 >/dev/null 2>&1 || true
}

write_manifest() {
  mkdir -p "$HYPRLAND_DIR"
  {
    echo "source=hyprwm/nwg-piotr GitHub"
    echo "hyprland_ref=$HYPRLAND_REF"
    echo "nwg_dock_hyprland_ref=$NWG_DOCK_REF"
    echo "hypr_deps_ref=$HYPR_DEPS_REF"
    echo "hyprwire_ref=$HYPRWIRE_REF"
    echo "glslang_ref=$GLSLANG_REF"
    echo "third_party_deps_ref=$THIRD_PARTY_DEPS_REF"
    echo "wayland_protocols_ref=$WAYLAND_PROTOCOLS_REF"
    echo "xkbcommon_ref=$XKBCOMMON_REF"
    echo "libinput_ref=$LIBINPUT_REF"
    echo "src_dir=$SRC_DIR"
    echo "timestamp_utc=$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
    for repo in glslang pugixml tomlplusplus abseil-cpp re2 muparser wayland-protocols libxkbcommon libinput hyprwayland-scanner hyprutils hyprlang hyprcursor hyprgraphics hyprwire aquamarine Hyprland nwg-dock-hyprland; do
      if [[ -d "$SRC_DIR/$repo/.git" ]]; then
        printf "%s %s\n" "$repo" "$(git -C "$SRC_DIR/$repo" rev-parse HEAD 2>/dev/null || printf unknown)"
      else
        printf "%s missing\n" "$repo"
      fi
    done
  } > "$MANIFEST"
}

echo "[hyprland-source] dir: $HYPRLAND_DIR"
echo "[hyprland-source] Hyprland ref: $HYPRLAND_REF"
echo "[hyprland-source] nwg-dock-hyprland ref: $NWG_DOCK_REF"
echo "[hyprland-source] Hypr deps ref: $HYPR_DEPS_REF"
echo "[hyprland-source] hyprwire ref: $HYPRWIRE_REF"
echo "[hyprland-source] glslang ref: $GLSLANG_REF"
echo "[hyprland-source] third-party deps ref: $THIRD_PARTY_DEPS_REF"
echo "[hyprland-source] wayland-protocols ref: $WAYLAND_PROTOCOLS_REF"
echo "[hyprland-source] libxkbcommon ref: $XKBCOMMON_REF"
echo "[hyprland-source] libinput ref: $LIBINPUT_REF"
clone_repo glslang "https://github.com/KhronosGroup/glslang.git" "$GLSLANG_REF" 0
clone_repo pugixml "https://github.com/zeux/pugixml.git" "$THIRD_PARTY_DEPS_REF" 0
clone_repo tomlplusplus "https://github.com/marzer/tomlplusplus.git" "$THIRD_PARTY_DEPS_REF" 0
clone_repo abseil-cpp "https://github.com/abseil/abseil-cpp.git" "$THIRD_PARTY_DEPS_REF" 0
clone_repo re2 "https://github.com/google/re2.git" "$THIRD_PARTY_DEPS_REF" 0
clone_repo muparser "https://github.com/beltoforion/muparser.git" "$THIRD_PARTY_DEPS_REF" 0
clone_repo wayland-protocols "https://gitlab.freedesktop.org/wayland/wayland-protocols.git" "$WAYLAND_PROTOCOLS_REF" 0
clone_repo libxkbcommon "https://github.com/xkbcommon/libxkbcommon.git" "$XKBCOMMON_REF" 0
clone_repo libinput "https://gitlab.freedesktop.org/libinput/libinput.git" "$LIBINPUT_REF" 0
clone_repo hyprwayland-scanner "https://github.com/hyprwm/hyprwayland-scanner.git" "$HYPR_DEPS_REF" 0
clone_repo hyprutils "https://github.com/hyprwm/hyprutils.git" "$HYPR_DEPS_REF" 0
clone_repo hyprlang "https://github.com/hyprwm/hyprlang.git" "$HYPR_DEPS_REF" 0
clone_repo hyprcursor "https://github.com/hyprwm/hyprcursor.git" "$HYPR_DEPS_REF" 0
clone_repo hyprgraphics "https://github.com/hyprwm/hyprgraphics.git" "$HYPR_DEPS_REF" 0
clone_repo hyprwire "https://github.com/hyprwm/hyprwire.git" "$HYPRWIRE_REF" 0
clone_repo aquamarine "https://github.com/hyprwm/aquamarine.git" "$HYPR_DEPS_REF" 0
clone_repo Hyprland "https://github.com/hyprwm/Hyprland.git" "$HYPRLAND_REF" 1
clone_repo nwg-dock-hyprland "https://github.com/nwg-piotr/nwg-dock-hyprland.git" "$NWG_DOCK_REF" 0

if [[ "$MODE" != "--dry-run" ]]; then
  write_manifest
  echo "[hyprland-source] manifest: $MANIFEST"
fi
echo "[hyprland-source] done."
