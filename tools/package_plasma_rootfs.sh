#!/usr/bin/env bash
set -euo pipefail

ROOTFS_DIR="${1:-rootfs}"
INSTALL_DIR="${2:-third_party/kde/install}"

if [[ ! -d "$ROOTFS_DIR" ]]; then
  echo "[plasma-rootfs] rootfs dir not found: $ROOTFS_DIR" >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_ABS="$(cd "$ROOTFS_DIR" && pwd -P)"
DEST="$ROOT_ABS/opt/kde-plasma"
MANIFEST="$DEST/PLASMA-MANIFEST.txt"
DEB_CACHE="${RIDUX_DEB_CACHE:-third_party/debian-plasma-stack/cache}"
FETCH_DEBIAN="${RIDUX_FETCH_DEBIAN_PLASMA:-0}"
GPU_KIND="${RIDUX_PLASMA_GPU:-virtio}"
WORK_DIR=""
PKG_ROOT=""

case "$DEST" in
  "$ROOT_ABS"/opt/kde-plasma) ;;
  *)
    echo "[plasma-rootfs] refusing unsafe destination: $DEST" >&2
    exit 2
    ;;
esac

cleanup() {
  [[ -z "$WORK_DIR" ]] || rm -rf "$WORK_DIR"
}
trap cleanup EXIT

log_manifest() {
  printf '%s\n' "$*" >> "$MANIFEST"
}

is_elf() {
  [[ -f "$1" ]] && readelf -h "$1" >/dev/null 2>&1
}

copy_deps_into_rootfs() {
  local bin="$1"
  local dep
  command -v ldd >/dev/null 2>&1 || return 0
  LD_LIBRARY_PATH="$DEST/lib:$DEST/lib64:$DEST/lib/x86_64-linux-gnu:$DEST/usr/lib:$DEST/usr/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}" \
    ldd "$bin" 2>/dev/null | awk '
      $1 ~ /^\// { print $1 }
      $3 ~ /^\// { print $3 }
    ' | sort -u | while read -r dep; do
      [[ -f "$dep" ]] || continue
      mkdir -p "$ROOT_ABS$(dirname "$dep")"
      cp -Lf "$dep" "$ROOT_ABS$dep" 2>/dev/null || true
    done
}

copy_file_to_dest() {
  local src="$1"
  local dst="$2"
  [[ -e "$src" ]] || return 0
  mkdir -p "$(dirname "$dst")"
  cp -aL "$src" "$dst" 2>/dev/null || true
  [[ -f "$src" ]] && copy_deps_into_rootfs "$src"
  log_manifest "file: $src -> ${dst#$ROOT_ABS}"
}

copy_tree_to_dest() {
  local src="$1"
  local dst="$2"
  [[ -d "$src" ]] || return 0
  mkdir -p "$dst"
  cp -a "$src"/. "$dst"/ 2>/dev/null || true
  log_manifest "tree: $src -> ${dst#$ROOT_ABS}"
}

copy_glob_to_dest() {
  local src_dir="$1"
  local dst_dir="$2"
  local pattern="$3"
  local f
  [[ -d "$src_dir" ]] || return 0
  mkdir -p "$dst_dir"
  for f in "$src_dir"/$pattern; do
    [[ -e "$f" ]] || continue
    copy_file_to_dest "$f" "$dst_dir/$(basename "$f")"
  done
}

materialize_locale_data() {
  mkdir -p "$ROOT_ABS/usr/lib/locale" "$ROOT_ABS/etc"
  if [[ -d /usr/lib/locale/C.utf8 ]]; then
    rm -rf "$ROOT_ABS/usr/lib/locale/C.utf8"
    cp -a /usr/lib/locale/C.utf8 "$ROOT_ABS/usr/lib/locale/"
    log_manifest "locale: copied /usr/lib/locale/C.utf8"
  else
    log_manifest "locale-missing: /usr/lib/locale/C.utf8"
  fi
  if [[ -f /usr/lib/locale/locale-archive ]]; then
    cp -a /usr/lib/locale/locale-archive "$ROOT_ABS/usr/lib/locale/locale-archive"
    log_manifest "locale: copied /usr/lib/locale/locale-archive"
  else
    log_manifest "locale-missing: /usr/lib/locale/locale-archive"
  fi
  cat > "$ROOT_ABS/etc/locale.conf" <<'EOF'
LANG=C.utf8
LC_ALL=C.utf8
LC_CTYPE=C.utf8
EOF
}

