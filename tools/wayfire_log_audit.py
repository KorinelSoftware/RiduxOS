#!/usr/bin/env python3
"""Small Wayfire boot-log audit for RiduxOS.

This is intentionally boring: it scans the serial log and tells us which parts
of the Wayland/Wayfire path are alive. That makes each boot useful without
having to guess from a screenshot only.
"""

from __future__ import annotations

import argparse
import re
import tarfile
from pathlib import Path


CHECKS = [
    ("kernel_panic", re.compile(r"PANIC|System halted", re.I), False),
    ("wayfire_process", re.compile(r"name=wayfire|\bwayfire\b", re.I), True),
    ("gpu_renderer", re.compile(r"GL renderer:\s+(?:virgl|SVGA3D)\b", re.I), True),
    ("pixman_renderer", re.compile(r"Loading WLR_RENDERER option:\s*pixman|\brenderer[=/]pixman\b|\bWLR_RENDERER=pixman\b", re.I), None),
    ("gpu_ladder_started", re.compile(r"\[ridux-gpu-ladder\] begin", re.I), True),
    ("gpu_ladder_drm", re.compile(r"\[ridux-gpu-ladder\] drm_resources=ok", re.I), True),
    ("gpu_ladder_atomic", re.compile(r"\[ridux-gpu-ladder\] drm_atomic_test_only=ok", re.I), True),
    ("gpu_ladder_syncobj", re.compile(r"\[ridux-gpu-ladder\] drm_syncobj_wait=ok", re.I), True),
    ("gpu_ladder_dbus", re.compile(r"\[ridux-gpu-ladder\] dbus_session_connect=ok", re.I), True),
    ("gpu_ladder_wayland", re.compile(r"\[ridux-gpu-ladder\] wayland_shm_window=ok", re.I), True),
    ("gpu_ladder_overall", re.compile(r"\[ridux-gpu-ladder\] overall=ok", re.I), True),
    ("drm_page_flip_events", re.compile(r"\[drm-event-read\]"), True),
    ("fake_scanout_mirror", re.compile(r"\[virtgpu\] scanout-mirror\b", re.I), False),
    ("wayland_fd_passing", re.compile(r"\[wf-scm-store!?\]|\[wf-scm-recv!?\]"), True),
    ("wayland_shared_buffers", re.compile(r"\[wf-mmap-memfd!?\]|\[wf-memfd!?\]"), True),
    ("ridux_visible_shell_fallback", re.compile(r"ridux-visible-shell: painted|\[wf-(?:memfd|ftruncate|mmap-memfd)!?\].*name=ridux-visible-shell", re.I), False),
    ("full_stack_mode", re.compile(r"desktop mode=wayfire-full-stack", re.I), None),
    ("launcher_wrapper_ready", re.compile(r"ready launcher-wrapper /usr/bin/ridux-open-launcher", re.I), None),
    ("files_wrapper_ready", re.compile(r"ready files-wrapper /usr/bin/ridux-open-files", re.I), None),
    ("terminal_wrapper_ready", re.compile(r"ready terminal-wrapper /usr/bin/ridux-terminal", re.I), None),
    ("background_client", re.compile(r"name=wf-background|\bwf-background\b|name=ridux-background|ridux-background"), True),
    ("panel_client", re.compile(r"name=wf-panel|\bwf-panel\b|name=waybar|\bwaybar\b|name=ridux-panel|\bridux-panel\b", re.I), True),
    ("waybar_panel", re.compile(r"name=waybar|\bwaybar\b", re.I), None),
    ("ridux_panel_fallback", re.compile(r"name=ridux-panel|\bridux-panel\b", re.I), None),
    ("wf_panel_client", re.compile(r"name=wf-panel|\bwf-panel\b", re.I), None),
    ("dock_client", re.compile(r"name=wf-dock|\bwf-dock\b|name=ridux-dock|ridux-dock"), True),
    ("wf_dock_client", re.compile(r"name=wf-dock|\bwf-dock\b", re.I), None),
    ("ridux_dock_fallback", re.compile(r"name=ridux-dock|\bridux-dock\b", re.I), None),
    ("qt_panel_client", re.compile(r"started ridux-qt-panel|Ridux Qt Panel", re.I), None),
    ("qt_dock_client", re.compile(r"started ridux-qt-dock|Ridux Qt Dock", re.I), None),
    ("qt_dashboard_client", re.compile(r"name=ridux-qt-dashboard|\bridux-qt-dashboard\b", re.I), True),
    ("qt_files_client", re.compile(r"name=ridux-qt-files|\bridux-qt-files\b", re.I), True),
    ("qt_monitor_client", re.compile(r"name=ridux-qt-monitor|\bridux-qt-monitor\b", re.I), True),
    ("files_client", re.compile(r"name=thunar|\bthunar\b|name=ridux-files|name=ridux-qt-files|\bridux-qt-files\b", re.I), True),
    ("pipewire_service", re.compile(r"name=pipewire|\bpipewire\b", re.I), True),
    ("wireplumber_service", re.compile(r"name=wireplumber|\bwireplumber\b", re.I), True),
    ("portal_service", re.compile(r"xdg-desktop-portal", re.I), True),
    ("swaync_service", re.compile(r"name=swaync|\bswaync\b", re.I), True),
    ("identity_heal", re.compile(r"\[kernel-identity-heal\]"), False),
]


