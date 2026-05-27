#!/usr/bin/env bash
set -euo pipefail

WAYFIRE_DIR="${1:-third_party/wayfire}"
TARGET="${2:-all}"
PATCH_DIR="${RIDUX_WAYFIRE_PATCH_DIR:-patches/wayfire}"

SRC_DIR="$WAYFIRE_DIR/src"
BUILD_DIR="$WAYFIRE_DIR/build"
INSTALL_DIR="$WAYFIRE_DIR/install"
JOBS="${RIDUX_WAYFIRE_JOBS:-2}"

MODULES=(
  "wf-config"
  "wayfire"
  "wayfire-plugins-extra"
  "wf-shell"
  "wcm"
)

if ! command -v meson >/dev/null 2>&1; then
  echo "[wayfire-build] meson is required." >&2
  exit 3
fi
if ! command -v ninja >/dev/null 2>&1; then
  echo "[wayfire-build] ninja is required." >&2
  exit 4
fi

if [[ ! -d "$SRC_DIR/wayfire/.git" ]]; then
  echo "[wayfire-build] Wayfire sources are missing." >&2
  echo "[wayfire-build] Run: make wayfire-source" >&2
  exit 2
fi

if [[ "${RIDUX_WAYFIRE_APPLY_PATCHES:-1}" == "1" ]]; then
  bash tools/apply_wayfire_patches.sh "$WAYFIRE_DIR" "$PATCH_DIR"
fi

export PKG_CONFIG_PATH="$PWD/$INSTALL_DIR/lib/pkgconfig:$PWD/$INSTALL_DIR/lib64/pkgconfig:$PWD/$INSTALL_DIR/lib/x86_64-linux-gnu/pkgconfig:${PKG_CONFIG_PATH:-}"
export LD_LIBRARY_PATH="$PWD/$INSTALL_DIR/lib:$PWD/$INSTALL_DIR/lib64:$PWD/$INSTALL_DIR/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}"

build_module() {
  local module="$1"
  local src="$SRC_DIR/$module"
  local build="$BUILD_DIR/$module"
  local setup_args=()

  add_meson_option_if_present() {
    local opt="$1"
    local value="$2"
    if [[ -f "$src/meson_options.txt" ]] &&
       grep -q "option('$opt'" "$src/meson_options.txt"; then
      setup_args+=("-D$opt=$value")
    fi
  }

  if [[ ! -f "$src/meson.build" ]]; then
    echo "[wayfire-build] skip $module: no meson.build"
    return 0
  fi

  echo "[wayfire-build] module: $module"
  case "$module" in
    wf-config)
      setup_args+=("-Dtests=disabled")
      ;;
    wayfire)
      add_meson_option_if_present "use_system_wlroots" "disabled"
      add_meson_option_if_present "use_system_wfconfig" "enabled"
      add_meson_option_if_present "xwayland" "disabled"
      add_meson_option_if_present "vulkan_effects" "false"
      add_meson_option_if_present "enable_openmp" "false"
      add_meson_option_if_present "tests" "disabled"
      ;;
  esac

  if [[ -f "$build/build.ninja" ]]; then
    meson setup "$build" "$src" --reconfigure "${setup_args[@]}"
  else
    meson setup "$build" "$src" \
      --prefix "$PWD/$INSTALL_DIR" \
      --buildtype release \
      --libdir lib \
      --wrap-mode default \
      "${setup_args[@]}"
  fi
  meson compile -C "$build" -j "$JOBS"
  meson install -C "$build"
}

mkdir -p "$BUILD_DIR" "$INSTALL_DIR"

if [[ "$TARGET" == "all" || "$TARGET" == "workspace" || "$TARGET" == "session" ]]; then
  for module in "${MODULES[@]}"; do
    build_module "$module"
  done
else
  build_module "$TARGET"
fi

echo "[wayfire-build] install: $INSTALL_DIR"
