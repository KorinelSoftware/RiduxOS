/*
 * Ridux shim resolved by FreeBSD's Linuxulator includes of the form
 * `<machine/../linux/linux.h>`. The `machine/..` part walks up out of
 * the (empty) src/freebsd_compat/machine/ directory and lands here.
 *
 * Real FreeBSD amd64/linux/linux.h pulls in heavy machine-dependent
 * Linux ABI types (l_int, l_long, struct l_pt_regs, l_sigcontext_t,
 * the entire linux_pcb layout, etc.). For the rseq-only pilot we
 * provide just the bare minimum needed for FreeBSD's linux_rseq.c to
 * compile: a forward declaration of `struct thread`. The function body
 * is `return (ENOSYS)` so no member access happens.
 *
 * As we import heavier files (linux_signal.c, linux_futex.c, ...) this
 * header will grow to either:
 *   (a) define the Linux ABI types directly here, OR
 *   (b) `#include <amd64/linux/linux.h>` and provide whatever shim
 *       support that header transitively needs from FreeBSD's kernel.
 *
 * Choose (a) when the type set is small/stable, (b) when we want full
 * upstream parity for a complex subsystem.
 */
#ifndef _RIDUX_SHIM_LINUX_LINUX_H_
#define _RIDUX_SHIM_LINUX_LINUX_H_

#include <sys/param.h>

/* Forward declarations sufficient for stub Linuxulator handlers. */
struct thread;
struct proc;

/* Linux ABI primitive types. These match Linux's int sizes on x86_64
 * and what FreeBSD's amd64/linux/linux.h defines. We define them here
 * so imported files can use l_int/l_long/etc. without pulling in the
 * entire amd64/linux/linux.h dependency chain. */
typedef int32_t  l_int;
typedef int64_t  l_long;
typedef int16_t  l_short;
typedef uint32_t l_uint;
typedef uint64_t l_ulong;
typedef uint16_t l_ushort;
typedef int32_t  l_pid_t;
typedef uint64_t l_size_t;
typedef int64_t  l_ssize_t;
typedef int64_t  l_off_t;
typedef int64_t  l_loff_t;
typedef uint32_t l_uid_t;
typedef uint32_t l_gid_t;
typedef uint64_t l_uintptr_t;
typedef int64_t  l_intptr_t;

#endif /* _RIDUX_SHIM_LINUX_LINUX_H_ */
