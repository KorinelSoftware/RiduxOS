/*
 * Declaraciones del runtime de navegador.
 */
#ifndef RIDUX_COMPAT8_H
#define RIDUX_COMPAT8_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "base.h"
#include "memory_tasks.h"
#include "linux_syscalls.h"
#include "user_libc.h"
#include "bsd_libc.h"
#include "linux_abi.h"
#include "display_wayland.h"

/* glibc ABI completion */

/* __libc_start_main prototype - the real entry point wrapper */
typedef int (*c8_main_t)(int, char**, char**);
int __libc_start_main(c8_main_t main, int argc, char** argv,
                      void (*init)(int,char**,char**),
                      void (*fini)(void),
                      void (*rtld_fini)(void),
                      void *stack_end);

/* atexit / cxa_atexit */
#define C8_ATEXIT_MAX 128
int  __cxa_atexit(void (*fn)(void*), void *arg, void *dso_handle);
int  __cxa_finalize(void *dso_handle);
int  atexit(void (*fn)(void));

/* DSO handle */
extern void *__dso_handle;

/* setjmp / longjmp - jmp_buf is 8 64-bit regs + rip + rsp + rbp */
typedef struct {
    uint64_t rbx, rbp, r12, r13, r14, r15;
    uint64_t rsp, rip;
    uint64_t rflags;
    uint64_t mxcsr;
    uint8_t  fpstate[128]; /* simplified x87/SSE state */
    uint64_t sigmask;
    uint8_t  has_sigmask;
} c8_jmp_buf_t;

int  c8_setjmp(c8_jmp_buf_t *env);
void c8_longjmp(c8_jmp_buf_t *env, int val);
int  c8_sigsetjmp(c8_jmp_buf_t *env, int savesigs);
void c8_siglongjmp(c8_jmp_buf_t *env, int val);

/* sigaction full */
typedef struct {
    uint64_t sa_handler;  /* or sa_sigaction */
    uint64_t sa_flags;
    uint64_t sa_mask;
    uint32_t sa_restorer;
} c8_sigaction_t;

int c8_sigaction(int sig, const c8_sigaction_t *act, c8_sigaction_t *old);

/* locale / nl_langinfo */
#define C8_LC_ALL       0
#define C8_LC_COLLATE   1
#define C8_LC_CTYPE     2
#define C8_LC_MONETARY  3
#define C8_LC_NUMERIC   4
#define C8_LC_TIME      5
#define C8_LC_MESSAGES  6

#define C8_CODESET      0
#define C8_D_T_FMT      1
#define C8_D_FMT        2
#define C8_T_FMT        3
#define C8_T_FMT_AMPM   4
#define C8_AM_STR       5
#define C8_PM_STR       6
#define C8_DAY_1        7
#define C8_ABDAY_1      14
#define C8_MON_1        21
#define C8_ABMON_1      33
#define C8_STRFTIME_FMT 45
#define C8_RADIXCHAR    46
#define C8_THOUSEP      47
#define C8_YESSTR       48
#define C8_NOSTR        49
#define C8_CRNCYSTR     50
#define C8_LANG         51
#define C8_TERRITORY    52
#define C8_ALT_DIGITS   53

char *c8_nl_langinfo(int item);
char *c8_setlocale(int category, const char *locale);

/* iconv subset (UTF-8/latin1/ascii conversion) */
typedef struct c8_iconv_desc *c8_iconv_t;
#define C8_ICONV_INVALID ((c8_iconv_t)(uintptr_t)(-1))
c8_iconv_t c8_iconv_open(const char *tocode, const char *fromcode);
size_t c8_iconv(c8_iconv_t cd, char **inbuf, size_t *inbytesleft, char **outbuf, size_t *outbytesleft);
int c8_iconv_close(c8_iconv_t cd);

/* glibc global variables */
extern char **__environ;
extern char **_environ;
extern char *__progname;
extern char *__progname_full;
extern char *program_invocation_name;
extern char *program_invocation_short_name;
extern void *__libc_stack_end;
extern int __libc_enable_secure;
extern int __libc_multiple_threads;
extern int __libc_single_threaded;

/* __libc_init_first, __libc_init_secure, etc. */
void __libc_init_first(int argc, char **argv, char **envp);
void __libc_init_secure(void);

/* gmon stub */
void __gmon_start__(void);
void _mcleanup(void);

/* stack protector - provided by compat4 */

