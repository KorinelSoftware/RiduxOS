/*
 * Ridux shim for FreeBSD <sys/lock.h>.
 *
 * Real FreeBSD sys/lock.h declares the witness/locking framework. We
 * only need the symbols imported Linuxulator files reference; on a
 * uniprocessor kernel the lock operations are no-ops.
 */
#ifndef _RIDUX_SHIM_SYS_LOCK_H_
#define _RIDUX_SHIM_SYS_LOCK_H_

#include <sys/param.h>

/* Lock-class placeholders. Linuxulator code rarely creates new lock
 * classes; when it does, the cookie is stored but never inspected on
 * uniprocessor. */
struct lock_object {
    int lo_dummy;
};

#define LOCK_DEBUG          0
#define LOCK_FILE           __FILE__
#define LOCK_LINE           __LINE__

#endif /* _RIDUX_SHIM_SYS_LOCK_H_ */
