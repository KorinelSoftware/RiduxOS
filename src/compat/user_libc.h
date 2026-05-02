/*
 * Declaraciones de la mini libc de Ridux.
 */
#ifndef RIDUX_COMPAT4_H
#define RIDUX_COMPAT4_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "base.h"
#include "memory_tasks.h"
#include "linux_syscalls.h"

/* errno */
extern int *__errno_location(void);
#define ULIBC_ERRNO (*__errno_location())

/* string.h */
void  *ulibc_memcpy(void *dst, const void *src, size_t n);
void  *ulibc_memmove(void *dst, const void *src, size_t n);
void  *ulibc_memset(void *s, int c, size_t n);
int    ulibc_memcmp(const void *s1, const void *s2, size_t n);
void  *ulibc_memchr(const void *s, int c, size_t n);
size_t ulibc_strlen(const char *s);
size_t ulibc_strnlen(const char *s, size_t max);
char  *ulibc_strcpy(char *dst, const char *src);
char  *ulibc_strncpy(char *dst, const char *src, size_t n);
int    ulibc_strcmp(const char *a, const char *b);
int    ulibc_strncmp(const char *a, const char *b, size_t n);
char  *ulibc_strcat(char *dst, const char *src);
char  *ulibc_strncat(char *dst, const char *src, size_t n);
char  *ulibc_strchr(const char *s, int c);
char  *ulibc_strrchr(const char *s, int c);
char  *ulibc_strstr(const char *h, const char *n);
char  *ulibc_strdup(const char *s);
char  *ulibc_strndup(const char *s, size_t n);
char  *ulibc_strtok(char *str, const char *delim);
char  *ulibc_strtok_r(char *str, const char *delim, char **saveptr);
size_t ulibc_strspn(const char *s, const char *accept);
size_t ulibc_strcspn(const char *s, const char *reject);
char  *ulibc_strpbrk(const char *s, const char *accept);
char  *ulibc_strerror(int errnum);
int    ulibc_strcasecmp(const char *a, const char *b);
int    ulibc_strncasecmp(const char *a, const char *b, size_t n);

/* stdlib.h - memory allocation (dlmalloc-inspired) */
#define ULIBC_HEAP_SIZE (16*1024*1024) /* 16MB internal heap */

void  *ulibc_malloc(size_t size);
void   ulibc_free(void *ptr);
void  *ulibc_realloc(void *ptr, size_t size);
void  *ulibc_calloc(size_t nmemb, size_t size);
void  *ulibc_memalign(size_t align, size_t size);
int    ulibc_posix_memalign(void **memptr, size_t align, size_t size);

/* stdlib.h - conversion */
int    ulibc_atoi(const char *s);
long   ulibc_atol(const char *s);
long long ulibc_atoll(const char *s);
long   ulibc_strtol(const char *s, char **endp, int base);
unsigned long ulibc_strtoul(const char *s, char **endp, int base);
long long ulibc_strtoll(const char *s, char **endp, int base);
unsigned long long ulibc_strtoull(const char *s, char **endp, int base);
long   ulibc_strtod(const char *s, char **endp); /* returns integer part; no FPU */

/* stdlib.h - sort/search */
void   ulibc_qsort(void *base, size_t nmemb, size_t size, int(*cmp)(const void*,const void*));
void  *ulibc_bsearch(const void *key, const void *base, size_t nmemb, size_t size, int(*cmp)(const void*,const void*));

/* stdlib.h - misc */
int    ulibc_abs(int x);
long   ulibc_labs(long x);
void   ulibc_abort(void);
void   ulibc_exit(int status);
void   ulibc__exit(int status);
int    ulibc_atexit(void(*func)(void));
char  *ulibc_getenv(const char *name);
int    ulibc_setenv(const char *name, const char *value, int overwrite);
int    ulibc_unsetenv(const char *name);

/* stdlib.h - random */
int    ulibc_rand(void);
void   ulibc_srand(unsigned int seed);
long   ulibc_random(void);
void   ulibc_srandom(unsigned int seed);

/* stdio.h */
#define ULIBC_BUFSIZ 4096
#define ULIBC_EOF    (-1)
#define ULIBC_FOPEN_MAX 64

