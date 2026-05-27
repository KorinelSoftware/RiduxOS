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
void  *ulibc_aligned_alloc(size_t align, size_t size);

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

/* Native compositor GPU present path. The kernel shell composes into its
 * backbuffer, then this shim tries to scan it out through the real virtual GPU
 * backend (VMSVGA/SVGA3D or virtio-gpu) before falling back to CPU fb blits. */
bool ridux_gpu_present_backbuffer(const uint32_t *pixels, uint32_t width,
                                  uint32_t height, uint32_t stride_pixels);
bool ridux_gpu_real_present_available(void);
const char *ridux_gpu_present_backend(void);

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
#define DRM_IOCTL_GET_UNIQUE       0xC0106401
#define DRM_IOCTL_GET_MAGIC        0x80046402
#define DRM_IOCTL_AUTH_MAGIC       0x40046411
#define DRM_IOCTL_GET_CAP          0xC010640C
#define DRM_IOCTL_MODE_GETRESOURCES 0xC04064A0
#define DRM_IOCTL_MODE_GETCRTC     0xC06864A1
#define DRM_IOCTL_MODE_GETCONNECTOR 0xC05064A7
#define DRM_IOCTL_MODE_GETENCODER  0xC01464A6
#define DRM_IOCTL_MODE_CURSOR      0xC01C64A3
#define DRM_IOCTL_MODE_CREATE_DUMB 0xC02064B2
#define DRM_IOCTL_MODE_MAP_DUMB    0xC01064B3
#define DRM_IOCTL_MODE_DESTROY_DUMB 0xC00464B4
#define DRM_IOCTL_MODE_ADDFB       0xC01C64AE
#define DRM_IOCTL_MODE_SETCRTC     0xC06864A2
#define DRM_IOCTL_SET_CLIENT_CAP   0x4010640D
#define DRM_IOCTL_MODE_GETPROPERTY 0xC04064AA
#define DRM_IOCTL_MODE_SETPROPERTY 0xC01064AB
#define DRM_IOCTL_MODE_GETPROPBLOB 0xC01064AC
#define DRM_IOCTL_MODE_GETFB       0xC01C64AD
#define DRM_IOCTL_MODE_PAGE_FLIP   0xC01864B0
#define DRM_IOCTL_MODE_DIRTYFB     0xC01864B1
#define DRM_IOCTL_MODE_RMFB        0xC00464AF
#define DRM_IOCTL_MODE_GETPLANERESOURCES 0xC01064B5
#define DRM_IOCTL_MODE_GETPLANE    0xC02064B6
#define DRM_IOCTL_MODE_SETPLANE    0xC03464B7
#define DRM_IOCTL_MODE_ADDFB2      0xC06864B8
#define DRM_IOCTL_MODE_OBJ_GETPROPERTIES 0xC02064B9
#define DRM_IOCTL_MODE_OBJ_SETPROPERTY 0xC01864BA
#define DRM_IOCTL_MODE_CURSOR2     0xC02464BB
#define DRM_IOCTL_MODE_ATOMIC      0xC03864BC
#define DRM_IOCTL_MODE_CREATEPROPBLOB 0xC01064BD
#define DRM_IOCTL_MODE_DESTROYPROPBLOB 0xC00464BE
#define DRM_IOCTL_SYNCOBJ_CREATE   0xC00864BF
#define DRM_IOCTL_SYNCOBJ_DESTROY  0xC00864C0
#define DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD 0xC01064C1
#define DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE 0xC01064C2
#define DRM_IOCTL_SYNCOBJ_WAIT     0xC02864C3
#define DRM_IOCTL_SYNCOBJ_RESET    0xC01064C4
#define DRM_IOCTL_SYNCOBJ_SIGNAL   0xC01064C5
#define DRM_IOCTL_MODE_CREATE_LEASE 0xC01864C6
#define DRM_IOCTL_MODE_LIST_LESSEES 0xC01064C7
#define DRM_IOCTL_MODE_GET_LEASE    0xC01064C8
#define DRM_IOCTL_MODE_REVOKE_LEASE 0x400464C9
#define DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT 0xC03064CA
#define DRM_IOCTL_SYNCOBJ_QUERY    0xC01864CB
#define DRM_IOCTL_SYNCOBJ_TRANSFER 0xC02064CC
#define DRM_IOCTL_SYNCOBJ_TIMELINE_SIGNAL 0xC01864CD
#define DRM_IOCTL_MODE_GETFB2      0xC06864CE
#define DRM_IOCTL_SYNCOBJ_EVENTFD  0xC01864CF
#define DRM_IOCTL_SET_MASTER       0x0000641E
#define DRM_IOCTL_DROP_MASTER      0x0000641F
#define DRM_IOCTL_GEM_CLOSE        0x40086409
#define DRM_IOCTL_PRIME_HANDLE_TO_FD 0xC00C642D
#define DRM_IOCTL_PRIME_FD_TO_HANDLE 0xC00C642E
#define DRM_IOCTL_WAIT_VBLANK        0xC018643A
#define DRM_IOCTL_MODE_GETGAMMA      0xC02064A4
#define DRM_IOCTL_MODE_SETGAMMA      0xC02064A5

