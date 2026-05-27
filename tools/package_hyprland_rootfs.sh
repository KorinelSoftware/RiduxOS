#!/usr/bin/env bash
set -euo pipefail

ROOTFS_DIR="${1:-rootfs}"
INSTALL_DIR="${2:-${RIDUX_HYPRLAND_INSTALL:-third_party/hyprland/install}}"
FETCH_DEBIAN="${RIDUX_HYPRLAND_FETCH_DEBIAN:-auto}"
DEB_CACHE="${RIDUX_HYPRLAND_DEB_CACHE:-third_party/hyprland/deb-cache}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd -P)"

if [[ ! -d "$ROOTFS_DIR" ]]; then
  echo "[hyprland-rootfs] rootfs dir not found: $ROOTFS_DIR" >&2
  exit 1
fi

ROOT_ABS="$(cd "$ROOTFS_DIR" && pwd -P)"
DEST="$ROOT_ABS/opt/hyprland"
MANIFEST="$DEST/SOURCE-MANIFEST.txt"
HYPRLAND_BIN=""

case "$DEST" in
  "$ROOT_ABS"/opt/hyprland) ;;
  *)
    echo "[hyprland-rootfs] refusing unsafe destination: $DEST" >&2
    exit 2
    ;;
esac

if ! command -v readelf >/dev/null 2>&1; then
  echo "[hyprland-rootfs] readelf is required (binutils)." >&2
  exit 3
fi
if ! command -v ldd >/dev/null 2>&1; then
  echo "[hyprland-rootfs] ldd is required." >&2
  exit 4
fi

is_elf() {
  [[ -f "$1" ]] && readelf -h "$1" >/dev/null 2>&1
}

copy_abs_into_rootfs() {
  local src="$1"
  local dst="$ROOT_ABS$src"
  local soname soname_dst
  [[ -e "$src" ]] || return 0
  mkdir -p "$(dirname "$dst")"
  rm -f "$dst"
  cp -L "$src" "$dst"
  soname="$({ readelf -d "$src" 2>/dev/null || true; } | awk -F'[][]' '/SONAME/ {print $2; exit}')"
  if [[ -n "$soname" && "$soname" != "$(basename "$dst")" ]]; then
    soname_dst="$(dirname "$dst")/$soname"
    rm -f "$soname_dst"
    cp -L "$src" "$soname_dst"
  fi
}

declare -A SEEN
QUEUE=()
COPIED_DEPS=()
declare -A OPT_BUNDLE_SEEN
OPT_BUNDLE_QUEUE=()
OPT_BUNDLE_DEPS=()