typedef struct ulibc_FILE {
    int   fd;
    int   flags;
    int   error;
    int   eof;
    int   ungot;
    uint8_t buf[ULIBC_BUFSIZ];
    size_t buf_pos;
    size_t buf_len;
    int   buf_mode; /* 0=full,1=line,2=none */
} ulibc_FILE;

extern ulibc_FILE *ulibc_stdin;
extern ulibc_FILE *ulibc_stdout;
extern ulibc_FILE *ulibc_stderr;

ulibc_FILE *ulibc_fopen(const char *path, const char *mode);
ulibc_FILE *ulibc_fdopen(int fd, const char *mode);
int    ulibc_fclose(ulibc_FILE *f);
size_t ulibc_fread(void *ptr, size_t size, size_t nmemb, ulibc_FILE *f);
size_t ulibc_fwrite(const void *ptr, size_t size, size_t nmemb, ulibc_FILE *f);
int    ulibc_fgetc(ulibc_FILE *f);
int    ulibc_fputc(int c, ulibc_FILE *f);
char  *ulibc_fgets(char *s, int size, ulibc_FILE *f);
int    ulibc_fputs(const char *s, ulibc_FILE *f);
int    ulibc_ungetc(int c, ulibc_FILE *f);
int    ulibc_fseek(ulibc_FILE *f, long offset, int whence);
long   ulibc_ftell(ulibc_FILE *f);
void   ulibc_rewind(ulibc_FILE *f);
int    ulibc_fflush(ulibc_FILE *f);
int    ulibc_feof(ulibc_FILE *f);
int    ulibc_ferror(ulibc_FILE *f);
void   ulibc_clearerr(ulibc_FILE *f);
int    ulibc_fileno(ulibc_FILE *f);
int    ulibc_fprintf(ulibc_FILE *f, const char *fmt, ...);
int    ulibc_printf(const char *fmt, ...);
int    ulibc_sprintf(char *buf, const char *fmt, ...);
int    ulibc_snprintf(char *buf, size_t size, const char *fmt, ...);
int    ulibc_vsnprintf(char *buf, size_t size, const char *fmt, __builtin_va_list ap);
int    ulibc_vfprintf(ulibc_FILE *f, const char *fmt, __builtin_va_list ap);
int    ulibc_sscanf(const char *str, const char *fmt, ...);
int    ulibc_puts(const char *s);
void   ulibc_perror(const char *s);

/* ctype.h */
int ulibc_isalpha(int c);
int ulibc_isdigit(int c);
int ulibc_isalnum(int c);
int ulibc_isspace(int c);
int ulibc_isupper(int c);
int ulibc_islower(int c);
int ulibc_isprint(int c);
int ulibc_isgraph(int c);
int ulibc_iscntrl(int c);
int ulibc_ispunct(int c);
int ulibc_isxdigit(int c);
int ulibc_isascii(int c);
int ulibc_isblank(int c);
int ulibc_toupper(int c);
int ulibc_tolower(int c);

/* unistd.h wrappers */
int64_t ulibc_read(int fd, void *buf, size_t count);
int64_t ulibc_write(int fd, const void *buf, size_t count);
int     ulibc_close(int fd);
int     ulibc_dup(int fd);
int     ulibc_dup2(int oldfd, int newfd);
int64_t ulibc_lseek(int fd, int64_t offset, int whence);
int     ulibc_pipe(int pipefd[2]);
int     ulibc_fork(void);
int     ulibc_execve(const char *path, char *const argv[], char *const envp[]);
unsigned int ulibc_sleep(unsigned int seconds);
int     ulibc_usleep(uint64_t usec);
int     ulibc_nanosleep_us(uint64_t us);
int     ulibc_chdir(const char *path);
char   *ulibc_getcwd(char *buf, size_t size);
int     ulibc_getpid(void);
int     ulibc_getppid(void);
int     ulibc_getuid(void);
int     ulibc_getgid(void);
int     ulibc_access(const char *path, int mode);
int     ulibc_unlink(const char *path);
int     ulibc_rmdir(const char *path);
int     ulibc_isatty(int fd);
int64_t ulibc_readlink(const char *path, char *buf, size_t bufsiz);
int     ulibc_symlink(const char *target, const char *linkpath);
int     ulibc_link(const char *oldpath, const char *newpath);
int     ulibc_fsync(int fd);