/* DMA-buf ioctls (linux/dma-buf.h). Numbers are stable Linux UAPI. */
#define DMA_BUF_IOCTL_SYNC                  0x40086200u
#define DMA_BUF_IOCTL_SET_NAME              0x40086201u
#define DMA_BUF_IOCTL_SET_NAME_A            0x40086201u
#define DMA_BUF_IOCTL_SET_NAME_B            0x40046201u
#define DMA_BUF_IOCTL_EXPORT_SYNC_FILE      0xC0086202u
#define DMA_BUF_IOCTL_IMPORT_SYNC_FILE      0x40086203u

#define DMA_BUF_SYNC_READ        (1u<<0)
#define DMA_BUF_SYNC_WRITE       (2u<<0)
#define DMA_BUF_SYNC_RW          (DMA_BUF_SYNC_READ|DMA_BUF_SYNC_WRITE)
#define DMA_BUF_SYNC_START       (0u<<2)
#define DMA_BUF_SYNC_END         (1u<<2)
#define DMA_BUF_SYNC_VALID_FLAGS (DMA_BUF_SYNC_RW|DMA_BUF_SYNC_END)

/* sync_file IOCTLs (linux/sync_file.h) */
#define SYNC_IOC_MERGE       0xC0303E03u
#define SYNC_IOC_FILE_INFO   0xC0383E04u
#define SYNC_IOC_SET_DEADLINE 0x40103E05u

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
int drm_resolve_mmap_offset(int fd, uint64_t offset, uint64_t len,
                            uint64_t *kernel_ptr, uint64_t *avail,
                            uint32_t *handle_out);
void drm_mmap_release_handle(uint32_t handle);
void drm_file_close(int fd);
int fb_ioctl_handler(int fd, uint64_t request, void *arg);

/* ----------------------------------------------------------------------
 * Public ridux_* DRM/KMS/GBM/Vulkan WSI helper API. The in-tree compositor
 * and any Ridux-native tool can use these to drive the framebuffer without
 * issuing raw ioctls. Implementations live in src/compat/user_libc.c and
 * forward to the core DRM/KMS subsystem. */

uint32_t ridux_drm_format_bpp(uint32_t fourcc);
bool     ridux_drm_format_is_yuv(uint32_t fourcc);

uint32_t ridux_drm_vblank_sequence(void);
uint64_t ridux_drm_now_ns(void);

uint32_t ridux_drm_connector_count(void);
typedef struct ridux_drm_connector_summary ridux_drm_connector_summary_t;
typedef struct ridux_drm_mode_summary      ridux_drm_mode_summary_t;
bool     ridux_drm_connector_summary(uint32_t index, ridux_drm_connector_summary_t *out);
uint32_t ridux_drm_connector_modes  (uint32_t index, ridux_drm_mode_summary_t *out, uint32_t cap);
bool     ridux_drm_connector_edid   (uint8_t *out, uint32_t cap, uint32_t *length);
bool     ridux_drm_set_dpms (uint32_t state);
uint32_t ridux_drm_get_dpms (void);
bool     ridux_drm_set_gamma(const uint16_t *r, const uint16_t *g, const uint16_t *b, uint32_t count);
uint32_t ridux_drm_get_gamma_size(void);
bool     ridux_drm_page_flip_pixels(const uint32_t *pixels, uint32_t width,
                                    uint32_t height, uint32_t stride_pixels);
