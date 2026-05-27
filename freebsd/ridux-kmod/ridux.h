/*
 * ridux.h - Ridux kernel bridge contract for FreeBSD.
 */

#ifndef _SYS_RIDUX_H_
#define _SYS_RIDUX_H_

#include <sys/types.h>

#define RIDUX_VERSION 1
#define RIDUX_MAX_PROCS 64

typedef struct ridux_proc {
	pid_t pid;
	int state;
	void *compat_data;
} ridux_proc_t;

int ridux_init(void);
int ridux_cleanup(void);
int ridux_proc_create(pid_t *pid);
int ridux_proc_destroy(pid_t pid);

#endif /* _SYS_RIDUX_H_ */
