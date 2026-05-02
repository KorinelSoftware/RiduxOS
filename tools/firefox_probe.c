// Tiny Firefox preload probe for Ridux browser bring-up.
// Logs dependentlibs parsing and dlopen/dlsym decisions without libc stdio.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

static long rp_sys3(long nr, long a0, long a1, long a2) {
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(nr), "D"(a0), "S"(a1), "d"(a2)
                     : "rcx", "r11", "memory");
    return ret;
}

static size_t rp_len(const char *s) {
    size_t n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
}

static int rp_eq(const char *a, const char *b) {
    size_t i = 0;
    if (!a || !b) return 0;
    for (;;) {
        if (a[i] != b[i]) return 0;
        if (!a[i]) return 1;
        ++i;
    }
}

static int rp_has(const char *s, const char *needle) {
    size_t i, j;
    if (!s || !needle || !needle[0]) return 0;
    for (i = 0; s[i]; ++i) {
        for (j = 0; needle[j] && s[i + j] == needle[j]; ++j) {}
        if (!needle[j]) return 1;
    }
    return 0;
}

static void rp_write(const char *s, size_t n) {
    if (s && n) (void)rp_sys3(1, 2, (long)s, (long)n);
}

static void rp_puts(const char *s) {
    rp_write(s, rp_len(s));
}

static void rp_hex(uint64_t v) {
    char out[18];
    static const char h[] = "0123456789abcdef";
    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < 16; ++i) out[2 + i] = h[(v >> (60 - 4 * i)) & 15];
    rp_write(out, sizeof(out));
}

static void rp_line_sample(const char *s) {
    char out[181];
    size_t n = 0;
    if (!s) {
        rp_puts("(null)");
        return;
    }
    while (s[n] && n < sizeof(out)) {
        char ch = s[n];
        if (ch == '\n') ch = '|';
        if (ch == '\r' || ch == '\t') ch = ' ';
        if (ch < ' ' || ch > '~') ch = '.';
        out[n++] = ch;
    }
    rp_write(out, n);
}

static void *rp_dlvsym_next(const char *name, const char *ver) {
    typedef void *(*dlvsym_fn)(void *, const char *, const char *);
    static dlvsym_fn real_dlvsym;
    if (!real_dlvsym) {
        real_dlvsym = (dlvsym_fn)dlvsym(RTLD_NEXT, "dlvsym", "GLIBC_2.2.5");
    }
    return real_dlvsym ? real_dlvsym(RTLD_NEXT, name, ver) : 0;
}

static void *rp_resolve(const char *name) {
    void *p = rp_dlvsym_next(name, "GLIBC_2.34");
    if (!p) p = rp_dlvsym_next(name, "GLIBC_2.2.5");
    return p;
}

char *fgets(char *s, int size, FILE *stream) {
    typedef char *(*real_fgets_t)(char *, int, FILE *);
    static real_fgets_t real_fgets;
    static unsigned seen;
    char *ret;
    if (!real_fgets) real_fgets = (real_fgets_t)rp_resolve("fgets");
    ret = real_fgets ? real_fgets(s, size, stream) : 0;
    if (seen < 16 && ret && (rp_has(ret, ".so") || rp_has(ret, "lib"))) {
        ++seen;
        rp_puts("[ffprobe] fgets ");
        rp_line_sample(ret);
        rp_puts("\n");
    }
    return ret;
}

void *dlopen(const char *filename, int flags) {
    typedef void *(*real_dlopen_t)(const char *, int);
    static real_dlopen_t real_dlopen;
    static unsigned seen;
    void *ret;
    if (!real_dlopen) real_dlopen = (real_dlopen_t)rp_resolve("dlopen");
    if (seen < 48 && filename && (rp_has(filename, "/opt/firefox/") || rp_has(filename, "libxul") || rp_has(filename, "libnspr"))) {
        ++seen;
        rp_puts("[ffprobe] dlopen enter file=");
        rp_line_sample(filename);
        rp_puts(" flags=");
        rp_hex((uint64_t)(uint32_t)flags);
        rp_puts("\n");
    }
    ret = real_dlopen ? real_dlopen(filename, flags) : 0;
    if (seen < 48 && filename && (rp_has(filename, "/opt/firefox/") || rp_has(filename, "libxul") || rp_has(filename, "libnspr"))) {
        rp_puts("[ffprobe] dlopen exit file=");
        rp_line_sample(filename);
        rp_puts(" handle=");
        rp_hex((uint64_t)(uintptr_t)ret);
        rp_puts("\n");
    }
    return ret;
}

void *dlsym(void *handle, const char *symbol) {
    typedef void *(*real_dlsym_t)(void *, const char *);
    static real_dlsym_t real_dlsym;
    void *ret;
    if (!real_dlsym) real_dlsym = (real_dlsym_t)rp_resolve("dlsym");
    ret = real_dlsym ? real_dlsym(handle, symbol) : 0;
    if (symbol && (rp_eq(symbol, "XRE_GetBootstrap") || rp_has(symbol, "Bootstrap"))) {
        rp_puts("[ffprobe] dlsym symbol=");
        rp_line_sample(symbol);
        rp_puts(" handle=");
        rp_hex((uint64_t)(uintptr_t)handle);
        rp_puts(" result=");
        rp_hex((uint64_t)(uintptr_t)ret);
        rp_puts("\n");
    }
    return ret;
}
