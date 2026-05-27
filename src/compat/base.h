/*
 * Tipos y APIs base para la capa de compatibilidad.
 */
#ifndef RIDUX_COMPAT_H
#define RIDUX_COMPAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Boot-time direct-UART trace helpers defined in kernel.c. These
 * bypass g_serial_ready so any TU can emit diagnostic output at any
 * point, including before drivers come up. Useful for root-causing
 * hangs that happen in compat*_init or in the ELF64 launch path. */
void __boot_serial_init_direct(void);
void __boot_serial_putc(char c);
void __boot_serial_puts(const char *s);
void __boot_serial_puthex64(uint64_t v);
void __boot_serial_putu32(uint32_t v);
void __boot_serial_force_putc(char c);
void __boot_serial_force_puts(const char *s);
void __boot_serial_force_puthex64(uint64_t v);
void __boot_serial_force_putu32(uint32_t v);

/* Syscall/task context helpers implemented in compat2.c. */
void task_capture_syscall_user_context(void);
void task_restore_current_user_msrs(void);
void task_browser_coop_yield_after_syscall(uint64_t nr);

/* ATA / block device layer */

#define ATA_SECTOR_SIZE   512
#define ATA_MAX_DISKS     4
#define BLOCK_MAX_DEVS    8

typedef struct {
    bool     present;
    bool     is_atapi;
    uint8_t  channel;   /* 0=primary, 1=secondary */
    uint8_t  drive;     /* 0=master, 1=slave */
    uint32_t sectors;   /* total LBA28 sector count */
    uint32_t sectors_hi;/* upper 32 bits for LBA48 */
    char     model[41];
    char     serial[21];
    char     firmware[9];
    uint16_t identify[256];
} ata_disk_t;

typedef struct {
    bool        present;
    const char *name;
    const char *type;     /* "ata", "ahci", "nvme", "ram" */
    uint32_t    sector_sz;
    uint64_t    total_sectors;
    int         ata_index; /* -1 if not ATA */
} block_dev_t;

extern ata_disk_t  g_ata_disks[ATA_MAX_DISKS];
extern int         g_ata_disk_count;
extern block_dev_t g_block_devs[BLOCK_MAX_DEVS];
extern int         g_block_dev_count;

void ata_init(void);
bool ata_read_sectors(int disk, uint32_t lba, uint8_t count, void *buf);
bool ata_write_sectors(int disk, uint32_t lba, uint8_t count, const void *buf);

/* AHCI (SATA) stub */

#define AHCI_MAX_PORTS 8

typedef struct {
    bool     present;
    uint8_t  port;
    uint8_t  type;    /* 0=none, 1=SATA, 2=SATAPI, 3=SEMB, 4=PM */
    uint64_t sectors;
    char     model[41];
} ahci_port_t;

extern ahci_port_t g_ahci_ports[AHCI_MAX_PORTS];
extern int         g_ahci_port_count;
extern bool        g_ahci_present;

void ahci_init(void);

/* Network interface / NIC driver stubs */

#define NET_MAC_LEN     6
#define NET_MAX_IFACES  4
#define NET_MTU         1500
#define NET_RX_RING     32
#define NET_TX_RING     16

typedef struct {
    bool     up;
    bool     present;
    char     name[16];
    char     driver[16];
    uint8_t  mac[NET_MAC_LEN];
    uint32_t ip4;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_errors;
    uint64_t tx_errors;
    /* Hardware ring pointers */
    uint32_t mmio_base;
    uint32_t irq;
    /* E1000 DMA ring state */
    void    *tx_descs;        /* TX descriptor ring (aligned) */
    void    *rx_descs;        /* RX descriptor ring (aligned) */
    void    *tx_bufs[NET_TX_RING];  /* TX buffer pointers */
    void    *rx_bufs[NET_RX_RING];  /* RX buffer pointers */
    uint32_t tx_head;
    uint32_t tx_tail;
    uint32_t rx_head;
    uint32_t rx_tail;
    bool     rx_pending;      /* packets waiting to be processed */
} net_iface_t;

extern net_iface_t g_net_ifaces[NET_MAX_IFACES];
extern int         g_net_iface_count;

