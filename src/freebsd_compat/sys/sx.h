/*
 * Ridux shim for FreeBSD <sys/sx.h> (shared/exclusive lock).
 *
 * Uniprocessor kernel: every lock operation is a no-op. The struct
 * exists so embedded sx locks (e.g. in struct linux_pemuldata) still
 * type-check.
 */
#ifndef _RIDUX_SHIM_SYS_SX_H_
#define _RIDUX_SHIM_SYS_SX_H_

#include <sys/param.h>
#include <sys/lock.h>

struct sx {
    int sx_dummy;
};

#define sx_init(sx, name)               ((void)((sx)->sx_dummy = 0))
#define sx_init_flags(sx, name, flags)  ((void)((sx)->sx_dummy = 0))
#define sx_destroy(sx)                  ((void)((sx)->sx_dummy = 0))
#define sx_xlock(sx)                    ((void)((sx)->sx_dummy = 1))
#define sx_xunlock(sx)                  ((void)((sx)->sx_dummy = 0))
#define sx_slock(sx)                    sx_xlock(sx)
#define sx_sunlock(sx)                  sx_xunlock(sx)
#define sx_xlocked(sx)                  (1)
#define sx_assert(sx, what)             ((void)0)

/*
 * SX_SYSINIT registers an sx_init at SYSINIT time. On Ridux there is
 * no SYSINIT framework yet, so we expand to nothing -- callers that
 * rely on the lock being initialised at module-load time are expected
 * to call sx_init explicitly somewhere in our glue layer.
 */
#define SX_SYSINIT(name, sx, descr)     /* no-op */

#endif /* _RIDUX_SHIM_SYS_SX_H_ */
