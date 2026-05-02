#!/bin/sh
set -eu

SELF="$(basename "$0")"
if [ "$SELF" != "ridux-compat" ] && [ "$SELF" != "ridux-compat.sh" ]; then
  set -- "$SELF" "$@"
fi

say() {
  printf '%s\n' "$*"
}

warn() {
  printf 'warning: %s\n' "$*" >&2
}

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    warn "command not available: $1"
    return 1
  fi
  return 0
}

default_pid() {
  printf '%s\n' "${1:-$$}"
}

cmd_help() {
  cat <<'EOF'
Ridux compat bridge (Debian/Linux track)

Ridux-style commands:
  abi6 pidfd io_uring statx sysplus
  lsblk route mount lsdev catproc syscalls
  threads ss elf64 pmm tasks paging shm timers fds
  realsys mmaps procfs dynlink libc dlsym heap bsd b64 rng
  browser help|check|run|realrun [target]

Examples:
  abi6
  browser check chrome
  browser run firefox
  statx /usr/bin/chromium
  dynlink /usr/bin/firefox
  mmaps 1
EOF
}

cmd_abi6() {
  say "ridux abi6 bridge: Linux compatibility summary"
  say "host: $(uname -sr)"
  say "kernel: $(uname -v)"
  if [ -f /etc/os-release ]; then
    say "distro: $(. /etc/os-release; printf '%s %s' "${NAME:-unknown}" "${VERSION_ID:-}")"
  fi
}

cmd_pidfd() {
  say "pidfd bridge status"
  if [ -r /proc/sys/kernel/pid_max ]; then
    say "pid_max=$(cat /proc/sys/kernel/pid_max)"
  fi
  say "tip: use 'strace -e pidfd_open,pidfd_send_signal <cmd>' for runtime checks"
}

cmd_io_uring() {
  say "io_uring bridge status"
  if [ -r /proc/sys/kernel/io_uring_disabled ]; then
    say "io_uring_disabled=$(cat /proc/sys/kernel/io_uring_disabled)"
  else
    say "io_uring sysctl knob not exposed on this host"
  fi
}

cmd_statx() {
  _path="${1:-}"
  [ -n "$_path" ] || {
    warn "usage: statx <path>"
    return 2
  }
  if need_cmd stat; then
    stat "$_path"
  fi
}

cmd_sysplus() {
  say "system summary"
  uname -a
  if [ -f /etc/os-release ]; then
    . /etc/os-release
    say "os=${PRETTY_NAME:-unknown}"
  fi
  if need_cmd lscpu; then
    lscpu | sed -n '1,20p'
  fi
}

cmd_lsblk() {
  if need_cmd lsblk; then
    lsblk -o NAME,SIZE,FSTYPE,TYPE,MOUNTPOINT
  fi
}

cmd_route() {
  if command -v ip >/dev/null 2>&1; then
    ip route show
    return 0
  fi
  if need_cmd netstat; then
    netstat -rn
  fi
}

cmd_mount() {
  mount
}

cmd_lsdev() {
  ls /dev | sed -n '1,200p'
}

cmd_catproc() {
  _path="${1:-/proc/self/status}"
  if [ -f "$_path" ]; then
    cat "$_path"
  else
    warn "path not found: $_path"
    return 1
  fi
}

cmd_syscalls() {
  say "syscall inspection bridge"
  say "Use strace for live tracing:"
  say "  strace -f <command>"
}

cmd_threads() {
  _pid="$(default_pid "${1:-}")"
  if need_cmd ps; then
    ps -T -p "$_pid"
  fi
}

cmd_ss() {
  if command -v ss >/dev/null 2>&1; then
    ss -tulpn
    return 0
  fi
  if need_cmd netstat; then
    netstat -tulpn
  fi
}

cmd_elf64() {
  _bin="${1:-}"
  [ -n "$_bin" ] || {
    warn "usage: elf64 <path-to-binary>"
    return 2
  }
  if need_cmd file; then
    file "$_bin"
  fi
  if need_cmd ldd; then
    ldd "$_bin" || true
  fi
}

cmd_pmm() {
  if [ -f /proc/meminfo ]; then
    sed -n '1,24p' /proc/meminfo
  fi
}

cmd_tasks() {
  ps -e -o pid,ppid,stat,ni,pri,comm
}

cmd_paging() {
  if need_cmd vmstat; then
    vmstat -s
  fi
}

cmd_shm() {
  if need_cmd ipcs; then
    ipcs -m
  else
    warn "ipcs not available"
  fi
}

cmd_timers() {
  _pid="$(default_pid "${1:-}")"
  _path="/proc/${_pid}/timers"
  if [ -f "$_path" ]; then
    cat "$_path"
  else
    warn "timers file not available: $_path"
    return 1
  fi
}

cmd_fds() {
  _pid="$(default_pid "${1:-}")"
  _path="/proc/${_pid}/fd"
  if [ -d "$_path" ]; then
    ls -la "$_path"
  else
    warn "fd path not available: $_path"
    return 1
  fi
}

cmd_realsys() {
  cmd_sysplus
  say
  cmd_abi6
  say
  cmd_ss || true
}

cmd_mmaps() {
  _pid="$(default_pid "${1:-}")"
  _path="/proc/${_pid}/maps"
  if [ -f "$_path" ]; then
    cat "$_path"
  else
    warn "maps file not available: $_path"
    return 1
  fi
}

cmd_procfs() {
  mount | grep ' on /proc ' || say "procfs not mounted"
}

cmd_dynlink() {
  _bin="${1:-}"
  [ -n "$_bin" ] || {
    warn "usage: dynlink <binary>"
    return 2
  }
  if need_cmd ldd; then
    ldd "$_bin"
  fi
}

