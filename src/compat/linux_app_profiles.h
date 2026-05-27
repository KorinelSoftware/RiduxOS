#ifndef RIDUX_LINUX_APP_PROFILES_H
#define RIDUX_LINUX_APP_PROFILES_H

#include <stdbool.h>
#include <stddef.h>

typedef enum ridux_linux_app_kind {
    RIDUX_LINUX_APP_GENERIC = 0,
    RIDUX_LINUX_APP_RIDUX_SHELL,
    RIDUX_LINUX_APP_GUI,
    RIDUX_LINUX_APP_FIREFOX,
    RIDUX_LINUX_APP_CHROMIUM,
    RIDUX_LINUX_APP_KDE,
    RIDUX_LINUX_APP_WAYFIRE,
    RIDUX_LINUX_APP_HYPRLAND,
    RIDUX_LINUX_APP_GL_COMPOSITOR,
    RIDUX_LINUX_APP_VULKAN_PROBE,
    RIDUX_LINUX_APP_STEAM
} ridux_linux_app_kind_t;

ridux_linux_app_kind_t ridux_linux_app_kind_from_path(const char *path);
bool ridux_linux_app_is_browser(ridux_linux_app_kind_t kind);
bool ridux_linux_app_should_chdir_to_app_dir(ridux_linux_app_kind_t kind);
void ridux_linux_app_prepare_arg(const char *in, char *out, size_t cap);

void ridux_linux_app_build_launch(ridux_linux_app_kind_t kind,
                                  const char *path,
                                  char **argv, int *argc, int argv_cap,
                                  char **extra_args, int extra_argc,
                                  char **env, int *envc, int env_cap);

const char *ridux_linux_app_trace_name(ridux_linux_app_kind_t kind);

#endif