void nic_init(void);
void net_iface_up(int idx);
void net_iface_down(int idx);
int  e1000_send(const uint8_t *frame, size_t len);
int  e1000_recv(uint8_t *buf, size_t buf_size);
void e1000_poll_rx(void);
void net_process_frame(const uint8_t *frame, size_t len);

/* Real network frame helpers — build full Eth+IP+TCP/UDP and send via E1000 */
int  net_send_tcp_frame(int sock_fd, const uint8_t *tcp_seg, size_t seg_len);
int  net_send_udp_frame(int sock_fd, const uint8_t *payload, size_t len);
bool net_is_real_external(uint32_t ip);

/* TCP/IP stack */

/* IP */
#define IP_PROTO_ICMP  1
#define IP_PROTO_TCP   6
#define IP_PROTO_UDP  17

typedef struct __attribute__((packed)) {
    uint8_t  ver_ihl;
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} ip_header_t;

/* ICMP */
typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} icmp_header_t;

/* UDP */
typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} udp_header_t;

/* TCP */
typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  data_off;  /* upper 4 bits = offset in 32-bit words */
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} tcp_header_t;

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10
#define TCP_URG 0x20

typedef enum {
    TCP_CLOSED = 0,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RCVD,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_CLOSE_WAIT,
    TCP_CLOSING,
    TCP_LAST_ACK,
    TCP_TIME_WAIT
} tcp_state_t;

#define SOCK_MAX        384
#define SOCK_BUF_SIZE   65536
#define SOCK_ACCEPTQ    16
#define SOCK_DGRAMQ     32
#define SOCK_ANCQ       64
#define SOCK_VIRT_NONE   0
#define SOCK_VIRT_HTTP   1
#define SOCK_VIRT_HTTPS  2
#define SOCK_VIRT_X11    3
#define SOCK_VIRT_WL     4
#define SOCK_VIRT_DBUS   5

typedef struct {
    bool       used;
    int        domain;     /* AF_INET=2 */
    int        type;       /* SOCK_STREAM=1, SOCK_DGRAM=2 */
    int        protocol;
    int        peer;       /* socketpair peer index, -1 if none */
    tcp_state_t tcp_state;
    uint32_t   local_ip;
    uint16_t   local_port;
    uint32_t   remote_ip;
    uint16_t   remote_port;
    uint32_t   tcp_seq;
    uint32_t   tcp_ack;
    uint16_t   tcp_window;
    uint8_t    rx_buf[SOCK_BUF_SIZE];
    uint32_t   rx_head;
    uint32_t   rx_tail;
    uint8_t    tx_buf[SOCK_BUF_SIZE];
    uint32_t   tx_head;
    uint32_t   tx_tail;
    int        anc_fds[SOCK_ANCQ];
    uint32_t   anc_pos[SOCK_ANCQ];
    uint8_t    anc_head;
    uint8_t    anc_tail;
    uint16_t   rx_msg_len[SOCK_DGRAMQ];
    uint32_t   rx_msg_ip[SOCK_DGRAMQ];
    uint16_t   rx_msg_port[SOCK_DGRAMQ];
    uint8_t    rx_msg_head;
    uint8_t    rx_msg_tail;
    int        accept_q[SOCK_ACCEPTQ];
    uint8_t    accept_head;
    uint8_t    accept_tail;
    bool       non_blocking;
    int        backlog;
    int        error;
    uint8_t    virt_service;
    uint8_t    virt_state;
    uint16_t   virt_flags;
    uint8_t    shutdown_rx;
    uint8_t    shutdown_tx;
} socket_t;

extern socket_t g_sockets[SOCK_MAX];

void    tcp_ip_init(void);
int     sock_create(int domain, int type, int protocol);
int     sock_bind(int fd, uint32_t addr, uint16_t port);
int     sock_listen(int fd, int backlog);
int     sock_accept(int fd, uint32_t *addr, uint16_t *port);
int     sock_connect(int fd, uint32_t addr, uint16_t port);
int     sock_pair_create(int domain, int type, int protocol, int out_pair[2]);
int     sock_send_right(int fd, int pass_fd);
int     sock_recv_right(int fd, int *pass_fd);
size_t  sock_recv_limit_before_right(int fd, size_t cap);
int     sock_send(int fd, const void *buf, size_t len, int flags);
int     sock_recv(int fd, void *buf, size_t len, int flags);
int     sock_shutdown(int fd, int how);
int     sock_close(int fd);
int     sock_setsockopt(int fd, int level, int optname, const void *val, size_t len);

