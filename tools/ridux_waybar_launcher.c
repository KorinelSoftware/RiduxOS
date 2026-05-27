typedef unsigned long u64;
typedef unsigned long usize;

struct rdx_dirent64 {
    u64 d_ino;
    long d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

struct rdx_timespec {
    long tv_sec;
    long tv_nsec;
};

static long rdx_syscall3(long n, long a, long b, long c) {
    long ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a), "S"(b), "d"(c)
        : "rcx", "r11", "memory");
    return ret;
}

static usize rdx_strlen(const char *s) {
    usize n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
}

static int rdx_streq(const char *a, const char *b) {
    usize i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        ++i;
    }
    return a[i] == b[i];
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

static char *rdx_find_env(char **envp, const char *key) {
    usize i;
    if (!envp) return 0;
    for (i = 0; envp[i] && i < 256; ++i) {
        if (rdx_env_key_matches(envp[i], key)) return envp[i];
    }
    return 0;
}

static char *rdx_signature_from_dir(const char *dir) {
    static char env[160];
    char buf[1024];
    long fd;
    long nread;
    long off = 0;
    const char prefix[] = "HYPRLAND_INSTANCE_SIGNATURE=";
    usize i;

    fd = rdx_syscall3(2, (long)dir, 0, 0);
    if (fd < 0) return 0;
    nread = rdx_syscall3(217, fd, (long)buf, sizeof(buf));
    rdx_syscall3(3, fd, 0, 0);
    if (nread <= 0) return 0;

    while (off < nread) {
        struct rdx_dirent64 *d = (struct rdx_dirent64 *)(buf + off);
        const char *name = d->d_name;
        usize pos = 0;
        if (d->d_reclen == 0) break;
        off += d->d_reclen;
        if (rdx_streq(name, ".") || rdx_streq(name, "..")) continue;
        for (i = 0; prefix[i] && pos + 1 < sizeof(env); ++i) env[pos++] = prefix[i];
        for (i = 0; name[i] && pos + 1 < sizeof(env); ++i) env[pos++] = name[i];
        env[pos] = 0;
        return env;
    }
    return 0;
}

static char *rdx_hyprland_signature_env(char **envp) {
    struct rdx_timespec ts;
    char *sig;
    int attempt;

    sig = rdx_find_env(envp, "HYPRLAND_INSTANCE_SIGNATURE");
    if (sig) return sig;

    ts.tv_sec = 0;
    ts.tv_nsec = 50000000;
    for (attempt = 0; attempt < 20; ++attempt) {
        sig = rdx_signature_from_dir("/run/user/1000/hypr");
        if (sig) return sig;
        sig = rdx_signature_from_dir("/tmp/hypr");
        if (sig) return sig;
        rdx_syscall3(35, (long)&ts, 0, 0);
    }
    return 0;
}

static char *rdx_build_wayland_display_env(char **envp) {
    static char wl_env[64];
    char *entry = rdx_find_env(envp, "WAYLAND_DISPLAY");
    if (entry) return entry; /* already has KEY=VALUE form */
    /* fallback */
    {
        const char prefix[] = "WAYLAND_DISPLAY=wayland-1";
        usize i;
        for (i = 0; prefix[i] && i + 1 < sizeof(wl_env); ++i) wl_env[i] = prefix[i];
        wl_env[i] = 0;
    }
    return wl_env;
}

