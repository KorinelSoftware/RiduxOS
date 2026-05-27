#!/usr/bin/env bash
set -euo pipefail

ROOT="${1:-rootfs}"
mkdir -p "$ROOT"
ROOT="$(cd "$ROOT" && pwd)"
MANIFEST="$ROOT/opt/wayfire/DESKTOP-STACK-MANIFEST.txt"
FETCH_DEBIAN="${RIDUX_FETCH_DEBIAN_STACK:-0}"
INCLUDE_QT_STACK="${RIDUX_INCLUDE_QT_STACK:-0}"
DEB_CACHE="${RIDUX_DEB_CACHE:-third_party/debian-wayland-stack/cache}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "$DEB_CACHE"
DEB_CACHE="$(cd "$DEB_CACHE" && pwd)"

mkdir -p \
  "$ROOT/usr/bin" \
  "$ROOT/usr/libexec" \
  "$ROOT/usr/share/applications" \
  "$ROOT/usr/share/dbus-1/services" \
  "$ROOT/usr/share/wayland-sessions" \
  "$ROOT/etc/xdg/waybar" \
  "$ROOT/etc/xdg/wofi" \
  "$ROOT/etc/xdg/swaync" \
  "$ROOT/etc/wlogout" \
  "$ROOT/etc/xdg/xdg-desktop-portal" \
  "$ROOT/etc/xdg/xdg-desktop-portal-wlr" \
  "$ROOT/etc/pipewire" \
  "$ROOT/opt/wayfire"

: > "$MANIFEST"
. "$SCRIPT_DIR/wayland_stack_lib.sh"

download_deb_stack_if_requested() {
  [ "$FETCH_DEBIAN" = "1" ] || [ "$FETCH_DEBIAN" = "true" ] || return 0
  command -v apt-get >/dev/null 2>&1 || {
    log_manifest "debian-fetch: apt-get not available"
    return 0
  }
  command -v dpkg-deb >/dev/null 2>&1 || {
    log_manifest "debian-fetch: dpkg-deb not available"
    return 0
  }

  mkdir -p "$DEB_CACHE"
  (
    cd "$DEB_CACHE"
    for pkg in \
      waybar wofi \
      foot foot-terminfo libfcft4t64 libutf8proc3 ncurses-term \
      wlogout wdisplays \
      pipewire-bin pipewire wireplumber pipewire-pulse \
      libpipewire-0.3-0t64 libpipewire-0.3-modules libspa-0.2-modules \
      xdg-desktop-portal xdg-desktop-portal-wlr \
      sway-notification-center swaylock grim slurp \
      thunar thunar-data exo-utils libexo-2-0 libexo-common xfconf \
      libstartup-notification0 \
      libegl-mesa0 libgl1-mesa-dri libgbm1 libglvnd0 libegl1 libgles2 \
      libfmt10 libspdlog1.15 libjsoncpp26 libgtk-layer-shell0 \
      libinih1 libnl-genl-3-200 \
      libgtkmm-3.0-1t64 libglibmm-2.4-1t64 libgiomm-2.4-1t64 \
      libgdkmm-3.0-1t64 libcairomm-1.0-1v5 libpangomm-1.4-1v5 \
      libatkmm-1.6-1v5 libsigc++-2.0-0v5 \
      libdbusmenu-glib4 libdbusmenu-gtk3-4 libplayerctl2 libmpdclient2t64 \
      libpulse0 libpulse-mainloop-glib0 libwireplumber-0.5-0 \
      libnotify4 libhandy-1-0 libgranite6 libgee-0.8-2 \
      libxfce4ui-2-0 libxfce4util7 libxfconf-0-3 libthunarx-3-0 \
      shared-mime-info desktop-file-utils adwaita-icon-theme hicolor-icon-theme
    do
      if ls "${pkg}"_*_amd64.deb "${pkg}"_*_all.deb >/dev/null 2>&1; then
        log_manifest "debian-cache: $pkg"
      else
        apt-get download "$pkg" >/dev/null 2>&1 && log_manifest "debian-download: $pkg" || log_manifest "debian-missing: $pkg"
      fi
    done
  )
  log_manifest "debian-fetch: downloaded requested desktop stack packages"
}

