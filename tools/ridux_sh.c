/*
 * ridux_sh.c - tiny freestanding /bin/sh for RiduxOS Linux ABI bring-up.
 *
 * This deliberately avoids glibc and the dynamic loader. Hyprland and other
 * real Linux desktop clients use /bin/sh -c for autostart/probes; keeping this
 * shell syscall-only prevents shell startup from depending on lazy PLT/IFUNC
 * paths while still exercising RiduxOS' real exec/pipe/fork ABI.
 */

typedef unsigned long      usize_t;
typedef unsigned long long u64_t;
typedef long long          i64_t;

#define RIDUX_SH_MAX_ARGS    64
#define RIDUX_SH_MAX_STAGES   8
#define RIDUX_SH_PATH_MAX   512

#define SYS_WRITE        1
#define SYS_CLOSE        3
#define SYS_PIPE        22
#define SYS_DUP2        33
#define SYS_FORK        57
#define SYS_EXECVE      59
#define SYS_EXIT        60
#define SYS_WAIT4       61
#define SYS_EXIT_GROUP 231

static inline i64_t sh_syscall6(u64_t nr, u64_t a0, u64_t a1, u64_t a2,
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

static void sh_exit(int code) {
    sh_syscall6(SYS_EXIT_GROUP, (u64_t)(unsigned int)code, 0, 0, 0, 0, 0);
    sh_syscall6(SYS_EXIT, (u64_t)(unsigned int)code, 0, 0, 0, 0, 0);
    for (;;) { }
}

__attribute__((naked, noreturn)) void _start(void) {
    __asm__ __volatile__(
        "mov %rsp, %rdi\n"
        "and $-16, %rsp\n"
        "call ridux_sh_start\n");
}

static usize_t sh_strlen(const char *s) {
    usize_t n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
}

static int sh_streq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static int sh_starts_word(const char *s, const char *word) {
    usize_t i = 0;
    if (!s || !word) return 0;
    while (word[i]) {
        if (s[i] != word[i]) return 0;
        ++i;
    }
    return s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n';
}

static int sh_has_slash(const char *s) {
    if (!s) return 0;
    while (*s) {
        if (*s == '/') return 1;
        ++s;
    }
    return 0;
}

static int sh_isspace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static char *sh_trim(char *s) {
    char *e;
    if (!s) return s;
    while (*s && sh_isspace(*s)) ++s;
    e = s + sh_strlen(s);
    while (e > s && sh_isspace(e[-1])) --e;
    *e = 0;
    return s;
}

static void sh_write_lit(int fd, const char *s) {
    sh_syscall6(SYS_WRITE, (u64_t)(unsigned int)fd, (u64_t)(usize_t)s,
                (u64_t)sh_strlen(s), 0, 0, 0);
}

static char *sh_getenv(char **envp, const char *name) {
    usize_t n = sh_strlen(name);
    int i;
    if (!envp || !name) return 0;
    for (i = 0; envp[i]; ++i) {
        usize_t j;
        for (j = 0; j < n; ++j) {
            if (envp[i][j] != name[j]) break;
        }
        if (j == n && envp[i][j] == '=') return envp[i] + j + 1;
    }
    return 0;
}

static int sh_make_path(char *out, usize_t cap, const char *dir, const char *cmd) {
    usize_t i = 0;
    if (!out || cap < 2 || !dir || !cmd) return -1;
    while (i + 1 < cap && dir[i]) {
        out[i] = dir[i];
        ++i;
    }
    if (i == 0 || out[i - 1] != '/') {
        if (i + 1 >= cap) return -1;
        out[i++] = '/';
    }
    while (i + 1 < cap && *cmd) out[i++] = *cmd++;
    if (*cmd) return -1;
    out[i] = 0;
    return 0;
}

static int sh_split_words(char *cmd, char **out) {
    int argc = 0;
    char *p = cmd;
    if (!cmd || !out) return 0;
    while (*p && argc < RIDUX_SH_MAX_ARGS - 1) {
        char quote = 0;
        while (*p && sh_isspace(*p)) ++p;
        if (!*p) break;
        if (*p == '"' || *p == '\'') quote = *p++;
        out[argc++] = p;
        while (*p) {
            if (quote) {
                if (*p == quote) {
                    *p++ = 0;
                    break;
                }
            } else if (sh_isspace(*p)) {
                *p++ = 0;
                break;
            }
            ++p;
        }
    }
    out[argc] = 0;
    return argc;
}

static int sh_split_pipeline(char *cmd, char **stages) {
    int count = 0;
    char quote = 0;
    char *p;
    if (!cmd || !stages) return 0;
    stages[count++] = sh_trim(cmd);
    for (p = cmd; *p && count < RIDUX_SH_MAX_STAGES; ++p) {
        if (quote) {
            if (*p == quote) quote = 0;
            continue;
        }
        if (*p == '"' || *p == '\'') {
            quote = *p;
            continue;
        }
        if (*p == '|') {
            *p = 0;
            stages[count++] = sh_trim(p + 1);
        }
    }
    return count;
}

static int sh_exec_direct(char *file, char **argv, char **envp) {
    char *path;
    char candidate[RIDUX_SH_PATH_MAX];
    if (!file || !file[0]) return -2;
    if (sh_has_slash(file)) {
        sh_syscall6(SYS_EXECVE, (u64_t)(usize_t)file, (u64_t)(usize_t)argv,
                    (u64_t)(usize_t)envp, 0, 0, 0);
        return -1;
    }
    path = sh_getenv(envp, "PATH");
    if (!path || !path[0]) {
        path = (char *)"/usr/local/bin:/usr/bin:/bin:/opt/hyprland/bin:/opt/hyprland/usr/bin";
    }
    while (*path) {
        char *dir = path;
        char saved = 0;
        while (*path && *path != ':') ++path;
        if (*path == ':') {
            saved = *path;
            *path = 0;
        }
        if (dir[0] && sh_make_path(candidate, sizeof(candidate), dir, file) == 0) {
            sh_syscall6(SYS_EXECVE, (u64_t)(usize_t)candidate, (u64_t)(usize_t)argv,
                        (u64_t)(usize_t)envp, 0, 0, 0);
        }
        if (saved) {
            *path = saved;
            ++path;
        }
    }
    return -1;
}

static int sh_run_single(char *cmd, char **envp) {
    char *argv[RIDUX_SH_MAX_ARGS];
    int argc;
    cmd = sh_trim(cmd);
    if (sh_starts_word(cmd, "exec")) cmd = sh_trim(cmd + 4);
    argc = sh_split_words(cmd, argv);
    if (argc <= 0) return 0;
    sh_exec_direct(argv[0], argv, envp);
    sh_write_lit(2, "ridux-sh: exec failed: ");
    sh_write_lit(2, argv[0]);
    sh_write_lit(2, "\n");
    return 127;
}

static int sh_wait_all(int *pids, int count) {
    int i;
    int rc = 0;
    for (i = 0; i < count; ++i) {
        int status = 0;
        i64_t w = sh_syscall6(SYS_WAIT4, (u64_t)(unsigned int)pids[i],
                              (u64_t)(usize_t)&status, 0, 0, 0, 0);
        if (w >= 0) rc = status;
    }
    if ((rc & 0x7f) == 0) return (rc >> 8) & 0xff;
    return 128 + (rc & 0x7f);
}

static int sh_run_pipeline(char **stages, int count, char **envp) {
    int pipes[RIDUX_SH_MAX_STAGES - 1][2];
    int pids[RIDUX_SH_MAX_STAGES];
    int i;
    if (count <= 0) return 0;
    if (count == 1) return sh_run_single(stages[0], envp);
    for (i = 0; i < count - 1; ++i) {
        if (sh_syscall6(SYS_PIPE, (u64_t)(usize_t)pipes[i], 0, 0, 0, 0, 0) < 0) {
            sh_write_lit(2, "ridux-sh: pipe failed\n");
            return 127;
        }
    }
    for (i = 0; i < count; ++i) {
        i64_t pid = sh_syscall6(SYS_FORK, 0, 0, 0, 0, 0, 0);
        if (pid == 0) {
            int j;
            char *argv[RIDUX_SH_MAX_ARGS];
            int argc;
            if (i > 0) {
                sh_syscall6(SYS_DUP2, (u64_t)(unsigned int)pipes[i - 1][0], 0, 0, 0, 0, 0);
            }
            if (i + 1 < count) {
                sh_syscall6(SYS_DUP2, (u64_t)(unsigned int)pipes[i][1], 1, 0, 0, 0, 0);
            }
            for (j = 0; j < count - 1; ++j) {
                sh_syscall6(SYS_CLOSE, (u64_t)(unsigned int)pipes[j][0], 0, 0, 0, 0, 0);
                sh_syscall6(SYS_CLOSE, (u64_t)(unsigned int)pipes[j][1], 0, 0, 0, 0, 0);
            }
            argc = sh_split_words(stages[i], argv);
            if (argc > 0) sh_exec_direct(argv[0], argv, envp);
            sh_write_lit(2, "ridux-sh: pipeline exec failed\n");
            sh_exit(127);
        }
        if (pid < 0) {
            sh_write_lit(2, "ridux-sh: fork failed\n");
            return 127;
        }
        pids[i] = (int)pid;
    }
    for (i = 0; i < count - 1; ++i) {
        sh_syscall6(SYS_CLOSE, (u64_t)(unsigned int)pipes[i][0], 0, 0, 0, 0, 0);
        sh_syscall6(SYS_CLOSE, (u64_t)(unsigned int)pipes[i][1], 0, 0, 0, 0, 0);
    }
    return sh_wait_all(pids, count);
}

static int sh_run_command(char *cmd, char **envp) {
    char *stages[RIDUX_SH_MAX_STAGES];
    int count;
    if (!cmd) return 0;
    count = sh_split_pipeline(cmd, stages);
    return sh_run_pipeline(stages, count, envp);
}

static int sh_main(int argc, char **argv, char **envp) {
    if (argc >= 3 && sh_streq(argv[1], "-c")) {
        return sh_run_command(argv[2], envp);
    }
    if (argc >= 2) {
        sh_exec_direct(argv[1], &argv[1], envp);
        sh_write_lit(2, "ridux-sh: exec failed: ");
        sh_write_lit(2, argv[1]);
        sh_write_lit(2, "\n");
        return 127;
    }
    sh_write_lit(2, "ridux-sh: interactive shell is not implemented yet\n");
    return 0;
}

void ridux_sh_start(u64_t *stack) {
    int argc = (int)stack[0];
    char **argv = (char **)(usize_t)&stack[1];
    char **envp = argv + argc + 1;
    sh_exit(sh_main(argc, argv, envp));
}
