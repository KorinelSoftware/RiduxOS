# RiduxOS: Wayfire integration path

Goal: use Wayfire as RiduxOS' main desktop/compositor instead of maintaining a
full custom desktop shell from scratch.

Wayfire is a smaller and more hackable target than a complete KDE Plasma
session. It still gives Ridux a real Wayland compositor path, wlroots-based
window management, plugins, effects and a professional base we can customize.

## Current integration

- `wayfire check` reports whether the Wayfire payload, wlroots stack, Linux ABI,
  DRM/input paths and runtime libraries are visible inside RiduxOS.
- The Ridux kernel launches `/opt/wayfire/bin/wayfire` directly through the
  Linux ABI when the payload exists.
- `wayfire run` remains available as a manual/debug command inside RiduxOS.
- `/etc/autoboot.cmd` intentionally does not run Wayfire anymore, because the
  kernel owns the compositor startup and we do not want two Wayfire instances.
- `desktop-shell-r3.elf` remains available only as a fallback/debug UI.
- The safe initrd overlay includes `/opt/wayfire`, `/etc/wayfire` and the
  Wayfire home config automatically when `make wayfire-rootfs` has staged them.
- If no Wayfire payload is staged yet, boot falls back to the native Ring 3
  shell instead of leaving Ridux with a blank desktop.
- `make wayfire-source` clones editable sources from WayfireWM.
- `make wayfire-build` applies patches from `patches/wayfire/...` and builds
  with Meson/Ninja.
- `make wayfire-rootfs` stages the build into `/opt/wayfire` in the rootfs.

## Commands

```sh
make wayfire-source
make wayfire-build
make wayfire-rootfs
```

Or all together:

```sh
make wayfire-desktop
```

Then:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\ridux-ui-safe.ps1 -Iso -Boot -AllowHeavy
```

Inside RiduxOS:

```sh
wayfire check
wayfire run
wayfire run shell
```

## What must become real

- DRM/KMS device behavior for wlroots, not just framebuffer stubs.
- libinput/evdev events for keyboard and mouse.
- session/seat behavior for libseat or a compatible direct backend.
- GBM/EGL/GLES2 later; current default prefers Pixman first so Wayfire can come
  up through the existing compatibility layer without hard GPU assumptions.
- Wayland socket lifecycle owned by the compositor.
- XWayland later, if we want Firefox/Chromium/X11 apps under Wayfire.

## Wayfire requirement matrix

This is the checklist I am using so the work does not turn into random syscall
whack-a-mole.

| Area | What Wayfire/wlroots expects | Ridux status |
| --- | --- | --- |
| DRM/KMS | Open `/dev/dri/card0`, modeset, queue page-flip events | Basic path works, Wayfire reaches 1024x768 DRM present |
| Renderer | Pixman or GLES/EGL renderer | Pixman path boots; EGL/GBM is later |
| Seat/session | A usable seat provider and udev device metadata | Direct/virtual seat works enough for Wayfire to start |
| Input | `evdev` + `libinput` for keyboard and mouse | Devices are visible; real event routing still needs validation |
| Wayland socket | `AF_UNIX`, bind/listen/accept, peer wakeups | Wayfire creates `/run/user/1000/wayland-1` and clients connect |
| Event loop | `epoll`, nested epoll, `poll`/`ppoll`, timers/eventfd | Nested epoll false-HUP was fixed; remaining wait/wakeup is under trace |
| FD passing | `sendmsg`/`recvmsg` with `SCM_RIGHTS` | Now traced specifically for Wayfire clients |
| Shared buffers | `memfd_create`, `ftruncate`, `mmap MAP_SHARED` | Shared file-page path exists; now traced for Wayland memfd buffers |
| Shell | `wf-background`, `wf-panel`, `wf-dock` | They launch and talk Wayland, but surfaces are still not visible |
| Apps | GTK/Wayland clients like Thunar | Blocked until Wayland shell surfaces render reliably |

## Current blocker

Wayfire itself starts. The old Ridux desktop is no longer the active UI in the
safe Wayfire boot. The black screen happens after `wf-background`, `wf-dock` and
`wf-panel` connect and start the Wayland handshake.

The next thing to prove is whether the first fd-bearing Wayland messages are
delivered correctly:

- Wayfire sends protocol bytes plus an attached fd.
- The client receives the fd via `SCM_RIGHTS`.
- The client maps the memfd with `MAP_SHARED`.
- The compositor sees the same shared pages when the client commits a surface.

If any one of those four steps is fake, the result is exactly what we see:
Wayfire is alive, but nothing useful appears on screen.

## Why Wayfire first

Wayfire is based on wlroots and is modular, so it gives us a real compositor
without needing the whole KDE Frameworks and Qt desktop stack immediately.