uint32_t ridux_drm_primary_plane_id(void);
uint32_t ridux_drm_cursor_plane_id (void);
uint32_t ridux_drm_overlay_plane_id(void);
uint32_t ridux_drm_crtc_id        (void);
uint32_t ridux_drm_connector_id   (void);
uint32_t ridux_drm_encoder_id     (void);
uint32_t ridux_drm_lease_create   (const uint32_t *objects, uint32_t count, int32_t *out_fd);
bool     ridux_drm_lease_revoke   (uint32_t lessee_id);
uint32_t ridux_drm_lease_count    (void);

/* HDR / color management. */
bool     ridux_drm_set_hdr_metadata (const void *blob, uint32_t length);
uint32_t ridux_drm_get_hdr_metadata (void *out, uint32_t cap);
bool     ridux_drm_set_icc_profile  (const void *blob, uint32_t length);
uint32_t ridux_drm_get_icc_profile  (void *out, uint32_t cap);
void     ridux_drm_set_colorspace        (uint32_t v);
uint32_t ridux_drm_get_colorspace        (void);
void     ridux_drm_set_max_bpc           (uint32_t v);
uint32_t ridux_drm_get_max_bpc           (void);
void     ridux_drm_set_broadcast_rgb     (uint32_t v);
uint32_t ridux_drm_get_broadcast_rgb     (void);
void     ridux_drm_set_panel_orientation (uint32_t v);
uint32_t ridux_drm_get_panel_orientation (void);
void     ridux_drm_set_content_type      (uint32_t v);
uint32_t ridux_drm_get_content_type      (void);
void     ridux_drm_set_scaling_mode      (uint32_t v);
uint32_t ridux_drm_get_scaling_mode      (void);
void     ridux_drm_set_hdcp              (uint32_t cp, uint32_t cp_type);
void     ridux_drm_get_hdcp              (uint32_t *cp, uint32_t *cp_type);
void     ridux_drm_set_background_color  (uint64_t argb);
uint64_t ridux_drm_get_background_color  (void);
void     ridux_drm_set_privacy_screen    (uint32_t sw_state);
uint32_t ridux_drm_get_privacy_screen    (uint32_t *hw_state);
void     ridux_drm_set_dithering_mode    (uint32_t v);
uint32_t ridux_drm_get_dithering_mode    (void);
void     ridux_drm_set_output_bpp        (uint32_t v);
uint32_t ridux_drm_get_output_bpp        (void);

/* GBM device shim (see top of section for the full layout). */
typedef struct ridux_gbm_device  ridux_gbm_device_t;
typedef struct ridux_gbm_bo      ridux_gbm_bo_t;
typedef struct ridux_gbm_surface ridux_gbm_surface_t;
ridux_gbm_device_t *ridux_gbm_create_device(int32_t drm_fd);
void                ridux_gbm_device_destroy(ridux_gbm_device_t *dev);
const char         *ridux_gbm_device_get_backend_name(ridux_gbm_device_t *dev);
int32_t             ridux_gbm_device_get_fd(ridux_gbm_device_t *dev);
bool                ridux_gbm_device_is_format_supported(ridux_gbm_device_t *dev,
                                                         uint32_t format, uint32_t use);
ridux_gbm_bo_t     *ridux_gbm_bo_create(ridux_gbm_device_t *dev,
                                        uint32_t width, uint32_t height,
                                        uint32_t format, uint32_t flags);
ridux_gbm_bo_t     *ridux_gbm_bo_create_with_modifiers(ridux_gbm_device_t *dev,
                                                       uint32_t width, uint32_t height,
                                                       uint32_t format,
                                                       const uint64_t *modifiers,
                                                       uint32_t count);
ridux_gbm_bo_t     *ridux_gbm_bo_import_dmabuf(ridux_gbm_device_t *dev,
                                               int32_t prime_fd, uint32_t width,
                                               uint32_t height, uint32_t format,
                                               uint32_t stride);
