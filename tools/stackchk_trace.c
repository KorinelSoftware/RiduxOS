// Tiny Linux/ELF preload used while bringing up real Chromium.
// It reports the call site that hit glibc's stack protector, then exits.
#include <stdint.h>
#include <stddef.h>

static long ridux_sys3(long nr, long a0, long a1, long a2) {
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(nr), "D"(a0), "S"(a1), "d"(a2)
                     : "rcx", "r11", "memory");
    return ret;
}

static void ridux_write(const char *s, size_t n) {
    (void)ridux_sys3(1, 2, (long)s, (long)n);
}

static void ridux_puts(const char *s) {
    size_t n = 0;
    while (s[n]) ++n;
    ridux_write(s, n);
}

static void ridux_hex64(uint64_t v) {
    char out[18];
    static const char h[] = "0123456789abcdef";
    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < 16; ++i) {
        out[2 + i] = h[(v >> (60 - 4 * i)) & 0xf];
    }
    ridux_write(out, sizeof(out));
}

__attribute__((noreturn, no_stack_protector))
void __stack_chk_fail(void) {
    void *ra = __builtin_return_address(0);
    ridux_puts("[ridux-stackchk] __stack_chk_fail caller=");
    ridux_hex64((uint64_t)(uintptr_t)ra);
    ridux_puts("\n");
    (void)ridux_sys3(231, 127, 0, 0);
    for (;;) {}
}

__attribute__((noreturn, no_stack_protector))
void __stack_chk_fail_local(void) {
    __stack_chk_fail();
}

__attribute__((noreturn, no_stack_protector))
void __fortify_fail(const char *msg) {
    void *ra = __builtin_return_address(0);
    ridux_puts("[ridux-stackchk] __fortify_fail caller=");
    ridux_hex64((uint64_t)(uintptr_t)ra);
    ridux_puts(" msg=");
    ridux_puts(msg ? msg : "(null)");
    ridux_puts("\n");
    (void)ridux_sys3(231, 127, 0, 0);
    for (;;) {}
}

__attribute__((noreturn, no_stack_protector))
void abort(void) {
    void *ra0 = __builtin_return_address(0);
    void *ra1 = __builtin_return_address(1);
    void *ra2 = __builtin_return_address(2);
    void *ra3 = __builtin_return_address(3);
    void *ra4 = __builtin_return_address(4);
    void *ra5 = __builtin_return_address(5);
    void *ra6 = __builtin_return_address(6);
    void *ra7 = __builtin_return_address(7);
    ridux_puts("[ridux-abort] caller0=");
    ridux_hex64((uint64_t)(uintptr_t)ra0);
    ridux_puts(" caller1=");
    ridux_hex64((uint64_t)(uintptr_t)ra1);
    ridux_puts(" caller2=");
    ridux_hex64((uint64_t)(uintptr_t)ra2);
    ridux_puts(" caller3=");
    ridux_hex64((uint64_t)(uintptr_t)ra3);
    ridux_puts(" caller4=");
    ridux_hex64((uint64_t)(uintptr_t)ra4);
    ridux_puts(" caller5=");
    ridux_hex64((uint64_t)(uintptr_t)ra5);
    ridux_puts(" caller6=");
    ridux_hex64((uint64_t)(uintptr_t)ra6);
    ridux_puts(" caller7=");
    ridux_hex64((uint64_t)(uintptr_t)ra7);
    ridux_puts("\n");
    (void)ridux_sys3(231, 134, 0, 0);
    for (;;) {}
}

__attribute__((no_stack_protector))
const char *g_strerror(int errnum) {
    /*
     * Chromium is currently aborting inside GLib's g_strerror() while it is
     * formatting a non-fatal startup error.  Keep this deliberately tiny and
     * libc-free so the browser can get past that path while the native ABI is
     * still being completed.
     */
    static unsigned seen;
    if (seen < 32) {
        ++seen;
        ridux_puts("[ridux-compat] g_strerror bypass err=");
        if (errnum < 0) {
            ridux_puts("-");
            errnum = -errnum;
        }
        char buf[12];
        unsigned v = (unsigned)errnum;
        unsigned n = 0;
        do {
            buf[n++] = (char)('0' + (v % 10));
            v /= 10;
        } while (v && n < sizeof(buf));
        while (n) ridux_write(&buf[--n], 1);
        ridux_puts("\n");
    }
    return "Ridux Linux compatibility error";
}
