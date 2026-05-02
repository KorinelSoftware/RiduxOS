#!/usr/bin/env bash
set -euo pipefail

FREEBSD_SRC="${1:-third_party/upstream/freebsd-src}"
RIDUX_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEMPLATE_DIR="$RIDUX_ROOT/freebsd/ridux-kmod"
PATCH_SRC="$RIDUX_ROOT/third_party/upstream/freebsd-src"
RIDUX_MODULE_LINE=$'\tridux \\'

need_file() {
  local path="$1"
  if [[ ! -f "$path" ]]; then
    echo "[freebsd-prepare] missing required file: $path" >&2
    exit 3
  fi
}

copy_file() {
  local src="$1"
  local dst="$2"
  local src_abs dst_abs

  src_abs="$(cd "$(dirname "$src")" && pwd)/$(basename "$src")"
  dst_abs="$(cd "$(dirname "$dst")" && pwd)/$(basename "$dst")"
  if [[ "$src_abs" == "$dst_abs" ]]; then
    return 0
  fi
  cp "$src" "$dst"
}

insert_after_pattern() {
  local file="$1"
  local pattern="$2"
  local line="$3"
  local marker="$4"

  if grep -Fq "$marker" "$file"; then
    return 0
  fi

  awk -v pattern="$pattern" -v line="$line" '
    {
      print $0
      if (!added && $0 ~ pattern) {
        print line
        added = 1
      }
    }
    END {
      if (!added) {
        print line
      }
    }
  ' "$file" > "$file.new"
  mv "$file.new" "$file"
}

if [[ ! -d "$FREEBSD_SRC" ]]; then
  echo "[freebsd-prepare] FreeBSD source dir not found: $FREEBSD_SRC" >&2
  echo "[freebsd-prepare] run: make freebsd-bootstrap" >&2
  exit 2
fi

need_file "$TEMPLATE_DIR/ridux_kmod.c"
need_file "$TEMPLATE_DIR/Makefile.module"
need_file "$TEMPLATE_DIR/ridux.h"
need_file "$TEMPLATE_DIR/RIDUX.kernconf"

need_file "$PATCH_SRC/usr.bin/ridux-browser/ridux-browser.c"
need_file "$PATCH_SRC/usr.bin/ridux-browser/Makefile"
need_file "$PATCH_SRC/usr.bin/ridux-compat/ridux-compat.c"
need_file "$PATCH_SRC/usr.bin/ridux-compat/Makefile"
need_file "$PATCH_SRC/usr.bin/ridux-shell/ridux-shell.c"
need_file "$PATCH_SRC/usr.bin/ridux-shell/Makefile"
need_file "$PATCH_SRC/usr.sbin/riduxd/riduxd.c"
need_file "$PATCH_SRC/usr.sbin/riduxd/Makefile"
need_file "$PATCH_SRC/lib/libflush/flush.c"
need_file "$PATCH_SRC/lib/libflush/flush.h"
need_file "$PATCH_SRC/lib/libflush/Makefile"

echo "[freebsd-prepare] installing Ridux source-level integration into $FREEBSD_SRC"

mkdir -p "$FREEBSD_SRC/sys/dev/ridux"
mkdir -p "$FREEBSD_SRC/sys/modules/ridux"
mkdir -p "$FREEBSD_SRC/sys/sys"
mkdir -p "$FREEBSD_SRC/sys/amd64/conf"

mkdir -p "$FREEBSD_SRC/usr.bin/ridux-browser"
mkdir -p "$FREEBSD_SRC/usr.bin/ridux-compat"
mkdir -p "$FREEBSD_SRC/usr.bin/ridux-shell"
mkdir -p "$FREEBSD_SRC/usr.sbin/riduxd"
mkdir -p "$FREEBSD_SRC/lib/libflush"

