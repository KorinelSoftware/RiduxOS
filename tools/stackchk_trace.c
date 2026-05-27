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

typedef unsigned int pthread_key_t;

#define RIDUX_TSD_KEYS 1024
#define RIDUX_TSD_THREADS 160

static volatile int ridux_tsd_lock;
static unsigned ridux_tsd_next_key;
static long ridux_tsd_tid[RIDUX_TSD_THREADS];
static void *ridux_tsd_values[RIDUX_TSD_THREADS][RIDUX_TSD_KEYS];

static void ridux_lock(volatile int *lock) {
    while (__sync_lock_test_and_set(lock, 1)) {
        __asm__ volatile("pause");
    }
}

static void ridux_unlock(volatile int *lock) {
    __sync_lock_release(lock);
}

static long ridux_gettid(void) {
    return ridux_sys3(186, 0, 0, 0);
}

static int ridux_tsd_slot_locked(long tid) {
    int free_slot = -1;
    for (int i = 0; i < RIDUX_TSD_THREADS; ++i) {
        if (ridux_tsd_tid[i] == tid) return i;
        if (ridux_tsd_tid[i] == 0 && free_slot < 0) free_slot = i;
    }
    if (free_slot >= 0) {
        ridux_tsd_tid[free_slot] = tid;
        return free_slot;
    }
    return 0;
}

__attribute__((no_stack_protector))
int pthread_key_create(pthread_key_t *key, void (*destructor)(void *)) {
    unsigned k;
    (void)destructor;
    if (!key) return 22;
    ridux_lock(&ridux_tsd_lock);
    k = ridux_tsd_next_key++;
    ridux_unlock(&ridux_tsd_lock);
    if (k >= RIDUX_TSD_KEYS) return 11;
    *key = (pthread_key_t)k;
    return 0;
}

__attribute__((no_stack_protector))
int pthread_key_delete(pthread_key_t key) {
    if (key >= RIDUX_TSD_KEYS) return 22;
    ridux_lock(&ridux_tsd_lock);
    for (int i = 0; i < RIDUX_TSD_THREADS; ++i) {
        ridux_tsd_values[i][key] = 0;
    }
    ridux_unlock(&ridux_tsd_lock);
    return 0;
}

__attribute__((no_stack_protector))
void *pthread_getspecific(pthread_key_t key) {
    void *value;
    int slot;
    if (key >= RIDUX_TSD_KEYS) return 0;
    ridux_lock(&ridux_tsd_lock);
    slot = ridux_tsd_slot_locked(ridux_gettid());
    value = ridux_tsd_values[slot][key];
    ridux_unlock(&ridux_tsd_lock);
    return value;
}

__attribute__((no_stack_protector))
int pthread_setspecific(pthread_key_t key, const void *value) {
    int slot;
    if (key >= RIDUX_TSD_KEYS) return 22;
    ridux_lock(&ridux_tsd_lock);
    slot = ridux_tsd_slot_locked(ridux_gettid());
    ridux_tsd_values[slot][key] = (void *)value;
    ridux_unlock(&ridux_tsd_lock);
    return 0;
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
