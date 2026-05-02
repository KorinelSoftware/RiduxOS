/*
 * Ridux shim for FreeBSD <sys/param.h>.
 *
 * Provides only the bits that imported Linuxulator code needs:
 *   - basic fixed-size integer types
 *   - the nitems() macro used to size static arrays
 *
 * Real FreeBSD sys/param.h is enormous (kernel limits, version macros,
 * machine-specific bits, etc.). We pull in only what compiles.
 */
#ifndef _RIDUX_SHIM_SYS_PARAM_H_
#define _RIDUX_SHIM_SYS_PARAM_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * FreeBSD's real <sys/param.h> also makes core kernel/POSIX types
 * visible. A few Linuxulator headers rely on that implicit contract
 * and reference pid_t/lwpid_t after including only <sys/param.h>.
 */
#include <sys/types.h>

#ifndef __printflike
#define __printflike(fmtarg, firstvararg) \
    __attribute__((__format__(__printf__, fmtarg, firstvararg)))
#endif

#ifndef __diagused
#define __diagused __attribute__((__unused__))
#endif

#ifndef __unused
#define __unused __attribute__((__unused__))
#endif

#ifndef EJUSTRETURN
#define EJUSTRETURN (-2)
#endif

#ifndef KASSERT
#define KASSERT(cond, msg) do { (void)(cond); } while (0)
#endif

#ifndef MPASS
#define MPASS(cond) do { (void)(cond); } while (0)
#endif

#ifndef RIDUX_COPYOUT_DEFINED
#define RIDUX_COPYOUT_DEFINED 1
static inline int
copyout(const void *kaddr, void *uaddr, size_t len)
{
    const unsigned char *s = (const unsigned char *)kaddr;
    unsigned char *d = (unsigned char *)uaddr;
    size_t i;
    if (!kaddr || !uaddr) return 14; /* EFAULT */
    for (i = 0; i < len; ++i) d[i] = s[i];
    return 0;
}
#endif

/* nitems(x): number of elements in a static array. Used heavily by
 * Linuxulator for sanity checks on translation tables. */
#ifndef nitems
#define nitems(x)       (sizeof((x)) / sizeof((x)[0]))
#endif

/* howmany / roundup / rounddown helpers used by some Linuxulator
 * sources. Cheap defines. */
#ifndef howmany
#define howmany(x, y)   (((x) + ((y) - 1)) / (y))
#endif
#ifndef roundup
#define roundup(x, y)   ((((x) + ((y) - 1)) / (y)) * (y))
#endif
#ifndef rounddown
#define rounddown(x, y) (((x) / (y)) * (y))
#endif

/* MIN / MAX */
#ifndef MIN
#define MIN(a, b)       (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b)       (((a) > (b)) ? (a) : (b))
#endif

#endif /* _RIDUX_SHIM_SYS_PARAM_H_ */
