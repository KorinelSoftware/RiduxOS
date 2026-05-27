/*
 * API del runtime Linux-ish: syscalls, ELF, TLS y dynlink.
 */
#ifndef RIDUX_COMPAT3_H
#define RIDUX_COMPAT3_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "base.h"
#include "memory_tasks.h"

/* VFS bridge: unified open/read/write/close over all backends */
#define VFS_PATH_MAX 256
#define OPEN_MAX_FILES 512

#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0040
#define O_EXCL      0x0080
#define O_TRUNC     0x0200
#define O_APPEND    0x0400
#define O_NONBLOCK  0x0800
#define O_DIRECTORY 0x10000
#define O_NOFOLLOW  0x20000
#define O_CLOEXEC   0x80000
#define O_PATH      0x200000
#define O_NOCTTY    0x100
#define O_TMPFILE   0x410000

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define AT_FDCWD (-100)

/* dirent for getdents64 */
typedef struct {
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[256];
} linux_dirent64_t;

/* Unified file read/write through FD table */
int64_t real_sys_open(const char *path, int flags, int mode);
int64_t real_sys_openat(int dirfd, const char *path, int flags, int mode);
int64_t real_sys_close(int fd);
int64_t real_sys_read(int fd, void *buf, size_t count);
int64_t real_sys_write(int fd, const void *buf, size_t count);
int64_t real_sys_pread64(int fd, void *buf, size_t count, int64_t offset);
int64_t real_sys_pwrite64(int fd, const void *buf, size_t count, int64_t offset);
int64_t real_sys_lseek(int fd, int64_t offset, int whence);
int64_t real_sys_dup(int oldfd);
int64_t real_sys_dup2(int oldfd, int newfd);
int64_t real_sys_dup3(int oldfd, int newfd, int flags);
int64_t real_sys_pipe(int pipefd[2]);
int64_t real_sys_pipe2(int pipefd[2], int flags);
int64_t real_sys_ioctl(int fd, uint64_t request, uint64_t arg);
int64_t real_sys_fcntl(int fd, int cmd, uint64_t arg);
void compat3_fd_flags_changed(int fd);
int64_t real_sys_fstat(int fd, kstat_t *st);
int64_t real_sys_stat(const char *path, kstat_t *st);
int64_t real_sys_lstat(const char *path, kstat_t *st);
int64_t real_sys_newfstatat(int dirfd, const char *path, kstat_t *st, int flags);
int64_t real_sys_access(const char *path, int mode);
int64_t real_sys_faccessat(int dirfd, const char *path, int mode, int flags);
int64_t real_sys_getdents64(int fd, void *dirp, size_t count);
int64_t real_sys_readlink(const char *path, char *buf, size_t bufsiz);
int64_t real_sys_getcwd(char *buf, size_t size);
int64_t real_sys_chdir(const char *path);
int64_t real_sys_fchdir(int fd);
int64_t real_sys_mkdir(const char *path, int mode);
int64_t real_sys_unlink(const char *path);
int64_t real_sys_rename(const char *oldpath, const char *newpath);
int64_t real_sys_truncate(const char *path, int64_t length);
int64_t real_sys_ftruncate(int fd, int64_t length);

/* Task/process syscalls */
int64_t real_sys_fork(void);
int64_t real_sys_vfork(void);
int64_t real_sys_clone(uint64_t flags, uint64_t child_stack, uint64_t ptid, uint64_t ctid, uint64_t tls);
int64_t real_sys_execve(const char *path, char *const argv[], char *const envp[]);
int64_t real_sys_exit(int code);
int64_t real_sys_exit_group(int code);
int64_t real_sys_wait4(int pid, int *status, int options, void *rusage);
int64_t real_sys_getpid(void);
int64_t real_sys_getppid(void);
int64_t real_sys_gettid(void);
int64_t real_sys_getuid(void);
int64_t real_sys_getgid(void);
int64_t real_sys_geteuid(void);
int64_t real_sys_getegid(void);
int64_t real_sys_setuid(int uid);
int64_t real_sys_setgid(int gid);
int64_t real_sys_setreuid(int ruid, int euid);
int64_t real_sys_setregid(int rgid, int egid);
int64_t real_sys_getresuid(uint32_t *ruid, uint32_t *euid, uint32_t *suid);
int64_t real_sys_getresgid(uint32_t *rgid, uint32_t *egid, uint32_t *sgid);
int64_t real_sys_setresuid(uint32_t ruid, uint32_t euid, uint32_t suid);
int64_t real_sys_setresgid(uint32_t rgid, uint32_t egid, uint32_t sgid);
int64_t real_sys_setpgid(int pid, int pgid);
int64_t real_sys_getpgid(int pid);
int64_t real_sys_setsid(void);
int64_t real_sys_getsid(int pid);

/* Time / clock common structs */
typedef struct { int64_t tv_sec; int64_t tv_nsec; } timespec_t;
typedef struct { int64_t tv_sec; int64_t tv_usec; } timeval_t;
typedef struct { int tz_minuteswest; int tz_dsttime; } timezone_t;

