#!/usr/bin/env bash
set -euo pipefail

ROOTFS_DIR="${1:-rootfs}"
INSTALL_DIR="${2:-${RIDUX_WAYFIRE_INSTALL:-third_party/wayfire/install}}"

if [[ ! -d "$ROOTFS_DIR" ]]; then
  echo "[wayfire-rootfs] rootfs dir not found: $ROOTFS_DIR" >&2
  exit 1
fi

if [[ ! -d "$INSTALL_DIR" ]]; then
  echo "[wayfire-rootfs] Wayfire install prefix not found: $INSTALL_DIR" >&2
  echo "[wayfire-rootfs] Build it first with: make wayfire-build" >&2
  exit 2
fi

if ! command -v readelf >/dev/null 2>&1; then
  echo "[wayfire-rootfs] readelf is required (binutils)." >&2
  exit 3
fi

if ! command -v ldd >/dev/null 2>&1; then
  echo "[wayfire-rootfs] ldd is required." >&2
  exit 4
fi

ROOT_ABS="$(cd "$ROOTFS_DIR" && pwd -P)"
INSTALL_ABS="$(cd "$INSTALL_DIR" && pwd -P)"
DEST="$ROOT_ABS/opt/wayfire"
MANIFEST="$DEST/SOURCE-MANIFEST.txt"

case "$DEST" in
  "$ROOT_ABS"/opt/wayfire) ;;
  *)
    echo "[wayfire-rootfs] refusing unsafe destination: $DEST" >&2
    exit 5
    ;;
esac

is_elf() {
  [[ -f "$1" ]] && readelf -h "$1" >/dev/null 2>&1
}

require_main_binary() {
  local bin
  for bin in \
    "$INSTALL_ABS/bin/wayfire" \
    "$INSTALL_ABS/bin/wf-shell" \
    "$INSTALL_ABS/bin/wcm"
  do
    if is_elf "$bin"; then
      return 0
    fi
  done
  echo "[wayfire-rootfs] no Wayfire ELF binaries found in $INSTALL_ABS/bin." >&2
  exit 6
}

copy_abs_into_rootfs() {
  local src="$1"
  local dst="$ROOT_ABS$src"
  local soname soname_dst
  [[ -e "$src" ]] || return 0
  mkdir -p "$(dirname "$dst")"
  cp -L "$src" "$dst"
  soname="$({ readelf -d "$src" 2>/dev/null || true; } | awk -F'[][]' '/SONAME/ {print $2; exit}')"
  if [[ -n "$soname" && "$soname" != "$(basename "$dst")" ]]; then
    soname_dst="$(dirname "$dst")/$soname"
    cp -L "$src" "$soname_dst"
  fi
}

declare -A SEEN
QUEUE=()
COPIED_DEPS=()

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

require_main_binary

echo "[wayfire-rootfs] staging $INSTALL_ABS -> $DEST"
rm -rf "$DEST"
mkdir -p "$DEST"
cp -aL "$INSTALL_ABS"/. "$DEST"/

while IFS= read -r -d '' f; do
  is_elf "$f" || continue
  scan_elf_deps "$f"
done < <(find "$INSTALL_ABS" -type f -print0)

while [[ "${#QUEUE[@]}" -gt 0 ]]; do
  dep="${QUEUE[0]}"
  QUEUE=("${QUEUE[@]:1}")
  [[ -z "${SEEN[$dep]:-}" ]] || continue
  SEEN["$dep"]=1
  copy_abs_into_rootfs "$dep"
  COPIED_DEPS+=("$dep")
  is_elf "$dep" && scan_elf_deps "$dep"
done

mkdir -p "$ROOT_ABS/etc/wayfire" "$ROOT_ABS/tmp/wayfire-home/config" "$DEST"
cat > "$ROOT_ABS/etc/wayfire/wayfire.ini" <<'EOF'
[core]
plugins = autostart command decoration move place resize switcher vswitch wm-actions
xwayland = false
preferred_decoration_mode = server

[autostart]
shell = /opt/wayfire/bin/wf-shell

[output:*]
mode = auto
position = 0,0
transform = normal
scale = 1.000000

[input]
xkb_layout = us
mouse_cursor_speed = 0.000000
touchpad_cursor_speed = 0.000000

[decoration]
font = Ridux Sans
title_height = 32
border_size = 1
active_color = \#172033ee
inactive_color = \#111827cc

[command]
binding_terminal = <super> KEY_ENTER
command_terminal = /opt/wayfire/bin/ridux-terminal
binding_launcher = <super> KEY_SPACE
command_launcher = /usr/bin/ridux-open-launcher
binding_lock = <super> KEY_ESC
command_lock = /usr/bin/ridux-lock
binding_screenshot = KEY_PRINT
command_screenshot = /usr/bin/ridux-screenshot
binding_screenshot_interactive = <shift> KEY_PRINT
command_screenshot_interactive = /usr/bin/ridux-screenshot
EOF

cat > "$ROOT_ABS/etc/wayfire/wf-shell.ini" <<'EOF'
[panel]
position = bottom
autohide = false
minimal_height = 44
widgets_left = menu launchers window-list
widgets_center = clock
widgets_right = network battery volume tray
launcher_cmd_1 = /opt/wayfire/bin/thunar
launcher_label_1 = Files
launcher_cmd_2 = /usr/bin/ridux-open-launcher
launcher_label_2 = Apps
launcher_cmd_3 = /usr/bin/ridux-screenshot
launcher_label_3 = Screenshot
launcher_cmd_4 = /usr/bin/ridux-lock
launcher_label_4 = Lock

[dock]
position = bottom
autohide = false
dock_height = 72
icon_height = 48
css_path = /tmp/wayfire-home/config/wf-shell/css/ridux-shell.css
icon_mapping_org.xfce.thunar = /opt/wayfire/share/wayfire/icons/ridux-files.png
icon_mapping_thunar = /opt/wayfire/share/wayfire/icons/ridux-files.png
icon_mapping_ridux-terminal = /opt/wayfire/share/wayfire/icons/ridux-terminal.png
icon_mapping_ridux-settings = /opt/wayfire/share/wayfire/icons/ridux-settings.png
icon_mapping_ridux-launcher = /opt/wayfire/share/wayfire/icons/ridux-logo.png

[background]
cycle_timeout = 0
image = /ridux/wallpaper.png
preserve_aspect = true

[launcher]
terminal = /bin/terminal-r3.elf
EOF

cp "$ROOT_ABS/etc/wayfire/wayfire.ini" "$ROOT_ABS/tmp/wayfire-home/config/wayfire.ini"
cp "$ROOT_ABS/etc/wayfire/wf-shell.ini" "$ROOT_ABS/tmp/wayfire-home/config/wf-shell.ini"

if [[ -f "$(dirname "$INSTALL_ABS")/source-manifest.txt" ]]; then
  cp -L "$(dirname "$INSTALL_ABS")/source-manifest.txt" "$DEST/WAYFIREWM-SOURCES.txt"
fi

{
  echo "source_prefix=$INSTALL_ABS"
  echo "rootfs_dest=/opt/wayfire"
  echo "timestamp_utc=$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
  echo "copied_system_deps=${#COPIED_DEPS[@]}"
  printf '%s\n' "${COPIED_DEPS[@]}" | sort -u
} > "$MANIFEST"

echo "[wayfire-rootfs] done"
echo "  dest: $DEST"
echo "  copied system deps: ${#COPIED_DEPS[@]}"
echo "  manifest: $MANIFEST"