cmd_libc() {
  if [ -f /lib/x86_64-linux-gnu/libc.so.6 ]; then
    say "/lib/x86_64-linux-gnu/libc.so.6"
  fi
  if [ -f /usr/lib/x86_64-linux-gnu/libc.so.6 ]; then
    say "/usr/lib/x86_64-linux-gnu/libc.so.6"
  fi
  if need_cmd ldd; then
    ldd /bin/sh 2>/dev/null || true
  fi
}

cmd_dlsym() {
  _lib="${1:-}"
  _sym="${2:-}"
  [ -n "$_lib" ] || {
    warn "usage: dlsym <library> <symbol>"
    return 2
  }
  [ -n "$_sym" ] || {
    warn "usage: dlsym <library> <symbol>"
    return 2
  }

  if command -v readelf >/dev/null 2>&1; then
    readelf -Ws "$_lib" | grep -F "$_sym" || true
    return 0
  fi
  if command -v nm >/dev/null 2>&1; then
    nm -D "$_lib" 2>/dev/null | grep -F "$_sym" || true
    return 0
  fi
  warn "neither readelf nor nm available"
  return 1
}

cmd_heap() {
  if [ -f /proc/meminfo ]; then
    grep -E 'MemTotal|MemFree|MemAvailable|SwapTotal|SwapFree' /proc/meminfo
  fi
}

cmd_bsd() {
  say "Ridux Linux track active (not FreeBSD)."
  uname -a
}

cmd_b64() {
  _mode="${1:-enc}"
  _data="${2:-}"
  case "$_mode" in
    enc|encode)
      if [ -n "$_data" ]; then
        printf '%s' "$_data" | base64
      else
        base64
      fi
      ;;
    dec|decode)
      if [ -n "$_data" ]; then
        printf '%s' "$_data" | base64 -d
      else
        base64 -d
      fi
      ;;
    *)
      warn "usage: b64 [enc|dec] [data]"
      return 2
      ;;
  esac
}

cmd_rng() {
  _count="${1:-32}"
  if need_cmd dd && need_cmd hexdump; then
    dd if=/dev/urandom bs=1 count="$_count" 2>/dev/null | hexdump -C
  fi
}

cmd_browser() {
  _sub="${1:-help}"
  _target="${2:-chrome}"
  shift 2 >/dev/null 2>&1 || true

  case "$_sub" in
    help)
      cat <<'EOF'
browser commands:
  browser check [chrome|firefox|chromium|discord]
  browser run [chrome|firefox|chromium|discord] [args...]
  browser realrun [chrome|firefox|chromium|discord] [args...]
EOF
      ;;
    check)
      _cand=""
      case "$_target" in
        chrome|google-chrome)
          _cand="google-chrome-stable google-chrome chromium chromium-browser firefox firefox-esr"
          ;;
        chromium)
          _cand="chromium chromium-browser google-chrome-stable google-chrome firefox firefox-esr"
          ;;
        firefox)
          _cand="firefox firefox-esr"
          ;;
        discord)
          _cand="discord"
          ;;
        *)
          _cand="$_target"
          ;;
      esac
      for _c in $_cand; do
        if command -v "$_c" >/dev/null 2>&1; then
          say "[ok] $_c -> $(command -v "$_c")"
          return 0
        fi
      done
      if [ "$_target" = "discord" ] && command -v flatpak >/dev/null 2>&1 && flatpak info com.discordapp.Discord >/dev/null 2>&1; then
        say "[ok] flatpak:com.discordapp.Discord"
        return 0
      fi
      say "[missing] no executable found for target: $_target"
      return 1
      ;;
    run|realrun)
      if command -v ridux-browser >/dev/null 2>&1; then
        exec ridux-browser "$_target" "$@"
      fi
      exec "$_target" "$@"
      ;;
    *)
      warn "unknown browser subcommand: $_sub"
      return 2
      ;;
  esac
}

cmd="${1:-help}"
shift || true

case "$cmd" in
  help|-h|--help) cmd_help ;;
  abi6) cmd_abi6 "$@" ;;
  pidfd) cmd_pidfd "$@" ;;
  io_uring) cmd_io_uring "$@" ;;
  statx) cmd_statx "$@" ;;
  sysplus) cmd_sysplus "$@" ;;
  lsblk) cmd_lsblk "$@" ;;
  route) cmd_route "$@" ;;
  mount) cmd_mount "$@" ;;
  lsdev) cmd_lsdev "$@" ;;
  catproc) cmd_catproc "$@" ;;
  syscalls) cmd_syscalls "$@" ;;
  threads) cmd_threads "$@" ;;
  ss) cmd_ss "$@" ;;
  elf64) cmd_elf64 "$@" ;;
  pmm) cmd_pmm "$@" ;;
  tasks) cmd_tasks "$@" ;;
  paging) cmd_paging "$@" ;;
  shm) cmd_shm "$@" ;;
  timers) cmd_timers "$@" ;;
  fds) cmd_fds "$@" ;;
  realsys) cmd_realsys "$@" ;;
  mmaps) cmd_mmaps "$@" ;;
  procfs) cmd_procfs "$@" ;;
  dynlink) cmd_dynlink "$@" ;;
  libc) cmd_libc "$@" ;;
  dlsym) cmd_dlsym "$@" ;;
  heap) cmd_heap "$@" ;;
  bsd) cmd_bsd "$@" ;;
  b64) cmd_b64 "$@" ;;
  rng) cmd_rng "$@" ;;
  browser) cmd_browser "$@" ;;
  *)
    warn "unknown command: $cmd"
    cmd_help
    exit 2
    ;;
esac
