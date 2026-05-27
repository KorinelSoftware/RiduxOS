#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RIDUX_FG_SLOTS 262144u
#define RIDUX_FG_DELETED ((void *)(uintptr_t)1u)

static void *g_ptrs[RIDUX_FG_SLOTS];
static size_t g_sizes[RIDUX_FG_SLOTS];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned g_ignored;
static unsigned g_leak_note;

static void *(*real_malloc_fn)(size_t);
static void *(*real_calloc_fn)(size_t, size_t);
static void *(*real_realloc_fn)(void *, size_t);
static void (*real_free_fn)(void *);
static int (*real_posix_memalign_fn)(void **, size_t, size_t);
static void *(*real_aligned_alloc_fn)(size_t, size_t);
static void *(*real_memalign_fn)(size_t, size_t);
static int g_resolving;
static int g_resolved;

extern void *__libc_malloc(size_t size);
extern void *__libc_calloc(size_t nmemb, size_t size);
extern void *__libc_realloc(void *ptr, size_t size);
extern void __libc_free(void *ptr);

static void ridux_fg_init(void) {
    void *sym;
    if (!real_malloc_fn) real_malloc_fn = __libc_malloc;
    if (!real_calloc_fn) real_calloc_fn = __libc_calloc;
    if (!real_realloc_fn) real_realloc_fn = __libc_realloc;
    if (!real_free_fn) real_free_fn = __libc_free;
    if (g_resolved || g_resolving) return;
    g_resolving = 1;
    sym = dlsym(RTLD_NEXT, "malloc");
    if (sym) real_malloc_fn = (void *(*)(size_t))sym;
    sym = dlsym(RTLD_NEXT, "calloc");
    if (sym) real_calloc_fn = (void *(*)(size_t, size_t))sym;
    sym = dlsym(RTLD_NEXT, "realloc");
    if (sym) real_realloc_fn = (void *(*)(void *, size_t))sym;
    sym = dlsym(RTLD_NEXT, "free");
    if (sym) real_free_fn = (void (*)(void *))sym;
    real_posix_memalign_fn = (int (*)(void **, size_t, size_t))dlsym(RTLD_NEXT, "posix_memalign");
    real_aligned_alloc_fn = (void *(*)(size_t, size_t))dlsym(RTLD_NEXT, "aligned_alloc");
    real_memalign_fn = (void *(*)(size_t, size_t))dlsym(RTLD_NEXT, "memalign");
    g_resolved = 1;
    g_resolving = 0;
}

static unsigned ridux_fg_hash(void *ptr) {
    uintptr_t v = (uintptr_t)ptr;
    v ^= v >> 17;
    v *= (uintptr_t)0xed5ad4bbU;
    v ^= v >> 11;
    return (unsigned)(v & (RIDUX_FG_SLOTS - 1u));
}

static int ridux_fg_leak_free(void) {
    const char *value = getenv("RIDUX_QT_LEAK_FREE");
    return value && strcmp(value, "0") != 0 && strcmp(value, "false") != 0;
}

