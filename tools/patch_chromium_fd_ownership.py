#!/usr/bin/env python3
from pathlib import Path


BINARIES = (
    Path("rootfs/opt/chromium/chrome"),
    Path("rootfs/opt/chromium/chrome_crashpad_handler"),
)

# Chromium's Linux non-component builds enable base::subtle FD ownership
# enforcement. Ridux's Linux ABI is still maturing around fork/exec/SCM_RIGHTS,
# so Chromium subprocesses can trip debug/security hard-crashes before
# rendering. Keep these binary patches intentionally narrow and signature based.
FD_OWNERSHIP_SIGNATURE = bytes.fromhex(
    "f3 0f 1e fa"          # endbr64
    "55 48 89 e5"          # push rbp; mov rsp, rbp
    "53"                   # push rbx
    "48 81 ec d8 07 00 00" # sub rsp, 0x7d8
)
FD_OWNERSHIP_PATCH = bytes.fromhex(
    "f3 0f 1e fa"          # endbr64
    "c3"                   # ret
    "90 90 90"             # padding
)

SCOPED_CLOSE_EBADF_SIGNATURE = bytes.fromhex(
    "83 f8 04"             # cmp eax, EINTR
    "74 05"                # je success
    "83 f8 09"             # cmp eax, EBADF
    "74 1b"                # je fatal PCHECK
    "64 48 8b 04 25 28 00 00 00"
    "48 3b 45 f0"
)
SCOPED_CLOSE_EBADF_PATCHED = bytes.fromhex(
    "83 f8 04"
    "74 05"
    "83 f8 09"
    "90 90"                # ignore EBADF too; close() was still called
    "64 48 8b 04 25 28 00 00 00"
    "48 3b 45 f0"
)

# Newer optimized Debian/Chromium builds use a larger inlined close helper:
#   cmp eax, EBADF
#   je  fatal_pcheck
#   mov dword ptr [rbx+8], -1
# Keep the close() call, but remove the fatal jump so Chromium can keep going
# while Ridux's FD ownership semantics are still converging with Linux.
SCOPED_CLOSE_EBADF_LONG_SIGNATURE = bytes.fromhex(
    "83 f8 09"
    "0f 84 9c 00 00 00"
    "c7 43 08 ff ff ff ff"
)
SCOPED_CLOSE_EBADF_LONG_PATCHED = bytes.fromhex(
    "83 f8 09"
    "90 90 90 90 90 90"
    "c7 43 08 ff ff ff ff"
)

SCOPED_CLOSE_ENTRY = bytes.fromhex(
    "f3 0f 1e fa"          # endbr64
    "55 48 89 e5"          # push rbp; mov rsp, rbp
    "53"                   # push rbx
    "48 83 ec 18"          # sub rsp, 0x18
)
SCOPED_CLOSE_ENTRY_PATCH = bytes.fromhex(
    "f3 0f 1e fa"          # endbr64
    "c3"                   # ret
    "90 90 90 90 90 90 90 90"
)

SCOPED_CLOSE_LONG_ENTRY = bytes.fromhex(
    "f3 0f 1e fa"          # endbr64
    "55 48 89 e5"          # push rbp; mov rsp, rbp
    "41 56"                # push r14
    "53"                   # push rbx
    "48 81 ec a0 00 00 00" # sub rsp, 0xa0
    "64 48 8b 04 25 28 00 00 00"
    "48 89 45 e8"
    "83 7f 08 ff"          # cmp dword ptr [rdi+8], -1
)
SCOPED_CLOSE_LONG_ENTRY_PATCH = bytes.fromhex(
    "f3 0f 1e fa"
    "c3"
    "90 90 90 90 90 90 90 90 90 90 90 90 90"
    "90 90 90 90 90 90 90 90 90 90 90 90 90"
    "90 90 90 90 90 90"
)

