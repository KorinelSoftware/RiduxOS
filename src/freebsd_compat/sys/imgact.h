/*
 * Ridux shim for FreeBSD <sys/imgact.h>.
 *
 * Real FreeBSD imgact.h declares the image-activator framework used by
 * exec to load ELF/script binaries. Linuxulator code only references
 * `struct image_params` and `struct image_args` as opaque pointers, so
 * forward declarations are enough.
 */
#ifndef _RIDUX_SHIM_SYS_IMGACT_H_
#define _RIDUX_SHIM_SYS_IMGACT_H_

#include <sys/param.h>
#include <sys/sysent.h>

struct image_params {
    struct sysentvec *sysent;
    int stack_prot;
};
struct image_args;

#ifndef VM_PROT_EXECUTE
#define VM_PROT_EXECUTE 0x04
#endif

#endif /* _RIDUX_SHIM_SYS_IMGACT_H_ */
