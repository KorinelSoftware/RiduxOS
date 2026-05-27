#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <unistd.h>

static char *const k_wf_background_argv[] = {
    "/opt/wayfire/bin/wf-background", "-c",
    "/tmp/wayfire-home/config/wf-shell.ini", NULL
};
static char *const k_wf_panel_argv[] = {
    "/opt/wayfire/bin/wf-panel", "-c",
    "/tmp/wayfire-home/config/wf-shell.ini", NULL
};
static char *const k_wf_dock_argv[] = {
    "/opt/wayfire/bin/wf-dock", "-c",
    "/tmp/wayfire-home/config/wf-shell.ini", NULL
};
static char *const k_ridux_panel_argv[] = {
    "/opt/wayfire/bin/ridux-panel", NULL
};
static char *const k_waybar_argv[] = {
    "/usr/bin/waybar", "-c", "/etc/xdg/waybar/config",
    "-s", "/etc/xdg/waybar/style.css", NULL
};
static char *const k_portal_wlr_libexec_argv[] = {
    "/usr/libexec/xdg-desktop-portal-wlr", NULL
};
static char *const k_portal_wlr_bin_argv[] = {
    "/usr/bin/xdg-desktop-portal-wlr", NULL
};
static char *const k_portal_libexec_argv[] = {
    "/usr/libexec/xdg-desktop-portal", NULL
};
static char *const k_portal_bin_argv[] = {
    "/usr/bin/xdg-desktop-portal", NULL
};
static char *const k_qt_dashboard_argv[] = {
    "/opt/wayfire/bin/ridux-qt-dashboard", "--mode=dashboard", NULL
};
static char *const k_qt_files_argv[] = {
    "/opt/wayfire/bin/ridux-qt-files", "--mode=files", NULL
};
static char *const k_qt_monitor_argv[] = {
    "/opt/wayfire/bin/ridux-qt-monitor", "--mode=monitor", NULL
};
static char *const k_qt_panel_argv[] = {
    "/opt/wayfire/bin/ridux-qt-dashboard", "--mode=panel", NULL
};
static char *const k_qt_dock_argv[] = {
    "/opt/wayfire/bin/ridux-qt-monitor", "--mode=dock", NULL
};
static char *const k_thunar_argv[] = {
    "/usr/bin/thunar", "/home", NULL
};
static char *const k_pipewire_argv[] = {
    "/usr/bin/pipewire", NULL
};
static char *const k_wireplumber_argv[] = {
    "/usr/bin/wireplumber", NULL
};
static char *const k_pipewire_pulse_argv[] = {
    "/usr/bin/pipewire-pulse", NULL
};

static pid_t g_panel_pid = -1;
static pid_t g_background_pid = -1;
static pid_t g_dock_pid = -1;
static pid_t g_pipewire_pid = -1;
static pid_t g_wireplumber_pid = -1;
static pid_t g_pipewire_pulse_pid = -1;
static pid_t g_portal_wlr_pid = -1;
static pid_t g_portal_pid = -1;
static pid_t g_swaync_pid = -1;
static pid_t g_gpu_ladder_pid = -1;
static int g_panel_mode = 0;      /* 1=Waybar, 2=wf-panel, 3=RiduxPanel */
static int g_background_mode = 0; /* 1=wf-background, 2=Ridux background */
static int g_dock_mode = 0;       /* 1=wf-dock, 2=Ridux dock */
static unsigned g_ridux_panel_restarts = 0;
static unsigned g_waybar_restarts = 0;
static unsigned g_wf_panel_restarts = 0;
static unsigned g_background_restarts = 0;
static unsigned g_dock_restarts = 0;
static unsigned g_pipewire_restarts = 0;
static unsigned g_wireplumber_restarts = 0;
static unsigned g_pipewire_pulse_restarts = 0;
static unsigned g_portal_wlr_restarts = 0;
static unsigned g_portal_restarts = 0;
static unsigned g_swaync_restarts = 0;

static void supervise_children_once(void);

static int file_exec(const char *path) {
    return path && *path && access(path, X_OK) == 0;
}

static int marker_exists(const char *path) {
    return path && *path && access(path, F_OK) == 0;
}

static void env_set(const char *key, const char *value) {
    if (key && value) setenv(key, value, 1);
}

static void env_set_default(const char *key, const char *value) {
    if (key && value && !getenv(key)) setenv(key, value, 0);
}

static void configure_client_heap(void) {
    /*
     * Wayfire itself is launched with a very conservative glibc malloc profile
     * from the kernel-side app profile.  The session supervisor then execs the
     * visible clients (Waybar, wf-background, wf-dock, Thunar, portals), so keep
     * the same single-arena discipline here too.  Do not inherit
     * MALLOC_MMAP_THRESHOLD_=0 into GTK/Pango clients: that turns normal startup
     * allocation into a storm of anonymous mmap arenas before any Wayland surface
     * appears, which looks like a black desktop even though VirGL is alive.
     */
    env_set("MALLOC_CHECK_", "0");
    env_set("MALLOC_ARENA_MAX", "1");
    env_set("MALLOC_MMAP_THRESHOLD_", "131072");
    env_set("MALLOC_TRIM_THRESHOLD_", "0");
    env_set("GLIBC_TUNABLES",
            "glibc.malloc.check=0:"
            "glibc.malloc.arena_max=1:"
            "glibc.malloc.tcache_count=0:"
            "glibc.malloc.mmap_threshold=131072:"
            "glibc.malloc.trim_threshold=0");
}

static int env_truthy(const char *key) {
    const char *value = key ? getenv(key) : NULL;
    return value && (strcmp(value, "1") == 0 ||
                     strcmp(value, "true") == 0 ||
                     strcmp(value, "yes") == 0 ||
                     strcmp(value, "on") == 0);
}

static int full_desktop_stack_enabled(void) {
    return env_truthy("RIDUX_FULL_DESKTOP_STACK") ||
           marker_exists("/etc/ridux-wayfire-full-stack.enable");
}

static int strict_wayfire_stack_enabled(void) {
    return env_truthy("RIDUX_STRICT_WAYFIRE_STACK") ||
           marker_exists("/etc/ridux-wayfire-primary.enable") ||
           marker_exists("/etc/ridux-wayfire-strict-stack.enable");
}

