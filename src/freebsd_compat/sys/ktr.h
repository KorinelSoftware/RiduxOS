/*
 * Ridux shim for FreeBSD <sys/ktr.h> (kernel trace).
 *
 * KTR is a debug-only ring buffer in FreeBSD. Linuxulator emits
 * LINUX_CTR1..LINUX_CTR6 records via these macros. On Ridux we don't
 * have a comparable subsystem yet, so they all expand to nothing.
 */
#ifndef _RIDUX_SHIM_SYS_KTR_H_
#define _RIDUX_SHIM_SYS_KTR_H_

#define KTR_LINUX       0
#define CTR0(level, fmt)
#define CTR1(level, fmt, a1)
#define CTR2(level, fmt, a1, a2)
#define CTR3(level, fmt, a1, a2, a3)
#define CTR4(level, fmt, a1, a2, a3, a4)
#define CTR5(level, fmt, a1, a2, a3, a4, a5)
#define CTR6(level, fmt, a1, a2, a3, a4, a5, a6)

#endif /* _RIDUX_SHIM_SYS_KTR_H_ */
