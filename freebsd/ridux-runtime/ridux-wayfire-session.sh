#!/bin/sh
# Ridux FreeBSD Wayfire session.
#
# This is the stable desktop entrypoint for the FreeBSD-kernel pivot:
# Wayfire first, Pixman/software rendering first, clear logs and fallbacks.

set -eu

LOG="${RIDUX_WAYFIRE_LOG:-/var/log/ridux-wayfire-session.log}"
exec 3>&1 4>&2
mkdir -p "$(dirname "$LOG")" 2>/dev/null || true
touch "$LOG" 2>/dev/null || LOG="/tmp/ridux-wayfire-session.log"
exec >>"$LOG" 2>&1

echo "[ridux-wayfire] starting at $(date)"
echo "[ridux-wayfire] uname: $(uname -a 2>/dev/null || true)"

UID_NOW="$(id -u 2>/dev/null || echo 0)"
export USER="${USER:-root}"
export HOME="${HOME:-/tmp/ridux-root}"
export SHELL="${SHELL:-/bin/sh}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/ridux-runtime-${UID_NOW}}"
export XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-${HOME}/.config}"
export XDG_CACHE_HOME="${XDG_CACHE_HOME:-${HOME}/.cache}"
export XDG_DATA_HOME="${XDG_DATA_HOME:-${HOME}/.local/share}"
export XDG_STATE_HOME="${XDG_STATE_HOME:-${HOME}/.local/state}"
export XDG_CURRENT_DESKTOP="Wayfire"
export DESKTOP_SESSION="wayfire"
export XCURSOR_THEME="${XCURSOR_THEME:-Adwaita}"
export XCURSOR_SIZE="${XCURSOR_SIZE:-24}"

# Stable first: no Mesa dependency is required for the first usable desktop.
# The GPU path can later remove/override these variables.
export WLR_RENDERER="${WLR_RENDERER:-pixman}"
export WLR_NO_HARDWARE_CURSORS="${WLR_NO_HARDWARE_CURSORS:-1}"
export WLR_DRM_NO_MODIFIERS="${WLR_DRM_NO_MODIFIERS:-1}"
export LIBGL_ALWAYS_SOFTWARE="${LIBGL_ALWAYS_SOFTWARE:-1}"
export QT_OPENGL="${QT_OPENGL:-software}"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-wayland;xcb}"
export GDK_BACKEND="${GDK_BACKEND:-wayland,x11}"
export MOZ_ENABLE_WAYLAND="${MOZ_ENABLE_WAYLAND:-1}"
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-wayland}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/usr/sbin:/bin:/sbin:${PATH:-}"

mkdir -p "$HOME" "$XDG_RUNTIME_DIR" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" \
    "$XDG_DATA_HOME" "$XDG_STATE_HOME"
chmod 0700 "$XDG_RUNTIME_DIR" 2>/dev/null || true

service dbus onestart >/dev/null 2>&1 || true
service seatd onestart >/dev/null 2>&1 || true

for mod in linux linux64 cuse evdev uhid drm virtio_gpu i915kms amdgpu radeonkms; do
    kldstat -n "$mod" >/dev/null 2>&1 || kldload "$mod" >/dev/null 2>&1 || true
done

echo "[ridux-wayfire] renderer: $WLR_RENDERER"
echo "[ridux-wayfire] runtime:  $XDG_RUNTIME_DIR"
echo "[ridux-wayfire] /dev/dri:"
ls -la /dev/dri 2>/dev/null || true
echo "[ridux-wayfire] /dev/input:"
ls -la /dev/input 2>/dev/null || true
echo "[ridux-wayfire] loaded drm-ish modules:"
kldstat 2>/dev/null | grep -Ei 'drm|kms|gpu|vbox|virtio|linux' || true

WF_CONFIG="${XDG_CONFIG_HOME}/wayfire.ini"
WF_SHELL_CONFIG="${XDG_CONFIG_HOME}/wf-shell.ini"
WAYBAR_DIR="${XDG_CONFIG_HOME}/waybar"
mkdir -p "$WAYBAR_DIR"

