/*
 * Tiny Ridux systemctl compatibility helper.
 *
 * Hyprland and shell components invoke a few "systemctl --user" environment
 * commands even when no systemd user manager exists.  Keep this helper out of
 * glibc/ld-linux so it can run before the Linux ABI is complete.
 */
typedef unsigned long usize;
typedef unsigned long uptr;

#define SYS_WRITE 1
#define SYS_EXIT  60

static long sys_write(unsigned long fd, const char *buf, usize len) {
    long ret;
    register long rax __asm__("rax") = SYS_WRITE;
    register long rdi __asm__("rdi") = (long)fd;
    register long rsi __asm__("rsi") = (long)buf;
    register long rdx __asm__("rdx") = (long)len;
    __asm__ volatile("syscall"
                     : "+r"(rax)
                     : "r"(rdi), "r"(rsi), "r"(rdx)
                     : "rcx", "r11", "memory");
    ret = rax;
    return ret;
}

static void sys_exit(int code) __attribute__((noreturn));
static void sys_exit(int code) {
    register long rax __asm__("rax") = SYS_EXIT;
    register long rdi __asm__("rdi") = (long)code;
    __asm__ volatile("syscall"
                     :
                     : "r"(rax), "r"(rdi)
                     : "rcx", "r11", "memory");
    for (;;) {
        __asm__ volatile("pause");
    }
}

static usize c_len(const char *s) {
    usize n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
}

static int c_eq(const char *a, const char *b) {
    usize i = 0;
    if (!a || !b) return 0;
    for (;;) {
        if (a[i] != b[i]) return 0;
        if (!a[i]) return 1;
        ++i;
    }
}

static int is_accepted_command(const char *cmd) {
    static const char *const ok[] = {
        "import-environment",
        "set-environment",
        "unset-environment",
        "daemon-reload",
        "reset-failed",
        "start",
        "stop",
        "restart",
        "reload",
        "try-restart",
        "status",
        "is-active",
        "is-enabled",
        "enable",
        "disable",
        0
    };
    int i;
    for (i = 0; ok[i]; ++i) {
        if (c_eq(cmd, ok[i])) return 1;
    }
    return 0;
}

static void write_lit(unsigned long fd, const char *s) {
    (void)sys_write(fd, s, c_len(s));
}

static int ridux_systemctl_main(int argc, char **argv) {
    const char *cmd = 0;
    int user_mode = 0;
    int i;

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (!arg) continue;
        if (c_eq(arg, "--user")) {
            user_mode = 1;
            continue;
        }
        if (c_eq(arg, "--version")) {
            write_lit(1, "ridux-systemctl 0.2\n");
            return 0;
        }
        if (arg[0] == '-') {
            continue;
        }
        cmd = arg;
        break;
    }

    if (!cmd) {
        write_lit(1, "ridux-systemctl 0.2\n");
        return 0;
    }

    if (c_eq(cmd, "is-system-running")) {
        write_lit(1, "running\n");
        return 0;
    }

    (void)user_mode;
    if (is_accepted_command(cmd)) {
        return 0;
    }

    write_lit(2, "ridux-systemctl: unsupported command: ");
    write_lit(2, cmd);
    write_lit(2, "\n");
    return 1;
}

void _start(void) __attribute__((noreturn));
void _start(void) {
    uptr *sp;
    int argc;
    char **argv;
    __asm__ volatile("mov %%rsp,%0" : "=r"(sp));
    argc = (int)sp[0];
    argv = (char **)&sp[1];
    sys_exit(ridux_systemctl_main(argc, argv));
}
