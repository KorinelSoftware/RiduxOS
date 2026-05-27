#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "base.h"
#include "memory_tasks.h"
#include "linux_syscalls.h"
#include "user_libc.h"
#include "linux_app_profiles.h"

extern bool kvfs_exists(const char *path);
extern bool drm_vbox_gpu_detected(void);

static char lap_lower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static bool lap_streq_ci(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (lap_lower(*a) != lap_lower(*b)) return false;
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static bool lap_starts_ci(const char *s, const char *prefix) {
    if (!s || !prefix) return false;
    while (*prefix) {
        if (lap_lower(*s) != lap_lower(*prefix)) return false;
        ++s;
        ++prefix;
    }
    return true;
}

static bool lap_has_token_ci(const char *s, const char *needle) {
    size_t n;
    if (!s || !needle) return false;
    n = ulibc_strlen(needle);
    if (!n) return false;
    while (*s) {
        size_t i;
        for (i = 0; i < n; ++i) {
            if (!s[i]) return false;
            if (lap_lower(s[i]) != lap_lower(needle[i])) break;
        }
        if (i == n) return true;
        ++s;
    }
    return false;
}

static bool lap_arg_has_scheme(const char *s) {
    const char *p = s;
    if (!s) return false;
    while (*p) {
        if (*p == ':') return true;
        if (*p == '/' || ulibc_isspace((unsigned char)*p)) return false;
        ++p;
    }
    return false;
}

static bool lap_arg_looks_like_host(const char *s) {
    bool dot = false;
    if (!s || !*s || *s == '-' || *s == '/' || lap_arg_has_scheme(s)) return false;
    while (*s) {
        if (*s == '.') dot = true;
        if (ulibc_isspace((unsigned char)*s)) return false;
        ++s;
    }
    return dot;
}

static void lap_push(char **items, int *count, int cap, const char *value) {
    if (!items || !count || !value) return;
    if (*count >= cap - 1) return;
    items[(*count)++] = (char *)value;
}

static bool lap_prefers_wayland_session(ridux_linux_app_kind_t kind) {
    return kind == RIDUX_LINUX_APP_RIDUX_SHELL ||
           kind == RIDUX_LINUX_APP_KDE ||
           kind == RIDUX_LINUX_APP_WAYFIRE ||
           kind == RIDUX_LINUX_APP_HYPRLAND ||
           kind == RIDUX_LINUX_APP_GUI ||
           kind == RIDUX_LINUX_APP_FIREFOX ||
           kind == RIDUX_LINUX_APP_CHROMIUM;
}

static bool lap_wayfire_pixman_enabled(void) {
    return kvfs_exists("/etc/ridux-wayfire-pixman.enable");
}

static bool lap_wayfire_gpu_enabled(void) {
    return !lap_wayfire_pixman_enabled();
}

static bool lap_wayfire_hw_cursor_enabled(void) {
    return kvfs_exists("/etc/ridux-wayfire-hw-cursor.enable") ||
           kvfs_exists("/etc/ridux-ui-hw-cursor-only.enable");
}

static bool lap_mesa_shader_cache_disabled(void) {
    return kvfs_exists("/etc/ridux-wayfire-disable-shader-cache.enable") ||
           kvfs_exists("/etc/ridux-mesa-disable-shader-cache.enable");
}

static void lap_push_mesa_cache_env(char **env, int *envc, int env_cap) {
    bool disable = lap_mesa_shader_cache_disabled();
    if (disable) {
        lap_push(env, envc, env_cap, "MESA_SHADER_CACHE_DISABLE=1");
        lap_push(env, envc, env_cap, "MESA_GLSL_CACHE_DISABLE=1");
        lap_push(env, envc, env_cap, "MESA_SHADER_CACHE_DIR=/tmp/ridux-mesa-cache-disabled");
        lap_push(env, envc, env_cap, "MESA_GLSL_CACHE_DIR=/tmp/ridux-mesa-cache-disabled");
    } else {
        lap_push(env, envc, env_cap, "MESA_SHADER_CACHE_DIR=/tmp/mesa-shader-cache");
        lap_push(env, envc, env_cap, "MESA_SHADER_CACHE_DISABLE=0");
    }
    lap_push(env, envc, env_cap, "MESA_DISK_CACHE_SINGLE_FILE=0");
    lap_push(env, envc, env_cap, "MESA_DISK_CACHE_MULTI_FILE=0");
}

static bool lap_wayfire_vbox_gpu_enabled(void) {
    return kvfs_exists("/etc/ridux-vbox-gpu.enable") ||
           (kvfs_exists("/etc/ridux-wayfire-vbox-gpu.enable") &&
            drm_vbox_gpu_detected());
}

static bool lap_wayfire_gles2_software_enabled(void) {
    return kvfs_exists("/etc/ridux-wayfire-gles2-software.enable");
}

static bool lap_physical_gpu_preferred(void) {
    return kvfs_exists("/etc/ridux-physical-gpu-preferred.enable");
}

static bool lap_physical_gpu_forced(void) {
    return kvfs_exists("/etc/ridux-physical-gpu-force.enable") ||
           kvfs_exists("/etc/ridux-gpu-physical.force") ||
           kvfs_exists("/etc/ridux-gpu-iris.force") ||
           kvfs_exists("/etc/ridux-gpu-force-physical.enable");
}

static bool lap_plasma_gpu_required(void) {
    return kvfs_exists("/etc/ridux-plasma-gpu.required");
}

static bool lap_plasma_vbox_gpu_enabled(void) {
    return kvfs_exists("/etc/ridux-vbox-gpu.enable") ||
           (kvfs_exists("/etc/ridux-plasma-vbox-gpu.enable") &&
            drm_vbox_gpu_detected());
}

static bool lap_ridux_vbox_gpu_enabled(void) {
    return kvfs_exists("/etc/ridux-vbox-gpu.enable") ||
           drm_vbox_gpu_detected();
}

static bool lap_ridux_virtio_gpu_enabled(void) {
    return kvfs_exists("/etc/ridux-virtio-gpu.enable") ||
           kvfs_exists("/etc/ridux-hyprland-virtio-gpu.enable") ||
           kvfs_exists("/etc/ridux-wayfire-virtio-gpu.enable") ||
           kvfs_exists("/etc/ridux-plasma-virtio-gpu.enable");
}

static bool lap_hyprland_primary_enabled(void) {
    return kvfs_exists("/etc/ridux-hyprland-primary.enable") ||
           kvfs_exists("/etc/ridux-hyprland-gpu.enable");
}

static bool lap_extra_has(char **extra_args, int extra_argc, const char *needle) {
    int i;
    if (!extra_args || !needle) return false;
    for (i = 0; i < extra_argc; ++i) {
        if (extra_args[i] && lap_streq_ci(extra_args[i], needle)) return true;
    }
    return false;
}

static const char *lap_physical_mesa_driver_override(void) {
    if (kvfs_exists("/etc/ridux-gpu-amd.enable")) return "radeonsi";
    if (kvfs_exists("/etc/ridux-gpu-intel.enable")) return "iris";
    if (kvfs_exists("/etc/ridux-gpu-nouveau.enable")) return "nouveau";
    return NULL;
}

static bool lap_push_physical_mesa_env(char **env, int *envc, int env_cap,
                                       const char *driver) {
    if (!driver) return false;
    if (lap_streq_ci(driver, "radeonsi")) {
        lap_push(env, envc, env_cap, "MESA_LOADER_DRIVER_OVERRIDE=radeonsi");
        lap_push(env, envc, env_cap, "GALLIUM_DRIVER=radeonsi");
        return true;
    }
    if (lap_streq_ci(driver, "iris")) {
        lap_push(env, envc, env_cap, "MESA_LOADER_DRIVER_OVERRIDE=iris");
        lap_push(env, envc, env_cap, "GALLIUM_DRIVER=iris");
        return true;
    }
    lap_push(env, envc, env_cap, "MESA_LOADER_DRIVER_OVERRIDE=nouveau");
    lap_push(env, envc, env_cap, "GALLIUM_DRIVER=nouveau");
    return true;
}

static void lap_push_physical_mesa_hint_env(char **env, int *envc, int env_cap,
                                            const char *driver) {
    if (!driver) return;
    if (lap_streq_ci(driver, "radeonsi")) {
        lap_push(env, envc, env_cap, "RIDUX_PREFERRED_MESA_DRIVER=radeonsi");
    } else if (lap_streq_ci(driver, "iris")) {
        lap_push(env, envc, env_cap, "RIDUX_PREFERRED_MESA_DRIVER=iris");
    } else {
        lap_push(env, envc, env_cap, "RIDUX_PREFERRED_MESA_DRIVER=nouveau");
    }
}

static void lap_push_vulkan_env(char **env, int *envc, int env_cap) {
    if (kvfs_exists("/etc/ridux-vulkan-virtio-only.enable") ||
        kvfs_exists("/etc/ridux-virtgpu-venus.enable")) {
        lap_push(env, envc, env_cap, "VK_DRIVER_FILES=/usr/share/vulkan/icd.d/virtio_icd.x86_64.json");
        lap_push(env, envc, env_cap, "VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/virtio_icd.x86_64.json");
        lap_push(env, envc, env_cap, "VK_LOADER_DRIVERS_SELECT=virtio_icd.x86_64.json");
        return;
    }
    lap_push(env, envc, env_cap, "VK_DRIVER_FILES=/usr/share/vulkan/icd.d/intel_icd.x86_64.json:/usr/share/vulkan/icd.d/intel_hasvk_icd.x86_64.json:/usr/share/vulkan/icd.d/radeon_icd.x86_64.json:/usr/share/vulkan/icd.d/nouveau_icd.x86_64.json:/usr/share/vulkan/icd.d/virtio_icd.x86_64.json:/usr/share/vulkan/icd.d/vmwgfx_icd.x86_64.json");
    lap_push(env, envc, env_cap, "VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/intel_icd.x86_64.json:/usr/share/vulkan/icd.d/intel_hasvk_icd.x86_64.json:/usr/share/vulkan/icd.d/radeon_icd.x86_64.json:/usr/share/vulkan/icd.d/nouveau_icd.x86_64.json:/usr/share/vulkan/icd.d/virtio_icd.x86_64.json:/usr/share/vulkan/icd.d/vmwgfx_icd.x86_64.json");
}

static void lap_push_mesa_gpu_env(char **env, int *envc, int env_cap) {
    const char *physical_override = lap_physical_mesa_driver_override();
    lap_push(env, envc, env_cap, "__GLX_VENDOR_LIBRARY_NAME=mesa");
    lap_push(env, envc, env_cap, "MESA_GL_VERSION_OVERRIDE=4.3");
    lap_push(env, envc, env_cap, "MESA_GLSL_VERSION_OVERRIDE=430");
    lap_push(env, envc, env_cap, "LIBGL_ALWAYS_SOFTWARE=0");
    lap_push(env, envc, env_cap, "vblank_mode=0");
    lap_push(env, envc, env_cap, "__GL_SYNC_TO_VBLANK=0");
    lap_push(env, envc, env_cap, "MESA_VK_WSI_PRESENT_MODE=immediate");
    lap_push_mesa_cache_env(env, envc, env_cap);
    lap_push_vulkan_env(env, envc, env_cap);
    if (lap_ridux_vbox_gpu_enabled()) {
        lap_push(env, envc, env_cap, "MESA_LOADER_DRIVER_OVERRIDE=vmwgfx");
        lap_push(env, envc, env_cap, "GALLIUM_DRIVER=svga");
    } else if (lap_ridux_virtio_gpu_enabled()) {
        lap_push(env, envc, env_cap, "MESA_LOADER_DRIVER_OVERRIDE=virtio_gpu");
        lap_push(env, envc, env_cap, "GALLIUM_DRIVER=virgl");
    } else if (physical_override && lap_physical_gpu_forced()) {
        lap_push_physical_mesa_env(env, envc, env_cap, physical_override);
    } else if (physical_override && lap_physical_gpu_preferred()) {
        lap_push_physical_mesa_hint_env(env, envc, env_cap, physical_override);
    }
}

static void lap_push_extras(char **argv, int *argc, int cap,
                            char **extra_args, int extra_argc) {
    int i;
    for (i = 0; i < extra_argc; ++i) {
        if (!extra_args[i] || !extra_args[i][0]) continue;
        lap_push(argv, argc, cap, extra_args[i]);
    }
}

static void lap_push_firefox_extras(char **argv, int *argc, int cap,
                                    char **extra_args, int extra_argc) {
    int i;
    for (i = 0; i < extra_argc; ++i) {
        if (!extra_args[i] || !extra_args[i][0]) continue;
        if (extra_args[i][0] != '-') {
            lap_push(argv, argc, cap, "--new-window");
        }
        lap_push(argv, argc, cap, extra_args[i]);
    }
}

ridux_linux_app_kind_t ridux_linux_app_kind_from_path(const char *path) {
    if (!path || !*path) return RIDUX_LINUX_APP_GENERIC;
    if (lap_has_token_ci(path, "ridux-vulkan-probe")) {
        return RIDUX_LINUX_APP_VULKAN_PROBE;
    }
    if (lap_has_token_ci(path, "ridux-ui-shell") ||
        lap_has_token_ci(path, "injury-compositor") ||
        lap_has_token_ci(path, "injury-shell") ||
        lap_has_token_ci(path, "ridux-gl-compositor")) {
        return RIDUX_LINUX_APP_GL_COMPOSITOR;
    }
    if (lap_has_token_ci(path, "/opt/hyprland/") ||
        lap_has_token_ci(path, "Hyprland") ||
        lap_has_token_ci(path, "hyprland") ||
        lap_has_token_ci(path, "hyprctl") ||
        lap_has_token_ci(path, "hyprpaper") ||
        lap_has_token_ci(path, "hypridle") ||
        lap_has_token_ci(path, "hyprlock") ||
        lap_has_token_ci(path, "xdg-desktop-portal-hyprland") ||
        lap_has_token_ci(path, "xdg-document-portal") ||
        lap_has_token_ci(path, "xdg-permission-store") ||
        lap_has_token_ci(path, "nwg-dock-hyprland")) {
        return RIDUX_LINUX_APP_HYPRLAND;
    }
    if (lap_hyprland_primary_enabled() &&
        (lap_has_token_ci(path, "waybar") ||
         lap_has_token_ci(path, "wofi") ||
         lap_has_token_ci(path, "xdg-desktop-portal") ||
         lap_has_token_ci(path, "xdg-document-portal") ||
         lap_has_token_ci(path, "xdg-permission-store"))) {
        return RIDUX_LINUX_APP_HYPRLAND;
    }
    if (lap_has_token_ci(path, "ridux-shell") ||
        lap_has_token_ci(path, "ridux-panel") ||
        lap_has_token_ci(path, "ridux-dock") ||
        lap_has_token_ci(path, "ridux-dashboard") ||
        lap_has_token_ci(path, "ridux-control") ||
        lap_has_token_ci(path, "ridux-monitor-qt") ||
        lap_has_token_ci(path, "ridux-files-qt")) {
        return RIDUX_LINUX_APP_RIDUX_SHELL;
    }
    if (lap_has_token_ci(path, "firefox")) return RIDUX_LINUX_APP_FIREFOX;
    if (lap_has_token_ci(path, "chromium") ||
        lap_has_token_ci(path, "google-chrome") ||
        lap_has_token_ci(path, "/chrome") ||
        lap_has_token_ci(path, "chrome.elf")) {
        return RIDUX_LINUX_APP_CHROMIUM;
    }
    if (lap_has_token_ci(path, "steam")) return RIDUX_LINUX_APP_STEAM;
    if (lap_has_token_ci(path, "plasmashell") ||
        lap_has_token_ci(path, "kwin") ||
        lap_has_token_ci(path, "kdeinit") ||
        lap_has_token_ci(path, "startplasma")) {
        return RIDUX_LINUX_APP_KDE;
    }
    if (lap_has_token_ci(path, "wayfire") ||
        lap_has_token_ci(path, "waybar") ||
        lap_has_token_ci(path, "wf-dock") ||
        lap_has_token_ci(path, "wf-panel") ||
        lap_has_token_ci(path, "wf-background") ||
        lap_has_token_ci(path, "wf-shell") ||
        lap_has_token_ci(path, "fuzzel") ||
        lap_has_token_ci(path, "wofi") ||
        lap_has_token_ci(path, "/wcm")) {
        return RIDUX_LINUX_APP_WAYFIRE;
    }
    if (lap_has_token_ci(path, "gtk") || lap_has_token_ci(path, "qt")) {
        return RIDUX_LINUX_APP_GUI;
    }
    return RIDUX_LINUX_APP_GENERIC;
}

bool ridux_linux_app_is_browser(ridux_linux_app_kind_t kind) {
    return kind == RIDUX_LINUX_APP_FIREFOX ||
           kind == RIDUX_LINUX_APP_CHROMIUM;
}

bool ridux_linux_app_should_chdir_to_app_dir(ridux_linux_app_kind_t kind) {
    return ridux_linux_app_is_browser(kind);
}

const char *ridux_linux_app_trace_name(ridux_linux_app_kind_t kind) {
    switch (kind) {
        case RIDUX_LINUX_APP_RIDUX_SHELL: return "ridux-shell";
        case RIDUX_LINUX_APP_FIREFOX:  return "firefox";
        case RIDUX_LINUX_APP_CHROMIUM: return "chromium";
        case RIDUX_LINUX_APP_KDE:      return "kde";
        case RIDUX_LINUX_APP_WAYFIRE:  return "wayfire";
        case RIDUX_LINUX_APP_HYPRLAND: return "hyprland";
        case RIDUX_LINUX_APP_GL_COMPOSITOR: return "ridux-gl-compositor";
        case RIDUX_LINUX_APP_VULKAN_PROBE: return "ridux-vulkan-probe";
        case RIDUX_LINUX_APP_STEAM:    return "steam";
        case RIDUX_LINUX_APP_GUI:      return "gui";
        default:                       return "linux";
    }
}

static const char *lap_basename(const char *path) {
    const char *base = path;
    const char *p = path;
    if (!path) return "";
    while (*p) {
        if (*p == '/' || *p == '\\') base = p + 1;
        ++p;
    }
    return base;
}

static bool lap_is_hyprland_compositor_path(const char *path) {
    const char *base = lap_basename(path);
    return lap_streq_ci(base, "Hyprland") ||
           lap_streq_ci(base, "hyprland") ||
           lap_streq_ci(base, "start-hyprland");
}

void ridux_linux_app_prepare_arg(const char *in, char *out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = 0;
    if (!in) return;
    if (lap_starts_ci(in, "https://") || lap_starts_ci(in, "http://")) {
        ulibc_snprintf(out, cap, "%s", in);
    } else if (lap_streq_ci(in, "google")) {
        ulibc_snprintf(out, cap, "https://www.google.com/");
    } else if (lap_streq_ci(in, "youtube")) {
        ulibc_snprintf(out, cap, "https://www.youtube.com/");
    } else if (lap_arg_looks_like_host(in)) {
        ulibc_snprintf(out, cap, "https://%s", in);
    } else {
        ulibc_snprintf(out, cap, "%s", in);
    }
}

static void lap_push_common_env(ridux_linux_app_kind_t kind,
                                bool direct_qt_shell,
                                bool hyprland_compositor,
                                char **env, int *envc, int env_cap) {
    lap_push(env, envc, env_cap, "PATH=/opt/hyprland/bin:/opt/hyprland/usr/bin:/opt/wayfire/bin:/opt/wayfire/usr/bin:/opt/kde-plasma/bin:/opt/kde-plasma/usr/bin:/usr/bin:/bin:/opt/chromium:/opt/google/chrome:/opt/firefox");
    lap_push(env, envc, env_cap, "HOME=/home");
    lap_push(env, envc, env_cap, "LANG=C.utf8");
    lap_push(env, envc, env_cap, "LC_ALL=C.utf8");
    lap_push(env, envc, env_cap, "LC_CTYPE=C.utf8");
    lap_push(env, envc, env_cap, "LOCPATH=/usr/lib/locale");
    lap_push(env, envc, env_cap, "TERM=xterm-256color");
    lap_push(env, envc, env_cap, "DISPLAY=:0");
    lap_push(env, envc, env_cap, "XDG_RUNTIME_DIR=/run/user/1000");
    if (!hyprland_compositor) {
        lap_push(env, envc, env_cap, "WAYLAND_DISPLAY=wayland-0");
    }
    if (kind == RIDUX_LINUX_APP_RIDUX_SHELL) {
        lap_push(env, envc, env_cap, direct_qt_shell ?
                 "XDG_SESSION_TYPE=tty" : "XDG_SESSION_TYPE=wayland");
        lap_push(env, envc, env_cap, "XDG_CURRENT_DESKTOP=Ridux");
        lap_push(env, envc, env_cap, "DESKTOP_SESSION=ridux");
        lap_push(env, envc, env_cap, "RIDUX_SHELL_SESSION=1");
    } else if (kind == RIDUX_LINUX_APP_KDE) {
        lap_push(env, envc, env_cap, "XDG_SESSION_TYPE=wayland");
        lap_push(env, envc, env_cap, "XDG_CURRENT_DESKTOP=KDE");
        lap_push(env, envc, env_cap, "DESKTOP_SESSION=plasma");
        lap_push(env, envc, env_cap, "KDE_FULL_SESSION=true");
        lap_push(env, envc, env_cap, "KDE_SESSION_VERSION=6");
        lap_push(env, envc, env_cap, "KDE_SESSION_UID=1000");
    } else if (kind == RIDUX_LINUX_APP_HYPRLAND) {
        lap_push(env, envc, env_cap, "XDG_SESSION_TYPE=wayland");
        lap_push(env, envc, env_cap, "XDG_CURRENT_DESKTOP=Hyprland");
        lap_push(env, envc, env_cap, "DESKTOP_SESSION=hyprland");
        lap_push(env, envc, env_cap, "HYPRLAND_CONFIG=/etc/hypr/hyprland.conf");
    } else if (lap_prefers_wayland_session(kind)) {
        lap_push(env, envc, env_cap, "XDG_SESSION_TYPE=wayland");
        lap_push(env, envc, env_cap, "XDG_CURRENT_DESKTOP=Wayfire");
        lap_push(env, envc, env_cap, "DESKTOP_SESSION=wayfire");
        if (kind == RIDUX_LINUX_APP_WAYFIRE) {
            lap_push(env, envc, env_cap, "WAYFIRE_CONFIG_FILE=/tmp/wayfire-home/config/wayfire.ini");
        }
    } else {
        lap_push(env, envc, env_cap, "XDG_SESSION_TYPE=x11");
        lap_push(env, envc, env_cap, "XDG_CURRENT_DESKTOP=Ridux");
        lap_push(env, envc, env_cap, "DESKTOP_SESSION=ridux");
    }
    if (kind == RIDUX_LINUX_APP_RIDUX_SHELL) {
        lap_push(env, envc, env_cap, "XDG_CONFIG_HOME=/tmp/ridux-home/config");
        lap_push(env, envc, env_cap, "XDG_CACHE_HOME=/tmp/ridux-home/cache");
        lap_push(env, envc, env_cap, "XDG_DATA_HOME=/tmp/ridux-home/share");
        lap_push(env, envc, env_cap, "XDG_STATE_HOME=/tmp/ridux-home/state");
        lap_push(env, envc, env_cap, "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus");
        lap_push(env, envc, env_cap, "DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket");
        lap_push(env, envc, env_cap, "XCURSOR_SIZE=28");
        lap_push(env, envc, env_cap, "XCURSOR_THEME=Adwaita");
        lap_push(env, envc, env_cap, "XCURSOR_PATH=/usr/share/icons:/usr/share/pixmaps");
    } else if (kind == RIDUX_LINUX_APP_KDE) {
        lap_push(env, envc, env_cap, "XDG_CONFIG_HOME=/tmp/kde-home/config");
        lap_push(env, envc, env_cap, "XDG_CACHE_HOME=/tmp/kde-home/cache");
        lap_push(env, envc, env_cap, "XDG_DATA_HOME=/tmp/kde-home/share");
        lap_push(env, envc, env_cap, "XDG_STATE_HOME=/tmp/kde-home/state");
        lap_push(env, envc, env_cap, "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus");
        lap_push(env, envc, env_cap, "DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket");
        lap_push(env, envc, env_cap, "XCURSOR_SIZE=28");
        lap_push(env, envc, env_cap, "XCURSOR_THEME=breeze_cursors");
        lap_push(env, envc, env_cap, "XCURSOR_PATH=/opt/kde-plasma/share/icons:/opt/kde-plasma/usr/share/icons:/usr/share/icons:/usr/share/pixmaps");
    } else if (kind == RIDUX_LINUX_APP_HYPRLAND) {
        lap_push(env, envc, env_cap, "XDG_CONFIG_HOME=/tmp/hyprland-home/config");
        lap_push(env, envc, env_cap, "XDG_CACHE_HOME=/tmp/hyprland-home/cache");
        lap_push(env, envc, env_cap, "XDG_DATA_HOME=/tmp/hyprland-home/share");
        lap_push(env, envc, env_cap, "XDG_STATE_HOME=/tmp/hyprland-home/state");
        lap_push(env, envc, env_cap, "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus");
        lap_push(env, envc, env_cap, "DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket");
        lap_push(env, envc, env_cap, "XCURSOR_SIZE=28");
        lap_push(env, envc, env_cap, "XCURSOR_THEME=Adwaita");
        lap_push(env, envc, env_cap, "XCURSOR_PATH=/usr/share/icons:/usr/share/pixmaps:/opt/hyprland/share/icons");
        lap_push(env, envc, env_cap, "MALLOC_ARENA_MAX=1");
        lap_push(env, envc, env_cap, "MALLOC_MMAP_THRESHOLD_=131072");
        lap_push(env, envc, env_cap, "MALLOC_TRIM_THRESHOLD_=262144");
        lap_push(env, envc, env_cap, "GLIBC_TUNABLES=glibc.malloc.arena_max=1:glibc.malloc.mmap_threshold=131072:glibc.malloc.trim_threshold=262144");
        lap_push(env, envc, env_cap, "LIBSEAT_BACKEND=builtin");
        lap_push(env, envc, env_cap, "HYPRLAND_NO_SD_NOTIFY=1");
        lap_push(env, envc, env_cap, "HYPRLAND_NO_SD_VARS=1");
        lap_push(env, envc, env_cap, "HYPRLAND_NO_RT=1");
        lap_push(env, envc, env_cap, "HYPRLAND_EGL_NO_MODIFIERS=1");
        lap_push(env, envc, env_cap, "AQ_NO_MODIFIERS=1");
        lap_push(env, envc, env_cap, "AQ_FORCE_LINEAR_BLIT=1");
        lap_push(env, envc, env_cap, "LIBINPUT_QUIRKS_DIR=/usr/share/libinput");
        lap_push(env, envc, env_cap, "LIBINPUT_QUIRKS_OVERRIDE_FILE=/usr/share/libinput/50-ridux.quirks");
        lap_push(env, envc, env_cap, "AQ_TRACE=0");
        lap_push(env, envc, env_cap, "WLR_BACKENDS=drm,libinput");
        lap_push(env, envc, env_cap, "WLR_LIBINPUT_NO_DEVICES=1");
        lap_push(env, envc, env_cap, "WLR_RENDERER=gles2");
        lap_push(env, envc, env_cap, "WLR_RENDERER_ALLOW_SOFTWARE=0");
        lap_push(env, envc, env_cap, "WLR_DRM_DEVICES=/dev/dri/card0");
        lap_push(env, envc, env_cap, "AQ_DRM_DEVICES=/dev/dri/card0");
        lap_push(env, envc, env_cap, "WLR_NO_HARDWARE_CURSORS=1");
        lap_push(env, envc, env_cap, "EGL_PLATFORM=gbm");
        lap_push(env, envc, env_cap, "GBM_BACKEND=drm");
        lap_push(env, envc, env_cap, "MESA_EXTENSION_OVERRIDE=-GL_KHR_parallel_shader_compile");
        lap_push(env, envc, env_cap, "MESA_GLTHREAD=false");
        lap_push(env, envc, env_cap, "mesa_glthread=false");
        lap_push(env, envc, env_cap, "GALLIUM_THREAD=0");
        lap_push_mesa_cache_env(env, envc, env_cap);
        if (lap_physical_gpu_forced() &&
            lap_push_physical_mesa_env(env, envc, env_cap,
                                       lap_physical_mesa_driver_override())) {
            lap_push(env, envc, env_cap, "RIDUX_HYPRLAND_GPU=physical-forced");
        } else if (lap_ridux_vbox_gpu_enabled()) {
            lap_push(env, envc, env_cap, "MESA_LOADER_DRIVER_OVERRIDE=vmwgfx");
            lap_push(env, envc, env_cap, "GALLIUM_DRIVER=svga");
        } else if (lap_ridux_virtio_gpu_enabled()) {
            lap_push(env, envc, env_cap, "MESA_LOADER_DRIVER_OVERRIDE=virtio_gpu");
            lap_push(env, envc, env_cap, "GALLIUM_DRIVER=virgl");
        } else {
            const char *driver = lap_physical_mesa_driver_override();
            if (lap_physical_gpu_preferred()) {
                lap_push_physical_mesa_hint_env(env, envc, env_cap, driver);
            }
        }
        lap_push_vulkan_env(env, envc, env_cap);
    } else if (lap_prefers_wayland_session(kind)) {
        lap_push(env, envc, env_cap, "XDG_CONFIG_HOME=/tmp/wayfire-home/config");
        lap_push(env, envc, env_cap, "XDG_CACHE_HOME=/tmp/wayfire-home/cache");
        lap_push(env, envc, env_cap, "XDG_DATA_HOME=/tmp/wayfire-home/share");
        lap_push(env, envc, env_cap, "XDG_STATE_HOME=/tmp/wayfire-home/state");
        lap_push(env, envc, env_cap, "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus");
        lap_push(env, envc, env_cap, "DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket");
        lap_push(env, envc, env_cap, "XCURSOR_SIZE=28");
        lap_push(env, envc, env_cap, "XCURSOR_THEME=Adwaita");
        lap_push(env, envc, env_cap, "XCURSOR_PATH=/usr/share/icons:/usr/share/pixmaps:/opt/wayfire/share/icons");
        if (kind == RIDUX_LINUX_APP_WAYFIRE) {
            /*
             * Mesa/Wayfire is the first real wlroots stack we run hard enough to
             * stress glibc's sysmalloc path.  Keep this scoped to Wayfire so the
             * compositor and Mesa prefer mmap-backed chunks while the Linux ABI
             * brk path matures, instead of corrupting the process heap during
             * renderer startup.  With mmap_threshold=0 glibc no longer grows the
             * fragile brk heap during Mesa startup; the Wayfire-only mmap slop
             * handler below covers the loader/LLVM sub-page edge clears this
             * exposes.
             */
            lap_push(env, envc, env_cap, "MALLOC_ARENA_MAX=1");
            lap_push(env, envc, env_cap, "MALLOC_MMAP_THRESHOLD_=0");
            lap_push(env, envc, env_cap, "MALLOC_TRIM_THRESHOLD_=0");
            lap_push(env, envc, env_cap, "GLIBC_TUNABLES=glibc.malloc.arena_max=1:glibc.malloc.mmap_threshold=0:glibc.malloc.trim_threshold=0");
            lap_push(env, envc, env_cap, "LD_PRELOAD=/opt/wayfire/lib/ridux-udev-monitor-shim.so");
            lap_push(env, envc, env_cap, "WAYFIRE_DEFAULT_CONFIG_BACKEND=/opt/wayfire/lib/wayfire/libdefault-config-backend.so");
            lap_push(env, envc, env_cap, "WAYFIRE_PLUGIN_XML_PATH=/opt/wayfire/share/wayfire/metadata");
            lap_push(env, envc, env_cap, "LIBSEAT_BACKEND=builtin");
            lap_push(env, envc, env_cap, "WLR_BACKENDS=drm,libinput");
            lap_push(env, envc, env_cap, "WLR_LIBINPUT_NO_DEVICES=1");
            lap_push(env, envc, env_cap, "RIDUX_VISIBLE_SHELL_FALLBACK=0");
            if (lap_wayfire_gpu_enabled()) {
                lap_push(env, envc, env_cap, "WLR_RENDERER=gles2");
                lap_push(env, envc, env_cap, "EGL_PLATFORM=gbm");
                lap_push(env, envc, env_cap, "GBM_BACKEND=drm");
                lap_push_mesa_cache_env(env, envc, env_cap);
                if (lap_wayfire_gles2_software_enabled()) {
                    lap_push(env, envc, env_cap, "LIBGL_ALWAYS_SOFTWARE=1");
                    lap_push(env, envc, env_cap, "MESA_LOADER_DRIVER_OVERRIDE=kms_swrast");
                    lap_push(env, envc, env_cap, "GALLIUM_DRIVER=llvmpipe");
                    lap_push(env, envc, env_cap, "WLR_RENDERER_ALLOW_SOFTWARE=1");
                } else if (lap_physical_gpu_forced() &&
                           lap_push_physical_mesa_env(env, envc, env_cap,
                                                      lap_physical_mesa_driver_override())) {
                    lap_push(env, envc, env_cap, "WLR_RENDERER_ALLOW_SOFTWARE=0");
                } else if (lap_wayfire_vbox_gpu_enabled()) {
                    lap_push(env, envc, env_cap, "MESA_LOADER_DRIVER_OVERRIDE=vmwgfx");
                    lap_push(env, envc, env_cap, "GALLIUM_DRIVER=svga");
                    lap_push(env, envc, env_cap, "WLR_RENDERER_ALLOW_SOFTWARE=0");
                } else if (lap_ridux_virtio_gpu_enabled()) {
                    lap_push(env, envc, env_cap, "MESA_LOADER_DRIVER_OVERRIDE=virtio_gpu");
                    lap_push(env, envc, env_cap, "GALLIUM_DRIVER=virgl");
                    lap_push(env, envc, env_cap, "WLR_RENDERER_ALLOW_SOFTWARE=0");
                } else {
                    const char *driver = lap_physical_mesa_driver_override();
                    if (lap_physical_gpu_preferred()) {
                        lap_push_physical_mesa_hint_env(env, envc, env_cap, driver);
                    }
                    lap_push(env, envc, env_cap, "WLR_RENDERER_ALLOW_SOFTWARE=0");
                }
                lap_push_vulkan_env(env, envc, env_cap);
            } else {
                lap_push(env, envc, env_cap, "WLR_RENDERER=pixman");
                lap_push(env, envc, env_cap, "WLR_RENDERER_ALLOW_SOFTWARE=1");
            }
            lap_push(env, envc, env_cap, "WLR_DRM_NO_ATOMIC=1");
            if (lap_wayfire_gpu_enabled() && !lap_wayfire_gles2_software_enabled() &&
                lap_wayfire_hw_cursor_enabled()) {
                lap_push(env, envc, env_cap, "WLR_NO_HARDWARE_CURSORS=0");
            } else {
                lap_push(env, envc, env_cap, "WLR_NO_HARDWARE_CURSORS=1");
            }
            /* wlroots expects WLR_DRM_DEVICES to contain KMS-capable cards.
             * renderD128 is a render node, not a scanout device; exposing it as
             * a second DRM backend makes Wayfire fail PRIME/import setup. */
            lap_push(env, envc, env_cap, "WLR_DRM_DEVICES=/dev/dri/card0");
        }
        lap_push(env, envc, env_cap, "XKB_DEFAULT_LAYOUT=us");
    } else {
        lap_push(env, envc, env_cap, "XDG_CONFIG_HOME=/tmp");
        lap_push(env, envc, env_cap, "XDG_CACHE_HOME=/tmp");
        lap_push(env, envc, env_cap, "XDG_DATA_HOME=/tmp");
        lap_push(env, envc, env_cap, "DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/dbus-ridux-session");
        lap_push(env, envc, env_cap, "DBUS_SYSTEM_BUS_ADDRESS=unix:path=/tmp/dbus-ridux-system");
    }
    lap_push(env, envc, env_cap, "NO_AT_BRIDGE=1");
    lap_push(env, envc, env_cap, "GTK_USE_PORTAL=0");
    lap_push(env, envc, env_cap, "GSETTINGS_BACKEND=memory");
    lap_push(env, envc, env_cap, "GSETTINGS_SCHEMA_DIR=/usr/share/glib-2.0/schemas");
    lap_push(env, envc, env_cap, "GTK_MODULES=");
    lap_push(env, envc, env_cap, "GTK_A11Y=none");
    if (direct_qt_shell) {
        lap_push(env, envc, env_cap, "GDK_BACKEND=wayland");
        lap_push(env, envc, env_cap, "QT_QPA_PLATFORM=eglfs");
        lap_push(env, envc, env_cap, "QT_QPA_EGLFS_INTEGRATION=eglfs_kms");
        lap_push(env, envc, env_cap, "QT_QPA_EGLFS_KMS_CONFIG=/etc/qt6/eglfs-kms.json");
        lap_push(env, envc, env_cap, "QT_QPA_EGLFS_ALWAYS_SET_MODE=1");
        lap_push(env, envc, env_cap, "QT_QPA_EGLFS_FORCEVSYNC=1");
        lap_push(env, envc, env_cap, "QT_QPA_EGLFS_HIDECURSOR=0");
        lap_push(env, envc, env_cap, "QT_QPA_EGLFS_NO_LIBINPUT=1");
        lap_push(env, envc, env_cap, "QT_QPA_GENERIC_PLUGINS=evdevmouse:/dev/input/event1,evdevkeyboard:/dev/input/event0");
        lap_push(env, envc, env_cap, "QT_QPA_EVDEV_MOUSE_PARAMETERS=/dev/input/event1");
        lap_push(env, envc, env_cap, "QT_QPA_EVDEV_KEYBOARD_PARAMETERS=/dev/input/event0");
        lap_push(env, envc, env_cap, "QT_QPA_FB_DRM=1");
        lap_push(env, envc, env_cap, "QT_QPA_FONTDIR=/usr/share/fonts/truetype/dejavu");
        lap_push(env, envc, env_cap, "QT_PLUGIN_PATH=/usr/lib/x86_64-linux-gnu/qt6/plugins:/usr/lib/qt6/plugins");
        lap_push(env, envc, env_cap, "QT_QPA_PLATFORM_PLUGIN_PATH=/usr/lib/x86_64-linux-gnu/qt6/plugins/platforms:/usr/lib/qt6/plugins/platforms");
        lap_push(env, envc, env_cap, "QT_OPENGL=es2");
        lap_push(env, envc, env_cap, "QSG_RHI_BACKEND=opengl");
        if (kvfs_exists("/etc/ridux-qt-quick.enable")) {
            lap_push(env, envc, env_cap, "RIDUX_QT_TRY_QUICK=1");
            lap_push(env, envc, env_cap, "QML_IMPORT_TRACE=1");
        } else {
            lap_push(env, envc, env_cap, "RIDUX_QT_WIDGETS_FALLBACK=1");
        }
        lap_push(env, envc, env_cap, "QML2_IMPORT_PATH=/usr/lib/x86_64-linux-gnu/qt6/qml:/usr/lib/qt6/qml:/usr/share/qt6/qml");
        lap_push(env, envc, env_cap, "QML_IMPORT_PATH=/usr/lib/x86_64-linux-gnu/qt6/qml:/usr/lib/qt6/qml:/usr/share/qt6/qml");
        lap_push(env, envc, env_cap, "QT_QUICK_CONTROLS_STYLE=Basic");
        lap_push(env, envc, env_cap, "EGL_PLATFORM=gbm");
        lap_push(env, envc, env_cap, "GBM_BACKEND=drm");
        lap_push(env, envc, env_cap, "RIDUX_REQUIRE_HARDWARE_GL=1");
        lap_push(env, envc, env_cap, "SDL_VIDEODRIVER=offscreen");
        lap_push_mesa_gpu_env(env, envc, env_cap);
    } else if (lap_prefers_wayland_session(kind)) {
        lap_push(env, envc, env_cap, "GDK_BACKEND=wayland");
        lap_push(env, envc, env_cap, "QT_QPA_PLATFORM=wayland");
        lap_push(env, envc, env_cap, "QT_PLUGIN_PATH=/usr/lib/x86_64-linux-gnu/qt6/plugins:/usr/lib/qt6/plugins");
        lap_push(env, envc, env_cap, "QT_QPA_PLATFORM_PLUGIN_PATH=/usr/lib/x86_64-linux-gnu/qt6/plugins/platforms:/usr/lib/qt6/plugins/platforms");
        lap_push(env, envc, env_cap, "QT_OPENGL=desktop");
        lap_push(env, envc, env_cap, "QSG_RHI_BACKEND=opengl");
        if (kind == RIDUX_LINUX_APP_RIDUX_SHELL || kind == RIDUX_LINUX_APP_GUI) {
            lap_push(env, envc, env_cap, "EGL_PLATFORM=wayland");
            lap_push(env, envc, env_cap, "QT_WAYLAND_DISABLE_WINDOWDECORATION=1");
            lap_push_mesa_gpu_env(env, envc, env_cap);
        }
        lap_push(env, envc, env_cap, "SDL_VIDEODRIVER=wayland");
    } else {
        lap_push(env, envc, env_cap, "GDK_BACKEND=x11");
        lap_push(env, envc, env_cap, "QT_QPA_PLATFORM=xcb");
        lap_push(env, envc, env_cap, "SDL_VIDEODRIVER=x11");
    }
    lap_push(env, envc, env_cap, "GIO_USE_VFS=local");
    lap_push(env, envc, env_cap, "GIO_USE_VOLUME_MONITOR=unix");
    lap_push(env, envc, env_cap, "FONTCONFIG_USE_MMAP=0");
    lap_push(env, envc, env_cap, "FC_DEBUG=0");
    if (direct_qt_shell) {
        lap_push(env, envc, env_cap, "LD_PRELOAD=/lib64/ridux-qt-freeguard.so");
        lap_push(env, envc, env_cap, "RIDUX_QT_LEAK_FREE=1");
        lap_push(env, envc, env_cap, "MALLOC_CHECK_=0");
        lap_push(env, envc, env_cap, "GLIBC_TUNABLES=glibc.malloc.check=0:glibc.malloc.tcache_count=0");
    }
    if (!direct_qt_shell) {
        lap_push(env, envc, env_cap, "FONTCONFIG_FILE=/etc/fonts/fonts-ridux.conf");
        lap_push(env, envc, env_cap, "FONTCONFIG_PATH=/etc/fonts");
        lap_push(env, envc, env_cap, "PANGOCAIRO_BACKEND=fontconfig");
    }
}

void ridux_linux_app_build_launch(ridux_linux_app_kind_t kind,
                                  const char *path,
                                  char **argv, int *argc, int argv_cap,
                                  char **extra_args, int extra_argc,
                                  char **env, int *envc, int env_cap) {
    bool direct_qt_shell = kind == RIDUX_LINUX_APP_RIDUX_SHELL &&
                           lap_extra_has(extra_args, extra_argc, "--direct-kms");
    bool hyprland_compositor = kind == RIDUX_LINUX_APP_HYPRLAND &&
                               lap_is_hyprland_compositor_path(path);
    if (!argv || !argc || !env || !envc) return;
    *argc = 0;
    *envc = 0;
    lap_push(argv, argc, argv_cap, path);
    if (kind == RIDUX_LINUX_APP_WAYFIRE && lap_wayfire_gles2_software_enabled()) {
        lap_push(argv, argc, argv_cap, "-r");
    }

    if (kind == RIDUX_LINUX_APP_CHROMIUM) {
        lap_push(argv, argc, argv_cap, "--no-sandbox");
        lap_push(argv, argc, argv_cap, "--user-data-dir=/tmp/chromium");
        lap_push(argv, argc, argv_cap, "--disk-cache-dir=/tmp/chromium-cache");
        lap_push(argv, argc, argv_cap, "--no-first-run");
        lap_push(argv, argc, argv_cap, "--no-default-browser-check");
        lap_push(argv, argc, argv_cap, "--test-type");
        lap_push(argv, argc, argv_cap, "--disable-session-crashed-bubble");
        lap_push(argv, argc, argv_cap, "--disable-infobars");
        lap_push(argv, argc, argv_cap, "--disable-component-update");
        lap_push(argv, argc, argv_cap, "--disable-default-apps");
        lap_push(argv, argc, argv_cap, "--ignore-gpu-blocklist");
        lap_push(argv, argc, argv_cap, "--enable-gpu-rasterization");
        lap_push(argv, argc, argv_cap, "--enable-zero-copy");
        lap_push(argv, argc, argv_cap, "--enable-accelerated-2d-canvas");
        lap_push(argv, argc, argv_cap, "--enable-accelerated-video-decode");
        lap_push(argv, argc, argv_cap, "--enable-native-gpu-memory-buffers");
        lap_push(argv, argc, argv_cap, "--use-gl=egl");
        lap_push(argv, argc, argv_cap, "--disable-dev-shm-usage");
        lap_push(argv, argc, argv_cap, "--no-zygote");
        lap_push(argv, argc, argv_cap, "--winhttp-proxy-resolver");
        lap_push(argv, argc, argv_cap, "--renderer-process-limit=1");
        lap_push(argv, argc, argv_cap, "--disable-local-storage");
        lap_push(argv, argc, argv_cap, "--disable-site-isolation-trials");
        lap_push(argv, argc, argv_cap, "--no-proxy-server");
        lap_push(argv, argc, argv_cap, "--proxy-server=direct://");
        lap_push(argv, argc, argv_cap, "--proxy-bypass-list=*");
        lap_push(argv, argc, argv_cap, "--password-store=basic");
        lap_push(argv, argc, argv_cap, "--disable-metrics");
        lap_push(argv, argc, argv_cap, "--disable-metrics-reporting");
        lap_push(argv, argc, argv_cap, "--disable-background-tracing");
        lap_push(argv, argc, argv_cap, "--disable-chrome-tracing-computation");
        lap_push(argv, argc, argv_cap, "--no-slow-histograms");
        lap_push(argv, argc, argv_cap, "--disable-field-trial-config");
        lap_push(argv, argc, argv_cap, "--disable-perfetto-system-tracing");
        lap_push(argv, argc, argv_cap, "--disable-features=MojoUseEventFd,CalculateNativeWinOcclusion,MediaRouter,DialMediaRouteProvider,OptimizationHints,AutofillServerCommunication,CertificateTransparencyComponentUpdater,FirstRunDesktopRevamp,FirstRunDesktopRefresh,SitePerProcess,IsolateOrigins,StrictOriginIsolation,ProcessPerSiteUpToMainFrameThreshold,EnablePerfettoSystemTracing,EnablePerfettoSystemBackgroundTracing,StorageServiceOutOfProcess,AudioServiceOutOfProcess,PartitionAllocBackupRefPtr,PartitionAllocMemoryTagging,PartitionAllocWithAdvancedChecks,PartitionAllocSchedulerLoopQuarantine,PartitionAllocSchedulerLoopQuarantineTaskControlledPurge,PartitionAllocEventuallyZeroFreedMemory,PartitionAllocMemoryReclaimer,PartitionAllocStraightenLargerSlotSpanFreeLists,PartitionAllocSortSmallerSlotSpanFreeLists,PartitionAllocSortActiveSlotSpans,PartitionAllocUseDenserDistribution,PartitionAllocFreeWithSize,PartitionAllocExternalMetadata,PartitionAllocLargeThreadCacheSize,PartitionAllocLargeEmptySlotSpanRing");
        lap_push(argv, argc, argv_cap, "--disable-background-networking");
        lap_push(argv, argc, argv_cap, "--disable-extensions");
        lap_push(argv, argc, argv_cap, "--disable-sync");
        lap_push(argv, argc, argv_cap, "--disable-breakpad");
        lap_push(argv, argc, argv_cap, "--disable-crash-reporter");
        lap_push(argv, argc, argv_cap, "--disable-crashpad");
        lap_push(argv, argc, argv_cap, "--disable-crashpad-for-testing");
        lap_push(argv, argc, argv_cap, "--disable-hang-monitor");
        lap_push(argv, argc, argv_cap, "--noerrdialogs");
        lap_push(argv, argc, argv_cap, "--enable-features=UseOzonePlatform,NetworkServiceInProcess2,Vulkan,CanvasOopRasterization,VaapiVideoDecoder");
        lap_push(argv, argc, argv_cap, "--ozone-platform=wayland");
    } else if (kind == RIDUX_LINUX_APP_FIREFOX) {
        lap_push(argv, argc, argv_cap, "--no-remote");
        lap_push(argv, argc, argv_cap, "--new-instance");
        lap_push(argv, argc, argv_cap, "--profile");
        lap_push(argv, argc, argv_cap, "/tmp/firefox-profile");
    } else if (kind == RIDUX_LINUX_APP_GL_COMPOSITOR) {
        lap_push(argv, argc, argv_cap, "--kms");
        lap_push(argv, argc, argv_cap, "--mesa");
    }

    if (extra_argc > 0 && kind == RIDUX_LINUX_APP_FIREFOX) {
        lap_push_firefox_extras(argv, argc, argv_cap, extra_args, extra_argc);
    } else if (extra_argc > 0) {
        lap_push_extras(argv, argc, argv_cap, extra_args, extra_argc);
    } else if (ridux_linux_app_is_browser(kind)) {
        lap_push(argv, argc, argv_cap, "file:///home/ridux-firefox-test.html");
    }
    argv[*argc] = 0;

    lap_push_common_env(kind, direct_qt_shell, hyprland_compositor, env, envc, env_cap);
    if (kind == RIDUX_LINUX_APP_CHROMIUM) {
        lap_push(env, envc, env_cap, "LD_PRELOAD=/opt/ridux/stackchk_trace.so");
        lap_push(env, envc, env_cap, "LD_LIBRARY_PATH=/opt/chromium:/opt/google/chrome:/lib64:/lib:/usr/lib:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu");
        lap_push_mesa_gpu_env(env, envc, env_cap);
    } else if (kind == RIDUX_LINUX_APP_FIREFOX) {
        lap_push(env, envc, env_cap, "MOZILLA_FIVE_HOME=/opt/firefox");
        lap_push(env, envc, env_cap, "MOZ_GRE_HOME=/opt/firefox");
        lap_push(env, envc, env_cap, "MOZ_XRE_DIR=/opt/firefox");
        lap_push(env, envc, env_cap, "MOZ_APP_LAUNCHER=/opt/firefox/firefox");
        lap_push(env, envc, env_cap, "MOZ_LEGACY_PROFILES=1");
        lap_push(env, envc, env_cap, "MOZ_ALLOW_DOWNGRADE=1");
        lap_push(env, envc, env_cap, "MALLOC_ARENA_MAX=1");
        lap_push(env, envc, env_cap, "MALLOC_MMAP_THRESHOLD_=131072");
        lap_push(env, envc, env_cap, "MALLOC_TRIM_THRESHOLD_=131072");
        lap_push(env, envc, env_cap, "G_SLICE=always-malloc");
        lap_push(env, envc, env_cap, "G_DEBUG=gc-friendly");
        lap_push(env, envc, env_cap, "GDK_CORE_DEVICE_EVENTS=1");
        lap_push(env, envc, env_cap, "GTK_IM_MODULE=xim");
        lap_push(env, envc, env_cap, "XMODIFIERS=@im=none");
        lap_push(env, envc, env_cap, "LD_PRELOAD=/opt/ridux/firefox_probe.so");
        lap_push(env, envc, env_cap, "LD_LIBRARY_PATH=/opt/firefox:/opt/chromium:/opt/google/chrome:/lib64:/lib:/usr/lib:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu");
        lap_push_mesa_gpu_env(env, envc, env_cap);
        lap_push(env, envc, env_cap, "MOZ_WEBRENDER=1");
        lap_push(env, envc, env_cap, "MOZ_ACCELERATED=1");
        lap_push(env, envc, env_cap, "MOZ_X11_EGL=1");
        lap_push(env, envc, env_cap, "MOZ_ENABLE_WAYLAND=1");
        lap_push(env, envc, env_cap, "MOZ_USE_XINPUT2=1");
        lap_push(env, envc, env_cap, "MOZ_DBUS_REMOTE=0");
        lap_push(env, envc, env_cap, "MOZ_ENABLE_DBUS=0");
        lap_push(env, envc, env_cap, "MOZ_DISABLE_DBUS=1");
        lap_push(env, envc, env_cap, "MOZ_DISABLE_GPU_SANDBOX=1");
        lap_push(env, envc, env_cap, "MOZ_DISABLE_FORKSERVER=1");
        lap_push(env, envc, env_cap, "MOZ_NO_REMOTE=1");
        lap_push(env, envc, env_cap, "GTK_CSD=0");
        lap_push(env, envc, env_cap, "MOZ_CRASHREPORTER_DISABLE=1");
        lap_push(env, envc, env_cap, "MOZ_CRASHREPORTER_NO_REPORT=1");
        lap_push(env, envc, env_cap, "MOZ_DISABLE_CRASHREPORTER=1");
    } else {
        if (kind == RIDUX_LINUX_APP_RIDUX_SHELL && direct_qt_shell) {
            lap_push(env, envc, env_cap, "LD_LIBRARY_PATH=/lib64:/lib:/usr/lib:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu");
        } else if (kind == RIDUX_LINUX_APP_HYPRLAND) {
            lap_push(env, envc, env_cap, "LD_LIBRARY_PATH=/opt/hyprland/lib:/opt/hyprland/lib/x86_64-linux-gnu:/opt/hyprland/lib64:/opt/hyprland/usr/lib:/opt/hyprland/usr/lib/x86_64-linux-gnu:/lib64:/lib:/usr/lib:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu");
        } else if (kind == RIDUX_LINUX_APP_WAYFIRE) {
            /* Wayfire must run as its own wlroots/Mesa stack.  Keeping the
             * old KDE payload in front of system libraries makes the dynamic
             * loader mix compositor-era libraries and can corrupt dlopen()
             * state while plugins are being loaded. */
            lap_push(env, envc, env_cap, "LD_LIBRARY_PATH=/opt/wayfire/lib:/opt/wayfire/lib64:/opt/wayfire/lib/x86_64-linux-gnu:/opt/wayfire/usr/lib:/opt/wayfire/usr/lib/x86_64-linux-gnu:/lib64:/lib:/usr/lib:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu");
        } else {
            lap_push(env, envc, env_cap, "LD_LIBRARY_PATH=/opt/wayfire/lib:/opt/wayfire/lib64:/opt/wayfire/lib/x86_64-linux-gnu:/opt/wayfire/usr/lib:/opt/wayfire/usr/lib/x86_64-linux-gnu:/opt/kde-plasma/lib:/opt/kde-plasma/lib64:/opt/kde-plasma/lib/x86_64-linux-gnu:/opt/kde-plasma/usr/lib:/opt/kde-plasma/usr/lib/x86_64-linux-gnu:/opt/chromium:/opt/google/chrome:/opt/firefox:/lib64:/lib:/usr/lib:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu");
        }
        if (kind == RIDUX_LINUX_APP_HYPRLAND) {
            lap_push(env, envc, env_cap, "XDG_DATA_DIRS=/opt/hyprland/share:/opt/hyprland/usr/share:/usr/share:/usr/local/share");
            lap_push(env, envc, env_cap, "HYPRLAND_CONFIG=/etc/hypr/hyprland.conf");
        } else if (kind == RIDUX_LINUX_APP_WAYFIRE) {
            lap_push(env, envc, env_cap, "XDG_DATA_DIRS=/opt/wayfire/share:/opt/wayfire/usr/share:/usr/share:/usr/local/share");
            lap_push(env, envc, env_cap, "WAYFIRE_PLUGIN_PATH=/opt/wayfire/lib/wayfire:/opt/wayfire/lib64/wayfire:/opt/wayfire/lib/x86_64-linux-gnu/wayfire:/usr/lib/x86_64-linux-gnu/wayfire");
        } else if (kind == RIDUX_LINUX_APP_GL_COMPOSITOR ||
                   kind == RIDUX_LINUX_APP_VULKAN_PROBE) {
            lap_push(env, envc, env_cap, "XDG_SESSION_TYPE=tty");
            lap_push(env, envc, env_cap, "XDG_CURRENT_DESKTOP=Ridux");
            if (kind == RIDUX_LINUX_APP_GL_COMPOSITOR) {
                lap_push(env, envc, env_cap, "DESKTOP_SESSION=ridux-gl");
                lap_push(env, envc, env_cap, "RIDUX_GL_COMPOSITOR=1");
            } else {
                lap_push(env, envc, env_cap, "DESKTOP_SESSION=ridux-vulkan");
                lap_push(env, envc, env_cap, "RIDUX_VULKAN_PROBE=1");
                lap_push(env, envc, env_cap, "LD_PRELOAD=/lib64/ridux-glibc-private-shim.so");
                lap_push(env, envc, env_cap, "LD_BIND_NOW=1");
                lap_push(env, envc, env_cap, "MESA_DEBUG=context");
                lap_push(env, envc, env_cap, "MESA_LOG_LEVEL=warn");
                lap_push(env, envc, env_cap, "MESA_LOG_FILE=/dev/stderr");
                lap_push(env, envc, env_cap, "VN_DEBUG=warn");
                lap_push(env, envc, env_cap, "MESA_VK_ABORT_ON_DEVICE_LOSS=0");
                lap_push(env, envc, env_cap, "VK_LOADER_DEBUG=error,warn");
            }
            lap_push(env, envc, env_cap, "EGL_PLATFORM=gbm");
            lap_push(env, envc, env_cap, "GBM_BACKEND=drm");
            lap_push(env, envc, env_cap, "LIBGL_ALWAYS_SOFTWARE=0");
            lap_push_mesa_cache_env(env, envc, env_cap);
            lap_push_mesa_gpu_env(env, envc, env_cap);
            if (lap_ridux_vbox_gpu_enabled()) {
                lap_push(env, envc, env_cap, "RIDUX_GBM_IMMEDIATE_PRESENT=1");
            }
        } else if (kind == RIDUX_LINUX_APP_KDE) {
            lap_push(env, envc, env_cap, "XDG_DATA_DIRS=/opt/kde-plasma/share:/opt/kde-plasma/usr/share:/usr/share:/usr/local/share");
            lap_push(env, envc, env_cap, "LD_PRELOAD=/lib64/ridux-qt-freeguard.so");
            lap_push(env, envc, env_cap, "RIDUX_QT_LEAK_FREE=1");
            lap_push(env, envc, env_cap, "MALLOC_CHECK_=0");
            lap_push(env, envc, env_cap, "GLIBC_TUNABLES=glibc.malloc.check=0:glibc.malloc.tcache_count=0");
            lap_push(env, envc, env_cap, "QT_PLUGIN_PATH=/opt/kde-plasma/lib/x86_64-linux-gnu/qt6/plugins:/opt/kde-plasma/lib/qt6/plugins:/opt/kde-plasma/usr/lib/x86_64-linux-gnu/qt6/plugins:/usr/lib/x86_64-linux-gnu/qt6/plugins:/usr/lib/qt6/plugins");
            lap_push(env, envc, env_cap, "QT_QPA_PLATFORM_PLUGIN_PATH=/opt/kde-plasma/lib/x86_64-linux-gnu/qt6/plugins/platforms:/opt/kde-plasma/lib/qt6/plugins/platforms:/opt/kde-plasma/usr/lib/x86_64-linux-gnu/qt6/plugins/platforms:/usr/lib/x86_64-linux-gnu/qt6/plugins/platforms:/usr/lib/qt6/plugins/platforms");
            lap_push(env, envc, env_cap, "QML2_IMPORT_PATH=/opt/kde-plasma/lib/x86_64-linux-gnu/qt6/qml:/opt/kde-plasma/lib/qt6/qml:/opt/kde-plasma/usr/lib/x86_64-linux-gnu/qt6/qml:/usr/lib/x86_64-linux-gnu/qt6/qml:/usr/lib/qt6/qml:/opt/kde-plasma/share/plasma/plasmoids:/usr/share/plasma/plasmoids");
            lap_push(env, envc, env_cap, "KDEDIRS=/opt/kde-plasma:/opt/kde-plasma/usr:/usr");
            lap_push(env, envc, env_cap, "KDEHOME=/tmp/kde-home");
            lap_push(env, envc, env_cap, "KDETMP=/tmp/kde-tmp");
            lap_push(env, envc, env_cap, "KDEVARTMP=/tmp/kde-var-tmp");
            lap_push(env, envc, env_cap, "QT_OPENGL=desktop");
            lap_push(env, envc, env_cap, "QSG_RHI_BACKEND=opengl");
            lap_push(env, envc, env_cap, "QT_WAYLAND_DISABLE_WINDOWDECORATION=1");
            lap_push(env, envc, env_cap, "KWIN_COMPOSE=O2");
            lap_push(env, envc, env_cap, "KWIN_OPENGL_INTERFACE=egl");
            lap_push(env, envc, env_cap, "KWIN_FORCE_SW_CURSOR=1");
            lap_push(env, envc, env_cap, "KWIN_DRM_DEVICES=/dev/dri/card0");
            lap_push(env, envc, env_cap, "__GLX_VENDOR_LIBRARY_NAME=mesa");
            lap_push(env, envc, env_cap, "EGL_PLATFORM=gbm");
            lap_push(env, envc, env_cap, "GBM_BACKEND=drm");
            if (lap_plasma_gpu_required()) {
                const char *driver = lap_physical_mesa_driver_override();
                if (lap_plasma_vbox_gpu_enabled()) {
                    lap_push(env, envc, env_cap, "MESA_LOADER_DRIVER_OVERRIDE=vmwgfx");
                    lap_push(env, envc, env_cap, "GALLIUM_DRIVER=svga");
                } else if (lap_ridux_virtio_gpu_enabled()) {
                    lap_push(env, envc, env_cap, "MESA_LOADER_DRIVER_OVERRIDE=virtio_gpu");
                    lap_push(env, envc, env_cap, "GALLIUM_DRIVER=virgl");
                } else if (driver && lap_physical_gpu_forced()) {
                    lap_push_physical_mesa_env(env, envc, env_cap, driver);
                } else if (driver && lap_physical_gpu_preferred()) {
                    lap_push_physical_mesa_hint_env(env, envc, env_cap, driver);
                }
            }
            lap_push_vulkan_env(env, envc, env_cap);
        } else if (kind == RIDUX_LINUX_APP_RIDUX_SHELL || kind == RIDUX_LINUX_APP_GUI) {
            lap_push(env, envc, env_cap, "XDG_DATA_DIRS=/usr/share:/usr/local/share");
            lap_push(env, envc, env_cap, "QML2_IMPORT_PATH=/usr/lib/x86_64-linux-gnu/qt6/qml:/usr/lib/qt6/qml");
        } else if (kind == RIDUX_LINUX_APP_STEAM) {
            lap_push_mesa_gpu_env(env, envc, env_cap);
        }
    }
    env[*envc] = 0;
}
