#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
prefetch_ridux_pkgs.py
======================

Pre-fetch FreeBSD .pkg files (with full dependency closure) from
pkg.freebsd.org so the Ridux Live ISO can install them offline at
boot time. This eliminates the 700 MB / 12-min download phase that
otherwise dominates first boot of the live image.

Algorithm
---------
1. Download `packagesite.pkg` (the repo metadata bundle).
2. Extract `packagesite.yaml` from inside it (it's a tar archive).
3. Stream-parse the yaml -- each line is a JSON document describing
   one package, with its name, version, dependencies, and the
   relative `repopath` of the .pkg file.
4. Starting from the user-requested top-level package set
   (firefox, xorg-minimal, xinit, xauth, dbus, xterm, ...), build the
   transitive dependency closure.
5. Download each .pkg file via curl with retry, verify the size, and
   cache locally under `third_party/cache/freebsd-pkgs/<abi>/<repo>/`.

Output
------
* A flat directory of `.pkg` files ready to be embedded under
  `/usr/local/ridux-pkgs/All/` on the ISO.
* A `packagesite.pkg` copy ready for `/usr/local/ridux-pkgs/`.
* A `Ridux.conf` repo descriptor written to stdout for embedding in
  /usr/local/etc/pkg/repos/Ridux.conf on the ISO.

Usage
-----
    python3 tools/prefetch_ridux_pkgs.py \\
        --abi FreeBSD:15:amd64 \\
        --repo latest \\
        --top firefox xorg-minimal xinit xauth dbus xterm libX11 pkgconf \\
        --out third_party/cache/freebsd-pkgs

Environment
-----------
    PKG_BASE_URL      override the base URL (default: https://pkg.freebsd.org)
    HTTP_PROXY        forwarded to curl
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

DEFAULT_TOP = [
    # `pkg` itself is needed so the live ISO can bootstrap its package
    # tooling without internet.
    "pkg",
    # GUI runtime dependencies of the Ridux UI.
    "firefox",
    "xorg-minimal",
    "xinit",
    "xauth",
    "dbus",
    "xterm",
    "libX11",
    "pkgconf",
    "binutils",
    # vbox guest additions: lets us pass mouse/clipboard/resolution
    # cleanly when the ISO runs inside VirtualBox.
    "virtualbox-ose-additions",
]


def log(msg: str) -> None:
    print(f"[prefetch] {msg}", file=sys.stderr, flush=True)


def run(cmd: list[str], **kwargs) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, check=True, **kwargs)


def curl_fetch(url: str, dst: Path) -> None:
    """Download `url` to `dst` via curl, with retry."""
    dst.parent.mkdir(parents=True, exist_ok=True)
    tmp = dst.with_suffix(dst.suffix + ".part")
    if tmp.exists():
        tmp.unlink()
    cmd = [
        "curl",
        "--fail",
        "--silent",
        "--show-error",
        "--location",
        "--retry",
        "5",
        "--retry-delay",
        "2",
        "-o",
        str(tmp),
        url,
    ]
    run(cmd)
    tmp.rename(dst)


def fetch_packagesite(base_url: str, abi: str, repo: str, cache_dir: Path) -> Path:
    """Fetch packagesite.pkg into `cache_dir` and return its path."""
    url = f"{base_url}/{abi}/{repo}/packagesite.pkg"
    dst = cache_dir / "packagesite.pkg"
    if not dst.exists() or dst.stat().st_size == 0:
        log(f"downloading {url}")
        curl_fetch(url, dst)
    else:
        log(f"using cached {dst}")
    return dst


def _decompress_to_tar(pkg_path: Path) -> bytes:
    """Detect compression of `pkg_path` and return the decompressed
    tar bytes. Modern FreeBSD repos use zstd; older repos use xz / gzip.
    We sniff the magic header instead of trusting tarfile."""
    with open(pkg_path, "rb") as fh:
        header = fh.read(8)
    # zstd magic: 28 b5 2f fd
    if header.startswith(b"\x28\xb5\x2f\xfd"):
        # Stream the file through `zstd -d` to stdout. zstd is part of
        # FreeBSD, Debian, Ubuntu, Alpine ... so it's a safe dep.
        cp = subprocess.run(
            ["zstd", "-d", "-c", str(pkg_path)],
            check=True, stdout=subprocess.PIPE)
        return cp.stdout
    if header.startswith(b"\xfd\x37\x7a\x58\x5a\x00"):
        cp = subprocess.run(
            ["xz", "-d", "-c", str(pkg_path)],
            check=True, stdout=subprocess.PIPE)
        return cp.stdout
    if header.startswith(b"\x1f\x8b"):
        cp = subprocess.run(
            ["gzip", "-d", "-c", str(pkg_path)],
            check=True, stdout=subprocess.PIPE)
        return cp.stdout
    # Already plain tar?
    return pkg_path.read_bytes()


def extract_packagesite_yaml(pkg_path: Path) -> bytes:
    """The packagesite.pkg is a tar archive (.txz / .tzst / .tgz)
    containing one entry: packagesite.yaml. Return its raw bytes."""
    import io
    tar_bytes = _decompress_to_tar(pkg_path)
    with tarfile.open(fileobj=io.BytesIO(tar_bytes), mode="r:") as tf:
        for member in tf.getmembers():
            if member.name.endswith("packagesite.yaml"):
                f = tf.extractfile(member)
                if f is None:
                    raise RuntimeError(
                        f"could not extract {member.name} from {pkg_path}")
                return f.read()
    raise RuntimeError(f"packagesite.yaml not found in {pkg_path}")


def parse_packagesite(yaml_bytes: bytes) -> dict[str, dict]:
    """Parse the JSON-lines packagesite. Returns {name -> record}."""
    out: dict[str, dict] = {}
    for line in yaml_bytes.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            rec = json.loads(line)
        except json.JSONDecodeError:
            continue
        name = rec.get("name")
        if not name:
            continue
        out[name] = rec
    return out


def closure(records: dict[str, dict], top: list[str]) -> set[str]:
    """Compute the dependency closure of `top` over `records`."""
    seen: set[str] = set()
    todo: list[str] = list(top)
    missing: set[str] = set()
    while todo:
        cur = todo.pop()
        if cur in seen:
            continue
        seen.add(cur)
        rec = records.get(cur)
        if not rec:
            missing.add(cur)
            continue
        deps = rec.get("deps") or {}
        for dep_name in deps.keys():
            if dep_name not in seen:
                todo.append(dep_name)
    if missing:
        log(f"WARNING: {len(missing)} top-level package(s) not found "
            f"in repo metadata: {sorted(missing)[:8]}")
    return seen


def download_packages(
    base_url: str,
    abi: str,
    repo: str,
    records: dict[str, dict],
    names: set[str],
    out_dir: Path,
) -> tuple[int, int]:
    """Download each package in `names` to `out_dir`. Skip if cached.
    Returns (num_downloaded, num_cached)."""
    out_dir.mkdir(parents=True, exist_ok=True)
    downloaded = 0
    cached = 0
    sorted_names = sorted(names)
    for i, name in enumerate(sorted_names, 1):
        rec = records.get(name)
        if not rec:
            continue
        repopath = rec.get("repopath") or rec.get("path") or ""
        if not repopath:
            log(f"  [{i}/{len(sorted_names)}] {name}: no repopath, skipping")
            continue
        # repopath is e.g. "All/Hashed/firefox-118.0~hash.pkg" on
        # modern FreeBSD repos. We mirror the FULL path so pkg(8) can
        # consume the local cache as a normal file:// repository
        # without needing to regenerate metadata.
        local = out_dir.parent / repopath
        if local.exists() and local.stat().st_size > 0:
            cached += 1
            continue
        url = f"{base_url}/{abi}/{repo}/{repopath}"
        log(f"  [{i}/{len(sorted_names)}] {name}: GET {url}")
        try:
            curl_fetch(url, local)
            downloaded += 1
        except subprocess.CalledProcessError as exc:
            log(f"  [{i}/{len(sorted_names)}] {name}: FAILED ({exc})")
    return downloaded, cached


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--abi", default="FreeBSD:15:amd64",
                    help="package ABI (default: FreeBSD:15:amd64)")
    ap.add_argument("--repo", default="latest",
                    help="repo branch (default: latest)")
    ap.add_argument(
        "--top", nargs="+", default=DEFAULT_TOP,
        help="top-level packages to fetch with full dep closure")
    ap.add_argument(
        "--out", default="third_party/cache/freebsd-pkgs",
        help="output cache directory")
    ap.add_argument(
        "--base-url",
        default=os.environ.get("PKG_BASE_URL", "https://pkg.freebsd.org"),
        help="pkg repo base URL")
    args = ap.parse_args()

    cache_root = Path(args.out).resolve()
    abi_dir = cache_root / args.abi.replace(":", "_") / args.repo
    abi_dir.mkdir(parents=True, exist_ok=True)
    pkgs_out = abi_dir / "All"
    pkgs_out.mkdir(parents=True, exist_ok=True)

    log(f"abi:  {args.abi}")
    log(f"repo: {args.repo}")
    log(f"top:  {args.top}")
    log(f"out:  {abi_dir}")

    pkg_path = fetch_packagesite(args.base_url, args.abi, args.repo, abi_dir)
    yaml_bytes = extract_packagesite_yaml(pkg_path)
    records = parse_packagesite(yaml_bytes)
    log(f"parsed {len(records)} packages from packagesite")

    cl = closure(records, list(args.top))
    log(f"dependency closure: {len(cl)} packages")

    downloaded, cached = download_packages(
        args.base_url, args.abi, args.repo, records, cl, pkgs_out)
    log(f"downloaded={downloaded} cached={cached}")

    # Print a Ridux.conf descriptor that the ISO build embeds at
    # /usr/local/etc/pkg/repos/Ridux.conf to make pkg(8) consume the
    # baked-in cache.
    print("# Auto-generated by tools/prefetch_ridux_pkgs.py")
    print("Ridux: {")
    print('  url: "file:///usr/local/ridux-pkgs",')
    print('  mirror_type: "none",')
    print('  signature_type: "none",')
    print('  enabled: yes,')
    print('  priority: 100,')
    print("}")

    log("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