SCOPED_CLOSE_FATAL_BUILDER = bytes.fromhex(
    "f3 0f 1e fa"          # endbr64
    "55 48 89 e5"          # push rbp; mov rsp, rbp
    "41 57"                # push r15
    "41 56"                # push r14
    "53"                   # push rbx
    "48 83 ec 28"          # sub rsp, 0x28
    "48 89 fb"             # mov rdi, rbx
    "64 48 8b 04 25 28 00 00 00"
    "48 89 45 e0"
    "48 8d 35 11 00 89 f8" # lea ../../base/files/scoped_file.cc(%rip), rsi
    "48 8d 7d c0"          # lea -0x40(%rbp), rdi
    "ba 2d 00 00 00"       # line 45
)

# The optimized build keeps a cold scoped_file.cc fatal block next to the
# close body. Some Chromium re-exec children can still branch there even after
# the public close entry is patched, usually with stale errno text from a
# previous ENOENT. If reached, return through the same frame shape instead of
# constructing LogMessageFatal and aborting the child before the renderer path
# has a chance to draw.
SCOPED_CLOSE_COLD_FATAL_SIGNATURE = bytes.fromhex(
    "48 8d 5d e8"          # lea -0x18(%rbp), %rbx
    "48 89 df"             # mov %rbx, %rdi
    "e8 17 00 00 00"       # call scoped_file fatal message builder
    "48 89 df"             # mov %rbx, %rdi
    "e8 ef 1b ff ff"       # call LogMessageFatal destructor / abort path
)
SCOPED_CLOSE_COLD_FATAL_PATCH = bytes.fromhex(
    "48 83 c4 18"          # add $0x18, %rsp
    "5b"                   # pop %rbx
    "5d"                   # pop %rbp
    "c3"                   # ret
    "90 90 90 90 90 90 90 90 90 90 90 90 90"
)

# Chromium 147's optimized build keeps the same scoped_file.cc fatal block,
# but next to the larger inlined close body used by the long entry above:
#
#   lea -0xb0(%rbp), %rbx
#   call LogMessage builder
#   call LogMessageFatal destructor / abort path
#
# The enclosing frame for this variant is:
#   push rbp; push r14; push rbx; sub rsp, 0xa0
SCOPED_CLOSE_COLD_FATAL_LONG_SIGNATURE = bytes.fromhex(
    "48 8d 9d 50 ff ff ff"
    "48 89 df"
    "e8 a4 c7 f3 ff"
    "48 89 df"
    "e8 7c e3 f2 ff"
)
SCOPED_CLOSE_COLD_FATAL_LONG_PATCH = bytes.fromhex(
    "48 81 c4 a0 00 00 00" # add $0xa0, %rsp
    "5b"                   # pop %rbx
    "41 5e"                # pop %r14
    "5d"                   # pop %rbp
    "c3"                   # ret
    "90 90 90 90 90 90 90 90 90 90 90"
)


def patch_unique(
    data: bytearray,
    binary: Path,
    name: str,
    signature: bytes,
    patch: bytes,
    *,
    required: bool = True,
) -> bool:
    idx = data.find(signature)
    if idx < 0:
        # Optional signatures can legitimately be absent from helper binaries.
        # Do not use a loose "patch in data" check here: several Chromium
        # functions start with the same endbr64/ret prefix after patching.
        if required and patch in data:
            print(f"[chromium-fd-patch] already patched {binary}: {name}")
            return False
        if required:
            raise RuntimeError(f"signature not found for {binary}: {name}; Chromium changed?")
        print(f"[chromium-fd-patch] signature absent in {binary}: {name}, skipping")
        return False

    if data.find(signature, idx + 1) >= 0:
        raise RuntimeError(f"signature for {binary}: {name} is not unique")

    data[idx:idx + len(patch)] = patch
    print(f"[chromium-fd-patch] patched {binary}: {name} at 0x{idx:x}")
    return True


