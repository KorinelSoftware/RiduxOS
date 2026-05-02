/*
 * Ridux shim for FreeBSD <sys/sysent.h>.
 *
 * FreeBSD's sysentvec dispatches per-ABI behaviour (Linux vs native).
 * Linuxulator code dereferences a few sysentvec fields (sv_thread_detach,
 * sv_minuser, sv_maxuser, ...). We provide a minimal struct so those
 * derefs type-check and a single shared instance reachable via
 * `p->p_sysent`.
 */
#ifndef _RIDUX_SHIM_SYS_SYSENT_H_
#define _RIDUX_SHIM_SYS_SYSENT_H_

#include <sys/param.h>

struct thread;
struct proc;

struct sysentvec {
    void  (*sv_thread_detach)(struct thread *);
    void  (*sv_set_syscall_retval)(struct thread *, int);
    int     sv_flags;
    uint64_t sv_minuser;
    uint64_t sv_maxuser;
    const char *sv_name;
};

#endif /* _RIDUX_SHIM_SYS_SYSENT_H_ */
