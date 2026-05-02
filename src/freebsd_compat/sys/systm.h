/*
 * Ridux shim for FreeBSD <sys/systm.h>.
 *
 * Real FreeBSD systm.h declares hundreds of kernel-internal helpers
 * (printf, panic, malloc, kproc_create, ...). We provide only what the
 * Linuxulator code we import actually references.
 *
 * KASSERT is the most common one. In FreeBSD it's enabled only when
 * the kernel is built with the INVARIANTS option; otherwise it's
 * compiled out. We compile it out unconditionally because Ridux does
 * not yet provide the panic/abort plumbing those assertions need.
 */
#ifndef _RIDUX_SHIM_SYS_SYSTM_H_
#define _RIDUX_SHIM_SYS_SYSTM_H_

#include <sys/param.h>
#include <sys/errno.h>     /* most Linuxulator files reach errno via systm.h */

/* Compile-out KASSERT. (void)(cond) keeps the expression evaluated
 * for side-effect compatibility but generates no code. */
#ifndef KASSERT
# define KASSERT(cond, msg) do { (void)(cond); } while (0)
#endif

#ifndef MPASS
# define MPASS(cond) do { (void)(cond); } while (0)
#endif

/* CTASSERT is FreeBSD's compile-time assertion. */
#ifndef CTASSERT
# define CTASSERT(cond) _Static_assert((cond), #cond)
#endif

/* __FBSDID is the source-id stamping macro used in many FreeBSD
 * sources. It expands to nothing in modern builds. */
#ifndef __FBSDID
# define __FBSDID(x)
#endif

#ifndef RIDUX_COPYOUT_DEFINED
#define RIDUX_COPYOUT_DEFINED 1
static inline int
copyout(const void *kaddr, void *uaddr, size_t len)
{
    const unsigned char *s = (const unsigned char *)kaddr;
    unsigned char *d = (unsigned char *)uaddr;
    size_t i;
    if (!kaddr || !uaddr) return EFAULT;
    for (i = 0; i < len; ++i) d[i] = s[i];
    return 0;
}
#endif

#endif /* _RIDUX_SHIM_SYS_SYSTM_H_ */
