#!/usr/bin/env bash
set -euo pipefail

RIDUX_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

remove_target() {
  local rel="$1"
  local target="${RIDUX_ROOT}/${rel}"
  local resolved parent

  if [[ ! -e "$target" ]]; then
    return 0
  fi

  parent="$(cd "$(dirname "$target")" && pwd)"
  resolved="${parent}/$(basename "$target")"
  case "$resolved" in
    "$RIDUX_ROOT"/build/RiduxOS-Live.iso|\
    "$RIDUX_ROOT"/build/RiduxOS-Live-Fast.iso|\
    "$RIDUX_ROOT"/build/RiduxOS-FreeBSD-Wayfire.iso|\
    "$RIDUX_ROOT"/third_party/cache/freebsd-iso|\
    "$RIDUX_ROOT"/third_party/cache/freebsd-pkgs)
      rm -rf "$resolved"
      echo "[riduxbsd-clean] removed $resolved"
      ;;
    *)
      echo "[riduxbsd-clean] refusing unexpected path: $resolved" >&2
      exit 3
      ;;
  esac
}

remove_target "build/RiduxOS-Live.iso"
remove_target "build/RiduxOS-Live-Fast.iso"
remove_target "build/RiduxOS-FreeBSD-Wayfire.iso"
remove_target "third_party/cache/freebsd-iso"
remove_target "third_party/cache/freebsd-pkgs"
