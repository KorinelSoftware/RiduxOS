#!/usr/bin/env bash
set -euo pipefail

HYPRLAND_DIR="${1:-third_party/hyprland}"
TARGET="${2:-all}"

SRC_DIR="$HYPRLAND_DIR/src"
BUILD_DIR="$HYPRLAND_DIR/build"
INSTALL_DIR="$HYPRLAND_DIR/install"
SYSROOT_DIR="$HYPRLAND_DIR/sysroot"
APT_CACHE_DIR="$HYPRLAND_DIR/pkg-cache"
JOBS="${RIDUX_HYPRLAND_JOBS:-2}"

HYPR_SRC="$SRC_DIR/Hyprland"
NWG_SRC="$SRC_DIR/nwg-dock-hyprland"
GLSLANG_SRC="$SRC_DIR/glslang"
PUGIXML_SRC="$SRC_DIR/pugixml"
TOMLPLUSPLUS_SRC="$SRC_DIR/tomlplusplus"
ABSEIL_SRC="$SRC_DIR/abseil-cpp"
RE2_SRC="$SRC_DIR/re2"
MUPARSER_SRC="$SRC_DIR/muparser"
WAYLAND_PROTOCOLS_SRC="$SRC_DIR/wayland-protocols"
XKBCOMMON_SRC="$SRC_DIR/libxkbcommon"
LIBINPUT_SRC="$SRC_DIR/libinput"
HYPRWAYLAND_SCANNER_SRC="$SRC_DIR/hyprwayland-scanner"
HYPRUTILS_SRC="$SRC_DIR/hyprutils"
HYPRLANG_SRC="$SRC_DIR/hyprlang"
HYPRCURSOR_SRC="$SRC_DIR/hyprcursor"
HYPRGRAPHICS_SRC="$SRC_DIR/hyprgraphics"
HYPRWIRE_SRC="$SRC_DIR/hyprwire"
AQUAMARINE_SRC="$SRC_DIR/aquamarine"
HOST_TRIPLET="${RIDUX_HYPRLAND_HOST_TRIPLET:-x86_64-linux-gnu}"
LUA_VERSION="${RIDUX_HYPRLAND_LUA_VERSION:-5.5.0}"
LUA_SHA256="${RIDUX_HYPRLAND_LUA_SHA256:-57ccc32bbbd005cab75bcc52444052535af691789dba2b9016d5c50640d68b3d}"
LUA_URL="${RIDUX_HYPRLAND_LUA_URL:-https://www.lua.org/ftp/lua-${LUA_VERSION}.tar.gz}"
LUA_SRC="$SRC_DIR/lua-$LUA_VERSION"

if [[ ! -d "$HYPR_SRC/.git" ]]; then
  echo "[hyprland-build] Hyprland sources are missing." >&2
  echo "[hyprland-build] Run: make hyprland-source" >&2
  exit 2
fi

if ! command -v cmake >/dev/null 2>&1; then
  echo "[hyprland-build] cmake is required." >&2
  exit 3
fi
if ! command -v make >/dev/null 2>&1 && ! command -v ninja >/dev/null 2>&1; then
  echo "[hyprland-build] make or ninja is required." >&2
  exit 4
fi

export PATH="$PWD/$INSTALL_DIR/bin:${PATH:-}"
export PKG_CONFIG_PATH="$PWD/$INSTALL_DIR/lib/pkgconfig:$PWD/$INSTALL_DIR/lib64/pkgconfig:$PWD/$INSTALL_DIR/lib/$HOST_TRIPLET/pkgconfig:$PWD/$INSTALL_DIR/share/pkgconfig:$PWD/$SYSROOT_DIR/usr/lib/$HOST_TRIPLET/pkgconfig:$PWD/$SYSROOT_DIR/usr/share/pkgconfig:${PKG_CONFIG_PATH:-}"
export CMAKE_PREFIX_PATH="$PWD/$INSTALL_DIR:${CMAKE_PREFIX_PATH:-}"
export LD_LIBRARY_PATH="$PWD/$INSTALL_DIR/lib:$PWD/$INSTALL_DIR/lib64:$PWD/$INSTALL_DIR/lib/$HOST_TRIPLET:$PWD/$SYSROOT_DIR/usr/lib/$HOST_TRIPLET:${LD_LIBRARY_PATH:-}"

