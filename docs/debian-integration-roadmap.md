# Ridux + Debian Fast Track

Objective: keep Ridux identity and speed up real app compatibility on a minimal
Linux base.

## Why this track exists

- Linux-first desktop apps (Discord/Chrome/Firefox) are easiest on Linux.
- Debian netinst gives a small base and fast boot.
- Ridux UI/runtime can run directly over tty autologin + startx.

## Target architecture

1. Debian minimal base (netinst)
- stable kernel/userspace + package ecosystem.

2. Ridux runtime layer
- `ridux-browser`, `ridux-app`, `ridux-compat`, `ridux-ui`.

3. Fast UX flow
- tty1 autologin
- `.profile` -> `startx`
- `.xinitrc` -> `ridux-ui`

## Repo commands

- `make debian-netinst-iso`
- `make debian-runtime-seed-iso`
- `make debian-vm`
- `make debian-vm-quick`
- `make debian-live-ridux-iso`
- `make debian-live-ridux-vm`

## First install flow

1. Build ISO assets:
- `make debian-netinst-iso`
- `make debian-runtime-seed-iso`

2. Boot VM installer:
- `make debian-vm`

3. Inside Debian after installation:
- `sudo bash /media/cdrom/setup_debian_ridux_runtime.sh --user ridux --fast`
- reboot

4. Daily use:
- `make debian-vm-quick`

## Ownership rule

- Track A: `src/kernel.c` + `src/compat/*.c` remain your core kernel IP.
- Track C: Debian runtime bridge is for immediate app compatibility.

## Live ISO mode (no installer / no login)

Use `tools/build_debian_live_ridux_iso.sh` to build a Debian live image that:

- boots directly into Ridux UI
- skips installer flow
- uses tty1 autologin + startx
- includes Firefox ESR and Chromium (plus optional Chrome hook)
