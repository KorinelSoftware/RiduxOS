/*
 * Estructuras publicas de memoria, tareas y syscall entry.
 */
#ifndef RIDUX_COMPAT2_H
#define RIDUX_COMPAT2_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "base.h"

/* Physical memory manager (bitmap) */
#define PMM_MAX_PAGES  (2*1024*1024) /* up to 8 GB */

void    pmm_set_alloc_base(uint64_t phys_base);
void    pmm_set_memory_limit(uint64_t mem_bytes);
void    pmm_clear_usable_ranges(void);
void    pmm_add_usable_range(uint64_t phys_base, uint64_t bytes);
void    pmm_init(uint64_t mem_bytes);
uint64_t pmm_alloc_frame(void);
uint64_t pmm_alloc_contiguous_frames(uint32_t pages);
void    pmm_free_frame(uint64_t phys);
uint32_t pmm_free_count(void);
uint32_t pmm_total_count(void);

/* Real paging: PML4 → PDPT → PD → PT */

/* FreeBSD-style Direct Map: all physical RAM is permanently mapped
 * at DMAP_BASE (PML4[256] = VA 0xFFFF800000000000).  The kernel can
 * reach any physical frame via PHYS_TO_DMAP regardless of which user
 * CR3 is currently active — no temporary CR3 switches needed. */
#define DMAP_BASE         0xFFFF800000000000ULL
#define PHYS_TO_DMAP(pa)  ((void*)((uint64_t)(pa) + DMAP_BASE))
#define DMAP_TO_PHYS(va)  ((uint64_t)(va) - DMAP_BASE)
/*
 * User mappings start above the low kernel identity region.  The kernel still
 * executes with process CR3s, so allowing user PTEs below this line can shadow
 * kernel globals in that address space.
 */
#define RIDUX_USER_LOW_SAFE 0x0000000040000000ULL
#define RIDUX_USER_LOW_MAP_MIN 0x0000000000400000ULL

typedef struct {
    uint64_t entries[512];
} page_table_t;

typedef struct {
    page_table_t *pml4;      /* always a DMAP pointer */
    uint64_t      cr3_phys;
} address_space_t;

void          paging_init(void);
address_space_t *paging_create_address_space(void);
void          paging_destroy_address_space(address_space_t *as);
bool          paging_map(address_space_t *as, uint64_t virt, uint64_t phys, uint64_t flags);
bool          paging_unmap(address_space_t *as, uint64_t virt);
uint64_t      paging_translate(address_space_t *as, uint64_t virt);
uint64_t      paging_get_entry(address_space_t *as, uint64_t virt);
bool          paging_set_entry(address_space_t *as, uint64_t virt, uint64_t entry);
void          paging_switch(address_space_t *as);
void          paging_flush_tlb(uint64_t virt);
address_space_t *paging_get_kernel_space(void);

/* TSS + GDT64 for ring3 support */
typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} tss64_t;

void tss_init(void);
void tss_set_rsp0(uint64_t rsp0);

/* GDT64 with ring3 + TSS descriptors */

/* GDT layout:
 *   0x00: Null descriptor
 *   0x08: Kernel 64-bit code  (DPL=0)
 *   0x10: Kernel data         (DPL=0)
 *   0x18: TSS64 low  8 bytes  (DPL=0)
 *   0x20: TSS64 high 8 bytes
 *   0x28: User 64-bit code    (DPL=3)  -> selector 0x28
 *   0x30: User data           (DPL=3)  -> selector 0x30
 *
 * For SYSCALL/SYSRET the hardware expects:
 *   STAR[32:47] = kernel CS  (0x08)
 *   STAR[48:63] = user CS-16 (0x28-0x08 = 0x20, then +0x08=0x28 code, +0x10=0x30 data)
 * So user code = 0x28, user data = 0x30.
 * But the ISR stubs use 0x23/0x1B which assumes the old layout.
 * We'll use the NEW layout and update isr64.S selectors.
 */

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    gdt_entry_t low;
    uint32_t base_high2;
    uint32_t reserved_zero;
} __attribute__((packed)) gdt_tss_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdt_ptr_t;

void gdt64_setup(void);   /* build + load new GDT with ring3 + TSS */
void syscall_msr_init(void);  /* configure STAR/LSTAR/SFMASK MSRs */

