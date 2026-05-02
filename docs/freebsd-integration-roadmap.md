# Ridux + FreeBSD Roadmap

Objective: move Ridux to a FreeBSD kernel base without discarding existing work
from `compat2..compat8`, shell tooling, and diagnostics.

## Technical reality

`kernel.c` cannot be pasted directly into FreeBSD kernel internals. The practical
path is layered integration:

- FreeBSD base: scheduler, VM, VFS, networking, drivers, security.
- Ridux layer: compat runtime, syscall bridge, shell tooling, UX.

## Target architecture

1. `ridux_kmod` in FreeBSD
- bridge entrypoint inside the FreeBSD kernel.
- exposes hooks for tracing, adapters, and runtime state.

2. `ridux_compat` ABI layer
- reuse high-level behavior from `compat3..compat8`.
- route primitives to FreeBSD APIs for VM/proc/fd/net/syscalls.

3. `ridux_userland`
- keep Ridux shell and diagnostics.
- keep browser launcher path (`browser check/run/realrun` equivalent tooling).

## Current status in this repo

- `make freebsd-bootstrap`: fetches `freebsd-src`.
- `make freebsd-prepare`: installs `ridux_kmod` scaffold.
- `make freebsd-compat-snapshot`: snapshots `kernel.c` + `compat*` into `freebsd/ridux-runtime/compat_snapshot`.
- `make freebsd-browser-iso`: builds a customized FreeBSD installer ISO with
  unattended install + first boot provisioning for desktop and browsers.
- `make freebsd-browser-iso-fast`: same installer ISO but fast profile
  (no linux-chrome and no extra apps on first boot).
- `make freebsd-browser-vm`: creates/boots VirtualBox VM for that ISO.
- `make freebsd-browser-vm-quick`: boots existing installed disk directly.
- `make freebsd-live-iso`: builds a no-install live ISO (boots directly from media).
- `make freebsd-ridux-kernel`: builds `KERNCONF=RIDUX` and exports kernel artifact.
- `make freebsd-live-iso-kernel`: live ISO + injected custom RIDUX kernel.
- `make freebsd-live-vm`: boots that live ISO in VirtualBox.
- Runtime bridge inside the ISO:
  - `ridux-browser` (launcher)
  - `ridux-app` (native + linuxulator app manager)
  - `ridux-compat` + aliases (`abi6`, `dynlink`, `mmaps`, `browser run`, etc.)

## Browser-ready path (fast track)

1. Build installer ISO:
- `make freebsd-browser-iso`

2. Boot/install VM:
- `make freebsd-browser-vm`
- for daily reopen after install: `make freebsd-browser-vm-quick`

3. First boot provisioning installs:
- `xorg`, `xinit`, `sdl2`, `firefox`, `chromium`
- optional: `linux_base-rl9`, `linux-chrome`
- plus `ttyv0` autologin (no DM login), direct `Ridux UI`, and helper `ridux-browser`.

## Kernel/compat ownership rule

`src/kernel.c` and `src/compat/*.c` are not discarded by this roadmap.

- Track A: native Ridux kernel/runtime (your core IP).
- Track B: FreeBSD kernel + Ridux runtime bridge for immediate app compatibility.

## Milestones

- H1: Ridux module loads on FreeBSD.
- H2: Ridux diagnostics/compat commands run on FreeBSD.
- H3: ELF64 dynamic runtime path stable on FreeBSD.
- H4: Real Firefox/Chromium usage stable.
- H5: Optional Linux Chrome workflow stable (`linux-chrome` on Linuxulator).
