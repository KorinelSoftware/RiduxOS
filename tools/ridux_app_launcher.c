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

static int rdx_contains(const char *s, const char *needle) {
    usize i, j;
    if (!s || !needle || !needle[0]) return 0;
    for (i = 0; s[i]; ++i) {
        for (j = 0; needle[j]; ++j) {
            if (s[i + j] != needle[j]) break;
        }
        if (!needle[j]) return 1;
    }
    return 0;
}

static const char *rdx_basename(const char *path) {
    const char *base = path ? path : "";
    usize i;
    for (i = 0; path && path[i]; ++i) {
        if (path[i] == '/') base = path + i + 1;
    }
    return base;
}

static void rdx_exec(const char *path, char *const argv[], char **envp) {
    rdx_syscall3(59, (long)path, (long)argv, (long)envp);
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
    const char *name = rdx_basename((argv && argv[0]) ? argv[0] : "");

    static char *files_argv[] = {
        "/usr/bin/thunar",
        "/tmp/hyprland-home",
        0,
    };
    static char *files_fallback_argv[] = {
        "/usr/bin/wofi",
        "--conf", "/etc/xdg/wofi/config",
        "--style", "/etc/xdg/wofi/style.css",
        "--show", "drun",
        "--insensitive",
        0,
    };
    static char *terminal_argv[] = {
        "/usr/bin/foot",
        "--working-directory=/tmp/hyprland-home",
        0,
    };
    static char *terminal_fallback_argv[] = {
        "/opt/wayfire/bin/ridux-terminal",
        0,
    };
    static char *display_argv[] = {
        "/usr/bin/wdisplays",
        0,
    };
    static char *display_fallback_argv[] = {
        "/usr/bin/wofi",
        "--conf", "/etc/xdg/wofi/config",
        "--style", "/etc/xdg/wofi/style.css",
        "--show", "drun",
        "--insensitive",
        0,
    };
    static char *power_argv[] = {
        "/usr/bin/wlogout",
        "--layout", "/etc/wlogout/layout",
        "--css", "/etc/wlogout/style.css",
        0,
    };

    if (rdx_contains(name, "open-files")) {
        rdx_exec("/usr/bin/thunar", files_argv, envp);
        rdx_exec("/usr/bin/wofi", files_fallback_argv, envp);
        rdx_exit(127);
    }
    if (rdx_contains(name, "terminal")) {
        rdx_exec("/usr/bin/foot", terminal_argv, envp);
        rdx_exec("/opt/wayfire/bin/ridux-terminal", terminal_fallback_argv, envp);
        rdx_exit(127);
    }
    if (rdx_contains(name, "display-settings")) {
        rdx_exec("/usr/bin/wdisplays", display_argv, envp);
        rdx_exec("/usr/bin/wofi", display_fallback_argv, envp);
        rdx_exit(127);
    }
    if (rdx_contains(name, "power-menu")) {
        rdx_exec("/usr/bin/wlogout", power_argv, envp);
        rdx_exit(127);
    }

    rdx_exec("/usr/bin/wofi", files_fallback_argv, envp);
    rdx_exit(127);
}

__attribute__((naked, noreturn)) void _start(void) {
    __asm__ volatile(
        "mov %rsp, %rdi\n"
        "and $-16, %rsp\n"
        "call rdx_start_c\n"
        "hlt\n");
}
