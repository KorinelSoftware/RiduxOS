#!/usr/bin/env bash
# Populate common library search paths in the initrd rootfs so that
# ld-linux-x86-64.so.2 (running in ring 3 on Ridux Track A) finds the
# glibc / pthread / libm / libdl / librt / ld itself at the canonical
# paths Debian userland checks. We also drop an empty ld.so.cache so
# openat() for that file returns success instead of ENOENT and ld
# moves straight to the /lib* search paths.
set -euo pipefail

ROOT="$(dirname "$0")/../rootfs"
cd "$ROOT"

mkdir -p lib64 lib/x86_64-linux-gnu usr/lib/x86_64-linux-gnu usr/lib64 etc

LIBS=(
  libc.so.6
  libpthread.so.0
  libdl.so.2
  libm.so.6
  librt.so.1
  libresolv.so.2
  libgcc_s.so.1
  ld-linux-x86-64.so.2
)

# usr/lib/x86_64-linux-gnu is the master; duplicate into /lib64 and
# /lib/x86_64-linux-gnu (tar in initrd doesn't preserve symlinks the
# way we want so we copy).
for f in "${LIBS[@]}"; do
  if [ -e "usr/lib/x86_64-linux-gnu/$f" ]; then
    cp -n "usr/lib/x86_64-linux-gnu/$f" "lib/x86_64-linux-gnu/$f" 2>/dev/null || true
    cp -n "usr/lib/x86_64-linux-gnu/$f" "lib64/$f"                 2>/dev/null || true
    cp -n "usr/lib/x86_64-linux-gnu/$f" "usr/lib64/$f"             2>/dev/null || true
  fi
done

# Ensure /etc/ld.so.cache exists (empty) so openat() succeeds on it.
: >etc/ld.so.cache

echo "[libs] lib64:"
ls lib64/
echo "[libs] lib/x86_64-linux-gnu:"
ls lib/x86_64-linux-gnu/
echo "[libs] usr/lib64:"
ls usr/lib64/
echo "[libs] ld.so.cache exists: $(stat -c '%n %s bytes' etc/ld.so.cache)"