write_pc_absolute_var() {
  local pc="$1"
  local var="$2"
  local value="$3"
  [[ -f "$pc" ]] || return 0
  sed -i -E "s|^${var}=.*|${var}=${value}|" "$pc"
}

write_tomlplusplus_pc() {
  local pc_dir="$PWD/$INSTALL_DIR/lib/pkgconfig"
  local version="3.4.0"

  mkdir -p "$pc_dir"
  if [[ -f "$TOMLPLUSPLUS_SRC/VERSION" ]]; then
    version="$(tr -d '[:space:]' < "$TOMLPLUSPLUS_SRC/VERSION")"
  elif grep -R "project(.*tomlplusplus" -n "$TOMLPLUSPLUS_SRC/CMakeLists.txt" >/dev/null 2>&1; then
    version="$(grep -E 'VERSION[[:space:]]+[0-9]' "$TOMLPLUSPLUS_SRC/CMakeLists.txt" | head -n 1 | sed -E 's/.*VERSION[[:space:]]+([0-9.]+).*/\1/')"
  fi

  cat > "$pc_dir/tomlplusplus.pc" <<EOF
prefix=$PWD/$INSTALL_DIR
includedir=\${prefix}/include

Name: tomlplusplus
Description: Header-only TOML parser and serializer for C++17
Version: ${version:-3.4.0}
Cflags: -I\${includedir}
EOF
}

apt_download_package() {
  local pkg="$1"

  mkdir -p "$APT_CACHE_DIR"
  if compgen -G "$APT_CACHE_DIR/${pkg}_*.deb" >/dev/null; then
    return 0
  fi
  if ! command -v apt-get >/dev/null 2>&1 || ! command -v dpkg-deb >/dev/null 2>&1; then
    echo "[hyprland-build] $pkg is required, and apt-get/dpkg-deb are not available." >&2
    exit 6
  fi

  echo "[hyprland-build] fetching local build package: $pkg"
  (cd "$APT_CACHE_DIR" && apt-get download "$pkg")
}

download_file() {
  local url="$1"
  local out="$2"

  mkdir -p "$(dirname "$out")"
  if [[ -f "$out" ]]; then
    return 0
  fi

  echo "[hyprland-build] fetching source: $url"
  if command -v curl >/dev/null 2>&1; then
    curl -L --fail -o "$out" "$url"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "$out" "$url"
  else
    echo "[hyprland-build] curl or wget is required to fetch $url." >&2
    exit 11
  fi
}

verify_sha256() {
  local file="$1"
  local expected="$2"
  local actual

  if ! command -v sha256sum >/dev/null 2>&1; then
    echo "[hyprland-build] sha256sum is required to verify $file." >&2
    exit 12
  fi

  actual="$(sha256sum "$file" | awk '{print $1}')"
  if [[ "$actual" != "$expected" ]]; then
    echo "[hyprland-build] checksum mismatch for $file" >&2
    echo "[hyprland-build] expected: $expected" >&2
    echo "[hyprland-build] actual:   $actual" >&2
    exit 13
  fi
}

extract_apt_package() {
  local pkg="$1"
  local deb

  apt_download_package "$pkg"
  for deb in "$APT_CACHE_DIR"/"${pkg}"_*.deb; do
    [[ -f "$deb" ]] || continue
    dpkg-deb -x "$deb" "$SYSROOT_DIR"
  done
}