/* misc glibc */
int __cxa_thread_atexit(void (*fn)(void*), void *arg, void *dso);
void __cxa_thread_atexit_impl(void);
void __tls_get_addr_opt(void);
long __syscall(long nr, ...);
int  __libc_sigaction(int sig, const void *act, void *old);
int  __libc_current_sigrtmin(void);
int  __libc_current_sigrtmax(void);

/* Epoll enhancements (edge-triggered, oneshot) */
#define C8_EPOLLET        (1u << 31)
#define C8_EPOLLONESHOT   (1u << 30)
#define C8_EPOLLRDHUP     (1u << 13)
#define C8_EPOLLWAKEUP    (1u << 29)
#define C8_EPOLLEXCLUSIVE (1u << 28)

/* mmap additional flags */
#define C8_MAP_STACK       0x040000
#define C8_MAP_GROWSDOWN   0x0100
#define C8_MAP_POPULATE    0x08000
#define C8_MAP_LOCKED      0x02000
#define C8_MAP_HUGETLB     0x040000
#define C8_MAP_SYNC        0x080000
#define C8_MAP_FIXED_NOREPLACE 0x100000

/* Dynamic loader enhancements */
#define C8_DYNOBJ_MAX 256

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
} c8_dlobj_info_t;

/* dl_iterate_phdr callback */
typedef int (*c8_dl_phdr_cb_t)(const c8_dlobj_info_t *info, size_t sz, void *data);
int  c8_dl_iterate_phdr(c8_dl_phdr_cb_t callback, void *data);

/* dlopen/dlsym/dlclose/dlerror */
void *c8_dlopen(const char *filename, int flags);
void *c8_dlsym(void *handle, const char *symbol);
int   c8_dlclose(void *handle);
char *c8_dlerror(void);

/* Lazy PLT resolution */
void c8_plt_trampoline(void);  /* assembly trampoline entry */
void c8_resolve_plt_entry(uint64_t *got_entry, const char *sym_name);

/* DT_RUNPATH search */
int  c8_dyn_search_runpath(const char *name, char *resolved, size_t rsize);

/* VERSYM resolution */
uint64_t c8_dyn_resolve_versioned(const char *name, uint32_t ver_hash, uint16_t ver_idx);

/* Sandbox syscalls */

/* seccomp */
#define C8_SECCOMP_MODE_STRICT   1
#define C8_SECCOMP_MODE_FILTER   2
#define C8_SECCOMP_SET_MODE_STRICT     0
#define C8_SECCOMP_SET_MODE_FILTER     1
#define C8_SECCOMP_GET_ACTION_AVAIL    2
#define C8_SECCOMP_GET_NOTIF_SIZES     3

/* BPF instruction */
typedef struct {
    uint16_t code;
    uint8_t  jt, jf;
    uint32_t k;
} c8_sock_filter_t;

typedef struct {
    uint16_t len;
    uint16_t padding;
    c8_sock_filter_t *filter;
} c8_sock_fprog_t;

int  c8_seccomp(unsigned int operation, unsigned int flags, void *args);
int  c8_prctl_full(int option, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);

/* userfaultfd */
#define C8_UFFD_API_FEATURES 0
typedef struct {
    uint64_t features;
    uint64_t ioctls;
} c8_uffdio_api_t;

int  c8_userfaultfd(int flags);

/* process_vm_readv/writev */
typedef struct {
    void  *iov_base;
    size_t iov_len;
} c8_iovec_t;

int64_t c8_process_vm_readv(int pid, const c8_iovec_t *lvec, unsigned long liovcnt,
                            const c8_iovec_t *rvec, unsigned long riovcnt, unsigned long flags);
int64_t c8_process_vm_writev(int pid, const c8_iovec_t *lvec, unsigned long liovcnt,
                             const c8_iovec_t *rvec, unsigned long riovcnt, unsigned long flags);

/* capget/capset */
typedef struct {
    uint32_t version;
    uint32_t pad;
    uint32_t effective, permitted, inheritable;
} c8_cap_user_data_t;

int c8_capget(void *hdr, c8_cap_user_data_t *data);
int c8_capset(void *hdr, const c8_cap_user_data_t *data);

/* Software rendering pipeline */
#define C8_BLIT_MAX_WIDTH  4096
#define C8_BLIT_MAX_HEIGHT 4096

/* ARGB32 pixel operations */
void c8_blit_argb32(uint32_t *dst, int dst_stride,
                    const uint32_t *src, int src_stride,
                    int x, int y, int w, int h);
void c8_fill_argb32(uint32_t *dst, int stride, int x, int y, int w, int h, uint32_t color);
void c8_blend_argb32(uint32_t *dst, int dst_stride,
                     const uint32_t *src, int src_stride,
                     int x, int y, int w, int h, uint8_t alpha);
