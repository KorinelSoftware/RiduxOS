#!/bin/sh
#
# ridux-compat: Ridux compatibility / introspection bridge.
#
# This script lives at /usr/local/bin/ridux-compat on the installed
# system, and the first-boot installer creates symlinks for a number
# of friendly aliases (abi6, dynlink, mmaps, browser, ...). The same
# script handles all of them by inspecting $0 / $1.
#
# Each alias is intentionally read-only: it answers a question, it does
# not change kernel state. The Ridux UI uses these to populate its
# Diagnostics panel; advanced users invoke them directly from the
# Ridux shell.

set -eu

invocation_name() {
    name="${0##*/}"
    case "$name" in
        ridux-compat) printf '%s\n' "${1:-help}" ;;
        *)            printf '%s\n' "$name" ;;
    esac
}

cmd="$(invocation_name "$@")"
case "${0##*/}" in
    ridux-compat)
        shift 2>/dev/null || true
        ;;
esac

print_section() {
    printf '\n=== %s ===\n' "$1"
}

abi_status() {
    print_section "Ridux Linux ABI"
    echo "kernel : $(uname -mrs)"
    echo "uname  : $(uname -a)"
    echo "linuxulator modules:"
    kldstat 2>/dev/null | grep -i linux || echo "  (none)"
    echo "linux_enable: $(sysrc -n linux_enable 2>/dev/null || echo unset)"
    echo "/compat/linux: $(test -d /compat/linux && echo present || echo MISSING)"
    if [ -d /compat/linux ]; then
        echo "/compat/linux usr: $(ls -1 /compat/linux/usr 2>/dev/null | head -3 | xargs)"
    fi
}

case "$cmd" in
    abi6|abi)
        abi_status
        ;;
    pidfd|io_uring|statx|sysplus|threads|elf64|pmm|tasks|paging|shm|timers|fds|realsys|mmaps|procfs|dynlink|libc|dlsym|heap|bsd|b64|rng)
        echo "[$cmd] backed by FreeBSD-native kernel + Linuxulator"
        echo "[$cmd] this alias is informational. The underlying ABI surface"
        echo "[$cmd] is provided by the Ridux kernel; see 'ridux-compat abi6'"
        echo "[$cmd] for module / runtime status."
        ;;
    lsblk)
        geom disk list 2>/dev/null || df -h ;;
    route)
        netstat -rn ;;
    mount)
        mount ;;
    lsdev)
        pciconf -lv 2>/dev/null | head -40 ;;
    catproc)
        if [ -d /compat/linux/proc ]; then
            ls -la /compat/linux/proc | head -40
        else
            echo "/compat/linux/proc not present (try 'ridux-app ensure-linux')"
        fi
        ;;
    syscalls)
        echo "Use 'ktrace -t fcsi -p <pid>' then 'kdump' to inspect syscall traces"
        echo "Linux ABI syscalls go through linux64.ko"
        ;;
    ss)
        sockstat -4 -l 2>/dev/null || netstat -an
        ;;
    browser)
        sub="${1:-help}"
        case "$sub" in
            check)
                /usr/local/bin/ridux-app doctor
                ;;
            run)
                target="${2:-firefox}"
                exec /usr/local/bin/ridux-browser "$target"
                ;;
            list)
                for b in firefox chromium google-chrome; do
                    if command -v "$b" >/dev/null 2>&1; then
                        echo "  $b -> $(command -v $b)"
                    fi
                done
                ;;
            *)
                echo "Usage: browser check|run <name>|list"
                ;;
        esac
        ;;
    help|-h|--help|"")
        cat <<EOF
ridux-compat: Ridux compatibility / introspection bridge

Aliases (each invokes the same script via symlink in /usr/local/bin):
  abi6, statx, pidfd, io_uring, sysplus, threads, elf64, pmm, tasks,
  paging, shm, timers, fds, realsys, mmaps, procfs, dynlink, libc,
  dlsym, heap, bsd, b64, rng

System helpers:
  lsblk     disk geometry summary
  route     routing table
  mount     mounted filesystems
  lsdev     PCI device list (pciconf -lv)
  catproc   /compat/linux/proc listing
  ss        socket statistics
  syscalls  hint for syscall tracing
  browser check | run <name> | list

Direct invocation:
  ridux-compat <subcommand>     same as the alias name
EOF
        ;;
    *)
        echo "ridux-compat: unknown subcommand '$cmd'" >&2
        echo "Run 'ridux-compat help' for the list of aliases." >&2
        exit 2
        ;;
esac