void     ridux_gbm_bo_destroy(ridux_gbm_bo_t *gbo);
uint32_t ridux_gbm_bo_get_width   (const ridux_gbm_bo_t *gbo);
uint32_t ridux_gbm_bo_get_height  (const ridux_gbm_bo_t *gbo);
uint32_t ridux_gbm_bo_get_stride  (const ridux_gbm_bo_t *gbo);
uint32_t ridux_gbm_bo_get_format  (const ridux_gbm_bo_t *gbo);
uint32_t ridux_gbm_bo_get_handle  (const ridux_gbm_bo_t *gbo);
uint64_t ridux_gbm_bo_get_modifier(const ridux_gbm_bo_t *gbo);
uint32_t ridux_gbm_bo_get_offset      (const ridux_gbm_bo_t *gbo, uint32_t plane);
uint32_t ridux_gbm_bo_get_plane_count (const ridux_gbm_bo_t *gbo);
uint32_t ridux_gbm_bo_get_bpp         (const ridux_gbm_bo_t *gbo);
int32_t  ridux_gbm_bo_get_fd          (ridux_gbm_bo_t *gbo);
void    *ridux_gbm_bo_map(ridux_gbm_bo_t *gbo, uint32_t x, uint32_t y,
                          uint32_t width, uint32_t height,
                          uint32_t flags, uint32_t *stride_out, void **map_data);
void     ridux_gbm_bo_unmap(ridux_gbm_bo_t *gbo, void *map_data);
void     ridux_gbm_bo_set_user_data(ridux_gbm_bo_t *gbo, void *data,
                                    void (*destroy)(ridux_gbm_bo_t*, void*));
void    *ridux_gbm_bo_get_user_data(ridux_gbm_bo_t *gbo);
ridux_gbm_surface_t *ridux_gbm_surface_create(ridux_gbm_device_t *dev,
                                              uint32_t width, uint32_t height,
                                              uint32_t format, uint32_t flags);
void                 ridux_gbm_surface_destroy(ridux_gbm_surface_t *gs);
ridux_gbm_bo_t      *ridux_gbm_surface_lock_front_buffer(ridux_gbm_surface_t *gs);
void                 ridux_gbm_surface_release_buffer(ridux_gbm_surface_t *gs,
                                                      ridux_gbm_bo_t *gbo);
bool                 ridux_gbm_surface_has_free_buffers(ridux_gbm_surface_t *gs);

/* Vulkan WSI direct-display extensions. */
typedef struct ridux_vk_display_info ridux_vk_display_info_t;
typedef struct ridux_vk_display_mode ridux_vk_display_mode_t;
uint32_t   ridux_vk_display_count(void);
bool       ridux_vk_display_get_info(uint32_t index, ridux_vk_display_info_t *out);
uint32_t   ridux_vk_display_modes(uint32_t display_index,
                                  ridux_vk_display_mode_t *out, uint32_t cap);
bool       ridux_vk_display_present(uint32_t display_index,
                                    const uint32_t *pixels, uint32_t width,
                                    uint32_t height, uint32_t stride_pixels);
const char *ridux_vk_present_backend(void);

/* Vulkan WSI swapchain (lightweight, single output). */
typedef enum {
    RIDUX_VK_PRESENT_MODE_IMMEDIATE = 0,
    RIDUX_VK_PRESENT_MODE_MAILBOX   = 1,
    RIDUX_VK_PRESENT_MODE_FIFO      = 2,
    RIDUX_VK_PRESENT_MODE_FIFO_RELAXED = 3,
} ridux_vk_present_mode_t;
typedef struct ridux_vk_swapchain ridux_vk_swapchain_t;
ridux_vk_swapchain_t *ridux_vk_swapchain_create(ridux_gbm_device_t *dev,
                                                uint32_t width, uint32_t height,
                                                uint32_t format, uint32_t image_count,
                                                ridux_vk_present_mode_t mode);
void                  ridux_vk_swapchain_destroy(ridux_vk_swapchain_t *sc);
bool                  ridux_vk_swapchain_acquire_next(ridux_vk_swapchain_t *sc,
                                                      uint32_t *out_index);
ridux_gbm_bo_t       *ridux_vk_swapchain_image(ridux_vk_swapchain_t *sc, uint32_t index);
bool                  ridux_vk_swapchain_queue_present(ridux_vk_swapchain_t *sc,
                                                       uint32_t index);
uint64_t              ridux_vk_swapchain_frame_count(const ridux_vk_swapchain_t *sc);
uint32_t              ridux_vk_swapchain_image_count(const ridux_vk_swapchain_t *sc);

