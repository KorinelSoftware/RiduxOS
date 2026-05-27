typedef unsigned long u64;

static long ridux_syscall3(long n, long a, long b, long c) {
    long ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a), "S"(b), "d"(c)
        : "rcx", "r11", "memory");
    return ret;
}

__attribute__((noreturn)) static void ridux_exit(int code) {
    ridux_syscall3(60, code, 0, 0);
    for (;;) {
    }
}

__attribute__((used, noinline, noreturn)) void ridux_start_c(u64 *initial_sp) {
    u64 argc = initial_sp ? initial_sp[0] : 0;
    char **envp = initial_sp ? (char **)(initial_sp + argc + 2) : 0;
    static char *wofi_argv[] = {
        "wofi",
        "--conf", "/etc/xdg/wofi/config",
        "--style", "/etc/xdg/wofi/style.css",
        "--show", "drun",
        "--allow-images",
        "--insensitive",
        0,
    };

    static char *fallback_argv[] = {
        "ridux-launcher",
        0,
    };

    ridux_syscall3(59, (long)"/usr/bin/wofi", (long)wofi_argv, (long)envp);
    ridux_syscall3(59, (long)"/usr/bin/ridux-launcher", (long)fallback_argv, (long)envp);
    ridux_exit(127);
}

__attribute__((naked, noreturn)) void _start(void) {
    __asm__ volatile(
        "mov %rsp, %rdi\n"
        "and $-16, %rsp\n"
        "call ridux_start_c\n"
        "hlt\n");
}
