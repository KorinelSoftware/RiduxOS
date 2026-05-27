/*
 * ridux_hyprland_session.c - tiny syscall-only Hyprland user session helper.
 *
 * Hyprland's exec-once is intentionally small on RiduxOS. This helper exports
 * the Wayland session environment to the real session bus, starts the real
 * PipeWire/WirePlumber user services expected by portals, then supervises
 * Waybar as the first visible shell surface. Portal daemons are D-Bus
 * activatable session services, so this helper deliberately does not launch
 * duplicate portal instances by hand.
 */

typedef unsigned long      usize_t;
typedef unsigned long long u64_t;
typedef long long          i64_t;

#define SYS_WRITE       1
#define SYS_CLONE      56
#define SYS_EXECVE     59
#define SYS_WAIT4      61
#define SYS_NANOSLEEP  35
#define SYS_EXIT       60
#define SYS_EXIT_GROUP 231

#define RDX_SIGCHLD   17
#define RDX_WNOHANG    1
#define RIDUX_SESSION_TRACE 0

struct rdx_timespec {
    long tv_sec;
    long tv_nsec;
};

static inline i64_t rdx_syscall6(u64_t nr, u64_t a0, u64_t a1, u64_t a2,
                                 u64_t a3, u64_t a4, u64_t a5) {
    i64_t ret;
    register u64_t r10 __asm__("r10") = a3;
    register u64_t r8  __asm__("r8")  = a4;
    register u64_t r9  __asm__("r9")  = a5;
    __asm__ __volatile__(
        "syscall"
        : "=a"(ret)
        : "0"(nr), "D"(a0), "S"(a1), "d"(a2), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory");
    return ret;
}

static usize_t rdx_strlen(const char *s) {
    usize_t n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
}

static void rdx_write_lit(int fd, const char *s) {
    rdx_syscall6(SYS_WRITE, (u64_t)(unsigned int)fd, (u64_t)(usize_t)s,
                 (u64_t)rdx_strlen(s), 0, 0, 0);
}

static const char *rdx_env_get(char **envp, const char *key) {
    usize_t i;
    usize_t klen = rdx_strlen(key);
    if (!envp || !key || !klen) return 0;
    for (i = 0; envp[i]; ++i) {
        usize_t j;
        for (j = 0; j < klen; ++j) {
            if (envp[i][j] != key[j]) break;
        }
        if (j == klen && envp[i][j] == '=') return envp[i] + j + 1;
    }
    return 0;
}

static void rdx_log_env_value(const char *key, char **envp) {
    const char *value;
    if (!RIDUX_SESSION_TRACE) return;
    value = rdx_env_get(envp, key);
    rdx_write_lit(2, "ridux-hyprland-session: env ");
    rdx_write_lit(2, key);
    rdx_write_lit(2, "=");
    rdx_write_lit(2, value ? value : "(missing)");
    rdx_write_lit(2, "\n");
}

static int rdx_env_key_matches(const char *entry, const char *key) {
    usize_t i = 0;
    if (!entry || !key) return 0;
    while (key[i]) {
        if (entry[i] != key[i]) return 0;
        ++i;
    }
    return entry[i] == '=';
}

static int rdx_env_is_forced_key(const char *entry) {
    return rdx_env_key_matches(entry, "XDG_CURRENT_DESKTOP") ||
           rdx_env_key_matches(entry, "XDG_SESSION_DESKTOP") ||
           rdx_env_key_matches(entry, "XDG_SESSION_TYPE") ||
           rdx_env_key_matches(entry, "DESKTOP_SESSION") ||
           rdx_env_key_matches(entry, "HOME") ||
           rdx_env_key_matches(entry, "USER") ||
           rdx_env_key_matches(entry, "LOGNAME") ||
           rdx_env_key_matches(entry, "GDK_BACKEND") ||
           rdx_env_key_matches(entry, "GTK_USE_PORTAL") ||
           rdx_env_key_matches(entry, "NO_AT_BRIDGE") ||
           rdx_env_key_matches(entry, "DBUS_SESSION_BUS_ADDRESS") ||
           rdx_env_key_matches(entry, "GIO_USE_PORTALS") ||
           rdx_env_key_matches(entry, "GIO_USE_VFS") ||
           rdx_env_key_matches(entry, "GIO_USE_VOLUME_MONITOR") ||
           rdx_env_key_matches(entry, "GSETTINGS_BACKEND") ||
           rdx_env_key_matches(entry, "PIPEWIRE_RUNTIME_DIR") ||
           rdx_env_key_matches(entry, "XDG_RUNTIME_DIR") ||
           rdx_env_key_matches(entry, "XDG_CONFIG_HOME") ||
           rdx_env_key_matches(entry, "XDG_CACHE_HOME") ||
           rdx_env_key_matches(entry, "XDG_DATA_HOME") ||
           rdx_env_key_matches(entry, "XDG_STATE_HOME") ||
           rdx_env_key_matches(entry, "WAYLAND_DISPLAY");
}

static char **rdx_session_env(char **envp) {
    static char *session_env[192];
    usize_t n = 0;
    usize_t i;
    if (envp) {
        for (i = 0; envp[i] && n + 32 < 192; ++i) {
            if (!rdx_env_is_forced_key(envp[i])) session_env[n++] = envp[i];
        }
    }
    session_env[n++] = "USER=ridux";
    session_env[n++] = "LOGNAME=ridux";
    session_env[n++] = "HOME=/tmp/hyprland-home";
    session_env[n++] = "XDG_CURRENT_DESKTOP=Hyprland";
    session_env[n++] = "XDG_SESSION_DESKTOP=Hyprland";
    session_env[n++] = "XDG_SESSION_TYPE=wayland";
    session_env[n++] = "DESKTOP_SESSION=hyprland";
    session_env[n++] = "XDG_RUNTIME_DIR=/run/user/1000";
    session_env[n++] = "XDG_CONFIG_HOME=/tmp/hyprland-home/config";
    session_env[n++] = "XDG_CACHE_HOME=/tmp/hyprland-home/cache";
    session_env[n++] = "XDG_DATA_HOME=/tmp/hyprland-home/share";
    session_env[n++] = "XDG_STATE_HOME=/tmp/hyprland-home/state";
    session_env[n++] = "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus";
    session_env[n++] = "GDK_BACKEND=wayland";
    session_env[n++] = "GTK_USE_PORTAL=0";
    session_env[n++] = "NO_AT_BRIDGE=1";
    session_env[n++] = "GIO_USE_VFS=local";
    session_env[n++] = "GIO_USE_VOLUME_MONITOR=unix";
    session_env[n++] = "GSETTINGS_BACKEND=memory";
    session_env[n++] = "PIPEWIRE_RUNTIME_DIR=/run/user/1000";
    session_env[n++] = "WAYLAND_DISPLAY=wayland-1";
    session_env[n] = 0;
    return session_env;
}

static i64_t rdx_spawn(char *const argv[], char **envp) {
    i64_t pid = rdx_syscall6(SYS_CLONE, RDX_SIGCHLD, 0, 0, 0, 0, 0);
    if (pid == 0) {
        rdx_syscall6(SYS_EXECVE, (u64_t)(usize_t)argv[0],
                     (u64_t)(usize_t)argv, (u64_t)(usize_t)envp, 0, 0, 0);
        rdx_syscall6(SYS_EXIT_GROUP, 127, 0, 0, 0, 0, 0);
        rdx_syscall6(SYS_EXIT, 127, 0, 0, 0, 0, 0);
        for (;;) { }
    }
    return pid;
}

static i64_t rdx_wait_any_nonblock(int *status) {
    return rdx_syscall6(SYS_WAIT4, (u64_t)-1, (u64_t)(usize_t)status,
                        RDX_WNOHANG, 0, 0, 0);
}

static void rdx_sleep_ms(unsigned int ms) {
    struct rdx_timespec ts;
    ts.tv_sec = (long)(ms / 1000U);
    ts.tv_nsec = (long)(ms % 1000U) * 1000000L;
    rdx_syscall6(SYS_NANOSLEEP, (u64_t)(usize_t)&ts, 0, 0, 0, 0, 0);
}

struct ridux_service {
    const char *name;
    char *const *argv;
    i64_t pid;
    unsigned restarts;
    unsigned max_restarts;
    unsigned settle_ms;
};

static void rdx_log_service(const char *prefix, const char *name) {
    if (!RIDUX_SESSION_TRACE) return;
    rdx_write_lit(2, "ridux-hyprland-session: ");
    rdx_write_lit(2, prefix);
    rdx_write_lit(2, " ");
    rdx_write_lit(2, name ? name : "(service)");
    rdx_write_lit(2, "\n");
}

static void rdx_start_service(struct ridux_service *svc, char **envp) {
    if (!svc || svc->pid > 0) return;
    if (svc->max_restarts && svc->restarts >= svc->max_restarts) return;
    rdx_log_service("starting", svc->name);
    svc->pid = rdx_spawn(svc->argv, envp);
    if (svc->pid <= 0) {
        svc->pid = 0;
        ++svc->restarts;
    }
    if (svc->settle_ms) rdx_sleep_ms(svc->settle_ms);
}

static void rdx_mark_service_exit(struct ridux_service *services, usize_t count,
                                  i64_t pid) {
    usize_t i;
    if (pid <= 0) return;
    for (i = 0; i < count; ++i) {
        if (services[i].pid == pid) {
            services[i].pid = 0;
            ++services[i].restarts;
            rdx_log_service("exited", services[i].name);
            return;
        }
    }
}

__attribute__((naked, noreturn)) void _start(void) {
    __asm__ __volatile__(
        "mov %rsp, %rdi\n"
        "and $-16, %rsp\n"
        "call ridux_hyprland_session_start\n");
}

void ridux_hyprland_session_start(u64_t *stack) {
    int argc = (int)stack[0];
    char **argv = (char **)(usize_t)&stack[1];
    char **envp = argv + argc + 1;
    char **session_env = rdx_session_env(envp);

    char *const dbus_update_env[] = {
        "/usr/bin/dbus-update-activation-environment",
        "--systemd",
        "WAYLAND_DISPLAY",
        "HYPRLAND_INSTANCE_SIGNATURE",
        "XDG_CURRENT_DESKTOP",
        "XDG_SESSION_DESKTOP",
        "XDG_SESSION_TYPE",
        "DESKTOP_SESSION",
        "GDK_BACKEND",
        "GTK_USE_PORTAL",
        "NO_AT_BRIDGE",
        "DBUS_SESSION_BUS_ADDRESS",
        "GIO_USE_PORTALS",
        "GIO_USE_VFS",
        "GIO_USE_VOLUME_MONITOR",
        "GSETTINGS_BACKEND",
        "PATH",
        "LD_LIBRARY_PATH",
        "XDG_DATA_DIRS",
        "XDG_CONFIG_DIRS",
        "XDG_RUNTIME_DIR",
        "XDG_CONFIG_HOME",
        "XDG_CACHE_HOME",
        "XDG_DATA_HOME",
        "XDG_STATE_HOME",
        "PIPEWIRE_RUNTIME_DIR",
        "XCURSOR_THEME",
        "XCURSOR_SIZE",
        0
    };
    char *const pipewire[] = {
        "/usr/bin/pipewire",
        0
    };
    char *const wireplumber[] = {
        "/usr/bin/wireplumber",
        0
    };
    /*
     * Waybar is launched directly by exec-once in hyprland.conf via
     * /usr/bin/ridux-waybar. Do NOT start it here as well — duplicate
     * layer-shell surfaces saturate the compositor render loop and
     * block mouse input processing.
     */
    struct ridux_service services[] = {
        { "pipewire", pipewire, 0, 0, 4, 500 },
        { "wireplumber", wireplumber, 0, 0, 4, 250 },
    };
    const usize_t service_count = sizeof(services) / sizeof(services[0]);
    (void)argc;

    if (RIDUX_SESSION_TRACE) rdx_write_lit(2, "ridux-hyprland-session: helper ready\n");
    rdx_spawn(dbus_update_env, session_env);
    if (RIDUX_SESSION_TRACE) rdx_write_lit(2, "ridux-hyprland-session: dbus env publish spawned\n");
    rdx_log_env_value("WAYLAND_DISPLAY", session_env);
    rdx_log_env_value("HYPRLAND_INSTANCE_SIGNATURE", session_env);

    for (;;) {
        usize_t i;
        int status = 0;
        i64_t exited;

        for (i = 0; i < service_count; ++i) {
            rdx_start_service(&services[i], session_env);
        }

        for (;;) {
            exited = rdx_wait_any_nonblock(&status);
            if (exited <= 0) break;
            rdx_mark_service_exit(services, service_count, exited);
        }

        rdx_sleep_ms(250);
    }
}