/* Native present hook used by EGL/GBM/Vulkan and the in-tree compositor. */
bool        ridux_gpu_present_backbuffer(const uint32_t *pixels, uint32_t width,
                                         uint32_t height, uint32_t stride_pixels);
bool        ridux_gpu_real_present_available(void);
const char *ridux_gpu_present_backend(void);

/* Render fence / sync_file helpers (signal/wait/transfer). */
uint32_t ridux_drm_fence_create     (const char *name, bool timeline);
bool     ridux_drm_fence_signal     (uint32_t fence_id, uint64_t timeline_value);
bool     ridux_drm_fence_is_signaled(uint32_t fence_id);
bool     ridux_drm_fence_wait       (uint32_t fence_id, uint64_t timeout_ns);
int32_t  ridux_drm_fence_export_fd  (uint32_t fence_id);
bool     ridux_drm_fence_destroy    (uint32_t fence_id);
uint32_t ridux_drm_fence_count      (void);

/* EGL_EXT_image_dma_buf_import_modifiers helpers (matrix probe). */
uint32_t ridux_egl_dmabuf_format_count(void);
uint32_t ridux_egl_dmabuf_formats     (uint32_t *out, uint32_t cap);
uint32_t ridux_egl_dmabuf_modifiers   (uint32_t format, uint64_t *out,
                                       uint8_t *external, uint32_t cap);

/* GPU stats counters (lightweight, in-process). */
typedef struct {
    uint64_t frames_presented;
    uint64_t frames_dropped;
    uint64_t fences_signaled;
    uint64_t bytes_uploaded;
    uint64_t bytes_downloaded;
    uint64_t last_present_ns;
    uint64_t last_present_duration_ns;
    uint64_t total_render_ns;
} ridux_gpu_stats_t;
void ridux_gpu_stats_record_present       (uint64_t duration_ns);
void ridux_gpu_stats_record_drop          (void);
void ridux_gpu_stats_record_fence_signaled(void);
void ridux_gpu_stats_record_upload        (uint64_t bytes);
void ridux_gpu_stats_record_download      (uint64_t bytes);
void ridux_gpu_stats_snapshot             (ridux_gpu_stats_t *out);
void ridux_gpu_stats_reset                (void);

/* Per-client GEM accounting (mirrors drm-fdinfo). */
typedef struct ridux_gem_client_struct {
    bool used;
    uint32_t client_pid;
    uint64_t bo_count;
    uint64_t bo_total_bytes;
    uint64_t bo_resident_bytes;
    uint64_t bo_purgeable_bytes;
    uint64_t bo_active_bytes;
    uint64_t fences_open;
    uint64_t last_activity_ns;
} ridux_gem_client_t;
void     ridux_gem_account_alloc   (uint32_t pid, uint64_t bytes);
void     ridux_gem_account_free    (uint32_t pid, uint64_t bytes);
void     ridux_gem_account_purge   (uint32_t pid, uint64_t bytes);
bool     ridux_gem_account_snapshot(uint32_t pid, ridux_gem_client_t *out);
uint32_t ridux_gem_account_iterate (ridux_gem_client_t *out, uint32_t cap);
void     ridux_gem_account_drop    (uint32_t pid);

/* Wayland wp_presentation_feedback timing helpers. */
typedef struct {
    uint64_t commit_ns;
    uint64_t present_ns;
    uint64_t refresh_ns;
    uint64_t seq;
    uint32_t flags;
} ridux_present_feedback_t;
#define RIDUX_PRESENT_FLAG_VSYNC         (1u<<0)
#define RIDUX_PRESENT_FLAG_HW_CLOCK      (1u<<1)
#define RIDUX_PRESENT_FLAG_HW_COMPLETION (1u<<2)
#define RIDUX_PRESENT_FLAG_ZERO_COPY     (1u<<3)
void     ridux_present_feedback_record(uint64_t commit_ns, uint32_t flags);
void     ridux_present_feedback_get   (ridux_present_feedback_t *out);
uint64_t ridux_present_refresh_ns     (void);

/* VRR / adaptive sync. */
bool     ridux_drm_vrr_supported  (void);
void     ridux_drm_vrr_set_range  (uint32_t min_hz, uint32_t max_hz);
void     ridux_drm_vrr_get_range  (uint32_t *min_hz, uint32_t *max_hz);
void     ridux_drm_vrr_set_enabled(bool en);
bool     ridux_drm_vrr_get_enabled(void);

