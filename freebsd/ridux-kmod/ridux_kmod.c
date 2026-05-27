/*
 * ridux_kmod.c - FreeBSD kernel bridge for Ridux.
 *
 * This is intentionally small: the FreeBSD kernel stays the stable base,
 * while Ridux-specific ABI/display contracts grow behind this module.
 */

#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/ridux.h>

static int ridux_loaded;
static char ridux_base[] = "freebsd-kernel";
static char ridux_desktop_contract[] = "wayland-wayfire-pixman-first";

SYSCTL_NODE(_kern, OID_AUTO, ridux, CTLFLAG_RW | CTLFLAG_MPSAFE, 0,
    "Ridux kernel bridge");
SYSCTL_INT(_kern_ridux, OID_AUTO, loaded, CTLFLAG_RD, &ridux_loaded, 0,
    "Ridux bridge loaded");
SYSCTL_STRING(_kern_ridux, OID_AUTO, base, CTLFLAG_RD, ridux_base, 0,
    "Ridux kernel base");
SYSCTL_STRING(_kern_ridux, OID_AUTO, desktop_contract, CTLFLAG_RD,
    ridux_desktop_contract, 0, "Ridux desktop compositor contract");

static int ridux_modevent(module_t mod, int type, void *data);

static moduledata_t ridux_mod = {
	"ridux",
	ridux_modevent,
	NULL
};

DECLARE_MODULE(ridux, ridux_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(ridux, 1);
MODULE_DEPEND(ridux, linux, 1, 1, 1);

int
ridux_init(void)
{
	ridux_loaded = 1;
	return (0);
}

int
ridux_cleanup(void)
{
	ridux_loaded = 0;
	return (0);
}

int
ridux_proc_create(pid_t *pid)
{
	if (pid == NULL)
		return (EINVAL);
	*pid = curproc != NULL ? curproc->p_pid : 0;
	return (0);
}

int
ridux_proc_destroy(pid_t pid)
{
	(void)pid;
	return (0);
}

static int
ridux_modevent(module_t mod, int type, void *data)
{
	(void)mod;
	(void)data;

	switch (type) {
	case MOD_LOAD:
		ridux_init();
		uprintf("Ridux kernel bridge loaded: FreeBSD base, Wayfire contract\n");
		return (0);
	case MOD_UNLOAD:
		ridux_cleanup();
		uprintf("Ridux kernel bridge unloaded\n");
		return (0);
	default:
		return (EOPNOTSUPP);
	}
}