download_deb_stack_if_requested() {
  [[ "$FETCH_DEBIAN" = "1" || "$FETCH_DEBIAN" = "true" ]] || return 0
  command -v apt-get >/dev/null 2>&1 || {
    echo "[plasma-rootfs] apt-get not available; skipping package fetch"
    return 0
  }
  command -v dpkg-deb >/dev/null 2>&1 || {
    echo "[plasma-rootfs] dpkg-deb not available; skipping package fetch"
    return 0
  }

  mkdir -p "$DEB_CACHE"
  (
    cd "$DEB_CACHE"
    for pkg in \
      plasma-workspace plasma-desktop plasma-integration plasma-pa \
      kwin-wayland kwin-common kded6 kactivitymanagerd kglobalacceld \
      libkwin6 libkdecorations3-6 libkdecorations3private2 kwin-style-breeze breeze \
      kio6 kpackagetool6 systemsettings dolphin konsole \
      libkf6coreaddons6 libkf6configcore6 libkf6configgui6 libkf6i18n6 \
      libkf6service-bin libkf6service6 libkf6windowsystem6 libkf6dbusaddons6 \
      libkf6kiocore6 libkf6kiogui6 libkf6kiowidgets6 libkf6package6 \
      libkf6notifications6 libkf6globalaccel6 \
      libkglobalacceld0 libkf6guiaddons6 libkf6kirigami6 libplasma6 libplasmaquick6 \
      libkworkspace6-6 libplasmaactivities6 plasma-activities-bin libkpipewire6 \
      libkf6svg6 libkf6configwidgets6 libkf6crash6 libkf6idletime6 \
      libkf6archive6 libkf6colorscheme6 libkf6configqml6 libkf6iconthemes6 \
      libkf6solid6 libkf6statusnotifieritem6 libkf6userfeedbackcore6 libkf6xmlgui6 \
      libkf6codecs6 libkf6i18nqml6 libkf6jobwidgets6 libkf6widgetsaddons6 \
      libkf6breezeicons6 libkf6iconwidgets6 libkf6itemviews6 libkf6kcmutilsquick6 \
      libkf6authcore6 libkf6completion6 libkf6kcmutilscore6 libkf6kcmutils6 \
      libkwaylandclient6 liblayershellqtinterface6 layer-shell-qt \
      libkscreenlocker6 kde-config-screenlocker \
      libqaccessibilityclient-qt6-0 \
      qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts \
      qml6-module-org-kde-kirigami qml6-module-org-kde-kcmutils \
      qml6-module-org-kde-kwindowsystem \
      qt6-wayland libqt6core6t64 libqt6gui6 libqt6widgets6 libqt6qml6 \
      libqt6quick6 libqt6waylandclient6 libqt6waylandeglclienthwintegration6 \
      libqt6sensors6 \
      libegl-mesa0 libgl1-mesa-dri libgbm1 libglvnd0 libegl1 libgles2 \
      libdrm2 libdrm-amdgpu1 libdrm-intel1 libwayland-client0 libwayland-server0 \
      dbus dbus-user-session xdg-desktop-portal xdg-desktop-portal-kde \
      shared-mime-info desktop-file-utils hicolor-icon-theme breeze-icon-theme
    do
      if ls "${pkg}"_*_amd64.deb "${pkg}"_*_all.deb >/dev/null 2>&1; then
        echo "[plasma-rootfs] cache: $pkg"
      else
        apt-get download "$pkg" >/dev/null 2>&1 && \
          echo "[plasma-rootfs] downloaded: $pkg" || \
          echo "[plasma-rootfs] missing package: $pkg"
      fi
    done
  )
}