/* linux-explicit-sync-v1. */
bool     ridux_explicit_sync_attach        (uint32_t surface_id,
                                            uint32_t acquire_fence_id,
                                            uint32_t release_fence_id);
bool     ridux_explicit_sync_signal_release(uint32_t surface_id);
bool     ridux_explicit_sync_wait_acquire  (uint32_t surface_id, uint64_t timeout_ns);
void     ridux_explicit_sync_drop          (uint32_t surface_id);

/* zwp_linux_dmabuf_v1 / linux-dmabuf-feedback. */
typedef struct {
    uint32_t format;
    uint64_t modifier;
    uint16_t tranche_index;
    uint16_t flags;
    uint32_t target_device;
} ridux_dmabuf_format_pair_t;
#define RIDUX_DMABUF_FLAG_SCANOUT (1u<<0)
uint32_t ridux_dmabuf_feedback_pair_count(void);
uint32_t ridux_dmabuf_feedback_fill      (ridux_dmabuf_format_pair_t *out, uint32_t cap);
uint64_t ridux_dmabuf_main_device        (void);
uint64_t ridux_dmabuf_target_device      (uint32_t connector_id);

/* wp_color_management_v1 helpers. */
typedef enum {
    RIDUX_CM_PRIMARIES_SRGB        = 0,
    RIDUX_CM_PRIMARIES_BT601_525   = 1,
    RIDUX_CM_PRIMARIES_BT601_625   = 2,
    RIDUX_CM_PRIMARIES_BT709       = 3,
    RIDUX_CM_PRIMARIES_BT2020      = 4,
    RIDUX_CM_PRIMARIES_DCI_P3      = 5,
    RIDUX_CM_PRIMARIES_DISPLAY_P3  = 6,
    RIDUX_CM_PRIMARIES_ADOBE_RGB   = 7,
    RIDUX_CM_PRIMARIES_GENERIC_FILM= 8,
    RIDUX_CM_PRIMARIES_CIE_XYZ     = 9,
} ridux_cm_primaries_t;
typedef enum {
    RIDUX_CM_TRANSFER_SRGB     = 0,
    RIDUX_CM_TRANSFER_LINEAR   = 1,
    RIDUX_CM_TRANSFER_BT709    = 2,
    RIDUX_CM_TRANSFER_BT470M   = 3,
    RIDUX_CM_TRANSFER_BT470BG  = 4,
    RIDUX_CM_TRANSFER_GAMMA22  = 5,
    RIDUX_CM_TRANSFER_GAMMA28  = 6,
    RIDUX_CM_TRANSFER_PQ       = 7,
    RIDUX_CM_TRANSFER_HLG      = 8,
    RIDUX_CM_TRANSFER_LOG_100  = 9,
    RIDUX_CM_TRANSFER_LOG_316  =10,
    RIDUX_CM_TRANSFER_ST428    =11,
} ridux_cm_transfer_t;
typedef enum {
    RIDUX_CM_RENDER_INTENT_PERCEPTUAL  = 0,
    RIDUX_CM_RENDER_INTENT_RELATIVE    = 1,
    RIDUX_CM_RENDER_INTENT_SATURATION  = 2,
    RIDUX_CM_RENDER_INTENT_ABSOLUTE    = 3,
    RIDUX_CM_RENDER_INTENT_RELATIVE_BPC= 4,
} ridux_cm_render_intent_t;
typedef struct {
    uint32_t magic;
    uint32_t image_description_id;
    ridux_cm_primaries_t primaries;
    ridux_cm_transfer_t  transfer;
    uint32_t max_cll;
    uint32_t max_fall;
    uint32_t white_luminance;
    uint32_t black_luminance;
} ridux_cm_image_description_t;
uint32_t ridux_cm_supported_primaries (uint32_t *out, uint32_t cap);
uint32_t ridux_cm_supported_transfers (uint32_t *out, uint32_t cap);
uint32_t ridux_cm_supported_intents   (uint32_t *out, uint32_t cap);
uint32_t ridux_cm_image_description_create(ridux_cm_primaries_t prim,
                                           ridux_cm_transfer_t  tf,
                                           uint32_t max_cll, uint32_t max_fall,
                                           uint32_t white_luminance,
                                           uint32_t black_luminance);