copy_file "$TEMPLATE_DIR/ridux_kmod.c" "$FREEBSD_SRC/sys/dev/ridux/ridux_kmod.c"
copy_file "$TEMPLATE_DIR/Makefile.module" "$FREEBSD_SRC/sys/modules/ridux/Makefile"
copy_file "$TEMPLATE_DIR/ridux.h" "$FREEBSD_SRC/sys/sys/ridux.h"
copy_file "$TEMPLATE_DIR/RIDUX.kernconf" "$FREEBSD_SRC/sys/amd64/conf/RIDUX"

copy_file "$PATCH_SRC/usr.bin/ridux-browser/ridux-browser.c" "$FREEBSD_SRC/usr.bin/ridux-browser/ridux-browser.c"
copy_file "$PATCH_SRC/usr.bin/ridux-browser/Makefile" "$FREEBSD_SRC/usr.bin/ridux-browser/Makefile"
copy_file "$PATCH_SRC/usr.bin/ridux-compat/ridux-compat.c" "$FREEBSD_SRC/usr.bin/ridux-compat/ridux-compat.c"
copy_file "$PATCH_SRC/usr.bin/ridux-compat/Makefile" "$FREEBSD_SRC/usr.bin/ridux-compat/Makefile"
copy_file "$PATCH_SRC/usr.bin/ridux-shell/ridux-shell.c" "$FREEBSD_SRC/usr.bin/ridux-shell/ridux-shell.c"
copy_file "$PATCH_SRC/usr.bin/ridux-shell/Makefile" "$FREEBSD_SRC/usr.bin/ridux-shell/Makefile"
copy_file "$PATCH_SRC/usr.sbin/riduxd/riduxd.c" "$FREEBSD_SRC/usr.sbin/riduxd/riduxd.c"
copy_file "$PATCH_SRC/usr.sbin/riduxd/Makefile" "$FREEBSD_SRC/usr.sbin/riduxd/Makefile"

copy_file "$PATCH_SRC/lib/libflush/flush.c" "$FREEBSD_SRC/lib/libflush/flush.c"
copy_file "$PATCH_SRC/lib/libflush/flush.h" "$FREEBSD_SRC/lib/libflush/flush.h"
copy_file "$PATCH_SRC/lib/libflush/Makefile" "$FREEBSD_SRC/lib/libflush/Makefile"

if [[ -f "$FREEBSD_SRC/sys/modules/Makefile" ]] && ! grep -Fq "ridux \\" "$FREEBSD_SRC/sys/modules/Makefile"; then
  insert_after_pattern "$FREEBSD_SRC/sys/modules/Makefile" '^[[:space:]]*rc4[[:space:]]*\\$' "$RIDUX_MODULE_LINE" "ridux \\"
fi

if [[ -f "$FREEBSD_SRC/usr.bin/Makefile" ]]; then
  insert_after_pattern "$FREEBSD_SRC/usr.bin/Makefile" '^[[:space:]]*renice[[:space:]]*\\$' $'\tridux-browser \\' "ridux-browser \\"
  insert_after_pattern "$FREEBSD_SRC/usr.bin/Makefile" '^[[:space:]]*ridux-browser[[:space:]]*\\$' $'\tridux-compat \\' "ridux-compat \\"
  insert_after_pattern "$FREEBSD_SRC/usr.bin/Makefile" '^[[:space:]]*ridux-compat[[:space:]]*\\$' $'\tridux-shell \\' "ridux-shell \\"
fi

if [[ -f "$FREEBSD_SRC/usr.sbin/Makefile" ]]; then
  insert_after_pattern "$FREEBSD_SRC/usr.sbin/Makefile" '^[[:space:]]*rarpd[[:space:]]*\\$' $'\triduxd \\' "riduxd \\"
fi

if [[ -f "$FREEBSD_SRC/lib/Makefile" ]]; then
  insert_after_pattern "$FREEBSD_SRC/lib/Makefile" '^[[:space:]]*libfetch[[:space:]]*\\$' $'\tlibflush \\' "libflush \\"
fi

echo "[freebsd-prepare] done."
echo "[freebsd-prepare] next in FreeBSD tree:"
echo "  cd $FREEBSD_SRC"
echo "  make buildworld"
echo "  make buildkernel KERNCONF=RIDUX"
