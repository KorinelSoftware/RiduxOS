/*
 * Ridux shim for FreeBSD <sys/proc.h>.
 *
 * Strategy: model FreeBSD's `struct thread` and `struct proc` as
 * lightweight wrappers around Ridux's existing `task_t`. Imported
 * Linuxulator code that does `td->td_emuldata`, `td->td_proc->p_pid`,
 * etc. then "just works" without changing the upstream source.
 *
 * Implementation lives in src/freebsd_compat/ridux_freebsd_glue.c.
 *
 * NOTE: We deliberately do NOT include FreeBSD's real sys/proc.h. That
 * file is huge (~2000 lines, deep dependency chain) and wraps every
 * kernel subsystem the Linuxulator never touches. Our minimal shim
 * here covers only the fields and helpers actually referenced by
 * imported files. We grow this header only when a build error reveals
 * a missing field.
 */
#ifndef _RIDUX_SHIM_SYS_PROC_H_
#define _RIDUX_SHIM_SYS_PROC_H_

#include <sys/param.h>

/* Forward decls Linuxulator expects to be visible. */
struct image_params;
struct linux_emuldata;
struct linux_pemuldata;
struct linux_robust_list_head;
struct sysentvec;
struct vmspace;
struct vnode;
struct ucred;

/*
 * proc / thread shim.
 *
 * The shim structs below are NOT the same as FreeBSD's. We only carry
 * the fields imported code derefs. Each shim instance is allocated and
 * paired 1:1 with a Ridux task_t in ridux_freebsd_glue.c.
 */
struct proc {
    int                          p_pid;       /* matches task_t.pid */
    void                        *p_emuldata;  /* struct linux_pemuldata * */
    struct sysentvec            *p_sysent;    /* set by linux_on_exec */
    void                        *p_task;      /* opaque task_t* back-ref */
    /* Spinlock represented by an int; uniprocessor builds = no-op. */
    int                          p_lock;
};

struct thread {
    int                          td_tid;      /* matches task_t.pid (1:1 threading) */
    struct proc                 *td_proc;     /* parent proc shim */
    void                        *td_emuldata; /* struct linux_emuldata * */
    void                        *td_task;     /* opaque task_t* back-ref */
    long                         td_retval[2];/* syscall return values */
};

/*
 * curthread: a macro that yields the current thread. Implemented as a
 * function call into the glue layer that walks Ridux's task table.
 */
struct thread *ridux_curthread(void);
#define curthread       (ridux_curthread())

/*
 * Process locking. Ridux is currently uniprocessor, so PROC_LOCK et al
 * are compiled to no-ops. Once SMP lands we'll route them to a real
 * spinlock. The macros must still type-check against a `struct proc *`.
 */
#define PROC_LOCK(p)        ((void)((p)->p_lock = 1))
#define PROC_UNLOCK(p)      ((void)((p)->p_lock = 0))
#define PROC_LOCK_ASSERT(p, what)   ((void)0)
#define PROC_LOCK_OWNED(p)          (1)

/*
 * SV_CURPROC_ABI / SV_ABI_LINUX: FreeBSD uses these to multiplex
 * sysentvec ABIs (Linux vs native). All Ridux user processes are Linux
 * ABI by construction, so SV_CURPROC_ABI() is hard-wired to LINUX.
 */
#define SV_ABI_LINUX     1
#define SV_ABI_FREEBSD   0
#define SV_ABI_MASK      0x000000ff
#define SV_ILP32         0x00000100
#define SV_CURPROC_ABI() (SV_ABI_LINUX)
#define SV_PROC_ABI(p)   (SV_ABI_LINUX)
#define SV_PROC_FLAG(p, flag) (0)

#define FOREACH_THREAD_IN_PROC(p, tdvar) \
    for ((tdvar) = (struct thread *)0; (tdvar) != (struct thread *)0; )

#endif /* _RIDUX_SHIM_SYS_PROC_H_ */
