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
    static char *argv[] = {
        "dbus-daemon",
        "--session",
        "--address=unix:path=/run/user/1000/bus",
        "--nofork",
        "--nopidfile",
        0,
    };

    ridux_syscall3(59, (long)"/usr/bin/dbus-daemon", (long)argv, (long)envp);
    ridux_exit(127);
}

__attribute__((naked, noreturn)) void _start(void) {
    __asm__ volatile(
        "mov %rsp, %rdi\n"
        "and $-16, %rsp\n"
        "call ridux_start_c\n"
        "hlt\n");
}
