/*
 * Mini libc usada por el runtime propio.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "base.h"
#include "memory_tasks.h"
#include "linux_syscalls.h"
#include "user_libc.h"

/* errno */
static int g_errno_val = 0;
int *__errno_location(void) { return &g_errno_val; }

/* string.h  (musl/FreeBSD style) */
void *ulibc_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d=(uint8_t*)dst; const uint8_t *s=(const uint8_t*)src;
    /* Word-at-a-time for large copies */
    if(n>=8&&!((uintptr_t)d&7)&&!((uintptr_t)s&7)){
        size_t wn=n/8;uint64_t *wd=(uint64_t*)d;const uint64_t *ws=(const uint64_t*)s;
        size_t i;for(i=0;i<wn;++i)wd[i]=ws[i];
        d+=wn*8;s+=wn*8;n-=wn*8;
    }
    while(n--)(*d++=*s++);
    return dst;
}
void *ulibc_memmove(void *dst, const void *src, size_t n) {
    uint8_t *d=(uint8_t*)dst; const uint8_t *s=(const uint8_t*)src;
    if(d==s||!n)return dst;
    if(d<s){size_t i;for(i=0;i<n;++i)d[i]=s[i];}
    else{size_t i=n;while(i--)d[i]=s[i];}
    return dst;
}
void *ulibc_memset(void *s, int c, size_t n) {
    uint8_t *p=(uint8_t*)s;uint8_t v=(uint8_t)c;
    if(n>=8){
        uint64_t w=v;w|=w<<8;w|=w<<16;w|=w<<32;
        while((uintptr_t)p&7&&n){*p++=v;--n;}
        uint64_t *wp=(uint64_t*)p;size_t wn=n/8;size_t i;
        for(i=0;i<wn;++i)wp[i]=w;
        p+=wn*8;n-=wn*8;
    }
    while(n--)*p++=v;
    return s;
}
int ulibc_memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *a=(const uint8_t*)s1,*b=(const uint8_t*)s2;
    size_t i;for(i=0;i<n;++i)if(a[i]!=b[i])return(int)a[i]-(int)b[i];
    return 0;
}
void *ulibc_memchr(const void *s, int c, size_t n) {
    const uint8_t *p=(const uint8_t*)s;uint8_t v=(uint8_t)c;
    size_t i;for(i=0;i<n;++i)if(p[i]==v)return(void*)(p+i);
    return 0;
}
size_t ulibc_strlen(const char *s) { size_t n=0; while(s[n])++n; return n; }
size_t ulibc_strnlen(const char *s, size_t max) { size_t n=0; while(n<max&&s[n])++n; return n; }

char *ulibc_strcpy(char *dst, const char *src) {
    char *d=dst; while((*d++=*src++)); return dst;
}
char *ulibc_strncpy(char *dst, const char *src, size_t n) {
    size_t i;for(i=0;i<n&&src[i];++i)dst[i]=src[i];
    for(;i<n;++i)dst[i]=0;
    return dst;
}
int ulibc_strcmp(const char *a, const char *b) {
    while(*a&&*b&&*a==*b){++a;++b;}
    return(int)((unsigned char)*a-(unsigned char)*b);
}
int ulibc_strncmp(const char *a, const char *b, size_t n) {
    size_t i;for(i=0;i<n&&a[i]&&b[i];++i)if(a[i]!=b[i])return(int)((unsigned char)a[i]-(unsigned char)b[i]);
    return(i<n)?(int)((unsigned char)a[i]-(unsigned char)b[i]):0;
}
char *ulibc_strcat(char *dst, const char *src) {
    char *d=dst+ulibc_strlen(dst);while((*d++=*src++));return dst;
}
char *ulibc_strncat(char *dst, const char *src, size_t n) {
    char *d=dst+ulibc_strlen(dst);size_t i;
    for(i=0;i<n&&src[i];++i)d[i]=src[i];d[i]=0;return dst;
}
char *ulibc_strchr(const char *s, int c) {
    for(;*s;++s)if(*s==(char)c)return(char*)s;
    return c?0:(char*)s;
}
char *ulibc_strrchr(const char *s, int c) {
    const char *last=0;
    for(;*s;++s)if(*s==(char)c)last=s;
    if(c==0)return(char*)s;
    return(char*)last;
}
char *ulibc_strstr(const char *h, const char *n) {
    size_t nlen=ulibc_strlen(n);
    if(!nlen)return(char*)h;
    for(;*h;++h)if(!ulibc_strncmp(h,n,nlen))return(char*)h;
    return 0;
}
char *ulibc_strdup(const char *s) {
    size_t len=ulibc_strlen(s)+1;
    char *d=(char*)ulibc_malloc(len);
    if(d)ulibc_memcpy(d,s,len);
    return d;
}
char *ulibc_strndup(const char *s, size_t n) {
    size_t len=ulibc_strnlen(s,n);
    char *d=(char*)ulibc_malloc(len+1);
    if(d){ulibc_memcpy(d,s,len);d[len]=0;}
    return d;
}
static char *g_strtok_saveptr=0;
char *ulibc_strtok(char *str, const char *delim) { return ulibc_strtok_r(str,delim,&g_strtok_saveptr); }
char *ulibc_strtok_r(char *str, const char *delim, char **saveptr) {
    char *s=str?str:*saveptr;
    if(!s)return 0;
    s+=ulibc_strspn(s,delim);
    if(!*s){*saveptr=0;return 0;}
    char *tok=s;
    s+=ulibc_strcspn(s,delim);
    if(*s)*s++=0;
    *saveptr=s;
    return tok;
}
size_t ulibc_strspn(const char *s, const char *accept) {
    size_t n=0;for(;s[n];++n){const char *a=accept;bool found=false;while(*a)if(*a++==s[n]){found=true;break;}if(!found)break;}return n;
}
size_t ulibc_strcspn(const char *s, const char *reject) {
    size_t n=0;for(;s[n];++n){const char *r=reject;while(*r)if(*r++==s[n])return n;}return n;
}
char *ulibc_strpbrk(const char *s, const char *accept) {
    for(;*s;++s){const char *a=accept;while(*a)if(*a++==*s)return(char*)s;}return 0;
}
static const char *g_errno_strings[] = {
    "Success","EPERM","ENOENT","ESRCH","EINTR","EIO","ENXIO","E2BIG","ENOEXEC","EBADF",
    "ECHILD","EAGAIN","ENOMEM","EACCES","EFAULT","ENOTBLK","EBUSY","EEXIST","EXDEV","ENODEV",
    "ENOTDIR","EISDIR","EINVAL","ENFILE","EMFILE","ENOTTY","ETXTBSY","EFBIG","ENOSPC","ESPIPE",
    "EROFS","EMLINK","EPIPE","EDOM","ERANGE"
};
char *ulibc_strerror(int errnum) {
    if(errnum<0)errnum=-errnum;
    if(errnum<(int)(sizeof(g_errno_strings)/sizeof(g_errno_strings[0])))return(char*)g_errno_strings[errnum];
    return(char*)"Unknown error";
}
int ulibc_strcasecmp(const char *a, const char *b) {
    while(*a&&*b){int ca=ulibc_tolower((unsigned char)*a),cb=ulibc_tolower((unsigned char)*b);
        if(ca!=cb)return ca-cb;++a;++b;}
    return(int)((unsigned char)*a-(unsigned char)*b);
}
int ulibc_strncasecmp(const char *a, const char *b, size_t n) {
    size_t i;for(i=0;i<n&&a[i]&&b[i];++i){int ca=ulibc_tolower((unsigned char)a[i]),cb=ulibc_tolower((unsigned char)b[i]);
        if(ca!=cb)return ca-cb;}
    return(i<n)?(int)((unsigned char)a[i]-(unsigned char)b[i]):0;
}

/* ctype.h */
int ulibc_isalpha(int c){return(c>='A'&&c<='Z')||(c>='a'&&c<='z');}
int ulibc_isdigit(int c){return c>='0'&&c<='9';}
int ulibc_isalnum(int c){return ulibc_isalpha(c)||ulibc_isdigit(c);}
int ulibc_isspace(int c){return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v';}
int ulibc_isupper(int c){return c>='A'&&c<='Z';}
int ulibc_islower(int c){return c>='a'&&c<='z';}
int ulibc_isprint(int c){return c>=0x20&&c<=0x7E;}
int ulibc_isgraph(int c){return c>0x20&&c<=0x7E;}
int ulibc_iscntrl(int c){return(c>=0&&c<0x20)||c==0x7F;}
int ulibc_ispunct(int c){return ulibc_isgraph(c)&&!ulibc_isalnum(c);}
int ulibc_isxdigit(int c){return ulibc_isdigit(c)||(c>='A'&&c<='F')||(c>='a'&&c<='f');}
int ulibc_isascii(int c){return(unsigned)c<=0x7F;}
int ulibc_isblank(int c){return c==' '||c=='\t';}
int ulibc_toupper(int c){return ulibc_islower(c)?c-32:c;}
int ulibc_tolower(int c){return ulibc_isupper(c)?c+32:c;}

/* stdlib - malloc/free (simple bump+freelist allocator) */
#define CHUNK_MAGIC 0xA110CA7E
#define CHUNK_FREE  0xFEEEFEEE
#define MIN_ALLOC   32
typedef struct chunk_hdr {
    uint32_t magic;
    uint32_t size;  /* payload size */
    struct chunk_hdr *next; /* next free chunk (when free) */
    uint32_t flags; /* 1=free */
} chunk_hdr_t;

static uint8_t g_heap[ULIBC_HEAP_SIZE] __attribute__((aligned(16)));
static chunk_hdr_t *g_free_list = 0;
static size_t g_heap_used = 0;
static bool g_heap_init = false;

static void heap_init(void) {
    g_heap_init = true;
    g_heap_used = 0;
    g_free_list = 0;
}

void *ulibc_malloc(size_t size) {
    chunk_hdr_t *c, *prev, *best, *best_prev;
    size_t alloc_size;
    if(!g_heap_init) heap_init();
    if(!size) return 0;
    size = (size + 15) & ~15; /* align 16 */
    if(size < MIN_ALLOC) size = MIN_ALLOC;
    /* First-fit from free list */
    best = 0; best_prev = 0; prev = 0;
    for(c = g_free_list; c; prev = c, c = c->next) {
        if(c->flags && c->size >= size) {
            if(!best || c->size < best->size) { best = c; best_prev = prev; }
            if(c->size == size) break; /* exact fit */
        }
    }
    if(best) {
        /* Split if remainder large enough */
        if(best->size >= size + sizeof(chunk_hdr_t) + MIN_ALLOC) {
            chunk_hdr_t *split = (chunk_hdr_t*)((uint8_t*)best + sizeof(chunk_hdr_t) + size);
            split->magic = CHUNK_FREE;
            split->size = best->size - size - (uint32_t)sizeof(chunk_hdr_t);
            split->flags = 1;
            split->next = best->next;
            best->size = (uint32_t)size;
            best->next = split;
        }
        best->flags = 0;
        best->magic = CHUNK_MAGIC;
        if(best_prev) best_prev->next = best->next;
        else g_free_list = best->next;
        best->next = 0;
        return (void*)((uint8_t*)best + sizeof(chunk_hdr_t));
    }
    /* Bump allocate */
    alloc_size = sizeof(chunk_hdr_t) + size;
    if(g_heap_used + alloc_size > ULIBC_HEAP_SIZE) return 0;
    c = (chunk_hdr_t*)(g_heap + g_heap_used);
    c->magic = CHUNK_MAGIC;
    c->size = (uint32_t)size;
    c->next = 0;
    c->flags = 0;
    g_heap_used += alloc_size;
    return (void*)((uint8_t*)c + sizeof(chunk_hdr_t));
}

void ulibc_free(void *ptr) {
    chunk_hdr_t *c;
    if(!ptr) return;
    c = (chunk_hdr_t*)((uint8_t*)ptr - sizeof(chunk_hdr_t));
    if(c->magic != CHUNK_MAGIC) return; /* corrupt */
    c->magic = CHUNK_FREE;
    c->flags = 1;
    c->next = g_free_list;
    g_free_list = c;
}

void *ulibc_realloc(void *ptr, size_t size) {
    chunk_hdr_t *c;
    void *new_ptr;
    if(!ptr) return ulibc_malloc(size);
    if(!size) { ulibc_free(ptr); return 0; }
    c = (chunk_hdr_t*)((uint8_t*)ptr - sizeof(chunk_hdr_t));
    if(c->magic != CHUNK_MAGIC) return 0;
    if(c->size >= size) return ptr;
    new_ptr = ulibc_malloc(size);
    if(!new_ptr) return 0;
    ulibc_memcpy(new_ptr, ptr, c->size);
    ulibc_free(ptr);
    return new_ptr;
}

void *ulibc_calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *p = ulibc_malloc(total);
    if(p) ulibc_memset(p, 0, total);
    return p;
}

