#!/usr/bin/env bash

log_manifest() {
  printf '%s\n' "$*" >> "$MANIFEST"
}

copy_deps() {
  local bin="$1"
  command -v ldd >/dev/null 2>&1 || return 0
  ldd "$bin" 2>/dev/null | awk '
    $1 ~ /^\// { print $1 }
    $3 ~ /^\// { print $3 }
  ' | sort -u | while read -r dep; do
    [ -f "$dep" ] || continue
    mkdir -p "$ROOT$(dirname "$dep")"
    cp -L "$dep" "$ROOT$dep" 2>/dev/null || true
  done
}

copy_tree_if_exists() {
  local src="$1"
  local dst="$2"
  if [ -d "$src" ]; then
    mkdir -p "$dst"
    cp -a "$src"/. "$dst"/ 2>/dev/null || true
    log_manifest "tree: $src -> ${dst#$ROOT}"
  fi
}

copy_file_with_deps() {
  local src="$1"
  local dst="$2"
  [ -e "$src" ] || return 0
  mkdir -p "$ROOT$(dirname "$dst")"
  cp -aL "$src" "$ROOT$dst" 2>/dev/null || true
  [ -f "$src" ] && copy_deps "$src"
  log_manifest "file: $src -> $dst"
}

copy_glob_materialized() {
  local src_dir="$1"
  local dst_dir="$2"
  local pattern="$3"
  local src dst
  [ -d "$src_dir" ] || return 0
  mkdir -p "$dst_dir"
  for src in "$src_dir"/$pattern; do
    [ -e "$src" ] || continue
    dst="$dst_dir/$(basename "$src")"
    rm -f "$dst"
    cp -Lf "$src" "$dst" 2>/dev/null || true
    if [ ! -s "$dst" ]; then
      rm -f "$dst"
      log_manifest "empty-skip: $src"
      continue
    fi
    chmod 0644 "$dst" 2>/dev/null || true
    [ -f "$src" ] && copy_deps "$src"
    log_manifest "materialized: $src -> ${dst#$ROOT}"
  done
}

materialize_soname() {
  local soname="$1"
  local src=""
  local d candidate base tmp
  for d in \
    "$ROOT/usr/lib/x86_64-linux-gnu" \
    "$ROOT/lib/x86_64-linux-gnu" \
    "$ROOT/lib64" \
    /usr/lib/x86_64-linux-gnu \
    /lib/x86_64-linux-gnu \
    /usr/lib \
    /lib; do
    [ -d "$d" ] || continue
    if [ -e "$d/$soname" ]; then
      src="$d/$soname"
      break
    fi
    candidate="$(find "$d" -maxdepth 1 -type f -name "$soname.*" | sort | tail -n 1 || true)"
    if [ -n "$candidate" ]; then
      src="$candidate"
      break
    fi
  done
  [ -n "$src" ] || {
    log_manifest "missing-soname: $soname"
    return 0
  }
  for d in "$ROOT/usr/lib/x86_64-linux-gnu" "$ROOT/lib64"; do
    mkdir -p "$d"
    tmp="$d/.$soname.ridux-tmp"
    rm -f "$tmp"
    cp -Lf "$src" "$tmp" 2>/dev/null && mv -f "$tmp" "$d/$soname" || rm -f "$tmp"
  done
  [ -f "$src" ] && copy_deps "$src"
  base="$(basename "$src")"
  log_manifest "soname: $soname <- $base"
}

materialize_needed_for() {
  local bin="$1"
  [ -f "$bin" ] || return 0
  command -v readelf >/dev/null 2>&1 || return 0
  readelf -d "$bin" 2>/dev/null | sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p' |
    while IFS= read -r soname; do
      [ -n "$soname" ] && materialize_soname "$soname"
    done
}

install_cmd() {
  local name="$1"
  local dst="${2:-/usr/bin/$name}"
  local src
  src="$(command -v "$name" 2>/dev/null || true)"
  if [ -z "$src" ] && [ -x "/usr/libexec/$name" ]; then
    src="/usr/libexec/$name"
  fi
  if [ -z "$src" ] && [ -x "/usr/bin/$name" ]; then
    src="/usr/bin/$name"
  fi
  if [ -n "$src" ] && [ -x "$src" ]; then
    mkdir -p "$ROOT$(dirname "$dst")"
    cp -L "$src" "$ROOT$dst"
    chmod 0755 "$ROOT$dst"
    copy_deps "$src"
    log_manifest "binary: $name -> $dst"
  else
    log_manifest "missing: $name"
  fi
}