static char **rdx_waybar_env(char **envp) {
    static char *out[192];
    char *hypr_sig;
    usize n = 0;
    out[n++] = "HOME=/tmp/hyprland-home";
    out[n++] = "USER=ridux";
    out[n++] = "LOGNAME=ridux";
    out[n++] = rdx_build_wayland_display_env(envp);
    out[n++] = "XDG_RUNTIME_DIR=/run/user/1000";
    out[n++] = "XDG_CONFIG_HOME=/tmp/hyprland-waybar/config";
    out[n++] = "XDG_CACHE_HOME=/tmp/hyprland-waybar/cache";
    out[n++] = "XDG_DATA_HOME=/tmp/hyprland-waybar/share";
    out[n++] = "XDG_STATE_HOME=/tmp/hyprland-waybar/state";
    out[n++] = "XDG_CONFIG_DIRS=/etc/xdg";
    out[n++] = "XDG_DATA_DIRS=/tmp/hyprland-home/share:/usr/local/share:/usr/share";
    out[n++] = "LD_LIBRARY_PATH=/opt/hyprland/lib:/opt/hyprland/lib/x86_64-linux-gnu:/opt/hyprland/lib64:/opt/hyprland/usr/lib:/opt/hyprland/usr/lib/x86_64-linux-gnu:/lib64:/lib:/usr/lib:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu";
    out[n++] = "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
    out[n++] = "LANG=C.UTF-8";
    out[n++] = "FONTCONFIG_FILE=/etc/fonts/fonts.conf";
    out[n++] = "FONTCONFIG_PATH=/etc/fonts";
    out[n++] = "XDG_CURRENT_DESKTOP=Hyprland";
    out[n++] = "XDG_SESSION_DESKTOP=Hyprland";
    out[n++] = "XDG_SESSION_TYPE=wayland";
    out[n++] = "DESKTOP_SESSION=hyprland";
    hypr_sig = rdx_hyprland_signature_env(envp);
    if (hypr_sig) out[n++] = hypr_sig;
    out[n++] = "DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/dbus-ridux-waybar";
    out[n++] = "GDK_BACKEND=wayland";
    out[n++] = "GTK_THEME=Adwaita";
    out[n++] = "XCURSOR_THEME=Adwaita";
    out[n++] = "XCURSOR_SIZE=28";
    out[n++] = "HYPRCURSOR_THEME=Adwaita";
    out[n++] = "HYPRCURSOR_SIZE=28";
    out[n++] = "XCURSOR_PATH=/usr/share/icons:/usr/share/pixmaps";
    out[n++] = "HYPRCURSOR_PATH=/usr/share/icons";
    out[n++] = "PIPEWIRE_RUNTIME_DIR=/run/user/1000";
    out[n++] = "PULSE_RUNTIME_PATH=/run/user/1000/pulse";
    out[n++] = "PULSE_SERVER=unix:/run/user/1000/pulse/native";
    out[n++] = "GIO_USE_VFS=local";
    out[n++] = "GIO_USE_VOLUME_MONITOR=unix";
    out[n++] = "GSETTINGS_BACKEND=memory";
    out[n++] = "NO_AT_BRIDGE=1";
    out[n++] = "RIDUX_WAYBAR_ISOLATED=1";
    out[n] = 0;
    return out;
}

static void rdx_write_lit(int fd, const char *s) {
    rdx_syscall3(1, fd, (long)s, (long)rdx_strlen(s));
}

static void rdx_close_inherited_fds(void) {
    long fd;
    for (fd = 3; fd < 128; ++fd) {
        rdx_syscall3(3, fd, 0, 0);
    }
}

static void rdx_exit(int code) {
    rdx_syscall3(60, code, 0, 0);
    for (;;) {
    }
}

__attribute__((used, noinline)) void rdx_start_c(u64 *initial_sp) {
    u64 argc = initial_sp ? initial_sp[0] : 0;
    char **envp = initial_sp ? (char **)(initial_sp + argc + 2) : 0;
    char **clean_env = rdx_waybar_env(envp);
    static char *argv[] = {
        "waybar",
        "-l", "warning",
        "-c", "/etc/xdg/waybar/config",
        "-s", "/etc/xdg/waybar/style.css",
        0,
    };

    rdx_write_lit(2, "ridux-waybar: starting real Waybar\n");
    rdx_close_inherited_fds();
    rdx_syscall3(59, (long)"/usr/bin/waybar", (long)argv, (long)clean_env);
    rdx_exit(127);
}

__attribute__((naked)) void _start(void) {
    __asm__ volatile(
        "mov %rsp, %rdi\n"
        "and $-16, %rsp\n"
        "call rdx_start_c\n"
        "hlt\n");
}
