# RiduxOS

![RiduxOS Header](./assets/RiduxOSheader.png)

RiduxOS is an attempt to build a full operating system from scratch — not just another Linux distro with a custom skin. The goal is to have the kernel, compositor, runtime, apps, and desktop environment all developed within a single cohesive project.

Everything is meant to be understandable, hackable, and self-contained — without needing to jump across ten different repositories just to follow the system.

The project is still in active development. Some parts are already quite advanced for a hobby OS, while others are still rough around the edges. The priority is to keep improving structure over time and avoid turning the codebase into something unmaintainable.

---

## Current State

![RiduxOS Desktop](./assets/ridux-desktop.png)

- x86_64 kernel with Multiboot2 boot
- Custom framebuffer + 2D renderer (Flush)
- Windowing system, desktop, and native apps
- Ring 3 applications (terminal, calculator, settings, file manager, paint, etc.)
- Linux compatibility layer (work-in-progress)
- X11 / Wayland bridge for running real apps like Firefox / Chromium
- Initrd with rootfs + overlay system for incremental updates
- Scripts for building and booting in VirtualBox

---

## Project Structure

```text
src/kernel.c                  kernel entry point
src/kernel/                   kernel modules (C, split by subsystem)
src/compat/                   Linux-like runtime / compatibility layer
src/flush.c                   2D renderer
src/ridux_r3wm.h              Ring 3 window protocol
tools/                        build tools, apps, packaging
scripts/                      boot and debugging scripts
grub/                         boot configuration
rootfs/                       filesystem bundled into initrd
```

The kernel is still compiled as a single unit from `src/kernel.c`. It's not ideal, but it prevents breaking the system while the codebase is being modularized.

---

## Compatibility Layer

- base.c → core syscalls, basic drivers, legacy glue
- memory_tasks.c → paging, tasking, scheduler, Ring 3 support
- linux_syscalls.c → Linux-like syscalls, ELF64 loading, mmap, dynlink, VFS bridge
- user_libc.c → minimal custom libc
- bsd_libc.c → FreeBSD-derived helpers + ELF launcher
- linux_abi.c → newer syscalls required by modern apps
- display_wayland.c → networking, X11, Wayland
- browser_runtime.c → extra workarounds for real browsers

---

## Build

Compile only the kernel:

```bash
nice -n 19 ionice -c3 make BUILD_DIR=build_wsl_firefox kernel-only -j1
```

Compile only apps:

```bash
nice -n 19 ionice -c3 make BUILD_DIR=build_wsl_firefox initrd-overlay.img -j1
```

Rebuild ISO:

```bash
nice -n 19 ionice -c3 make BUILD_DIR=build_wsl_firefox iso-from-existing-initrd -j1
```

---

## VirtualBox

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\boot-virtualbox.ps1 `
  -IsoPath .\build_wsl_firefox\RiduxOS-Unix.iso `
  -VmName RiduxOS_Unix_Demo `
  -MemoryMB 2048 `
  -CpuCount 2 `
  -CpuExecutionCap 55 `
  -BootSeconds 35
```

---

## Notes

- UI is functional but still evolving
- Running real browsers is a long-term goal
- SMP and GPU drivers are still in progress

---

## Philosophy

Small changes. Test often. Keep it maintainable.