/* ARP */
#define ARP_CACHE_SIZE 32
typedef struct {
    uint32_t ip;
    uint8_t  mac[NET_MAC_LEN];
    uint32_t timestamp;
    bool     valid;
} compat_arp_entry_t;

extern compat_arp_entry_t g_arp_cache[ARP_CACHE_SIZE];
void arp_init(void);
bool arp_resolve(uint32_t ip, uint8_t *mac_out);
void arp_process(const uint8_t *frame, size_t len);

/* DHCP */
typedef struct {
    bool     obtained;
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns;
    uint32_t lease_time;
    uint32_t server_ip;
} dhcp_lease_t;

extern dhcp_lease_t g_dhcp_lease;
void dhcp_discover(int iface);
void dhcp_process(const uint8_t *data, size_t len);

/* DNS */
#define DNS_CACHE_SIZE 16
typedef struct {
    char     name[64];
    uint32_t ip;
    uint32_t ttl;
    bool     valid;
} dns_entry_t;

extern dns_entry_t g_dns_cache[DNS_CACHE_SIZE];
bool dns_resolve(const char *hostname, uint32_t *ip_out);

/* Filesystem: FAT32, ext2 */

/* FAT32 */
typedef struct {
    bool     mounted;
    int      block_dev;
    uint32_t fat_start_lba;
    uint32_t cluster_start_lba;
    uint32_t root_cluster;
    uint32_t sectors_per_fat;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint16_t bytes_per_sector;
    uint8_t  num_fats;
    char     label[12];
} fat32_fs_t;

typedef struct {
    char     name[256];
    uint32_t cluster;
    uint32_t size;
    uint8_t  attr;
    bool     is_dir;
} fat32_dirent_t;

#define FAT32_MAX_DIR_ENTRIES 128

extern fat32_fs_t g_fat32;
bool fat32_mount(int block_dev);
bool fat32_read_file(const char *path, uint8_t *buf, uint32_t buf_size, uint32_t *out_size);
int  fat32_list_dir(const char *path, fat32_dirent_t *entries, int max);

/* ext2 */
typedef struct {
    bool     mounted;
    int      block_dev;
    uint32_t block_size;
    uint32_t blocks_count;
    uint32_t inodes_count;
    uint32_t inodes_per_group;
    uint32_t blocks_per_group;
    uint32_t first_data_block;
    uint16_t inode_size;
    char     volume_name[17];
} ext2_fs_t;

extern ext2_fs_t g_ext2;
bool ext2_mount(int block_dev);
bool ext2_read_file(const char *path, uint8_t *buf, uint32_t buf_size, uint32_t *out_size);

/* Mount table */
#define MOUNT_MAX 8
typedef struct {
    bool  used;
    char  source[32];
    char  target[32];
    char  fstype[16];
    int   block_dev;
} mount_entry_t;

extern mount_entry_t g_mounts[MOUNT_MAX];
extern int           g_mount_count;

bool vfs_ext_mount(const char *source, const char *target, const char *fstype);
bool vfs_ext_umount(const char *target);

/* Linux syscall compatibility layer */

#define SYSCALL_MAX 512

/* Signal definitions (POSIX) */
#define SIGHUP     1
#define SIGINT     2
#define SIGQUIT    3
#define SIGILL     4
#define SIGTRAP    5
#define SIGABRT    6
#define SIGBUS     7
#define SIGFPE     8
#define SIGKILL    9
#define SIGUSR1   10
#define SIGSEGV   11
#define SIGUSR2   12
#define SIGPIPE   13
#define SIGALRM   14
#define SIGTERM   15
#define SIGSTKFLT 16
#define SIGCHLD   17
#define SIGCONT   18
#define SIGSTOP   19
#define SIGTSTP   20
#define SIGTTIN   21
#define SIGTTOU   22
#define SIGURG    23
#define SIGXCPU   24
#define SIGXFSZ   25
#define SIGVTALRM 26
#define SIGPROF   27
#define SIGWINCH  28
#define SIGIO     29
#define SIGPWR    30
#define SIGSYS    31