cat > "$WF_CONFIG" <<'EOF'
[core]
plugins = autostart command move resize place switcher vswitch wm-actions decoration
xwayland = true
preferred_decoration_mode = server
focus_button_with_modifiers = false
vwidth = 2
vheight = 2

[output:*]
mode = preferred
scale = 1
transform = normal

[autostart]
background = sh -c 'command -v wf-background >/dev/null 2>&1 && exec wf-background -c "$XDG_CONFIG_HOME/wf-shell.ini"'
panel = sh -c 'command -v wf-panel >/dev/null 2>&1 && exec wf-panel -c "$XDG_CONFIG_HOME/wf-shell.ini"'
dock = sh -c 'command -v wf-dock >/dev/null 2>&1 && exec wf-dock -c "$XDG_CONFIG_HOME/wf-shell.ini"'
waybar = sh -c 'command -v waybar >/dev/null 2>&1 && exec waybar'
terminal = sh -c 'sleep 2; command -v foot >/dev/null 2>&1 && exec foot'
files = sh -c 'sleep 3; command -v thunar >/dev/null 2>&1 && exec thunar'

[command]
binding_terminal = <super> KEY_ENTER
command_terminal = foot
binding_launcher = <super> KEY_D
command_launcher = sh -c 'command -v wofi >/dev/null 2>&1 && exec wofi --show drun || command -v foot >/dev/null 2>&1 && exec foot'
binding_files = <super> KEY_E
command_files = thunar
binding_close = <super> KEY_Q
command_close = wclose
EOF

cat > "$WF_SHELL_CONFIG" <<'EOF'
[background]
color = \#70747a
image =
preserve_aspect = false

[panel]
position = top
autohide = false
height = 32
widgets_left = menu launchers
widgets_center = window-list
widgets_right = tray clock

[dock]
position = bottom
autohide = false
icon_size = 48
launchers = thunar foot
EOF

cat > "$WAYBAR_DIR/config" <<'EOF'
{
  "layer": "top",
  "position": "top",
  "height": 32,
  "modules-left": ["custom/ridux", "wlr/taskbar"],
  "modules-right": ["pulseaudio", "network", "clock"],
  "custom/ridux": {
    "format": "Ridux",
    "on-click": "foot"
  },
  "clock": {
    "format": "{:%H:%M}"
  }
}
EOF

cat > "$WAYBAR_DIR/style.css" <<'EOF'
* {
  border: 0;
  border-radius: 0;
  font-family: DejaVu Sans, sans-serif;
  font-size: 13px;
  min-height: 0;
}

window#waybar {
  background: rgba(36, 40, 45, 0.94);
  color: #f4f7fb;
}

#custom-ridux,
#taskbar,
#pulseaudio,
#network,
#clock {
  padding: 0 10px;
}

#custom-ridux {
  background: #d7ff5c;
  color: #111418;
  font-weight: 700;
}
EOF

if command -v wayfire >/dev/null 2>&1; then
    echo "[ridux-wayfire] launching wayfire: $(command -v wayfire)"
    set +e
    wayfire -c "$WF_CONFIG"
    rc=$?
    set -e
    echo "[ridux-wayfire] wayfire exited with status $rc"
else
    echo "[ridux-wayfire] wayfire binary not found"
    rc=127
fi

if [ "${RIDUX_WAYFIRE_BACKGROUND:-0}" = "1" ]; then
    exit "$rc"
fi

echo "[ridux-wayfire] falling back to rescue desktop"
exec 1>&3 2>&4
if [ -x /usr/local/bin/ridux-desktop ]; then
    exec env TERM="${TERM:-xterm}" /usr/local/bin/ridux-desktop
fi
if [ -x /usr/local/bin/ridux-shell ]; then
    exec env TERM="${TERM:-xterm}" /usr/local/bin/ridux-shell
fi
exec /bin/sh
