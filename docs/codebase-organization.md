# RiduxOS Codebase Organization

Ridux keeps its own kernel. BSD and FreeBSD code are reference material or
selectively imported compatibility pieces; they are not the base kernel.

## Current Boundaries

- `src/kernel/`: Ridux kernel runtime, boot flow, scheduler/VFS glue, window
  manager integration, and shell-side kernel services.
- `src/compat/base.*`: shared compatibility types, constants, and shell command
  registration helpers.
- `src/compat/memory_tasks.*`: task/memory helpers used by the Linux ABI and
  user-mode runtime.
- `src/compat/linux_syscalls.*`: Linux syscall dispatch, file descriptor
  semantics, mmap, futex/eventfd/poll, and loader-facing kernel calls.
- `src/compat/linux_app_profiles.*`: application-specific runtime profiles and
  environment shaping for shells, Qt, browsers, and desktop stacks.
- `src/compat/user_libc.c`: micro-libc, stdio, stdlib, pthread/TLS, dl stubs,
  dlsym table, and compat shell commands.
- `src/compat/user_libc_drm.inc`: internal DRM/GPU include extracted from
  `user_libc.c`. It still compiles inside the same translation unit while the
  static helper graph is untangled.
- `src/compat/bsd_libc.*`, `src/freebsd_compat/`, `src/linuxulator/`: BSD and
  Linuxulator compatibility imports/shims.
- `src/compat/display_wayland.*` and `src/compat/browser_runtime.*`: higher
  level display/runtime compatibility for Wayland/browser paths.

## DRM/GPU Split Target

The extracted `user_libc_drm.inc` should be split into real modules in this
order:

1. `src/compat/drm/drm_uapi.h`: DRM constants and public UAPI structs.
2. `src/compat/drm/drm_core.c`: file ownership, handles, GEM BO tracking,
   PRIME fd tracking, mmap resolution, and common tracing.
3. `src/compat/drm/drm_kms.c`: modes, connectors, CRTCs, planes, properties,
   EDID/HDR/color management, page flip helpers.
4. `src/compat/drm/virtgpu.c`: virtio-gpu PCI/MMIO probing, queue setup,
   resource creation, capsets, transfers, and virgl submit path.
5. `src/compat/drm/vmsvga.c`: VMSVGA/vmwgfx command buffers, surfaces,
   fences, 2D/3D present, and VirtualBox-specific rescue paths.
6. `src/compat/drm/present.c`: high-level present selection and CPU fallback.
7. `src/compat/drm/wayland_shims.c`: wlroots/Wayfire protocol shims currently
   embedded near the DRM path.

Keep each split compiling before moving to the next one. The include extraction
is intentionally conservative: it reduces navigation cost now without changing
the kernel ABI or object layout.

## Cleanup Policy

Generated builds, old ISOs, command-output junk, and temporary logs should be
removed with:

```powershell
scripts/clean-workspace.ps1 -Apply
```

By default the script preserves `build_codex_gpu`, the active build directory.
Use `-AlsoDeleteActiveBuild` only when a completely fresh rebuild is intended.

Do not delete `rootfs/lib*`, `rootfs/usr/lib*`, Qt/KDE/Wayfire/Mesa/Vulkan
payloads, or `third_party/` blindly. Those packages are runtime compatibility
inputs and need a manifest/audit pass before pruning.
