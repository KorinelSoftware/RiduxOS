#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TARGETS = (
    REPO_ROOT / "rootfs/usr/lib/x86_64-linux-gnu/libglib-2.0.so.0",
    REPO_ROOT / "rootfs/usr/lib/x86_64-linux-gnu/libglib-2.0.so.0.8400.4",
)
GOBJECT_TARGETS = (
    REPO_ROOT / "rootfs/usr/lib/x86_64-linux-gnu/libgobject-2.0.so.0",
    REPO_ROOT / "rootfs/usr/lib/x86_64-linux-gnu/libgobject-2.0.so.0.8400.4",
)

G_STRERROR_OFF = 0x82A00
UNKNOWN_ERROR_VADDR = 0xC2A48

ORIGINAL_PREFIX = bytes.fromhex(
    "f30f1efa"
    "4157"
    "4156"
    "4155"
    "4c8d2d37430d00"
    "4154"
    "55"
    "53"
    "4881ec28040000"
)


def make_patch() -> bytes:
    lea_addr = G_STRERROR_OFF + 4
    next_rip = lea_addr + 7
    disp = UNKNOWN_ERROR_VADDR - next_rip
    if not -(1 << 31) <= disp < (1 << 31):
        raise RuntimeError("g_strerror replacement string is out of rel32 range")
    return (
        bytes.fromhex("f30f1efa")  # endbr64
        + b"\x48\x8d\x05"
        + disp.to_bytes(4, "little", signed=True)
        + b"\xc3"
        + b"\x90" * (len(ORIGINAL_PREFIX) - 12)
    )


def patch_one(path: Path) -> None:
    if not path.exists():
        print(f"[glib-patch] skip missing {path.relative_to(REPO_ROOT)}")
        return

    data = bytearray(path.read_bytes())
    patch = make_patch()
    current = bytes(data[G_STRERROR_OFF : G_STRERROR_OFF + len(patch)])

    if current == patch:
        print(f"[glib-patch] already patched {path.relative_to(REPO_ROOT)}")
        return

    if current != ORIGINAL_PREFIX:
        raise SystemExit(
            f"[glib-patch] unexpected g_strerror bytes in {path.relative_to(REPO_ROOT)}"
        )

    data[G_STRERROR_OFF : G_STRERROR_OFF + len(patch)] = patch
    path.write_bytes(data)
    print(f"[glib-patch] patched {path.relative_to(REPO_ROOT)}")


def patch_gobject_one(path: Path) -> None:
    if not path.exists():
        print(f"[gobject-patch] skip missing {path.relative_to(REPO_ROOT)}")
        return

    data = bytearray(path.read_bytes())
    # g_cclosure_marshal/property validation uses a small stack GValue during
    # Chromium/GTK startup.  On Ridux's current ABI this path trips the canary
    # even though Chromium can continue if the validation warning path returns.
    # Keep this scoped to the exact Debian GLib build bytes.
    spots = (
        (0x1C44B, bytes.fromhex("0f853d010000")),
        (0x1C56E, bytes.fromhex("751e")),
        (0x1D3B4, bytes.fromhex("0f85b5010000")),
        (0x1D3DE, bytes.fromhex("0f858b010000")),
        (0x1D686, bytes.fromhex("0f8543010000")),
        (0x1D70C, bytes.fromhex("0f85bd000000")),
        (0x1D789, bytes.fromhex("7544")),
        (0x1DF01, bytes.fromhex("0f85a3030000")),
    )
    changed = False
    for off, expected in spots:
        current = bytes(data[off : off + len(expected)])
        patch = b"\x90" * len(expected)
        if current == patch:
            continue
        if current != expected:
            raise SystemExit(
                f"[gobject-patch] unexpected bytes at 0x{off:x} in {path.relative_to(REPO_ROOT)}"
            )
        data[off : off + len(expected)] = patch
        changed = True

    # Chromium/GTK startup can reach g_object_new_valist() with a param spec
    # whose value_type does not have a referenced value table yet on Ridux.
    # Stock GLib returns NULL from g_type_value_table_peek(); the immediate
    # caller dereferences it at +0x20 and crashes before GLib can recover.
    #
    # Keep the workaround local to this exact Debian libgobject build: at the
    # "about to return NULL" path in g_type_value_table_peek(), retry as
    # G_TYPE_POINTER (0x44). That consumes a single pointer-sized vararg and
    # lets the object construction path continue instead of killing Chromium.
    fallback_off = 0x3DAA5
    fallback_original = bytes.fromhex("4531e4e98b0000000f1f00")
    fallback_patch = bytes.fromhex("bf44000000e90100000090")
    current = bytes(data[fallback_off : fallback_off + len(fallback_patch)])
    if current == fallback_patch:
        pass
    elif current == fallback_original:
        data[fallback_off : fallback_off + len(fallback_patch)] = fallback_patch
        changed = True
    else:
        raise SystemExit(
            f"[gobject-patch] unexpected value-table fallback bytes at 0x{fallback_off:x} "
            f"in {path.relative_to(REPO_ROOT)}"
        )

    # The direct crash observed in Chromium is in g_object_new_valist():
    #
    #   call g_type_value_table_peek
    #   mov  %rax,(%rcx)
    #   mov  0x20(%rax),%rax   <-- NULL deref when value table is unavailable
    #
    # Use the padding between .plt.got and .text as a tiny executable trampoline
    # that retries NULL value tables as G_TYPE_POINTER, then resumes at the
    # original instruction stream after loading value_table->collect_format.
    valist_off = 0x203D8
    valist_original = bytes.fromhex("488b0c2431f6488d7c2450488901")
    valist_patch = bytes.fromhex("e9b304ffff") + b"\x90" * 9
    cave_off = 0x10890
    cave_original = b"\x00" * 48
    cave_patch = bytes.fromhex(
        "488b0c24"          # mov    (%rsp),%rcx
        "4885c0"            # test   %rax,%rax
        "750e"              # jne    have_table
        "bf44000000"        # mov    $G_TYPE_POINTER,%edi
        "e81dd10200"        # call   g_type_value_table_peek
        "488b0c24"          # mov    (%rsp),%rcx
        "31f6"              # have_table: xor %esi,%esi
        "488d7c2450"        # lea    0x50(%rsp),%rdi
        "488901"            # mov    %rax,(%rcx)
        "488b4020"          # mov    0x20(%rax),%rax
        "e930fb0000"        # jmp    g_object_new_valist+0x2fa
    ) + b"\x90" * 6
    current = bytes(data[valist_off : valist_off + len(valist_patch)])
    if current == valist_patch:
        pass
    elif current == valist_original:
        cave_current = bytes(data[cave_off : cave_off + len(cave_patch)])
        if cave_current not in (cave_original, cave_patch):
            raise SystemExit(
                f"[gobject-patch] executable cave not empty at 0x{cave_off:x} "
                f"in {path.relative_to(REPO_ROOT)}"
            )
        data[cave_off : cave_off + len(cave_patch)] = cave_patch
        data[valist_off : valist_off + len(valist_patch)] = valist_patch
        changed = True
    else:
        raise SystemExit(
            f"[gobject-patch] unexpected g_object_new_valist bytes at 0x{valist_off:x} "
            f"in {path.relative_to(REPO_ROOT)}"
        )

    if changed:
        path.write_bytes(data)
        print(f"[gobject-patch] patched {path.relative_to(REPO_ROOT)}")
    else:
        print(f"[gobject-patch] already patched {path.relative_to(REPO_ROOT)}")


def main() -> int:
    for target in TARGETS:
        patch_one(target)
    for target in GOBJECT_TARGETS:
        patch_gobject_one(target)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