extract_first_available_package() {
  local pkg candidate

  for pkg in "$@"; do
    candidate="$(apt-cache policy "$pkg" 2>/dev/null | awk '/Candidate:/ {candidate=$2} END {print candidate}')"
    if [[ -n "$candidate" && "$candidate" != "(none)" ]]; then
      extract_apt_package "$pkg"
      return 0
    fi
  done

  echo "[hyprland-build] none of these packages has an apt candidate: $*" >&2
  exit 9
}

prepare_go_toolchain() {
  local goroot="$PWD/$SYSROOT_DIR/usr/lib/go-1.24"
  local gobin="$goroot/bin/go"

  if command -v go >/dev/null 2>&1; then
    export GOTOOLCHAIN="${GOTOOLCHAIN:-local}"
    return 0
  fi

  extract_apt_package golang-1.24-src
  extract_apt_package golang-1.24-go
  extract_apt_package golang-go || true

  if [[ ! -x "$gobin" ]]; then
    echo "[hyprland-build] local Go toolchain was not extracted correctly." >&2
    exit 15
  fi

  export GOROOT="$goroot"
  export GOTOOLCHAIN=local
  export PATH="$goroot/bin:$PWD/$SYSROOT_DIR/usr/bin:$PATH"
}

prepare_nwg_dock_build_deps() {
  if ! pkg-config --exists gobject-introspection-1.0; then
    extract_apt_package libgirepository-1.0-1
    extract_apt_package libgirepository-1.0-dev
    extract_apt_package libgirepository1.0-dev
    extract_apt_package gir1.2-girepository-2.0-dev
    extract_apt_package gobject-introspection
  fi

  if ! pkg-config --exists gtk+-3.0 gtk-layer-shell-0 gobject-introspection-1.0; then
    echo "[hyprland-build] nwg-dock-hyprland missing GTK/layer-shell/introspection pkg-config metadata:" >&2
    PKG_CONFIG_PATH="$PKG_CONFIG_PATH" pkg-config --print-errors --exists gtk+-3.0 gtk-layer-shell-0 gobject-introspection-1.0 || true
    exit 17
  fi
}

copy_sysroot_runtime_libs() {
  local src_lib_dir="$PWD/$SYSROOT_DIR/usr/lib/$HOST_TRIPLET"
  local dst_lib_dir="$PWD/$INSTALL_DIR/lib/$HOST_TRIPLET"
  local pattern

  mkdir -p "$dst_lib_dir"
  for pattern in libzip.so* librsvg-2.so* libdav1d.so* libmagic.so*; do
    if compgen -G "$src_lib_dir/$pattern" >/dev/null; then
      cp -a "$src_lib_dir"/$pattern "$dst_lib_dir/"
    fi
  done
}

write_lua55_pc() {
  local pc_dir="$PWD/$INSTALL_DIR/lib/pkgconfig"
  local pc

  mkdir -p "$pc_dir"
  for pc in lua55 lua5.5 lua-5.5; do
    cat > "$pc_dir/$pc.pc" <<EOF
prefix=$PWD/$INSTALL_DIR
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include/lua5.5

Name: Lua
Description: Lua language engine
Version: $LUA_VERSION
Libs: -L\${libdir} -llua5.5 -lm -ldl
Cflags: -I\${includedir}
EOF
  done
}