static int wayfire_hw_cursor_enabled(void) {
    return env_truthy("RIDUX_WAYFIRE_HW_CURSOR") ||
           env_truthy("RIDUX_UI_ASSUME_HW_CURSOR_VISIBLE") ||
           marker_exists("/etc/ridux-wayfire-hw-cursor.enable") ||
           marker_exists("/etc/ridux-ui-hw-cursor-only.enable");
}

static void ensure_dir(const char *path, mode_t mode) {
    if (!path || !*path) return;
    if (mkdir(path, mode) < 0 && errno != EEXIST)
        fprintf(stderr, "ridux-session: mkdir failed %s: %s\n", path, strerror(errno));
}

static void session_pause_ms(unsigned ms) {
    usleep((useconds_t)ms * 1000u);
}

static int probe_unix_socket(const char *path) {
    struct sockaddr_un addr;
    int fd;
    int ok;

    if (!path || !*path) return 0;
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    ok = (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    close(fd);
    return ok;
}

static int wait_for_pipewire_socket(unsigned timeout_ms) {
    unsigned elapsed = 0;

    while (elapsed <= timeout_ms) {
        if (probe_unix_socket("/run/user/1000/pipewire-0-manager") ||
            probe_unix_socket("/run/user/1000/pipewire-0") ||
            probe_unix_socket("/run/pipewire/pipewire-0-manager") ||
            probe_unix_socket("/run/pipewire/pipewire-0")) {
            fprintf(stderr, "ridux-session: pipewire native socket is ready\n");
            return 1;
        }
        session_pause_ms(100);
        elapsed += 100;
    }

    fprintf(stderr, "ridux-session: pipewire native socket not ready after %u ms\n",
            timeout_ms);
    return 0;
}

static pid_t spawn_argv(const char *label, const char *path, char *const argv[]) {
    pid_t pid;
    if (!file_exec(path)) {
        fprintf(stderr, "ridux-session: skip %s (%s not executable)\n",
                label ? label : path, path ? path : "?");
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "ridux-session: fork failed for %s (%s): %s\n",
                label ? label : path, path, strerror(errno));
        return -1;
    }

    if (pid == 0) {
        execv(path, argv);
        fprintf(stderr, "ridux-session: exec failed for %s (%s): %s\n",
                label ? label : path, path, strerror(errno));
        _exit(127);
    }

    fprintf(stderr, "ridux-session: started %s pid=%ld path=%s\n",
            label ? label : path, (long)pid, path);
    return pid;
}

static pid_t spawn_path(const char *label, const char *path) {
    char *const argv[] = {(char *)path, NULL};
    return spawn_argv(label, path, argv);
}

static void build_heap_guard_preload(char *dst, size_t dst_size,
                                     const char *old_preload) {
    const char *guard = "/opt/wayfire/lib/ridux-client-freeguard.so";
    if (!dst || dst_size == 0) return;
    if (old_preload && (strstr(old_preload, "ridux-client-freeguard.so") ||
                        strstr(old_preload, "ridux-qt-freeguard.so"))) {
        snprintf(dst, dst_size, "%s", old_preload);
    } else if (old_preload && *old_preload) {
        snprintf(dst, dst_size, "%s:%s", guard, old_preload);
    } else {
        snprintf(dst, dst_size, "%s", guard);
    }
}

static pid_t spawn_heap_guarded_argv(const char *label, const char *path,
                                     char *const argv[], int leak_free) {
    char preload[1024];
    pid_t pid;

    if (!file_exec(path)) {
        fprintf(stderr, "ridux-session: skip %s (%s not executable)\n",
                label ? label : path, path ? path : "?");
        return -1;
    }

    build_heap_guard_preload(preload, sizeof(preload), getenv("LD_PRELOAD"));
    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "ridux-session: fork failed for %s (%s): %s\n",
                label ? label : path, path, strerror(errno));
        return -1;
    }

    if (pid == 0) {
        env_set("LD_PRELOAD", preload);
        env_set("RIDUX_QT_LEAK_FREE", leak_free ? "1" : "0");
        env_set("G_SLICE", "always-malloc");
        env_set("G_DEBUG", "gc-friendly");
        configure_client_heap();
        execv(path, argv);
        fprintf(stderr, "ridux-session: exec failed for %s (%s): %s\n",
                label ? label : path, path, strerror(errno));
        _exit(127);
    }

    fprintf(stderr, "ridux-session: started %s pid=%ld path=%s heap-guard=%s\n",
            label ? label : path, (long)pid, path,
            leak_free ? "leak-free" : "normal");
    return pid;
}

static pid_t spawn_qt_argv(const char *label, const char *path, char *const argv[]) {
    return spawn_heap_guarded_argv(label, path, argv, 1);
}

static pid_t spawn_waybar_argv(const char *label, char *const argv[]) {
    return spawn_heap_guarded_argv(label, "/usr/bin/waybar", argv, 0);
}

static void report_tool(const char *label, const char *path) {
    fprintf(stderr, "ridux-session: %s %s %s\n",
            file_exec(path) ? "ready" : "missing", label, path ? path : "?");
}

static void report_interactive_tools(void) {
    report_tool("launcher-wrapper", "/usr/bin/ridux-open-launcher");
    report_tool("files-wrapper", "/usr/bin/ridux-open-files");
    report_tool("terminal-wrapper", "/usr/bin/ridux-terminal");
    report_tool("power-menu-wrapper", "/usr/bin/ridux-power-menu");
    report_tool("display-settings-wrapper", "/usr/bin/ridux-display-settings");
    report_tool("wofi", "/usr/bin/wofi");
    report_tool("lock-wrapper", "/usr/bin/ridux-lock");
    report_tool("swaylock", "/usr/bin/swaylock");
    report_tool("screenshot-wrapper", "/usr/bin/ridux-screenshot");
    report_tool("grim", "/usr/bin/grim");
    report_tool("slurp", "/usr/bin/slurp");
}

