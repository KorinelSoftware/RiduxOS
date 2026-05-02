/*
 * Ridux FreeBSD Compatibility Glue
 * ================================
 *
 * Implements the helpers declared in src/freebsd_compat/sys/*.h that the
 * imported FreeBSD Linuxulator code calls into:
 *
 *   - ridux_curthread()       walks Ridux's task table and returns the
 *                             matching `struct thread` shim.
 *   - ridux_freebsd_malloc()  thin wrapper over Ridux's ulibc_malloc.
 *   - ridux_freebsd_free()    matching free wrapper.
 *   - M_LINUX / M_TEMP        single-instance malloc tags used by every
 *                             Linuxulator allocation.
 *
 * Per-task `struct thread` and `struct proc` shims live in static
 * parallel arrays indexed by Ridux task slot. They are lazily
 * initialised the first time they're requested, so no changes are
 * needed in compat2's task creation paths.
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/resourcevar.h>
#include <sys/syscallsubr.h>
#include <sys/sysent.h>

/* Pull Ridux types via ulibc allocator and task lookup helpers. We
 * include them via relative path because src/freebsd_compat/ has no
 * other way to reach src/compat/*.h headers; the include path used to
 * compile this file is just `-Isrc/freebsd_compat -Ithird_party/...`. */
#include "../compat/base.h"
#include "../compat/memory_tasks.h"
#include "../compat/user_libc.h"

/*
 * One-to-one shim slots paired with Ridux task slots.
 *
 * We rely on TASK_MAX from compat2.h. Each entry is initialised the
 * first time `ridux_curthread()` is called for the corresponding
 * task; until then, used=false.
 */
typedef struct {
    bool          used;
    struct thread td;
    struct proc   p;
} ridux_freebsd_slot_t;

static ridux_freebsd_slot_t g_freebsd_slots[TASK_MAX];

int linux_debug = 0;
int linux_default_openfiles = -1;
int linux_default_stacksize = -1;
int linux_dummy_rlimits = 0;
int linux_ignore_ip_recverr = 1;
int linux_preserve_vstatus = 0;
bool linux_map_sched_prio = false;

static void
ridux_linux_thread_detach(struct thread *td)
{
    if (td) td->td_emuldata = (void *)0;
}

static struct sysentvec g_ridux_linux_sysent = {
    .sv_thread_detach = ridux_linux_thread_detach,
    .sv_set_syscall_retval = (void *)0,
    .sv_flags = SV_ABI_LINUX,
    .sv_minuser = 0x1000,
    .sv_maxuser = 0x0000800000000000ULL,
    .sv_name = "Linux ELF64"
};

static void
ridux_freebsd_slot_lazy_init(int idx, task_t *t)
{
    ridux_freebsd_slot_t *s;
    if (idx < 0 || idx >= TASK_MAX) return;
    s = &g_freebsd_slots[idx];
    if (s->used) {
        /* Refresh fields that may have changed (pid is stable, but
         * keep this trivially cheap so we always see the latest). */
        s->td.td_tid    = t ? t->pid : 0;
        s->td.td_proc   = &s->p;
        s->td.td_task   = t;
        s->p.p_pid      = t ? t->pid : 0;
        s->p.p_sysent   = &g_ridux_linux_sysent;
        s->p.p_task     = t;
        return;
    }
    s->used = true;
    s->td.td_tid       = t ? t->pid : 0;
    s->td.td_proc      = &s->p;
    s->td.td_emuldata  = (void *)0;
    s->td.td_task      = t;
    s->td.td_retval[0] = 0;
    s->td.td_retval[1] = 0;
    s->p.p_pid         = t ? t->pid : 0;
    s->p.p_emuldata    = (void *)0;
    s->p.p_sysent      = &g_ridux_linux_sysent;
    s->p.p_task        = t;
    s->p.p_lock        = 0;
}

struct thread *
ridux_curthread(void)
{
    int idx = g_current_task;
    task_t *t;
    if (idx < 0 || idx >= TASK_MAX) return (struct thread *)0;
    t = &g_tasks[idx];
    if (!t->used) return (struct thread *)0;
    ridux_freebsd_slot_lazy_init(idx, t);
    return &g_freebsd_slots[idx].td;
}

/*
 * Allocator. Ridux's ulibc_malloc returns zeroed memory only on
 * explicit calloc, so we honour M_ZERO ourselves.
 */
struct malloc_type *const M_LINUX = &(struct malloc_type){ "M_LINUX" };
struct malloc_type *const M_TEMP  = &(struct malloc_type){ "M_TEMP"  };

/* The malloc.h shim defines `malloc(...)` as a macro that calls
 * ridux_freebsd_malloc(). Since this file's #include <sys/malloc.h>
 * brings the same macro in, our local function definition would
 * collide with the macro expansion. Undefine the macros here so the
 * implementations use the unprefixed C names cleanly. */
#undef malloc
#undef free

void *
ridux_freebsd_malloc(size_t size, struct malloc_type *type, int flags)
{
    void *p;
    (void)type;
    if (size == 0) return (void *)0;
    p = ulibc_malloc(size);
    if (!p) return (void *)0;
    if (flags & M_ZERO) {
        unsigned char *b = (unsigned char *)p;
        size_t i;
        for (i = 0; i < size; ++i) b[i] = 0;
    }
    return p;
}

void
ridux_freebsd_free(void *ptr, struct malloc_type *type)
{
    (void)type;
    if (ptr) ulibc_free(ptr);
}

void
lim_rlimit_proc(struct proc *p, int which, struct rlimit *out)
{
    (void)p;
    (void)which;
    if (!out) return;
    out->rlim_cur = RLIM_INFINITY;
    out->rlim_max = RLIM_INFINITY;
}

int
kern_proc_setrlimit(struct thread *td, struct proc *p, int which,
                    const struct rlimit *lim)
{
    (void)td;
    (void)p;
    (void)which;
    (void)lim;
    return 0;
}

int
pre_execve(struct thread *td, struct vmspace **oldvmspace)
{
    (void)td;
    if (oldvmspace) *oldvmspace = (struct vmspace *)0;
    return 0;
}

int
kern_execve(struct thread *td, struct image_args *args, void *mac_p,
            struct vmspace *oldvmspace)
{
    (void)td;
    (void)args;
    (void)mac_p;
    (void)oldvmspace;
    return EJUSTRETURN;
}

void
post_execve(struct thread *td, int error, struct vmspace *oldvmspace)
{
    (void)td;
    (void)error;
    (void)oldvmspace;
}

int
linux_pwd_onexec(struct thread *td)
{
    (void)td;
    return 0;
}

void
linux_pwd_onexec_native(struct thread *td)
{
    (void)td;
}

void
linux_msg(const struct thread *td, const char *fmt, ...)
{
    (void)td;
    (void)fmt;
}