#define SIG_MAX   32

typedef void (*sighandler_t)(int);

/* errno values */
#define EPERM        1
#define ENOENT       2
#define ESRCH        3
#define EINTR        4
#define EIO          5
#define ENXIO        6
#define E2BIG        7
#define ENOEXEC      8
#define EBADF        9
#define ECHILD      10
#define EAGAIN      11
#define ENOMEM      12
#define EACCES      13
#define EFAULT      14
#define ENOTBLK     15
#define EBUSY       16
#define EEXIST      17
#define EXDEV       18
#define ENODEV      19
#define ENOTDIR     20
#define EISDIR      21
#define EINVAL      22
#define ENFILE      23
#define EMFILE      24
#define ENOTTY      25
#define ETXTBSY     26
#define EFBIG       27
#define ENOSPC      28
#define ESPIPE      29
#define EROFS       30
#define EMLINK      31
#define EPIPE       32
#define EDOM        33
#define ERANGE      34
#define ENOSYS      38
#define ENOTEMPTY   39
#define ELOOP       40
#define EADDRINUSE  98
#define EISCONN    106
#define ENOTCONN   107
#define ENOTSOCK    88
#define ECONNREFUSED 111
#define ETIMEDOUT   110
#define EINPROGRESS 115
#define EALREADY    114

/* File descriptor table per process */
#define FD_MAX   512
#define FD_TYPE_NONE    0
#define FD_TYPE_FILE    1
#define FD_TYPE_SOCKET  2
#define FD_TYPE_PIPE    3
#define FD_TYPE_DEVNULL 4
#define FD_TYPE_DEVFB   5
#define FD_TYPE_DEVTTY  6
#define FD_TYPE_DEVRANDOM 7
#define FD_TYPE_PROC    8
#define FD_TYPE_STDIN   9
#define FD_TYPE_STDOUT 10
#define FD_TYPE_STDERR 11

typedef struct {
    uint8_t  type;
    int      ref;      /* underlying index (vfs slot, socket fd, etc.) */
    uint32_t offset;
    uint16_t flags;
} fd_entry_t;

/* Process compat context (per-process POSIX state) */
#define PROC_COMPAT_MAX 64

typedef struct {
    bool        used;
    int         pid;
    int         ppid;
    int         pgid;
    int         uid, gid, euid, egid;
    fd_entry_t  fds[FD_MAX];
    sighandler_t sig_handlers[SIG_MAX];
    uint64_t    sig_mask;
    uint64_t    sig_pending;
    uint64_t    brk_base;
    uint64_t    brk_current;
    char        cwd[128];
    int         exit_code;
} proc_compat_t;

extern proc_compat_t g_proc_compat[PROC_COMPAT_MAX];

typedef int64_t (*syscall_fn_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern syscall_fn_t g_syscall_table[SYSCALL_MAX];

void     syscall_init(void);
int64_t  syscall_dispatch(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2,
                          uint64_t a3, uint64_t a4, uint64_t a5);
void     compat_syscall_trace_note(uint64_t nr, uint64_t a0, int64_t ret);
void     compat_syscall_trace_reset(void);
void     compat_syscall_trace_wayfire_after_keymap(void);

/* Virtual memory / paging stubs */

#define PAGE_SIZE      4096
#define PAGE_PRESENT   0x001
#define PAGE_WRITABLE  0x002
#define PAGE_USER      0x004
#define PAGE_ACCESSED  0x020
#define PAGE_DIRTY     0x040
#define PAGE_PS        0x080  /* 2MB page */
#define PAGE_NX        (1ULL << 63)

typedef struct {
    uint64_t *pml4;
    uint64_t  cr3;
    uint64_t  virt_base;
    uint64_t  virt_end;
    uint64_t  phys_bitmap_start;
    uint32_t  total_pages;
    uint32_t  free_pages;
} vmm_context_t;

extern vmm_context_t g_kernel_vmm;

void     vmm_init(uint64_t mem_end);
uint64_t vmm_alloc_page(void);
void     vmm_free_page(uint64_t phys);
bool     vmm_map_page(vmm_context_t *ctx, uint64_t virt, uint64_t phys, uint64_t flags);
bool     vmm_unmap_page(vmm_context_t *ctx, uint64_t virt);
uint64_t vmm_virt_to_phys(vmm_context_t *ctx, uint64_t virt);

/* mmap */
#define MMAP_MAX_REGIONS 64
typedef struct {
    uint64_t virt;
    uint64_t size;
    int      prot;
    int      flags;
    int      fd;
    uint64_t offset;
    bool     used;
} mmap_region_t;

int64_t sys_mmap(uint64_t addr, uint64_t length, int prot, int flags, int fd, uint64_t offset);
int64_t sys_munmap(uint64_t addr, uint64_t length);
int64_t sys_mprotect(uint64_t addr, uint64_t length, int prot);
int64_t sys_brk(uint64_t addr);

/* ELF64 loader */

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) elf64_ehdr_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed)) elf64_phdr_t;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} __attribute__((packed)) elf64_shdr_t;