void c8_scale_argb32(uint32_t *dst, int dw, int dh, int dst_stride,
                     const uint32_t *src, int sw, int sh, int src_stride);

/* Rectangle operations (pixman-style) */
typedef struct { int x, y, w, h; } c8_rect_t;
bool c8_rect_intersect(const c8_rect_t *a, const c8_rect_t *b, c8_rect_t *out);

/* Font rendering stubs (freetype-like API) */
typedef struct {
    uint16_t width, height;
    int16_t  bearing_x, bearing_y;
    uint16_t advance;
    uint8_t  *bitmap;  /* 8-bit alpha */
} c8_glyph_t;

typedef struct {
    uint16_t units_per_em;
    int16_t  ascender, descender;
    int      height;
} c8_font_metrics_t;

void c8_font_init(void);
const c8_glyph_t *c8_font_get_glyph(uint32_t codepoint);
const c8_font_metrics_t *c8_font_get_metrics(void);
void c8_render_glyph_argb32(uint32_t *dst, int dst_stride, int dst_w, int dst_h,
                             int x, int y, const c8_glyph_t *g, uint32_t color);

/* Skia software backend glue */
typedef struct {
    uint32_t *pixels;
    int       width, height, stride;
} c8_surface_t;

c8_surface_t *c8_surface_create(int w, int h);
void          c8_surface_destroy(c8_surface_t *s);
void          c8_surface_flush(c8_surface_t *s);

/* IPC / DBus */
#define C8_DBUS_MAX_SERVICES  64
#define C8_DBUS_MAX_MSG_SIZE  8192
#define C8_DBUS_MAX_MATCHES   32

typedef struct {
    bool     used;
    char     name[128];
    uint32_t owner_pid;
    int      sock_fd;  /* unix socket for message delivery */
} c8_dbus_service_t;

typedef struct {
    bool     used;
    int      service_idx;
    char     rule[256];
} c8_dbus_match_t;

typedef struct {
    uint8_t  endian;     /* 'l' or 'B' */
    uint8_t  type;       /* 1=method_call, 2=method_return, 3=error, 4=signal */
    uint8_t  flags;
    uint8_t  version;
    uint32_t body_len;
    uint32_t serial;
    uint32_t hdr_fields_len;
    /* followed by header fields, then body */
} c8_dbus_msg_hdr_t;

/* DBus API */
int  c8_dbus_acquire_name(const char *name, int sock_fd);
int  c8_dbus_release_name(const char *name);
int  c8_dbus_send_signal(const char *dest, const char *path,
                         const char *iface, const char *member,
                         const uint8_t *body, size_t body_len);
int  c8_dbus_add_match(int sock_fd, const char *rule);
int  c8_dbus_remove_match(int sock_fd, const char *rule);
int  c8_dbus_process_incoming(int sock_fd);

/* Unix domain socket path registration */
int  c8_register_unix_socket(const char *path, int sock_fd);
int  c8_lookup_unix_socket(const char *path);

/* Additional syscalls */
int64_t c8_sys_clock_nanosleep(int clockid, int flags, const timespec_t *req, timespec_t *rem);
int64_t c8_sys_getresuid(uint32_t *ruid, uint32_t *euid, uint32_t *suid);
int64_t c8_sys_getresgid(uint32_t *rgid, uint32_t *egid, uint32_t *sgid);
int64_t c8_sys_setresuid(uint32_t ruid, uint32_t euid, uint32_t suid);
int64_t c8_sys_setresgid(uint32_t rgid, uint32_t egid, uint32_t sgid);
int64_t c8_sys_copy_file_range(int fd_in, uint64_t *off_in, int fd_out, uint64_t *off_out, size_t len, unsigned flags);
int64_t c8_sys_preadv2(int fd, const void *iov, int iovcnt, uint64_t pos_l, uint64_t pos_h, int flags);
int64_t c8_sys_pwritev2(int fd, const void *iov, int iovcnt, uint64_t pos_l, uint64_t pos_h, int flags);
int64_t c8_sys_membarrier(int cmd, int flags, int cpu_id);
int64_t c8_sys_rseq(void *rseq, uint32_t rseq_len, int flags, uint32_t sig);
int64_t c8_sys_getcpu(uint32_t *cpu, uint32_t *node, void *tcache);

/* Master init + shell commands */
void compat8_init_all(void);
void compat8_register_shell_cmds(void);
bool compat8_browser_runtime_ready(void);

#endif /* RIDUX_COMPAT8_H */