/* pthread (POSIX threads) */
typedef uint64_t ulibc_pthread_t;
typedef struct { int attr; } ulibc_pthread_attr_t;
typedef struct { volatile int lock; int owner; int count; int type; } ulibc_pthread_mutex_t;
typedef struct { int attr; } ulibc_pthread_mutexattr_t;
typedef struct { volatile int seq; volatile int waiters; } ulibc_pthread_cond_t;
typedef struct { int attr; } ulibc_pthread_condattr_t;
typedef struct { volatile int lock; int readers; int writer; int writer_waiters; int reader_waiters; } ulibc_pthread_rwlock_t;
typedef struct { int attr; } ulibc_pthread_rwlockattr_t;
typedef unsigned int ulibc_pthread_key_t;
typedef volatile int ulibc_pthread_once_t;

#define ULIBC_PTHREAD_MUTEX_INITIALIZER {0,0,0,0}
#define ULIBC_PTHREAD_COND_INITIALIZER  {0,0}
#define ULIBC_PTHREAD_RWLOCK_INITIALIZER {0,0,0,0,0}
#define ULIBC_PTHREAD_ONCE_INIT 0
#define ULIBC_PTHREAD_KEYS_MAX 128
#define ULIBC_PTHREAD_MUTEX_NORMAL    0
#define ULIBC_PTHREAD_MUTEX_RECURSIVE 1
#define ULIBC_PTHREAD_MUTEX_ERRORCHECK 2

int ulibc_pthread_create(ulibc_pthread_t *thread, const ulibc_pthread_attr_t *attr, void*(*start)(void*), void *arg);
int ulibc_pthread_join(ulibc_pthread_t thread, void **retval);
int ulibc_pthread_detach(ulibc_pthread_t thread);
ulibc_pthread_t ulibc_pthread_self(void);
int ulibc_pthread_equal(ulibc_pthread_t t1, ulibc_pthread_t t2);
void ulibc_pthread_exit(void *retval);
int ulibc_pthread_mutexattr_init(ulibc_pthread_mutexattr_t *attr);
int ulibc_pthread_mutexattr_destroy(ulibc_pthread_mutexattr_t *attr);
int ulibc_pthread_mutexattr_settype(ulibc_pthread_mutexattr_t *attr, int type);
int ulibc_pthread_mutexattr_gettype(const ulibc_pthread_mutexattr_t *attr, int *type);
int ulibc_pthread_mutex_init(ulibc_pthread_mutex_t *m, const ulibc_pthread_mutexattr_t *attr);
int ulibc_pthread_mutex_lock(ulibc_pthread_mutex_t *m);
int ulibc_pthread_mutex_trylock(ulibc_pthread_mutex_t *m);
int ulibc_pthread_mutex_unlock(ulibc_pthread_mutex_t *m);
int ulibc_pthread_mutex_destroy(ulibc_pthread_mutex_t *m);
int ulibc_pthread_condattr_init(ulibc_pthread_condattr_t *attr);
int ulibc_pthread_condattr_destroy(ulibc_pthread_condattr_t *attr);
int ulibc_pthread_condattr_setclock(ulibc_pthread_condattr_t *attr, int clock_id);
int ulibc_pthread_condattr_getclock(const ulibc_pthread_condattr_t *attr, int *clock_id);
int ulibc_pthread_cond_init(ulibc_pthread_cond_t *c, const ulibc_pthread_condattr_t *attr);
int ulibc_pthread_cond_wait(ulibc_pthread_cond_t *c, ulibc_pthread_mutex_t *m);
int ulibc_pthread_cond_timedwait(ulibc_pthread_cond_t *c, ulibc_pthread_mutex_t *m, const timespec_t *abstime);
int ulibc_pthread_cond_signal(ulibc_pthread_cond_t *c);
int ulibc_pthread_cond_broadcast(ulibc_pthread_cond_t *c);
int ulibc_pthread_cond_destroy(ulibc_pthread_cond_t *c);
int ulibc_pthread_rwlock_init(ulibc_pthread_rwlock_t *rw, const ulibc_pthread_rwlockattr_t *attr);
int ulibc_pthread_rwlock_rdlock(ulibc_pthread_rwlock_t *rw);
int ulibc_pthread_rwlock_wrlock(ulibc_pthread_rwlock_t *rw);
int ulibc_pthread_rwlock_unlock(ulibc_pthread_rwlock_t *rw);
int ulibc_pthread_rwlock_destroy(ulibc_pthread_rwlock_t *rw);
int ulibc_pthread_key_create(ulibc_pthread_key_t *key, void(*destructor)(void*));
int ulibc_pthread_key_delete(ulibc_pthread_key_t key);
void *ulibc_pthread_getspecific(ulibc_pthread_key_t key);
int ulibc_pthread_setspecific(ulibc_pthread_key_t key, const void *value);
int ulibc_pthread_once(ulibc_pthread_once_t *once, void(*init_routine)(void));
int ulibc_pthread_setname_np(ulibc_pthread_t thread, const char *name);
int ulibc_pthread_getname_np(ulibc_pthread_t thread, char *name, size_t len);
int ulibc_pthread_attr_init(ulibc_pthread_attr_t *attr);
int ulibc_pthread_attr_destroy(ulibc_pthread_attr_t *attr);
int ulibc_pthread_attr_setdetachstate(ulibc_pthread_attr_t *attr, int state);
int ulibc_pthread_attr_setstacksize(ulibc_pthread_attr_t *attr, size_t stacksize);