typedef struct {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} __attribute__((packed)) elf64_sym_t;

typedef struct {
    int64_t  d_tag;
    uint64_t d_val;
} __attribute__((packed)) elf64_dyn_t;

#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_TLS     7
#define PT_PHDR    6

#define ET_EXEC 2
#define ET_DYN  3

#define EM_X86_64  62

#define ELF64_MAX_IMAGES 8
#define ELF64_IMAGE_MAX  (256*1024*1024) /* large browser DSOs, e.g. Firefox libxul.so */

typedef struct {
    bool     used;
    char     path[128];
    uint64_t entry;
    uint64_t base_vaddr;
    uint64_t load_end;
    uint64_t brk;
    uint32_t phdr_count;
    bool     is_dynamic;
    char     interp[128];
} elf64_image_t;

extern elf64_image_t g_elf64_images[ELF64_MAX_IMAGES];

int  elf64_load(const uint8_t *data, uint32_t size, const char *name);
bool elf64_validate(const uint8_t *data, uint32_t size);
void compat_ui_open_app(const char *name);

/* Threading / synchronization primitives */

#define THREAD_MAX 256
#define THREAD_STACK_SIZE 16384

typedef enum {
    THREAD_UNUSED = 0,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_ZOMBIE
} thread_state_t;

typedef struct {
    bool          used;
    int           tid;
    int           pid;     /* owner process */
    thread_state_t state;
    uint64_t      rsp;
    uint64_t      rip;
    uint64_t      stack_base;
    uint64_t      stack_size;
    int           priority;
    uint64_t      cpu_time;
    void         *tls;
} thread_t;

typedef struct {
    volatile int lock;
    int          owner_tid;
    int          type;  /* 0=normal, 1=recursive, 2=errorcheck */
    int          count;
} mutex_t;

typedef struct {
    volatile int waiters;
    mutex_t     *mutex;
} condvar_t;

typedef struct {
    volatile int value;
} semaphore_t;

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

extern thread_t g_threads[THREAD_MAX];

void thread_init(void);
int  thread_create(int pid, uint64_t entry, uint64_t arg);
void thread_exit(int tid, int code);
void thread_yield(void);
int  thread_join(int tid);

void mutex_init(mutex_t *m, int type);
void mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);
bool mutex_trylock(mutex_t *m);

void condvar_init(condvar_t *cv);
void condvar_wait(condvar_t *cv, mutex_t *m);
void condvar_signal(condvar_t *cv);
void condvar_broadcast(condvar_t *cv);

void sem_init(semaphore_t *s, int value);
void sem_wait(semaphore_t *s);
void sem_post(semaphore_t *s);

int64_t sys_futex(uint32_t *uaddr, int op, uint32_t val, uint64_t timeout);

/* /dev, /proc, /sys virtual filesystem generators */

#define VDEV_MAX 32

typedef struct {
    bool  present;
    char  name[32];
    char  type[16];  /* "char", "block", "misc" */
    int   major;
    int   minor;
} vdev_entry_t;

extern vdev_entry_t g_vdevs[VDEV_MAX];
extern int          g_vdev_count;