/* Per-CPU area accessed via GS base (for SYSCALL entry) */
typedef struct {
    uint64_t kernel_rsp;   /* [gs:0]  = TSS rsp0 / kernel stack top */
    uint64_t user_rsp;     /* [gs:8]  = saved user RSP */
    uint64_t tss_addr;     /* [gs:16] = address of TSS struct */
} per_cpu_t;

extern per_cpu_t g_per_cpu;  /* singleton for uniprocessor */

/* ================================================================
 * 3c. Syscall dispatch (canonical prototype lives in compat.h;
 *     this file re-declares it with the same 7-arg signature for
 *     translation units that include compat2.h but not compat.h).
 * ================================================================ */
int64_t syscall_dispatch(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2,
                         uint64_t a3, uint64_t a4, uint64_t a5);

/* FPU/SSE. The full `task_t` definition lives later in this header
 * (tagged `struct task`); forward-declare the typedef here so prototypes
 * that take `task_t *` compile without depending on include order. */
typedef struct task task_t;
void fpu_init(void);
void fpu_save(task_t *t);
void fpu_restore(task_t *t);

/* Real context / task state */
typedef struct {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rip, rsp, rflags;
    uint64_t cs, ss;
    uint64_t fs_base, gs_base;
} cpu_context_t;

#define TASK_MAX         256
#define TASK_KERNEL_STACK 32768
#define TASK_USER_STACK   (8*1024*1024)
#define TASK_NAME_LEN    32
#define TASK_FPU_STATE_SIZE 4096

typedef enum {
    TASK_FREE = 0,
    TASK_RUNNABLE,
    TASK_RUNNING,
    TASK_SLEEPING,
    TASK_STOPPED,
    TASK_ZOMBIE
} task_state_t;

/* Forward declarations */
struct task;

/* File descriptor table (per-process, real) */
#define TASK_FD_MAX  512
#define FDFL_READABLE  0x01
#define FDFL_WRITABLE  0x02
#define FDFL_APPEND    0x04
#define FDFL_NONBLOCK  0x08
#define FDFL_CLOEXEC   0x10

#define FDKIND_NONE     0
#define FDKIND_VFSFILE  1
#define FDKIND_SOCKET   2
#define FDKIND_PIPE_R   3
#define FDKIND_PIPE_W   4
#define FDKIND_DEVNULL  5
#define FDKIND_DEVZERO  6
#define FDKIND_DEVRANDOM 7
#define FDKIND_DEVFB    8
#define FDKIND_DEVTTY   9
#define FDKIND_DEVPTMX 10
#define FDKIND_PROC    11
#define FDKIND_EPOLL   12
#define FDKIND_EVENTFD 13
#define FDKIND_TIMERFD 14
#define FDKIND_SIGNALFD 15
#define FDKIND_DIR     16
#define FDKIND_INOTIFY 17
#define FDKIND_DEVINPUT 18
#define FDKIND_DEVSND  19
#define FDKIND_SYMLINK 20
#define FDKIND_DEVFUSE 21
#define FDKIND_PIDFD   22

typedef struct {
    uint8_t  kind;
    uint16_t flags;
    int      ref;       /* backend index (vfs slot, socket fd, pipe id, etc.) */
    uint64_t offset;
    int      refcount;  /* for dup/fork sharing */
} real_fd_t;

typedef struct {
    real_fd_t fds[TASK_FD_MAX];
} fd_table_t;

int  fd_alloc(fd_table_t *t, uint8_t kind, int ref, uint16_t flags);
int  fd_dup(fd_table_t *t, int oldfd);
int  fd_dup2(fd_table_t *t, int oldfd, int newfd);
void fd_close(fd_table_t *t, int fd);
bool fd_valid(fd_table_t *t, int fd);

/* Signal infrastructure (real) */
#define NSIG 64

#define SA_RESTART   0x10000000
#define SA_NOCLDSTOP 0x00000001
#define SA_SIGINFO   0x00000004
#define SA_ONSTACK   0x08000000
#define SA_NODEFER   0x40000000
#define SA_RESETHAND 0x80000000

typedef void (*sig_handler_t)(int);
#define SIG_DFL ((sig_handler_t)0)
#define SIG_IGN ((sig_handler_t)1)
#define SIG_ERR ((sig_handler_t)-1)

typedef struct {
    uint64_t sig[2]; /* 128 bits for up to 128 signals */
} sigset_t2;

typedef struct {
    sig_handler_t handler;
    sigset_t2     mask;
    uint32_t      flags;
    void         *restorer;  /* sigreturn trampoline address */
} sigaction_info_t;

