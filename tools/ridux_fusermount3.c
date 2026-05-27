/*
 * ridux_fusermount3.c - minimal fusermount3 helper for RiduxOS FUSE mounts.
 *
 * libfuse starts fusermount3 with _FUSE_COMMFD and waits for a /dev/fuse fd
 * over SCM_RIGHTS. Ridux provides the FUSE device and mount table in-kernel,
 * so this helper performs the mount syscall and returns a fresh /dev/fuse fd
 * through the expected Linux protocol.
 */

typedef unsigned long      usize_t;
typedef unsigned long long u64_t;
typedef long long          i64_t;

#define SYS_WRITE       1
#define SYS_CLOSE       3
#define SYS_SENDMSG    46
#define SYS_EXIT       60
#define SYS_UMOUNT2   166
#define SYS_MOUNT     165
#define SYS_OPENAT    257
#define SYS_EXIT_GROUP 231

#define AT_FDCWD       -100
#define O_RDWR            2
#define O_CLOEXEC   02000000
#define SOL_SOCKET        1
#define SCM_RIGHTS        1

struct rdx_iovec {
    void *iov_base;
    usize_t iov_len;
};

struct rdx_msghdr {
    void *msg_name;
    unsigned int msg_namelen;
    struct rdx_iovec *msg_iov;
    usize_t msg_iovlen;
    void *msg_control;
    usize_t msg_controllen;
    int msg_flags;
};

struct rdx_cmsghdr {
    usize_t cmsg_len;
    int cmsg_level;
    int cmsg_type;
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

static void rdx_exit(int code) {
    rdx_syscall6(SYS_EXIT_GROUP, (u64_t)(unsigned int)code, 0, 0, 0, 0, 0);
    rdx_syscall6(SYS_EXIT, (u64_t)(unsigned int)code, 0, 0, 0, 0, 0);
    for (;;) { }
}

static int rdx_starts_with(const char *s, const char *prefix) {
    usize_t i = 0;
    if (!s || !prefix) return 0;
    while (prefix[i]) {
        if (s[i] != prefix[i]) return 0;
        ++i;
    }
    return 1;
}

static int rdx_parse_int(const char *s) {
    int v = 0;
    if (!s || !*s) return -1;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        ++s;
    }
    return v;
}

static char *rdx_getenv(char **envp, const char *key_eq) {
    int i;
    if (!envp || !key_eq) return 0;
    for (i = 0; envp[i]; ++i) {
        if (rdx_starts_with(envp[i], key_eq)) return envp[i] + rdx_strlen(key_eq);
    }
    return 0;
}

static const char *rdx_mountpoint(int argc, char **argv) {
    int i;
    for (i = argc - 1; i > 0; --i) {
        if (!argv[i] || !argv[i][0]) continue;
        if (argv[i][0] == '-') continue;
        return argv[i];
    }
    return "/run/user/1000/doc";
}

static int rdx_has_unmount(int argc, char **argv) {
    int i;
    for (i = 1; i < argc; ++i) {
        if (argv[i] && argv[i][0] == '-' && argv[i][1] == 'u') return 1;
    }
    return 0;
}

static usize_t rdx_align(usize_t v) {
    usize_t a = sizeof(usize_t) - 1;
    return (v + a) & ~a;
}

static int rdx_send_fd(int commfd, int fd) {
    char byte = 0;
    struct rdx_iovec iov;
    struct rdx_msghdr msg;
    union {
        struct rdx_cmsghdr hdr;
        unsigned char raw[sizeof(struct rdx_cmsghdr) + sizeof(int) + sizeof(usize_t)];
    } control;
    struct rdx_cmsghdr *ch;
    int *data;
    usize_t hdr_len = rdx_align(sizeof(struct rdx_cmsghdr));
    usize_t cmsg_len = hdr_len + sizeof(int);
    usize_t cmsg_space = rdx_align(cmsg_len);

    for (usize_t i = 0; i < sizeof(control.raw); ++i) control.raw[i] = 0;
    ch = (struct rdx_cmsghdr *)control.raw;
    ch->cmsg_len = cmsg_len;
    ch->cmsg_level = SOL_SOCKET;
    ch->cmsg_type = SCM_RIGHTS;
    data = (int *)(control.raw + hdr_len);
    *data = fd;

    iov.iov_base = &byte;
    iov.iov_len = 1;

    msg.msg_name = 0;
    msg.msg_namelen = 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control.raw;
    msg.msg_controllen = cmsg_space;
    msg.msg_flags = 0;

    return (int)rdx_syscall6(SYS_SENDMSG, (u64_t)(unsigned int)commfd,
                             (u64_t)(usize_t)&msg, 0, 0, 0, 0);
}

__attribute__((naked, noreturn)) void _start(void) {
    __asm__ __volatile__(
        "mov %rsp, %rdi\n"
        "and $-16, %rsp\n"
        "call ridux_fusermount3_start\n");
}

void ridux_fusermount3_start(u64_t *stack) {
    int argc = (int)stack[0];
    char **argv = (char **)(usize_t)&stack[1];
    char **envp = argv + argc + 1;
    const char *mnt = rdx_mountpoint(argc, argv);
    char *comm = rdx_getenv(envp, "_FUSE_COMMFD=");
    int commfd = rdx_parse_int(comm);
    int fusefd;

    if (rdx_has_unmount(argc, argv)) {
        (void)rdx_syscall6(SYS_UMOUNT2, (u64_t)(usize_t)mnt, 0, 0, 0, 0, 0);
        rdx_exit(0);
    }

    fusefd = (int)rdx_syscall6(SYS_OPENAT, (u64_t)(unsigned int)AT_FDCWD,
                               (u64_t)(usize_t)"/dev/fuse",
                               O_RDWR | O_CLOEXEC, 0, 0, 0);
    if (fusefd < 0) {
        rdx_write_lit(2, "ridux-fusermount3: cannot open /dev/fuse\n");
        rdx_exit(1);
    }

    (void)rdx_syscall6(SYS_MOUNT, (u64_t)(usize_t)"portal", (u64_t)(usize_t)mnt,
                       (u64_t)(usize_t)"fuse.portal", 0,
                       (u64_t)(usize_t)"rw,nosuid,nodev", 0);

    if (commfd >= 0) {
        int rc = rdx_send_fd(commfd, fusefd);
        if (rc < 0) {
            rdx_write_lit(2, "ridux-fusermount3: cannot send fuse fd\n");
            rdx_exit(1);
        }
    }

    rdx_syscall6(SYS_CLOSE, (u64_t)(unsigned int)fusefd, 0, 0, 0, 0, 0);
    rdx_exit(0);
}