def checks_for_renderer(renderer: str, require_full_stack: bool = False) -> list[tuple[str, re.Pattern[str], bool | None]]:
    checks = []
    for name, pattern, expected in CHECKS:
        adjusted = expected
        if renderer == "pixman":
            if name == "gpu_renderer":
                adjusted = False
            elif name == "pixman_renderer":
                adjusted = True
            elif name.startswith("gpu_ladder_"):
                adjusted = None
            elif name == "waybar_panel":
                adjusted = True
        elif renderer == "gpu":
            if name == "pixman_renderer":
                adjusted = False
        if require_full_stack and name in {"background_client", "panel_client", "dock_client", "waybar_panel", "wf_dock_client"}:
            adjusted = True
        elif require_full_stack and name in {"qt_files_client", "files_client"}:
            adjusted = None
        if require_full_stack and name in {"qt_panel_client", "qt_dock_client"}:
            adjusted = False
        if require_full_stack and name in {
            "qt_dashboard_client",
            "qt_monitor_client",
        }:
            adjusted = None
        checks.append((name, pattern, adjusted))
    return checks

IMPORTANT_EXIT_NAMES = {
    "ridux-session",
    "ridux-panel",
    "ridux-qt-dashboard",
    "ridux-qt-files",
    "ridux-qt-monitor",
    "ridux-visible-shell",
    "wayfire",
    "waybar",
    "wofi",
    "wf-panel",
    "wf-dock",
    "wf-background",
    "thunar",
    "pipewire",
    "wireplumber",
    "xdg-desktop-portal",
    "xdg-desktop-portal-wlr",
    "swaync",
    "foot",
    "wlogout",
    "wdisplays",
}

OVERLAY_CHECKS = [
    ("visible_shell_binary", "opt/wayfire/bin/ridux-visible-shell", None),
    ("gpu_ladder_binary", "opt/wayfire/bin/ridux-gpu-ladder", None),
    ("qt_freeguard_library", "opt/wayfire/lib/ridux-qt-freeguard.so", None),
    ("qt_dashboard_binary", "opt/wayfire/bin/ridux-qt-dashboard", None),
    ("qt_files_binary", "opt/wayfire/bin/ridux-qt-files", None),
    ("qt_monitor_binary", "opt/wayfire/bin/ridux-qt-monitor", None),
    ("baked_wayfire_shell_plugin", "mnt/c/Users/SEBA/Downloads/RiduxOS/third_party/wayfire/install/lib/wayfire/libwayfire-shell.so", None),
    ("baked_wayfire_shell_xml", "mnt/c/Users/SEBA/Downloads/RiduxOS/third_party/wayfire/install/share/wayfire/metadata/wayfire-shell.xml", None),
    ("ridux_panel_binary", "opt/wayfire/bin/ridux-panel", None),
    ("ridux_dock_binary", "opt/wayfire/bin/ridux-dock", None),
    ("wf_dock_binary", "opt/wayfire/bin/wf-dock", None),
    ("waybar_binary", "usr/bin/waybar", None),
    ("launcher_wrapper_file", "usr/bin/ridux-open-launcher", None),
    ("files_wrapper_file", "usr/bin/ridux-open-files", None),
    ("terminal_wrapper_file", "usr/bin/ridux-terminal", None),
    ("power_menu_wrapper_file", "usr/bin/ridux-power-menu", None),
    ("display_settings_wrapper_file", "usr/bin/ridux-display-settings", None),
    ("wofi_binary", "usr/bin/wofi", None),
    ("foot_binary", "usr/bin/foot", None),
    ("wlogout_binary", "usr/bin/wlogout", None),
    ("wdisplays_binary", "usr/bin/wdisplays", None),
    ("lock_wrapper_file", "usr/bin/ridux-lock", None),
    ("swaylock_binary", "usr/bin/swaylock", None),
    ("screenshot_wrapper_file", "usr/bin/ridux-screenshot", None),
    ("grim_binary", "usr/bin/grim", None),
    ("slurp_binary", "usr/bin/slurp", None),
    ("wayfire_command_plugin", "etc/wayfire/wayfire.ini", re.compile(r"^plugins = .*\bcommand\b", re.M)),
    ("wayfire_resize_plugin", "etc/wayfire/wayfire.ini", re.compile(r"^plugins = .*\bresize\b", re.M)),
    ("wayfire_launcher_binding", "etc/wayfire/wayfire.ini", re.compile(r"^command_launcher = /usr/bin/ridux-open-launcher$", re.M)),
    ("wayfire_terminal_binding", "etc/wayfire/wayfire.ini", re.compile(r"^command_terminal = /usr/bin/ridux-terminal$", re.M)),
    ("wayfire_lock_binding", "etc/wayfire/wayfire.ini", re.compile(r"^command_lock = /usr/bin/ridux-lock$", re.M)),
    ("wayfire_screenshot_binding", "etc/wayfire/wayfire.ini", re.compile(r"^command_screenshot = /usr/bin/ridux-screenshot$", re.M)),
    ("full_stack_marker", "etc/ridux-wayfire-full-stack.enable", None),
]