prepare_lua55_source_deps() {
  local tarball="$APT_CACHE_DIR/lua-$LUA_VERSION.tar.gz"
  local lua_lib_dir="$PWD/$INSTALL_DIR/lib"
  local lua_inc_dir="$PWD/$INSTALL_DIR/include/lua5.5"
  local pc_dir="$PWD/$INSTALL_DIR/lib/pkgconfig"
  local objects

  if PKG_CONFIG_PATH="$PKG_CONFIG_PATH" pkg-config --exists lua55 || \
     PKG_CONFIG_PATH="$PKG_CONFIG_PATH" pkg-config --exists lua5.5 || \
     PKG_CONFIG_PATH="$PKG_CONFIG_PATH" pkg-config --exists lua-5.5; then
    return 0
  fi

  download_file "$LUA_URL" "$tarball"
  verify_sha256 "$tarball" "$LUA_SHA256"

  mkdir -p "$SRC_DIR"
  if [[ ! -d "$LUA_SRC/src" ]]; then
    tar -xzf "$tarball" -C "$SRC_DIR"
  fi

  echo "[hyprland-build] dependency: lua-$LUA_VERSION"
  make -C "$LUA_SRC/src" clean >/dev/null 2>&1 || true
  make -C "$LUA_SRC/src" all MYCFLAGS="-fPIC -DLUA_COMPAT_5_4" MYLDFLAGS="" SYSLIBS="-lm -ldl"

  mkdir -p "$lua_lib_dir" "$lua_inc_dir" "$pc_dir" "$PWD/$INSTALL_DIR/bin"
  objects="$(cd "$LUA_SRC/src" && ar t liblua.a | tr '\n' ' ')"
  (cd "$LUA_SRC/src" && cc -shared -Wl,-soname,liblua5.5.so.0 -o "$lua_lib_dir/liblua5.5.so.0.0.0" $objects -lm -ldl)
  ln -sfn liblua5.5.so.0.0.0 "$lua_lib_dir/liblua5.5.so.0"
  ln -sfn liblua5.5.so.0 "$lua_lib_dir/liblua5.5.so"
  cp -f "$LUA_SRC/src/lua.h" "$LUA_SRC/src/lauxlib.h" "$LUA_SRC/src/lualib.h" "$LUA_SRC/src/luaconf.h" "$LUA_SRC/src/lua.hpp" "$lua_inc_dir/"
  cp -f "$LUA_SRC/src/lua" "$PWD/$INSTALL_DIR/bin/lua5.5"
  cp -f "$LUA_SRC/src/luac" "$PWD/$INSTALL_DIR/bin/luac5.5"
  chmod 0755 "$PWD/$INSTALL_DIR/bin/lua5.5" "$PWD/$INSTALL_DIR/bin/luac5.5" 2>/dev/null || true
  write_lua55_pc

  PKG_CONFIG_PATH="$PKG_CONFIG_PATH" pkg-config --exists lua5.5 || {
    echo "[hyprland-build] Lua 5.5 pkg-config metadata was not generated correctly." >&2
    exit 14
  }
}

prepare_hyprcursor_system_deps() {
  local pc_dir="$PWD/$SYSROOT_DIR/usr/lib/$HOST_TRIPLET/pkgconfig"
  local lib_dir="$PWD/$INSTALL_DIR/lib/$HOST_TRIPLET"
  local include_dir="$PWD/$SYSROOT_DIR/usr/include"
  local libzip_pc="$pc_dir/libzip.pc"
  local librsvg_pc="$pc_dir/librsvg-2.0.pc"
  local dav1d_pc="$pc_dir/dav1d.pc"

  write_tomlplusplus_pc

  if ! PKG_CONFIG_PATH="$PKG_CONFIG_PATH" pkg-config --exists libzip; then
    extract_apt_package libzip5
    extract_apt_package libzip-dev
  fi
  if ! PKG_CONFIG_PATH="$PKG_CONFIG_PATH" pkg-config --exists librsvg-2.0; then
    extract_apt_package libdav1d7
    extract_apt_package libdav1d-dev
    extract_apt_package librsvg2-2
    extract_apt_package librsvg2-dev
  fi

  copy_sysroot_runtime_libs

  if [[ -f "$dav1d_pc" ]]; then
    write_pc_absolute_var "$dav1d_pc" prefix "$PWD/$SYSROOT_DIR/usr"
    write_pc_absolute_var "$dav1d_pc" libdir "$lib_dir"
    write_pc_absolute_var "$dav1d_pc" includedir "$include_dir"
  fi

  if [[ -f "$libzip_pc" ]]; then
    write_pc_absolute_var "$libzip_pc" prefix "$PWD/$SYSROOT_DIR/usr"
    write_pc_absolute_var "$libzip_pc" exec_prefix "\${prefix}"
    write_pc_absolute_var "$libzip_pc" libdir "$lib_dir"
    write_pc_absolute_var "$libzip_pc" includedir "$include_dir"
    sed -i -E "s|[[:space:]]+-I[^[:space:]]*/libzip/include||g" "$libzip_pc"
  fi

  if [[ -f "$librsvg_pc" ]]; then
    write_pc_absolute_var "$librsvg_pc" prefix "$PWD/$SYSROOT_DIR/usr"
    write_pc_absolute_var "$librsvg_pc" exec_prefix "\${prefix}"
    write_pc_absolute_var "$librsvg_pc" libdir "$lib_dir"
    write_pc_absolute_var "$librsvg_pc" includedir "$include_dir"
  fi

  pkg-config --exists libzip librsvg-2.0 tomlplusplus || {
    echo "[hyprland-build] missing hyprcursor dependencies after local sysroot preparation:" >&2
    PKG_CONFIG_PATH="$PKG_CONFIG_PATH" pkg-config --print-errors --exists libzip librsvg-2.0 tomlplusplus || true
    exit 7
  }
}

