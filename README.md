# RiduxOS

RiduxOS es mi intento de hacer un sistema operativo propio, no una distro con skin. La idea es tener kernel, compositor, apps, runtime Linux-ish y escritorio todo en el mismo proyecto, entendible y hackeable sin tener que abrir diez repos distintos.

Todavia esta en obra. Hay cosas muy avanzadas para ser hobby OS y otras que siguen siendo medio Frankenstein. Lo importante es que cada cambio quede mas ordenado que antes y que no se vuelva imposible de tocar.

## Que hay ahora

- Kernel x86_64 con arranque Multiboot2.
- Framebuffer propio con renderer Flush.
- Ventanas, escritorio y apps nativas.
- Apps Ring 3 para terminal, calculadora, ajustes, archivos, paint, etc.
- Capa de compatibilidad Linux para intentar correr binarios reales.
- Bridge grafico X11/Wayland en progreso para Firefox/Chromium.
- Initrd con rootfs y overlay para meter apps nuevas sin regenerar todo.
- Scripts para compilar y bootear en VirtualBox.

## Estructura

```text
src/kernel.c                  entrada del kernel
src/kernel/                   kernel separado por tema, todo en .c
src/compat/                   runtime/compat Linux, ordenado por responsabilidad
src/flush.c                   renderer 2D del OS
src/ridux_r3wm.h              protocolo de ventanas Ring 3
tools/                        generadores, apps Ring 3 y empaquetado
scripts/                      scripts de boot y diagnostico
grub/                         config de arranque
rootfs/                       filesystem que termina dentro del initrd
```

El kernel se sigue compilando como una sola unidad desde `src/kernel.c`. No es lo mas lindo del mundo, pero evita romper medio sistema mientras se separan archivos. Al menos ya no esta todo en un solo archivo gigante ni hay archivos de inclusion raros en el codigo del kernel.

## Compat

La carpeta `src/compat/` reemplaza los viejos `compat.c`, `compat2.c`, etc. Los nombres nuevos son mas faciles de ubicar:

- `base.c`: syscalls base, drivers simples, sockets virtuales y glue viejo.
- `memory_tasks.c`: paging, tareas, scheduler de usuario y Ring 3.
- `linux_syscalls.c`: syscalls Linux-ish, ELF64, mmap, dynlink y VFS bridge.
- `user_libc.c`: mini libc propia.
- `bsd_libc.c`: helpers adaptados de FreeBSD y launcher de ELFs Ring 3.
- `linux_abi.c`: syscalls modernas que suelen pedir apps grandes.
- `display_wayland.c`: red, X11 y Wayland.
- `browser_runtime.c`: cosas extra para navegadores reales.

Los nombres de funciones tipo `compat5_*` siguen existiendo por ahora para no romper todo de golpe. Eso se puede limpiar despues, con calma y con tests.

## Build sin matar la PC

El build completo de ISO puede consumir bastante porque mete assets grandes y llama a `grub-mkrescue`/`xorriso`. Si solo estas tocando kernel o apps, no conviene regenerar todo cada vez.

Para compilar solo kernel:

```bash
nice -n 19 ionice -c3 make BUILD_DIR=build_wsl_firefox kernel-only -j1
```

Para compilar solo apps Ring 3 y overlay:

```bash
nice -n 19 ionice -c3 make BUILD_DIR=build_wsl_firefox initrd-overlay.img -j1
```

Para regenerar ISO usando la initrd base que ya existe:

```bash
nice -n 19 ionice -c3 make BUILD_DIR=build_wsl_firefox iso-from-existing-initrd -j1
```

Evitar `make all -j` si la PC esta justa de RAM. No vale la pena freir todo por una ISO.

## Boot en VirtualBox

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\boot-virtualbox.ps1 `
  -IsoPath .\build_wsl_firefox\RiduxOS-Unix.iso `
  -VmName RiduxOS_Unix_Demo `
  -MemoryMB 2048 `
  -CpuCount 2 `
  -CpuExecutionCap 55 `
  -BootSeconds 35
```

El script intenta no dejar la VM corriendo si solo se queria probar el arranque. Si queres dejarla abierta, usar `-KeepRunning`.

## Estado real

- La UI nativa ya corre bastante, pero todavia hay partes que estan migrando a Ring 3.
- Firefox es el objetivo grande. El runtime tiene Wayland/X11 y syscalls modernas, pero no hay que mentirse: correr un navegador real como Linux requiere muchisima compatibilidad.
- SMP y GPU real no son un checkbox. Hay deteccion/topologia y aceleraciones de framebuffer, pero AP startup completo y driver GPU real siguen siendo trabajo serio.

## Regla para tocar esto

Cambios chicos, probables y con logs. Si algo se puede probar con `kernel-only`, no generar ISO. Si una parte esta fea pero funciona, primero se la rodea con estructura y despues se refactoriza.

Ese es el plan: que Ridux se vaya pareciendo cada vez mas a un OS real sin convertir el repo en una bomba.