def patch_scoped_fd_close_entry(data: bytearray, binary: Path) -> bool:
    body_idx = data.find(SCOPED_CLOSE_EBADF_PATCHED)
    if body_idx < 0:
        body_idx = data.find(SCOPED_CLOSE_EBADF_SIGNATURE)
    if body_idx < 0:
        if SCOPED_CLOSE_ENTRY_PATCH in data:
            print(f"[chromium-fd-patch] already patched {binary}: ScopedFDCloseTraits::Free entry")
            return False
        raise RuntimeError(f"ScopedFDCloseTraits::Free body not found for {binary}")

    entry_idx = body_idx - 0x38
    if entry_idx < 0 or data[entry_idx:entry_idx + len(SCOPED_CLOSE_ENTRY)] != SCOPED_CLOSE_ENTRY:
        if data[entry_idx:entry_idx + len(SCOPED_CLOSE_ENTRY_PATCH)] == SCOPED_CLOSE_ENTRY_PATCH:
            print(f"[chromium-fd-patch] already patched {binary}: ScopedFDCloseTraits::Free entry")
            return False
        if SCOPED_CLOSE_ENTRY_PATCH in data or SCOPED_CLOSE_LONG_ENTRY_PATCH in data:
            print(f"[chromium-fd-patch] already patched {binary}: ScopedFDCloseTraits::Free entry")
            return False
        raise RuntimeError(f"ScopedFDCloseTraits::Free entry mismatch for {binary}")

    data[entry_idx:entry_idx + len(SCOPED_CLOSE_ENTRY_PATCH)] = SCOPED_CLOSE_ENTRY_PATCH
    print(f"[chromium-fd-patch] patched {binary}: ScopedFDCloseTraits::Free entry at 0x{entry_idx:x}")
    return True


def patch_inlined_scoped_fd_ebadf_branches(data: bytearray, binary: Path) -> bool:
    builder_idx = data.find(SCOPED_CLOSE_FATAL_BUILDER)
    if builder_idx < 0:
        print(f"[chromium-fd-patch] signature absent in {binary}: ScopedFD fatal builder, skipping")
        return False
    if data.find(SCOPED_CLOSE_FATAL_BUILDER, builder_idx + 1) >= 0:
        raise RuntimeError(f"ScopedFD fatal builder signature for {binary} is not unique")

    # Chromium's executable text LOAD is laid out with p_vaddr = p_offset + 0x1000
    # in this Debian-style PIE. The check below is intentionally guarded by the
    # exact scoped_file.cc helper signature above; if Chromium changes layout, the
    # patch becomes a no-op instead of touching unrelated branches.
    vma_delta = 0x1000
    builder_vma = builder_idx + vma_delta

    def target_block_calls_builder(target_idx: int) -> bool:
        if target_idx < 0 or target_idx >= len(data):
            return False
        end = min(len(data) - 5, target_idx + 64)
        i = target_idx
        while i <= end:
            if data[i] == 0xE8:
                rel = int.from_bytes(data[i + 1:i + 5], "little", signed=True)
                call_vma = i + vma_delta
                if call_vma + 5 + rel == builder_vma:
                    return True
            i += 1
        return False

    changed = False
    patched = 0
    n = len(data)
    i = 0
    while True:
        i = data.find(b"\x83\xf8\x09", i)
        if i < 0 or i + 9 >= n:
            break
        if data[i + 3] == 0x74:
            rel = int.from_bytes(data[i + 4:i + 5], "little", signed=True)
            target = i + 5 + rel
            if target_block_calls_builder(target):
                data[i + 3:i + 5] = b"\x90\x90"
                changed = True
                patched += 1
                i += 5
                continue
        if data[i + 3:i + 5] == b"\x0f\x84":
            rel = int.from_bytes(data[i + 5:i + 9], "little", signed=True)
            target = i + 9 + rel
            if target_block_calls_builder(target):
                data[i + 3:i + 9] = b"\x90\x90\x90\x90\x90\x90"
                changed = True
                patched += 1
                i += 9
                continue
        i += 1

    if patched:
        print(f"[chromium-fd-patch] patched {binary}: {patched} inlined ScopedFD EBADF fatal branches")
    else:
        print(f"[chromium-fd-patch] already patched {binary}: inlined ScopedFD EBADF fatal branches")
    return changed