prepare_hyprgraphics_system_deps() {
  local pc_dir="$PWD/$SYSROOT_DIR/usr/lib/$HOST_TRIPLET/pkgconfig"
  local lib_dir="$PWD/$INSTALL_DIR/lib/$HOST_TRIPLET"
  local include_dir="$PWD/$SYSROOT_DIR/usr/include"
  local magic_pc="$pc_dir/libmagic.pc"

  if ! PKG_CONFIG_PATH="$PKG_CONFIG_PATH" pkg-config --exists libmagic; then
    extract_first_available_package libmagic1 libmagic1t64
    extract_apt_package libmagic-dev
  fi

  copy_sysroot_runtime_libs

  if [[ -f "$magic_pc" ]]; then
    write_pc_absolute_var "$magic_pc" prefix "$PWD/$SYSROOT_DIR/usr"
    write_pc_absolute_var "$magic_pc" exec_prefix "\${prefix}"
    write_pc_absolute_var "$magic_pc" libdir "$lib_dir"
    write_pc_absolute_var "$magic_pc" includedir "$include_dir"
  fi

  pkg-config --exists libmagic || {
    echo "[hyprland-build] missing hyprgraphics dependency after local sysroot preparation:" >&2
    PKG_CONFIG_PATH="$PKG_CONFIG_PATH" pkg-config --print-errors --exists libmagic || true
    exit 8
  }
}

cmake_generator_args() {
  if command -v ninja >/dev/null 2>&1; then
    printf '%s\n' "-G" "Ninja"
  fi
}

build_cmake_project() {
  local name="$1"
  local src="$2"
  shift 2
  local build="$BUILD_DIR/$name"
  local generator_args=()

  [[ -f "$src/CMakeLists.txt" ]] || {
    echo "[hyprland-build] skip $name: no CMakeLists.txt"
    return 0
  }

  mkdir -p "$build" "$INSTALL_DIR"
  while IFS= read -r arg; do
    [[ -n "$arg" ]] && generator_args+=("$arg")
  done < <(cmake_generator_args)

  echo "[hyprland-build] dependency: $name"
  cmake -S "$src" -B "$build" "${generator_args[@]}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PWD/$INSTALL_DIR" \
    -DCMAKE_PREFIX_PATH="$PWD/$INSTALL_DIR" \
    -DCMAKE_SKIP_INSTALL_RPATH=OFF \
    -DCMAKE_INSTALL_RPATH="/opt/hyprland/lib;/opt/hyprland/lib64;/opt/hyprland/lib/x86_64-linux-gnu;/lib64;/usr/lib/x86_64-linux-gnu" \
    "$@"
  cmake --build "$build" -j "$JOBS"
  cmake --install "$build"
}

