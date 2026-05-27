#!/usr/bin/env python3
from pathlib import Path


LIBFONTCONFIG_PATHS = (
    Path("rootfs/usr/lib/x86_64-linux-gnu/libfontconfig.so.1"),
    Path("rootfs/lib/x86_64-linux-gnu/libfontconfig.so.1"),
    Path("rootfs/lib64/libfontconfig.so.1"),
)

# Ridux still has rough edges around Linux file-backed mmap/cache lifetime.
# fontconfig's frozen charset cache can then contain string bytes where a
# linked-list pointer is expected. Chromium/Firefox die in FcCharSetCoverage
# before first paint. Keep the workaround narrow: if the cache bucket is
# non-empty, force the existing "cache miss" path and let fontconfig rebuild
# the leaf in ordinary heap memory instead of walking the suspect bucket.
LEAF_BUCKET_SIGNATURE = bytes.fromhex(
    "49 8b 01"             # mov (%r9), %rax
    "48 85 c0"             # test %rax, %rax
    "75 14"                # jne walk_bucket
    "e9 13 02 00 00"       # jmp cache_miss
)
LEAF_BUCKET_PATCH = bytes.fromhex(
    "49 8b 01"
    "48 85 c0"
    "90 90"                # always fall through to cache_miss jump
    "e9 13 02 00 00"
)

CHARSET_BUCKET_SIGNATURE = bytes.fromhex(
    "48 8b 84 c7 30 0a 00 00" # mov 0xa30(%rdi,%rax,8), %rax
    "48 85 c0"                # test %rax, %rax
    "75 0e"                   # jne walk_bucket
    "eb 2c"                   # jmp cache_miss
)
CHARSET_BUCKET_PATCH = bytes.fromhex(
    "48 8b 84 c7 30 0a 00 00"
    "48 85 c0"
    "90 90"                   # always fall through to cache_miss jump
    "eb 2c"
)


def patch_unique(data: bytearray, name: str, signature: bytes, patch: bytes) -> bool:
    idx = data.find(signature)
    if idx < 0:
        if patch in data:
            print(f"[fontconfig-cache-guard] already patched: {name}")
            return False
        raise RuntimeError(f"signature not found: {name}; fontconfig changed?")
    if data.find(signature, idx + 1) >= 0:
        raise RuntimeError(f"signature is not unique: {name}")
    data[idx:idx + len(patch)] = patch
    print(f"[fontconfig-cache-guard] patched {name} at 0x{idx:x}")
    return True


def main() -> int:
    patched_any = False
    for libfontconfig in LIBFONTCONFIG_PATHS:
        if not libfontconfig.exists():
            print(f"[fontconfig-cache-guard] missing {libfontconfig}, skipping")
            continue
        data = bytearray(libfontconfig.read_bytes())
        changed = patch_unique(data, f"{libfontconfig}: FcCharSet leaf bucket walk", LEAF_BUCKET_SIGNATURE, LEAF_BUCKET_PATCH)
        changed |= patch_unique(data, f"{libfontconfig}: FcCharSet charset bucket walk", CHARSET_BUCKET_SIGNATURE, CHARSET_BUCKET_PATCH)
        if changed:
            libfontconfig.write_bytes(data)
            patched_any = True
    return 0 if patched_any or any(path.exists() for path in LIBFONTCONFIG_PATHS) else 0


if __name__ == "__main__":
    raise SystemExit(main())
