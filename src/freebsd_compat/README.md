# FreeBSD Compatibility Shim Layer

This directory contains a thin **shim** that maps FreeBSD kernel APIs to
Ridux equivalents. Its purpose is to allow files copied from FreeBSD's
Linuxulator (`sys/compat/linux/`) to compile and link unchanged against
the Ridux kernel.

## Layout

```
src/freebsd_compat/
  sys/                          <- shim for <sys/*.h> includes
    param.h                     basic FreeBSD types + nitems()
    systm.h                     KASSERT, panic, printf-like helpers
    errno.h                     FreeBSD errno values (1..97)
    types.h                     pid_t, uid_t, etc.
    ...
  compat/
    linux/                      <- copies of FreeBSD's compat/linux/ headers
      linux.h
      linux_errno.h
      linux_errno_table.h
      ...
  machine/
    ...                         <- arch-specific shims if needed
  ridux_freebsd_glue.c          C glue: kern_sigaction, fork1, kern_clone
                                 implemented as wrappers calling Ridux's
                                 real_sys_* functions.
```

## Imported files

`src/linuxulator/` contains the `*.c` files copied verbatim from
`third_party/upstream/freebsd-src/sys/compat/linux/`. They are compiled
with `-Isrc/freebsd_compat -Ithird_party/upstream/freebsd-src/sys` so
their `<sys/*.h>` and `<compat/linux/*.h>` includes resolve to our
shim plus the unchanged FreeBSD headers respectively.

## Build flow

The Makefile target `linuxulator-objs` adds each imported file to the
final kernel link. Each imported file becomes one `.o` in `BUILD_DIR/`.

## License notice

Files in `linuxulator/` retain their original BSD-2-Clause headers. The
shim files in this directory are written for Ridux from scratch under
the Ridux license.