extract_cached_debs() {
  local deb count=0
  command -v dpkg-deb >/dev/null 2>&1 || return 0
  WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/ridux-plasma.XXXXXX")"
  PKG_ROOT="$WORK_DIR/pkgroot"
  mkdir -p "$PKG_ROOT"
  for deb in "$DEB_CACHE"/*.deb; do
    [[ -f "$deb" ]] || continue
    dpkg-deb -x "$deb" "$PKG_ROOT" >/dev/null 2>&1 || true
    count=$((count + 1))
  done
  echo "[plasma-rootfs] extracted cached debs: $count"
}

copy_usr_payload_from() {
  local usr="$1"
  local bin
  [[ -d "$usr" ]] || return 0

  for bin in \
    startplasma-wayland kwin_wayland plasmashell kded6 kglobalacceld \
    kactivitymanagerd ksmserver klauncher kbuildsycoca6 systemsettings \
    dolphin konsole dbus-daemon dbus-launch xdg-desktop-portal \
    xdg-desktop-portal-kde
  do
    [[ ! -x "$usr/bin/$bin" ]] || copy_file_to_dest "$usr/bin/$bin" "$DEST/usr/bin/$bin"
    [[ ! -x "$usr/libexec/$bin" ]] || copy_file_to_dest "$usr/libexec/$bin" "$DEST/usr/libexec/$bin"
    [[ ! -x "$usr/lib/x86_64-linux-gnu/libexec/$bin" ]] || \
      copy_file_to_dest "$usr/lib/x86_64-linux-gnu/libexec/$bin" "$DEST/usr/lib/x86_64-linux-gnu/libexec/$bin"
  done

  for d in \
    share/plasma share/kpackage share/kservices6 share/knotifications6 \
    share/kglobalaccel share/ksmserver share/kwin share/kxmlgui5 share/kxmlgui6 \
    share/applications share/dbus-1 share/wayland-sessions share/xdg-desktop-portal \
    share/icons/hicolor share/icons/breeze share/color-schemes share/wallpapers \
    share/mime share/metainfo
  do
    copy_tree_to_dest "$usr/$d" "$DEST/usr/$d"
  done

  copy_tree_to_dest "$usr/lib/x86_64-linux-gnu/qt6" "$DEST/usr/lib/x86_64-linux-gnu/qt6"
  copy_tree_to_dest "$usr/lib/qt6" "$DEST/usr/lib/qt6"
  copy_tree_to_dest "$usr/lib/x86_64-linux-gnu/plugins" "$DEST/usr/lib/x86_64-linux-gnu/plugins"
  copy_tree_to_dest "$usr/lib/x86_64-linux-gnu/qml" "$DEST/usr/lib/x86_64-linux-gnu/qml"

  copy_glob_to_dest "$usr/lib/x86_64-linux-gnu" "$DEST/usr/lib/x86_64-linux-gnu" "libQt6*.so*"
  copy_glob_to_dest "$usr/lib/x86_64-linux-gnu" "$DEST/usr/lib/x86_64-linux-gnu" "libKF6*.so*"
  copy_glob_to_dest "$usr/lib/x86_64-linux-gnu" "$DEST/usr/lib/x86_64-linux-gnu" "libK*.so*"
  copy_glob_to_dest "$usr/lib/x86_64-linux-gnu" "$DEST/usr/lib/x86_64-linux-gnu" "libPlasma*.so*"
  copy_glob_to_dest "$usr/lib/x86_64-linux-gnu" "$DEST/usr/lib/x86_64-linux-gnu" "libk*.so*"
  copy_glob_to_dest "$usr/lib/x86_64-linux-gnu" "$DEST/usr/lib/x86_64-linux-gnu" "libLayerShellQt*.so*"
  copy_glob_to_dest "$usr/lib/x86_64-linux-gnu" "$DEST/usr/lib/x86_64-linux-gnu" "libqaccessibilityclient*.so*"
  copy_glob_to_dest "$usr/lib/x86_64-linux-gnu" "$DEST/usr/lib/x86_64-linux-gnu" "libdbus-1.so*"
  copy_glob_to_dest "$usr/lib/x86_64-linux-gnu" "$DEST/usr/lib/x86_64-linux-gnu" "libwayland-*.so*"
}

copy_source_install_prefix() {
  local prefix="$1"
  [[ -d "$prefix" ]] || return 0
  echo "[plasma-rootfs] staging source install prefix: $prefix"
  cp -aL "$prefix"/. "$DEST"/ 2>/dev/null || true
  log_manifest "source-prefix: $prefix -> /opt/kde-plasma"
  find "$prefix" -type f -perm -0100 -print 2>/dev/null | while read -r f; do
    copy_deps_into_rootfs "$f"
  done
}

stage_gpu_runtime() {
  local usr="$1"
  [[ -d "$usr" ]] || return 0

  copy_tree_to_dest "$usr/share/glvnd" "$ROOT_ABS/usr/share/glvnd"
  copy_tree_to_dest "$usr/share/drirc.d" "$ROOT_ABS/usr/share/drirc.d"
  copy_glob_to_dest "$usr/lib/x86_64-linux-gnu" "$ROOT_ABS/usr/lib/x86_64-linux-gnu" "libEGL*.so*"
  copy_glob_to_dest "$usr/lib/x86_64-linux-gnu" "$ROOT_ABS/usr/lib/x86_64-linux-gnu" "libGL*.so*"
  copy_glob_to_dest "$usr/lib/x86_64-linux-gnu" "$ROOT_ABS/usr/lib/x86_64-linux-gnu" "libGLES*.so*"
  copy_glob_to_dest "$usr/lib/x86_64-linux-gnu" "$ROOT_ABS/usr/lib/x86_64-linux-gnu" "libgbm*.so*"
  copy_glob_to_dest "$usr/lib/x86_64-linux-gnu" "$ROOT_ABS/usr/lib/x86_64-linux-gnu" "libdrm*.so*"
  copy_glob_to_dest "$usr/lib/x86_64-linux-gnu" "$ROOT_ABS/usr/lib/x86_64-linux-gnu" "libvulkan*.so*"
  copy_tree_to_dest "$usr/lib/x86_64-linux-gnu/dri" "$ROOT_ABS/usr/lib/x86_64-linux-gnu/dri"
  copy_tree_to_dest "$usr/lib/x86_64-linux-gnu/gbm" "$ROOT_ABS/usr/lib/x86_64-linux-gnu/gbm"
}

write_plasma_state() {
  mkdir -p \
    "$ROOT_ABS/etc" \
    "$ROOT_ABS/tmp/kde-home/config" \
    "$ROOT_ABS/tmp/kde-home/cache" \
    "$ROOT_ABS/tmp/kde-home/share" \
    "$ROOT_ABS/tmp/kde-home/state" \
    "$ROOT_ABS/tmp/kde-tmp" \
    "$ROOT_ABS/tmp/kde-var-tmp" \
    "$ROOT_ABS/run/user/1000" \
    "$ROOT_ABS/run/dbus" \
    "$ROOT_ABS/var/lib/dbus"

  : > "$ROOT_ABS/etc/ridux-plasma-gpu.required"
  rm -f "$ROOT_ABS/etc/ridux-plasma-vbox-gpu.enable" "$ROOT_ABS/etc/ridux-plasma-virtio-gpu.enable"
  case "$GPU_KIND" in
    vbox|vmwgfx|svga)
      : > "$ROOT_ABS/etc/ridux-plasma-vbox-gpu.enable"
      ;;
    *)
      : > "$ROOT_ABS/etc/ridux-plasma-virtio-gpu.enable"
      ;;
  esac

  printf '7f32c1d83e1e4a5db42b8c3d0f55a9c1\n' > "$ROOT_ABS/etc/machine-id"
  printf '7f32c1d83e1e4a5db42b8c3d0f55a9c1\n' > "$ROOT_ABS/var/lib/dbus/machine-id"
  cat > "$ROOT_ABS/tmp/kde-home/config/kdeglobals" <<'EOF'
[General]
ColorScheme=BreezeDark
Name=RiduxOS

[KDE]
SingleClick=false
EOF
  cat > "$ROOT_ABS/tmp/kde-home/config/kwinrc" <<'EOF'
[Compositing]
Backend=OpenGL
Enabled=true
GLCore=true
OpenGLIsUnsafe=false

[Wayland]
VirtualOutputs=1
EOF
  cat > "$ROOT_ABS/tmp/kde-home/config/plasmashellrc" <<'EOF'
[PlasmaViews][Panel 1]
panelVisibility=0

[Updates]
performed=/usr/share/plasma/shells/org.kde.plasma.desktop/contents/updates
EOF
}

rm -rf "$DEST"
mkdir -p "$DEST" "$ROOT_ABS/usr/lib/x86_64-linux-gnu" "$ROOT_ABS/usr/share"
: > "$MANIFEST"

download_deb_stack_if_requested
extract_cached_debs
materialize_locale_data

copy_source_install_prefix "$INSTALL_DIR"
[[ -z "$PKG_ROOT" ]] || copy_usr_payload_from "$PKG_ROOT/usr"
copy_usr_payload_from /usr

[[ -z "$PKG_ROOT" ]] || stage_gpu_runtime "$PKG_ROOT/usr"
stage_gpu_runtime /usr

write_plasma_state

if ! is_elf "$DEST/bin/kwin_wayland" && ! is_elf "$DEST/usr/bin/kwin_wayland"; then
  echo "[plasma-rootfs] missing kwin_wayland ELF64 payload." >&2
  echo "[plasma-rootfs] Options:" >&2
  echo "  RIDUX_FETCH_DEBIAN_PLASMA=1 make plasma-rootfs" >&2
  echo "  KDE_INSTALL_DIR=/path/to/kde/install make plasma-rootfs" >&2
  exit 10
fi

if ! is_elf "$DEST/bin/plasmashell" && ! is_elf "$DEST/usr/bin/plasmashell"; then
  echo "[plasma-rootfs] missing plasmashell ELF64 payload." >&2
  exit 11
fi

{
  echo "rootfs=/"
  echo "dest=/opt/kde-plasma"
  echo "install_dir=$INSTALL_DIR"
  echo "fetch_debian=$FETCH_DEBIAN"
  echo "gpu_required=1"
  echo "gpu_kind=$GPU_KIND"
  echo "timestamp_utc=$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
} >> "$MANIFEST"

echo "[plasma-rootfs] done"
echo "  dest: $DEST"
echo "  manifest: $MANIFEST"
echo "  GPU: required ($GPU_KIND)"
