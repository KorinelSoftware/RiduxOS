/*
 * Helpers libc/FreeBSD usados por el runtime de usuario.
 */
#ifndef RIDUX_COMPAT5_H
#define RIDUX_COMPAT5_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "base.h"
#include "memory_tasks.h"
#include "linux_syscalls.h"
#include "user_libc.h"

/* fnmatch  (adapted from FreeBSD lib/libc/gen/fnmatch.c, BSD-3) */
#define FNM_NOMATCH     1
#define FNM_PATHNAME    (1<<0)
#define FNM_NOESCAPE    (1<<1)
#define FNM_PERIOD      (1<<2)
#define FNM_LEADING_DIR (1<<3)
#define FNM_CASEFOLD    (1<<4)

int ulibc_fnmatch(const char *pattern, const char *string, int flags);

/* base64  (adapted from FreeBSD lib/libc/net/base64.c, BSD/IBM) */
int ulibc_b64_ntop(const uint8_t *src, size_t srclen, char *tgt, size_t tgtsize);
int ulibc_b64_pton(const char *src, uint8_t *tgt, size_t tgtsize);
int ulibc_base64_encode(const void *src, size_t srclen, char *tgt, size_t tgtsize);
int ulibc_base64_decode(const char *src, void *tgt, size_t tgtsize);

/* arc4random  (adapted from OpenBSD/FreeBSD, ChaCha20-based) */
uint32_t ulibc_arc4random(void);
void     ulibc_arc4random_buf(void *buf, size_t n);
uint32_t ulibc_arc4random_uniform(uint32_t upper_bound);

/* regex  (simplified POSIX regex) */
#define REG_BASIC      0
#define REG_EXTENDED   1
#define REG_ICASE      2
#define REG_NOSUB      4
#define REG_NEWLINE    8
#define REG_NOMATCH    1
#define REG_BADPAT     2
#define REG_ECOLLATE   3
#define REG_ECTYPE     4
#define REG_EESCAPE    5
#define REG_ESUBREG    6
#define REG_EBRACK     7
#define REG_EPAREN     8
#define REG_EBRACE     9
#define REG_BADBR     10
#define REG_ERANGE    11
#define REG_ESPACE    12
#define REG_BADRPT    13

typedef struct {
    int rm_so, rm_eo;
} regmatch_t;

typedef struct ulibc_regex_t {
    void    *re_compiled;  /* opaque internal state */
    size_t   re_nsub;      /* number of paren groups */
    int      re_cflags;
} ulibc_regex_t;

int  ulibc_regcomp(ulibc_regex_t *preg, const char *regex, int cflags);
int  ulibc_regexec(const ulibc_regex_t *preg, const char *string,
                   size_t nmatch, regmatch_t pmatch[], int eflags);
void ulibc_regfree(ulibc_regex_t *preg);
size_t ulibc_regerror(int errcode, const ulibc_regex_t *preg,
                       char *errbuf, size_t errbuf_size);

/* glob  (adapted from FreeBSD lib/libc/gen/glob.c, BSD-3) */
#define GLOB_ERR      (1<<0)
#define GLOB_MARK     (1<<1)
#define GLOB_NOSORT   (1<<2)
#define GLOB_DOOFFS   (1<<3)
#define GLOB_NOCHECK  (1<<4)
#define GLOB_APPEND   (1<<5)
#define GLOB_NOESCAPE (1<<6)
#define GLOB_PERIOD   (1<<7)

#define GLOB_NOSPACE  1
#define GLOB_ABORTED  2
#define GLOB_NOMATCH  3
#define GLOB_NOSYS    4

typedef struct {
    size_t gl_pathc;
    char  **gl_pathv;
    size_t gl_offs;
} ulibc_glob_t;

int ulibc_glob(const char *pattern, int flags,
               int(*errfunc)(const char*,int), ulibc_glob_t *pglob);
void ulibc_globfree(ulibc_glob_t *pglob);

/* scandir / dirent */
typedef struct ulibc_dirent {
    uint64_t d_ino;
    char     d_name[256];
} ulibc_dirent_t;

int ulibc_scandir(const char *dirp,
                  ulibc_dirent_t ***namelist,
                  int(*filter)(const ulibc_dirent_t*),
                  int(*compar)(const ulibc_dirent_t**,const ulibc_dirent_t**));

/* realpath */
char *ulibc_realpath(const char *path, char *resolved);

/* getaddrinfo / DNS stubs  (adapted from FreeBSD, simplified) */
#define AI_PASSIVE     1
#define AI_CANONNAME   2
#define AI_NUMERICHOST 4
#define AI_ADDRCONFIG  0x20
#define AI_V4MAPPED    0x08