static void ridux_fg_track(void *ptr, size_t size) {
    unsigned i;
    unsigned h;
    unsigned first_deleted = RIDUX_FG_SLOTS;
    if (!ptr) return;
    pthread_mutex_lock(&g_lock);
    h = ridux_fg_hash(ptr);
    for (i = 0; i < RIDUX_FG_SLOTS; ++i) {
        unsigned idx = (h + i) & (RIDUX_FG_SLOTS - 1u);
        if (g_ptrs[idx] == RIDUX_FG_DELETED) {
            if (first_deleted == RIDUX_FG_SLOTS) first_deleted = idx;
            continue;
        }
        if (!g_ptrs[idx]) {
            if (first_deleted != RIDUX_FG_SLOTS) idx = first_deleted;
            g_ptrs[idx] = ptr;
            g_sizes[idx] = size;
            break;
        }
        if (g_ptrs[idx] == ptr) {
            g_ptrs[idx] = ptr;
            g_sizes[idx] = size;
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
}

static int ridux_fg_untrack(void *ptr, size_t *size_out) {
    unsigned i;
    unsigned h;
    int found = 0;
    if (!ptr) return 1;
    pthread_mutex_lock(&g_lock);
    h = ridux_fg_hash(ptr);
    for (i = 0; i < RIDUX_FG_SLOTS; ++i) {
        unsigned idx = (h + i) & (RIDUX_FG_SLOTS - 1u);
        if (g_ptrs[idx] == RIDUX_FG_DELETED) continue;
        if (!g_ptrs[idx]) break;
        if (g_ptrs[idx] == ptr) {
            if (size_out) *size_out = g_sizes[idx];
            g_ptrs[idx] = RIDUX_FG_DELETED;
            g_sizes[idx] = 0;
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return found;
}

static size_t ridux_fg_size(void *ptr) {
    unsigned i;
    unsigned h;
    size_t size = 0;
    if (!ptr) return 0;
    pthread_mutex_lock(&g_lock);
    h = ridux_fg_hash(ptr);
    for (i = 0; i < RIDUX_FG_SLOTS; ++i) {
        unsigned idx = (h + i) & (RIDUX_FG_SLOTS - 1u);
        if (g_ptrs[idx] == RIDUX_FG_DELETED) continue;
        if (!g_ptrs[idx]) break;
        if (g_ptrs[idx] == ptr) {
            size = g_sizes[idx];
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return size;
}

void *malloc(size_t size) {
    void *ptr;
    ridux_fg_init();
    ptr = real_malloc_fn ? real_malloc_fn(size) : NULL;
    ridux_fg_track(ptr, size);
    return ptr;
}

void *calloc(size_t nmemb, size_t size) {
    void *ptr;
    ridux_fg_init();
    ptr = real_calloc_fn ? real_calloc_fn(nmemb, size) : NULL;
    ridux_fg_track(ptr, nmemb * size);
    return ptr;
}

void free(void *ptr) {
    ridux_fg_init();
    if (!ptr) return;
    if (ridux_fg_leak_free()) {
        if (g_leak_note < 3) {
            ++g_leak_note;
            fprintf(stderr, "ridux-qt-freeguard: leak-free mode kept ptr=%p\n", ptr);
        }
        return;
    }
    if (!ridux_fg_untrack(ptr, NULL)) {
        if (g_ignored < 8) {
            ++g_ignored;
            fprintf(stderr, "ridux-qt-freeguard: ignored foreign free ptr=%p\n", ptr);
        }
        return;
    }
    if (real_free_fn) real_free_fn(ptr);
}

void *realloc(void *ptr, size_t size) {
    void *next;
    size_t old_size = 0;
    ridux_fg_init();
    if (ptr && ridux_fg_leak_free()) {
        old_size = ridux_fg_size(ptr);
        next = real_malloc_fn ? real_malloc_fn(size) : NULL;
        if (next && old_size) memcpy(next, ptr, old_size < size ? old_size : size);
        ridux_fg_track(next, size);
        return next;
    }
    if (ptr && !ridux_fg_untrack(ptr, NULL)) {
        if (g_ignored < 8) {
            ++g_ignored;
            fprintf(stderr, "ridux-qt-freeguard: ignored foreign realloc ptr=%p\n", ptr);
        }
        ptr = NULL;
    }
    next = real_realloc_fn ? real_realloc_fn(ptr, size) : NULL;
    ridux_fg_track(next, size);
    return next;
}

int posix_memalign(void **memptr, size_t alignment, size_t size) {
    int rc;
    ridux_fg_init();
    if (!real_posix_memalign_fn) return 12;
    rc = real_posix_memalign_fn(memptr, alignment, size);
    if (rc == 0) ridux_fg_track(*memptr, size);
    return rc;
}

void *aligned_alloc(size_t alignment, size_t size) {
    void *ptr;
    ridux_fg_init();
    ptr = real_aligned_alloc_fn ? real_aligned_alloc_fn(alignment, size) : NULL;
    ridux_fg_track(ptr, size);
    return ptr;
}

void *memalign(size_t alignment, size_t size) {
    void *ptr = NULL;
    ridux_fg_init();
    if (real_memalign_fn) {
        ptr = real_memalign_fn(alignment, size);
    } else if (real_posix_memalign_fn &&
               real_posix_memalign_fn(&ptr, alignment, size) != 0) {
        ptr = NULL;
    }
    ridux_fg_track(ptr, size);
    return ptr;
}

void *valloc(size_t size) {
    return memalign(4096u, size);
}

void *pvalloc(size_t size) {
    const size_t page = 4096u;
    size_t rounded;
    if (size > ((size_t)-1) - (page - 1u)) return NULL;
    rounded = (size + page - 1u) & ~(page - 1u);
    return memalign(page, rounded);
}

void *reallocarray(void *ptr, size_t nmemb, size_t size) {
    if (size && nmemb > ((size_t)-1) / size) return NULL;
    return realloc(ptr, nmemb * size);
}

char *strdup(const char *s) {
    size_t n;
    char *ptr;
    n = strlen(s) + 1u;
    ptr = (char *)malloc(n);
    if (ptr) memcpy(ptr, s, n);
    return ptr;
}