void vdev_init(void);
int  vdev_generate_proc_stat(char *buf, int max);
int  vdev_generate_proc_meminfo(char *buf, int max);
int  vdev_generate_proc_cpuinfo(char *buf, int max);
int  vdev_generate_proc_version(char *buf, int max);
int  vdev_generate_proc_uptime(char *buf, int max);
int  vdev_generate_proc_mounts(char *buf, int max);
int  vdev_generate_proc_net_dev(char *buf, int max);
int  vdev_generate_sys_kernel_hostname(char *buf, int max);
int  vdev_generate_sys_kernel_osrelease(char *buf, int max);

/* Linux-style desktop devices exposed by the compat layer. */

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint64_t fb_phys;
    uint32_t fb_size;
} drm_mode_t;

typedef struct {
    uint64_t time_sec;
    uint64_t time_usec;
    uint16_t type;
    uint16_t code;
    int32_t  value;
} input_event_t;

/* evdev types */
#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02
#define EV_ABS 0x03

#define SYN_REPORT 0x00

/* Common key codes (Linux evdev) */
#define KEY_ESC       1
#define KEY_ENTER    28
#define KEY_SPACE    57
#define KEY_BACKSPACE 14

#define INPUT_EVENT_RING 512

typedef struct {
    input_event_t events[INPUT_EVENT_RING];
    int head, tail;
} evdev_device_t;

extern evdev_device_t g_evdev_kbd;
extern evdev_device_t g_evdev_mouse;
extern drm_mode_t     g_drm_mode;

void drm_init(void);
void evdev_init(void);
void evdev_push_key(uint16_t code, int32_t value);
void evdev_push_mouse_key(uint16_t code, int32_t value);
void evdev_push_rel(uint16_t code, int32_t value);
void evdev_push_rel_xy(int32_t dx, int32_t dy);

/* epoll / poll / select stubs */

#define EPOLL_MAX_EVENTS 64
#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3
#define EPOLLIN   0x001
#define EPOLLOUT  0x004
#define EPOLLERR  0x008
#define EPOLLHUP  0x010

typedef struct {
    uint32_t events;
    uint64_t data;
} __attribute__((packed)) epoll_event_t;

typedef struct {
    bool         used;
    int          fd;
    uint32_t     events;
    uint64_t     data;
} epoll_item_t;

#define EPOLL_INSTANCES 64
typedef struct {
    bool       used;
    epoll_item_t items[EPOLL_MAX_EVENTS];
    int        count;
    int        next_scan;
} epoll_instance_t;

extern epoll_instance_t g_epoll_instances[EPOLL_INSTANCES];

int     sys_epoll_create(int size);
int     sys_epoll_ctl(int epfd, int op, int fd, epoll_event_t *ev);
int     sys_epoll_wait(int epfd, epoll_event_t *events, int maxevents, int timeout);
int     sys_poll(void *fds, uint64_t nfds, int timeout);
int     sys_select(int nfds, void *readfds, void *writefds, void *exceptfds, void *timeout);

/* Pipe support */

#define PIPE_BUF_SIZE 4096
#define PIPE_MAX      16

typedef struct {
    bool    used;
    uint8_t buf[PIPE_BUF_SIZE];
    int     head, tail;
    int     readers;
    int     writers;
} pipe_t;

extern pipe_t g_pipes[PIPE_MAX];

int  pipe_create(int fds[2]);
int  pipe_read(int pipe_id, void *buf, size_t count);
int  pipe_write(int pipe_id, const void *buf, size_t count);
void pipe_close_read(int pipe_id);
void pipe_close_write(int pipe_id);

/* Master init - call from kernel_main */

void compat_init_all(void);

/* Summary / info for shell */
int  compat_summary(char *buf, int max);

/* Shell command interface */
typedef void (*compat_shell_fn_t)(const char *args, char *out, int out_max);

#define COMPAT_SHELL_CMD_MAX 64
typedef struct {
    const char        *name;
    const char        *help;
    compat_shell_fn_t  handler;
} compat_shell_cmd_t;

extern compat_shell_cmd_t g_compat_cmds[COMPAT_SHELL_CMD_MAX];
extern int                g_compat_cmd_count;

bool compat_shell_dispatch(const char *name, const char *args, char *out, int out_max);

#endif /* RIDUX_COMPAT_H */