bool     ridux_cm_image_description_get   (uint32_t image_description_id,
                                           ridux_cm_image_description_t *out);
bool     ridux_cm_image_description_destroy(uint32_t image_description_id);

/* Damage tracking. */
typedef struct {
    int32_t x, y;
    int32_t w, h;
} ridux_damage_rect_t;
void     ridux_damage_reset(void);
bool     ridux_damage_add  (int32_t x, int32_t y, int32_t w, int32_t h);
uint32_t ridux_damage_count(void);
bool     ridux_damage_get  (uint32_t index, ridux_damage_rect_t *out);
void     ridux_damage_full (int32_t w, int32_t h);

/* Hardware video decode/encode session API (VAAPI/VDPAU-style). */
typedef enum {
    RIDUX_VIDEO_CODEC_NONE = 0,
    RIDUX_VIDEO_CODEC_H264,
    RIDUX_VIDEO_CODEC_H265,
    RIDUX_VIDEO_CODEC_VP8,
    RIDUX_VIDEO_CODEC_VP9,
    RIDUX_VIDEO_CODEC_AV1,
    RIDUX_VIDEO_CODEC_MPEG2,
    RIDUX_VIDEO_CODEC_VC1,
    RIDUX_VIDEO_CODEC_JPEG,
} ridux_video_codec_t;
typedef enum {
    RIDUX_VIDEO_PROFILE_DECODE = (1u<<0),
    RIDUX_VIDEO_PROFILE_ENCODE = (1u<<1),
    RIDUX_VIDEO_PROFILE_8BIT   = (1u<<2),
    RIDUX_VIDEO_PROFILE_10BIT  = (1u<<3),
    RIDUX_VIDEO_PROFILE_12BIT  = (1u<<4),
    RIDUX_VIDEO_PROFILE_HDR    = (1u<<5),
} ridux_video_profile_flags_t;
typedef struct {
    ridux_video_codec_t codec;
    uint32_t flags;
    uint32_t max_width;
    uint32_t max_height;
    uint32_t max_macroblocks;
} ridux_video_capability_t;
uint32_t ridux_video_capabilities  (ridux_video_capability_t *out, uint32_t cap);
uint32_t ridux_video_session_create(ridux_video_codec_t codec, uint32_t flags,
                                    uint32_t width, uint32_t height, uint32_t format);
bool     ridux_video_session_submit (uint32_t session_id, uint64_t input_bytes);
bool     ridux_video_session_destroy(uint32_t session_id);
uint32_t ridux_video_session_count  (void);

/* zwlr_output_management_v1 helpers. */
typedef struct {
    uint32_t output_id;
    uint32_t connector_id;
    uint32_t crtc_id;
    int32_t  position_x;
    int32_t  position_y;
    uint32_t width_mm;
    uint32_t height_mm;
    uint32_t scale_milli;
    uint32_t transform;
    uint32_t enabled;
    uint32_t refresh_milli_hz;
    uint32_t mode_width;
    uint32_t mode_height;
    uint32_t adaptive_sync;
    uint32_t image_description_id;
    char     name[32];
    char     description[128];
    char     make[32];
    char     model[32];
} ridux_output_state_t;
uint32_t ridux_output_count       (void);
bool     ridux_output_get_state   (uint32_t output_id, ridux_output_state_t *out);
bool     ridux_output_apply_state (uint32_t output_id, const ridux_output_state_t *in);
bool     ridux_output_test_state  (const ridux_output_state_t *in);

/* Idle inhibit + keyboard shortcuts inhibit. */
uint32_t ridux_idle_inhibit_create        (uint32_t surface_id, uint32_t client_pid);
uint32_t ridux_kbd_shortcuts_inhibit_create(uint32_t surface_id, uint32_t client_pid);
bool     ridux_inhibit_destroy            (uint32_t inhibitor_id);
bool     ridux_idle_is_inhibited          (void);
bool     ridux_kbd_shortcuts_is_inhibited (uint32_t surface_id);
uint32_t ridux_inhibit_count_for          (uint32_t scope);

