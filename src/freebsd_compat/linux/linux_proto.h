/*
 * Ridux shim resolved by FreeBSD's Linuxulator includes of the form
 * `<machine/../linux/linux_proto.h>`.
 *
 * The real FreeBSD linux_proto.h is auto-generated from
 * sys/amd64/linux/syscalls.master and contains a `struct linux_<name>_args`
 * for every Linux syscall (~430 structs at last count) plus prototypes
 * for every linux_<name>(struct thread *, struct linux_<name>_args *).
 * That file is ~120 KB.
 *
 * For the rseq pilot we only need `struct linux_rseq_args` to be
 * declared (forward decl is enough — the function body never derefs
 * the args). As bigger files come in, this shim grows to either define
 * the args structs directly or include the full upstream file.
 */
#ifndef _RIDUX_SHIM_LINUX_LINUX_PROTO_H_
#define _RIDUX_SHIM_LINUX_LINUX_PROTO_H_

#include <linux/linux.h>

/* Forward declarations of syscall arg structs we know we'll touch.
 * Each will be promoted to a real definition (or pulled in from
 * upstream) when the corresponding file is imported. */
struct linux_rseq_args;

#endif /* _RIDUX_SHIM_LINUX_LINUX_PROTO_H_ */
