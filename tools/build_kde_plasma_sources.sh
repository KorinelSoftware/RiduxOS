#!/usr/bin/env bash
set -euo pipefail

KDE_DIR="${1:-third_party/kde}"
CONFIG="$KDE_DIR/kde-builder.yaml"
PY_STUB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/kde_python_stubs"
REAL_BUILD="${RIDUX_KDE_REAL_BUILD:-0}"

if [[ ! -f "$CONFIG" ]]; then
  echo "[kde-build] missing config: $CONFIG" >&2
  echo "[kde-build] Run first: make kde-source" >&2
  exit 2
fi

if ! head -n 1 "$CONFIG" | grep -qx "config-version: 2"; then
  echo "[kde-build] kde-builder config must start with: config-version: 2" >&2
  exit 3
fi

if ! command -v kde-builder >/dev/null 2>&1; then
  echo "[kde-build] kde-builder not found." >&2
  echo "[kde-build] Install KDE's kde-builder, then retry." >&2
  exit 4
fi

export PYTHONPATH="$PY_STUB_DIR${PYTHONPATH:+:$PYTHONPATH}"

if [[ "$REAL_BUILD" = "1" || "$REAL_BUILD" = "true" ]]; then
  echo "[kde-build] REAL build: qt6-set -> workspace"
  kde-builder --rc-file "$CONFIG" qt6-set
  kde-builder --rc-file "$CONFIG" workspace
else
  echo "[kde-build] Pretend mode only. Set RIDUX_KDE_REAL_BUILD=1 for the long build."
  kde-builder --rc-file "$CONFIG" --pretend qt6-set
  kde-builder --rc-file "$CONFIG" --pretend workspace
fi
