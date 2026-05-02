/*
 * Ridux shim for FreeBSD <sys/syscallsubr.h>.
 *
 * FreeBSD declares ~200 kern_*() helpers here. Linuxulator code calls
 * a subset of them (kern_sigaction, kern_clone, kern_socket, ...). We
 * declare them on demand in this header as we import the files that
 * need them; the implementations live in ridux_freebsd_glue.c and are
 * thin wrappers over Ridux's existing real_sys_* handlers.
 *
 * For the linux_emul.c / linux_rseq.c pilot, no kern_* call is made,
 * so this header is a stub that just resolves the include.
 */
#ifndef _RIDUX_SHIM_SYS_SYSCALLSUBR_H_
#define _RIDUX_SHIM_SYS_SYSCALLSUBR_H_

#include <sys/param.h>

struct thread;
struct image_args;
struct vmspace;

int  pre_execve(struct thread *td, struct vmspace **oldvmspace);
int  kern_execve(struct thread *td, struct image_args *args, void *mac_p,
                 struct vmspace *oldvmspace);
void post_execve(struct thread *td, int error, struct vmspace *oldvmspace);

#endif /* _RIDUX_SHIM_SYS_SYSCALLSUBR_H_ */
