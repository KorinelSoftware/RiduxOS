/*
 * Ridux shim for FreeBSD <sys/malloc.h>.
 *
 * Linuxulator code does `malloc(size, M_LINUX, M_WAITOK | M_ZERO)`.
 * We route this to Ridux's existing kernel allocator and synthesise
 * M_LINUX as an opaque tag (no per-tag accounting yet).
 */
#ifndef _RIDUX_SHIM_SYS_MALLOC_H_
#define _RIDUX_SHIM_SYS_MALLOC_H_

#include <sys/param.h>

/* malloc() flags. Linuxulator typically uses M_WAITOK | M_ZERO so
 * we honour the M_ZERO bit in our wrapper and ignore the rest. */
#define M_WAITOK    0x0002
#define M_NOWAIT    0x0001
#define M_ZERO      0x0100
#define M_NODUMP    0x0800

/* MALLOC_DEFINE(tag, name, descr): real FreeBSD records statistics per
 * tag. We expand to a `static` placeholder so the symbol is unique per
 * compilation unit but never referenced. */
struct malloc_type {
    const char *ks_shortdesc;
};
#define MALLOC_DEFINE(tag, shortdesc, longdesc) \
    static struct malloc_type tag##_storage = { shortdesc }; \
    struct malloc_type *const tag = &tag##_storage
#define MALLOC_DECLARE(tag) \
    extern struct malloc_type *const tag

/* M_LINUX is referenced by emul/signal/futex/socket. Provide it as a
 * single shared tag implemented in ridux_freebsd_glue.c. */
extern struct malloc_type *const M_LINUX;
extern struct malloc_type *const M_TEMP;

/* Allocator entry points wired in glue layer. */
void *ridux_freebsd_malloc(size_t size, struct malloc_type *type, int flags);
void  ridux_freebsd_free(void *ptr, struct malloc_type *type);

#define malloc(size, type, flags)   ridux_freebsd_malloc((size), (type), (flags))
#define free(ptr, type)             ridux_freebsd_free((ptr), (type))
#define mallocarray(n, sz, type, flags) ridux_freebsd_malloc((n)*(sz), (type), (flags))

#endif /* _RIDUX_SHIM_SYS_MALLOC_H_ */