def patch_inlined_scoped_fd_fallthrough_fatals(data: bytearray, binary: Path) -> bool:
    builder_idx = data.find(SCOPED_CLOSE_FATAL_BUILDER)
    if builder_idx < 0:
        print(f"[chromium-fd-patch] signature absent in {binary}: ScopedFD fatal builder, skipping")
        return False
    if data.find(SCOPED_CLOSE_FATAL_BUILDER, builder_idx + 1) >= 0:
        raise RuntimeError(f"ScopedFD fatal builder signature for {binary} is not unique")

    vma_delta = 0x1000
    builder_vma = builder_idx + vma_delta

    def fallthrough_calls_builder(start_idx: int, stop_idx: int) -> bool:
        if start_idx < 0 or stop_idx <= start_idx:
            return False
        end = min(len(data) - 5, stop_idx, start_idx + 96)
        i = start_idx
        while i <= end:
            if data[i] == 0xE8:
                rel = int.from_bytes(data[i + 1:i + 5], "little", signed=True)
                call_vma = i + vma_delta
                if call_vma + 5 + rel == builder_vma:
                    return True
            i += 1
        return False

    changed = False
    patched = 0
    n = len(data)
    i = 0
    while True:
        i = data.find(b"\x83\xf8\x09", i)
        if i < 0 or i + 9 >= n:
            break

        # Some inlined close paths are arranged as:
        #   cmp eax, EBADF
        #   jne success
        #   <scoped_file.cc:45 fatal>
        # For Ridux's current Chromium compatibility route we want every close
        # failure to fall through as non-fatal, so make that branch unconditional.
        if data[i + 3] == 0x75:
            rel = int.from_bytes(data[i + 4:i + 5], "little", signed=True)
            target = i + 5 + rel
            if fallthrough_calls_builder(i + 5, target):
                data[i + 3] = 0xEB
                changed = True
                patched += 1
                i += 5
                continue

        if data[i + 3:i + 5] == b"\x0f\x85":
            rel = int.from_bytes(data[i + 5:i + 9], "little", signed=True)
            target = i + 9 + rel
            if fallthrough_calls_builder(i + 9, target):
                jmp_idx = i + 3
                new_rel = target - (jmp_idx + 5)
                data[jmp_idx] = 0xE9
                data[jmp_idx + 1:jmp_idx + 5] = int(new_rel).to_bytes(4, "little", signed=True)
                data[jmp_idx + 5] = 0x90
                changed = True
                patched += 1
                i += 9
                continue

        i += 1

    if patched:
        print(f"[chromium-fd-patch] patched {binary}: {patched} inlined ScopedFD fallthrough fatal branches")
    else:
        print(f"[chromium-fd-patch] already patched {binary}: inlined ScopedFD fallthrough fatal branches")
    return changed