extract_cached_deb_stack() {
  command -v dpkg-deb >/dev/null 2>&1 || {
    log_manifest "debian-cache: dpkg-deb not available"
    return 0
  }
  local extracted=0
  for deb in "$DEB_CACHE"/*.deb; do
    [ -f "$deb" ] || continue
    if [ "$INCLUDE_QT_STACK" != "1" ] && [ "$INCLUDE_QT_STACK" != "true" ]; then
      case "$(basename "$deb")" in
        qt6-*|libqt6*)
          log_manifest "debian-cache-skip: $(basename "$deb")"
          continue
          ;;
      esac
    fi
    dpkg-deb -x "$deb" "$ROOT" >/dev/null 2>&1 || log_manifest "debian-extract-failed: $(basename "$deb")"
    extracted=$((extracted + 1))
  done
  log_manifest "debian-cache: extracted $extracted cached desktop stack packages"
}

repair_cached_thunar_binary() {
  command -v dpkg-deb >/dev/null 2>&1 || return 0
  local deb="" candidate tmp
  for candidate in "$DEB_CACHE"/thunar_*_amd64.deb; do
    [ -f "$candidate" ] || continue
    deb="$candidate"
    break
  done
  [ -n "$deb" ] || return 0

  tmp="$(mktemp -d "${TMPDIR:-/tmp}/ridux-thunar.XXXXXX")" || return 0
  if dpkg-deb -x "$deb" "$tmp" >/dev/null 2>&1 && [ -f "$tmp/usr/bin/thunar" ]; then
    mkdir -p "$ROOT/usr/bin"
    rm -f "$ROOT/usr/bin/thunar" "$ROOT/usr/bin/Thunar" 2>/dev/null || true
    cp -Lf "$tmp/usr/bin/thunar" "$ROOT/usr/bin/thunar"
    chmod 0755 "$ROOT/usr/bin/thunar" 2>/dev/null || true
    copy_deps "$ROOT/usr/bin/thunar"
    materialize_needed_for "$ROOT/usr/bin/thunar"
    log_manifest "repair: materialized /usr/bin/thunar from $(basename "$deb")"
  else
    log_manifest "repair-missing: /usr/bin/thunar in $(basename "$deb")"
  fi
  rm -rf "$tmp"
}

root_has_exec() {
  local label="$1"
  local path="$2"
  if [ -x "$ROOT$path" ]; then
    log_manifest "available: $label -> $path"
  else
    log_manifest "unavailable: $label -> $path"
  fi
}

materialize_locale_data() {
  [ "${RIDUX_MATERIALIZE_UTF8_LOCALE:-0}" = "1" ] ||
    [ "${RIDUX_MATERIALIZE_UTF8_LOCALE:-0}" = "true" ] || {
      rm -rf "$ROOT/usr/lib/locale/C.utf8" "$ROOT/usr/lib/locale/locale-archive" 2>/dev/null || true
      log_manifest "locale: skipped utf8 locale materialization"
      return 0
    }
  mkdir -p "$ROOT/usr/lib/locale" "$ROOT/etc"
  if [ -d /usr/lib/locale/C.utf8 ]; then
    rm -rf "$ROOT/usr/lib/locale/C.utf8"
    cp -a /usr/lib/locale/C.utf8 "$ROOT/usr/lib/locale/"
    log_manifest "locale: copied /usr/lib/locale/C.utf8"
  else
    log_manifest "locale-missing: /usr/lib/locale/C.utf8"
  fi
  if [ -f /usr/lib/locale/locale-archive ]; then
    cp -a /usr/lib/locale/locale-archive "$ROOT/usr/lib/locale/locale-archive"
    log_manifest "locale: copied /usr/lib/locale/locale-archive"
  else
    log_manifest "locale-missing: /usr/lib/locale/locale-archive"
  fi
  cat > "$ROOT/etc/locale.conf" <<'EOF'
LANG=C.UTF-8
LC_ALL=C.UTF-8
EOF
}

download_deb_stack_if_requested
extract_cached_deb_stack
materialize_locale_data

install_cmd waybar
install_cmd wofi
install_cmd foot
install_cmd wlogout
install_cmd wdisplays
install_cmd pipewire
install_cmd wireplumber
install_cmd pipewire-pulse
install_cmd xdg-desktop-portal /usr/libexec/xdg-desktop-portal
install_cmd xdg-desktop-portal-wlr /usr/libexec/xdg-desktop-portal-wlr
install_cmd swaync
install_cmd swaylock
install_cmd grim
install_cmd slurp
install_cmd thunar
repair_cached_thunar_binary

if [ -L "$ROOT/usr/bin/thunar" ] && [ "$(readlink "$ROOT/usr/bin/thunar")" = "thunar" ]; then
  rm -f "$ROOT/usr/bin/thunar"
  log_manifest "repair: removed circular /usr/bin/thunar symlink"
fi

root_has_exec waybar /usr/bin/waybar
root_has_exec wofi /usr/bin/wofi
root_has_exec foot /usr/bin/foot
root_has_exec wlogout /usr/bin/wlogout
root_has_exec wdisplays /usr/bin/wdisplays
root_has_exec pipewire /usr/bin/pipewire
root_has_exec wireplumber /usr/bin/wireplumber
root_has_exec pipewire-pulse /usr/bin/pipewire-pulse
root_has_exec xdg-desktop-portal /usr/libexec/xdg-desktop-portal
root_has_exec xdg-desktop-portal-wlr /usr/libexec/xdg-desktop-portal-wlr
root_has_exec swaync /usr/bin/swaync
root_has_exec swaylock /usr/bin/swaylock
root_has_exec grim /usr/bin/grim
root_has_exec slurp /usr/bin/slurp
root_has_exec thunar /usr/bin/thunar

copy_qt6_plugin() {
  local rel="$1"
  local src="/usr/lib/x86_64-linux-gnu/qt6/plugins/$rel"
  [ -f "$src" ] || {
    log_manifest "qt6-plugin-missing: $rel"
    return 0
  }
  mkdir -p "$ROOT/usr/lib/x86_64-linux-gnu/qt6/plugins/$(dirname "$rel")"
  cp -Lf "$src" "$ROOT/usr/lib/x86_64-linux-gnu/qt6/plugins/$rel" 2>/dev/null || true
  chmod 0644 "$ROOT/usr/lib/x86_64-linux-gnu/qt6/plugins/$rel" 2>/dev/null || true
  copy_deps "$src"
  materialize_needed_for "$src"
  log_manifest "qt6-plugin: $rel"
}

copy_qt6_runtime() {
  if ! pkg-config --exists Qt6Widgets >/dev/null 2>&1; then
    log_manifest "qt6-runtime: Qt6Widgets pkg-config unavailable"
    return 0
  fi

  materialize_needed_for "$ROOT/opt/wayfire/bin/ridux-qt-dashboard"
  materialize_needed_for "$ROOT/opt/wayfire/bin/ridux-qt-files"
  materialize_needed_for "$ROOT/opt/wayfire/bin/ridux-qt-monitor"

  copy_qt6_plugin platforms/libqwayland-egl.so
  copy_qt6_plugin platforms/libqwayland-generic.so
  copy_qt6_plugin wayland-shell-integration/libxdg-shell.so
  copy_qt6_plugin wayland-shell-integration/libwl-shell-plugin.so
  copy_qt6_plugin wayland-shell-integration/libfullscreen-shell-v1.so
  copy_qt6_plugin wayland-decoration-client/libadwaita.so
  copy_qt6_plugin wayland-graphics-integration-client/libqt-plugin-wayland-egl.so
  copy_qt6_plugin wayland-graphics-integration-client/libdmabuf-server.so
  copy_qt6_plugin wayland-graphics-integration-client/libshm-emulation-server.so

  copy_tree_if_exists /usr/share/qt6 "$ROOT/usr/share/qt6"
  log_manifest "qt6-runtime: widgets wayland plugins staged"
}

copy_tree_if_exists /usr/share/pipewire "$ROOT/usr/share/pipewire"
copy_tree_if_exists /usr/share/wireplumber "$ROOT/usr/share/wireplumber"
copy_tree_if_exists /usr/lib/x86_64-linux-gnu/pipewire-0.3 "$ROOT/usr/lib/x86_64-linux-gnu/pipewire-0.3"
copy_tree_if_exists /usr/lib/x86_64-linux-gnu/spa-0.2 "$ROOT/usr/lib/x86_64-linux-gnu/spa-0.2"
copy_tree_if_exists /usr/lib/pipewire-0.3 "$ROOT/usr/lib/pipewire-0.3"
copy_tree_if_exists /usr/lib/spa-0.2 "$ROOT/usr/lib/spa-0.2"
copy_tree_if_exists /usr/share/xdg-desktop-portal "$ROOT/usr/share/xdg-desktop-portal"
copy_tree_if_exists /usr/share/xdg-desktop-portal-wlr "$ROOT/usr/share/xdg-desktop-portal-wlr"
copy_tree_if_exists /usr/share/glvnd "$ROOT/usr/share/glvnd"
copy_tree_if_exists /usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0 "$ROOT/usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0"
copy_tree_if_exists /usr/lib/gdk-pixbuf-2.0 "$ROOT/usr/lib/gdk-pixbuf-2.0"
if [ -d /usr/share/mime ]; then
  mkdir -p "$ROOT/usr/share/mime"
  find /usr/share/mime -maxdepth 1 -type f -exec cp -Lf '{}' "$ROOT/usr/share/mime/" ';'
  log_manifest "mime: copied top-level cache files"
fi
for icon_theme in Adwaita hicolor; do
  if [ -d "/usr/share/icons/$icon_theme" ]; then
    mkdir -p "$ROOT/usr/share/icons/$icon_theme"
    [ ! -f "/usr/share/icons/$icon_theme/index.theme" ] || cp -Lf "/usr/share/icons/$icon_theme/index.theme" "$ROOT/usr/share/icons/$icon_theme/index.theme"
    [ ! -f "/usr/share/icons/$icon_theme/icon-theme.cache" ] || cp -Lf "/usr/share/icons/$icon_theme/icon-theme.cache" "$ROOT/usr/share/icons/$icon_theme/icon-theme.cache"
    [ ! -f "/usr/share/icons/$icon_theme/cursor.theme" ] || cp -Lf "/usr/share/icons/$icon_theme/cursor.theme" "$ROOT/usr/share/icons/$icon_theme/cursor.theme"
    if [ -d "/usr/share/icons/$icon_theme/cursors" ]; then
      mkdir -p "$ROOT/usr/share/icons/$icon_theme/cursors"
      # Dereference symlinks so cursor aliases (left_ptr -> default,
      # arrow -> default, etc.) become real files in the rootfs. Using
      # plain `cp -a` preserved them as symlinks that became 0-byte files
      # once the rootfs was packaged into the initrd, leaving wlroots with
      # broken cursors.
      for cursor_src in "/usr/share/icons/$icon_theme/cursors"/*; do
        [ -e "$cursor_src" ] || continue
        cp -Lf "$cursor_src" "$ROOT/usr/share/icons/$icon_theme/cursors/" 2>/dev/null || true
      done
      chmod 0644 "$ROOT/usr/share/icons/$icon_theme/cursors"/* 2>/dev/null || true
    fi
    log_manifest "icon-theme: copied minimal $icon_theme metadata"
  fi
done
if [ "$INCLUDE_QT_STACK" = "1" ] || [ "$INCLUDE_QT_STACK" = "true" ]; then
  copy_qt6_runtime
else
  log_manifest "qt6-runtime: skipped for original Wayfire/Waybar/wf-shell desktop"
fi
copy_tree_if_exists /usr/lib/x86_64-linux-gnu/gbm "$ROOT/usr/lib/x86_64-linux-gnu/gbm"
copy_tree_if_exists /usr/lib/x86_64-linux-gnu/dri "$ROOT/usr/lib/x86_64-linux-gnu/dri"
copy_glob_materialized /usr/lib/x86_64-linux-gnu/gbm "$ROOT/usr/lib/x86_64-linux-gnu/gbm" '*_gbm.so'
copy_glob_materialized /usr/lib/x86_64-linux-gnu/dri "$ROOT/usr/lib/x86_64-linux-gnu/dri" '*_dri.so'
copy_glob_materialized /usr/lib/x86_64-linux-gnu/dri "$ROOT/usr/lib/x86_64-linux-gnu/dri" '*_drv_video.so'
copy_file_with_deps /usr/lib/x86_64-linux-gnu/libEGL_mesa.so.0 /usr/lib/x86_64-linux-gnu/libEGL_mesa.so.0
copy_file_with_deps /usr/lib/x86_64-linux-gnu/libEGL_mesa.so.0.0.0 /usr/lib/x86_64-linux-gnu/libEGL_mesa.so.0.0.0
copy_file_with_deps /usr/lib/x86_64-linux-gnu/gbm/dri_gbm.so /usr/lib/x86_64-linux-gnu/gbm/dri_gbm.so
if [ -L "$ROOT/usr/lib/x86_64-linux-gnu/gbm/drm_gbm.so" ] ||
   [ ! -f "$ROOT/usr/lib/x86_64-linux-gnu/gbm/drm_gbm.so" ]; then
  rm -f "$ROOT/usr/lib/x86_64-linux-gnu/gbm/drm_gbm.so"
  cp -Lf "$ROOT/usr/lib/x86_64-linux-gnu/gbm/dri_gbm.so" "$ROOT/usr/lib/x86_64-linux-gnu/gbm/drm_gbm.so"
  log_manifest "file: /usr/lib/x86_64-linux-gnu/gbm/drm_gbm.so materialized from dri_gbm.so"
fi
if [ -d /usr/lib/x86_64-linux-gnu/gbm ]; then
  find /usr/lib/x86_64-linux-gnu/gbm -maxdepth 1 -type f -name '*_gbm.so' -print0 |
    while IFS= read -r -d '' gbm_module; do
      copy_deps "$gbm_module"
    done
fi
if [ -d /usr/lib/x86_64-linux-gnu/dri ]; then
  find /usr/lib/x86_64-linux-gnu/dri -maxdepth 1 -type f -name '*_dri.so' -print0 |
    while IFS= read -r -d '' dri_module; do
      copy_deps "$dri_module"
    done
fi

for soname in \
  libspdlog.so.1.15 \
  libnl-genl-3.so.200 \
  libplayerctl.so.2 \
  libwireplumber-0.5.so.0 \
  libmpdclient.so.2 \
  libstartup-notification-1.so.0 \
  libinih.so.1; do
  materialize_soname "$soname"
done

materialize_needed_for "$ROOT/usr/bin/waybar"
materialize_needed_for "$ROOT/usr/bin/wofi"
materialize_needed_for "$ROOT/usr/bin/foot"
materialize_needed_for "$ROOT/usr/bin/wlogout"
materialize_needed_for "$ROOT/usr/bin/wdisplays"
materialize_needed_for "$ROOT/usr/bin/wireplumber"
materialize_needed_for "$ROOT/usr/libexec/xdg-desktop-portal-wlr"

if [ ! -e "$ROOT/usr/lib/x86_64-linux-gnu/libnl-genl-3.so.200" ] &&
   [ -f tools/wayfire/ridux_libnl_genl_stub.c ]; then
  gcc tools/wayfire/ridux_libnl_genl_stub.c -shared -fPIC -O2 -Wall -Wextra \
    -Wl,-soname,libnl-genl-3.so.200 \
    -o "$ROOT/usr/lib/x86_64-linux-gnu/libnl-genl-3.so.200"
  cp -Lf "$ROOT/usr/lib/x86_64-linux-gnu/libnl-genl-3.so.200" "$ROOT/lib64/libnl-genl-3.so.200"
  log_manifest "soname: libnl-genl-3.so.200 <- ridux stub"
fi

cat > "$ROOT/usr/bin/ridux-open-launcher" <<'EOF'
#!/bin/sh
if [ -x /usr/bin/wofi ]; then
  exec /usr/bin/wofi --conf /etc/xdg/wofi/config --style /etc/xdg/wofi/style.css --show drun --allow-images --insensitive
fi
exec /opt/wayfire/bin/ridux-launcher
EOF

cat > "$ROOT/usr/bin/ridux-open-files" <<'EOF'
#!/bin/sh
if [ -x /usr/bin/thunar ]; then
  exec /usr/bin/thunar /home
fi
exec /usr/bin/wofi --show drun 2>/dev/null || exit 0
EOF

cat > "$ROOT/usr/bin/ridux-terminal" <<'EOF'
#!/bin/sh
export TERM="${TERM:-xterm-256color}"
if [ -x /usr/bin/foot ]; then
  if [ "$#" -gt 0 ]; then
    exec /usr/bin/foot --working-directory=/home "$@"
  fi
  exec /usr/bin/foot --working-directory=/home /bin/sh
fi
if [ -x /opt/wayfire/bin/ridux-terminal ]; then
  exec /opt/wayfire/bin/ridux-terminal
fi
exec /bin/sh
EOF

cat > "$ROOT/usr/bin/ridux-power-menu" <<'EOF'
#!/bin/sh
if [ -x /usr/bin/wlogout ]; then
  exec /usr/bin/wlogout --layout /etc/wlogout/layout --css /etc/wlogout/style.css
fi
if [ -x /opt/wayfire/bin/wayland-logout ]; then
  exec /opt/wayfire/bin/wayland-logout
fi
exit 0
EOF

cat > "$ROOT/usr/bin/ridux-display-settings" <<'EOF'
#!/bin/sh
if [ -x /usr/bin/wdisplays ]; then
  exec /usr/bin/wdisplays
fi
exec /usr/bin/wofi --show drun 2>/dev/null || exit 0
EOF

cat > "$ROOT/usr/bin/ridux-screenshot" <<'EOF'
#!/bin/sh
out="/home/ridux-screenshot.png"
if [ -x /usr/bin/grim ] && [ -x /usr/bin/slurp ]; then
  exec sh -c '/usr/bin/grim -g "$(/usr/bin/slurp)" "$0"' "$out"
fi
if [ -x /usr/bin/grim ]; then
  exec /usr/bin/grim "$out"
fi
echo "grim/slurp are not installed in this Ridux image." >&2
exit 1
EOF

cat > "$ROOT/usr/bin/ridux-lock" <<'EOF'
#!/bin/sh
if [ -x /usr/bin/swaylock ]; then
  exec /usr/bin/swaylock -f -c 071427
fi
echo "swaylock is not installed in this Ridux image." >&2
exit 1
EOF
chmod 0755 \
  "$ROOT/usr/bin/ridux-open-launcher" \
  "$ROOT/usr/bin/ridux-open-files" \
  "$ROOT/usr/bin/ridux-terminal" \
  "$ROOT/usr/bin/ridux-power-menu" \
  "$ROOT/usr/bin/ridux-display-settings" \
  "$ROOT/usr/bin/ridux-screenshot" \
  "$ROOT/usr/bin/ridux-lock"

cat > "$ROOT/etc/xdg/waybar/config" <<'EOF'
{
  "layer": "top",
  "position": "top",
  "height": 48,
  "exclusive": true,
  "passthrough": false,
  "reload_style_on_change": true,
  "name": "waybar",
  "spacing": 8,
  "margin-top": 6,
  "margin-left": 8,
  "margin-right": 8,
  "modules-left": ["custom/launcher", "custom/files", "custom/terminal", "custom/displays"],
  "modules-center": ["custom/title"],
  "modules-right": ["custom/audio", "custom/screenshot", "custom/lock", "custom/power"],
  "custom/launcher": {
    "format": "Apps",
    "tooltip": false,
    "on-click": "/usr/bin/ridux-open-launcher"
  },
  "custom/files": {
    "format": "Files",
    "tooltip": false,
    "on-click": "/usr/bin/ridux-open-files"
  },
  "custom/terminal": {
    "format": "Term",
    "tooltip": false,
    "on-click": "/usr/bin/ridux-terminal"
  },
  "custom/displays": {
    "format": "Display",
    "tooltip": false,
    "on-click": "/usr/bin/ridux-display-settings"
  },
  "custom/title": {
    "format": "Wayfire",
    "tooltip": false
  },
  "custom/audio": {
    "format": "Audio",
    "tooltip": false,
    "on-click": "/usr/bin/ridux-terminal /bin/sh -lc 'wpctl status; printf \"\\npress enter...\"; read x'"
  },
  "custom/screenshot": {
    "format": "Shot",
    "tooltip": false,
    "on-click": "/usr/bin/ridux-screenshot"
  },
  "custom/lock": {
    "format": "Lock",
    "tooltip": false,
    "on-click": "/usr/bin/ridux-lock"
  },
  "custom/power": {
    "format": "Power",
    "tooltip": false,
    "on-click": "/usr/bin/ridux-power-menu"
  },
  "clock": {
    "format": "{:%H:%M}",
    "tooltip": false
  }
}
EOF

cat > "$ROOT/etc/xdg/waybar/style.css" <<'EOF'
* {
  border: 0;
  font-family: "DejaVu Sans", "Cantarell", sans-serif;
  font-size: 13px;
  font-weight: 400;
  min-height: 0;
}

window#waybar {
  color: #f7fbff;
  background: rgba(15, 66, 116, 0.96);
  border: 1px solid rgba(124, 217, 255, 0.42);
  border-radius: 14px;
  box-shadow: 0 12px 28px rgba(0, 0, 0, 0.34);
}

#custom-launcher,
#custom-files,
#custom-terminal,
#custom-displays,
#custom-audio,
#custom-title,
#clock,
#custom-screenshot,
#custom-lock,
#custom-power {
  margin: 7px 2px;
  padding: 0 14px;
  min-height: 32px;
  border-radius: 10px;
  border: 1px solid rgba(255, 255, 255, 0.12);
  background: rgba(255, 255, 255, 0.10);
  box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.14);
}

#custom-launcher {
  color: #ffffff;
  background: rgba(31, 152, 255, 0.74);
  border-color: rgba(168, 230, 255, 0.62);
}

#custom-title,
#clock {
  font-weight: 400;
  color: #ffffff;
  background: rgba(5, 34, 72, 0.36);
}

#custom-files,
#custom-terminal,
#custom-displays,
#custom-audio {
  color: #eef7ff;
}

#custom-screenshot,
#custom-lock,
#custom-power {
  color: #e2f4ff;
}
EOF

cat > "$ROOT/etc/xdg/wofi/config" <<'EOF'
show=drun
width=620
height=460
location=center
allow_images=true
insensitive=true
prompt=Search Ridux
term=/usr/bin/ridux-terminal
EOF

cat > "$ROOT/etc/xdg/wofi/style.css" <<'EOF'
window {
  margin: 0;
  border: 1px solid rgba(255, 255, 255, 0.18);
  border-radius: 22px;
  background-color: rgba(10, 17, 31, 0.94);
  color: #eef6ff;
}

#input {
  margin: 18px;
  padding: 12px 14px;
  border: 1px solid rgba(255, 255, 255, 0.14);
  border-radius: 16px;
  background-color: rgba(255, 255, 255, 0.10);
  color: #eef6ff;
}

#entry {
  margin: 4px 14px;
  padding: 10px;
  border-radius: 14px;
}

#entry:selected {
  background-color: rgba(28, 139, 255, 0.34);
}
EOF

cat > "$ROOT/etc/xdg/swaync/config.json" <<'EOF'
{
  "$schema": "/etc/xdg/swaync/configSchema.json",
  "positionX": "right",
  "positionY": "top",
  "control-center-margin-top": 48,
  "control-center-margin-right": 16,
  "notification-window-width": 380,
  "timeout": 6,
  "timeout-low": 3,
  "timeout-critical": 0,
  "widgets": ["title", "notifications", "mpris", "buttons-grid"],
  "widget-config": {
    "title": { "text": "Ridux Notifications", "clear-all-button": true },
    "buttons-grid": {
      "actions": [
        { "label": "Lock", "command": "/usr/bin/ridux-lock" },
        { "label": "Shot", "command": "/usr/bin/ridux-screenshot" }
      ]
    }
  }
}
EOF

cat > "$ROOT/etc/xdg/swaync/style.css" <<'EOF'
.control-center,
.notification-row .notification-background {
  background: rgba(10, 17, 31, 0.94);
  border: 1px solid rgba(255, 255, 255, 0.16);
  border-radius: 18px;
  color: #eef6ff;
}

.notification {
  background: transparent;
  box-shadow: none;
}
EOF

cat > "$ROOT/etc/wlogout/layout" <<'EOF'
{
  "label" : "shutdown",
  "action" : "echo poweroff >/tmp/ridux-power-action",
  "text" : "Power Off",
  "keybind" : "s"
}
{
  "label" : "reboot",
  "action" : "echo reboot >/tmp/ridux-power-action",
  "text" : "Reboot",
  "keybind" : "r"
}
{
  "label" : "lock",
  "action" : "/usr/bin/ridux-lock",
  "text" : "Lock",
  "keybind" : "l"
}
EOF

cat > "$ROOT/etc/wlogout/style.css" <<'EOF'
window {
  background-color: rgba(7, 12, 22, 0.84);
}

button {
  margin: 12px;
  padding: 18px;
  border-radius: 18px;
  color: #eef6ff;
  background-color: rgba(10, 17, 31, 0.94);
  border: 1px solid rgba(255, 255, 255, 0.16);
}

button:hover {
  background-color: rgba(28, 139, 255, 0.34);
}
EOF

cat > "$ROOT/etc/xdg/xdg-desktop-portal/portals.conf" <<'EOF'
[preferred]
default=wlr;gtk
org.freedesktop.impl.portal.Screenshot=wlr
org.freedesktop.impl.portal.ScreenCast=wlr
EOF

cat > "$ROOT/etc/xdg/xdg-desktop-portal-wlr/config" <<'EOF'
[screencast]
output_name=
max_fps=30
chooser_type=simple
chooser_cmd=slurp -f %o -or
EOF

cat > "$ROOT/etc/pipewire/pipewire.conf" <<'EOF'
context.properties = {
    core.daemon = true
    core.name = pipewire-0
    link.max-buffers = 16
    support.dbus = false
    module.rt = false
    module.portal = false
    module.session-manager = false
    default.clock.rate = 48000
    default.clock.allowed-rates = [ 44100 48000 ]
    default.clock.quantum = 1024
    default.clock.min-quantum = 1024
}

context.spa-libs = {
    audio.convert.* = audioconvert/libspa-audioconvert
    support.* = support/libspa-support
}

context.modules = [
    { name = libpipewire-module-protocol-native
        args = {
            sockets = [
                { name = "pipewire-0" }
                { name = "pipewire-0-manager" }
            ]
        }
    }
    { name = libpipewire-module-metadata }
    { name = libpipewire-module-spa-device-factory }
    { name = libpipewire-module-spa-node-factory }
    { name = libpipewire-module-client-node }
    { name = libpipewire-module-client-device }
    { name = libpipewire-module-access }
    { name = libpipewire-module-adapter }
    { name = libpipewire-module-link-factory }
]
EOF

cat > "$ROOT/etc/pipewire/pipewire-pulse.conf" <<'EOF'
context.properties = {
    link.max-buffers = 16
    support.dbus = false
    module.rt = false
}

context.spa-libs = {
    audio.convert.* = audioconvert/libspa-audioconvert
    support.* = support/libspa-support
}

context.modules = [
    { name = libpipewire-module-protocol-native }
    { name = libpipewire-module-client-node }
    { name = libpipewire-module-adapter }
    { name = libpipewire-module-metadata }
    { name = libpipewire-module-protocol-pulse
        args = {
            server.address = [ "unix:native" ]
        }
    }
]

pulse.properties = {
    pulse.min.req = 32/48000
    pulse.default.req = 1024/48000
    pulse.default.frag = 96000/48000
}
EOF

cat > "$ROOT/usr/share/wayland-sessions/ridux-wayfire.desktop" <<'EOF'
[Desktop Entry]
Name=Ridux Wayfire
Comment=RiduxOS Wayland session
Exec=/opt/wayfire/bin/wayfire
Type=Application
DesktopNames=Wayfire;Ridux
EOF

log_manifest "config: waybar wofi foot wlogout wdisplays pipewire portal swaync wrappers"
printf 'Wayland desktop stack packaged. Missing binaries use Ridux fallbacks.\n' >> "$MANIFEST"
