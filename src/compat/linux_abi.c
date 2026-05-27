/*
 * Syscalls modernas que suelen pedir apps grandes como navegadores.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "base.h"
#include "memory_tasks.h"
#include "linux_syscalls.h"
#include "user_libc.h"
#include "bsd_libc.h"
#include "linux_abi.h"

/* local helpers */
static void *c6_memset(void *d, int v, size_t n) {
    uint8_t *p = (uint8_t*)d;
    size_t i;
    for (i = 0; i < n; ++i) p[i] = (uint8_t)v;
    return d;
}
static void *c6_memcpy(void *d, const void *s, size_t n) {
    uint8_t *dd = (uint8_t*)d;
    const uint8_t *ss = (const uint8_t*)s;
    size_t i;
    for (i = 0; i < n; ++i) dd[i] = ss[i];
    return d;
}
static size_t c6_strlen(const char *s) {
    size_t n = 0;
    while (s && s[n]) ++n;
    return n;
}
static int c6_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { ++a; ++b; }
    return (int)((unsigned char)*a - (unsigned char)*b);
}
static bool c6_starts_with(const char *s, const char *pfx) {
    while (s && pfx && *pfx) {
        if (*s != *pfx) return false;
        ++s; ++pfx;
    }
    return pfx && *pfx == 0;
}
static bool c6_is_sep(char ch) {
    return ch == '/';
}
static bool c6_path_has_dotdot_escape(const char *path) {
    size_t i = 0;
    if (!path) return false;
    while (path[i]) {
        size_t st = i;
        size_t len;
        while (path[i] && c6_is_sep(path[i])) ++i;
        st = i;
        while (path[i] && !c6_is_sep(path[i])) ++i;
        len = i - st;
        if (len == 2 && path[st] == '.' && path[st + 1] == '.') return true;
    }
    return false;
}
static bool c6_path_has_magiclink(const char *path) {
    if (!path) return false;
    if (c6_starts_with(path, "/proc/self/fd/")) return true;
    if (c6_starts_with(path, "/proc/thread-self/")) return true;
    return false;
}
static uint32_t c6_round_pow2_u32(uint32_t v) {
    uint32_t p = 1;
    if (v <= 1) return 1;
    while (p < v && p < 0x80000000u) p <<= 1;
    return p ? p : v;
}
static int c6_task_index_for_pid(int pid) {
    int i;
    for (i = 0; i < TASK_MAX; ++i) {
        if (g_tasks[i].used && g_tasks[i].pid == pid) return i;
    }
    return -1;
}
static void c6_append_ch(char *d, size_t *l, size_t cap, char ch) {
    if (*l + 1 < cap) {
        d[(*l)++] = ch;
        d[*l] = 0;
    }
}
static void c6_append_str(char *d, size_t *l, size_t cap, const char *s) {
    while (s && *s && *l + 1 < cap) {
        d[(*l)++] = *s++;
    }
    d[*l] = 0;
}
static void c6_append_u32(char *d, size_t *l, size_t cap, uint32_t v) {
    char t[12];
    int i = 0;
    if (!v) {
        c6_append_ch(d, l, cap, '0');
        return;
    }
    while (v && i < (int)sizeof(t)) {
        t[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (--i >= 0) c6_append_ch(d, l, cap, t[i]);
}
static void c6_append_u64(char *d, size_t *l, size_t cap, uint64_t v) {
    char t[24];
    int i = 0;
    if (!v) {
        c6_append_ch(d, l, cap, '0');
        return;
    }
    while (v && i < (int)sizeof(t)) {
        t[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (--i >= 0) c6_append_ch(d, l, cap, t[i]);
}
static const char *c6_skip_ws(const char *s) {
    if (!s) return "";
    while (*s == ' ' || *s == '\t') ++s;
    return s;
}

/* modern syscall structs/constants */
#define C6_CLOSE_RANGE_CLOEXEC  (1u << 2)
#define C6_CLONE_PIDFD          0x00001000ULL
#define C6_CLONE_INTO_CGROUP    0x200000000ULL
#define C6_AT_SYMLINK_NOFOLLOW  0x100
#define C6_AT_NO_AUTOMOUNT      0x800
#define C6_AT_EMPTY_PATH        0x1000

#define C6_MEMBARRIER_CMD_QUERY               0
#define C6_MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED (1 << 4)
#define C6_MEMBARRIER_CMD_PRIVATE_EXPEDITED   (1 << 3)
#define C6_MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE (1 << 5)
#define C6_RSEQ_FLAG_UNREGISTER              1u
#define C6_RSEQ_UNREGISTERED_CPU             0xFFFFFFFFu

#define C6_OPEN_HOW_SIZE_VER0   ((size_t)(sizeof(c6_open_how_t)))
#define C6_RESOLVE_NO_XDEV      0x01u
#define C6_RESOLVE_NO_MAGICLINKS 0x02u
#define C6_RESOLVE_NO_SYMLINKS  0x04u
#define C6_RESOLVE_BENEATH      0x08u
#define C6_RESOLVE_IN_ROOT      0x10u
#define C6_RESOLVE_CACHED       0x20u
#define C6_RESOLVE_ALL (C6_RESOLVE_NO_XDEV|C6_RESOLVE_NO_MAGICLINKS|C6_RESOLVE_NO_SYMLINKS|C6_RESOLVE_BENEATH|C6_RESOLVE_IN_ROOT|C6_RESOLVE_CACHED)

#ifndef ELOOP
#define ELOOP 40
#endif

#define C6_IOURING_SETUP_IOPOLL     (1u << 0)
#define C6_IOURING_SETUP_SQPOLL     (1u << 1)
#define C6_IOURING_SETUP_SQ_AFF     (1u << 2)
#define C6_IOURING_SETUP_CQSIZE     (1u << 3)
#define C6_IOURING_SETUP_CLAMP      (1u << 4)
#define C6_IOURING_SETUP_ATTACH_WQ  (1u << 5)
#define C6_IOURING_SETUP_ALL (C6_IOURING_SETUP_IOPOLL|C6_IOURING_SETUP_SQPOLL|C6_IOURING_SETUP_SQ_AFF|C6_IOURING_SETUP_CQSIZE|C6_IOURING_SETUP_CLAMP|C6_IOURING_SETUP_ATTACH_WQ)

#define C6_IOURING_FEAT_SINGLE_MMAP   (1u << 0)
#define C6_IOURING_FEAT_NODROP        (1u << 1)
#define C6_IOURING_FEAT_SUBMIT_STABLE (1u << 2)
#define C6_IOURING_FEAT_RW_CUR_POS    (1u << 3)
#define C6_IOURING_FEAT_EXT_ARG       (1u << 8)

#define C6_IORING_ENTER_GETEVENTS  (1u << 0)
#define C6_IORING_ENTER_SQ_WAKEUP  (1u << 1)
#define C6_IORING_ENTER_SQ_WAIT    (1u << 2)
#define C6_IORING_ENTER_EXT_ARG    (1u << 3)
#define C6_IORING_ENTER_ALL (C6_IORING_ENTER_GETEVENTS|C6_IORING_ENTER_SQ_WAKEUP|C6_IORING_ENTER_SQ_WAIT|C6_IORING_ENTER_EXT_ARG)

#define C6_IORING_REGISTER_BUFFERS          0u
#define C6_IORING_UNREGISTER_BUFFERS        1u
#define C6_IORING_REGISTER_FILES            2u
#define C6_IORING_UNREGISTER_FILES          3u
#define C6_IORING_REGISTER_EVENTFD          4u
#define C6_IORING_UNREGISTER_EVENTFD        5u
#define C6_IORING_REGISTER_EVENTFD_ASYNC    7u
#define C6_IORING_REGISTER_PROBE            8u

#define C6_STATX_TYPE        0x00000001U
#define C6_STATX_MODE        0x00000002U
#define C6_STATX_NLINK       0x00000004U
#define C6_STATX_UID         0x00000008U
#define C6_STATX_GID         0x00000010U
#define C6_STATX_ATIME       0x00000020U
#define C6_STATX_MTIME       0x00000040U
#define C6_STATX_CTIME       0x00000080U
#define C6_STATX_INO         0x00000100U
#define C6_STATX_SIZE        0x00000200U
#define C6_STATX_BLOCKS      0x00000400U
#define C6_STATX_BASIC_STATS 0x000007FFU
#define C6_STATX_MNT_ID      0x00001000U

typedef struct {
    uint64_t flags;
    uint64_t mode;
    uint64_t resolve;
} c6_open_how_t;

typedef struct {
    uint64_t flags;
    uint64_t pidfd;
    uint64_t child_tid;
    uint64_t parent_tid;
    uint64_t exit_signal;
    uint64_t stack;
    uint64_t stack_size;
    uint64_t tls;
    uint64_t set_tid;
    uint64_t set_tid_size;
    uint64_t cgroup;
} c6_clone_args_t;

typedef struct {
    uint32_t sq_entries;
    uint32_t cq_entries;
    uint32_t flags;
    uint32_t sq_thread_cpu;
    uint32_t sq_thread_idle;
    uint32_t features;
    uint32_t wq_fd;
    uint32_t resv[3];
    uint64_t resv2[2];
} c6_io_uring_params_t;

typedef struct {
    int64_t  tv_sec;
    uint32_t tv_nsec;
    int32_t  __reserved;
} c6_statx_timestamp_t;

typedef struct {
    uint32_t stx_mask;
    uint32_t stx_blksize;
    uint64_t stx_attributes;
    uint32_t stx_nlink;
    uint32_t stx_uid;
    uint32_t stx_gid;
    uint16_t stx_mode;
    uint16_t __spare0[1];
    uint64_t stx_ino;
    uint64_t stx_size;
    uint64_t stx_blocks;
    uint64_t stx_attributes_mask;
    c6_statx_timestamp_t stx_atime;
    c6_statx_timestamp_t stx_btime;
    c6_statx_timestamp_t stx_ctime;
    c6_statx_timestamp_t stx_mtime;
    uint32_t stx_rdev_major;
    uint32_t stx_rdev_minor;
    uint32_t stx_dev_major;
    uint32_t stx_dev_minor;
    uint64_t stx_mnt_id;
    uint32_t stx_dio_mem_align;
    uint32_t stx_dio_offset_align;
    uint64_t __spare3[12];
} c6_statx_t;

typedef struct {
    uint64_t val;
    uint64_t uaddr;
    uint32_t flags;
    uint32_t __reserved;
} c6_futex_waitv_t;

typedef struct {
    uint16_t nr;
    const char *name;
    syscall_fn_t fn;
} c6_sysmap_t;

compat6_io_uring_t g_compat6_io_urings[C6_IOURING_MAX];
static int g_c6_registered_syscalls;
typedef struct {
    bool     registered;
    uint64_t user_ptr;
    uint32_t user_len;
    uint32_t signature;
} c6_rseq_state_t;
static c6_rseq_state_t g_c6_rseq_state[TASK_MAX];
static uint8_t g_c6_membarrier_reg[TASK_MAX];

typedef struct {
    uint32_t cpu_id_start;
    uint32_t cpu_id;
    uint64_t rseq_cs;
    uint32_t flags;
} c6_rseq_user_t;

static void c6_rseq_publish_cpu(int tidx, uint32_t cpu) {
    c6_rseq_state_t *st;
    c6_rseq_user_t *usr;
    if (tidx < 0 || tidx >= TASK_MAX) return;
    st = &g_c6_rseq_state[tidx];
    if (!st->registered || !st->user_ptr || st->user_len < sizeof(uint32_t) * 2) return;
    usr = (c6_rseq_user_t*)(uintptr_t)st->user_ptr;
    usr->cpu_id_start = cpu;
    usr->cpu_id = cpu;
}

static void c6_rseq_mark_unregistered(int tidx) {
    c6_rseq_state_t *st;
    c6_rseq_user_t *usr;
    if (tidx < 0 || tidx >= TASK_MAX) return;
    st = &g_c6_rseq_state[tidx];
    if (st->user_ptr && st->user_len >= sizeof(uint32_t) * 2) {
        usr = (c6_rseq_user_t*)(uintptr_t)st->user_ptr;
        usr->cpu_id_start = C6_RSEQ_UNREGISTERED_CPU;
        usr->cpu_id = C6_RSEQ_UNREGISTERED_CPU;
    }
    c6_memset(st, 0, sizeof(*st));
}

static int c6_current_task_index(void) {
    if (g_current_task < 0 || g_current_task >= TASK_MAX) return -1;
    if (!g_tasks[g_current_task].used) return -1;
    return g_current_task;
}

static compat6_io_uring_t *c6_uring_find_by_fd(int fd) {
    int i;
    for (i = 0; i < C6_IOURING_MAX; ++i) {
        if (g_compat6_io_urings[i].used && g_compat6_io_urings[i].fd == fd) {
            return &g_compat6_io_urings[i];
        }
    }
    return 0;
}

static compat6_io_uring_t *c6_uring_alloc_slot(void) {
    int i;
    for (i = 0; i < C6_IOURING_MAX; ++i) {
        if (!g_compat6_io_urings[i].used) {
            c6_memset(&g_compat6_io_urings[i], 0, sizeof(g_compat6_io_urings[i]));
            g_compat6_io_urings[i].eventfd = -1;
            g_compat6_io_urings[i].used = true;
            return &g_compat6_io_urings[i];
        }
    }
    return 0;
}

static void c6_uring_gc_for_task(task_t *cur) {
    int i;
    if (!cur) return;
    for (i = 0; i < C6_IOURING_MAX; ++i) {
        compat6_io_uring_t *r = &g_compat6_io_urings[i];
        if (!r->used) continue;
        if (r->owner_pid != cur->pid) continue;
        if (!fd_valid(&cur->fdt, r->fd)) {
            c6_memset(r, 0, sizeof(*r));
            r->eventfd = -1;
        }
    }
}

static bool c6_pid_exists(int pid) {
    int i;
    for (i = 0; i < TASK_MAX; ++i) {
        if (g_tasks[i].used && g_tasks[i].pid == pid) return true;
    }
    return false;
}

static int c6_alloc_pidfd_for_pid(int pid) {
    task_t *cur = task_current();
    if (!cur) return -ESRCH;
    return fd_alloc(&cur->fdt, FDKIND_PIDFD, pid, FDFL_READABLE);
}

static uint32_t c6_linux_dev_major(uint64_t dev) {
    return (uint32_t)(((dev >> 8) & 0xFFFu) | ((dev >> 32) & ~0xFFFu));
}

static uint32_t c6_linux_dev_minor(uint64_t dev) {
    return (uint32_t)((dev & 0xFFu) | ((dev >> 12) & ~0xFFu));
}

static void c6_fill_statx_from_kstat(c6_statx_t *dst, const kstat_t *st) {
    if (!dst || !st) return;
    c6_memset(dst, 0, sizeof(*dst));
    dst->stx_mask = C6_STATX_BASIC_STATS | C6_STATX_MNT_ID;
    dst->stx_blksize = (uint32_t)st->st_blksize;
    dst->stx_nlink = st->st_nlink;
    dst->stx_uid = st->st_uid;
    dst->stx_gid = st->st_gid;
    dst->stx_mode = (uint16_t)(st->st_mode & 0xFFFFu);
    dst->stx_ino = st->st_ino;
    dst->stx_size = (uint64_t)st->st_size;
    dst->stx_blocks = (uint64_t)st->st_blocks;
    dst->stx_atime.tv_sec = st->st_atime_sec;
    dst->stx_atime.tv_nsec = (uint32_t)st->st_atime_nsec;
    dst->stx_mtime.tv_sec = st->st_mtime_sec;
    dst->stx_mtime.tv_nsec = (uint32_t)st->st_mtime_nsec;
    dst->stx_ctime.tv_sec = st->st_ctime_sec;
    dst->stx_ctime.tv_nsec = (uint32_t)st->st_ctime_nsec;
    dst->stx_rdev_major = c6_linux_dev_major(st->st_rdev);
    dst->stx_rdev_minor = c6_linux_dev_minor(st->st_rdev);
    dst->stx_dev_major = c6_linux_dev_major(st->st_dev);
    dst->stx_dev_minor = c6_linux_dev_minor(st->st_dev);
    dst->stx_mnt_id = 1;
}

/* syscall handlers (6-arg ABI) */
static int64_t c6_sys_getcpu(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    uint32_t *cpu = (uint32_t*)(uintptr_t)a0;
    uint32_t *node = (uint32_t*)(uintptr_t)a1;
    int tidx = c6_current_task_index();
    (void)a2; (void)a3; (void)a4; (void)a5;
    if (cpu) *cpu = 0;
    if (node) *node = 0;
    if (tidx >= 0) c6_rseq_publish_cpu(tidx, 0);
    return 0;
}

static int64_t c6_sys_memfd_create(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    return real_sys_memfd_create((const char*)(uintptr_t)a0, (unsigned int)a1);
}

static int64_t c6_sys_statx(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    int dirfd = (int)a0;
    const char *path = (const char*)(uintptr_t)a1;
    int flags = (int)a2;
    uint32_t mask = (uint32_t)a3;
    c6_statx_t *out = (c6_statx_t*)(uintptr_t)a4;
    kstat_t st;
    int rc;
    (void)a5;

    if (!out) return -EFAULT;
    if (!path) return -EFAULT;

    rc = real_sys_newfstatat(
        dirfd,
        path,
        &st,
        flags & (C6_AT_SYMLINK_NOFOLLOW | C6_AT_NO_AUTOMOUNT | C6_AT_EMPTY_PATH));
    if (rc < 0) return rc;

    c6_fill_statx_from_kstat(out, &st);
    if (mask) out->stx_mask &= mask;
    return 0;
}

static int64_t c6_sys_rseq(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    c6_rseq_user_t *usr = (c6_rseq_user_t*)(uintptr_t)a0;
    uint32_t len = (uint32_t)a1;
    int flags = (int)a2;
    uint32_t sig = (uint32_t)a3;
    int tidx = c6_current_task_index();
    c6_rseq_state_t *st;
    (void)a4; (void)a5;
    if (tidx < 0) return -ESRCH;
    if (!usr) return -EFAULT;
    if (len < sizeof(c6_rseq_user_t)) return -EINVAL;
    if (flags != 0 && flags != (int)C6_RSEQ_FLAG_UNREGISTER) return -EINVAL;
    st = &g_c6_rseq_state[tidx];
    if (flags & (int)C6_RSEQ_FLAG_UNREGISTER) {
        if (!st->registered) return -EINVAL;
        if (st->user_ptr != (uint64_t)(uintptr_t)usr) return -EINVAL;
        c6_rseq_mark_unregistered(tidx);
        return 0;
    }
    if (st->registered) {
        if (st->user_ptr == (uint64_t)(uintptr_t)usr) return 0;
        return -EBUSY;
    }
    st->registered = true;
    st->user_ptr = (uint64_t)(uintptr_t)usr;
    st->user_len = len;
    st->signature = sig;
    usr->cpu_id_start = 0;
    usr->cpu_id = 0;
    usr->rseq_cs = 0;
    usr->flags = 0;
    return 0;
}

static int64_t c6_sys_pidfd_send_signal(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    int pidfd = (int)a0;
    int sig = (int)a1;
    task_t *cur = task_current();
    int target_pid;
    (void)a2; (void)a3; (void)a4; (void)a5;

    if (!cur) return -ESRCH;
    if (!fd_valid(&cur->fdt, pidfd)) return -EBADF;
    if (cur->fdt.fds[pidfd].kind != FDKIND_PIDFD) return -EINVAL;

    target_pid = cur->fdt.fds[pidfd].ref;
    return real_sys_kill(target_pid, sig);
}

static int64_t c6_sys_io_uring_setup(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    uint32_t entries = (uint32_t)a0;
    c6_io_uring_params_t *p = (c6_io_uring_params_t*)(uintptr_t)a1;
    task_t *cur = task_current();
    compat6_io_uring_t *slot;
    uint32_t flags = 0;
    uint32_t cq_entries;
    int64_t fd64;
    uint32_t feat;
    int fd;
    (void)a2; (void)a3; (void)a4; (void)a5;

    if (!cur) return -ESRCH;
    if (entries == 0) return -EINVAL;
    if (p) flags = p->flags;
    if (flags & ~C6_IOURING_SETUP_ALL) return -EINVAL;
    if ((flags & C6_IOURING_SETUP_SQ_AFF) && !(flags & C6_IOURING_SETUP_SQPOLL)) return -EINVAL;
    if (entries > 32768u) {
        if (!(flags & C6_IOURING_SETUP_CLAMP)) return -EINVAL;
        entries = 32768u;
    }
    entries = c6_round_pow2_u32(entries);
    if (entries < 2u) entries = 2u;
    cq_entries = entries * 2u;
    if (p && (flags & C6_IOURING_SETUP_CQSIZE)) {
        if (p->cq_entries < entries) return -EINVAL;
        cq_entries = p->cq_entries;
        if (cq_entries > 65536u) {
            if (!(flags & C6_IOURING_SETUP_CLAMP)) return -EINVAL;
            cq_entries = 65536u;
        }
    }

    c6_uring_gc_for_task(cur);

    slot = c6_uring_alloc_slot();
    if (!slot) return -ENOMEM;

    fd64 = real_sys_eventfd2(0, O_CLOEXEC);
    if (fd64 < 0) {
        slot->used = false;
        return fd64;
    }
    fd = (int)fd64;

    slot->fd = fd;
    slot->owner_pid = cur->pid;
    slot->entries = entries;
    slot->cq_entries = cq_entries;
    slot->flags = flags;
    feat = C6_IOURING_FEAT_SINGLE_MMAP |
           C6_IOURING_FEAT_NODROP |
           C6_IOURING_FEAT_SUBMIT_STABLE |
           C6_IOURING_FEAT_RW_CUR_POS |
           C6_IOURING_FEAT_EXT_ARG;
    slot->feature_bits = feat;
    slot->reg_files = 0;
    slot->reg_buffers = 0;
    slot->eventfd = fd;
    slot->eventfd_async = false;
    slot->enter_wakeups = 0;
    slot->submit_count = 0;
    slot->complete_count = 0;

    if (p) {
        c6_memset(p, 0, sizeof(*p));
        p->sq_entries = entries;
        p->cq_entries = cq_entries;
        p->flags = flags;
        p->sq_thread_cpu = 0;
        p->sq_thread_idle = (flags & C6_IOURING_SETUP_SQPOLL) ? 2000u : 0u;
        p->features = feat;
    }
    return fd;
}

static int64_t c6_sys_io_uring_enter(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    int fd = (int)a0;
    uint32_t to_submit = (uint32_t)a1;
    uint32_t min_complete = (uint32_t)a2;
    uint32_t flags = (uint32_t)a3;
    task_t *cur = task_current();
    c6_uring_gc_for_task(cur);
    compat6_io_uring_t *ring = c6_uring_find_by_fd(fd);
    uint32_t submitted = to_submit;
    (void)a4; (void)a5;

    if (!ring) return -EBADF;
    if (!cur) return -ESRCH;
    if (ring->owner_pid != cur->pid) return -EBADF;
    if (flags & ~C6_IORING_ENTER_ALL) return -EINVAL;
    if (submitted > ring->entries) submitted = ring->entries;
    if (submitted && ring->submit_count > ~0ULL - (uint64_t)submitted) ring->submit_count = ~0ULL;
    else ring->submit_count += (uint64_t)submitted;
    if (submitted && ring->complete_count > ~0ULL - (uint64_t)submitted) ring->complete_count = ~0ULL;
    else ring->complete_count += (uint64_t)submitted;
    if ((flags & (C6_IORING_ENTER_SQ_WAKEUP | C6_IORING_ENTER_SQ_WAIT)) != 0) {
        if (ring->enter_wakeups != 0xFFFFFFFFu) ++ring->enter_wakeups;
    }
    if (flags & C6_IORING_ENTER_GETEVENTS) {
        uint64_t have = ring->complete_count;
        if (have < (uint64_t)min_complete) ring->complete_count = (uint64_t)min_complete;
    }
    return (int64_t)submitted;
}

static int64_t c6_sys_io_uring_register(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    int fd = (int)a0;
    uint32_t opcode = (uint32_t)a1;
    void *arg = (void*)(uintptr_t)a2;
    uint32_t nr_args = (uint32_t)a3;
    task_t *cur = task_current();
    c6_uring_gc_for_task(cur);
    compat6_io_uring_t *ring = c6_uring_find_by_fd(fd);
    (void)a4; (void)a5;
    if (!ring) return -EBADF;
    if (!cur) return -ESRCH;
    if (ring->owner_pid != cur->pid) return -EBADF;
    switch (opcode) {
        case C6_IORING_REGISTER_BUFFERS:
            if (nr_args == 0 || nr_args > 4096u) return -EINVAL;
            if (ring->reg_buffers) return -EBUSY;
            ring->reg_buffers = nr_args;
            return 0;
        case C6_IORING_UNREGISTER_BUFFERS:
            if (!ring->reg_buffers) return -ENXIO;
            ring->reg_buffers = 0;
            return 0;
        case C6_IORING_REGISTER_FILES:
            if (nr_args == 0 || nr_args > TASK_FD_MAX) return -EINVAL;
            if (ring->reg_files) return -EBUSY;
            ring->reg_files = nr_args;
            return 0;
        case C6_IORING_UNREGISTER_FILES:
            if (!ring->reg_files) return -ENXIO;
            ring->reg_files = 0;
            return 0;
        case C6_IORING_REGISTER_EVENTFD:
        case C6_IORING_REGISTER_EVENTFD_ASYNC: {
            int efd;
            if (!arg) return -EFAULT;
            efd = *(int*)arg;
            if (!fd_valid(&cur->fdt, efd)) return -EBADF;
            if (cur->fdt.fds[efd].kind != FDKIND_EVENTFD) return -EINVAL;
            ring->eventfd = efd;
            ring->eventfd_async = (opcode == C6_IORING_REGISTER_EVENTFD_ASYNC);
            return 0;
        }
        case C6_IORING_UNREGISTER_EVENTFD:
            ring->eventfd = ring->fd;
            ring->eventfd_async = false;
            return 0;
        case C6_IORING_REGISTER_PROBE:
            if (arg && nr_args) c6_memset(arg, 0, (size_t)nr_args);
            return 0;
        default:
            return -EINVAL;
    }
}

static int64_t c6_sys_pidfd_open(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    int pid = (int)a0;
    uint32_t flags = (uint32_t)a1;
    (void)a2; (void)a3; (void)a4; (void)a5;
    if (flags != 0) return -EINVAL;
    if (!c6_pid_exists(pid)) return -ESRCH;
    return c6_alloc_pidfd_for_pid(pid);
}

static int64_t c6_sys_clone3(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    const c6_clone_args_t *args = (const c6_clone_args_t*)(uintptr_t)a0;
    size_t sz = (size_t)a1;
    task_t *cur = task_current();
    int64_t child;
    (void)a2; (void)a3; (void)a4; (void)a5;

    if (!args) return -EFAULT;
    if (sz < sizeof(uint64_t) * 8) return -EINVAL;
    if (!cur) return -ESRCH;
    /* Previously this short-circuited with -ENOSYS to force glibc/Firefox
     * down their clone(2) fallback path. With the new permissive
     * real_sys_clone (accepts namespace flags + vfork|VM), clone3 can
     * now route through the same code path and Firefox/Chrome can use
     * their preferred posix_spawn/threading entry points. */
    if (args->flags & C6_CLONE_INTO_CGROUP) {
        int cgroup_fd = (int)args->cgroup;
        if (cgroup_fd < 0) return -EINVAL;
        if (!fd_valid(&cur->fdt, cgroup_fd)) return -EBADF;
        if (cur->fdt.fds[cgroup_fd].kind != FDKIND_DIR && cur->fdt.fds[cgroup_fd].kind != FDKIND_PROC) return -ENOTDIR;
    }

    /* clone3's stack field is the BASE of the stack region; the actual
     * child RSP must be stack + stack_size (stacks grow downward). */
    child = real_sys_clone(args->flags, args->stack + args->stack_size, args->parent_tid, args->child_tid, args->tls);
    if (child >= 0 && (args->flags & C6_CLONE_PIDFD) && args->pidfd) {
        int pfd = c6_alloc_pidfd_for_pid((int)child);
        if (pfd >= 0) *(int*)(uintptr_t)args->pidfd = pfd;
    }
    if (child >= 0) {
        int cidx = c6_task_index_for_pid((int)child);
        if (cidx >= 0) {
            c6_rseq_mark_unregistered(cidx);
            g_c6_membarrier_reg[cidx] = 0;
        }
    }
    return child;
}

static int64_t c6_sys_close_range(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    uint64_t first = a0;
    uint64_t last = a1;
    uint32_t flags = (uint32_t)a2;
    task_t *cur = task_current();
    uint64_t fd;
    (void)a3; (void)a4; (void)a5;

    if (!cur) return -ESRCH;
    if (flags & ~C6_CLOSE_RANGE_CLOEXEC) return -EINVAL;
    if (first >= TASK_FD_MAX) return 0;
    if (last >= TASK_FD_MAX) last = TASK_FD_MAX - 1;
    if (last < first) return -EINVAL;

    for (fd = first; fd <= last; ++fd) {
        int ifd = (int)fd;
        if (!fd_valid(&cur->fdt, ifd)) continue;
        if (flags & C6_CLOSE_RANGE_CLOEXEC) {
            cur->fdt.fds[ifd].flags |= FDFL_CLOEXEC;
            compat3_fd_flags_changed(ifd);
        } else {
            real_sys_close(ifd);
        }
    }
    return 0;
}

static int64_t c6_sys_openat2(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    int dirfd = (int)a0;
    const char *path = (const char*)(uintptr_t)a1;
    const c6_open_how_t *how = (const c6_open_how_t*)(uintptr_t)a2;
    size_t howsz = (size_t)a3;
    int flags;
    int mode;
    uint64_t resolve;
    const uint8_t *raw;
    size_t i;
    bool path_abs;
    bool pseudo_path;
    (void)a4; (void)a5;

    if (!path) return -EFAULT;
    if (!how) return -EFAULT;
    if (howsz < C6_OPEN_HOW_SIZE_VER0) return -EINVAL;
    flags = (int)how->flags;
    mode = (int)how->mode;
    resolve = how->resolve;
    if (howsz > C6_OPEN_HOW_SIZE_VER0) {
        raw = (const uint8_t*)(uintptr_t)how;
        for (i = C6_OPEN_HOW_SIZE_VER0; i < howsz; ++i) {
            if (raw[i] != 0) return -E2BIG;
        }
    }
    if (resolve & ~(uint64_t)C6_RESOLVE_ALL) return -EINVAL;
    if (mode && !(flags & O_CREAT)) return -EINVAL;
    path_abs = (path[0] == '/');
    pseudo_path = c6_starts_with(path, "/proc/") || c6_starts_with(path, "/sys/") || c6_starts_with(path, "/dev/");
    if (resolve & C6_RESOLVE_CACHED) {
        if (!path_abs || !pseudo_path) return -EAGAIN;
    }
    if ((resolve & C6_RESOLVE_BENEATH) && path_abs) return -EXDEV;
    if ((resolve & (C6_RESOLVE_BENEATH | C6_RESOLVE_IN_ROOT)) && c6_path_has_dotdot_escape(path)) return -EXDEV;
    if ((resolve & C6_RESOLVE_NO_XDEV) && path_abs && pseudo_path) return -EXDEV;
    if ((resolve & C6_RESOLVE_NO_MAGICLINKS) && c6_path_has_magiclink(path)) return -ELOOP;
    if ((resolve & C6_RESOLVE_NO_SYMLINKS) && c6_path_has_magiclink(path)) return -ELOOP;
    if ((resolve & C6_RESOLVE_IN_ROOT) && path_abs && dirfd != AT_FDCWD) {
        /* IN_ROOT should reinterpret absolute paths relative to dirfd root.
           Current VFS lacks that remapping, so fail explicitly instead of escaping root. */
        return -EXDEV;
    }
    return real_sys_openat(dirfd, path, flags, mode);
}

static int64_t c6_sys_pidfd_getfd(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    int pidfd = (int)a0;
    int targetfd = (int)a1;
    uint32_t flags = (uint32_t)a2;
    task_t *cur = task_current();
    int target_pid;
    (void)a3; (void)a4; (void)a5;

    if (!cur) return -ESRCH;
    if (flags != 0) return -EINVAL;
    if (!fd_valid(&cur->fdt, pidfd)) return -EBADF;
    if (cur->fdt.fds[pidfd].kind != FDKIND_PIDFD) return -EINVAL;

    target_pid = cur->fdt.fds[pidfd].ref;
    if (target_pid != cur->pid) return -ENOSYS;
    return real_sys_dup(targetfd);
}

static int64_t c6_sys_faccessat2(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a4; (void)a5;
    if (!(const char*)(uintptr_t)a1) return -EFAULT;
    return real_sys_faccessat((int)a0, (const char*)(uintptr_t)a1, (int)a2, (int)a3);
}

static int64_t c6_sys_process_madvise(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    return -ENOSYS;
}

static int64_t c6_sys_epoll_pwait2(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    int timeout_ms = -1;
    const timespec_t *ts = (const timespec_t*)(uintptr_t)a3;
    (void)a4; (void)a5;
    if (ts) {
        if (ts->tv_sec < 0 || ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000LL) return -EINVAL;
        timeout_ms = (int)(ts->tv_sec * 1000 + ts->tv_nsec / 1000000);
    }
    return real_sys_epoll_wait((int)a0, (void*)(uintptr_t)a1, (int)a2, timeout_ms);
}

static int64_t c6_sys_membarrier(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    int cmd = (int)a0;
    int flags = (int)a1;
    int cpu_id = (int)a2;
    int tidx = c6_current_task_index();
    (void)a3; (void)a4; (void)a5;
    if (tidx < 0) return -ESRCH;
    if (flags != 0) return -EINVAL;
    if (cpu_id != 0) return -EINVAL;
    if (cmd == C6_MEMBARRIER_CMD_QUERY) {
        return C6_MEMBARRIER_CMD_PRIVATE_EXPEDITED |
               C6_MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED |
               C6_MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE;
    }
    if (cmd == C6_MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED) {
        g_c6_membarrier_reg[tidx] = 1;
        return 0;
    }
    if (cmd == C6_MEMBARRIER_CMD_PRIVATE_EXPEDITED ||
        cmd == C6_MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE) {
        if (!g_c6_membarrier_reg[tidx]) return -EPERM;
        return 0;
    }
    return -EINVAL;
}

static int64_t c6_sys_memfd_secret(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    static const char secret_name[] = "memfd_secret";
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    return real_sys_memfd_create(secret_name, 0);
}

static int64_t c6_sys_process_mrelease(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    return 0;
}

static int64_t c6_sys_futex_waitv(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    const c6_futex_waitv_t *waitv = (const c6_futex_waitv_t*)(uintptr_t)a0;
    uint32_t nr = (uint32_t)a1;
    uint32_t flags = (uint32_t)a2;
    const timespec_t *timeout = (const timespec_t*)(uintptr_t)a3;
    (void)a4; (void)a5;
    if (!waitv || nr == 0) return -EINVAL;
    if (flags != 0) return -EINVAL;
    return real_sys_futex((uint32_t*)(uintptr_t)waitv[0].uaddr, FUTEX_WAIT,
                          (uint32_t)waitv[0].val, timeout, 0, 0);
}

static int64_t c6_sys_set_mempolicy_home_node(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    return 0;
}

static int64_t c6_sys_enosys(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    return -ENOSYS;
}

static int64_t c6_sys_execveat(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    int dirfd = (int)a0;
    const char *path = (const char*)(uintptr_t)a1;
    char *const *argv = (char *const *)(uintptr_t)a2;
    char *const *envp = (char *const *)(uintptr_t)a3;
    int flags = (int)a4;
    (void)a5;
    if (!path) return -EFAULT;
    if (dirfd != AT_FDCWD || flags != 0) return -ENOSYS;
    return real_sys_execve(path, (char *const *)argv, (char *const *)envp);
}

static int64_t c6_sys_rwv2_at(bool is_write,int fd,const iovec_t *iov,int iovcnt,uint64_t pos_l,uint64_t pos_h,int flags){
    uint64_t off_raw=((pos_h&0xFFFFFFFFu)<<32)|(pos_l&0xFFFFFFFFu);
    int64_t off=(int64_t)off_raw;
    int64_t saved_off;
    int64_t rc;
    if(iovcnt<0)return -EINVAL;
    if(iovcnt==0)return 0;
    if(!iov)return -EFAULT;
    if(flags!=0)return -EINVAL;
    if(off==-1LL){
        return is_write?real_writev(fd,iov,iovcnt):real_readv(fd,iov,iovcnt);
    }
    if(off<0)return -EINVAL;
    saved_off=real_sys_lseek(fd,0,SEEK_CUR);
    if(saved_off<0)return saved_off;
    rc=real_sys_lseek(fd,off,SEEK_SET);
    if(rc<0)return rc;
    rc=is_write?real_writev(fd,iov,iovcnt):real_readv(fd,iov,iovcnt);
    (void)real_sys_lseek(fd,saved_off,SEEK_SET);
    return rc;
}

static int64_t c6_sys_preadv2(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    int fd = (int)a0;
    const iovec_t *iov = (const iovec_t*)(uintptr_t)a1;
    int iovcnt = (int)a2;
    return c6_sys_rwv2_at(false,fd,iov,iovcnt,a3,a4,(int)a5);
}

static int64_t c6_sys_pwritev2(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    int fd = (int)a0;
    const iovec_t *iov = (const iovec_t*)(uintptr_t)a1;
    int iovcnt = (int)a2;
    return c6_sys_rwv2_at(true,fd,iov,iovcnt,a3,a4,(int)a5);
}

static int64_t c6_sys_pkey_mprotect(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    return real_sys_mprotect(a0, a1, (int)a2);
}

static const c6_sysmap_t g_c6_sysmap[] = {
    {309, "getcpu", c6_sys_getcpu},
    {319, "memfd_create", c6_sys_memfd_create},
    {322, "execveat", c6_sys_execveat},
    {327, "preadv2", c6_sys_preadv2},
    {328, "pwritev2", c6_sys_pwritev2},
    {329, "pkey_mprotect", c6_sys_pkey_mprotect},
    {332, "statx", c6_sys_statx},
    {324, "membarrier", c6_sys_membarrier},
    {334, "rseq", c6_sys_rseq},
    {424, "pidfd_send_signal", c6_sys_pidfd_send_signal},
    {425, "io_uring_setup", c6_sys_io_uring_setup},
    {426, "io_uring_enter", c6_sys_io_uring_enter},
    {427, "io_uring_register", c6_sys_io_uring_register},
    {428, "open_tree", c6_sys_enosys},
    {429, "move_mount", c6_sys_enosys},
    {430, "fsopen", c6_sys_enosys},
    {431, "fsconfig", c6_sys_enosys},
    {432, "fsmount", c6_sys_enosys},
    {433, "fspick", c6_sys_enosys},
    {434, "pidfd_open", c6_sys_pidfd_open},
    {435, "clone3", c6_sys_clone3},
    {436, "close_range", c6_sys_close_range},
    {437, "openat2", c6_sys_openat2},
    {438, "pidfd_getfd", c6_sys_pidfd_getfd},
    {439, "faccessat2", c6_sys_faccessat2},
    {440, "process_madvise", c6_sys_process_madvise},
    {441, "epoll_pwait2", c6_sys_epoll_pwait2},
    {442, "mount_setattr", c6_sys_enosys},
    {443, "quotactl_fd", c6_sys_enosys},
    {444, "landlock_create_ruleset", c6_sys_enosys},
    {445, "landlock_add_rule", c6_sys_enosys},
    {446, "landlock_restrict_self", c6_sys_enosys},
    {447, "memfd_secret", c6_sys_memfd_secret},
    {448, "process_mrelease", c6_sys_process_mrelease},
    {449, "futex_waitv", c6_sys_futex_waitv},
    {450, "set_mempolicy_home_node", c6_sys_set_mempolicy_home_node},
};

void compat6_rewire_syscalls(void) {
    size_t i;
    g_c6_registered_syscalls = 0;
    for (i = 0; i < sizeof(g_c6_sysmap) / sizeof(g_c6_sysmap[0]); ++i) {
        uint16_t nr = g_c6_sysmap[i].nr;
        if (nr < SYSCALL_MAX) {
            g_syscall_table[nr] = g_c6_sysmap[i].fn;
            ++g_c6_registered_syscalls;
        }
    }
}

int compat6_registered_syscalls(void) {
    return g_c6_registered_syscalls;
}

bool compat6_core_browser_abi_ready(void) {
    return g_syscall_table[332] &&
           g_syscall_table[435] &&
           g_syscall_table[436] &&
           g_syscall_table[437] &&
           g_syscall_table[439] &&
           g_syscall_table[441] &&
           g_syscall_table[424] &&
           g_syscall_table[434] &&
           g_syscall_table[425];
}

/* shell diagnostics */
static void cmd6_abi(const char *args, char *out, int out_max) {
    size_t l = 0;
    int i, total = 0;
    (void)args;
    out[0] = 0;

    for (i = 0; i < SYSCALL_MAX; ++i) if (g_syscall_table[i]) ++total;

    c6_append_str(out, &l, (size_t)out_max, "=== RiduxOS Compat6 ABI ===\n");
    c6_append_str(out, &l, (size_t)out_max, "syscalls table: ");
    c6_append_u32(out, &l, (size_t)out_max, (uint32_t)total);
    c6_append_ch(out, &l, (size_t)out_max, '/');
    c6_append_u32(out, &l, (size_t)out_max, SYSCALL_MAX);
    c6_append_ch(out, &l, (size_t)out_max, '\n');
    c6_append_str(out, &l, (size_t)out_max, "compat6 added: ");
    c6_append_u32(out, &l, (size_t)out_max, (uint32_t)compat6_registered_syscalls());
    c6_append_ch(out, &l, (size_t)out_max, '\n');

    c6_append_str(out, &l, (size_t)out_max, "openat2/close_range/faccessat2/statx: ");
    c6_append_str(out, &l, (size_t)out_max, (g_syscall_table[437] && g_syscall_table[436] &&
                    g_syscall_table[439] && g_syscall_table[332]) ? "ok\n" : "partial\n");
    c6_append_str(out, &l, (size_t)out_max, "pidfd + clone3: ");
    c6_append_str(out, &l, (size_t)out_max, (g_syscall_table[434] && g_syscall_table[424] &&
                    g_syscall_table[435]) ? "ok\n" : "partial\n");
    c6_append_str(out, &l, (size_t)out_max, "io_uring + epoll_pwait2: ");
    c6_append_str(out, &l, (size_t)out_max, (g_syscall_table[425] && g_syscall_table[426] &&
                    g_syscall_table[427] && g_syscall_table[441]) ? "ok\n" : "partial\n");
    c6_append_str(out, &l, (size_t)out_max, "browser ABI core verdict: ");
    c6_append_str(out, &l, (size_t)out_max, compat6_core_browser_abi_ready() ? "ready-ish\n" : "still partial\n");
}

static void cmd6_pidfd(const char *args, char *out, int out_max) {
    task_t *cur = task_current();
    size_t l = 0;
    int i;
    (void)args;
    out[0] = 0;

    if (!cur) {
        c6_append_str(out, &l, (size_t)out_max, "pidfd: no current task\n");
        return;
    }

    c6_append_str(out, &l, (size_t)out_max, "FD  TARGET_PID\n");
    for (i = 0; i < TASK_FD_MAX; ++i) {
        if (cur->fdt.fds[i].kind != FDKIND_PIDFD) continue;
        c6_append_u32(out, &l, (size_t)out_max, (uint32_t)i);
        c6_append_str(out, &l, (size_t)out_max, "   ");
        c6_append_u32(out, &l, (size_t)out_max, (uint32_t)cur->fdt.fds[i].ref);
        c6_append_ch(out, &l, (size_t)out_max, '\n');
    }
}

static void cmd6_iouring(const char *args, char *out, int out_max) {
    size_t l = 0;
    int i;
    (void)args;
    out[0] = 0;

    c6_append_str(out, &l, (size_t)out_max, "FD  ENTRIES/CQ  SUBMITS  COMPLETES  EVFD  REGS(F/B)\n");
    for (i = 0; i < C6_IOURING_MAX; ++i) {
        if (!g_compat6_io_urings[i].used) continue;
        c6_append_u32(out, &l, (size_t)out_max, (uint32_t)g_compat6_io_urings[i].fd);
        c6_append_str(out, &l, (size_t)out_max, "   ");
        c6_append_u32(out, &l, (size_t)out_max, g_compat6_io_urings[i].entries);
        c6_append_ch(out, &l, (size_t)out_max, '/');
        c6_append_u32(out, &l, (size_t)out_max, g_compat6_io_urings[i].cq_entries);
        c6_append_str(out, &l, (size_t)out_max, "      ");
        c6_append_u64(out, &l, (size_t)out_max, g_compat6_io_urings[i].submit_count);
        c6_append_str(out, &l, (size_t)out_max, "       ");
        c6_append_u64(out, &l, (size_t)out_max, g_compat6_io_urings[i].complete_count);
        c6_append_str(out, &l, (size_t)out_max, "      ");
        c6_append_u32(out, &l, (size_t)out_max, (uint32_t)((g_compat6_io_urings[i].eventfd >= 0) ? g_compat6_io_urings[i].eventfd : 0));
        c6_append_str(out, &l, (size_t)out_max, "    ");
        c6_append_u32(out, &l, (size_t)out_max, g_compat6_io_urings[i].reg_files);
        c6_append_ch(out, &l, (size_t)out_max, '/');
        c6_append_u32(out, &l, (size_t)out_max, g_compat6_io_urings[i].reg_buffers);
        c6_append_ch(out, &l, (size_t)out_max, '\n');
    }
}

static void cmd6_statx(const char *args, char *out, int out_max) {
    const char *path = c6_skip_ws(args);
    c6_statx_t stx;
    int64_t rc;
    size_t l = 0;
    out[0] = 0;

    if (!path || !*path) {
        c6_append_str(out, &l, (size_t)out_max, "usage: statx <path>\n");
        return;
    }

    rc = c6_sys_statx((uint64_t)AT_FDCWD,
                      (uint64_t)(uintptr_t)path,
                      0,
                      C6_STATX_BASIC_STATS,
                      (uint64_t)(uintptr_t)&stx,
                      0);
    if (rc < 0) {
        c6_append_str(out, &l, (size_t)out_max, "statx: error ");
        c6_append_u32(out, &l, (size_t)out_max, (uint32_t)(-rc));
        c6_append_ch(out, &l, (size_t)out_max, '\n');
        return;
    }

    c6_append_str(out, &l, (size_t)out_max, "path: ");
    c6_append_str(out, &l, (size_t)out_max, path);
    c6_append_ch(out, &l, (size_t)out_max, '\n');
    c6_append_str(out, &l, (size_t)out_max, "mode: ");
    c6_append_u32(out, &l, (size_t)out_max, stx.stx_mode);
    c6_append_str(out, &l, (size_t)out_max, "  size: ");
    c6_append_u64(out, &l, (size_t)out_max, stx.stx_size);
    c6_append_str(out, &l, (size_t)out_max, "  blocks: ");
    c6_append_u64(out, &l, (size_t)out_max, stx.stx_blocks);
    c6_append_ch(out, &l, (size_t)out_max, '\n');
}

static void cmd6_sysplus(const char *args, char *out, int out_max) {
    size_t l = 0;
    size_t i;
    (void)args;
    out[0] = 0;
    c6_append_str(out, &l, (size_t)out_max, "compat6 syscall map:\n");
    for (i = 0; i < sizeof(g_c6_sysmap) / sizeof(g_c6_sysmap[0]); ++i) {
        c6_append_u32(out, &l, (size_t)out_max, g_c6_sysmap[i].nr);
        c6_append_str(out, &l, (size_t)out_max, " ");
        c6_append_str(out, &l, (size_t)out_max, g_c6_sysmap[i].name);
        c6_append_str(out, &l, (size_t)out_max, (g_c6_sysmap[i].nr < SYSCALL_MAX && g_syscall_table[g_c6_sysmap[i].nr]) ? " [on]\n" : " [off]\n");
    }
}

void compat6_register_shell_cmds(void) {
    extern compat_shell_cmd_t g_compat_cmds[];
    extern int g_compat_cmd_count;
    #define REG6(n,h,fn) if(g_compat_cmd_count<COMPAT_SHELL_CMD_MAX){g_compat_cmds[g_compat_cmd_count].name=n;g_compat_cmds[g_compat_cmd_count].help=h;g_compat_cmds[g_compat_cmd_count].handler=fn;++g_compat_cmd_count;}
    REG6("abi6", "Extended ABI readiness (compat6)", cmd6_abi)
    REG6("pidfd", "List current pidfd descriptors", cmd6_pidfd)
    REG6("io_uring", "List io_uring instances", cmd6_iouring)
    REG6("statx", "Query file metadata via statx path", cmd6_statx)
    REG6("sysplus", "List compat6 syscall map", cmd6_sysplus)
    #undef REG6
}

void compat6_init_all(void) {
    int i;
    c6_memset(g_compat6_io_urings, 0, sizeof(g_compat6_io_urings));
    c6_memset(g_c6_rseq_state, 0, sizeof(g_c6_rseq_state));
    c6_memset(g_c6_membarrier_reg, 0, sizeof(g_c6_membarrier_reg));
    for (i = 0; i < C6_IOURING_MAX; ++i) g_compat6_io_urings[i].eventfd = -1;
    compat6_rewire_syscalls();
    compat6_register_shell_cmds();
}