enqueue_dep() {
  local dep="${1:-}"
  [[ -n "$dep" && -e "$dep" ]] || return 0
  dep="$(readlink -f "$dep")"
  case "$dep" in
    "$INSTALL_ABS"/*) return 0 ;;
  esac
  [[ -z "${SEEN[$dep]:-}" ]] || return 0
  QUEUE+=("$dep")
}

scan_elf_deps() {
  local elf="$1"
  local dep
  while IFS= read -r dep; do
    [[ -n "$dep" ]] || continue
    enqueue_dep "$dep"
  done < <(
    LD_LIBRARY_PATH="$INSTALL_ABS/lib:$INSTALL_ABS/lib64:$INSTALL_ABS/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}" \
      ldd "$elf" 2>/dev/null | awk '
        /=> \// {print $3}
        /^[[:space:]]*\/lib/ {print $1}
        /^[[:space:]]*\/usr\/lib/ {print $1}
      '
  )
}

copy_system_cmd() {
  local name="$1"
  local dst="$2"
  local src
  src="$(command -v "$name" 2>/dev/null || true)"
  [[ -n "$src" && -x "$src" ]] || return 0
  mkdir -p "$ROOT_ABS$(dirname "$dst")"
  cp -Lf "$src" "$ROOT_ABS$dst"
  chmod 0755 "$ROOT_ABS$dst" 2>/dev/null || true
  is_elf "$src" && scan_elf_deps "$src"
}

copy_host_soname_into_rootfs() {
  local soname="$1"
  local src
  src="$(ldconfig -p 2>/dev/null | awk -v n="$soname" '$1 == n {print $NF; exit}')"
  [[ -n "$src" && -e "$src" ]] || return 0
  copy_abs_into_rootfs "$src"
  is_elf "$src" && scan_elf_deps "$src"
}

stage_gsettings_schemas() {
  local schema_dir="$ROOT_ABS/usr/share/glib-2.0/schemas"
  local src

  mkdir -p "$schema_dir"

  for src in /usr/share/glib-2.0/schemas/*.xml /usr/share/glib-2.0/schemas/*.override; do
    [[ -f "$src" ]] || continue
    cp -Lf "$src" "$schema_dir/$(basename "$src")"
  done

  if command -v glib-compile-schemas >/dev/null 2>&1; then
    glib-compile-schemas "$schema_dir"
  elif [[ -f /usr/share/glib-2.0/schemas/gschemas.compiled ]]; then
    cp -Lf /usr/share/glib-2.0/schemas/gschemas.compiled "$schema_dir/gschemas.compiled"
  fi

  if [[ -f "$schema_dir/gschemas.compiled" ]] && ! grep -a -q 'org.gnome.desktop.lockdown' "$schema_dir/gschemas.compiled"; then
    echo "[hyprland-rootfs] warn: compiled schemas do not include org.gnome.desktop.lockdown" >&2
  fi
}

resolve_rootfs_soname() {
  local soname="$1"
  local dir candidate

  [[ -n "$soname" ]] || return 1
  for dir in \
    "$DEST/lib" \
    "$DEST/lib64" \
    "$DEST/lib/x86_64-linux-gnu" \
    "$DEST/usr/lib" \
    "$DEST/usr/lib/x86_64-linux-gnu" \
    "$ROOT_ABS/usr/lib/x86_64-linux-gnu" \
    "$ROOT_ABS/usr/lib/x86_64-linux-gnu/pulseaudio" \
    "$ROOT_ABS/lib/x86_64-linux-gnu" \
    "$ROOT_ABS/lib/x86_64-linux-gnu/pulseaudio" \
    "$ROOT_ABS/lib64" \
    "$ROOT_ABS/usr/lib" \
    "$ROOT_ABS/lib"
  do
    candidate="$dir/$soname"
    if [[ -e "$candidate" ]]; then
      readlink -f "$candidate"
      return 0
    fi
  done
  return 1
}

copy_runtime_lib_into_opt() {
  local src="$1"
  local base dst soname soname_dst

  [[ -f "$src" ]] || return 0
  case "$src" in
    "$DEST"/lib/*|"$DEST"/lib64/*|"$DEST"/lib/x86_64-linux-gnu/*) return 0 ;;
  esac

  base="$(basename "$src")"
  dst="$DEST/lib/$base"
  mkdir -p "$DEST/lib"
  rm -f "$dst"
  cp -Lf "$src" "$dst"
  chmod 0755 "$dst" 2>/dev/null || true

  soname="$({ readelf -d "$src" 2>/dev/null || true; } | awk -F'[][]' '/SONAME/ {print $2; exit}')"
  if [[ -n "$soname" && "$soname" != "$base" ]]; then
    soname_dst="$DEST/lib/$soname"
    rm -f "$soname_dst"
    cp -Lf "$src" "$soname_dst"
    chmod 0755 "$soname_dst" 2>/dev/null || true
  fi
}

enqueue_opt_bundle_elf() {
  local elf="$1"
  [[ -f "$elf" ]] || return 0
  is_elf "$elf" || return 0
  elf="$(readlink -f "$elf")"
  [[ -z "${OPT_BUNDLE_SEEN[$elf]:-}" ]] || return 0
  OPT_BUNDLE_QUEUE+=("$elf")
}

bundle_runtime_deps_into_opt() {
  local elf dep soname src

  for elf in "$@"; do
    enqueue_opt_bundle_elf "$elf"
  done

  while [[ "${#OPT_BUNDLE_QUEUE[@]}" -gt 0 ]]; do
    elf="${OPT_BUNDLE_QUEUE[0]}"
    OPT_BUNDLE_QUEUE=("${OPT_BUNDLE_QUEUE[@]:1}")
    [[ -z "${OPT_BUNDLE_SEEN[$elf]:-}" ]] || continue
    OPT_BUNDLE_SEEN["$elf"]=1

    while IFS= read -r dep; do
      [[ -n "$dep" ]] || continue
      soname="$(printf '%s\n' "$dep" | awk -F'[][]' '{print $2}')"
      [[ -n "$soname" ]] || continue
      case "$soname" in
        linux-vdso.so.*|ld-linux*.so*|libc.so.*|libm.so.*|libpthread.so.*|librt.so.*|libdl.so.*|libutil.so.*|libresolv.so.*)
          continue
          ;;
      esac
      src="$(resolve_rootfs_soname "$soname" || true)"
      if [[ -z "$src" || ! -f "$src" ]]; then
        echo "[hyprland-rootfs] warn: unresolved runtime library for opt bundle: $soname" >&2
        continue
      fi
      copy_runtime_lib_into_opt "$src"
      OPT_BUNDLE_DEPS+=("$src")
      enqueue_opt_bundle_elf "$src"
    done < <(readelf -d "$elf" 2>/dev/null | awk '/NEEDED/ {print}')
  done
}

install_opt_runtime_libs_into_system_search_path() {
  local src dst_dir dst

  mkdir -p \
    "$ROOT_ABS/usr/lib/x86_64-linux-gnu" \
    "$ROOT_ABS/lib/x86_64-linux-gnu" \
    "$ROOT_ABS/lib64"
  while IFS= read -r -d '' src; do
    [[ -f "$src" ]] || continue
    for dst_dir in \
      "$ROOT_ABS/usr/lib/x86_64-linux-gnu" \
      "$ROOT_ABS/lib/x86_64-linux-gnu" \
      "$ROOT_ABS/lib64"
    do
      dst="$dst_dir/$(basename "$src")"
      rm -f "$dst"
      cp -Lf "$src" "$dst"
      chmod 0755 "$dst" 2>/dev/null || true
    done
  done < <(
    find \
      "$DEST/lib" \
      "$DEST/lib64" \
      "$DEST/lib/x86_64-linux-gnu" \
      "$DEST/usr/lib" \
      "$DEST/usr/lib/x86_64-linux-gnu" \
      -maxdepth 1 -type f \( -name '*.so' -o -name '*.so.*' \) -print0 2>/dev/null
  )
}

download_debian_hyprland_stack() {
  [[ "$FETCH_DEBIAN" == "1" || "$FETCH_DEBIAN" == "true" || "$FETCH_DEBIAN" == "auto" ]] || return 0
  command -v apt-get >/dev/null 2>&1 || return 0
  command -v dpkg-deb >/dev/null 2>&1 || return 0

  mkdir -p "$DEB_CACHE"
  (
    cd "$DEB_CACHE"
    for pkg in \
      hyprland xdg-desktop-portal xdg-desktop-portal-hyprland xdg-desktop-portal-gtk \
      waybar swaybg hypridle hyprlock hyprland-guiutils hyprland-qtutils qml6-module-org-hyprland-style \
      libaquamarine9 libhyprcursor0 libhyprgraphics4 libhyprlang2 libhyprutils10 libhyprwire3 \
      libdrm2 libegl1 libegl-mesa0 libgbm1 libgles2 libglvnd0 libgl1-mesa-dri \
      libinput10 libmuparser2v5 libpango-1.0-0 libpangocairo-1.0-0 libpixman-1-0 \
      libre2-11 libtomlplusplus3t64 libudis86-0 libuuid1 libwayland-server0 libwayland-client0 libwayland-egl1 \
      libxcb-composite0 libxcb-errors0 libxcb-icccm4 libxcb-render0 libxcb-res0 libxcb-xfixes0 libxcb1 \
      libxcursor1 libxkbcommon0 libxkbregistry0 \
      libsdbus-c++2 libpipewire-0.3-0t64 libwireplumber-0.5-0 \
      libqt6core6t64 libqt6gui6 libqt6widgets6 \
      libgtk-3-0t64 libgtk-layer-shell0 libgtkmm-3.0-1t64 libglibmm-2.4-1t64 libgdkmm-3.0-1t64 \
      libcairomm-1.0-1v5 libatkmm-1.6-1v5 libpangomm-1.4-1v5 libsigc++-2.0-0v5 \
      libfmt10 libhyprtoolkit5 libiniparser4 libjsoncpp26 libmpdclient2t64 libnl-3-200 libnl-genl-3-200 libplayerctl2 libpulse0 \
      libsndio7.0 libspdlog1.15 libudev1 libupower-glib3 libpam0g polkitd rtkit binutils pciutils libpci3 libkmod2 grep \
      dbus dbus-bin dbus-user-session \
      gsettings-desktop-schemas adwaita-icon-theme hicolor-icon-theme fonts-noto-core fonts-noto-mono \
      fontconfig shared-mime-info desktop-file-utils
    do
      if compgen -G "${pkg}_*.deb" >/dev/null; then
        continue
      fi
      apt-get download "$pkg" >/dev/null 2>&1 || true
    done
  )

  for deb in "$DEB_CACHE"/*.deb; do
    [[ -f "$deb" ]] || continue
    dpkg-deb -x "$deb" "$ROOT_ABS" >/dev/null 2>&1 || true
  done
}

repair_debian_hyprland_binary() {
  local deb="" candidate tmp

  for candidate in "$DEB_CACHE"/hyprland_*.deb; do
    [[ -f "$candidate" ]] || continue
    deb="$candidate"
    break
  done
  [[ -n "$deb" ]] || return 0

  tmp="$(mktemp -d "${TMPDIR:-/tmp}/ridux-hyprland.XXXXXX")" || return 0
  if dpkg-deb -x "$deb" "$tmp" >/dev/null 2>&1 && is_elf "$tmp/usr/bin/Hyprland"; then
    mkdir -p "$ROOT_ABS/usr/bin"
    rm -f "$ROOT_ABS/usr/bin/Hyprland" "$ROOT_ABS/usr/bin/hyprland" 2>/dev/null || true
    cp -Lf "$tmp/usr/bin/Hyprland" "$ROOT_ABS/usr/bin/Hyprland"
    chmod 0755 "$ROOT_ABS/usr/bin/Hyprland" 2>/dev/null || true
  fi
  rm -rf "$tmp"
}

repair_local_hyprland_binary() {
  [[ -n "$INSTALL_ABS" ]] || return 0
  local built="$INSTALL_ABS/../build/Hyprland/Hyprland"
  [[ -f "$built" ]] || return 0
  is_elf "$built" || return 0
  if is_elf "$INSTALL_ABS/bin/Hyprland"; then
    return 0
  fi

  mkdir -p "$INSTALL_ABS/bin"
  rm -f "$INSTALL_ABS/bin/Hyprland" "$INSTALL_ABS/bin/hyprland" 2>/dev/null || true
  cp -f "$built" "$INSTALL_ABS/bin/Hyprland"
  chmod 0755 "$INSTALL_ABS/bin/Hyprland" 2>/dev/null || true
}

if [[ -d "$INSTALL_DIR" ]]; then
  INSTALL_ABS="$(cd "$INSTALL_DIR" && pwd -P)"
  repair_local_hyprland_binary
  echo "[hyprland-rootfs] staging $INSTALL_ABS -> $DEST"
  rm -rf "$DEST"
  mkdir -p "$DEST"
  cp -aL "$INSTALL_ABS"/. "$DEST"/

  while IFS= read -r -d '' f; do
    is_elf "$f" || continue
    scan_elf_deps "$f"
  done < <(find "$INSTALL_ABS" -type f -print0)
else
  INSTALL_ABS="$ROOT_ABS/opt/hyprland"
  echo "[hyprland-rootfs] install prefix not found; checking system commands"
  rm -rf "$DEST"
  mkdir -p "$DEST/bin" "$DEST/lib" "$DEST/share"
fi

for candidate in \
  "$DEST/bin/Hyprland" \
  "$DEST/usr/bin/Hyprland" \
  "$ROOT_ABS/usr/bin/Hyprland"
do
  if is_elf "$candidate"; then
    HYPRLAND_BIN="$candidate"
    break
  fi
done

if [[ -z "$HYPRLAND_BIN" ]]; then
  download_debian_hyprland_stack
  repair_debian_hyprland_binary
  for candidate in \
    "$DEST/bin/Hyprland" \
    "$DEST/usr/bin/Hyprland" \
    "$ROOT_ABS/usr/bin/Hyprland"
  do
    if is_elf "$candidate"; then
      HYPRLAND_BIN="$candidate"
      break
    fi
  done
fi

if [[ -n "$HYPRLAND_BIN" && "$HYPRLAND_BIN" == "$ROOT_ABS/usr/bin/Hyprland" ]]; then
  mkdir -p "$DEST/bin"
  cp -Lf "$HYPRLAND_BIN" "$DEST/bin/Hyprland"
  chmod 0755 "$DEST/bin/Hyprland" 2>/dev/null || true
  HYPRLAND_BIN="$DEST/bin/Hyprland"
  scan_elf_deps "$HYPRLAND_BIN"
fi

mkdir -p "$ROOT_ABS/usr/bin"
if [[ -x "$DEST/bin/start-hyprland" ]]; then
  cp -Lf "$DEST/bin/start-hyprland" "$ROOT_ABS/usr/bin/start-hyprland"
  chmod 0755 "$ROOT_ABS/usr/bin/start-hyprland" 2>/dev/null || true
elif [[ -x "$DEST/usr/bin/start-hyprland" ]]; then
  cp -Lf "$DEST/usr/bin/start-hyprland" "$ROOT_ABS/usr/bin/start-hyprland"
  chmod 0755 "$ROOT_ABS/usr/bin/start-hyprland" 2>/dev/null || true
else
  copy_system_cmd start-hyprland /usr/bin/start-hyprland
fi

if [[ -z "$HYPRLAND_BIN" ]]; then
  sys_hyprland="$(command -v Hyprland 2>/dev/null || true)"
  if [[ -n "$sys_hyprland" && -x "$sys_hyprland" ]]; then
    mkdir -p "$DEST/bin"
    cp -Lf "$sys_hyprland" "$DEST/bin/Hyprland"
    chmod 0755 "$DEST/bin/Hyprland" 2>/dev/null || true
    HYPRLAND_BIN="$DEST/bin/Hyprland"
    scan_elf_deps "$sys_hyprland"
  fi
fi

if [[ -z "$HYPRLAND_BIN" ]]; then
  echo "[hyprland-rootfs] Hyprland binary missing; build it with: make hyprland-build" >&2
  exit 6
fi

mkdir -p "$ROOT_ABS/usr/bin"
if [[ -x "$DEST/bin/nwg-dock-hyprland" ]]; then
  cp -Lf "$DEST/bin/nwg-dock-hyprland" "$ROOT_ABS/usr/bin/nwg-dock-hyprland"
  chmod 0755 "$ROOT_ABS/usr/bin/nwg-dock-hyprland" 2>/dev/null || true
elif [[ -x "$DEST/usr/bin/nwg-dock-hyprland" ]]; then
  cp -Lf "$DEST/usr/bin/nwg-dock-hyprland" "$ROOT_ABS/usr/bin/nwg-dock-hyprland"
  chmod 0755 "$ROOT_ABS/usr/bin/nwg-dock-hyprland" 2>/dev/null || true
else
  copy_system_cmd nwg-dock-hyprland /usr/bin/nwg-dock-hyprland
fi
copy_system_cmd waybar /usr/bin/waybar
copy_system_cmd swaybg /usr/bin/swaybg
copy_system_cmd hyprctl /usr/bin/hyprctl
copy_system_cmd hyprpaper /usr/bin/hyprpaper
copy_system_cmd hyprland-dialog /usr/bin/hyprland-dialog
copy_system_cmd hyprland-run /usr/bin/hyprland-run
copy_system_cmd hyprland-welcome /usr/bin/hyprland-welcome
copy_system_cmd hyprland-update-screen /usr/bin/hyprland-update-screen
copy_system_cmd hyprland-donate-screen /usr/bin/hyprland-donate-screen
copy_system_cmd hyprland-share-picker /usr/bin/hyprland-share-picker
copy_system_cmd lspci /usr/bin/lspci
copy_system_cmd grep /usr/bin/grep
copy_system_cmd dbus-daemon /usr/bin/dbus-daemon
copy_system_cmd dbus-run-session /usr/bin/dbus-run-session
copy_system_cmd dbus-update-activation-environment /usr/bin/dbus-update-activation-environment
copy_system_cmd xdg-desktop-portal /usr/libexec/xdg-desktop-portal
copy_system_cmd xdg-desktop-portal-hyprland /usr/libexec/xdg-desktop-portal-hyprland
copy_system_cmd xdg-document-portal /usr/libexec/xdg-document-portal
copy_system_cmd xdg-permission-store /usr/libexec/xdg-permission-store
copy_system_cmd fusermount3 /usr/bin/fusermount3
if [[ -x "$ROOT_ABS/usr/bin/fusermount3" ]]; then
  cp -Lf "$ROOT_ABS/usr/bin/fusermount3" "$ROOT_ABS/usr/bin/fusermount3.real" 2>/dev/null || true
  mkdir -p "$ROOT_ABS/bin"
  cp -Lf "$ROOT_ABS/usr/bin/fusermount3" "$ROOT_ABS/bin/fusermount3"
  chmod 4755 "$ROOT_ABS/usr/bin/fusermount3" "$ROOT_ABS/bin/fusermount3" 2>/dev/null || true
fi
if [[ -x /usr/libexec/xdg-desktop-portal-gtk ]]; then
  copy_abs_into_rootfs /usr/libexec/xdg-desktop-portal-gtk
  scan_elf_deps /usr/libexec/xdg-desktop-portal-gtk
fi
copy_host_soname_into_rootfs libfuse3.so.4

if command -v gcc >/dev/null 2>&1; then
  gcc -O2 -Wall -Wextra -ffreestanding -fno-builtin \
    -mno-red-zone -mno-sse -mno-sse2 -mno-mmx \
    -fno-stack-protector -fno-pie -no-pie -nostdlib -static \
    -Wl,-e,_start \
    -o "$ROOT_ABS/usr/bin/ridux-dbus-session" "$SCRIPT_DIR/ridux_dbus_session.c"
  chmod 0755 "$ROOT_ABS/usr/bin/ridux-dbus-session"

  gcc -O2 -Wall -Wextra -ffreestanding -fno-builtin \
    -mno-red-zone -mno-sse -mno-sse2 -mno-mmx \
    -fno-stack-protector -fno-pie -no-pie -nostdlib -static \
    -Wl,-e,_start \
    -o "$ROOT_ABS/usr/bin/ridux-dbus-system" "$SCRIPT_DIR/ridux_dbus_system.c"
  chmod 0755 "$ROOT_ABS/usr/bin/ridux-dbus-system"

  gcc -O2 -Wall -Wextra -ffreestanding -fno-builtin \
    -mno-red-zone -mno-sse -mno-sse2 -mno-mmx \
    -fno-stack-protector -fno-pie -no-pie -nostdlib -static \
    -Wl,-e,_start \
    -o "$ROOT_ABS/usr/bin/ridux-open-launcher-native" "$SCRIPT_DIR/ridux_open_launcher_native.c"
  chmod 0755 "$ROOT_ABS/usr/bin/ridux-open-launcher-native"

  gcc -O2 -Wall -Wextra -ffreestanding -fno-builtin \
    -mno-red-zone -mno-sse -mno-sse2 -mno-mmx \
    -fno-stack-protector -fno-pie -no-pie -nostdlib -static \
    -Wl,-e,_start \
    -o "$ROOT_ABS/usr/bin/ridux-hyprland-session" "$SCRIPT_DIR/ridux_hyprland_session.c"
  chmod 0755 "$ROOT_ABS/usr/bin/ridux-hyprland-session"

  gcc -O2 -Wall -Wextra -ffreestanding -fno-builtin \
    -mno-red-zone -mno-sse -mno-sse2 -mno-mmx \
    -fno-stack-protector -fno-pie -no-pie -nostdlib -static \
    -Wl,-e,_start \
    -o "$ROOT_ABS/usr/bin/ridux-waybar" "$SCRIPT_DIR/ridux_waybar_launcher.c"
  chmod 0755 "$ROOT_ABS/usr/bin/ridux-waybar"

  gcc -O2 -Wall -Wextra -ffreestanding -fno-builtin \
    -mno-red-zone -mno-sse -mno-sse2 -mno-mmx \
    -fno-stack-protector -fno-pie -no-pie -nostdlib -static \
    -Wl,-e,_start \
    -o "$ROOT_ABS/usr/bin/ridux-user-services" "$SCRIPT_DIR/ridux_user_services.c"
  chmod 0755 "$ROOT_ABS/usr/bin/ridux-user-services"

  gcc -O2 -Wall -Wextra -ffreestanding -fno-builtin \
    -mno-red-zone -mno-sse -mno-sse2 -mno-mmx \
    -fno-stack-protector -fno-pie -no-pie -nostdlib -static \
    -Wl,-e,_start \
    -o "$ROOT_ABS/usr/bin/ridux-app-launcher" "$SCRIPT_DIR/ridux_app_launcher.c"
  for launcher_name in ridux-open-files ridux-terminal ridux-display-settings ridux-power-menu; do
    cp -Lf "$ROOT_ABS/usr/bin/ridux-app-launcher" "$ROOT_ABS/usr/bin/$launcher_name"
    chmod 0755 "$ROOT_ABS/usr/bin/$launcher_name"
  done

  gcc -O2 -Wall -Wextra -ffreestanding -fno-builtin \
    -mno-red-zone -mno-sse -mno-sse2 -mno-mmx \
    -fno-stack-protector -fno-pie -no-pie -nostdlib -static \
    -Wl,-e,_start \
    -o "$ROOT_ABS/usr/bin/ridux-session-spawn" "$SCRIPT_DIR/ridux_session_spawn.c"
  chmod 0755 "$ROOT_ABS/usr/bin/ridux-session-spawn"

  if pkg-config --exists gtk+-3.0 gtk-layer-shell-0; then
    gcc -O2 -Wall -Wextra -fno-pie -no-pie \
      -o "$ROOT_ABS/usr/bin/ridux-wallpaper" "$SCRIPT_DIR/ridux_wallpaper_gtk.c" \
      $(pkg-config --cflags --libs gtk+-3.0 gtk-layer-shell-0) -lm
    chmod 0755 "$ROOT_ABS/usr/bin/ridux-wallpaper"
    scan_elf_deps "$ROOT_ABS/usr/bin/ridux-wallpaper"
  else
    echo "[hyprland-rootfs] warn: gtk-layer-shell development files missing; wallpaper client not rebuilt" >&2
  fi

  gcc -O2 -Wall -Wextra -ffreestanding -fno-builtin \
    -mno-red-zone -fno-stack-protector -fno-pie -no-pie -nostdlib -static \
    -Wl,-e,_start \
    -o "$ROOT_ABS/usr/bin/fusermount3" "$SCRIPT_DIR/ridux_fusermount3.c"
  mkdir -p "$ROOT_ABS/bin"
  cp -Lf "$ROOT_ABS/usr/bin/fusermount3" "$ROOT_ABS/bin/fusermount3"
  chmod 0755 "$ROOT_ABS/usr/bin/fusermount3" "$ROOT_ABS/bin/fusermount3"
fi

while [[ "${#QUEUE[@]}" -gt 0 ]]; do
  dep="${QUEUE[0]}"
  QUEUE=("${QUEUE[@]:1}")
  [[ -z "${SEEN[$dep]:-}" ]] || continue
  SEEN["$dep"]=1
  copy_abs_into_rootfs "$dep"
  COPIED_DEPS+=("$dep")
  is_elf "$dep" && scan_elf_deps "$dep"
done

bundle_runtime_deps_into_opt \
  "$HYPRLAND_BIN" \
  "$ROOT_ABS/usr/bin/waybar" \
  "$ROOT_ABS/usr/bin/swaybg" \
  "$ROOT_ABS/usr/bin/hyprctl" \
  "$ROOT_ABS/usr/bin/nwg-dock-hyprland" \
  "$ROOT_ABS/usr/bin/ridux-wallpaper" \
  "$ROOT_ABS/usr/libexec/xdg-desktop-portal" \
  "$ROOT_ABS/usr/libexec/xdg-desktop-portal-hyprland" \
  "$ROOT_ABS/usr/libexec/xdg-desktop-portal-gtk" \
  "$ROOT_ABS/usr/libexec/xdg-document-portal" \
  "$ROOT_ABS/usr/libexec/xdg-permission-store"

install_opt_runtime_libs_into_system_search_path

mkdir -p \
  "$ROOT_ABS/etc/hypr" \
  "$ROOT_ABS/etc/xdg/xdg-desktop-portal" \
  "$ROOT_ABS/etc/xdg/xdg-desktop-portal-wlr" \
  "$ROOT_ABS/etc/xdg/waybar" \
  "$ROOT_ABS/etc/nwg-dock-hyprland" \
  "$ROOT_ABS/tmp/hyprland-home/config/hypr" \
  "$ROOT_ABS/tmp/hyprland-home/config/gtk-3.0" \
  "$ROOT_ABS/tmp/hyprland-home/config/gtk-4.0" \
  "$ROOT_ABS/tmp/hyprland-home/config/xdg-desktop-portal" \
  "$ROOT_ABS/tmp/hyprland-home/config/waybar" \
  "$ROOT_ABS/tmp/hyprland-home/config/nwg-dock-hyprland" \
  "$ROOT_ABS/tmp/hyprland-home/config/wofi" \
  "$ROOT_ABS/tmp/hyprland-home/cache/fontconfig" \
  "$ROOT_ABS/tmp/hyprland-home/cache/waybar" \
  "$ROOT_ABS/tmp/hyprland-home/share" \
  "$ROOT_ABS/tmp/hyprland-home/state" \
  "$ROOT_ABS/tmp/hyprland-waybar/config/gtk-3.0" \
  "$ROOT_ABS/tmp/hyprland-waybar/config/gtk-4.0" \
  "$ROOT_ABS/tmp/hyprland-waybar/config/xdg-desktop-portal" \
  "$ROOT_ABS/tmp/hyprland-waybar/cache" \
  "$ROOT_ABS/tmp/hyprland-waybar/cache/waybar" \
  "$ROOT_ABS/tmp/hyprland-waybar/share" \
  "$ROOT_ABS/tmp/hyprland-waybar/state" \
  "$ROOT_ABS/tmp/hyprland-home/.cache/waybar" \
  "$ROOT_ABS/tmp/fontconfig-cache" \
  "$ROOT_ABS/var/cache/fontconfig" \
  "$ROOT_ABS/usr/share/ridux/wallpapers" \
  "$ROOT_ABS/usr/share/icons/Ridux/256x256/apps" \
  "$ROOT_ABS/usr/share/pixmaps" \
  "$ROOT_ABS/etc/gtk-3.0" \
  "$ROOT_ABS/etc/gtk-4.0" \
  "$ROOT_ABS/usr/share/drirc.d" \
  "$ROOT_ABS/usr/share/wayland-sessions" \
  "$ROOT_ABS/usr/share/dbus-1" \
  "$ROOT_ABS/etc/dbus-1/session.d" \
  "$ROOT_ABS/etc/dbus-1/system.d" \
  "$ROOT_ABS/run/user/1000" \
  "$ROOT_ABS/etc"

cat > "$ROOT_ABS/etc/passwd" <<'EOF'
root:x:0:0:root:/root:/bin/sh
ridux:x:1000:1000:Ridux User:/tmp/hyprland-home:/bin/sh
EOF

cat > "$ROOT_ABS/etc/group" <<'EOF'
root:x:0:
ridux:x:1000:
EOF

cat > "$ROOT_ABS/etc/nsswitch.conf" <<'EOF'
passwd: files
group: files
shadow: files
hosts: files dns
EOF

for dbus_conf in /usr/share/dbus-1/session.conf /usr/share/dbus-1/system.conf; do
  if [[ -f "$dbus_conf" ]]; then
    copy_abs_into_rootfs "$dbus_conf"
  fi
done

for portal_conf in \
  /usr/share/dbus-1/services/org.freedesktop.impl.portal.desktop.gtk.service \
  /usr/share/dbus-1/services/org.freedesktop.impl.portal.desktop.hyprland.service \
  /usr/share/dbus-1/services/org.freedesktop.impl.portal.desktop.wlr.service \
  /usr/share/dbus-1/services/org.freedesktop.impl.portal.PermissionStore.service \
  /usr/share/dbus-1/services/org.freedesktop.portal.Documents.service \
  /usr/share/dbus-1/services/org.freedesktop.portal.Desktop.service \
  /usr/share/xdg-desktop-portal/portals/gtk.portal \
  /usr/share/xdg-desktop-portal/portals/hyprland.portal \
  /usr/share/xdg-desktop-portal/portals/wlr.portal; do
  if [[ -f "$portal_conf" ]]; then
    copy_abs_into_rootfs "$portal_conf"
  fi
done

if [[ -x "$ROOT_ABS/usr/libexec/xdg-document-portal" &&
      ! -f "$ROOT_ABS/usr/share/dbus-1/services/org.freedesktop.portal.Documents.service" ]]; then
  cat > "$ROOT_ABS/usr/share/dbus-1/services/org.freedesktop.portal.Documents.service" <<'EOF'
[D-BUS Service]
Name=org.freedesktop.portal.Documents
Exec=/usr/libexec/xdg-document-portal
SystemdService=xdg-document-portal.service
EOF
fi

cat > "$ROOT_ABS/usr/share/xdg-desktop-portal/hyprland-portals.conf" <<'EOF'
[preferred]
default=none;
org.freedesktop.impl.portal.ScreenCast=hyprland;
org.freedesktop.impl.portal.Screenshot=hyprland;
org.freedesktop.impl.portal.GlobalShortcuts=hyprland;
org.freedesktop.impl.portal.Settings=none;
EOF
cat > "$ROOT_ABS/etc/xdg/xdg-desktop-portal/hyprland-portals.conf" <<'EOF'
[preferred]
default=none;
org.freedesktop.impl.portal.ScreenCast=hyprland;
org.freedesktop.impl.portal.Screenshot=hyprland;
org.freedesktop.impl.portal.GlobalShortcuts=hyprland;
org.freedesktop.impl.portal.Settings=none;
EOF
cp "$ROOT_ABS/etc/xdg/xdg-desktop-portal/hyprland-portals.conf" \
   "$ROOT_ABS/etc/xdg/xdg-desktop-portal/portals.conf"
cp "$ROOT_ABS/etc/xdg/xdg-desktop-portal/hyprland-portals.conf" \
   "$ROOT_ABS/tmp/hyprland-home/config/xdg-desktop-portal/hyprland-portals.conf"
cp "$ROOT_ABS/etc/xdg/xdg-desktop-portal/hyprland-portals.conf" \
   "$ROOT_ABS/tmp/hyprland-home/config/xdg-desktop-portal/portals.conf"
cat > "$ROOT_ABS/tmp/hyprland-waybar/config/xdg-desktop-portal/hyprland-portals.conf" <<'EOF'
[preferred]
default=none;
EOF
cp "$ROOT_ABS/tmp/hyprland-waybar/config/xdg-desktop-portal/hyprland-portals.conf" \
   "$ROOT_ABS/tmp/hyprland-waybar/config/xdg-desktop-portal/portals.conf"
mkdir -p "$ROOT_ABS/usr/share/xdg-desktop-portal/ridux-portals"
if [[ -f "$ROOT_ABS/usr/share/xdg-desktop-portal/portals/gtk.portal" ]]; then
  sed 's/^UseIn=.*/UseIn=Hyprland;wlroots;Wayfire;gnome;/' \
    "$ROOT_ABS/usr/share/xdg-desktop-portal/portals/gtk.portal" \
    > "$ROOT_ABS/usr/share/xdg-desktop-portal/ridux-portals/gtk.portal"
  cp "$ROOT_ABS/usr/share/xdg-desktop-portal/ridux-portals/gtk.portal" \
    "$ROOT_ABS/usr/share/xdg-desktop-portal/portals/gtk.portal"
fi
cp "$ROOT_ABS/usr/share/xdg-desktop-portal/portals/hyprland.portal" \
   "$ROOT_ABS/usr/share/xdg-desktop-portal/ridux-portals/hyprland.portal" 2>/dev/null || true
cp "$ROOT_ABS/usr/share/xdg-desktop-portal/portals/wlr.portal" \
   "$ROOT_ABS/usr/share/xdg-desktop-portal/ridux-portals/wlr.portal" 2>/dev/null || true

stage_gsettings_schemas

if [[ -f "$REPO_DIR/Wallpapers/RiduxIconsWallpaper.png" ]]; then
  cp -Lf "$REPO_DIR/Wallpapers/RiduxIconsWallpaper.png" \
    "$ROOT_ABS/usr/share/ridux/wallpapers/RiduxIconsWallpaper.png"
elif [[ -f "$REPO_DIR/Wallpapers/WallpaperMain.png" ]]; then
  cp -Lf "$REPO_DIR/Wallpapers/WallpaperMain.png" \
    "$ROOT_ABS/usr/share/ridux/wallpapers/RiduxIconsWallpaper.png"
fi
if [[ -f "$REPO_DIR/RiduxIcons/RiduxIconLogo.png" ]]; then
  cp -Lf "$REPO_DIR/RiduxIcons/RiduxIconLogo.png" \
    "$ROOT_ABS/usr/share/icons/Ridux/256x256/apps/ridux-logo.png"
  cp -Lf "$REPO_DIR/RiduxIcons/RiduxIconLogo.png" \
    "$ROOT_ABS/usr/share/pixmaps/ridux-logo.png"
fi
if [[ -f "$REPO_DIR/RiduxIcons/FileBrowserIcon.png" ]]; then
  for icon_name in ridux-files org.xfce.thunar system-file-manager inode-directory; do
    cp -Lf "$REPO_DIR/RiduxIcons/FileBrowserIcon.png" \
      "$ROOT_ABS/usr/share/icons/Ridux/256x256/apps/${icon_name}.png"
  done
fi
if [[ -f "$REPO_DIR/RiduxIcons/TerminalIcon.png" ]]; then
  for icon_name in ridux-terminal utilities-terminal foot; do
    cp -Lf "$REPO_DIR/RiduxIcons/TerminalIcon.png" \
      "$ROOT_ABS/usr/share/icons/Ridux/256x256/apps/${icon_name}.png"
  done
fi
if [[ -f "$REPO_DIR/RiduxIcons/SettingsIcon.png" ]]; then
  for icon_name in ridux-settings preferences-system network.cycles.wdisplays; do
    cp -Lf "$REPO_DIR/RiduxIcons/SettingsIcon.png" \
      "$ROOT_ABS/usr/share/icons/Ridux/256x256/apps/${icon_name}.png"
  done
fi
if [[ -f "$REPO_DIR/RiduxIcons/SearchLupaIcon.png" ]]; then
  cp -Lf "$REPO_DIR/RiduxIcons/SearchLupaIcon.png" \
    "$ROOT_ABS/usr/share/icons/Ridux/256x256/apps/ridux-launcher.png"
fi

cat > "$ROOT_ABS/usr/share/icons/Ridux/index.theme" <<'EOF'
[Icon Theme]
Name=Ridux
Comment=RiduxOS desktop icons
Inherits=Adwaita,hicolor
Directories=256x256/apps

[256x256/apps]
Size=256
Context=Applications
Type=Fixed
EOF

cat > "$ROOT_ABS/etc/gtk-3.0/settings.ini" <<'EOF'
[Settings]
gtk-theme-name=Adwaita
gtk-icon-theme-name=Ridux
gtk-application-prefer-dark-theme=1
gtk-cursor-theme-name=Adwaita
gtk-cursor-theme-size=28
gtk-enable-animations=1
EOF
cp "$ROOT_ABS/etc/gtk-3.0/settings.ini" "$ROOT_ABS/etc/gtk-4.0/settings.ini"
cp "$ROOT_ABS/etc/gtk-3.0/settings.ini" "$ROOT_ABS/tmp/hyprland-home/config/gtk-3.0/settings.ini"
cp "$ROOT_ABS/etc/gtk-4.0/settings.ini" "$ROOT_ABS/tmp/hyprland-home/config/gtk-4.0/settings.ini"
cp "$ROOT_ABS/etc/gtk-3.0/settings.ini" "$ROOT_ABS/tmp/hyprland-waybar/config/gtk-3.0/settings.ini"
cp "$ROOT_ABS/etc/gtk-4.0/settings.ini" "$ROOT_ABS/tmp/hyprland-waybar/config/gtk-4.0/settings.ini"

cat > "$ROOT_ABS/etc/hypr/hyprland.conf" <<'EOF'
# RiduxOS Hyprland production session.
# The compositor owns blur, shadows, animation, input and presentation.
monitor=,preferred,auto,1

env = XCURSOR_THEME,Adwaita
env = XCURSOR_SIZE,28
env = XCURSOR_PATH,/usr/share/icons:/usr/share/pixmaps
env = HYPRCURSOR_THEME,Adwaita
env = HYPRCURSOR_SIZE,28
env = HYPRCURSOR_PATH,/usr/share/icons
env = HYPRLAND_NO_SD_NOTIFY,1
env = HYPRLAND_NO_SD_VARS,1
env = HYPRLAND_NO_RT,1
env = HYPRLAND_EGL_NO_MODIFIERS,1
env = AQ_NO_MODIFIERS,1
env = AQ_FORCE_LINEAR_BLIT,1
env = LIBINPUT_QUIRKS_DIR,/usr/share/libinput
env = LIBINPUT_QUIRKS_OVERRIDE_FILE,/usr/share/libinput/50-ridux.quirks
env = AQ_TRACE,0
env = WLR_BACKENDS,drm,libinput
env = WLR_LIBINPUT_NO_DEVICES,1
env = WLR_DRM_DEVICES,/dev/dri/card0
env = AQ_DRM_DEVICES,/dev/dri/card0
env = WLR_RENDERER,gles2
env = WLR_RENDERER_ALLOW_SOFTWARE,0
env = WLR_NO_HARDWARE_CURSORS,1
env = EGL_PLATFORM,gbm
env = GBM_BACKEND,drm
env = LIBGL_ALWAYS_SOFTWARE,0
env = NO_AT_BRIDGE,1
env = GTK_THEME,Adwaita
env = GDK_BACKEND,wayland
env = XDG_SESSION_TYPE,wayland
env = XDG_CURRENT_DESKTOP,Hyprland
env = XDG_SESSION_DESKTOP,Hyprland
env = DESKTOP_SESSION,hyprland
env = HOME,/tmp/hyprland-home
env = USER,ridux
env = LOGNAME,ridux
env = XDG_RUNTIME_DIR,/run/user/1000
env = XDG_CONFIG_HOME,/tmp/hyprland-home/config
env = XDG_CACHE_HOME,/tmp/hyprland-home/cache
env = XDG_DATA_HOME,/tmp/hyprland-home/share
env = XDG_STATE_HOME,/tmp/hyprland-home/state
env = DBUS_SESSION_BUS_ADDRESS,unix:path=/run/user/1000/bus
env = XDG_CONFIG_DIRS,/etc/xdg
env = XDG_DATA_DIRS,/tmp/hyprland-home/share:/usr/local/share:/usr/share
env = QT_QPA_PLATFORM,wayland
env = SDL_VIDEODRIVER,wayland
env = GSETTINGS_BACKEND,memory
env = GIO_USE_VFS,local
env = GIO_USE_VOLUME_MONITOR,unix
env = MESA_LOADER_DRIVER_OVERRIDE,virtio_gpu
env = GALLIUM_DRIVER,virgl
env = MESA_DRICONF_EXECUTABLE_OVERRIDE,Hyprland
env = MESA_EXTENSION_OVERRIDE,GL_EXT_texture_format_BGRA8888 GL_EXT_read_format_bgra -GL_KHR_parallel_shader_compile
env = MESA_GLTHREAD,false
env = mesa_glthread,false
env = GALLIUM_THREAD,0
env = MESA_SHADER_CACHE_DIR,/tmp/mesa-shader-cache
env = MESA_SHADER_CACHE_DISABLE,0
env = MESA_GLSL_CACHE_DISABLE,0
env = PIPEWIRE_MODULE_DIR,/usr/lib/x86_64-linux-gnu/pipewire-0.3
env = SPA_PLUGIN_DIR,/usr/lib/x86_64-linux-gnu/spa-0.2
env = WIREPLUMBER_CONFIG_DIR,/usr/share/wireplumber:/etc/wireplumber
env = PIPEWIRE_RUNTIME_DIR,/run/user/1000
env = PULSE_RUNTIME_PATH,/run/user/1000/pulse
env = PULSE_SERVER,unix:/run/user/1000/pulse/native

debug:disable_logs = true
debug:enable_stdout_logs = false
debug:disable_time = true
debug:colored_stdout_logs = false

exec-once = /usr/bin/dbus-update-activation-environment --systemd HOME WAYLAND_DISPLAY HYPRLAND_INSTANCE_SIGNATURE XDG_CURRENT_DESKTOP XDG_SESSION_DESKTOP DESKTOP_SESSION XDG_SESSION_TYPE XDG_RUNTIME_DIR XDG_CONFIG_HOME XDG_DATA_HOME XDG_CACHE_HOME XDG_CONFIG_DIRS GDK_BACKEND GTK_THEME GSETTINGS_BACKEND GIO_USE_VFS GIO_USE_VOLUME_MONITOR XDG_DATA_DIRS QT_QPA_PLATFORM SDL_VIDEODRIVER XCURSOR_THEME XCURSOR_SIZE XCURSOR_PATH HYPRCURSOR_THEME HYPRCURSOR_SIZE HYPRCURSOR_PATH PIPEWIRE_RUNTIME_DIR PULSE_RUNTIME_PATH PULSE_SERVER
exec-once = /usr/bin/swaybg -m fill -i /usr/share/ridux/wallpapers/RiduxIconsWallpaper.png
exec-once = /usr/bin/ridux-user-services
exec-once = /usr/bin/ridux-waybar

input {
    kb_layout = us
    follow_mouse = 1
    sensitivity = 0
}

cursor {
    no_hardware_cursors = true
    inactive_timeout = 0
    hide_on_key_press = false
    enable_hyprcursor = false
}

general {
    gaps_in = 6
    gaps_out = 12
    border_size = 1
    col.active_border = rgba(71b8ffee)
    col.inactive_border = rgba(ffffff33)
    layout = dwindle
    allow_tearing = false
}

decoration {
    rounding = 13
    active_opacity = 0.96
    inactive_opacity = 0.90
    fullscreen_opacity = 1.0

    blur {
        enabled = true
        size = 7
        passes = 2
        new_optimizations = true
        xray = false
    }

    shadow {
        enabled = true
        range = 22
        render_power = 3
        color = rgba(00000055)
        color_inactive = rgba(00000028)
    }
}

layerrule = match:namespace launcher, blur on, ignore_alpha 0
layerrule = match:namespace waybar, noanim, blur off

animations {
    enabled = true
    bezier = riduxEase, 0.16, 1, 0.3, 1
    animation = windows, 1, 5, riduxEase, popin 84%
    animation = windowsOut, 1, 4, riduxEase, popin 84%
    animation = border, 1, 5, riduxEase
    animation = fade, 1, 4, riduxEase
    animation = workspaces, 1, 5, riduxEase, slide
}

dwindle {
    preserve_split = true
}

misc {
    disable_hyprland_logo = true
    disable_splash_rendering = true
    vrr = 2
    animate_manual_resizes = true
    animate_mouse_windowdragging = true
    focus_on_activate = true
    allow_session_lock_restore = true
}

bind = SUPER, Return, exec, /usr/bin/ridux-session-spawn /usr/bin/ridux-terminal
bind = SUPER, Space, exec, /usr/bin/ridux-session-spawn /usr/bin/wofi --conf /etc/xdg/wofi/config --style /etc/xdg/wofi/style.css --show drun --insensitive
bind = SUPER, Q, killactive
bind = SUPER, F, fullscreen
bind = SUPER, V, togglefloating
bind = SUPER, left, movefocus, l
bind = SUPER, right, movefocus, r
bind = SUPER, up, movefocus, u
bind = SUPER, down, movefocus, d
bind = SUPER SHIFT, left, movewindow, l
bind = SUPER SHIFT, right, movewindow, r
bind = SUPER SHIFT, up, movewindow, u
bind = SUPER SHIFT, down, movewindow, d
EOF

cat > "$ROOT_ABS/usr/share/drirc.d/10-ridux-virgl.conf" <<'EOF'
<?xml version="1.0" standalone="yes"?>
<!DOCTYPE driconf [
<!ELEMENT driconf      (device+)>
<!ELEMENT device       (application | engine)+>
<!ELEMENT application  (option+)>
<!ELEMENT engine       (option+)>
<!ELEMENT option       EMPTY>
<!ATTLIST device       driver CDATA #IMPLIED>
<!ATTLIST application  name CDATA #REQUIRED executable CDATA #REQUIRED>
<!ATTLIST engine       engine_name_match CDATA #REQUIRED>
<!ATTLIST option       name CDATA #REQUIRED value CDATA #REQUIRED>
]>
<driconf>
    <device driver="virtio_gpu">
        <application name="Ridux Hyprland" executable="Hyprland">
            <option name="gles_emulate_bgra" value="true" />
            <option name="gles_apply_bgra_dest_swizzle" value="true" />
        </application>
        <application name="Ridux Waybar" executable="waybar">
            <option name="gles_emulate_bgra" value="true" />
            <option name="gles_apply_bgra_dest_swizzle" value="true" />
        </application>
        <application name="Ridux Dock" executable="nwg-dock-hyprland">
            <option name="gles_emulate_bgra" value="true" />
            <option name="gles_apply_bgra_dest_swizzle" value="true" />
        </application>
    </device>
</driconf>
EOF

if [ -d "$ROOT_ABS/usr/share/icons/Adwaita/cursors" ]; then
  while IFS= read -r link; do
    target="$(readlink "$link" || true)"
    [ -n "$target" ] || continue
    case "$target" in
      /*) src="$ROOT_ABS$target" ;;
      *) src="$(dirname "$link")/$target" ;;
    esac
    if [ -f "$src" ]; then
      rm -f "$link"
      cp "$src" "$link"
    fi
  done < <(find "$ROOT_ABS/usr/share/icons/Adwaita/cursors" -type l)

  ensure_cursor_alias() {
    local alias_name="$1"
    local source_name="$2"
    local cursor_dir="$ROOT_ABS/usr/share/icons/Adwaita/cursors"
    if [ ! -e "$cursor_dir/$alias_name" ] && [ -f "$cursor_dir/$source_name" ]; then
      cp "$cursor_dir/$source_name" "$cursor_dir/$alias_name"
    fi
  }

  ensure_cursor_alias plus copy
  ensure_cursor_alias dnd-link alias
  ensure_cursor_alias dnd-copy copy
  ensure_cursor_alias dnd-none no-drop
  ensure_cursor_alias crossed_circle not-allowed
fi

if command -v gcc >/dev/null 2>&1; then
  gcc -O2 -Wall -Wextra -ffreestanding -fno-builtin \
    -fno-stack-protector -fno-pie -no-pie -nostdlib -static \
    -Wl,-e,_start \
    -o "$ROOT_ABS/usr/bin/systemctl" "$SCRIPT_DIR/ridux_systemctl_shim.c"
  chmod 0755 "$ROOT_ABS/usr/bin/systemctl"
fi

cat > "$ROOT_ABS/etc/xdg/waybar/config" <<'EOF'
{
  "layer": "top",
  "position": "bottom",
  "height": 50,
  "margin": "0",
  "spacing": 5,
  "exclusive": true,
  "passthrough": false,
  "fixed-center": true,
  "modules-left": [
    "custom/start",
    "hyprland/workspaces",
    "custom/files",
    "custom/terminal",
    "custom/browser"
  ],
  "modules-center": [
    "clock"
  ],
  "modules-right": [
    "custom/network",
    "custom/audio",
    "custom/settings",
    "custom/power"
  ],
  "custom/start": {
    "format": "R",
    "tooltip": false,
    "on-click": "/usr/bin/ridux-session-spawn /usr/bin/wofi --conf /etc/xdg/wofi/config --style /etc/xdg/wofi/style.css --show drun --insensitive"
  },
  "hyprland/workspaces": {
    "format": "{name}",
    "tooltip": false,
    "all-outputs": true,
    "on-click": "activate"
  },
  "custom/files": {
    "format": "Files",
    "tooltip": false,
    "on-click": "/usr/bin/ridux-session-spawn /usr/bin/ridux-open-files"
  },
  "custom/terminal": {
    "format": "Term",
    "tooltip": false,
    "on-click": "/usr/bin/ridux-session-spawn /usr/bin/ridux-terminal"
  },
  "custom/browser": {
    "format": "Apps",
    "tooltip": false,
    "on-click": "/usr/bin/ridux-session-spawn /usr/bin/wofi --conf /etc/xdg/wofi/config --style /etc/xdg/wofi/style.css --show drun --insensitive"
  },
  "custom/network": {
    "format": "Wi-Fi",
    "tooltip": false
  },
  "custom/audio": {
    "format": "Vol",
    "tooltip": false
  },
  "clock": {
    "format": "{:%H:%M}",
    "format-alt": "{:%a %d %b  %H:%M}",
    "tooltip": false
  },
  "custom/settings": {
    "format": "Display",
    "tooltip": false,
    "on-click": "/usr/bin/ridux-session-spawn /usr/bin/ridux-display-settings"
  },
  "custom/power": {
    "format": "Power",
    "tooltip": false,
    "on-click": "/usr/bin/ridux-session-spawn /usr/bin/ridux-power-menu"
  }
}
EOF

cat > "$ROOT_ABS/etc/xdg/waybar/style.css" <<'EOF'
* {
  border: none;
  border-radius: 0;
  font-family: "Ridux Sans", "Noto Sans", "DejaVu Sans", sans-serif;
  font-size: 12.5px;
  font-weight: 600;
  min-height: 0;
}

window#waybar {
  background: rgba(13, 17, 25, 0.54);
  border-top: 1px solid rgba(255, 255, 255, 0.18);
  color: rgba(248, 251, 255, 0.96);
  box-shadow:
    0 -18px 44px rgba(0, 0, 0, 0.34),
    inset 0 1px rgba(255, 255, 255, 0.11);
}

#custom-start,
#workspaces button,
#custom-files,
#custom-terminal,
#custom-browser,
#custom-network,
#custom-audio,
#clock,
#custom-settings,
#custom-power {
  margin: 7px 2px 8px;
  padding: 0 12px;
  border-radius: 12px;
  border: 1px solid transparent;
  background: transparent;
  color: rgba(238, 245, 255, 0.92);
  box-shadow:
    inset 0 1px rgba(255, 255, 255, 0.00),
    inset 0 -1px rgba(0, 0, 0, 0.00);
}

#custom-start {
  margin-left: 10px;
  min-width: 34px;
  padding: 0 0;
  border-radius: 13px;
  border-color: rgba(160, 214, 255, 0.38);
  background: rgba(44, 126, 232, 0.58);
  color: #ffffff;
  font-size: 15px;
  font-weight: 800;
  box-shadow:
    0 8px 18px rgba(16, 82, 170, 0.30),
    inset 0 1px rgba(255, 255, 255, 0.28);
}

#workspaces {
  margin: 0 7px;
}

#workspaces button {
  min-width: 30px;
  padding: 0 8px;
  color: rgba(224, 235, 249, 0.78);
}

#workspaces button.active {
  border-color: rgba(148, 213, 255, 0.34);
  background: rgba(255, 255, 255, 0.13);
  color: #ffffff;
  box-shadow:
    inset 0 1px rgba(255, 255, 255, 0.18),
    0 8px 18px rgba(0, 0, 0, 0.18);
}

#workspaces button.urgent {
  background: rgba(236, 104, 85, 0.64);
  color: #ffffff;
}

#workspaces button:hover,
#custom-start:hover,
#custom-files:hover,
#custom-terminal:hover,
#custom-browser:hover,
#custom-settings:hover,
#custom-power:hover {
  border-color: rgba(255, 255, 255, 0.12);
  background: rgba(255, 255, 255, 0.105);
  box-shadow:
    inset 0 1px rgba(255, 255, 255, 0.16),
    0 8px 20px rgba(0, 0, 0, 0.16);
}

#clock {
  min-width: 76px;
  font-weight: 800;
  border-color: rgba(255, 255, 255, 0.10);
  background: rgba(255, 255, 255, 0.08);
}

#custom-power {
  margin-right: 9px;
  color: rgba(255, 242, 238, 0.96);
}

EOF

cat > "$ROOT_ABS/etc/nwg-dock-hyprland/ridux.css" <<'EOF'
window {
  background: rgba(14, 20, 32, 0.72);
  border: 1px solid rgba(255, 255, 255, 0.22);
  border-radius: 20px;
  color: rgba(245, 249, 255, 0.96);
}

#box {
  padding: 8px 12px;
}

#active {
  border-bottom: 2px solid rgba(98, 184, 255, 0.92);
}

button,
image {
  background: none;
  border-style: none;
  box-shadow: none;
  color: rgba(245, 249, 255, 0.96);
}

button {
  margin: 5px 6px;
  padding: 8px;
  border-radius: 15px;
  background: rgba(255, 255, 255, 0.11);
  box-shadow: inset 0 1px rgba(255, 255, 255, 0.18), 0 10px 24px rgba(0, 0, 0, 0.24);
}

button:hover {
  background: rgba(84, 169, 255, 0.34);
}

button:active,
button:checked {
  background: rgba(84, 169, 255, 0.58);
}

button:focus {
  outline-style: none;
}
EOF

cp "$ROOT_ABS/etc/nwg-dock-hyprland/ridux.css" "$ROOT_ABS/etc/nwg-dock-hyprland/style.css"

cp "$ROOT_ABS/etc/hypr/hyprland.conf" "$ROOT_ABS/tmp/hyprland-home/config/hypr/hyprland.conf"
cp "$ROOT_ABS/etc/xdg/waybar/config" "$ROOT_ABS/tmp/hyprland-home/config/waybar/config"
cp "$ROOT_ABS/etc/xdg/waybar/style.css" "$ROOT_ABS/tmp/hyprland-home/config/waybar/style.css"
cp "$ROOT_ABS/etc/nwg-dock-hyprland/style.css" "$ROOT_ABS/tmp/hyprland-home/config/nwg-dock-hyprland/style.css"
cp "$ROOT_ABS/etc/xdg/wofi/config" "$ROOT_ABS/tmp/hyprland-home/config/wofi/config"
cp "$ROOT_ABS/etc/xdg/wofi/style.css" "$ROOT_ABS/tmp/hyprland-home/config/wofi/style.css"

cat > "$ROOT_ABS/usr/share/wayland-sessions/ridux-hyprland.desktop" <<'EOF'
[Desktop Entry]
Name=Ridux Hyprland
Comment=RiduxOS Hyprland Wayland session
Exec=/usr/bin/Hyprland
Type=Application
DesktopNames=Hyprland;Ridux
EOF

: > "$ROOT_ABS/etc/ridux-hyprland-primary.enable"
: > "$ROOT_ABS/etc/ridux-hyprland-gpu.enable"
: > "$ROOT_ABS/etc/ridux-hyprland-virtio-gpu.enable"

{
  echo "source_prefix=$INSTALL_ABS"
  echo "rootfs_dest=/opt/hyprland"
  echo "hyprland_bin=${HYPRLAND_BIN#$ROOT_ABS}"
  echo "timestamp_utc=$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
  echo "copied_system_deps=${#COPIED_DEPS[@]}"
  echo "bundled_opt_deps=${#OPT_BUNDLE_DEPS[@]}"
  printf '%s\n' "${COPIED_DEPS[@]}" | sort -u
  printf '%s\n' "${OPT_BUNDLE_DEPS[@]}" | sort -u
} > "$MANIFEST"

echo "[hyprland-rootfs] done"
echo "  dest: $DEST"
echo "  binary: ${HYPRLAND_BIN#$ROOT_ABS}"
echo "  copied system deps: ${#COPIED_DEPS[@]}"
echo "  manifest: $MANIFEST"
