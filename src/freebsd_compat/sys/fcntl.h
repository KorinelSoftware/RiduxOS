/*
 * Ridux shim for FreeBSD <sys/fcntl.h>.
 *
 * Linuxulator files include this transitively (e.g. linux_emul.c) but
 * rarely use the symbols directly — most fcntl(2) handling lives in
 * linux_file.c. We provide an empty stub so the include resolves; if
 * linux_file.c is later imported, this header grows.
 */
#ifndef _RIDUX_SHIM_SYS_FCNTL_H_
#define _RIDUX_SHIM_SYS_FCNTL_H_

#include <sys/param.h>

#endif /* _RIDUX_SHIM_SYS_FCNTL_H_ */
