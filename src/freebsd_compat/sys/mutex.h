/*
 * Ridux shim for FreeBSD <sys/mutex.h>.
 *
 * Uniprocessor kernel: every mutex operation is a no-op. The struct
 * exists so imported code that embeds a mutex still type-checks.
 */
#ifndef _RIDUX_SHIM_SYS_MUTEX_H_
#define _RIDUX_SHIM_SYS_MUTEX_H_

#include <sys/param.h>
#include <sys/lock.h>

struct mtx {
    int mtx_dummy;
};

#define mtx_init(m, name, type, opts)   ((void)((m)->mtx_dummy = 0))
#define mtx_destroy(m)                  ((void)((m)->mtx_dummy = 0))
#define mtx_lock(m)                     ((void)((m)->mtx_dummy = 1))
#define mtx_unlock(m)                   ((void)((m)->mtx_dummy = 0))
#define mtx_lock_spin(m)                mtx_lock(m)
#define mtx_unlock_spin(m)              mtx_unlock(m)
#define mtx_assert(m, what)             ((void)0)
#define mtx_owned(m)                    (1)

#define MTX_DEF                         0x00000000
#define MTX_SPIN                        0x00000001
#define MTX_RECURSE                     0x00000004
#define MTX_NEW                         0x00000040

#define MA_OWNED                        0x01
#define MA_NOTOWNED                     0x02

#endif /* _RIDUX_SHIM_SYS_MUTEX_H_ */
