typedef unsigned long u64;
typedef unsigned long usize;

static long rdx_syscall3(long n, long a, long b, long c) {
    long ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a), "S"(b), "d"(c)
        : "rcx", "r11", "memory");
    return ret;
}

static int rdx_env_key_matches(const char *entry, const char *key) {
    usize i = 0;
    if (!entry || !key) return 0;
    while (key[i]) {
        if (entry[i] != key[i]) return 0;
        ++i;
    }
    return entry[i] == '=';
}

static int rdx_env_should_drop(const char *entry) {
    return rdx_env_key_matches(entry, "DBUS_SESSION_BUS_ADDRESS") ||
           rdx_env_key_matches(entry, "XDG_RUNTIME_DIR") ||
           rdx_env_key_matches(entry, "XDG_CONFIG_HOME") ||
           rdx_env_key_matches(entry, "XDG_CACHE_HOME") ||
           rdx_env_key_matches(entry, "XDG_DATA_HOME") ||
           rdx_env_key_matches(entry, "XDG_STATE_HOME") ||
           rdx_env_key_matches(entry, "GDK_BACKEND") ||
           rdx_env_key_matches(entry, "GTK_USE_PORTAL") ||
           rdx_env_key_matches(entry, "FONTCONFIG_FILE") ||
           rdx_env_key_matches(entry, "FONTCONFIG_PATH") ||
           rdx_env_key_matches(entry, "GSETTINGS_BACKEND") ||
           rdx_env_key_matches(entry, "GIO_USE_VFS") ||
           rdx_env_key_matches(entry, "GIO_USE_VOLUME_MONITOR") ||
           rdx_env_key_matches(entry, "NO_AT_BRIDGE") ||
           rdx_env_key_matches(entry, "XCURSOR_THEME") ||
           rdx_env_key_matches(entry, "XCURSOR_SIZE") ||
           rdx_env_key_matches(entry, "HYPRCURSOR_THEME") ||
           rdx_env_key_matches(entry, "HYPRCURSOR_SIZE") ||
           rdx_env_key_matches(entry, "XCURSOR_PATH") ||
           rdx_env_key_matches(entry, "HYPRCURSOR_PATH") ||
           rdx_env_key_matches(entry, "PIPEWIRE_RUNTIME_DIR") ||
           rdx_env_key_matches(entry, "PULSE_RUNTIME_PATH") ||
           rdx_env_key_matches(entry, "PULSE_SERVER") ||
           rdx_env_key_matches(entry, "SHELL") ||
           rdx_env_key_matches(entry, "TERM");
}

static char **rdx_session_env(char **envp) {
    static char *out[224];
    usize n = 0;
    usize i;
    if (envp) {
        for (i = 0; envp[i] && n + 24 < 224; ++i) {
            if (!rdx_env_should_drop(envp[i])) out[n++] = envp[i];
        }
    }
    out[n++] = "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus";
    out[n++] = "XDG_RUNTIME_DIR=/run/user/1000";
    out[n++] = "XDG_CONFIG_HOME=/tmp/hyprland-home/config";
    out[n++] = "XDG_CACHE_HOME=/tmp/hyprland-home/cache";
    out[n++] = "XDG_DATA_HOME=/tmp/hyprland-home/share";
    out[n++] = "XDG_STATE_HOME=/tmp/hyprland-home/state";
    out[n++] = "FONTCONFIG_FILE=/etc/fonts/fonts.conf";
    out[n++] = "FONTCONFIG_PATH=/etc/fonts";
    out[n++] = "GDK_BACKEND=wayland";
    out[n++] = "GSETTINGS_BACKEND=memory";
    out[n++] = "GIO_USE_VFS=local";
    out[n++] = "GIO_USE_VOLUME_MONITOR=unix";
    out[n++] = "NO_AT_BRIDGE=1";
    out[n++] = "XCURSOR_THEME=Adwaita";
    out[n++] = "XCURSOR_SIZE=28";
    out[n++] = "HYPRCURSOR_THEME=Adwaita";
    out[n++] = "HYPRCURSOR_SIZE=28";
    out[n++] = "XCURSOR_PATH=/usr/share/icons:/usr/share/pixmaps";
    out[n++] = "HYPRCURSOR_PATH=/usr/share/icons";
    out[n++] = "PIPEWIRE_RUNTIME_DIR=/run/user/1000";
    out[n++] = "PULSE_RUNTIME_PATH=/run/user/1000/pulse";
    out[n++] = "PULSE_SERVER=unix:/run/user/1000/pulse/native";
    out[n++] = "SHELL=/bin/sh";
    out[n++] = "TERM=xterm-256color";
    out[n] = 0;
    return out;
}

static void rdx_close_inherited_fds(void) {
    long fd;
    for (fd = 3; fd < 128; ++fd) {
        rdx_syscall3(3, fd, 0, 0);
    }
}

__attribute__((noreturn)) static void rdx_exit(int code) {
    rdx_syscall3(60, code, 0, 0);
    for (;;) {
    }
}

__attribute__((used, noinline, noreturn)) void rdx_start_c(u64 *initial_sp) {
    u64 argc = initial_sp ? initial_sp[0] : 0;
    char **argv = initial_sp ? (char **)(initial_sp + 1) : 0;
    char **envp = initial_sp ? (char **)(initial_sp + argc + 2) : 0;

    if (argc < 2 || !argv[1]) rdx_exit(64);
    rdx_close_inherited_fds();
    rdx_syscall3(59, (long)argv[1], (long)(argv + 1), (long)rdx_session_env(envp));
    rdx_exit(127);
}

__attribute__((naked, noreturn)) void _start(void) {
    __asm__ volatile(
        "mov %rsp, %rdi\n"
        "and $-16, %rsp\n"
        "call rdx_start_c\n"
        "hlt\n");
}