void *ulibc_memalign(size_t align, size_t size) {
    /* Simplified: over-allocate and align */
    void *p = ulibc_malloc(size + align);
    if(!p) return 0;
    uintptr_t addr = ((uintptr_t)p + align - 1) & ~(align - 1);
    return (void*)addr;
}

int ulibc_posix_memalign(void **memptr, size_t align, size_t size) {
    void *p = ulibc_memalign(align, size);
    if(!p) return ENOMEM;
    *memptr = p;
    return 0;
}

/* stdlib - conversions (strtol family from musl) */
long ulibc_strtol(const char *s, char **endp, int base) {
    const char *p = s;
    long result = 0;
    int neg = 0;
    while(ulibc_isspace((unsigned char)*p)) ++p;
    if(*p == '-') { neg = 1; ++p; }
    else if(*p == '+') ++p;
    if(base == 0) {
        if(*p == '0') {
            ++p;
            if(*p == 'x' || *p == 'X') { base = 16; ++p; }
            else base = 8;
        } else base = 10;
    } else if(base == 16 && *p == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    while(*p) {
        int digit;
        if(ulibc_isdigit((unsigned char)*p)) digit = *p - '0';
        else if(*p >= 'a' && *p <= 'z') digit = *p - 'a' + 10;
        else if(*p >= 'A' && *p <= 'Z') digit = *p - 'A' + 10;
        else break;
        if(digit >= base) break;
        result = result * base + digit;
        ++p;
    }
    if(endp) *endp = (char*)p;
    return neg ? -result : result;
}
unsigned long ulibc_strtoul(const char *s, char **endp, int base) {
    return (unsigned long)ulibc_strtol(s, endp, base);
}
long long ulibc_strtoll(const char *s, char **endp, int base) {
    return (long long)ulibc_strtol(s, endp, base);
}
unsigned long long ulibc_strtoull(const char *s, char **endp, int base) {
    return (unsigned long long)ulibc_strtoul(s, endp, base);
}
int ulibc_atoi(const char *s) { return (int)ulibc_strtol(s, 0, 10); }
long ulibc_atol(const char *s) { return ulibc_strtol(s, 0, 10); }
long long ulibc_atoll(const char *s) { return ulibc_strtoll(s, 0, 10); }
long ulibc_strtod(const char *s, char **endp) {
    /* No FPU/SSE in kernel mode - parse integer part only */
    long whole = 0;
    int neg = 0;
    const char *p = s;
    while(ulibc_isspace((unsigned char)*p)) ++p;
    if(*p == '-') { neg = 1; ++p; } else if(*p == '+') ++p;
    while(ulibc_isdigit((unsigned char)*p)) { whole = whole * 10 + (*p - '0'); ++p; }
    if(*p == '.') { ++p; while(ulibc_isdigit((unsigned char)*p)) ++p; }
    if(endp) *endp = (char*)p;
    return neg ? -whole : whole;
}

/* stdlib - qsort (shellsort, simple and robust) */
void ulibc_qsort(void *base, size_t nmemb, size_t size, int(*cmp)(const void*,const void*)) {
    uint8_t *b = (uint8_t*)base;
    size_t gap, i, j;
    uint8_t tmp[256]; /* max element size for stack swap */
    if(size > 256 || nmemb < 2) return;
    for(gap = nmemb/2; gap > 0; gap /= 2) {
        for(i = gap; i < nmemb; ++i) {
            ulibc_memcpy(tmp, b + i*size, size);
            for(j = i; j >= gap && cmp(b + (j-gap)*size, tmp) > 0; j -= gap)
                ulibc_memcpy(b + j*size, b + (j-gap)*size, size);
            ulibc_memcpy(b + j*size, tmp, size);
        }
    }
}

void *ulibc_bsearch(const void *key, const void *base, size_t nmemb, size_t size, int(*cmp)(const void*,const void*)) {
    const uint8_t *b = (const uint8_t*)base;
    size_t lo = 0, hi = nmemb;
    while(lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int r = cmp(key, b + mid*size);
        if(r < 0) hi = mid;
        else if(r > 0) lo = mid + 1;
        else return (void*)(b + mid*size);
    }
    return 0;
}

/* stdlib - misc */
int ulibc_abs(int x) { return x < 0 ? -x : x; }
long ulibc_labs(long x) { return x < 0 ? -x : x; }
void ulibc_abort(void) { real_sys_kill((int)real_sys_getpid(), SIGABRT); real_sys_exit(134); }
void ulibc_exit(int status) { real_sys_exit_group(status); }
void ulibc__exit(int status) { real_sys_exit(status); }
static void(*g_atexit_fns[32])(void);
static int g_atexit_count = 0;
int ulibc_atexit(void(*func)(void)) {
    if(g_atexit_count >= 32) return -1;
    g_atexit_fns[g_atexit_count++] = func;
    return 0;
}

/* Environment */
#define ENV_MAX 128
static char *g_environ[ENV_MAX];
static int g_env_count = 0;
static bool g_env_init = false;

static void env_init(void) {
    if(g_env_init) return;
    g_env_init = true;
    g_env_count = 0;
    /* Seed default env vars that apps expect */
    g_environ[g_env_count++] = (char*)"HOME=/root";
    g_environ[g_env_count++] = (char*)"USER=root";
    g_environ[g_env_count++] = (char*)"LOGNAME=root";
    g_environ[g_env_count++] = (char*)"PATH=/usr/bin:/bin:/usr/sbin:/sbin:/usr/local/bin";
    g_environ[g_env_count++] = (char*)"SHELL=/bin/sh";
    g_environ[g_env_count++] = (char*)"TERM=xterm-256color";
    g_environ[g_env_count++] = (char*)"LANG=en_US.UTF-8";
    g_environ[g_env_count++] = (char*)"LC_ALL=en_US.UTF-8";
    g_environ[g_env_count++] = (char*)"DISPLAY=:0";
    g_environ[g_env_count++] = (char*)"XDG_RUNTIME_DIR=/run/user/0";
    g_environ[g_env_count++] = (char*)"XDG_SESSION_TYPE=x11";
    g_environ[g_env_count++] = (char*)"DBUS_SESSION_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket";
    g_environ[g_env_count++] = (char*)"HOSTNAME=ridux";
    g_environ[g_env_count++] = (char*)"PWD=/root";
    g_environ[g_env_count++] = (char*)"TMPDIR=/tmp";
    g_environ[g_env_count++] = (char*)"LD_LIBRARY_PATH=/usr/lib:/lib:/usr/local/lib";
}

char *ulibc_getenv(const char *name) {
    int i; size_t nlen;
    if(!g_env_init) env_init();
    nlen = ulibc_strlen(name);
    for(i = 0; i < g_env_count; ++i) {
        if(ulibc_strncmp(g_environ[i], name, nlen) == 0 && g_environ[i][nlen] == '=')
            return g_environ[i] + nlen + 1;
    }
    return 0;
}

int ulibc_setenv(const char *name, const char *value, int overwrite) {
    int i; size_t nlen;
    if(!g_env_init) env_init();
    nlen = ulibc_strlen(name);
    for(i = 0; i < g_env_count; ++i) {
        if(ulibc_strncmp(g_environ[i], name, nlen) == 0 && g_environ[i][nlen] == '=') {
            if(!overwrite) return 0;
            /* Replace in-place (simplified: leak old) */
            char *buf = (char*)ulibc_malloc(nlen + ulibc_strlen(value) + 2);
            if(!buf) return -1;
            ulibc_strcpy(buf, name); ulibc_strcat(buf, "="); ulibc_strcat(buf, value);
            g_environ[i] = buf;
            return 0;
        }
    }
    if(g_env_count >= ENV_MAX) return -1;
    {char *buf = (char*)ulibc_malloc(nlen + ulibc_strlen(value) + 2);
     if(!buf) return -1;
     ulibc_strcpy(buf, name); ulibc_strcat(buf, "="); ulibc_strcat(buf, value);
     g_environ[g_env_count++] = buf;}
    return 0;
}

int ulibc_unsetenv(const char *name) {
    int i; size_t nlen;
    if(!g_env_init) env_init();
    nlen = ulibc_strlen(name);
    for(i = 0; i < g_env_count; ++i) {
        if(ulibc_strncmp(g_environ[i], name, nlen) == 0 && g_environ[i][nlen] == '=') {
            g_environ[i] = g_environ[--g_env_count];
            return 0;
        }
    }
    return 0;
}

/* Random (xorshift64) */
static uint64_t g_rand_state = 12345;
int ulibc_rand(void) {
    g_rand_state ^= g_rand_state << 13;
    g_rand_state ^= g_rand_state >> 7;
    g_rand_state ^= g_rand_state << 17;
    return (int)(g_rand_state & 0x7FFFFFFF);
}
void ulibc_srand(unsigned int seed) { g_rand_state = seed ? seed : 1; }
long ulibc_random(void) { return (long)ulibc_rand(); }
void ulibc_srandom(unsigned int seed) { ulibc_srand(seed); }

/* stdio - FILE streams, printf family */
static ulibc_FILE g_stdio_files[ULIBC_FOPEN_MAX];
static ulibc_FILE g_stdin_obj  = {.fd=0, .flags=1, .buf_mode=1};
static ulibc_FILE g_stdout_obj = {.fd=1, .flags=2, .buf_mode=1};
static ulibc_FILE g_stderr_obj = {.fd=2, .flags=2, .buf_mode=2};
ulibc_FILE *ulibc_stdin  = &g_stdin_obj;
ulibc_FILE *ulibc_stdout = &g_stdout_obj;
ulibc_FILE *ulibc_stderr = &g_stderr_obj;

ulibc_FILE *ulibc_fopen(const char *path, const char *mode) {
    int flags = 0, i;
    ulibc_FILE *f = 0;
    int fd;
    if(mode[0]=='r') flags = O_RDONLY;
    else if(mode[0]=='w') flags = O_WRONLY|O_CREAT|O_TRUNC;
    else if(mode[0]=='a') flags = O_WRONLY|O_CREAT|O_APPEND;
    if(mode[1]=='+' || (mode[1] && mode[2]=='+')) flags = O_RDWR|O_CREAT;
    fd = (int)real_sys_open(path, flags, 0644);
    if(fd < 0) return 0;
    for(i = 0; i < ULIBC_FOPEN_MAX; ++i) if(!g_stdio_files[i].fd && !g_stdio_files[i].flags) { f = &g_stdio_files[i]; break; }
    if(!f) { real_sys_close(fd); return 0; }
    ulibc_memset(f, 0, sizeof(*f));
    f->fd = fd;
    f->flags = (flags & O_RDWR) ? 3 : ((flags & O_WRONLY) ? 2 : 1);
    f->buf_mode = 0; /* full buffering */
    f->ungot = -1;
    return f;
}

ulibc_FILE *ulibc_fdopen(int fd, const char *mode) {
    int i; ulibc_FILE *f = 0;
    for(i = 0; i < ULIBC_FOPEN_MAX; ++i) if(!g_stdio_files[i].fd && !g_stdio_files[i].flags) { f = &g_stdio_files[i]; break; }
    if(!f) return 0;
    ulibc_memset(f, 0, sizeof(*f));
    f->fd = fd;
    f->flags = (mode[0] == 'r') ? 1 : 2;
    f->buf_mode = 0;
    f->ungot = -1;
    return f;
}

int ulibc_fclose(ulibc_FILE *f) {
    if(!f) return -1;
    ulibc_fflush(f);
    real_sys_close(f->fd);
    ulibc_memset(f, 0, sizeof(*f));
    return 0;
}

size_t ulibc_fread(void *ptr, size_t size, size_t nmemb, ulibc_FILE *f) {
    size_t total = size * nmemb, got = 0;
    int64_t r;
    if(!f || !total) return 0;
    r = real_sys_read(f->fd, ptr, total);
    if(r <= 0) { if(r == 0) f->eof = 1; else f->error = 1; return 0; }
    got = (size_t)r;
    return got / size;
}

size_t ulibc_fwrite(const void *ptr, size_t size, size_t nmemb, ulibc_FILE *f) {
    size_t total = size * nmemb;
    int64_t w;
    if(!f || !total) return 0;
    w = real_sys_write(f->fd, ptr, total);
    if(w < 0) { f->error = 1; return 0; }
    return (size_t)w / size;
}

int ulibc_fgetc(ulibc_FILE *f) {
    uint8_t c;
    if(!f) return ULIBC_EOF;
    if(f->ungot >= 0) { int r = f->ungot; f->ungot = -1; return r; }
    if(real_sys_read(f->fd, &c, 1) != 1) { f->eof = 1; return ULIBC_EOF; }
    return (int)c;
}

int ulibc_fputc(int c, ulibc_FILE *f) {
    uint8_t ch = (uint8_t)c;
    if(!f) return ULIBC_EOF;
    if(real_sys_write(f->fd, &ch, 1) != 1) { f->error = 1; return ULIBC_EOF; }
    return c;
}

char *ulibc_fgets(char *s, int size, ulibc_FILE *f) {
    int i = 0, c;
    if(!f || size <= 0) return 0;
    while(i < size - 1) {
        c = ulibc_fgetc(f);
        if(c == ULIBC_EOF) { if(i == 0) return 0; break; }
        s[i++] = (char)c;
        if(c == '\n') break;
    }
    s[i] = 0;
    return s;
}

int ulibc_fputs(const char *s, ulibc_FILE *f) {
    size_t len = ulibc_strlen(s);
    return (real_sys_write(f->fd, s, len) >= 0) ? 0 : ULIBC_EOF;
}

int ulibc_ungetc(int c, ulibc_FILE *f) {
    if(!f || c == ULIBC_EOF) return ULIBC_EOF;
    f->ungot = c; f->eof = 0;
    return c;
}

int ulibc_fseek(ulibc_FILE *f, long offset, int whence) {
    if(!f) return -1;
    f->eof = 0;
    return (real_sys_lseek(f->fd, offset, whence) < 0) ? -1 : 0;
}
long ulibc_ftell(ulibc_FILE *f) { return f ? (long)real_sys_lseek(f->fd, 0, SEEK_CUR) : -1; }
void ulibc_rewind(ulibc_FILE *f) { if(f) { ulibc_fseek(f, 0, SEEK_SET); f->error = 0; } }
int ulibc_fflush(ulibc_FILE *f) { (void)f; return 0; }
int ulibc_feof(ulibc_FILE *f) { return f ? f->eof : 0; }
int ulibc_ferror(ulibc_FILE *f) { return f ? f->error : 0; }
void ulibc_clearerr(ulibc_FILE *f) { if(f) { f->eof = 0; f->error = 0; } }
int ulibc_fileno(ulibc_FILE *f) { return f ? f->fd : -1; }

/* vsnprintf - core formatting engine (musl-inspired) */
int ulibc_vsnprintf(char *buf, size_t size, const char *fmt, __builtin_va_list ap) {
    size_t pos = 0;
    #define PUTC(ch) do { if(pos+1<size) buf[pos]=(char)(ch); ++pos; } while(0)
    while(*fmt) {
        if(*fmt != '%') { PUTC(*fmt++); continue; }
        ++fmt;
        /* Flags */
        int left=0, zero=0, plus=0, space=0, hash=0;
        while(*fmt=='-'||*fmt=='0'||*fmt=='+'||*fmt==' '||*fmt=='#'){
            if(*fmt=='-')left=1;if(*fmt=='0')zero=1;if(*fmt=='+')plus=1;
            if(*fmt==' ')space=1;if(*fmt=='#')hash=1;++fmt;
        }
        /* Width */
        int width=0;
        if(*fmt=='*'){width=__builtin_va_arg(ap,int);++fmt;}
        else while(ulibc_isdigit((unsigned char)*fmt)){width=width*10+(*fmt-'0');++fmt;}
        /* Precision */
        int prec=-1;
        if(*fmt=='.'){++fmt;prec=0;
            if(*fmt=='*'){prec=__builtin_va_arg(ap,int);++fmt;}
            else while(ulibc_isdigit((unsigned char)*fmt)){prec=prec*10+(*fmt-'0');++fmt;}
        }
        /* Length modifier */
        int lmod=0; /* 0=int,1=long,2=long long,3=size_t */
        if(*fmt=='l'){++fmt;++lmod;if(*fmt=='l'){++fmt;++lmod;}}
        else if(*fmt=='z'){++fmt;lmod=3;}
        else if(*fmt=='h'){++fmt;if(*fmt=='h')++fmt;}
        (void)left;(void)plus;(void)space;(void)hash;
        /* Conversion */
        if(*fmt=='d'||*fmt=='i'){
            long long val;
            char tmp[24];int ti=0;int neg=0;
            if(lmod>=2)val=__builtin_va_arg(ap,long long);
            else if(lmod==1)val=(long long)__builtin_va_arg(ap,long);
            else val=(long long)__builtin_va_arg(ap,int);
            if(val<0){neg=1;val=-val;}
            if(!val)tmp[ti++]='0';
            else while(val){tmp[ti++]='0'+(char)(val%10);val/=10;}
            if(neg)tmp[ti++]='-';
            {int pad=width-ti;if(zero&&!left)while(pad-->0)PUTC('0');
             else while(pad-->0)PUTC(' ');}
            while(--ti>=0)PUTC(tmp[ti]);
        } else if(*fmt=='u'){
            unsigned long long val;
            char tmp[24];int ti=0;
            if(lmod>=2)val=__builtin_va_arg(ap,unsigned long long);
            else if(lmod==1)val=(unsigned long long)__builtin_va_arg(ap,unsigned long);
            else val=(unsigned long long)__builtin_va_arg(ap,unsigned int);
            if(!val)tmp[ti++]='0';
            else while(val){tmp[ti++]='0'+(char)(val%10);val/=10;}
            {int pad=width-ti;while(pad-->0)PUTC(zero?'0':' ');}
            while(--ti>=0)PUTC(tmp[ti]);
        } else if(*fmt=='x'||*fmt=='X'){
            unsigned long long val;
            char tmp[20];int ti=0;
            const char *hex=(*fmt=='x')?"0123456789abcdef":"0123456789ABCDEF";
            if(lmod>=2)val=__builtin_va_arg(ap,unsigned long long);
            else if(lmod==1)val=(unsigned long long)__builtin_va_arg(ap,unsigned long);
            else val=(unsigned long long)__builtin_va_arg(ap,unsigned int);
            if(!val)tmp[ti++]='0';
            else while(val){tmp[ti++]=hex[val&0xF];val>>=4;}
            if(hash){PUTC('0');PUTC(*fmt);}
            {int pad=width-ti;while(pad-->0)PUTC(zero?'0':' ');}
            while(--ti>=0)PUTC(tmp[ti]);
        } else if(*fmt=='o'){
            unsigned long long val;
            char tmp[24];int ti=0;
            if(lmod>=2)val=__builtin_va_arg(ap,unsigned long long);
            else val=(unsigned long long)__builtin_va_arg(ap,unsigned int);
            if(!val)tmp[ti++]='0';
            else while(val){tmp[ti++]='0'+(char)(val&7);val>>=3;}
            while(--ti>=0)PUTC(tmp[ti]);
        } else if(*fmt=='s'){
            const char *s=__builtin_va_arg(ap,const char*);
            if(!s)s="(null)";
            {int slen=(int)ulibc_strlen(s);
             if(prec>=0&&slen>prec)slen=prec;
             {int pad=width-slen;if(!left)while(pad-->0)PUTC(' ');
              {int j;for(j=0;j<slen;++j)PUTC(s[j]);}
              if(left)while(pad-->0)PUTC(' ');}}
        } else if(*fmt=='c'){
            int ch=__builtin_va_arg(ap,int);PUTC(ch);
        } else if(*fmt=='p'){
            uint64_t val=(uint64_t)(uintptr_t)__builtin_va_arg(ap,void*);
            const char *hex="0123456789abcdef";
            char tmp[18];int ti=0;
            PUTC('0');PUTC('x');
            if(!val)tmp[ti++]='0';
            else while(val){tmp[ti++]=hex[val&0xF];val>>=4;}
            while(--ti>=0)PUTC(tmp[ti]);
        } else if(*fmt=='%'){
            PUTC('%');
        } else if(*fmt=='n'){
            int *np=__builtin_va_arg(ap,int*);if(np)*np=(int)pos;
        } else {
            PUTC('%');PUTC(*fmt);
        }
        ++fmt;
    }
    if(size>0) buf[pos<size?pos:size-1]=0;
    #undef PUTC
    return(int)pos;
}

int ulibc_snprintf(char *buf, size_t size, const char *fmt, ...) {
    int r; __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    r = ulibc_vsnprintf(buf, size, fmt, ap);
    __builtin_va_end(ap);
    return r;
}
int ulibc_sprintf(char *buf, const char *fmt, ...) {
    int r; __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    r = ulibc_vsnprintf(buf, 0x7FFFFFFF, fmt, ap);
    __builtin_va_end(ap);
    return r;
}
int ulibc_vfprintf(ulibc_FILE *f, const char *fmt, __builtin_va_list ap) {
    char buf[4096]; int r;
    r = ulibc_vsnprintf(buf, sizeof(buf), fmt, ap);
    if(r > 0) real_sys_write(f->fd, buf, (size_t)r);
    return r;
}
int ulibc_fprintf(ulibc_FILE *f, const char *fmt, ...) {
    int r; __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    r = ulibc_vfprintf(f, fmt, ap);
    __builtin_va_end(ap);
    return r;
}
int ulibc_printf(const char *fmt, ...) {
    int r; __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    r = ulibc_vfprintf(ulibc_stdout, fmt, ap);
    __builtin_va_end(ap);
    return r;
}
int ulibc_puts(const char *s) {
    ulibc_fputs(s, ulibc_stdout);
    ulibc_fputc('\n', ulibc_stdout);
    return 0;
}
void ulibc_perror(const char *s) {
    if(s && *s) { ulibc_fputs(s, ulibc_stderr); ulibc_fputs(": ", ulibc_stderr); }
    ulibc_fputs(ulibc_strerror(g_errno_val), ulibc_stderr);
    ulibc_fputc('\n', ulibc_stderr);
}
int ulibc_sscanf(const char *str, const char *fmt, ...) {
    (void)str; (void)fmt; return 0; /* stub */
}

/* unistd.h wrappers */
int64_t ulibc_read(int fd, void *buf, size_t count) { return real_sys_read(fd, buf, count); }
int64_t ulibc_write(int fd, const void *buf, size_t count) { return real_sys_write(fd, buf, count); }
int ulibc_close(int fd) { return (int)real_sys_close(fd); }
int ulibc_dup(int fd) { return (int)real_sys_dup(fd); }
int ulibc_dup2(int oldfd, int newfd) { return (int)real_sys_dup2(oldfd, newfd); }
int64_t ulibc_lseek(int fd, int64_t offset, int whence) { return real_sys_lseek(fd, offset, whence); }
int ulibc_pipe(int pipefd[2]) { return (int)real_sys_pipe(pipefd); }
int ulibc_fork(void) { return (int)real_sys_fork(); }
int ulibc_execve(const char *path, char *const argv[], char *const envp[]) { return (int)real_sys_execve(path, argv, envp); }
unsigned int ulibc_sleep(unsigned int seconds) {
    timespec_t req = {.tv_sec = seconds, .tv_nsec = 0};
    real_sys_nanosleep(&req, 0);
    return 0;
}
int ulibc_usleep(uint64_t usec) {
    timespec_t req = {.tv_sec = (int64_t)(usec/1000000), .tv_nsec = (int64_t)((usec%1000000)*1000)};
    return (int)real_sys_nanosleep(&req, 0);
}
int ulibc_nanosleep_us(uint64_t us) { return ulibc_usleep(us); }
int ulibc_chdir(const char *path) { return (int)real_sys_chdir(path); }
char *ulibc_getcwd(char *buf, size_t size) { real_sys_getcwd(buf, size); return buf; }
int ulibc_getpid(void) { return (int)real_sys_getpid(); }
int ulibc_getppid(void) { return (int)real_sys_getppid(); }
int ulibc_getuid(void) { return (int)real_sys_getuid(); }
int ulibc_getgid(void) { return (int)real_sys_getgid(); }
int ulibc_access(const char *path, int mode) { return (int)real_sys_access(path, mode); }
int ulibc_unlink(const char *path) { return (int)real_sys_unlink(path); }
int ulibc_rmdir(const char *path) { (void)path; return 0; }
int ulibc_isatty(int fd) { return (fd >= 0 && fd <= 2) ? 1 : 0; }
int64_t ulibc_readlink(const char *path, char *buf, size_t bufsiz) { return real_sys_readlink(path, buf, bufsiz); }
int ulibc_symlink(const char *t, const char *l) { (void)t; (void)l; return 0; }
int ulibc_link(const char *o, const char *n) { (void)o; (void)n; return 0; }
int ulibc_fsync(int fd) { (void)fd; return 0; }
int ulibc_sched_yield(void) { return (int)real_sys_sched_yield(); }
int ulibc_gettid(void) { return (int)real_sys_gettid(); }
long ulibc_syscall(long nr, ...) {
    __builtin_va_list ap;
    uint64_t a0=0,a1=0,a2=0,a3=0,a4=0,a5=0;
    __builtin_va_start(ap, nr);
    a0 = (uint64_t)__builtin_va_arg(ap, uint64_t);
    a1 = (uint64_t)__builtin_va_arg(ap, uint64_t);
    a2 = (uint64_t)__builtin_va_arg(ap, uint64_t);
    a3 = (uint64_t)__builtin_va_arg(ap, uint64_t);
    a4 = (uint64_t)__builtin_va_arg(ap, uint64_t);
    a5 = (uint64_t)__builtin_va_arg(ap, uint64_t);
    __builtin_va_end(ap);
    return (long)syscall_dispatch((uint64_t)nr, a0, a1, a2, a3, a4, a5);
}

/* pthread (POSIX threads) - spinlock-based, inspired by musl */
static inline int atomic_cas(volatile int *p, int old, int new_val) {
    int prev;
    __asm__ volatile("lock cmpxchgl %2, %1"
        : "=a"(prev), "+m"(*p)
        : "r"(new_val), "0"(old)
        : "memory");
    return prev;
}
static inline void atomic_store(volatile int *p, int v) {
    __asm__ volatile("" ::: "memory");
    *p = v;
    __asm__ volatile("" ::: "memory");
}
static inline int atomic_load(volatile int *p) {
    int v;
    __asm__ volatile("" ::: "memory");
    v = *p;
    __asm__ volatile("" ::: "memory");
    return v;
}

static void ulibc_tls_ensure_task_base(int tidx);

int ulibc_pthread_create(ulibc_pthread_t *thread, const ulibc_pthread_attr_t *attr, void*(*start)(void*), void *arg) {
    (void)attr;
    task_t *cur = task_current();
    int pid = task_create("pthread", (uint64_t)(uintptr_t)start, true);
    if(pid < 0) return EAGAIN;
    ulibc_tls_ensure_task_base(g_current_task);
    /* Store arg in the new task's rdi (first arg in SysV ABI) */
    {int i; for(i = 0; i < TASK_MAX; ++i) {
        if(g_tasks[i].used && g_tasks[i].pid == pid) {
            g_tasks[i].ctx.rdi = (uint64_t)(uintptr_t)arg;
            g_tasks[i].ppid = cur->pid;
            ulibc_tls_ensure_task_base(i);
            break;
        }
    }}
    if(thread) *thread = (ulibc_pthread_t)pid;
    return 0;
}

int ulibc_pthread_join(ulibc_pthread_t thread, void **retval) {
    int status = 0;
    task_waitpid((int)thread, &status, 0);
    if(retval) *retval = (void*)(uintptr_t)status;
    return 0;
}

int ulibc_pthread_detach(ulibc_pthread_t thread) { (void)thread; return 0; }
ulibc_pthread_t ulibc_pthread_self(void) { return (ulibc_pthread_t)task_current()->pid; }
int ulibc_pthread_equal(ulibc_pthread_t t1, ulibc_pthread_t t2) { return t1 == t2; }
void ulibc_pthread_exit(void *retval) { (void)retval; real_sys_exit(0); }

#ifndef EDEADLK
#define EDEADLK 35
#endif

int ulibc_pthread_mutexattr_init(ulibc_pthread_mutexattr_t *attr) {
    if(!attr) return EINVAL;
    attr->attr = ULIBC_PTHREAD_MUTEX_NORMAL;
    return 0;
}
int ulibc_pthread_mutexattr_destroy(ulibc_pthread_mutexattr_t *attr) { (void)attr; return 0; }
int ulibc_pthread_mutexattr_settype(ulibc_pthread_mutexattr_t *attr, int type) {
    if(!attr) return EINVAL;
    if(type != ULIBC_PTHREAD_MUTEX_NORMAL &&
       type != ULIBC_PTHREAD_MUTEX_RECURSIVE &&
       type != ULIBC_PTHREAD_MUTEX_ERRORCHECK) return EINVAL;
    attr->attr = type;
    return 0;
}
int ulibc_pthread_mutexattr_gettype(const ulibc_pthread_mutexattr_t *attr, int *type) {
    if(!attr || !type) return EINVAL;
    *type = attr->attr;
    return 0;
}

int ulibc_pthread_mutex_init(ulibc_pthread_mutex_t *m, const ulibc_pthread_mutexattr_t *attr) {
    ulibc_memset(m, 0, sizeof(*m));
    m->type = attr ? attr->attr : ULIBC_PTHREAD_MUTEX_NORMAL;
    return 0;
}
int ulibc_pthread_mutex_lock(ulibc_pthread_mutex_t *m) {
    int self = (int)ulibc_pthread_self();
    if(m->owner == self) {
        if(m->type == ULIBC_PTHREAD_MUTEX_RECURSIVE) {
            ++m->count;
            return 0;
        }
        if(m->type == ULIBC_PTHREAD_MUTEX_ERRORCHECK) return EDEADLK;
    }
    for(;;) {
        int v = atomic_cas(&m->lock, 0, 1);
        if(v == 0) {
            m->owner = self;
            m->count = 1;
            return 0;
        }
        if(v == 1) atomic_cas(&m->lock, 1, 2);
        real_sys_futex((uint32_t*)&m->lock, FUTEX_WAIT, 2, 0, 0, 0);
    }
}
int ulibc_pthread_mutex_trylock(ulibc_pthread_mutex_t *m) {
    int self = (int)ulibc_pthread_self();
    if(m->owner == self) {
        if(m->type == ULIBC_PTHREAD_MUTEX_RECURSIVE) {
            ++m->count;
            return 0;
        }
        if(m->type == ULIBC_PTHREAD_MUTEX_ERRORCHECK) return EDEADLK;
    }
    if(atomic_cas(&m->lock, 0, 1) == 0) {
        m->owner = self;
        m->count = 1;
        return 0;
    }
    return EBUSY;
}
int ulibc_pthread_mutex_unlock(ulibc_pthread_mutex_t *m) {
    int self = (int)ulibc_pthread_self();
    int prev;
    if(m->owner != self) return EPERM;
    if(m->type == ULIBC_PTHREAD_MUTEX_RECURSIVE && m->count > 1) {
        --m->count;
        return 0;
    }
    m->owner = 0;
    m->count = 0;
    prev = atomic_load(&m->lock);
    atomic_store(&m->lock, 0);
    if(prev == 2) real_sys_futex((uint32_t*)&m->lock, FUTEX_WAKE, 1, 0, 0, 0);
    return 0;
}
int ulibc_pthread_mutex_destroy(ulibc_pthread_mutex_t *m) { (void)m; return 0; }

int ulibc_pthread_condattr_init(ulibc_pthread_condattr_t *attr) {
    if(!attr) return EINVAL;
    attr->attr = 0; /* CLOCK_REALTIME */
    return 0;
}
int ulibc_pthread_condattr_destroy(ulibc_pthread_condattr_t *attr) { (void)attr; return 0; }
int ulibc_pthread_condattr_setclock(ulibc_pthread_condattr_t *attr, int clock_id) {
    if(!attr) return EINVAL;
    attr->attr = clock_id;
    return 0;
}
int ulibc_pthread_condattr_getclock(const ulibc_pthread_condattr_t *attr, int *clock_id) {
    if(!attr || !clock_id) return EINVAL;
    *clock_id = attr->attr;
    return 0;
}

int ulibc_pthread_cond_init(ulibc_pthread_cond_t *c, const ulibc_pthread_condattr_t *attr) {
    (void)attr; ulibc_memset(c, 0, sizeof(*c)); return 0;
}
int ulibc_pthread_cond_wait(ulibc_pthread_cond_t *c, ulibc_pthread_mutex_t *m) {
    int seq = atomic_load(&c->seq);
    __asm__ volatile("lock incl %0" : "+m"(c->waiters) :: "memory");
    ulibc_pthread_mutex_unlock(m);
    while(atomic_load(&c->seq) == seq) {
        int64_t fr = real_sys_futex((uint32_t*)&c->seq, FUTEX_WAIT, (uint32_t)seq, 0, 0, 0);
        if(fr == -EAGAIN) break; /* seq changed before sleeping */
        if(fr == -EINTR) continue;
    }
    ulibc_pthread_mutex_lock(m);
    __asm__ volatile("lock decl %0" : "+m"(c->waiters) :: "memory");
    return 0;
}
int ulibc_pthread_cond_timedwait(ulibc_pthread_cond_t *c, ulibc_pthread_mutex_t *m, const timespec_t *abstime) {
    timespec_t now, rel, *timeout = 0;
    int seq = atomic_load(&c->seq);
    if(abstime) {
        if(real_sys_clock_gettime(0, &now) == 0) {
            int64_t dsec = abstime->tv_sec - now.tv_sec;
            int64_t dnsec = abstime->tv_nsec - now.tv_nsec;
            if(dnsec < 0) { dnsec += 1000000000LL; --dsec; }
            if(dsec < 0 || (dsec == 0 && dnsec <= 0)) return ETIMEDOUT;
            rel.tv_sec = dsec;
            rel.tv_nsec = dnsec;
            timeout = &rel;
        } else {
            timeout = (timespec_t*)abstime;
        }
    }
    __asm__ volatile("lock incl %0" : "+m"(c->waiters) :: "memory");
    ulibc_pthread_mutex_unlock(m);
    while(atomic_load(&c->seq) == seq) {
        int64_t fr = real_sys_futex((uint32_t*)&c->seq, FUTEX_WAIT, (uint32_t)seq, timeout, 0, 0);
        if(fr == -ETIMEDOUT) {
            __asm__ volatile("lock decl %0" : "+m"(c->waiters) :: "memory");
            ulibc_pthread_mutex_lock(m);
            return ETIMEDOUT;
        }
        if(fr == -EAGAIN) break;
        if(fr == -EINTR) continue;
    }
    ulibc_pthread_mutex_lock(m);
    __asm__ volatile("lock decl %0" : "+m"(c->waiters) :: "memory");
    return 0;
}
int ulibc_pthread_cond_signal(ulibc_pthread_cond_t *c) {
    atomic_store(&c->seq, atomic_load(&c->seq) + 1);
    if(atomic_load(&c->waiters) > 0) {
        real_sys_futex((uint32_t*)&c->seq, FUTEX_WAKE, 1, 0, 0, 0);
    }
    return 0;
}
int ulibc_pthread_cond_broadcast(ulibc_pthread_cond_t *c) {
    atomic_store(&c->seq, atomic_load(&c->seq) + 1);
    if(atomic_load(&c->waiters) > 0) {
        real_sys_futex((uint32_t*)&c->seq, FUTEX_WAKE, (uint32_t)atomic_load(&c->waiters), 0, 0, 0);
    }
    return 0;
}
int ulibc_pthread_cond_destroy(ulibc_pthread_cond_t *c) { (void)c; return 0; }

int ulibc_pthread_rwlock_init(ulibc_pthread_rwlock_t *rw, const ulibc_pthread_rwlockattr_t *attr) {
    (void)attr; ulibc_memset(rw, 0, sizeof(*rw)); return 0;
}
int ulibc_pthread_rwlock_rdlock(ulibc_pthread_rwlock_t *rw) {
    while(atomic_load(&rw->writer)) __asm__ volatile("pause");
    __asm__ volatile("lock incl %0" : "+m"(rw->readers) :: "memory");
    return 0;
}
int ulibc_pthread_rwlock_wrlock(ulibc_pthread_rwlock_t *rw) {
    while(atomic_cas(&rw->writer, 0, 1) != 0) __asm__ volatile("pause");
    while(atomic_load(&rw->readers)) __asm__ volatile("pause");
    return 0;
}
int ulibc_pthread_rwlock_unlock(ulibc_pthread_rwlock_t *rw) {
    if(atomic_load(&rw->writer)) atomic_store(&rw->writer, 0);
    else __asm__ volatile("lock decl %0" : "+m"(rw->readers) :: "memory");
    return 0;
}
int ulibc_pthread_rwlock_destroy(ulibc_pthread_rwlock_t *rw) { (void)rw; return 0; }

/* Thread-local storage */
static void *g_tls_values[TASK_MAX][ULIBC_PTHREAD_KEYS_MAX];
static void(*g_tls_destructors[ULIBC_PTHREAD_KEYS_MAX])(void*);
static int g_tls_next_key = 0;
#define ULIBC_TLS_MODULE_MAX 128
#define ULIBC_TLS_BLOCK_INIT 4096
typedef struct {
    void  *block;
    size_t size;
} ulibc_tls_mod_t;
static ulibc_tls_mod_t g_tls_modules[TASK_MAX][ULIBC_TLS_MODULE_MAX];
typedef struct {
    bool     valid;
    uint64_t init;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} ulibc_tls_desc_t;
static ulibc_tls_desc_t g_tls_desc[ULIBC_TLS_MODULE_MAX];
typedef struct {
    void *self;
    void *dtv;
    uint64_t canary;
} ulibc_tcb_t;
static ulibc_tcb_t *g_tls_tcb[TASK_MAX];

static void ulibc_tls_refresh_modules(void){
    compat3_dlobj_info_t objs[64];
    int n,i;
    ulibc_memset(g_tls_desc,0,sizeof(g_tls_desc));
    n=compat3_dynobj_snapshot(objs,(int)(sizeof(objs)/sizeof(objs[0])));
    for(i=0;i<n;++i){
        uint64_t mod=objs[i].tls_module;
        if(mod==0||mod>=ULIBC_TLS_MODULE_MAX)continue;
        g_tls_desc[mod].valid=(objs[i].tls_memsz!=0);
        g_tls_desc[mod].init=objs[i].tls_init;
        g_tls_desc[mod].filesz=objs[i].tls_filesz;
        g_tls_desc[mod].memsz=objs[i].tls_memsz;
        g_tls_desc[mod].align=objs[i].tls_align?objs[i].tls_align:16;
    }
}

static void ulibc_tls_ensure_task_base(int tidx){
    ulibc_tcb_t *tcb;
    if(tidx < 0 || tidx >= TASK_MAX) return;
    if(!g_tasks[tidx].used) return;
    if(g_tasks[tidx].ctx.fs_base) return;
    if(g_tls_tcb[tidx]) {
        g_tls_tcb[tidx]->dtv = (void*)&g_tls_modules[tidx][0];
        g_tasks[tidx].ctx.fs_base = (uint64_t)(uintptr_t)g_tls_tcb[tidx];
        return;
    }
    tcb = (ulibc_tcb_t*)ulibc_calloc(1, sizeof(*tcb));
    if(!tcb) return;
    tcb->self = tcb;
    tcb->dtv = (void*)&g_tls_modules[tidx][0];
    tcb->canary = (uint64_t)(uintptr_t)tcb ^ 0x9e3779b97f4a7c15ULL;
    g_tls_tcb[tidx] = tcb;
    g_tasks[tidx].ctx.fs_base = (uint64_t)(uintptr_t)tcb;
}

void compat4_tls_reset_task(int tidx){
    if(tidx < 0 || tidx >= TASK_MAX) return;
    ulibc_memset(g_tls_values[tidx], 0, sizeof(g_tls_values[tidx]));
    ulibc_memset(g_tls_modules[tidx], 0, sizeof(g_tls_modules[tidx]));
    if(g_tls_tcb[tidx]) {
        g_tls_tcb[tidx]->dtv = (void*)&g_tls_modules[tidx][0];
        g_tasks[tidx].ctx.fs_base = (uint64_t)(uintptr_t)g_tls_tcb[tidx];
    } else {
        g_tasks[tidx].ctx.fs_base = 0;
        ulibc_tls_ensure_task_base(tidx);
    }
}

int ulibc_pthread_key_create(ulibc_pthread_key_t *key, void(*dtor)(void*)) {
    if(g_tls_next_key >= ULIBC_PTHREAD_KEYS_MAX) return EAGAIN;
    *key = (ulibc_pthread_key_t)g_tls_next_key;
    g_tls_destructors[g_tls_next_key] = dtor;
    ++g_tls_next_key;
    return 0;
}
int ulibc_pthread_key_delete(ulibc_pthread_key_t key) {
    if(key >= ULIBC_PTHREAD_KEYS_MAX) return EINVAL;
    g_tls_destructors[key] = 0;
    return 0;
}
void *ulibc_pthread_getspecific(ulibc_pthread_key_t key) {
    if(key >= ULIBC_PTHREAD_KEYS_MAX) return 0;
    return g_tls_values[g_current_task][key];
}
int ulibc_pthread_setspecific(ulibc_pthread_key_t key, const void *value) {
    if(key >= ULIBC_PTHREAD_KEYS_MAX) return EINVAL;
    g_tls_values[g_current_task][key] = (void*)value;
    return 0;
}
int ulibc_pthread_once(ulibc_pthread_once_t *once, void(*init_routine)(void)) {
    if(atomic_cas(once, 0, 1) == 0) init_routine();
    return 0;
}
int ulibc_pthread_setname_np(ulibc_pthread_t thread, const char *name) {
    int i; for(i=0;i<TASK_MAX;++i)if(g_tasks[i].used&&(ulibc_pthread_t)g_tasks[i].pid==thread){
        ulibc_strncpy(g_tasks[i].name,name,TASK_NAME_LEN-1);return 0;}
    return ESRCH;
}
int ulibc_pthread_getname_np(ulibc_pthread_t thread, char *name, size_t len) {
    int i; for(i=0;i<TASK_MAX;++i)if(g_tasks[i].used&&(ulibc_pthread_t)g_tasks[i].pid==thread){
        ulibc_strncpy(name,g_tasks[i].name,len);return 0;}
    return ESRCH;
}
int ulibc_pthread_attr_init(ulibc_pthread_attr_t *attr) { attr->attr=0; return 0; }
int ulibc_pthread_attr_destroy(ulibc_pthread_attr_t *attr) { (void)attr; return 0; }
int ulibc_pthread_attr_setdetachstate(ulibc_pthread_attr_t *attr, int state) { (void)attr;(void)state; return 0; }
int ulibc_pthread_attr_setstacksize(ulibc_pthread_attr_t *attr, size_t stacksize) { (void)attr;(void)stacksize; return 0; }

/* DRM / framebuffer ioctl handlers */
/* framebuffer_t defined in kernel.c */
typedef struct {
    uint8_t *address;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t  bpp;
    uint8_t  red_pos, red_size;
    uint8_t  green_pos, green_size;
    uint8_t  blue_pos, blue_size;
    bool     ready;
} compat4_fb_t;
extern compat4_fb_t g_fb;

#define C4_DRM_ID_FB 1u
#define C4_DRM_ID_CRTC 42u
#define C4_DRM_ID_CONNECTOR 43u
#define C4_DRM_ID_ENCODER 44u

typedef struct {
    uint32_t clock;
    uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew;
    uint16_t vdisplay, vsync_start, vsync_end, vtotal, vscan;
    uint32_t vrefresh;
    uint32_t flags;
    uint32_t type;
    char name[32];
} c4_drm_mode_modeinfo_t;

typedef struct {
    uint64_t fb_id_ptr;
    uint64_t crtc_id_ptr;
    uint64_t connector_id_ptr;
    uint64_t encoder_id_ptr;
    uint32_t count_fbs;
    uint32_t count_crtcs;
    uint32_t count_connectors;
    uint32_t count_encoders;
    uint32_t min_width;
    uint32_t max_width;
    uint32_t min_height;
    uint32_t max_height;
} c4_drm_mode_card_res_t;

typedef struct {
    uint64_t set_connectors_ptr;
    uint32_t count_connectors;
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t x;
    uint32_t y;
    uint32_t gamma_size;
    uint32_t mode_valid;
    c4_drm_mode_modeinfo_t mode;
} c4_drm_mode_crtc_t;

typedef struct {
    uint32_t encoder_id;
    uint32_t encoder_type;
    uint32_t crtc_id;
    uint32_t possible_crtcs;
    uint32_t possible_clones;
} c4_drm_mode_get_encoder_t;

typedef struct {
    uint64_t encoders_ptr;
    uint64_t modes_ptr;
    uint64_t props_ptr;
    uint64_t prop_values_ptr;
    uint32_t count_modes;
    uint32_t count_props;
    uint32_t count_encoders;
    uint32_t encoder_id;
    uint32_t connector_id;
    uint32_t connector_type;
    uint32_t connector_type_id;
    uint32_t connection;
    uint32_t mm_width;
    uint32_t mm_height;
    uint32_t subpixel;
    uint32_t pad;
} c4_drm_mode_get_connector_t;

typedef struct {
    uint32_t fb_id;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t depth;
    uint32_t handle;
} c4_drm_mode_fb_cmd_t;

typedef struct {
    uint32_t height;
    uint32_t width;
    uint32_t bpp;
    uint32_t flags;
    uint32_t handle;
    uint32_t pitch;
    uint64_t size;
} c4_drm_mode_create_dumb_t;

typedef struct {
    uint32_t handle;
    uint32_t pad;
    uint64_t offset;
} c4_drm_mode_map_dumb_t;

typedef struct {
    uint64_t capability;
    uint64_t value;
} c4_drm_get_cap_t;

typedef struct {
    uint32_t handle;
    uint32_t flags;
    int32_t  fd;
} c4_drm_prime_handle_t;

typedef struct {
    uint32_t handle;
    uint32_t pad;
} c4_drm_gem_close_t;

static int32_t g_c4_drm_prime_export_fd = 100;

static void c4_drm_fill_mode(c4_drm_mode_modeinfo_t *m){
    uint32_t w=(g_fb.width?g_fb.width:1024u);
    uint32_t h=(g_fb.height?g_fb.height:768u);
    if(!m)return;
    ulibc_memset(m,0,sizeof(*m));
    m->hdisplay=(uint16_t)w;
    m->hsync_start=(uint16_t)(w+16u);
    m->hsync_end=(uint16_t)(w+16u+32u);
    m->htotal=(uint16_t)(w+16u+32u+48u);
    m->vdisplay=(uint16_t)h;
    m->vsync_start=(uint16_t)(h+10u);
    m->vsync_end=(uint16_t)(h+10u+2u);
    m->vtotal=(uint16_t)(h+10u+2u+33u);
    m->vrefresh=60;
    m->type=1;
    ulibc_strcpy(m->name,"ridux-60");
}

int drm_ioctl_handler(int fd, uint64_t request, void *arg) {
    (void)fd;
    if(request == DRM_IOCTL_VERSION) {
        /* Return driver version info */
        if(arg) {
            uint32_t *v = (uint32_t*)arg;
            v[0] = 1; v[1] = 0; v[2] = 0; /* major.minor.patch */
        }
        return 0;
    }
    if(request == DRM_IOCTL_GET_CAP) {
        if(arg) {
            c4_drm_get_cap_t *cap=(c4_drm_get_cap_t*)arg;
            cap->value=1;
        }
        return 0;
    }
    if(request == DRM_IOCTL_SET_MASTER || request == DRM_IOCTL_DROP_MASTER) return 0;
    if(request == DRM_IOCTL_GEM_CLOSE || (request&0xFFu)==0x09u) {
        if(arg){
            c4_drm_gem_close_t *c=(c4_drm_gem_close_t*)arg;
            (void)c->handle;
            c->handle=0;
        }
        return 0;
    }
    if(request == DRM_IOCTL_PRIME_HANDLE_TO_FD || (request&0xFFu)==0x2Du) {
        if(arg){
            c4_drm_prime_handle_t *p=(c4_drm_prime_handle_t*)arg;
            if(!p->fd)p->fd=g_c4_drm_prime_export_fd++;
            if(g_c4_drm_prime_export_fd<100)g_c4_drm_prime_export_fd=100;
        }
        return 0;
    }
    if(request == DRM_IOCTL_PRIME_FD_TO_HANDLE || (request&0xFFu)==0x2Eu) {
        if(arg){
            c4_drm_prime_handle_t *p=(c4_drm_prime_handle_t*)arg;
            p->handle=1;
            (void)p->flags;
        }
        return 0;
    }
    if(request == DRM_IOCTL_MODE_GETRESOURCES) {
        c4_drm_mode_card_res_t *r=(c4_drm_mode_card_res_t*)arg;
        uint32_t w=(g_fb.width?g_fb.width:1024u);
        uint32_t h=(g_fb.height?g_fb.height:768u);
        if(!r)return 0;
        if(r->fb_id_ptr&&r->count_fbs>0)((uint32_t*)(uintptr_t)r->fb_id_ptr)[0]=C4_DRM_ID_FB;
        if(r->crtc_id_ptr&&r->count_crtcs>0)((uint32_t*)(uintptr_t)r->crtc_id_ptr)[0]=C4_DRM_ID_CRTC;
        if(r->connector_id_ptr&&r->count_connectors>0)((uint32_t*)(uintptr_t)r->connector_id_ptr)[0]=C4_DRM_ID_CONNECTOR;
        if(r->encoder_id_ptr&&r->count_encoders>0)((uint32_t*)(uintptr_t)r->encoder_id_ptr)[0]=C4_DRM_ID_ENCODER;
        r->count_fbs=1;
        r->count_crtcs=1;
        r->count_connectors=1;
        r->count_encoders=1;
        r->min_width=64;
        r->max_width=w;
        r->min_height=64;
        r->max_height=h;
        return 0;
    }
    if(request == DRM_IOCTL_MODE_GETCRTC) {
        c4_drm_mode_crtc_t *c=(c4_drm_mode_crtc_t*)arg;
        if(!c)return 0;
        if(c->crtc_id&&c->crtc_id!=C4_DRM_ID_CRTC)return -EINVAL;
        c->crtc_id=C4_DRM_ID_CRTC;
        c->fb_id=C4_DRM_ID_FB;
        c->x=0;c->y=0;
        c->gamma_size=256;
        c->mode_valid=1;
        c4_drm_fill_mode(&c->mode);
        return 0;
    }
    if(request == DRM_IOCTL_MODE_GETENCODER) {
        c4_drm_mode_get_encoder_t *e=(c4_drm_mode_get_encoder_t*)arg;
        if(!e)return 0;
        if(e->encoder_id&&e->encoder_id!=C4_DRM_ID_ENCODER)return -EINVAL;
        e->encoder_id=C4_DRM_ID_ENCODER;
        e->encoder_type=1;
        e->crtc_id=C4_DRM_ID_CRTC;
        e->possible_crtcs=1;
        e->possible_clones=0;
        return 0;
    }
    if(request == DRM_IOCTL_MODE_GETCONNECTOR) {
        c4_drm_mode_get_connector_t *c=(c4_drm_mode_get_connector_t*)arg;
        uint32_t w=(g_fb.width?g_fb.width:1024u);
        uint32_t h=(g_fb.height?g_fb.height:768u);
        if(!c)return 0;
        if(c->connector_id&&c->connector_id!=C4_DRM_ID_CONNECTOR)return -EINVAL;
        if(c->encoders_ptr&&c->count_encoders>0)((uint32_t*)(uintptr_t)c->encoders_ptr)[0]=C4_DRM_ID_ENCODER;
        if(c->modes_ptr&&c->count_modes>0)c4_drm_fill_mode((c4_drm_mode_modeinfo_t*)(uintptr_t)c->modes_ptr);
        c->count_encoders=1;
        c->count_modes=1;
        c->count_props=0;
        c->encoder_id=C4_DRM_ID_ENCODER;
        c->connector_id=C4_DRM_ID_CONNECTOR;
        c->connector_type=1;
        c->connector_type_id=1;
        c->connection=1;
        c->mm_width=w/4u;
        c->mm_height=h/4u;
        c->subpixel=1;
        return 0;
    }
    if(request == DRM_IOCTL_MODE_CREATE_DUMB) {
        /* Return framebuffer as dumb buffer */
        if(arg) {
            c4_drm_mode_create_dumb_t *d=(c4_drm_mode_create_dumb_t*)arg;
            uint32_t w=(g_fb.width?g_fb.width:1024u);
            uint32_t h=(g_fb.height?g_fb.height:768u);
            uint32_t bpp=(d->bpp?d->bpp:32u);
            if(d->width)d->width=(d->width<w)?d->width:w;
            else d->width=w;
            if(d->height)d->height=(d->height<h)?d->height:h;
            else d->height=h;
            d->bpp=bpp;
            d->pitch=((d->width*bpp)+7u)/8u;
            d->size=(uint64_t)d->pitch*d->height;
            d->handle=1;
        }
        return 0;
    }
    if(request == DRM_IOCTL_MODE_MAP_DUMB) {
        if(arg) {
            c4_drm_mode_map_dumb_t *m=(c4_drm_mode_map_dumb_t*)arg;
            m->offset=(uint64_t)(uintptr_t)g_fb.address;
        }
        return 0;
    }
    if(request == DRM_IOCTL_MODE_DESTROY_DUMB) {
        return 0;
    }
    if(request == DRM_IOCTL_MODE_ADDFB) {
        if(arg){
            c4_drm_mode_fb_cmd_t *f=(c4_drm_mode_fb_cmd_t*)arg;
            (void)f->width;(void)f->height;(void)f->pitch;(void)f->bpp;(void)f->depth;(void)f->handle;
            f->fb_id=C4_DRM_ID_FB;
        }
        return 0;
    }
    if(request == DRM_IOCTL_MODE_SETCRTC) {
        if(arg){
            c4_drm_mode_crtc_t *s=(c4_drm_mode_crtc_t*)arg;
            if(s->crtc_id&&s->crtc_id!=C4_DRM_ID_CRTC)return -EINVAL;
            s->crtc_id=C4_DRM_ID_CRTC;
            if(!s->fb_id)s->fb_id=C4_DRM_ID_FB;
            s->mode_valid=1;
        }
        return 0;
    }
    return -ENOTTY;
}

int fb_ioctl_handler(int fd, uint64_t request, void *arg) {
    (void)fd;
    if(request == FBIOGET_VSCREENINFO) {
        fb_var_screeninfo_t *v = (fb_var_screeninfo_t*)arg;
        ulibc_memset(v, 0, sizeof(*v));
        v->xres = g_fb.width; v->yres = g_fb.height;
        v->xres_virtual = g_fb.width; v->yres_virtual = g_fb.height;
        v->bits_per_pixel = 32;
        v->red_offset = 16; v->red_length = 8;
        v->green_offset = 8; v->green_length = 8;
        v->blue_offset = 0; v->blue_length = 8;
        v->transp_offset = 24; v->transp_length = 8;
        return 0;
    }
    if(request == FBIOPUT_VSCREENINFO) return 0;
    if(request == FBIOGET_FSCREENINFO) {
        fb_fix_screeninfo_t *f = (fb_fix_screeninfo_t*)arg;
        ulibc_memset(f, 0, sizeof(*f));
        ulibc_strcpy(f->id, "ridux-fb");
        f->smem_start = (uint64_t)(uintptr_t)g_fb.address;
        f->smem_len = g_fb.width * g_fb.height * 4;
        f->type = 0; /* FB_TYPE_PACKED_PIXELS */
        f->visual = 2; /* FB_VISUAL_TRUECOLOR */
        f->line_length = g_fb.pitch;
        return 0;
    }
    return -ENOTTY;
}

/* Dynamic linker stub */
static uintptr_t g_stack_chk_guard = 0x9e3779b97f4a7c15ULL;
static void(*g_cxa_atexit_fns[64])(void*);
static void *g_cxa_atexit_args[64];
static int g_cxa_atexit_count = 0;

void ulibc___stack_chk_fail(void) { ulibc_abort(); }
int ulibc___cxa_atexit(void(*func)(void*), void *arg, void *dso_handle) {
    (void)dso_handle;
    if(!func) return 0;
    if(g_cxa_atexit_count >= (int)(sizeof(g_cxa_atexit_fns)/sizeof(g_cxa_atexit_fns[0]))) return -1;
    g_cxa_atexit_fns[g_cxa_atexit_count] = func;
    g_cxa_atexit_args[g_cxa_atexit_count] = arg;
    ++g_cxa_atexit_count;
    return 0;
}
void ulibc___cxa_finalize(void *dso_handle) {
    (void)dso_handle;
    while(g_cxa_atexit_count > 0) {
        --g_cxa_atexit_count;
        if(g_cxa_atexit_fns[g_cxa_atexit_count]) {
            g_cxa_atexit_fns[g_cxa_atexit_count](g_cxa_atexit_args[g_cxa_atexit_count]);
        }
    }
}

uint64_t ulibc_getauxval(uint64_t type) {
    task_t *cur = task_current();
    if(!cur) return 0;
    switch(type) {
        case AT_PAGESZ: return 4096;
        case AT_PHDR: return cur->aux_at_phdr;
        case AT_PHENT: return cur->aux_at_phent;
        case AT_PHNUM: return cur->aux_at_phnum;
        case AT_BASE: return cur->aux_at_base;
        case AT_FLAGS: return cur->aux_at_flags;
        case AT_ENTRY: return cur->aux_at_entry ? cur->aux_at_entry : cur->entry_point;
        case AT_UID: return cur->aux_at_uid;
        case AT_EUID: return cur->aux_at_euid;
        case AT_GID: return cur->aux_at_gid;
        case AT_EGID: return cur->aux_at_egid;
        case AT_PLATFORM: return cur->aux_at_platform;
        case AT_HWCAP: return 0;
        case AT_CLKTCK: return 100;
        case AT_SECURE: return 0;
        case AT_RANDOM: return cur->aux_at_random;
        case AT_HWCAP2: return 0;
        case AT_EXECFN: return cur->aux_at_execfn;
        case AT_SYSINFO_EHDR: return 0;
        default: return 0;
    }
}

void *ulibc___tls_get_addr(ulibc_tls_index_t *ti) {
    uint64_t module;
    uint64_t offset;
    uint64_t need;
    int tidx = g_current_task;
    ulibc_tls_desc_t *desc;
    ulibc_tls_mod_t *slot;
    if(!ti || tidx < 0 || tidx >= TASK_MAX) return 0;
    ulibc_tls_ensure_task_base(tidx);
    ulibc_tls_refresh_modules();
    module = ti->ti_module ? ti->ti_module : 1;
    offset = ti->ti_offset;
    if(module >= ULIBC_TLS_MODULE_MAX) return 0;
    desc = &g_tls_desc[module];
    slot = &g_tls_modules[tidx][module];
    need = offset + 1;
    if(desc->valid && desc->memsz > need) need = desc->memsz;
    if(!slot->block) {
        size_t alloc_sz = ULIBC_TLS_BLOCK_INIT;
        while((uint64_t)alloc_sz < need) alloc_sz <<= 1;
        slot->block = ulibc_malloc(alloc_sz);
        if(!slot->block) return 0;
        ulibc_memset(slot->block, 0, alloc_sz);
        if(desc->valid && desc->init && desc->filesz) {
            size_t copy_sz = (size_t)((desc->filesz < (uint64_t)alloc_sz) ? desc->filesz : (uint64_t)alloc_sz);
            ulibc_memcpy(slot->block, (const void*)(uintptr_t)desc->init, copy_sz);
        }
        slot->size = alloc_sz;
    } else if(need > slot->size) {
        size_t old_sz = slot->size;
        size_t new_sz = old_sz ? old_sz : ULIBC_TLS_BLOCK_INIT;
        void *nb;
        while((uint64_t)new_sz < need) new_sz <<= 1;
        nb = ulibc_realloc(slot->block, new_sz);
        if(!nb) return 0;
        slot->block = nb;
        slot->size = new_sz;
        ulibc_memset((uint8_t*)slot->block + old_sz, 0, new_sz - old_sz);
    }
    if(g_tls_tcb[tidx]) g_tls_tcb[tidx]->dtv = (void*)&g_tls_modules[tidx][0];
    return (uint8_t*)slot->block + offset;
}

int ulibc_dl_iterate_phdr(int(*cb)(ulibc_dl_phdr_info_t*, size_t, void*), void *data) {
    compat3_dlobj_info_t objs[64];
    int n, i;
    if(!cb) return 0;
    n = compat3_dynobj_snapshot(objs, (int)(sizeof(objs)/sizeof(objs[0])));
    for(i = 0; i < n; ++i) {
        ulibc_dl_phdr_info_t info;
        info.dlpi_addr = objs[i].dlpi_addr;
        info.dlpi_name = objs[i].dlpi_name;
        info.dlpi_phdr = (const ulibc_elf64_phdr_t*)(uintptr_t)objs[i].dlpi_phdr;
        info.dlpi_phnum = objs[i].dlpi_phnum;
        if(cb(&info, sizeof(info), data) != 0) return 1;
    }
    return 0;
}

/* Symbol table for pseudo-dlsym - maps standard C names to our ulibc fns */
static const ulibc_sym_entry_t g_dlsym_table[] = {
    {"memcpy",  (void*)ulibc_memcpy},  {"memmove", (void*)ulibc_memmove},
    {"memset",  (void*)ulibc_memset},  {"memcmp",  (void*)ulibc_memcmp},
    {"strlen",  (void*)ulibc_strlen},  {"strcpy",  (void*)ulibc_strcpy},
    {"strncpy", (void*)ulibc_strncpy}, {"strcmp",   (void*)ulibc_strcmp},
    {"strncmp", (void*)ulibc_strncmp}, {"strcat",  (void*)ulibc_strcat},
    {"strchr",  (void*)ulibc_strchr},  {"strrchr", (void*)ulibc_strrchr},
    {"strstr",  (void*)ulibc_strstr},  {"strdup",  (void*)ulibc_strdup},
    {"strtok",  (void*)ulibc_strtok},  {"strerror",(void*)ulibc_strerror},
    {"dlopen",  (void*)ulibc_dlopen},  {"dlsym",   (void*)ulibc_dlsym},
    {"dlclose", (void*)ulibc_dlclose}, {"dlerror", (void*)ulibc_dlerror},
    {"dl_iterate_phdr", (void*)ulibc_dl_iterate_phdr},
    {"malloc",  (void*)ulibc_malloc},  {"free",    (void*)ulibc_free},
    {"realloc", (void*)ulibc_realloc}, {"calloc",  (void*)ulibc_calloc},
    {"atoi",    (void*)ulibc_atoi},    {"atol",    (void*)ulibc_atol},
    {"strtol",  (void*)ulibc_strtol},  {"strtoul", (void*)ulibc_strtoul},
    {"qsort",   (void*)ulibc_qsort},   {"bsearch", (void*)ulibc_bsearch},
    {"abs",     (void*)ulibc_abs},     {"rand",    (void*)ulibc_rand},
    {"srand",   (void*)ulibc_srand},   {"getenv",  (void*)ulibc_getenv},
    {"exit",    (void*)ulibc_exit},    {"abort",   (void*)ulibc_abort},
    {"printf",  (void*)ulibc_printf},  {"fprintf", (void*)ulibc_fprintf},
    {"sprintf", (void*)ulibc_sprintf}, {"snprintf",(void*)ulibc_snprintf},
    {"puts",    (void*)ulibc_puts},    {"fputs",   (void*)ulibc_fputs},
    {"fgets",   (void*)ulibc_fgets},   {"fopen",   (void*)ulibc_fopen},
    {"fclose",  (void*)ulibc_fclose},  {"fread",   (void*)ulibc_fread},
    {"fwrite",  (void*)ulibc_fwrite},  {"fseek",   (void*)ulibc_fseek},
    {"ftell",   (void*)ulibc_ftell},   {"fflush",  (void*)ulibc_fflush},
    {"read",    (void*)ulibc_read},    {"write",   (void*)ulibc_write},
    {"close",   (void*)ulibc_close},   {"open",    (void*)real_sys_open},
    {"fork",    (void*)ulibc_fork},    {"pipe",    (void*)ulibc_pipe},
    {"dup",     (void*)ulibc_dup},     {"dup2",    (void*)ulibc_dup2},
    {"syscall", (void*)ulibc_syscall}, {"sched_yield",(void*)ulibc_sched_yield},
    {"gettid",  (void*)ulibc_gettid},
    {"sleep",   (void*)ulibc_sleep},   {"usleep",  (void*)ulibc_usleep},
    {"getpid",  (void*)ulibc_getpid},  {"getcwd",  (void*)ulibc_getcwd},
    {"chdir",   (void*)ulibc_chdir},   {"access",  (void*)ulibc_access},
    {"isatty",  (void*)ulibc_isatty},
    {"isalpha", (void*)ulibc_isalpha}, {"isdigit", (void*)ulibc_isdigit},
    {"isspace", (void*)ulibc_isspace}, {"toupper", (void*)ulibc_toupper},
    {"tolower", (void*)ulibc_tolower}, {"isprint", (void*)ulibc_isprint},
    {"pthread_create",       (void*)ulibc_pthread_create},
    {"pthread_join",         (void*)ulibc_pthread_join},
    {"pthread_detach",       (void*)ulibc_pthread_detach},
    {"pthread_self",         (void*)ulibc_pthread_self},
    {"pthread_mutexattr_init",(void*)ulibc_pthread_mutexattr_init},
    {"pthread_mutexattr_destroy",(void*)ulibc_pthread_mutexattr_destroy},
    {"pthread_mutexattr_settype",(void*)ulibc_pthread_mutexattr_settype},
    {"pthread_mutexattr_gettype",(void*)ulibc_pthread_mutexattr_gettype},
    {"pthread_mutex_init",   (void*)ulibc_pthread_mutex_init},
    {"pthread_mutex_lock",   (void*)ulibc_pthread_mutex_lock},
    {"pthread_mutex_trylock",(void*)ulibc_pthread_mutex_trylock},
    {"pthread_mutex_unlock", (void*)ulibc_pthread_mutex_unlock},
    {"pthread_mutex_destroy",(void*)ulibc_pthread_mutex_destroy},
    {"pthread_condattr_init",(void*)ulibc_pthread_condattr_init},
    {"pthread_condattr_destroy",(void*)ulibc_pthread_condattr_destroy},
    {"pthread_condattr_setclock",(void*)ulibc_pthread_condattr_setclock},
    {"pthread_condattr_getclock",(void*)ulibc_pthread_condattr_getclock},
    {"pthread_cond_init",    (void*)ulibc_pthread_cond_init},
    {"pthread_cond_wait",    (void*)ulibc_pthread_cond_wait},
    {"pthread_cond_timedwait",(void*)ulibc_pthread_cond_timedwait},
    {"pthread_cond_signal",  (void*)ulibc_pthread_cond_signal},
    {"pthread_cond_broadcast",(void*)ulibc_pthread_cond_broadcast},
    {"pthread_key_create",   (void*)ulibc_pthread_key_create},
    {"pthread_key_delete",   (void*)ulibc_pthread_key_delete},
    {"pthread_getspecific",  (void*)ulibc_pthread_getspecific},
    {"pthread_setspecific",  (void*)ulibc_pthread_setspecific},
    {"pthread_once",         (void*)ulibc_pthread_once},
    {"__tls_get_addr",       (void*)ulibc___tls_get_addr},
    {"__errno_location",     (void*)__errno_location},
    {"__stack_chk_fail",     (void*)ulibc___stack_chk_fail},
    {"__stack_chk_guard",    (void*)&g_stack_chk_guard},
    {"__cxa_atexit",         (void*)ulibc___cxa_atexit},
    {"__cxa_finalize",       (void*)ulibc___cxa_finalize},
    {"getauxval",            (void*)ulibc_getauxval},
    {"mmap",    (void*)real_sys_mmap},
    {"munmap",  (void*)real_sys_munmap},
    {"brk",     (void*)real_sys_brk},
    {"clone",   (void*)real_sys_clone},
    {"set_tid_address",(void*)real_sys_set_tid_address},
    {"set_robust_list",(void*)real_sys_set_robust_list},
    {"get_robust_list",(void*)real_sys_get_robust_list},
    {"futex",   (void*)real_sys_futex},
    {"clock_gettime", (void*)real_sys_clock_gettime},
    {"gettimeofday",  (void*)real_sys_gettimeofday},
    {"nanosleep",     (void*)real_sys_nanosleep},
    {"getrandom",     (void*)real_sys_getrandom},
    {"uname",         (void*)real_sys_uname},
    {0, 0}
};

static char g_dlerror_buf[128] = {0};

void *ulibc_dlopen(const char *filename, int flags) {
    (void)filename; (void)flags;
    return (void*)1; /* Always succeed - we're a monolithic kernel */
}

void *ulibc_dlsym(void *handle, const char *symbol) {
    const ulibc_sym_entry_t *e;
    (void)handle;
    for(e = g_dlsym_table; e->name; ++e) {
        if(ulibc_strcmp(e->name, symbol) == 0) return e->addr;
    }
    {
        uint64_t gsym = compat3_lookup_global_symbol(symbol);
        if(gsym) return (void*)(uintptr_t)gsym;
    }
    ulibc_snprintf(g_dlerror_buf, sizeof(g_dlerror_buf), "undefined symbol: %s", symbol);
    return 0;
}

int ulibc_dlclose(void *handle) { (void)handle; return 0; }
char *ulibc_dlerror(void) { return g_dlerror_buf[0] ? g_dlerror_buf : 0; }

/* Shell commands */
static void cmd4_libc_info(const char *a, char *o, int mx) {
    (void)a;
    ulibc_snprintf(o, (size_t)mx,
        "=== RiduxOS Micro-libc (compat4) ===\n"
        "string.h: memcpy/memmove/memset/memcmp/strlen/strcpy/strcmp/strcat/strchr/strrchr/strstr/strdup/strtok/strerror +16 more\n"
        "stdlib.h: malloc/free/realloc/calloc (16MB heap), atoi/strtol/strtoul/qsort/bsearch/abs/rand/getenv/setenv/exit/abort\n"
        "stdio.h:  FILE*/fopen/fclose/fread/fwrite/fgetc/fputc/fgets/fputs/fprintf/printf/sprintf/snprintf/vsnprintf\n"
        "ctype.h:  isalpha/isdigit/isalnum/isspace/toupper/tolower +10 more\n"
        "unistd.h: read/write/close/dup/dup2/pipe/fork/execve/sleep/usleep/chdir/getcwd/getpid/access/isatty\n"
        "pthread:  create/join/detach/self/mutex(init/lock/unlock)/cond(init/wait/signal)/rwlock/key(TLS)/once\n"
        "dlopen:   dlopen/dlsym/dlclose/dlerror (%d symbols in table)\n"
        "DRM:      VERSION/GET_CAP/GETRES/GETCRTC/GETCONNECTOR/GETENCODER/ADDFB/SETCRTC/CREATE_DUMB/MAP_DUMB\n"
        "FB:       FBIOGET_VSCREENINFO/FBIOGET_FSCREENINFO (real resolution from VESA)\n"
        "Env:      %d vars (HOME,PATH,TERM,DISPLAY,LANG,XDG_*,etc)\n"
        "Heap:     %u/%u bytes used\n",
        (int)(sizeof(g_dlsym_table)/sizeof(g_dlsym_table[0])-1),
        g_env_count,
        (unsigned)g_heap_used, ULIBC_HEAP_SIZE);
}

static void cmd4_env(const char *a, char *o, int mx) {
    size_t l = 0; int i; (void)a;
    if(!g_env_init) env_init();
    o[0] = 0;
    for(i = 0; i < g_env_count; ++i) {
        size_t elen = ulibc_strlen(g_environ[i]);
        if(l + elen + 2 >= (size_t)mx) break;
        ulibc_memcpy(o + l, g_environ[i], elen);
        l += elen; o[l++] = '\n'; o[l] = 0;
    }
}

static void cmd4_dlsym(const char *a, char *o, int mx) {
    if(!a || !*a) {
        ulibc_snprintf(o, (size_t)mx, "usage: dlsym <symbol_name>\n  Lists address of symbol in micro-libc.\n");
        return;
    }
    void *sym = ulibc_dlsym((void*)1, a);
    if(sym) ulibc_snprintf(o, (size_t)mx, "%s = %p\n", a, sym);
    else ulibc_snprintf(o, (size_t)mx, "%s: not found\n", a);
}

static void cmd4_heap(const char *a, char *o, int mx) {
    (void)a;
    ulibc_snprintf(o, (size_t)mx,
        "Heap: %u / %u bytes used (%u KB / %u KB)\nFree list: %s\n",
        (unsigned)g_heap_used, ULIBC_HEAP_SIZE,
        (unsigned)(g_heap_used/1024), ULIBC_HEAP_SIZE/1024,
        g_free_list ? "active" : "empty");
}

void compat4_register_shell_cmds(void) {
    extern compat_shell_cmd_t g_compat_cmds[];
    extern int g_compat_cmd_count;
    #define REG4(n,h,fn) if(g_compat_cmd_count<COMPAT_SHELL_CMD_MAX){g_compat_cmds[g_compat_cmd_count].name=n;g_compat_cmds[g_compat_cmd_count].help=h;g_compat_cmds[g_compat_cmd_count].handler=fn;++g_compat_cmd_count;}
    REG4("libc","Micro-libc info",cmd4_libc_info)
    REG4("env","Show environment variables",cmd4_env)
    REG4("dlsym","Lookup symbol in micro-libc",cmd4_dlsym)
    REG4("heap","Show heap status",cmd4_heap)
    #undef REG4
}

/* Master init */
void compat4_init_all(void) {
    uint64_t seed = ((uint64_t)real_sys_getpid() << 32) ^ (uint64_t)g_heap_used ^ 0xA5A5A5A55A5A5A5AULL;
    heap_init();
    env_init();
    ulibc_memset(g_stdio_files, 0, sizeof(g_stdio_files));
    ulibc_memset(g_tls_values, 0, sizeof(g_tls_values));
    ulibc_memset(g_tls_destructors, 0, sizeof(g_tls_destructors));
    ulibc_memset(g_tls_modules, 0, sizeof(g_tls_modules));
    ulibc_memset(g_tls_tcb, 0, sizeof(g_tls_tcb));
    ulibc_memset(g_cxa_atexit_fns, 0, sizeof(g_cxa_atexit_fns));
    ulibc_memset(g_cxa_atexit_args, 0, sizeof(g_cxa_atexit_args));
    g_cxa_atexit_count = 0;
    g_stack_chk_guard ^= (uintptr_t)seed;
    ulibc_tls_refresh_modules();
    ulibc_tls_ensure_task_base(g_current_task);
    compat4_register_shell_cmds();
}