def patch_branches_to_scoped_fd_fatal_blocks(data: bytearray, binary: Path) -> bool:
    builder_idx = data.find(SCOPED_CLOSE_FATAL_BUILDER)
    if builder_idx < 0:
        print(f"[chromium-fd-patch] signature absent in {binary}: ScopedFD fatal builder, skipping")
        return False
    if data.find(SCOPED_CLOSE_FATAL_BUILDER, builder_idx + 1) >= 0:
        raise RuntimeError(f"ScopedFD fatal builder signature for {binary} is not unique")

    vma_delta = 0x1000
    builder_vma = builder_idx + vma_delta

    def target_block_calls_builder(target_idx: int) -> bool:
        if target_idx < 0 or target_idx >= len(data):
            return False
        end = min(len(data) - 5, target_idx + 96)
        i = target_idx
        while i <= end:
            if data[i] == 0xE8:
                rel = int.from_bytes(data[i + 1:i + 5], "little", signed=True)
                call_vma = i + vma_delta
                if call_vma + 5 + rel == builder_vma:
                    return True
            i += 1
        return False

    changed = False
    patched = 0
    n = len(data)
    i = 0
    while True:
        i = data.find(b"\x0f", i)
        if i < 0 or i + 6 > n:
            break
        # Near conditional branches used by optimized FD ownership guards:
        #   jne scoped_file.cc:45 fatal block
        # The branch target constructs the scoped_file fatal message; letting
        # execution fall through keeps Chromium's renderer bootstrap alive while
        # Ridux catches up with Linux's exact FD ownership tracking semantics.
        if 0x80 <= data[i + 1] <= 0x8F:
            rel = int.from_bytes(data[i + 2:i + 6], "little", signed=True)
            target = i + 6 + rel
            if target_block_calls_builder(target):
                data[i:i + 6] = b"\x90\x90\x90\x90\x90\x90"
                changed = True
                patched += 1
                i += 6
                continue
        i += 1

    if patched:
        print(f"[chromium-fd-patch] patched {binary}: {patched} ScopedFD guard fatal branches")
    else:
        print(f"[chromium-fd-patch] already patched {binary}: ScopedFD guard fatal branches")
    return changed


def patch_binary(binary: Path) -> bool:
    if not binary.exists():
        print(f"[chromium-fd-patch] missing {binary}, skipping")
        return False

    data = bytearray(binary.read_bytes())
    try:
        changed = patch_unique(
            data,
            binary,
            "CrashOnFdOwnershipViolation",
            FD_OWNERSHIP_SIGNATURE,
            FD_OWNERSHIP_PATCH,
            required=False,
        )
        changed |= patch_unique(
            data,
            binary,
            "ScopedFDCloseTraits::Free EBADF fatal branch",
            SCOPED_CLOSE_EBADF_SIGNATURE,
            SCOPED_CLOSE_EBADF_PATCHED,
            required=False,
        )
        changed |= patch_unique(
            data,
            binary,
            "ScopedFDCloseTraits::Free long EBADF fatal branch",
            SCOPED_CLOSE_EBADF_LONG_SIGNATURE,
            SCOPED_CLOSE_EBADF_LONG_PATCHED,
            required=False,
        )
        changed |= patch_unique(
            data,
            binary,
            "ScopedFDCloseTraits::Free long entry",
            SCOPED_CLOSE_LONG_ENTRY,
            SCOPED_CLOSE_LONG_ENTRY_PATCH,
            required=False,
        )
        changed |= patch_unique(
            data,
            binary,
            "ScopedFDCloseTraits::Free cold fatal epilogue",
            SCOPED_CLOSE_COLD_FATAL_SIGNATURE,
            SCOPED_CLOSE_COLD_FATAL_PATCH,
            required=False,
        )
        changed |= patch_unique(
            data,
            binary,
            "ScopedFDCloseTraits::Free long cold fatal epilogue",
            SCOPED_CLOSE_COLD_FATAL_LONG_SIGNATURE,
            SCOPED_CLOSE_COLD_FATAL_LONG_PATCH,
            required=False,
        )
        changed |= patch_inlined_scoped_fd_ebadf_branches(data, binary)
        changed |= patch_inlined_scoped_fd_fallthrough_fatals(data, binary)
        changed |= patch_branches_to_scoped_fd_fatal_blocks(data, binary)
        changed |= patch_scoped_fd_close_entry(data, binary)
    except RuntimeError as exc:
        print(f"[chromium-fd-patch] {exc}")
        raise

    if changed:
        binary.write_bytes(data)
    return changed


def main() -> int:
    try:
        for binary in BINARIES:
            patch_binary(binary)
    except RuntimeError:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
