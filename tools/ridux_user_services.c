typedef unsigned long u64;
typedef unsigned long usize;

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

static void rdx_write_str(int fd, const char *s) {
    rdx_syscall3(1, fd, (long)s, (long)rdx_strlen(s));
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

static void rdx_sleep_ms(long ms) {
    struct rdx_timespec ts;
    if (ms <= 0) return;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    rdx_syscall3(35, (long)&ts, 0, 0);
}

static void rdx_close_inherited_fds(void) {
    long fd;
    for (fd = 3; fd < 128; ++fd) {
        rdx_syscall3(3, fd, 0, 0);
    }
}

static void rdx_prepare_pulse_runtime(void) {
    rdx_syscall3(83, (long)"/run/user/1000/pulse", 0700, 0);
    rdx_syscall3(87, (long)"/run/user/1000/pulse/native", 0, 0);
}

__attribute__((noreturn)) static void rdx_exit(int code) {
    rdx_syscall3(60, code, 0, 0);
    for (;;) {
    }
}

static void rdx_spawn_after_ms(const char *label,
                               const char *path,
                               char *const argv[],
                               char **envp,
                               long delay_ms) {
    long pid = rdx_syscall3(57, 0, 0, 0);
    if (pid == 0) {
        rdx_sleep_ms(delay_ms);
        if (rdx_streq(label, "/usr/bin/pipewire-pulse")) {
            rdx_prepare_pulse_runtime();
        }
        rdx_close_inherited_fds();
        rdx_syscall3(59, (long)path, (long)argv, (long)envp);
        rdx_write_str(2, "ridux-user-services: exec failed: ");
        rdx_write_str(2, label ? label : path);
        rdx_write_str(2, "\n");
        rdx_exit(127);
    }
}

__attribute__((used, noinline, noreturn)) void rdx_start_c(u64 *initial_sp) {
    u64 argc = initial_sp ? initial_sp[0] : 0;
    char **envp = initial_sp ? (char **)(initial_sp + argc + 2) : 0;

    static char *pipewire_argv[] = { "/usr/bin/pipewire", 0 };
    static char *wireplumber_argv[] = { "/usr/bin/wireplumber", 0 };
    static char *pipewire_pulse_argv[] = { "/usr/bin/pipewire-pulse", 0 };
    static char *permission_store_argv[] = { "/usr/libexec/xdg-permission-store", 0 };
    static char *document_portal_argv[] = { "/usr/libexec/xdg-document-portal", 0 };
    static char *hyprland_portal_argv[] = { "/usr/libexec/xdg-desktop-portal-hyprland", 0 };
    static char *desktop_portal_argv[] = { "/usr/libexec/xdg-desktop-portal", 0 };

    rdx_spawn_after_ms("/usr/bin/pipewire", "/usr/bin/pipewire", pipewire_argv, envp, 700);
    rdx_spawn_after_ms("/usr/bin/wireplumber", "/usr/bin/wireplumber", wireplumber_argv, envp, 2200);
    /*
     * Debian exposes pipewire-pulse as a symlink to pipewire. RiduxFS can
     * stat/read that symlink, but direct execve through it is still stricter
     * than Linux. Execute the real binary with the pulse argv[0] so PipeWire
     * takes the same code path without depending on symlink exec semantics.
     */
    rdx_spawn_after_ms("/usr/libexec/xdg-permission-store", "/usr/libexec/xdg-permission-store", permission_store_argv, envp, 3400);
    rdx_spawn_after_ms("/usr/libexec/xdg-document-portal", "/usr/libexec/xdg-document-portal", document_portal_argv, envp, 3900);
    rdx_spawn_after_ms("/usr/libexec/xdg-desktop-portal-hyprland", "/usr/libexec/xdg-desktop-portal-hyprland", hyprland_portal_argv, envp, 4600);
    rdx_spawn_after_ms("/usr/libexec/xdg-desktop-portal", "/usr/libexec/xdg-desktop-portal", desktop_portal_argv, envp, 5200);
    rdx_spawn_after_ms("/usr/bin/pipewire-pulse", "/usr/bin/pipewire", pipewire_pulse_argv, envp, 6800);

    rdx_exit(0);
}

__attribute__((naked, noreturn)) void _start(void) {
    __asm__ volatile(
        "mov %rsp, %rdi\n"
        "and $-16, %rsp\n"
        "call rdx_start_c\n"
        "hlt\n");
}