/* Pointer constraints + relative pointer. */
typedef enum {
    RIDUX_POINTER_LIFETIME_ONESHOT    = 0,
    RIDUX_POINTER_LIFETIME_PERSISTENT = 1,
} ridux_pointer_lifetime_t;
uint32_t ridux_pointer_lock_create        (uint32_t surface_id, uint32_t pointer_id,
                                           ridux_pointer_lifetime_t lifetime);
uint32_t ridux_pointer_confine_create     (uint32_t surface_id, uint32_t pointer_id,
                                           int32_t x, int32_t y, int32_t w, int32_t h,
                                           ridux_pointer_lifetime_t lifetime);
bool     ridux_pointer_constraint_set_cursor_hint(uint32_t constraint_id,
                                                  int32_t hx, int32_t hy);
bool     ridux_pointer_constraint_activate(uint32_t constraint_id, bool active);
bool     ridux_pointer_constraint_destroy (uint32_t constraint_id);
uint32_t ridux_pointer_active_constraint  (uint32_t surface_id);
void     ridux_pointer_relative_accumulate(int32_t dx, int32_t dy);
void     ridux_pointer_relative_drain     (int32_t *dx, int32_t *dy);

/* Tablet manager v2. */
typedef enum {
    RIDUX_TABLET_TOOL_PEN     = 0x140u,
    RIDUX_TABLET_TOOL_ERASER  = 0x141u,
    RIDUX_TABLET_TOOL_BRUSH   = 0x142u,
    RIDUX_TABLET_TOOL_PENCIL  = 0x143u,
    RIDUX_TABLET_TOOL_AIRBRUSH= 0x144u,
    RIDUX_TABLET_TOOL_FINGER  = 0x145u,
    RIDUX_TABLET_TOOL_MOUSE   = 0x146u,
    RIDUX_TABLET_TOOL_LENS    = 0x147u,
} ridux_tablet_tool_type_t;
uint32_t ridux_tablet_register     (uint32_t vid, uint32_t pid,
                                    const char *name, const char *path);
uint32_t ridux_tablet_tool_register(uint32_t tablet_id, ridux_tablet_tool_type_t type,
                                    uint64_t serial, uint64_t hw_id);
bool     ridux_tablet_tool_motion  (uint32_t tool_id, int32_t x_milli, int32_t y_milli,
                                    uint32_t pressure, int32_t tilt_x, int32_t tilt_y);
bool     ridux_tablet_tool_proximity(uint32_t tool_id, bool in_prox);
uint32_t ridux_tablet_count        (void);
uint32_t ridux_tablet_tool_count   (void);

/* Data device / clipboard / DnD. */
bool     ridux_clipboard_set        (const char *const *mimes, uint32_t mime_count,
                                     const void *data, uint32_t data_len, uint32_t source_pid);
bool     ridux_primary_selection_set(const char *const *mimes, uint32_t mime_count,
                                     const void *data, uint32_t data_len, uint32_t source_pid);
bool     ridux_dnd_set_source       (const char *const *mimes, uint32_t mime_count,
                                     const void *data, uint32_t data_len, uint32_t source_pid);
uint32_t ridux_clipboard_mime_count (void);
const char *ridux_clipboard_mime_at (uint32_t index);
uint32_t ridux_clipboard_read       (void *out, uint32_t cap);
uint32_t ridux_primary_selection_read(void *out, uint32_t cap);
uint32_t ridux_dnd_mime_count       (void);
const char *ridux_dnd_mime_at       (uint32_t index);
uint32_t ridux_dnd_read             (void *out, uint32_t cap);
void     ridux_dnd_finish           (void);

/* Input method v2 + virtual keyboard. */
bool        ridux_im_activate          (uint32_t surface_id);
bool        ridux_im_deactivate        (void);
bool        ridux_im_set_preedit       (const char *text, int32_t cursor_begin,
                                        int32_t cursor_end);
bool        ridux_im_commit_text       (const char *text);
const char *ridux_im_get_preedit       (void);
const char *ridux_im_get_commit        (void);
uint32_t    ridux_im_serial            (void);
uint32_t    ridux_im_focused_surface   (void);
bool        ridux_im_is_active         (void);
void        ridux_virtual_keyboard_press_key      (uint32_t keycode);
void        ridux_virtual_keyboard_release_key    (uint32_t keycode);
void        ridux_virtual_keyboard_press_modifier (uint32_t keycode);
void        ridux_virtual_keyboard_release_modifier(uint32_t keycode);

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
