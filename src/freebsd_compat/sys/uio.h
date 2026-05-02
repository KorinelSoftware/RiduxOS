/*
 * Ridux shim for FreeBSD <sys/uio.h>.
 *
 * The Linuxulator files imported so far only need the basic iovec
 * shape through linux_util.h. Pulling the upstream FreeBSD sys/uio.h
 * drags in the full FreeBSD type system, so keep this intentionally
 * small until a copied source file proves it needs more.
 */
#ifndef _RIDUX_SHIM_SYS_UIO_H_
#define _RIDUX_SHIM_SYS_UIO_H_

#include <sys/types.h>

struct iovec {
    void  *iov_base;
    size_t iov_len;
};

enum uio_seg {
    UIO_USERSPACE = 0,
    UIO_SYSSPACE  = 1,
};

enum uio_rw {
    UIO_READ  = 0,
    UIO_WRITE = 1,
};

#endif /* _RIDUX_SHIM_SYS_UIO_H_ */
