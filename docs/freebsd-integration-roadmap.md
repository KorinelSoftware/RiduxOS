# Ridux + FreeBSD Roadmap

Objective: move Ridux to a FreeBSD kernel base by building RiduxBSD directly
from `third_party/upstream/freebsd-src`, without discarding existing work from
`compat2..compat8`, shell tooling, and diagnostics.

## Technical reality

`kernel.c` cannot be pasted directly into FreeBSD kernel internals. The practical
path is layered integration:

- FreeBSD source base: scheduler, VM, VFS, networking, drivers, security.
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
- `make freebsd-prepare`: installs `ridux_kmod`, `KERNCONF=RIDUX`, and
  RiduxBSD kernel branding (`TYPE=RiduxBSD`, `BRANCH=RIDUX`).
- `make riduxbsd-world`: builds `buildworld` + `buildkernel KERNCONF=RIDUX`
  from the modified FreeBSD source tree.
- `make riduxbsd-iso`: builds the source-based RiduxBSD release ISO at
  `build/RiduxOS-RIDUXBSD-amd64.iso`.
- `make riduxbsd-clean-live-artifacts`: removes old patched live ISO/cache
  artifacts that are no longer the main path.
- `make freebsd-compat-snapshot`: snapshots `kernel.c` + `compat*` into `freebsd/ridux-runtime/compat_snapshot`.
- Legacy patched live ISO targets are disabled in the Makefile. Use
  `make riduxbsd-iso` instead.
- Runtime bridge inside the ISO:
  - `ridux-browser` (launcher)
  - `ridux-app` (native + linuxulator app manager)
  - `ridux-compat` + aliases (`abi6`, `dynlink`, `mmaps`, `browser run`, etc.)

## Source-built RiduxBSD path

1. Prepare the source tree:
- `make freebsd-bootstrap`
- `make freebsd-prepare`

2. Build world/kernel from source:
- `make riduxbsd-world`

3. Build release ISO:
- `make riduxbsd-iso`

The full release ISO step is FreeBSD-native. On Windows/WSL, use a FreeBSD
builder VM or host mounted on this repo; experimental Linux cross-build is
guarded behind `RIDUX_ALLOW_LINUX_CROSS=yes`.

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