/* Signal syscalls */
int64_t real_sys_kill(int pid, int sig);
int64_t real_sys_tkill(int tid, int sig);
int64_t real_sys_tgkill(int tgid, int tid, int sig);
int64_t real_sys_rt_sigaction(int sig, const void *act, void *oldact, size_t sigsetsize);
int64_t real_sys_rt_sigprocmask(int how, const void *set, void *oldset, size_t sigsetsize);
int64_t real_sys_rt_sigreturn(void);
int64_t real_sys_rt_sigpending(void *set, size_t sigsetsize);
int64_t real_sys_rt_sigtimedwait(const void *set, void *info, const timespec_t *timeout, size_t sigsetsize);
int64_t real_sys_rt_sigsuspend(const void *mask, size_t sigsetsize);
int64_t real_sys_sigaltstack(const void *ss, void *oss);

/* Memory syscalls (real paging) */
int64_t real_sys_mmap(uint64_t addr, uint64_t len, int prot, int flags, int fd, uint64_t off);
int64_t real_sys_munmap(uint64_t addr, uint64_t len);
int compat3_take_scm_right(int token, int *out_fd);
int compat3_send_scm_right_from_current(int sock_fd, int pass_fd);
int compat3_send_scm_right_to_socket_ref_from_current(int sock_ref, int pass_fd);
int compat3_send_devnull_right_to_socket_ref_from_current(int sock_ref);
int64_t real_sys_mprotect(uint64_t addr, uint64_t len, int prot);
int64_t real_sys_brk(uint64_t addr);
int64_t real_sys_mremap(uint64_t old_addr, uint64_t old_size, uint64_t new_size, int flags, uint64_t new_addr);
int64_t real_sys_madvise(uint64_t addr, uint64_t len, int advice);
int64_t real_sys_msync(uint64_t addr, uint64_t len, int flags);
void compat3_debug_wayfire_addr_mapping(const task_t *cur, uint64_t addr, const char *tag);
void compat3_debug_wayfire_stack_mapping(const task_t *cur, uint64_t sp, const char *tag);
bool compat3_recover_null_plt_call(uint32_t vec, uint64_t err_code,
                                   uint64_t cr2, uint64_t fault_rip,
                                   uint64_t frame_base);

/* ELF64 real mapper + dynamic linker */
#define AT_NULL    0
#define AT_PHDR    3
#define AT_PHENT   4
#define AT_PHNUM   5
#define AT_PAGESZ  6
#define AT_BASE    7
#define AT_FLAGS   8
#define AT_ENTRY   9
#define AT_UID    11
#define AT_EUID   12
#define AT_GID    13
#define AT_EGID   14
#define AT_PLATFORM 15
#define AT_HWCAP   16
#define AT_CLKTCK  17
#define AT_SECURE  23
#define AT_RANDOM 25
#define AT_HWCAP2  26
#define AT_EXECFN 31
#define AT_SYSINFO_EHDR 33

typedef struct {
    uint64_t a_type;
    uint64_t a_val;
} auxv_t;

int  elf64_map_into_task(task_t *t, const uint8_t *data, uint32_t sz);
int  elf64_setup_stack(task_t *t, int argc, char **argv, char **envp, auxv_t *auxv, int auxc);
int  compat3_init_user_tls(task_t *t);

/* Net syscalls */
int64_t real_sys_socket(int domain, int type, int protocol);
int64_t real_sys_bind(int fd, const void *addr, uint32_t addrlen);
int64_t real_sys_listen(int fd, int backlog);
int64_t real_sys_accept(int fd, void *addr, uint32_t *addrlen);
int64_t real_sys_accept4(int fd, void *addr, uint32_t *addrlen, int flags);
int64_t real_sys_connect(int fd, const void *addr, uint32_t addrlen);
int64_t real_sys_sendto(int fd, const void *buf, size_t len, int flags, const void *addr, uint32_t addrlen);
int64_t real_sys_recvfrom(int fd, void *buf, size_t len, int flags, void *addr, uint32_t *addrlen);
int64_t real_sys_setsockopt(int fd, int level, int optname, const void *optval, uint32_t optlen);
int64_t real_sys_getsockopt(int fd, int level, int optname, void *optval, uint32_t *optlen);
int64_t real_sys_getsockname(int fd, void *addr, uint32_t *addrlen);
int64_t real_sys_getpeername(int fd, void *addr, uint32_t *addrlen);
int64_t real_sys_shutdown(int fd, int how);
int64_t real_sys_socketpair(int domain, int type, int protocol, int sv[2]);
int64_t real_sys_sendmsg(int fd, const void *msg, int flags);
int64_t real_sys_recvmsg(int fd, void *msg, int flags);

/* Time / clock syscalls */
int64_t real_sys_sendmmsg(int fd, void *vmessages, unsigned int vlen, unsigned int flags);
int64_t real_sys_recvmmsg(int fd, void *vmessages, unsigned int vlen, unsigned int flags, const timespec_t *timeout);