/* DRM / framebuffer ioctls */
#define DRM_IOCTL_VERSION          0xC0406400
#define DRM_IOCTL_GET_CAP          0xC010640C
#define DRM_IOCTL_MODE_GETRESOURCES 0xC04064A0
#define DRM_IOCTL_MODE_GETCRTC     0xC06864A1
#define DRM_IOCTL_MODE_GETCONNECTOR 0xC05064A7
#define DRM_IOCTL_MODE_GETENCODER  0xC01464A6
#define DRM_IOCTL_MODE_CREATE_DUMB 0xC02064B2
#define DRM_IOCTL_MODE_MAP_DUMB    0xC01064B3
#define DRM_IOCTL_MODE_DESTROY_DUMB 0xC00464B4
#define DRM_IOCTL_MODE_ADDFB       0xC04464AE
#define DRM_IOCTL_MODE_SETCRTC     0xC06864A2
#define DRM_IOCTL_SET_MASTER       0x0000641E
#define DRM_IOCTL_DROP_MASTER      0x0000641F
#define DRM_IOCTL_GEM_CLOSE        0x40086409
#define DRM_IOCTL_PRIME_HANDLE_TO_FD 0xC00C642D
#define DRM_IOCTL_PRIME_FD_TO_HANDLE 0xC00C642E

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOPUT_VSCREENINFO 0x4601
#define FBIOGET_FSCREENINFO 0x4602

typedef struct {
    uint32_t xres, yres;
    uint32_t xres_virtual, yres_virtual;
    uint32_t xoffset, yoffset;
    uint32_t bits_per_pixel;
    uint32_t red_offset, red_length;
    uint32_t green_offset, green_length;
    uint32_t blue_offset, blue_length;
    uint32_t transp_offset, transp_length;
} fb_var_screeninfo_t;

typedef struct {
    char id[16];
    uint64_t smem_start;
    uint32_t smem_len;
    uint32_t type;
    uint32_t visual;
    uint32_t line_length;
} fb_fix_screeninfo_t;

int drm_ioctl_handler(int fd, uint64_t request, void *arg);
int fb_ioctl_handler(int fd, uint64_t request, void *arg);

/* Dynamic linker stub */
typedef struct {
    const char *name;
    void       *addr;
} ulibc_sym_entry_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} ulibc_elf64_phdr_t;

typedef struct {
    uint64_t                  dlpi_addr;
    const char               *dlpi_name;
    const ulibc_elf64_phdr_t *dlpi_phdr;
    uint16_t                  dlpi_phnum;
} ulibc_dl_phdr_info_t;

typedef struct {
    uint64_t ti_module;
    uint64_t ti_offset;
} ulibc_tls_index_t;

void  *ulibc_dlopen(const char *filename, int flags);
void  *ulibc_dlsym(void *handle, const char *symbol);
int    ulibc_dlclose(void *handle);
char  *ulibc_dlerror(void);
int    ulibc_dl_iterate_phdr(int(*cb)(ulibc_dl_phdr_info_t*, size_t, void*), void *data);
void  *ulibc___tls_get_addr(ulibc_tls_index_t *ti);
void   compat4_tls_reset_task(int tidx);

/* Master init */
void compat4_init_all(void);
void compat4_register_shell_cmds(void);

#endif /* RIDUX_COMPAT4_H */
