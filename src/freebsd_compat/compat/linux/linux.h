/*
 * Ridux shim for FreeBSD <compat/linux/linux.h>.
 *
 * NOTE: We deliberately shadow FreeBSD's real <compat/linux/linux.h>
 * (in third_party/upstream/freebsd-src/sys/compat/linux/linux.h)
 * because that file pulls in heavy dependencies — function declarations
 * using sigset_t, l_sigset_t, dev_t, struct linux_device_handler, etc. —
 * that need a much larger shim than the pilot phase needs.
 *
 * For the trivial pilots (linux_errno.c, linux_rseq.c) we just need
 * this header to be includeable. As we import heavier files
 * (linux_signal.c uses sigset translations heavily), we will gradually
 * grow this shim to declare the functions and types those files need.
 */
#ifndef _RIDUX_SHIM_COMPAT_LINUX_LINUX_H_
#define _RIDUX_SHIM_COMPAT_LINUX_LINUX_H_

/* Pull in our amd64-flavoured shim that defines the l_* types
 * (l_int, l_long, ...) so any code path that reaches us via
 * <compat/linux/linux.h> still sees those typedefs without having
 * to also include <machine/../linux/linux.h>. */
#include <linux/linux.h>

#endif /* _RIDUX_SHIM_COMPAT_LINUX_LINUX_H_ */