def count(pattern: re.Pattern[str], text: str) -> int:
    return sum(1 for _ in pattern.finditer(text))


def audit_important_exits(text: str, fail_on_missing: bool) -> bool:
    failed = False
    exit_re = re.compile(r"\[(?:exit|task-exit)!\]\s+pid=\d+\s+code=(\d+)\s+(?:survivors=\d+\s+)?name=([^\s]+)", re.I)
    seen: dict[str, list[int]] = {}
    for match in exit_re.finditer(text):
        code = int(match.group(1))
        name = match.group(2)
        if name in IMPORTANT_EXIT_NAMES and code != 0:
            seen.setdefault(name, []).append(code)
    for name in sorted(IMPORTANT_EXIT_NAMES):
        codes = seen.get(name, [])
        n = len(codes)
        state = "ok" if n == 0 else "check"
        detail = "" if n == 0 else " codes=" + ",".join(str(code) for code in codes[:6])
        print(f"[wayfire-audit] {state:5} {'exit_' + name:24} {n}{detail}")
        if n and fail_on_missing:
            failed = True
    return failed


def normalize_member(name: str) -> str:
    while name.startswith("./"):
        name = name[2:]
    return name.lstrip("/")


def audit_overlay(path: Path, fail_on_missing: bool) -> bool:
    failed = False
    print(f"[wayfire-audit] overlay: {path}")
    try:
        with tarfile.open(path, "r:*") as tar:
            members = {normalize_member(member.name): member for member in tar.getmembers()}
            for name, member_name, pattern in OVERLAY_CHECKS:
                member = members.get(member_name)
                n = 0
                if member is not None:
                    if pattern is None:
                        n = 1
                    else:
                        handle = tar.extractfile(member)
                        if handle is not None:
                            text = handle.read().decode(errors="replace")
                            n = 1 if pattern.search(text) else 0
                ok = n > 0
                state = "ok" if ok else "check"
                print(f"[wayfire-audit] {state:5} {name:24} {n}")
                if not ok and fail_on_missing:
                    failed = True
    except (OSError, tarfile.TarError) as exc:
        print(f"[wayfire-audit] check overlay_read_error        {exc}")
        if fail_on_missing:
            failed = True
    return failed


def main() -> int:
    parser = argparse.ArgumentParser(description="Audit a RiduxOS Wayfire serial log.")
    parser.add_argument("log", nargs="?", default="build_wayfire_safe/vbox-wayfire-safe.log")
    parser.add_argument("--overlay", default=None)
    parser.add_argument("--renderer", choices=("auto", "gpu", "pixman"), default="auto")
    parser.add_argument("--require-full-stack", action="store_true")
    parser.add_argument("--fail-on-panic", action="store_true")
    parser.add_argument("--fail-on-missing", action="store_true")
    args = parser.parse_args()

    path = Path(args.log)
    if not path.exists():
        print(f"[wayfire-audit] missing log: {path}")
        return 2

    text = path.read_text(errors="replace")
    print(f"[wayfire-audit] log: {path}")
    print(f"[wayfire-audit] size: {path.stat().st_size} bytes")

    failed = False
    renderer = args.renderer
    if renderer == "auto":
        if "ridux-wayfire-pixman.enable" in text or re.search(r"WLR_RENDERER option:\s*pixman", text, re.I):
            renderer = "pixman"
        else:
            renderer = "gpu"
    print(f"[wayfire-audit] renderer-mode: {renderer}")

    for name, pattern, expected in checks_for_renderer(renderer, args.require_full_stack):
        n = count(pattern, text)
        if expected is None:
            ok = True
            state = "note"
        else:
            ok = (n > 0) if expected else (n == 0)
            state = "ok" if ok else "check"
        print(f"[wayfire-audit] {state:5} {name:24} {n}")
        if name == "kernel_panic" and n and args.fail_on_panic:
            failed = True
        if expected is True and n == 0 and args.fail_on_missing:
            failed = True

    failed = audit_important_exits(text, args.fail_on_missing) or failed

    overlay = Path(args.overlay) if args.overlay else path.parent / "initrd-overlay.img"
    if overlay.exists():
        failed = audit_overlay(overlay, args.fail_on_missing) or failed
    else:
        print(f"[wayfire-audit] check overlay_missing          {overlay}")
        if args.fail_on_missing:
            failed = True

    if "kernel_panic" not in text and "[kernel-identity-heal]" in text:
        print("[wayfire-audit] note: identity-heal avoided a crash, but the root paging bug still exists.")

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
