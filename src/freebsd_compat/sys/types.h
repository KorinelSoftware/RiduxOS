/*
 * Ridux shim for FreeBSD <sys/types.h>.
 *
 * Provides the POSIX integer types Linuxulator code references
 * (pid_t, lwpid_t, uid_t, gid_t, off_t, mode_t, etc.) without
 * pulling in the entire FreeBSD type system.
 */
#ifndef _RIDUX_SHIM_SYS_TYPES_H_
#define _RIDUX_SHIM_SYS_TYPES_H_

#include <sys/param.h>

typedef int32_t  pid_t;
typedef int32_t  lwpid_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef int64_t  off_t;
typedef uint32_t mode_t;
typedef int64_t  ssize_t;
typedef uint64_t nlink_t;
typedef uint32_t dev_t;
typedef uint64_t ino_t;
typedef int64_t  blkcnt_t;
typedef int32_t  blksize_t;
typedef int64_t  fsblkcnt_t;
typedef int64_t  fsfilcnt_t;
#if !defined(_TIME_T_DEFINED) && !defined(_TIME_T_DECLARED) && !defined(__time_t_defined)
typedef long     time_t;
#endif
typedef long     suseconds_t;
typedef int      key_t;
typedef int32_t  id_t;
typedef int32_t  clockid_t;
typedef void    *timer_t;
typedef void    *device_t;

#define NODEV   ((dev_t)-1)
#define major(d) ((int)(((d) >> 8) & 0xff))
#define minor(d) ((int)((d) & 0xff))

#endif /* _RIDUX_SHIM_SYS_TYPES_H_ */