/* Signal frame pushed onto user stack for real signal delivery.
 * Layout matches Linux x86-64 rt_sigreturn convention. */
typedef struct {
    /* Saved user context (in the order the kernel pushes for iretq) */
    uint64_t sigmask_sig[2];  /* saved signal mask (16 bytes) */
    uint64_t ucontext_ptr;    /* pointer to ucontext for sigreturn */
    /* ucontext_t (simplified) */
    uint64_t uc_flags;
    uint64_t uc_link;         /* 0 */
    /* altstack: ss_sp, ss_flags, ss_size */
    uint64_t uc_stack_sp;
    uint64_t uc_stack_flags;
    uint64_t uc_stack_size;
    /* Saved registers (mcontext_t) */
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rdi, rsi, rbp, rbx, rdx, rax, rcx;
    uint64_t rsp;       /* saved user RSP at time of signal */
    uint64_t rip;       /* saved user RIP — where to resume */
    uint64_t eflags;
    uint16_t cs, gs, fs, ss;
    uint16_t errcode_hi; /* padding */
    uint64_t trapno;
    uint64_t errcode;
    uint64_t cr2;        /* fault address */
    /* siginfo_t (simplified) */
    int      si_signo;
    int      si_errno;
    int      si_code;
    uint64_t si_addr;    /* faulting address for SIGSEGV etc */
} sig_frame_t;

/* sigreturn trampoline: a tiny code snippet that the kernel
 * places on the user stack so the signal handler can return
 * to it, triggering rt_sigreturn. The trampoline is:
 *   mov $15, %rax   (SYS_rt_sigreturn = 15 on x86-64)
 *   syscall
 * We embed it as 8 bytes of machine code. */
#define SIGRETURN_TRAMP_SIZE 8

/* Full task structure */
typedef struct task {
    bool           used;
    int            pid;
    int            tgid;       /* Linux thread-group id: getpid() value */
    int            ppid;
    int            pgid;
    int            sid;
    int            uid, gid, euid, egid, suid, sgid;
    char           name[TASK_NAME_LEN];
    int            fdt_group;  /* Linux CLONE_FILES descriptor-table group */
    task_state_t   state;
    int            exit_code;
    int            nice;

    /* CPU state */
    cpu_context_t  ctx;
    uint8_t       *kernel_stack;
    uint64_t       kernel_stack_top;
    uint64_t       kernel_rsp_saved;  /* saved RSP for context switch */
    bool           needs_first_launch; /* true if thread hasn't entered ring3 yet */

    /* Extended FPU/SIMD state (large enough for XSAVE with AVX enabled). */
    uint8_t        fpu_state[TASK_FPU_STATE_SIZE] __attribute__((aligned(64)));
    bool           fpu_used;

    /* Address space */
    address_space_t *addr_space;
    uint64_t       brk_start;
    uint64_t       brk_current;
    uint64_t       mmap_base;

    /* File descriptors */
    fd_table_t     fdt;

    /* Signals */
    sigaction_info_t sigactions[NSIG];
    sigset_t2      sig_blocked;
    sigset_t2      sig_pending;
    sigset_t2      sig_saved_mask;
    bool           sig_in_handler;
    int            last_signal;

    /* Scheduler */
    uint64_t       cpu_time;
    uint64_t       sleep_until;
    int            wait_pid;
    uint32_t       syscall_preempt_disable;
    bool           no_timer_preempt;
    uint64_t      *syscall_user_frame;
    uint64_t       syscall_user_frame_copy[22];
    bool           syscall_user_frame_valid;
    uint64_t       clear_child_tid;
    uint64_t       robust_list_head;
    size_t         robust_list_len;

    /* Working directory */
    char           cwd[128];

    /* ELF info */
    int            elf_slot;
    uint64_t       entry_point;
    uint64_t       aux_at_phdr;
    uint64_t       aux_at_phent;
    uint64_t       aux_at_phnum;
    uint64_t       aux_at_base;
    uint64_t       aux_at_flags;
    uint64_t       aux_at_entry;
    uint64_t       aux_at_uid;
    uint64_t       aux_at_euid;
    uint64_t       aux_at_gid;
    uint64_t       aux_at_egid;
    uint64_t       aux_at_random;
    uint64_t       aux_at_execfn;
    uint64_t       aux_at_platform;
    char           exec_path[256];
} task_t;

extern task_t  g_tasks[TASK_MAX];
extern int     g_current_task;
extern int     g_task_next_pid;
extern bool    g_user_foreground_active;
extern volatile uint32_t g_task_preempt_defer_ticks;
extern volatile uint32_t g_kernel_preempt_disable;