build_meson_project() {
  local name="$1"
  local src="$2"
  shift 2
  local build="$BUILD_DIR/$name"

  [[ -f "$src/meson.build" ]] || {
    echo "[hyprland-build] skip $name: no meson.build"
    return 0
  }
  if ! command -v meson >/dev/null 2>&1 || ! command -v ninja >/dev/null 2>&1; then
    echo "[hyprland-build] meson and ninja are required for $name." >&2
    exit 10
  fi

  mkdir -p "$build" "$INSTALL_DIR"
  echo "[hyprland-build] dependency: $name"
  if [[ -f "$build/build.ninja" ]]; then
    meson setup --reconfigure "$build" "$src" \
      --prefix="$PWD/$INSTALL_DIR" \
      --buildtype=release \
      "$@"
  else
    meson setup "$build" "$src" \
      --prefix="$PWD/$INSTALL_DIR" \
      --buildtype=release \
      "$@"
  fi
  meson compile -C "$build" -j "$JOBS"
  meson install -C "$build"
}

patch_hyprland_default_config_embed() {
  local default_header="$HYPR_SRC/src/config/lua/DefaultConfig.hpp"
  local example_config="$HYPR_SRC/example/hyprland.lua"

  [[ -f "$default_header" && -f "$example_config" ]] || return 0
  if ! grep -q '#embed' "$default_header"; then
    return 0
  fi

  echo "[hyprland-build] patch: replace C++26 #embed default config with generated raw string"
  {
    cat <<'EOF'
#pragma once

#include <string_view>

inline constexpr std::string_view AUTOGENERATED_PREFIX_LUA = R"#(
-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
-- AUTOGENERATED HYPRLAND CONFIG.                        --
-- EDIT THIS CONFIG ACCORDING TO THE WIKI INSTRUCTIONS.  --
-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

hl.config({ autogenerated = true }) -- remove this line to remove the warning

)#";

inline constexpr std::string_view EXAMPLE_CONFIG_LUA = R"RIDUX_HYPR_LUA(
EOF
    cat "$example_config"
    cat <<'EOF'
)RIDUX_HYPR_LUA";
EOF
  } > "$default_header"
}

build_hyprland_deps() {
  build_cmake_project glslang "$GLSLANG_SRC" \
    -DENABLE_GLSLANG_BINARIES=OFF \
    -DENABLE_SPVREMAPPER=OFF \
    -DENABLE_OPT=OFF \
    -DENABLE_CTEST=OFF \
    -DBUILD_TESTING=OFF
  build_cmake_project pugixml "$PUGIXML_SRC" -DBUILD_TESTING=OFF
  build_cmake_project tomlplusplus "$TOMLPLUSPLUS_SRC" \
    -DTOMLPLUSPLUS_BUILD_TESTS=OFF \
    -DTOMLPLUSPLUS_BUILD_EXAMPLES=OFF
  build_cmake_project abseil-cpp "$ABSEIL_SRC" \
    -DABSL_ENABLE_INSTALL=ON \
    -DABSL_BUILD_TESTING=OFF \
    -DBUILD_TESTING=OFF
  build_cmake_project re2 "$RE2_SRC" \
    -DRE2_BUILD_TESTING=OFF \
    -DBUILD_TESTING=OFF
  build_cmake_project muparser "$MUPARSER_SRC" \
    -DENABLE_SAMPLES=OFF \
    -DENABLE_OPENMP=OFF \
    -DBUILD_SHARED_LIBS=ON
  build_meson_project wayland-protocols "$WAYLAND_PROTOCOLS_SRC" \
    -Dtests=false
  build_meson_project libxkbcommon "$XKBCOMMON_SRC" \
    -Denable-docs=false \
    -Denable-x11=false \
    -Denable-tools=false \
    -Denable-wayland=false \
    -Denable-bash-completion=false \
    -Denable-zsh-completion=false
  build_meson_project libinput "$LIBINPUT_SRC" \
    -Ddocumentation=false \
    -Dtests=false \
    -Ddebug-gui=false \
    -Dlibwacom=true \
    -Dmtdev=true \
    -Dzshcompletiondir=no
  build_cmake_project hyprwayland-scanner "$HYPRWAYLAND_SCANNER_SRC"
  build_cmake_project hyprutils "$HYPRUTILS_SRC"
  build_cmake_project hyprlang "$HYPRLANG_SRC"
  prepare_hyprcursor_system_deps
  build_cmake_project hyprcursor "$HYPRCURSOR_SRC"
  prepare_hyprgraphics_system_deps
  build_cmake_project hyprgraphics "$HYPRGRAPHICS_SRC"
  build_cmake_project hyprwire "$HYPRWIRE_SRC"
  prepare_lua55_source_deps
  build_cmake_project aquamarine "$AQUAMARINE_SRC" -DNO_SYSTEMD=true
}