typedef struct {
    uint64_t uptime;
    uint64_t loads[3];
    uint64_t totalram;
    uint64_t freeram;
    uint64_t sharedram;
    uint64_t bufferram;
    uint64_t totalswap;
    uint64_t freeswap;
    uint16_t procs;
    uint64_t totalhigh;
    uint64_t freehigh;
    uint32_t mem_unit;
} sysinfo_t;

int64_t real_sys_clock_gettime(int clockid, timespec_t *tp);
int64_t real_sys_clock_getres(int clockid, timespec_t *tp);
int64_t real_sys_gettimeofday(timeval_t *tv, timezone_t *tz);
int64_t real_sys_time(int64_t *tloc);
int64_t real_sys_nanosleep(const timespec_t *req, timespec_t *rem);
int64_t real_sys_alarm(uint32_t seconds);
int64_t real_sys_timer_create(int clockid, void *sevp, int *timerid);
int64_t real_sys_timer_settime(int id, int flags, const void *new_val, void *old_val);
int64_t real_sys_sysinfo(sysinfo_t *info);

/* Linux ABI / info syscalls */
typedef struct {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
} utsname_t;

int64_t real_sys_uname(utsname_t *buf);
int64_t real_sys_getrandom(void *buf, size_t buflen, unsigned int flags);
int64_t real_sys_getrlimit(int resource, void *rlim);
int64_t real_sys_setrlimit(int resource, const void *rlim);
int64_t real_sys_prlimit64(int pid, int resource, const void *new_rlim, void *old_rlim);
int64_t real_sys_prctl(int option, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
int64_t real_sys_arch_prctl(int code, uint64_t addr);
int64_t real_sys_set_tid_address(uint64_t tidptr);
int64_t real_sys_set_robust_list(uint64_t head, size_t len);
int64_t real_sys_get_robust_list(int pid, uint64_t *head, size_t *len);
int64_t real_sys_futex(uint32_t *uaddr, int op, uint32_t val, const timespec_t *timeout, uint32_t *uaddr2, uint32_t val3);

/* epoll/poll/select real */
int64_t real_sys_epoll_create1(int flags);
int64_t real_sys_epoll_ctl(int epfd, int op, int fd, void *event);
int64_t real_sys_epoll_wait(int epfd, void *events, int maxevents, int timeout);
int64_t real_sys_poll(void *fds, uint64_t nfds, int timeout);
int64_t real_sys_select(int nfds, void *rfds, void *wfds, void *efds, timeval_t *timeout);
int64_t real_sys_eventfd2(unsigned int initval, int flags);
int64_t real_sys_timerfd_create(int clockid, int flags);
int64_t real_sys_timerfd_settime(int fd, int flags, const void *new_val, void *old_val);
int64_t real_sys_signalfd4(int fd, const void *mask, size_t sizemask, int flags);

/* Misc syscalls */
int64_t real_sys_shmget(int key, size_t size, int shmflg);
int64_t real_sys_shmat(int shmid, uint64_t shmaddr, int shmflg);
int64_t real_sys_shmdt(uint64_t shmaddr);
int64_t real_sys_shmctl(int shmid, int cmd, void *buf);
int64_t real_sys_umask(int mask);
int64_t real_sys_getrusage(int who, void *usage);
int64_t real_sys_times(void *buf);
int64_t real_sys_sched_yield(void);
int64_t real_sys_sched_setparam(int pid, const void *param);
int64_t real_sys_sched_getparam(int pid, void *param);
int64_t real_sys_sched_setscheduler(int pid, int policy, const void *param);
int64_t real_sys_sched_getscheduler(int pid);
int64_t real_sys_sched_getaffinity(int pid, size_t len, void *mask);
int64_t real_sys_sched_setaffinity(int pid, size_t len, const void *mask);
int64_t real_sys_sched_get_priority_max(int policy);
int64_t real_sys_sched_get_priority_min(int policy);
int64_t real_sys_sched_rr_get_interval(int pid, void *tp);
int64_t real_sys_memfd_create(const char *name, unsigned int flags);
int64_t real_sys_mlock(uint64_t addr, size_t len);
int64_t real_sys_munlock(uint64_t addr, size_t len);

/* Dynamic linker/relocation diagnostics */
bool compat3_has_dynamic_relocator(void);
int  compat3_dynlink_stats(char *buf, int max);
void compat3_set_next_image_name(const char *path);
uint64_t compat3_lookup_global_symbol(const char *name);
typedef struct {
    uint64_t dlpi_addr;
    uint64_t dlpi_phdr;
    uint16_t dlpi_phnum;
    uint16_t tls_module;
    uint64_t tls_init;
    uint64_t tls_filesz;
    uint64_t tls_memsz;
    uint64_t tls_align;
    char     dlpi_name[128];
} compat3_dlobj_info_t;
int  compat3_dynobj_snapshot(compat3_dlobj_info_t *out, int max);
int  compat3_fork_address_space(task_t *parent, task_t *child);
bool compat3_handle_page_fault(uint64_t fault_addr, uint64_t err_code);

/* Master init + syscall table rewire */
void compat3_init_all(void);
void compat3_rewire_syscalls(void);
void compat3_register_shell_cmds(void);

#endif /* RIDUX_COMPAT3_H */