static void start_visible_shell(void) {
    const char *enabled = getenv("RIDUX_VISIBLE_SHELL_FALLBACK");
    if (enabled && (strcmp(enabled, "0") == 0 || strcmp(enabled, "false") == 0))
        return;
    if (file_exec("/opt/wayfire/bin/ridux-visible-shell")) {
        spawn_path("ridux-visible-shell", "/opt/wayfire/bin/ridux-visible-shell");
        session_pause_ms(350);
    }
}

static void start_gpu_ladder(void) {
    const char *enabled = getenv("RIDUX_GPU_LADDER");
    if (enabled && (strcmp(enabled, "0") == 0 || strcmp(enabled, "false") == 0))
        return;
    if (!file_exec("/opt/wayfire/bin/ridux-gpu-ladder")) {
        fprintf(stderr, "ridux-session: missing gpu-ladder /opt/wayfire/bin/ridux-gpu-ladder\n");
        return;
    }
    g_gpu_ladder_pid = spawn_path("gpu-ladder", "/opt/wayfire/bin/ridux-gpu-ladder");
}

static int child_exited(pid_t pid, const char *label) {
    int status = 0;
    pid_t got;

    if (pid <= 0) return 0;
    got = waitpid(pid, &status, WNOHANG);
    if (got != pid) return 0;
    if (WIFEXITED(status)) {
        fprintf(stderr, "ridux-session: %s pid=%ld exited early code=%d\n",
                label ? label : "child", (long)pid, WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        fprintf(stderr, "ridux-session: %s pid=%ld crashed early sig=%d\n",
                label ? label : "child", (long)pid, WTERMSIG(status));
    } else {
        fprintf(stderr, "ridux-session: %s pid=%ld stopped early status=0x%x\n",
                label ? label : "child", (long)pid, status);
    }
    return 1;
}

static void log_child_status(pid_t pid, int status, const char *label) {
    if (WIFEXITED(status)) {
        fprintf(stderr, "ridux-session: %s pid=%ld exited code=%d\n",
                label ? label : "child", (long)pid, WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        fprintf(stderr, "ridux-session: %s pid=%ld signaled sig=%d\n",
                label ? label : "child", (long)pid, WTERMSIG(status));
    } else {
        fprintf(stderr, "ridux-session: %s pid=%ld changed status=0x%x\n",
                label ? label : "child", (long)pid, status);
    }
}

static pid_t restart_path_service(pid_t *slot, const char *label,
                                  const char *path, unsigned *restarts,
                                  unsigned max_restarts) {
    if (!slot || !restarts) return -1;
    if (*restarts >= max_restarts) {
        fprintf(stderr, "ridux-session: not restarting %s after %u attempts\n",
                label ? label : path, *restarts);
        *slot = -1;
        return -1;
    }
    ++*restarts;
    session_pause_ms(450);
    *slot = spawn_path(label, path);
    return *slot;
}

static void start_panel_chain(int prefer_ridux) {
    int prefer_waybar = env_truthy("RIDUX_PREFER_WAYBAR") ||
                        env_truthy("RIDUX_START_NATIVE_WAYBAR");
    int prefer_wf_panel = env_truthy("RIDUX_PREFER_WF_PANEL") ||
                          env_truthy("RIDUX_START_NATIVE_WF_PANEL") ||
                          env_truthy("RIDUX_ORIGINAL_WF_SHELL");
    int strict = strict_wayfire_stack_enabled();
    /* Prefer the upstream wf-shell panel for the default Wayfire desktop. Keep
       Waybar installed and selectable, but do not make the boot path depend on
       its custom Ridux config unless the user opts in. */
    if (strict) {
        if (!prefer_wf_panel && !prefer_waybar) prefer_wf_panel = 1;
        if (prefer_wf_panel) prefer_waybar = 0;
        prefer_ridux = 0;
    } else if (prefer_waybar) {
        prefer_ridux = 0;
    } else if (prefer_wf_panel) {
        prefer_ridux = 0;
    } else if (env_truthy("RIDUX_PREFER_RIDUX_PANEL")) {
        prefer_ridux = 1;
    } else {
        prefer_ridux = 0;
    }

    if (prefer_wf_panel && file_exec("/opt/wayfire/bin/wf-panel")) {
        g_panel_mode = 2;
        g_panel_pid = spawn_heap_guarded_argv("wf-panel", "/opt/wayfire/bin/wf-panel",
                                              k_wf_panel_argv, 0);
        return;
    }
    if (prefer_waybar && file_exec("/usr/bin/waybar")) {
        g_panel_mode = 1;
        g_panel_pid = spawn_waybar_argv("waybar", k_waybar_argv);
        return;
    }
    if (strict && prefer_waybar) {
        g_panel_mode = 1;
        g_panel_pid = -1;
        fprintf(stderr, "ridux-session: required Waybar is not executable; strict stack will not use panel fallback\n");
        return;
    }
    if (strict && prefer_wf_panel) {
        g_panel_mode = 2;
        g_panel_pid = -1;
        fprintf(stderr, "ridux-session: required wf-panel is not executable; strict stack will not use panel fallback\n");
        return;
    }
    if (prefer_ridux && file_exec("/opt/wayfire/bin/ridux-panel")) {
        g_panel_mode = 3;
        g_panel_pid = spawn_argv("ridux-panel", "/opt/wayfire/bin/ridux-panel",
                                 k_ridux_panel_argv);
        return;
    }
    if (file_exec("/opt/wayfire/bin/wf-panel")) {
        g_panel_mode = 2;
        g_panel_pid = spawn_heap_guarded_argv("wf-panel", "/opt/wayfire/bin/wf-panel",
                                              k_wf_panel_argv, 0);
        return;
    }
    if (file_exec("/usr/bin/waybar")) {
        g_panel_mode = 1;
        g_panel_pid = spawn_waybar_argv("waybar-fallback", k_waybar_argv);
        return;
    }
    if (prefer_ridux && file_exec("/opt/wayfire/bin/ridux-panel")) {
        g_panel_mode = 3;
        g_panel_pid = spawn_argv("ridux-panel-fallback", "/opt/wayfire/bin/ridux-panel",
                                 k_ridux_panel_argv);
        return;
    }
    g_panel_mode = 0;
    g_panel_pid = -1;
    fprintf(stderr, "ridux-session: no panel implementation is executable\n");
}

static void restart_panel_after_exit(void) {
    if (g_panel_mode == 1) {
        unsigned max_restarts = strict_wayfire_stack_enabled() ? 8u : 1u;
        if ((env_truthy("RIDUX_PREFER_WAYBAR") || strict_wayfire_stack_enabled()) &&
            g_waybar_restarts < max_restarts &&
            file_exec("/usr/bin/waybar")) {
            ++g_waybar_restarts;
            fprintf(stderr, "ridux-session: restarting Waybar attempt=%u\n",
                    g_waybar_restarts);
            session_pause_ms(650);
            g_panel_pid = spawn_waybar_argv("waybar", k_waybar_argv);
            return;
        }
        if (strict_wayfire_stack_enabled()) {
            fprintf(stderr, "ridux-session: Waybar exhausted restarts; strict stack keeps panel down\n");
            g_panel_pid = -1;
            return;
        }
        fprintf(stderr, "ridux-session: Waybar exhausted restarts, falling back\n");
        g_panel_mode = 2;
        if (file_exec("/opt/wayfire/bin/wf-panel")) {
            g_panel_pid = spawn_heap_guarded_argv("wf-panel", "/opt/wayfire/bin/wf-panel",
                                                  k_wf_panel_argv, 0);
            return;
        }
        g_panel_mode = 3;
        if (file_exec("/opt/wayfire/bin/ridux-panel")) {
            g_panel_pid = spawn_argv("ridux-panel-fallback", "/opt/wayfire/bin/ridux-panel",
                                     k_ridux_panel_argv);
            return;
        }
        g_panel_pid = -1;
        return;
    }

    if (g_panel_mode == 2) {
        if (g_wf_panel_restarts < 3 && file_exec("/opt/wayfire/bin/wf-panel")) {
            ++g_wf_panel_restarts;
            session_pause_ms(650);
            g_panel_pid = spawn_heap_guarded_argv("wf-panel", "/opt/wayfire/bin/wf-panel",
                                                  k_wf_panel_argv, 0);
            return;
        }
        if (file_exec("/opt/wayfire/bin/ridux-panel")) {
            g_panel_mode = 3;
            g_panel_pid = spawn_argv("ridux-panel-fallback", "/opt/wayfire/bin/ridux-panel",
                                     k_ridux_panel_argv);
            return;
        }
    }

    if (g_panel_mode == 3 && g_ridux_panel_restarts < 3 &&
        file_exec("/opt/wayfire/bin/ridux-panel")) {
        ++g_ridux_panel_restarts;
        session_pause_ms(650);
        g_panel_pid = spawn_argv("ridux-panel-fallback", "/opt/wayfire/bin/ridux-panel",
                                 k_ridux_panel_argv);
        return;
    }

    g_panel_pid = -1;
}

static void start_background_chain(int prefer_ridux) {
    int strict = strict_wayfire_stack_enabled();
    if (file_exec("/opt/wayfire/bin/wf-background")) {
        g_background_mode = 1;
        g_background_pid = spawn_heap_guarded_argv("wf-background", "/opt/wayfire/bin/wf-background",
                                                   k_wf_background_argv, 0);
        return;
    }
    if (strict) {
        g_background_mode = 1;
        g_background_pid = -1;
        fprintf(stderr, "ridux-session: required wf-background is not executable; strict stack will not use background fallback\n");
        return;
    }
    if (prefer_ridux && file_exec("/opt/wayfire/bin/ridux-background")) {
        g_background_mode = 2;
        g_background_pid = spawn_path("ridux-background-fallback", "/opt/wayfire/bin/ridux-background");
        return;
    }
    g_background_mode = 0;
    g_background_pid = -1;
}

static void restart_background_after_exit(void) {
    if (g_background_mode == 1) {
        unsigned max_restarts = strict_wayfire_stack_enabled() ? 8u : 3u;
        if (g_background_restarts < max_restarts && file_exec("/opt/wayfire/bin/wf-background")) {
            ++g_background_restarts;
            session_pause_ms(650);
            g_background_pid = spawn_heap_guarded_argv("wf-background", "/opt/wayfire/bin/wf-background",
                                                       k_wf_background_argv, 0);
            return;
        }
        if (strict_wayfire_stack_enabled()) {
            fprintf(stderr, "ridux-session: wf-background exhausted restarts; strict stack keeps background down\n");
            g_background_pid = -1;
            return;
        }
        fprintf(stderr, "ridux-session: wf-background exhausted restarts, switching to Ridux background\n");
        start_background_chain(1);
        return;
    }
    restart_path_service(&g_background_pid, "ridux-background-fallback",
                         "/opt/wayfire/bin/ridux-background",
                         &g_background_restarts, 3);
}

static void start_dock_chain(int prefer_ridux) {
    int prefer_wf_dock = env_truthy("RIDUX_PREFER_WF_DOCK") ||
                         env_truthy("RIDUX_START_NATIVE_WF_DOCK");
    int strict = strict_wayfire_stack_enabled();
    /* Default to the real wf-dock binary so the desktop matches the Linux
       Wayfire stack the user expects. The Ridux custom dock is only used as
       an explicit opt-in (RIDUX_PREFER_RIDUX_DOCK=1) or as a last-resort
       fallback if wf-dock is missing. The previous logic forced the custom
       dock whenever ridux-wayfire-vbox-gpu.enable was present, which made the
       desktop look unfamiliar in VirtualBox GPU mode. */
    if (strict || prefer_wf_dock) {
        prefer_ridux = 0;
    } else if (env_truthy("RIDUX_PREFER_RIDUX_DOCK")) {
        prefer_ridux = 1;
    } else {
        prefer_ridux = 0;
    }
    if (file_exec("/opt/wayfire/bin/wf-dock") && !prefer_ridux) {
        g_dock_mode = 1;
        g_dock_pid = spawn_heap_guarded_argv("wf-dock", "/opt/wayfire/bin/wf-dock",
                                             k_wf_dock_argv, 0);
        return;
    }
    if (strict) {
        g_dock_mode = 1;
        g_dock_pid = -1;
        fprintf(stderr, "ridux-session: required wf-dock is not executable; strict stack will not use dock fallback\n");
        return;
    }
    if (prefer_ridux && file_exec("/opt/wayfire/bin/ridux-dock")) {
        g_dock_mode = 2;
        g_dock_pid = spawn_path("ridux-dock", "/opt/wayfire/bin/ridux-dock");
        return;
    }
    if (file_exec("/opt/wayfire/bin/wf-dock")) {
        g_dock_mode = 1;
        g_dock_pid = spawn_heap_guarded_argv("wf-dock", "/opt/wayfire/bin/wf-dock",
                                             k_wf_dock_argv, 0);
        return;
    }
    if (file_exec("/opt/wayfire/bin/ridux-dock")) {
        g_dock_mode = 2;
        g_dock_pid = spawn_path("ridux-dock-fallback", "/opt/wayfire/bin/ridux-dock");
        return;
    }
    g_dock_mode = 0;
    g_dock_pid = -1;
}

static void restart_dock_after_exit(void) {
    if (g_dock_mode == 1) {
        unsigned max_restarts = strict_wayfire_stack_enabled() ? 8u : 3u;
        if (g_dock_restarts < max_restarts && file_exec("/opt/wayfire/bin/wf-dock")) {
            ++g_dock_restarts;
            session_pause_ms(650);
            g_dock_pid = spawn_heap_guarded_argv("wf-dock", "/opt/wayfire/bin/wf-dock",
                                                 k_wf_dock_argv, 0);
            return;
        }
        if (strict_wayfire_stack_enabled()) {
            fprintf(stderr, "ridux-session: wf-dock exhausted restarts; strict stack keeps dock down\n");
            g_dock_pid = -1;
            return;
        }
        fprintf(stderr, "ridux-session: wf-dock exhausted restarts, switching to Ridux dock\n");
        start_dock_chain(1);
        return;
    }
    restart_path_service(&g_dock_pid, "ridux-dock-fallback", "/opt/wayfire/bin/ridux-dock",
                         &g_dock_restarts, 3);
}

static void start_portal_wlr_service(void) {
    if (file_exec("/usr/libexec/xdg-desktop-portal-wlr"))
        g_portal_wlr_pid = spawn_argv("xdg-desktop-portal-wlr",
                                      "/usr/libexec/xdg-desktop-portal-wlr",
                                      k_portal_wlr_libexec_argv);
    else
        g_portal_wlr_pid = spawn_argv("xdg-desktop-portal-wlr",
                                      "/usr/bin/xdg-desktop-portal-wlr",
                                      k_portal_wlr_bin_argv);
}

static void start_portal_service(void) {
    if (file_exec("/usr/libexec/xdg-desktop-portal"))
        g_portal_pid = spawn_argv("xdg-desktop-portal",
                                  "/usr/libexec/xdg-desktop-portal",
                                  k_portal_libexec_argv);
    else
        g_portal_pid = spawn_argv("xdg-desktop-portal",
                                  "/usr/bin/xdg-desktop-portal",
                                  k_portal_bin_argv);
}

static void supervise_children_once(void) {
    int status = 0;
    pid_t pid;

    for (;;) {
        pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0) break;

        if (pid == g_panel_pid) {
            log_child_status(pid, status, "panel");
            g_panel_pid = -1;
            restart_panel_after_exit();
        } else if (pid == g_background_pid) {
            log_child_status(pid, status, "background");
            g_background_pid = -1;
            restart_background_after_exit();
        } else if (pid == g_dock_pid) {
            log_child_status(pid, status, "dock");
            g_dock_pid = -1;
            restart_dock_after_exit();
        } else if (pid == g_pipewire_pid) {
            log_child_status(pid, status, "pipewire");
            restart_path_service(&g_pipewire_pid, "pipewire", "/usr/bin/pipewire",
                                 &g_pipewire_restarts, 3);
        } else if (pid == g_wireplumber_pid) {
            log_child_status(pid, status, "wireplumber");
            restart_path_service(&g_wireplumber_pid, "wireplumber",
                                 "/usr/bin/wireplumber",
                                 &g_wireplumber_restarts, 3);
        } else if (pid == g_pipewire_pulse_pid) {
            log_child_status(pid, status, "pipewire-pulse");
            restart_path_service(&g_pipewire_pulse_pid, "pipewire-pulse",
                                 "/usr/bin/pipewire-pulse",
                                 &g_pipewire_pulse_restarts, 3);
        } else if (pid == g_portal_wlr_pid) {
            log_child_status(pid, status, "xdg-desktop-portal-wlr");
            if (g_portal_wlr_restarts < 3) {
                ++g_portal_wlr_restarts;
                session_pause_ms(450);
                start_portal_wlr_service();
            } else {
                g_portal_wlr_pid = -1;
            }
        } else if (pid == g_portal_pid) {
            log_child_status(pid, status, "xdg-desktop-portal");
            if (g_portal_restarts < 3) {
                ++g_portal_restarts;
                session_pause_ms(450);
                start_portal_service();
            } else {
                g_portal_pid = -1;
            }
        } else if (pid == g_swaync_pid) {
            log_child_status(pid, status, "swaync");
            restart_path_service(&g_swaync_pid, "swaync", "/usr/bin/swaync",
                                 &g_swaync_restarts, 3);
        } else if (pid == g_gpu_ladder_pid) {
            log_child_status(pid, status, "gpu-ladder");
            g_gpu_ladder_pid = -1;
        } else {
            log_child_status(pid, status, "child");
        }
    }
}

static void wait_for_gpu_ladder_initial(unsigned timeout_ms) {
    unsigned delay = timeout_ms < 2500 ? timeout_ms : 2500;
    session_pause_ms(delay);
    if (g_gpu_ladder_pid > 0)
        fprintf(stderr, "ridux-session: continuing after gpu-ladder warmup\n");
}

static void configure_environment(void) {
    int pixman_desktop = marker_exists("/etc/ridux-wayfire-pixman.enable");
    int present2d_shell = marker_exists("/etc/ridux-wayfire-present2d.enable");
    int full_stack = full_desktop_stack_enabled();
    int strict_stack = strict_wayfire_stack_enabled();

    if (strict_stack) {
        pixman_desktop = 0;
        present2d_shell = 0;
        full_stack = 1;
    }

    ensure_dir("/tmp/fontconfig-cache", 0777);
    ensure_dir("/tmp/wayfire-home", 0700);
    ensure_dir("/tmp/wayfire-home/cache", 0700);
    ensure_dir("/tmp/wayfire-home/cache/fontconfig", 0700);
    ensure_dir("/var/cache", 0755);
    ensure_dir("/var/cache/fontconfig", 0777);
    env_set("GDK_BACKEND", "wayland");
    env_set("QT_QPA_PLATFORM", "wayland");
    env_set("QT_PLUGIN_PATH", "/usr/lib/x86_64-linux-gnu/qt6/plugins");
    env_set("QT_QPA_PLATFORM_PLUGIN_PATH", "/usr/lib/x86_64-linux-gnu/qt6/plugins/platforms");
    env_set("QT_OPENGL", "desktop");
    env_set("QSG_RHI_BACKEND", "opengl");
    env_set("SDL_VIDEODRIVER", "wayland");
    env_set("CLUTTER_BACKEND", "wayland");
    env_set("XDG_SESSION_TYPE", "wayland");
    env_set("XDG_CURRENT_DESKTOP", "Wayfire");
    env_set("DESKTOP_SESSION", "wayfire");
    env_set("HOME", "/home");
    env_set("USER", "ridux");
    env_set("LOGNAME", "ridux");
    env_set("LANG", "C.UTF-8");
    env_set("LC_ALL", "C.UTF-8");
    env_set("XDG_RUNTIME_DIR", "/run/user/1000");
    env_set("PIPEWIRE_RUNTIME_DIR", "/run/user/1000");
    env_set("PIPEWIRE_MODULE_DIR", "/usr/lib/x86_64-linux-gnu/pipewire-0.3");
    env_set("SPA_PLUGIN_DIR", "/usr/lib/x86_64-linux-gnu/spa-0.2");
    env_set("WIREPLUMBER_CONFIG_DIR", "/usr/share/wireplumber:/etc/wireplumber");
    env_set("WAYLAND_DISPLAY", "wayland-1");
    env_set("XDG_CONFIG_HOME", "/tmp/wayfire-home/config");
    env_set("XDG_CACHE_HOME", "/tmp/wayfire-home/cache");
    env_set("XDG_DATA_HOME", "/tmp/wayfire-home/share");
    env_set("XDG_STATE_HOME", "/tmp/wayfire-home/state");
    env_set("NO_AT_BRIDGE", "1");
    env_set("GTK_USE_PORTAL", "0");
    env_set("GSETTINGS_BACKEND", "memory");
    env_set("GSETTINGS_SCHEMA_DIR", "/usr/share/glib-2.0/schemas");
    env_set("GTK_MODULES", "");
    env_set("GTK_A11Y", "none");
    env_set("GIO_USE_VFS", "local");
    env_set("GIO_USE_VOLUME_MONITOR", "unix");
    env_set("XDG_DATA_DIRS", "/opt/wayfire/share:/usr/local/share:/usr/share");
    env_set("GDK_PIXBUF_MODULE_FILE",
            "/usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0/2.10.0/loaders.cache");
    env_set("GDK_PIXBUF_MODULEDIR",
            "/usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0/2.10.0/loaders");
    env_set("XCURSOR_SIZE", "28");
    env_set("XCURSOR_THEME", "Adwaita");
    env_set("XCURSOR_PATH", "/usr/share/icons:/usr/share/pixmaps:/opt/wayfire/share/icons");
    configure_client_heap();
    if (marker_exists("/etc/ridux-wayfire-disable-shader-cache.enable") ||
        marker_exists("/etc/ridux-mesa-disable-shader-cache.enable")) {
        env_set("MESA_SHADER_CACHE_DISABLE", "1");
        env_set("MESA_GLSL_CACHE_DISABLE", "1");
    }
    if (pixman_desktop) {
        env_set("RIDUX_VISIBLE_SHELL_FALLBACK", "0");
        env_set("RIDUX_SIMPLE_WAYLAND_SHELL", "0");
        env_set("RIDUX_GPU_LADDER", "0");
        env_set("RIDUX_AUTOSTART_FILES", "1");
        env_set("RIDUX_PREFER_WAYBAR", "0");
        env_set("RIDUX_PREFER_WF_DOCK", "0");
        env_set("RIDUX_PREFER_RIDUX_PANEL", "1");
        env_set("RIDUX_PREFER_RIDUX_DOCK", "1");
        env_set("RIDUX_FULL_DESKTOP_STACK", "1");
        /* Force wlroots to skip GLES2 entirely and use the pixman software
           renderer over the DRM/dumb-buffer backend. Without these, wlroots
           tries EGL/GLES2 first, fails on VMSVGA and exits the compositor,
           leaving the user with a blank wallpaper and no widgets. */
        env_set("WLR_RENDERER", "pixman");
        env_set("WLR_BACKENDS", "drm");
        env_set("WLR_RENDERER_ALLOW_SOFTWARE", "1");
        env_set("WLR_NO_HARDWARE_CURSORS", "1");
        env_set("WLR_DRM_NO_ATOMIC", "1");
        env_set("WLR_DRM_NO_MODIFIERS", "1");
        env_set("LIBGL_ALWAYS_SOFTWARE", "1");
        env_set("MESA_LOADER_DRIVER_OVERRIDE", "swrast");
        env_set("GALLIUM_DRIVER", "llvmpipe");
        env_set("QT_OPENGL", "software");
        env_set("QT_QUICK_BACKEND", "software");
        env_set("QSG_RHI_BACKEND", "software");
        env_set("MOZ_ENABLE_WAYLAND", "1");
        fprintf(stderr, "ridux-session: desktop mode=pixman-full (WLR_RENDERER=pixman)\n");
    } else if (present2d_shell) {
        env_set("RIDUX_VISIBLE_SHELL_FALLBACK", "1");
        env_set("RIDUX_SIMPLE_WAYLAND_SHELL", "0");
        env_set("RIDUX_AUTOSTART_FILES", "1");
        env_set("RIDUX_PREFER_WAYBAR", "0");
        env_set("RIDUX_PREFER_WF_DOCK", "0");
        env_set("RIDUX_PREFER_RIDUX_PANEL", "1");
        env_set("RIDUX_PREFER_RIDUX_DOCK", "1");
        env_set("WLR_NO_HARDWARE_CURSORS", "1");
        fprintf(stderr, "ridux-session: desktop mode=present2d-full\n");
    } else {
        int hw_cursor = wayfire_hw_cursor_enabled();
        env_set("RIDUX_VISIBLE_SHELL_FALLBACK", "0");
        env_set("RIDUX_SIMPLE_WAYLAND_SHELL", "0");
        env_set("WLR_NO_HARDWARE_CURSORS", hw_cursor ? "0" : "1");
        fprintf(stderr, "ridux-session: cursor mode=%s\n",
                hw_cursor ? "drm-hardware-plane" : "wayland-adwaita-composited");
        if (full_stack) {
            env_set("RIDUX_AUTOSTART_FILES", "0");
            env_set("RIDUX_AUTOSTART_QT", "0");
            env_set("RIDUX_QT_SHOWCASE_WINDOWS", "0");
            env_set("RIDUX_PREFER_WAYBAR", "0");
            env_set("RIDUX_PREFER_WF_PANEL", "1");
            env_set("RIDUX_ORIGINAL_WF_SHELL", "1");
            env_set("RIDUX_PREFER_WF_DOCK", "1");
            env_set("RIDUX_PREFER_RIDUX_PANEL", "0");
            env_set("RIDUX_PREFER_RIDUX_DOCK", "0");
            env_set("RIDUX_FULL_DESKTOP_STACK", "1");
            env_set("RIDUX_STRICT_WAYFIRE_STACK", strict_stack ? "1" : "0");
            env_set("MOZ_ENABLE_WAYLAND", "1");
            env_set("LIBGL_ALWAYS_SOFTWARE", "0");
            if (marker_exists("/etc/ridux-wayfire-virtio-gpu.enable") ||
                marker_exists("/etc/ridux-virtio-gpu.enable") ||
                marker_exists("/etc/ridux-virtgpu-venus.enable")) {
                env_set("MESA_LOADER_DRIVER_OVERRIDE", "virtio_gpu");
                env_set("GALLIUM_DRIVER", "virgl");
                fprintf(stderr, "ridux-session: Mesa renderer preference=virtio_gpu/virgl\n");
            } else {
                env_set("MESA_LOADER_DRIVER_OVERRIDE", "vmwgfx");
                env_set("GALLIUM_DRIVER", "svga");
                fprintf(stderr, "ridux-session: Mesa renderer preference=vmwgfx/svga\n");
            }
            env_set("WLR_RENDERER_ALLOW_SOFTWARE", "0");
            env_set("WLR_DRM_NO_MODIFIERS", "1");
            fprintf(stderr, "ridux-session: desktop mode=wayfire-full-stack original-wf-panel original-wf-dock strict=%d\n",
                    strict_stack);
        } else if (marker_exists("/etc/ridux-wayfire-vbox-gpu.enable")) {
            /* Even when the VirtualBox GPU marker is present, prefer the
               real Wayfire stack (Waybar + wf-dock + Thunar) so the desktop
               looks like a normal Linux Wayfire setup. The custom Ridux
               panel/dock are still available if the user opts in via
               RIDUX_PREFER_RIDUX_PANEL/RIDUX_PREFER_RIDUX_DOCK. */
            env_set("RIDUX_PREFER_WAYBAR", "0");
            env_set("RIDUX_PREFER_WF_DOCK", "0");
            env_set("RIDUX_PREFER_RIDUX_PANEL", "1");
            env_set("RIDUX_PREFER_RIDUX_DOCK", "1");
            env_set("MOZ_ENABLE_WAYLAND", "1");
            env_set("MESA_LOADER_DRIVER_OVERRIDE", "vmwgfx");
            env_set("GALLIUM_DRIVER", "svga");
            env_set("WLR_RENDERER_ALLOW_SOFTWARE", "0");
            env_set("WLR_DRM_NO_MODIFIERS", "1");
            fprintf(stderr, "ridux-session: desktop mode=wayfire-full vbox-gpu gtk-riduxui\n");
        } else {
            fprintf(stderr, "ridux-session: desktop mode=wayfire-full\n");
        }
    }
    env_set("FONTCONFIG_FILE", "/etc/fonts/fonts-ridux.conf");
    env_set("FONTCONFIG_PATH", "/etc/fonts");
    env_set("FONTCONFIG_USE_MMAP", "0");
    env_set("PANGOCAIRO_BACKEND", "fontconfig");
    env_set("DBUS_SESSION_BUS_ADDRESS", "unix:path=/run/user/1000/bus");
    env_set("LD_LIBRARY_PATH",
            "/opt/wayfire/lib:/opt/wayfire/lib64:/opt/wayfire/lib/x86_64-linux-gnu:"
            "/usr/lib/x86_64-linux-gnu:/lib/x86_64-linux-gnu:/lib64:/usr/lib:/lib");
    env_set("PATH", "/opt/wayfire/bin:/usr/bin:/bin");
}

static void start_qt_showcase_apps(void) {
    const char *enabled = getenv("RIDUX_AUTOSTART_QT");
    if (full_desktop_stack_enabled()) {
        fprintf(stderr,
                "ridux-session: Qt showcase autostart skipped; real Linux desktop stack owns startup\n");
        return;
    }
    if (enabled && (strcmp(enabled, "0") == 0 || strcmp(enabled, "false") == 0))
        return;

    if (file_exec("/opt/wayfire/bin/ridux-qt-dashboard")) {
        spawn_qt_argv("ridux-qt-dashboard", "/opt/wayfire/bin/ridux-qt-dashboard",
                      k_qt_dashboard_argv);
        session_pause_ms(450);
    }
    if (file_exec("/opt/wayfire/bin/ridux-qt-files")) {
        spawn_qt_argv("ridux-qt-files", "/opt/wayfire/bin/ridux-qt-files",
                      k_qt_files_argv);
        session_pause_ms(350);
    }
    if (file_exec("/opt/wayfire/bin/ridux-qt-monitor")) {
        spawn_qt_argv("ridux-qt-monitor", "/opt/wayfire/bin/ridux-qt-monitor",
                      k_qt_monitor_argv);
        session_pause_ms(350);
    }
}

static void autostart_files_if_enabled(void) {
    const char *files = getenv("RIDUX_AUTOSTART_FILES");
    if (!(files && (strcmp(files, "1") == 0 || strcmp(files, "true") == 0)))
        return;

    if (file_exec("/usr/bin/thunar"))
        spawn_heap_guarded_argv("files", "/usr/bin/thunar", k_thunar_argv, 0);
    else if (file_exec("/opt/wayfire/bin/thunar"))
        spawn_heap_guarded_argv("files", "/opt/wayfire/bin/thunar",
                                (char *const[]){"/opt/wayfire/bin/thunar", NULL}, 0);
}

static void start_full_stack_bridge_controls(void) {
    if (!env_truthy("RIDUX_ENABLE_FULL_STACK_BRIDGE"))
        return;

    if (file_exec("/opt/wayfire/bin/ridux-panel")) {
        fprintf(stderr, "ridux-session: starting Ridux panel bridge for visible full-stack controls\n");
        spawn_path("ridux-panel-bridge", "/opt/wayfire/bin/ridux-panel");
        session_pause_ms(450);
    }
    if (file_exec("/opt/wayfire/bin/ridux-dock")) {
        fprintf(stderr, "ridux-session: starting Ridux dock bridge for visible full-stack controls\n");
        spawn_path("ridux-dock-bridge", "/opt/wayfire/bin/ridux-dock");
        session_pause_ms(450);
    }
}

static void start_native_full_stack_clients_if_enabled(void) {
    if (env_truthy("RIDUX_START_NATIVE_WAYBAR") && file_exec("/usr/bin/waybar")) {
        fprintf(stderr, "ridux-session: starting native Waybar opt-in\n");
        spawn_waybar_argv("waybar-native", k_waybar_argv);
        session_pause_ms(650);
    }
    if (env_truthy("RIDUX_START_NATIVE_WF_DOCK") && file_exec("/opt/wayfire/bin/wf-dock")) {
        fprintf(stderr, "ridux-session: starting native wf-dock opt-in\n");
        spawn_heap_guarded_argv("wf-dock-native", "/opt/wayfire/bin/wf-dock",
                                k_wf_dock_argv, 0);
        session_pause_ms(450);
    }
}

static void start_full_stack_accessories(void) {
    if (full_desktop_stack_enabled() ||
        env_truthy("RIDUX_ENABLE_FULL_STACK_CONTROLS")) {
        if (g_panel_pid <= 0) {
            start_panel_chain(1);
            session_pause_ms(800);
        } else {
            fprintf(stderr, "ridux-session: panel already started pid=%ld\n",
                    (long)g_panel_pid);
        }

        if (env_truthy("RIDUX_START_NATIVE_WF_DOCK") ||
            env_truthy("RIDUX_PREFER_WF_DOCK") ||
            marker_exists("/etc/ridux-wayfire-start-wf-dock.enable")) {
            if (g_dock_pid <= 0) {
                start_dock_chain(0);
                session_pause_ms(450);
            } else {
                fprintf(stderr, "ridux-session: wf-dock already started pid=%ld\n",
                        (long)g_dock_pid);
            }
        } else if (env_truthy("RIDUX_PREFER_RIDUX_DOCK")) {
            if (g_dock_pid <= 0) {
                start_dock_chain(1);
                session_pause_ms(450);
            }
        } else {
            fprintf(stderr,
                    "ridux-session: wf-dock autostart disabled by policy\n");
        }
    }

    start_full_stack_bridge_controls();
    start_native_full_stack_clients_if_enabled();
    autostart_files_if_enabled();
}

static void start_core_desktop(void) {
    int simple_shell = env_truthy("RIDUX_SIMPLE_WAYLAND_SHELL");
    int full_stack = full_desktop_stack_enabled();
    if (simple_shell) {
        session_pause_ms(500);
        return;
    }

    if (full_stack) {
        fprintf(stderr, "ridux-session: full-stack uses wf-background\n");
        start_background_chain(0);
        session_pause_ms(500);
        start_panel_chain(1);
        session_pause_ms(650);
        return;
    }

    start_background_chain(0);
    session_pause_ms(500);

    start_panel_chain(0);
    session_pause_ms(800);

    start_dock_chain(0);
    session_pause_ms(300);

    autostart_files_if_enabled();
}

static void start_services(void) {
    session_pause_ms(500);
    g_pipewire_pid = spawn_argv("pipewire", "/usr/bin/pipewire", k_pipewire_argv);
    wait_for_pipewire_socket(3500);

    g_wireplumber_pid = spawn_argv("wireplumber", "/usr/bin/wireplumber",
                                   k_wireplumber_argv);
    session_pause_ms(320);

    g_pipewire_pulse_pid = spawn_argv("pipewire-pulse", "/usr/bin/pipewire-pulse",
                                      k_pipewire_pulse_argv);
    session_pause_ms(320);

    start_portal_wlr_service();
    session_pause_ms(220);

    start_portal_service();
    session_pause_ms(220);

    g_swaync_pid = spawn_heap_guarded_argv("swaync", "/usr/bin/swaync",
                                           (char *const[]){"/usr/bin/swaync", NULL}, 0);
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    configure_environment();

    fprintf(stderr, "ridux-session: starting Wayfire desktop session\n");
    report_interactive_tools();
    start_visible_shell();
    start_core_desktop();
    session_pause_ms(1000);
    start_gpu_ladder();
    wait_for_gpu_ladder_initial(12000);
    session_pause_ms(500);
    start_qt_showcase_apps();
    if (full_desktop_stack_enabled()) {
        session_pause_ms(1200);
        start_full_stack_accessories();
    }
    session_pause_ms(3000);
    start_services();
    fprintf(stderr, "ridux-session: desktop session supervisor is alive\n");

    for (;;) {
        session_pause_ms(500);
    }
}