/* getaddrinfo error codes (POSIX-compatible subset) */
#define EAI_BADFLAGS   1
#define EAI_NONAME     2
#define EAI_AGAIN      3
#define EAI_FAIL       4
#define EAI_FAMILY     5
#define EAI_SOCKTYPE   6
#define EAI_SERVICE    7
#define EAI_MEMORY     8
#define EAI_SYSTEM     9

#define NI_NUMERICHOST 1
#define NI_NUMERICSERV 2

typedef struct ulibc_sockaddr {
    uint16_t sa_family;
    uint8_t  sa_data[14];
} ulibc_sockaddr_t;

typedef struct ulibc_sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;    /* network byte order */
    uint32_t sin_addr;    /* network byte order */
    char     sin_zero[8];
} ulibc_sockaddr_in_t;

typedef struct ulibc_addrinfo {
    int                ai_flags;
    int                ai_family;
    int                ai_socktype;
    int                ai_protocol;
    size_t             ai_addrlen;
    char              *ai_canonname;
    ulibc_sockaddr_t  *ai_addr;
    struct ulibc_addrinfo *ai_next;
} ulibc_addrinfo_t;

int  ulibc_getaddrinfo(const char *node, const char *service,
                       const ulibc_addrinfo_t *hints, ulibc_addrinfo_t **res);
void ulibc_freeaddrinfo(ulibc_addrinfo_t *res);
int  ulibc_getnameinfo(const ulibc_sockaddr_t *sa, size_t salen,
                       char *host, size_t hostlen,
                       char *serv, size_t servlen, int flags);
const char *ulibc_gai_strerror(int errcode);
int  ulibc_inet_pton(int af, const char *src, void *dst);
const char *ulibc_inet_ntop(int af, const void *src, char *dst, size_t size);
uint32_t ulibc_inet_addr(const char *cp);
uint16_t ulibc_htons(uint16_t v);
uint16_t ulibc_ntohs(uint16_t v);
uint32_t ulibc_htonl(uint32_t v);
uint32_t ulibc_ntohl(uint32_t v);

/* X11 / Wayland protocol stubs */
/* X11 socket path */
#define ULIBC_X11_SOCKET_PATH "/tmp/.X11-unix/X0"

/* Wayland socket path */
#define ULIBC_WAYLAND_SOCKET_PATH "/run/user/0/wayland-0"

/* X11 protocol constants */
#define X11_CreateWindow      1
#define X11_CreateGC          2
#define X11_MapWindow         8
#define X11_PolyFillRect      0x3F
#define X11_PutImage          72
#define X11_GetImage          73

int ulibc_x11_connect(int display);
int ulibc_x11_create_window(int xfd, int x, int y, int w, int h, uint32_t bg);
int ulibc_x11_map_window(int xfd, uint32_t wid);
int ulibc_wayland_connect(void);

/* Browser-specific syscall stubs */
/* seccomp stubs */
#define SECCOMP_MODE_STRICT   1
#define SECCOMP_MODE_FILTER   2
int ulibc_seccomp(unsigned int operation, unsigned int flags, void *args);

/* prctl extensions for browsers */
int ulibc_prctl_set_vma(uint64_t addr, uint64_t size, const char *name);

/* userfaultfd for Chrome */
int ulibc_userfaultfd(int flags);

/* memfd_create already in compat3 */

/* process_vm_readv for debuggers */
int64_t ulibc_process_vm_readv(int pid, const void *lvec, unsigned long liovcnt,
                                const void *rvec, unsigned long riovcnt, unsigned long flags);
int64_t ulibc_process_vm_writev(int pid, const void *lvec, unsigned long liovcnt,
                                const void *rvec, unsigned long riovcnt, unsigned long flags);

/* Shell commands + init */
void compat5_init_all(void);
void compat5_register_shell_cmds(void);

/* Ring 3 launcher.
 *
 * Stage an ELF64 user binary at `path` as a Ring 3 task and mark it
 * runnable so the scheduler picks it up. Does NOT iretq into ring 3
 * synchronously, so the caller (the WM main loop) keeps running.
 *
 *   path     : absolute VFS path to a static or dynamic ELF64 binary
 *   detail   : optional buffer that receives a human readable status
 *   detail_cap: size of `detail`, or 0 if detail is NULL
 *
 * Returns the task pid (>= 0) on success, or a negative errno on
 * failure (-ENOENT if the path doesn't exist, -ENOEXEC if it isn't a
 * valid ELF64, -ENOMEM if address space allocation fails, etc.). */
int compat5_spawn_user_elf_background(const char *path, char *detail, size_t detail_cap);
int compat5_spawn_user_elf_background_args(const char *path, const char *extra_args,
                                           char *detail, size_t detail_cap);

#endif /* RIDUX_COMPAT5_H */