void   task_init(void);
int    task_create(const char *name, uint64_t entry, bool is_user);
int    task_create_user_shell(const char *name, uint64_t entry);
int    task_create_user_thread(const char *name, uint64_t entry,
                               address_space_t *shared_as);
int    task_fork(int parent_pid);
void   task_exit(int pid, int code);
int    task_waitpid(int pid, int *status, int options);
void   task_schedule(void);
bool   task_make_runnable(int task_index);
task_t *task_current(void);
uint64_t *task_syscall_user_frame(const task_t *t);
int64_t sig_restore_context(task_t *t);

/* First-time entry to ring 3 for a freshly-staged user task.
 * Commits the task as current, wires CR3 / FS / GS / TSS / RFLAGS,
 * then iretq's. NEVER RETURNS. See compat2.c for the full contract. */
void   task_launch_to_user(task_t *t);

/* ext2 full implementation */
#define EXT2_ROOT_INO       2
#define EXT2_NAME_LEN       255
#define EXT2_NDIR_BLOCKS    12
#define EXT2_IND_BLOCK      12
#define EXT2_DIND_BLOCK     13
#define EXT2_TIND_BLOCK     14
#define EXT2_N_BLOCKS       15

#define EXT2_FT_UNKNOWN  0
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR      2
#define EXT2_FT_CHRDEV   3
#define EXT2_FT_BLKDEV   4
#define EXT2_FT_FIFO     5
#define EXT2_FT_SOCK     6
#define EXT2_FT_SYMLINK  7

typedef struct __attribute__((packed)) {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[EXT2_N_BLOCKS];
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t  i_osd2[12];
} ext2_inode_t;

typedef struct __attribute__((packed)) {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint8_t  bg_reserved[12];
} ext2_group_desc_t;

typedef struct __attribute__((packed)) {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[EXT2_NAME_LEN];
} ext2_dir_entry_t;

bool    ext2_read_inode(int dev, uint32_t ino, ext2_inode_t *out);
int     ext2_read_inode_data(int dev, const ext2_inode_t *ino, uint8_t *buf, uint32_t offset, uint32_t size);
uint32_t ext2_lookup(int dev, uint32_t dir_ino, const char *name);
uint32_t ext2_path_resolve(int dev, const char *path);
int     ext2_list_dir_full(int dev, uint32_t dir_ino, fat32_dirent_t *ents, int max);
bool    ext2_read_file_full(int dev, const char *path, uint8_t *buf, uint32_t buf_size, uint32_t *out_size);

/* Network packet construction */
#define ETH_HEADER_SIZE  14
#define IP_HEADER_SIZE   20
#define TCP_HEADER_SIZE  20
#define UDP_HEADER_SIZE   8
#define ICMP_HEADER_SIZE  8
#define ARP_PACKET_SIZE  28

#define ETH_TYPE_IP   0x0800
#define ETH_TYPE_ARP  0x0806
#define ETH_TYPE_IPV6 0x86DD

typedef struct __attribute__((packed)) {
    uint8_t  dst_mac[6];
    uint8_t  src_mac[6];
    uint16_t ethertype;
} eth_header_t;

typedef struct __attribute__((packed)) {
    uint16_t htype;
    uint16_t ptype;
    uint8_t  hlen;
    uint8_t  plen;
    uint16_t oper;
    uint8_t  sha[6];
    uint32_t spa;
    uint8_t  tha[6];
    uint32_t tpa;
} arp_packet_t;

int     net_build_eth(uint8_t *buf, const uint8_t *dst, const uint8_t *src, uint16_t type);
int     net_build_ip(uint8_t *buf, uint32_t src, uint32_t dst, uint8_t proto, uint16_t payload_len);
int     net_build_udp(uint8_t *buf, uint16_t src_port, uint16_t dst_port, uint16_t data_len);
int     net_build_tcp(uint8_t *buf, uint16_t src_port, uint16_t dst_port, uint32_t seq, uint32_t ack, uint8_t flags, uint16_t window);
int     net_build_icmp_echo(uint8_t *buf, uint16_t id, uint16_t seq, const uint8_t *data, uint16_t data_len);
int     net_build_arp_request(uint8_t *buf, const uint8_t *src_mac, uint32_t src_ip, uint32_t target_ip);
uint16_t net_checksum(const void *data, size_t len);

