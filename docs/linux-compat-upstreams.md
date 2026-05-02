# Ridux Linux ABI: upstreams utiles

Objetivo: acelerar la compatibilidad Linux nativa sin convertir Ridux en una
distro Linux y sin reemplazar el kernel propio. No hay una biblioteca magica
que haga correr Firefox/Chromium/Steam completa por si sola: hay que unir ABI,
VFS/procfs, procesos, futex, sockets, display, audio, GPU y sandbox. Pero si
hay codigo open source muy valioso para copiar por modulo o usar como oracle.

## Decision actual

El camino principal para Firefox/Chromium/Steam es:

```text
ELF Linux x86_64
  -> loader/interpreter Linux
  -> syscalls Linux en src/compat/*.c
  -> VFS/procfs/devfs Ridux
  -> DRM/KMS/input/audio nativos
  -> Wayland/X11 o bridge grafico Ridux
```

La ruta seL4/VM queda como fallback/oraculo para comparar comportamiento real,
no como camino por defecto.

## Codigo base recomendado

| Proyecto | Licencia general | Para que nos sirve | Como integrarlo |
| --- | --- | --- | --- |
| FreeBSD Linuxulator | BSD | Semantica kernel-level de Linux ABI: ELF, errno, futex, signals, files, sockets, mmap, proc quirks. | Seguir importando por subsistema bajo `src/linuxulator` + shims Ridux. |
| Fuchsia Starnix | BSD/Apache segun archivo | Arquitectura moderna de Linux UAPI sin VM; muy bueno para VFS, task model, syscall loop y tests. | Usarlo como referencia/oraculo; portar ideas y casos de prueba, no mezclar Rust directo en kernel C. |
| gVisor | Apache-2.0 | Tabla de syscalls, semantica detallada, tests y comportamiento de edge cases. | Usarlo como oracle de compatibilidad y para priorizar syscalls faltantes. |
| NetBSD Rump | BSD | Drivers, filesystems y networking como servicios aislados. | Integrar despues como componentes, no para reemplazar el ABI Linux. |
| mlibc | MIT | Libc portable para ports nativos Ridux y headers ABI Linux. | Usarlo para toolchain/ports nativos; no reemplaza glibc para Firefox oficial. |
| Mesa/libdrm/Wayland/Xorg | MIT/BSD/MIT-like mixto | Render real: GPU, ventanas, input y compositing. | Prioridad despues de que procesos/syscalls sobrevivan: primero software rendering, luego DRM/KMS. |

## Orden pragmatico

1. FreeBSD Linuxulator sigue siendo el tronco principal porque ya existe en el
   repo y su licencia nos conviene.
2. Starnix y gVisor se usan como mapa de comportamiento: si Ridux falla una
   syscall, miramos como la resuelven ellos y escribimos la version Ridux.
3. Para Firefox/Chromium reales hay que cerrar primero estas piezas:
   `clone/clone3`, `futex`, `epoll`, `poll`, `mmap/mprotect`, signals,
   `/proc`, `/sys`, `/dev`, sockets, DNS, shared memory, `eventfd`,
   `timerfd`, `memfd`, `prctl`, `ioctl`, `seccomp` y namespaces minimos.
4. Despues viene la parte visible: Wayland/X11 minimo, input real, audio y
   render por Mesa/software. Steam exige ademas Vulkan/DRM, gamepads y Proton.

## Fuentes primarias

- FreeBSD Linuxulator handbook:
  https://docs.freebsd.org/en/books/handbook/linuxemu/
- FreeBSD Linuxulator source:
  https://github.com/freebsd/freebsd-src/tree/main/sys/compat/linux
- Fuchsia Starnix docs:
  https://fuchsia.dev/fuchsia-src/concepts/starnix
- Fuchsia Starnix source:
  https://fuchsia.googlesource.com/fuchsia/+/main/src/starnix/kernel/
- gVisor syscall compatibility:
  https://gvisor.dev/docs/user_guide/compatibility/linux/amd64/
- gVisor syscall source:
  https://github.com/google/gvisor/tree/master/pkg/sentry/syscalls/linux
- NetBSD rump sysproxy:
  https://www.netbsd.org/docs/rump/sysproxy.html
- mlibc porting guide:
  https://docs.managarm.org/mlibc-book/porting/implementing_sysdeps_p1.html