build_hyprland() {
  local build="$BUILD_DIR/Hyprland"
  local generator_args=()

  mkdir -p "$build" "$INSTALL_DIR"
  patch_hyprland_default_config_embed
  while IFS= read -r arg; do
    [[ -n "$arg" ]] && generator_args+=("$arg")
  done < <(cmake_generator_args)

  echo "[hyprland-build] module: Hyprland"
  cmake -S "$HYPR_SRC" -B "$build" "${generator_args[@]}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PWD/$INSTALL_DIR" \
    -DCMAKE_PREFIX_PATH="$PWD/$INSTALL_DIR" \
    -DCMAKE_SKIP_INSTALL_RPATH=OFF \
    -DCMAKE_INSTALL_RPATH="/opt/hyprland/lib;/opt/hyprland/lib64;/opt/hyprland/lib/x86_64-linux-gnu;/lib64;/usr/lib/x86_64-linux-gnu" \
    -DNO_XWAYLAND="${RIDUX_HYPRLAND_NO_XWAYLAND:-true}" \
    -DNO_SYSTEMD="${RIDUX_HYPRLAND_NO_SYSTEMD:-true}"
  cmake --build "$build" -j "$JOBS"
  cmake --install "$build"
}

build_nwg_dock() {
  local outbin=""
  local go_cache="${RIDUX_HYPRLAND_GO_CACHE:-${TMPDIR:-/tmp}/ridux-hyprland-go-cache}"
  local go_path="${RIDUX_HYPRLAND_GO_PATH:-${TMPDIR:-/tmp}/ridux-hyprland-go-path}"
  local go_src="${RIDUX_HYPRLAND_NWG_BUILD_SRC:-${TMPDIR:-/tmp}/ridux-nwg-dock-hyprland-src}"
  local nwg_cgo_ldflags="-L$PWD/$SYSROOT_DIR/usr/lib/$HOST_TRIPLET -L/usr/lib/$HOST_TRIPLET ${CGO_LDFLAGS:-}"
  [[ -d "$NWG_SRC/.git" ]] || {
    echo "[hyprland-build] skip nwg-dock-hyprland: sources missing"
    return 0
  }

  if ! command -v go >/dev/null 2>&1; then
    prepare_go_toolchain
  fi
  if ! command -v go >/dev/null 2>&1; then
    echo "[hyprland-build] nwg-dock-hyprland requires Go." >&2
    exit 16
  fi
  prepare_nwg_dock_build_deps

  echo "[hyprland-build] module: nwg-dock-hyprland"
  rm -rf "$go_src"
  mkdir -p "$go_cache" "$go_path" "$go_src" "$NWG_SRC/bin"
  cp -a "$NWG_SRC"/. "$go_src"/
  (
    cd "$go_src"
    GOCACHE="$go_cache" \
    GOPATH="$go_path" \
    CGO_LDFLAGS="$nwg_cgo_ldflags" \
    GOTOOLCHAIN="${GOTOOLCHAIN:-local}" \
      go mod download
    GOCACHE="$go_cache" \
    GOPATH="$go_path" \
    CGO_LDFLAGS="$nwg_cgo_ldflags" \
    GOFLAGS="${GOFLAGS:-} -p=2" \
    GOTOOLCHAIN="${GOTOOLCHAIN:-local}" \
      go build -v -buildmode="${RIDUX_NWG_DOCK_BUILDMODE:-pie}" -o bin/nwg-dock-hyprland .
  )

  for candidate in \
    "$go_src/bin/nwg-dock-hyprland" \
    "$go_src/nwg-dock-hyprland" \
    "$go_src/build/nwg-dock-hyprland" \
    "$NWG_SRC/bin/nwg-dock-hyprland" \
    "$NWG_SRC/nwg-dock-hyprland" \
    "$NWG_SRC/build/nwg-dock-hyprland"
  do
    if [[ -x "$candidate" ]]; then
      outbin="$candidate"
      break
    fi
  done

  if [[ -z "$outbin" ]]; then
    echo "[hyprland-build] nwg-dock-hyprland built, but no binary was found" >&2
    return 0
  fi

  mkdir -p "$INSTALL_DIR/bin" "$INSTALL_DIR/share/nwg-dock-hyprland"
  cp -Lf "$outbin" "$INSTALL_DIR/bin/nwg-dock-hyprland"
  chmod 0755 "$INSTALL_DIR/bin/nwg-dock-hyprland"
  [[ ! -d "$NWG_SRC/images" ]] || cp -a "$NWG_SRC/images" "$INSTALL_DIR/share/nwg-dock-hyprland/"
  [[ ! -d "$NWG_SRC/config" ]] || cp -a "$NWG_SRC/config" "$INSTALL_DIR/share/nwg-dock-hyprland/"
}