/* Shared memory (POSIX-ish) */
#define SHM_MAX 16
#define SHM_SIZE_MAX (64*1024*1024)

typedef struct {
    bool     used;
    int      key;
    uint64_t phys;
    uint32_t size;
    int      refcount;
    uint16_t mode;
} shm_region_t;

extern shm_region_t g_shm_regions[SHM_MAX];

int     shm_get(int key, uint32_t size, int flags);
void   *shm_attach(int id, uint64_t addr);
int     shm_detach(void *addr);
int     shm_ctl(int id, int cmd, void *buf);

/* Termios (terminal I/O control) */
#define NCCS 32

/* Input flags */
#define IGNBRK  0x01
#define BRKINT  0x02
#define IGNPAR  0x04
#define INLCR   0x40
#define IGNCR   0x80
#define ICRNL   0x100
#define IXON    0x400

/* Output flags */
#define OPOST   0x01
#define ONLCR   0x04

/* Control flags */
#define CS8     0x30
#define CREAD   0x80

/* Local flags */
#define ISIG    0x01
#define ICANON  0x02
#define ECHO    0x08
#define ECHOE   0x10
#define ECHOK   0x20
#define ECHONL  0x40
#define IEXTEN  0x8000

/* Special control chars */
#define VEOF    4
#define VEOL    11
#define VERASE  2
#define VINTR   0
#define VKILL   3
#define VMIN    6
#define VQUIT   1
#define VSTART  8
#define VSTOP   9
#define VSUSP   10
#define VTIME   5

typedef struct {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t  c_cc[NCCS];
    uint32_t c_ispeed;
    uint32_t c_ospeed;
} termios_t;

typedef struct {
    termios_t tio;
    uint8_t   input_buf[4096];
    int       input_head, input_tail;
    uint8_t   line_buf[4096];
    int       line_len;
    int       fg_pgid;
    uint16_t  rows, cols;
} tty_device_t;

extern tty_device_t g_tty0;

void    tty_init(tty_device_t *tty);
int     tty_read(tty_device_t *tty, void *buf, size_t count);
int     tty_write(tty_device_t *tty, const void *buf, size_t count);
int     tty_ioctl(tty_device_t *tty, uint64_t request, void *arg);
void    tty_input_char(tty_device_t *tty, char c);

/* /dev device operations */
int     dev_null_read(void *buf, size_t count);
int     dev_null_write(const void *buf, size_t count);
int     dev_zero_read(void *buf, size_t count);
int     dev_random_read(void *buf, size_t count);
int     dev_urandom_read(void *buf, size_t count);
int     dev_fb_read(void *buf, size_t count, uint64_t offset);
int     dev_fb_write(const void *buf, size_t count, uint64_t offset);
int     dev_fb_mmap(uint64_t *addr_out, uint32_t size);

/* Timer / alarm */
typedef struct {
    bool     armed;
    uint64_t expire_tick;
    uint64_t interval;
    int      sig;
    int      pid;
} timer_entry_t;

#define TIMER_MAX 32
extern timer_entry_t g_timers[TIMER_MAX];

int     timer_create_entry(int pid, uint64_t expire, uint64_t interval, int sig);
void    timer_tick(uint64_t current_tick);
int     sys_alarm(uint32_t seconds);

/* Process groups / sessions */
int     task_setpgid(int pid, int pgid);
int     task_getpgid(int pid);
int     task_setsid(int pid);
int     task_getsid(int pid);

/* Real readv / writev */
typedef struct {
    void   *iov_base;
    size_t  iov_len;
} iovec_t;

int64_t real_readv(int fd, const iovec_t *iov, int iovcnt);
int64_t real_writev(int fd, const iovec_t *iov, int iovcnt);

/* stat structure */
typedef struct {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad0;
    uint64_t st_rdev;
    int64_t  st_size;
    int64_t  st_blksize;
    int64_t  st_blocks;
    int64_t  st_atime_sec;
    int64_t  st_atime_nsec;
    int64_t  st_mtime_sec;
    int64_t  st_mtime_nsec;
    int64_t  st_ctime_sec;
    int64_t  st_ctime_nsec;
    int64_t  __reserved[3];
} kstat_t;

int     vfs_stat(const char *path, kstat_t *st);
int     vfs_fstat(int fd, kstat_t *st);
int     vfs_lstat(const char *path, kstat_t *st);

/* Master init */
void    compat2_init_all(void);
void    compat2_register_shell_cmds(void);

#endif /* RIDUX_COMPAT2_H */
