#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UPSTREAM_DIR="$ROOT_DIR/third_party/upstream"

if [[ ! -d "$UPSTREAM_DIR" ]]; then
  echo "[license-guard] no upstream dir found: $UPSTREAM_DIR"
  echo "[license-guard] run: make vendor-upstream"
  exit 2
fi

forbidden='(GNU GENERAL PUBLIC LICENSE|LESSER GENERAL PUBLIC LICENSE|AFFERO GENERAL PUBLIC LICENSE)'
allowed='(MIT|BSD|APACHE LICENSE|APACHE-2|ISC LICENSE|ZLIB)'

status=0

echo "[license-guard] scanning $UPSTREAM_DIR"
for comp in "$UPSTREAM_DIR"/*; do
  [[ -d "$comp" ]] || continue
  name="$(basename "$comp")"

  lic_file="$(find "$comp" -maxdepth 2 -type f \( -iname 'LICENSE*' -o -iname 'COPYING*' -o -iname 'NOTICE*' \) | head -n1 || true)"
  if [[ -z "$lic_file" ]]; then
    echo "[license-guard] FAIL $name -> no license file found"
    status=1
    continue
  fi

  upper="$(tr '[:lower:]' '[:upper:]' < "$lic_file")"
  if grep -Eq "$allowed" <<<"$upper"; then
    if grep -Eq "$forbidden" <<<"$upper"; then
      echo "[license-guard] WARN $name -> allowlisted license + GPL text found in $(basename "$lic_file"), revisar manualmente"
    else
      echo "[license-guard] OK   $name -> $(basename "$lic_file")"
    fi
  elif grep -Eq "$forbidden" <<<"$upper"; then
    echo "[license-guard] FAIL $name -> forbidden GPL/LGPL/AGPL text in $(basename "$lic_file")"
    status=1
  else
    echo "[license-guard] WARN $name -> unknown license signature in $(basename "$lic_file")"
  fi
done

if [[ "$status" -ne 0 ]]; then
  echo "[license-guard] blocked"
  exit "$status"
fi

echo "[license-guard] pass"
