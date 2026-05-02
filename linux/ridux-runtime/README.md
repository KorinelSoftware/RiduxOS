# Ridux Runtime Layer (Debian/Linux Track)

This track keeps Ridux identity while using a minimal Linux base for fast app
compatibility.

It does **not** replace `src/kernel.c` or `src/compat/*.c`.

- Track A: your native Ridux kernel/runtime.
- Track C: Debian/Linux runtime bridge for immediate app compatibility.

## Components

- `ridux-browser.sh`: unified launcher (Chrome/Chromium/Firefox + Discord fallback).
- `ridux-app.sh`: app manager using `apt` (with optional Chrome/Discord helpers).
- `ridux-compat.sh`: Ridux-style command bridge on Linux (`abi6`, `dynlink`, `mmaps`, etc.).
- `ridux-ui.c`: X11 desktop shell using `ridux-flush` (userspace Flush renderer) inspired by the old Flush-era Ridux UI.
- `ridux-flush.c` + `ridux-flush.h`: userspace Flush queue + software raster primitives (`rect`, `round_rect`, `gradient`, `shadow`, `noise`, `text`).

## Setup

Use:

- `tools/setup_debian_ridux_runtime.sh`

That script installs runtime files, compiles `ridux-ui`, configures tty autologin
and starts Ridux UI via `startx`.

For a portable no-installer image, use:

- `tools/build_debian_live_ridux_iso.sh`