mkdir -p "$BUILD_DIR" "$INSTALL_DIR"
case "$TARGET" in
  all)
    build_hyprland_deps
    build_hyprland
    build_nwg_dock
    ;;
  deps)
    build_hyprland_deps
    ;;
  Hyprland|hyprland)
    build_hyprland_deps
    build_hyprland
    ;;
  hyprcursor)
    prepare_hyprcursor_system_deps
    build_cmake_project hyprcursor "$HYPRCURSOR_SRC"
    ;;
  hyprgraphics|graphics)
    prepare_hyprcursor_system_deps
    prepare_hyprgraphics_system_deps
    build_cmake_project hyprgraphics "$HYPRGRAPHICS_SRC"
    ;;
  wayland-protocols)
    build_meson_project wayland-protocols "$WAYLAND_PROTOCOLS_SRC" -Dtests=false
    ;;
  libxkbcommon|xkbcommon)
    build_meson_project libxkbcommon "$XKBCOMMON_SRC" \
      -Denable-docs=false \
      -Denable-x11=false \
      -Denable-tools=false \
      -Denable-wayland=false \
      -Denable-bash-completion=false \
      -Denable-zsh-completion=false
    ;;
  libinput)
    build_meson_project libinput "$LIBINPUT_SRC" \
      -Ddocumentation=false \
      -Dtests=false \
      -Ddebug-gui=false \
      -Dlibwacom=true \
      -Dmtdev=true \
      -Dzshcompletiondir=no
    ;;
  aquamarine)
    build_cmake_project aquamarine "$AQUAMARINE_SRC" -DNO_SYSTEMD=true
    ;;
  hyprwire)
    build_cmake_project hyprwire "$HYPRWIRE_SRC"
    ;;
  lua55|lua5.5|lua)
    prepare_lua55_source_deps
    ;;
  nwg-dock-hyprland|nwg-dock|dock)
    build_nwg_dock
    ;;
  *)
    echo "[hyprland-build] unknown target: $TARGET" >&2
    exit 5
    ;;
esac

echo "[hyprland-build] install: $INSTALL_DIR"
