#!/usr/bin/env bash
set -euo pipefail

RIDUX_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ -z "${SEL4_PARENT:-}" ]]; then
  if [[ "$RIDUX_ROOT" == /mnt/* ]]; then
    SEL4_PARENT="/var/tmp/ridux-sel4"
  else
    SEL4_PARENT="${RIDUX_ROOT}/third_party/sel4"
  fi
fi
SEL4_TREE="${SEL4_PARENT}/camkes-vm-examples"
SEL4_BIN="${SEL4_PARENT}/bin"
SEL4_VENV="${SEL4_PARENT}/venv"
SEL4_PYUSER="${SEL4_PARENT}/pyuser"
BUILD_DIR="${SEL4_TREE}/build-x86_64-browser"
MANIFEST_URL="${MANIFEST_URL:-https://github.com/seL4/camkes-vm-examples-manifest.git}"
REPO_TOOL_URL="${REPO_TOOL_URL:-https://storage.googleapis.com/git-repo-downloads/repo}"
GET_PIP_URL="${GET_PIP_URL:-https://bootstrap.pypa.io/get-pip.py}"
CAMKES_VM_APP="${CAMKES_VM_APP:-minimal_64}"

usage() {
  cat <<'EOF'
Usage:
  tools/bootstrap_sel4_browser_vm.sh <info|deps|fetch|build|stage>

Commands:
  info    Print the Ridux Browser VM architecture and local paths.
  deps    Prepare local no-sudo helper tools when possible.
  fetch   Fetch seL4 CAmkES VM examples into third_party/sel4.
  build   Build the x86_64 Linux guest VMM example.
  stage   Copy built seL4 VM boot images into build/browser-vm.

Environment:
  MANIFEST_URL      seL4 repo manifest URL
  REPO_TOOL_URL     repo tool download URL
  GET_PIP_URL       get-pip.py fallback URL
  CAMKES_VM_APP     CAmkES VM app, default minimal_64
  SEL4_PARENT       source/cache parent (default: /var/tmp/ridux-sel4 on WSL /mnt)

Notes:
  This intentionally builds the smallest official Linux guest first.
  Firefox/Chromium are added after the VMM boots reliably and the Ridux
  framebuffer/input bridge has a stable protocol.
EOF
}

need_cmd() {
  local cmd="$1"
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "[sel4-browser-vm] missing command: $cmd" >&2
    return 1
  fi
}

ensure_repo_tool() {
  local local_repo="${SEL4_BIN}/repo"
  if command -v repo >/dev/null 2>&1; then
    command -v repo
    return 0
  fi
  if [[ -x "$local_repo" ]]; then
    printf '%s\n' "$local_repo"
    return 0
  fi
  need_cmd curl || return 1
  mkdir -p "$SEL4_BIN"
  echo "[sel4-browser-vm] downloading repo tool locally: $local_repo" >&2
  curl -fL "$REPO_TOOL_URL" -o "${local_repo}.part"
  mv "${local_repo}.part" "$local_repo"
  chmod +x "$local_repo"
  printf '%s\n' "$local_repo"
}

ensure_ninja_tool() {
  local local_ninja="${SEL4_BIN}/ninja"
  local deps_dir="${SEL4_PARENT}/deps/ninja"
  if command -v ninja >/dev/null 2>&1; then
    command -v ninja
    return 0
  fi
  if [[ -x "$local_ninja" ]]; then
    printf '%s\n' "$local_ninja"
    return 0
  fi
  if command -v apt-get >/dev/null 2>&1 && command -v dpkg-deb >/dev/null 2>&1; then
    mkdir -p "$deps_dir" "$SEL4_BIN"
    (
      cd "$deps_dir"
      rm -f ninja-build_*.deb
      echo "[sel4-browser-vm] downloading ninja-build locally" >&2
      apt-get download ninja-build >/dev/null
      deb="$(ls ninja-build_*.deb 2>/dev/null | head -n 1)"
      if [[ -z "$deb" ]]; then
        exit 1
      fi
      rm -rf root
      mkdir -p root
      dpkg-deb -x "$deb" root
      cp root/usr/bin/ninja "$local_ninja"
      chmod +x "$local_ninja"
    ) || return 1
    printf '%s\n' "$local_ninja"
    return 0
  fi
  return 1
}

ensure_deb_binary() {
  local package="$1"
  local relpath="$2"
  local tool="$3"
  local local_tool="${SEL4_BIN}/${tool}"
  local deps_dir="${SEL4_PARENT}/deps/${package}"
  if command -v "$tool" >/dev/null 2>&1; then
    command -v "$tool"
    return 0
  fi
  if [[ -x "$local_tool" ]]; then
    printf '%s\n' "$local_tool"
    return 0
  fi
  if command -v apt-get >/dev/null 2>&1 && command -v dpkg-deb >/dev/null 2>&1; then
    mkdir -p "$deps_dir" "$SEL4_BIN"
    (
      cd "$deps_dir"
      rm -f "${package}"_*.deb
      echo "[sel4-browser-vm] downloading ${package} locally" >&2
      apt-get download "$package" >/dev/null
      deb="$(ls "${package}"_*.deb 2>/dev/null | head -n 1)"
      if [[ -z "$deb" ]]; then
        exit 1
      fi
      rm -rf root
      mkdir -p root
      dpkg-deb -x "$deb" root
      cp "root/${relpath}" "$local_tool"
      chmod +x "$local_tool"
    ) || return 1
    printf '%s\n' "$local_tool"
    return 0
  fi
  return 1
}

ensure_host_tools() {
  ensure_deb_binary libxml2-utils usr/bin/xmllint xmllint >/dev/null || return 1
  ensure_deb_binary haskell-stack usr/bin/stack stack >/dev/null || return 1
}

ensure_user_pip() {
  local get_pip="${SEL4_PARENT}/deps/get-pip.py"
  mkdir -p "${SEL4_PARENT}/deps" "$SEL4_PYUSER"
  if PYTHONUSERBASE="$SEL4_PYUSER" python3 -m pip --version >/dev/null 2>&1; then
    return 0
  fi
  need_cmd curl || return 1
  echo "[sel4-browser-vm] bootstrapping pip locally: $SEL4_PYUSER" >&2
  curl -fL "$GET_PIP_URL" -o "${get_pip}.part"
  mv "${get_pip}.part" "$get_pip"
  PYTHONUSERBASE="$SEL4_PYUSER" python3 "$get_pip" --user --break-system-packages >/dev/null || return 1
}

ensure_python_deps() {
  local py
  local py_packages="aenum jinja2 plyplus ply pyyaml six pyelftools ordered-set sortedcontainers hypothesis future pycparser pyfdt concurrencytest lxml psutil beautifulsoup4 sh pexpect jsonschema libarchive-c"
  mkdir -p "$SEL4_PARENT"
  if python3 -m ensurepip --version >/dev/null 2>&1; then
    if [[ ! -x "${SEL4_VENV}/bin/python3" || ! -x "${SEL4_VENV}/bin/pip" ]]; then
      rm -rf "$SEL4_VENV"
      echo "[sel4-browser-vm] creating local Python venv: $SEL4_VENV" >&2
      python3 -m venv "$SEL4_VENV" || return 1
    fi
    py="${SEL4_VENV}/bin/python3"
    "$py" -m pip install --upgrade pip wheel >/dev/null || return 1
    "$py" -m pip install $py_packages >/dev/null || return 1
    "$py" - <<'PY' || return 1
import importlib.util, sys
mods = ["aenum", "jinja2", "plyplus", "ply", "yaml", "six", "elftools", "ordered_set", "sortedcontainers", "hypothesis", "future", "pycparser", "pyfdt", "concurrencytest", "lxml", "psutil", "bs4", "sh", "pexpect", "jsonschema", "libarchive"]
missing = [m for m in mods if importlib.util.find_spec(m) is None]
if missing:
    print("missing python modules: " + ", ".join(missing), file=sys.stderr)
    raise SystemExit(1)
PY
    export PATH="${SEL4_VENV}/bin:$PATH"
    return 0
  fi

  ensure_user_pip || return 1
  PYTHONUSERBASE="$SEL4_PYUSER" python3 -m pip install --user --break-system-packages \
    $py_packages >/dev/null || return 1
  PYTHONUSERBASE="$SEL4_PYUSER" python3 - <<'PY' || return 1
import importlib.util, sys
mods = ["aenum", "jinja2", "plyplus", "ply", "yaml", "six", "elftools", "ordered_set", "sortedcontainers", "hypothesis", "future", "pycparser", "pyfdt", "concurrencytest", "lxml", "psutil", "bs4", "sh", "pexpect", "jsonschema", "libarchive"]
missing = [m for m in mods if importlib.util.find_spec(m) is None]
if missing:
    print("missing python modules: " + ", ".join(missing), file=sys.stderr)
    raise SystemExit(1)
PY
  export PYTHONUSERBASE="$SEL4_PYUSER"
  export PATH="${SEL4_PYUSER}/bin:$PATH"
  return 0
}

cmd_deps() {
  local repo_bin ninja_bin
  repo_bin="$(ensure_repo_tool)" || {
    echo "[sel4-browser-vm] repo tool unavailable" >&2
    exit 3
  }
  ninja_bin="$(ensure_ninja_tool)" || {
    cat >&2 <<'EOF'
[sel4-browser-vm] ninja unavailable and local apt download failed.
Debian/Ubuntu:
  sudo apt-get install -y ninja-build build-essential gcc-multilib g++-multilib
EOF
    exit 3
  }
  ensure_python_deps || {
    cat >&2 <<'EOF'
[sel4-browser-vm] Python dependency setup failed.
Debian/Ubuntu fallback:
  sudo apt-get install -y python3-venv python3-pip
EOF
    exit 3
  }
  ensure_host_tools || {
    cat >&2 <<'EOF'
[sel4-browser-vm] host tool setup failed.
Debian/Ubuntu fallback:
  sudo apt-get install -y libxml2-utils haskell-stack
EOF
    exit 3
  }
  echo "[sel4-browser-vm] repo:  $repo_bin"
  echo "[sel4-browser-vm] ninja: $ninja_bin"
  if [[ -x "${SEL4_VENV}/bin/python3" && -x "${SEL4_VENV}/bin/pip" ]]; then
    echo "[sel4-browser-vm] python venv: $SEL4_VENV"
  else
    echo "[sel4-browser-vm] python user base: $SEL4_PYUSER"
  fi
}

cmd_info() {
  cat <<EOF
[sel4-browser-vm]
  repo root:        ${RIDUX_ROOT}
  seL4 parent:      ${SEL4_PARENT}
  local bin:        ${SEL4_BIN}
  Python venv:      ${SEL4_VENV}
  Python user base: ${SEL4_PYUSER}
  source tree:      ${SEL4_TREE}
  build dir:        ${BUILD_DIR}
  manifest:         ${MANIFEST_URL}
  CAmkES VM app:    ${CAMKES_VM_APP}

Architecture target:
  seL4 kernel + CAmkES VMM
  Linux guest owns real Firefox/Chromium
  Ridux owns window chrome, focus, input policy, and display bridge
  final ISO carries all required kernels, rootfs images, and bridge payloads

Next commands:
  make sel4-browser-vm-bootstrap
  make sel4-browser-vm-build
EOF
}

cmd_fetch() {
  local repo_bin
  repo_bin="$(ensure_repo_tool)" || {
    cat >&2 <<'EOF'
[sel4-browser-vm] Could not install Google's repo tool locally.
Debian/Ubuntu fallback:
  sudo apt-get update
  sudo apt-get install -y repo git python3 cmake ninja-build build-essential curl
EOF
    exit 3
  }
  need_cmd git || exit 3
  need_cmd python3 || exit 3

  mkdir -p "$SEL4_PARENT"
  if [[ ! -d "$SEL4_TREE/.repo" ]]; then
    mkdir -p "$SEL4_TREE"
    cd "$SEL4_TREE"
    "$repo_bin" init -u "$MANIFEST_URL"
  else
    cd "$SEL4_TREE"
  fi
  "$repo_bin" sync
  echo "[sel4-browser-vm] fetched: $SEL4_TREE"
}

cmd_build() {
  local ninja_bin
  need_cmd cmake || exit 3
  ninja_bin="$(ensure_ninja_tool)" || {
    cat >&2 <<'EOF'
[sel4-browser-vm] ninja is required for the seL4 build.
Debian/Ubuntu:
  sudo apt-get install -y ninja-build build-essential gcc-multilib g++-multilib
EOF
    exit 3
  }
  need_cmd python3 || exit 3
  ensure_python_deps || exit 3
  ensure_host_tools || exit 3
  export PATH="${SEL4_BIN}:$PATH"

  if [[ ! -x "$SEL4_TREE/init-build.sh" ]]; then
    echo "[sel4-browser-vm] source tree missing. Run: make sel4-browser-vm-bootstrap" >&2
    exit 4
  fi

  if [[ -f "${BUILD_DIR}/CMakeCache.txt" && ! -f "${BUILD_DIR}/build.ninja" ]]; then
    echo "[sel4-browser-vm] removing incomplete CMake build dir: $BUILD_DIR" >&2
    rm -rf "$BUILD_DIR"
  fi
  mkdir -p "$BUILD_DIR"
  cd "$BUILD_DIR"
  if [[ ! -f build.ninja ]]; then
    ../init-build.sh -DCAMKES_VM_APP="$CAMKES_VM_APP"
  fi
  "$ninja_bin"
  echo "[sel4-browser-vm] build complete"
  find images -maxdepth 1 -type f -print 2>/dev/null || true
  cmd_stage
}

cmd_stage() {
  local out_dir="${RIDUX_ROOT}/build/browser-vm"
  local kernel_img="${BUILD_DIR}/images/kernel-x86_64-pc99"
  local loader_img="${BUILD_DIR}/images/capdl-loader-image-x86_64-pc99"
  if [[ ! -f "$kernel_img" || ! -f "$loader_img" ]]; then
    echo "[sel4-browser-vm] built images missing. Run: make sel4-browser-vm-build" >&2
    exit 4
  fi
  mkdir -p "$out_dir"
  cp -f "$kernel_img" "${out_dir}/kernel-x86_64-pc99"
  cp -f "$loader_img" "${out_dir}/capdl-loader-image-x86_64-pc99"
  cat > "${out_dir}/README.txt" <<EOF
Ridux Browser VM seL4 prototype artifacts

Source tree: ${SEL4_TREE}
Build dir:   ${BUILD_DIR}
App:         ${CAMKES_VM_APP}

GRUB boot files:
  /boot/sel4/kernel-x86_64-pc99
  /boot/sel4/capdl-loader-image-x86_64-pc99
EOF
  echo "[sel4-browser-vm] staged: $out_dir"
}

cmd="${1:-info}"
case "$cmd" in
  info) cmd_info ;;
  deps) cmd_deps ;;
  fetch) cmd_fetch ;;
  build) cmd_build ;;
  stage) cmd_stage ;;
  -h|--help|help) usage ;;
  *)
    echo "[sel4-browser-vm] unknown command: $cmd" >&2
    usage >&2
    exit 2
    ;;
esac
