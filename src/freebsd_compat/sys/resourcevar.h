/*
 * Ridux shim for FreeBSD <sys/resourcevar.h>.
 *
 * Provides only what linux_emul.c references: the rlimit struct used
 * by lim_rlimit_proc, plus that helper signature. We implement
 * lim_rlimit_proc in ridux_freebsd_glue.c — for now it returns a pair
 * of large numbers so the Linuxulator default-stack-size and
 * default-files heuristics in linux_emul.c are no-ops.
 */
#ifndef _RIDUX_SHIM_SYS_RESOURCEVAR_H_
#define _RIDUX_SHIM_SYS_RESOURCEVAR_H_

#include <sys/param.h>

struct proc;

typedef uint64_t rlim_t;

struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
};

#define RLIM_INFINITY   ((rlim_t)~(uint64_t)0)

#define RLIMIT_CPU       0
#define RLIMIT_FSIZE     1
#define RLIMIT_DATA      2
#define RLIMIT_STACK     3
#define RLIMIT_CORE      4
#define RLIMIT_RSS       5
#define RLIMIT_MEMLOCK   6
#define RLIMIT_NPROC     7
#define RLIMIT_NOFILE    8

void lim_rlimit_proc(struct proc *p, int which, struct rlimit *out);
int  kern_proc_setrlimit(struct thread *td, struct proc *p, int which,
                         const struct rlimit *lim);

#endif /* _RIDUX_SHIM_SYS_RESOURCEVAR_H_ */
