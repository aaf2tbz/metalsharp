#!/usr/bin/env python3
"""Verify M12 .msl -> .metallib freshness/quarantine invariants.

Read-only.  Does not launch games and does not rebuild cache files.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Iterable

HEX_RE = re.compile(r"^(?:0x)?([0-9a-fA-F]{1,16})")


def normalize_hash(text: str) -> str:
    m = HEX_RE.match(text.strip())
    if not m:
        raise ValueError(f"invalid hash: {text!r}")
    return m.group(1).lower().zfill(16)


def hashes_from_file(path: Path) -> list[str]:
    hashes: list[str] = []
    for line in path.read_text(errors="replace").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        hashes.append(normalize_hash(stripped))
    return hashes


def discover_hashes(cache_dir: Path) -> list[str]:
    found: set[str] = set()
    for p in cache_dir.iterdir() if cache_dir.exists() else []:
        if not p.is_file():
            continue
        stem = p.name.split(".", 1)[0]
        if len(stem) == 16 and all(c in "0123456789abcdefABCDEF" for c in stem):
            if p.name.endswith((".msl", ".metallib", ".metallib.err.txt")):
                found.add(stem.lower())
    return sorted(found)


@dataclass
class FileInfo:
    path: str
    exists: bool
    size: int | None = None
    mtime: float | None = None


@dataclass
class Row:
    hash: str
    status: str
    categories: list[str]
    msl: FileInfo
    metallib: FileInfo
    metallib_error: FileInfo
    objdump_ok: bool | None = None
    objdump_error: str | None = None


def file_info(path: Path) -> FileInfo:
    try:
        st = path.stat()
        return FileInfo(str(path), True, st.st_size, st.st_mtime)
    except OSError:
        return FileInfo(str(path), False)


def has_mtlb_header(path: Path) -> bool:
    try:
        with path.open("rb") as f:
            return f.read(4) == b"MTLB"
    except OSError:
        return False


def run_objdump(path: Path, timeout: float) -> tuple[bool, str | None]:
    tool = None
    try:
        tool = subprocess.check_output(["xcrun", "-f", "metal-objdump"], text=True, stderr=subprocess.DEVNULL).strip()
    except Exception as exc:
        return False, f"metal-objdump unavailable: {exc}"
    # macOS tool variants differ; --help must succeed, and a no-output metadata
    # query is not guaranteed.  Prefer a conservative invocation and report text.
    candidates = [
        [tool, "--private-headers", str(path)],
        [tool, "--macho", str(path)],
        [tool, str(path)],
    ]
    last = None
    for cmd in candidates:
        try:
            cp = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
            if cp.returncode == 0:
                return True, None
            last = (cp.stderr or cp.stdout).strip()[:500]
        except Exception as exc:
            last = str(exc)
    return False, last or "metal-objdump failed"


def inspect_hash(cache_dir: Path, h: str, *, check_objdump: bool, objdump_timeout: float) -> Row:
    msl_path = cache_dir / f"{h}.msl"
    metallib_path = cache_dir / f"{h}.metallib"
    err_path = cache_dir / f"{h}.metallib.err.txt"
    msl = file_info(msl_path)
    metallib = file_info(metallib_path)
    err = file_info(err_path)
    cats: list[str] = []

    if not msl.exists:
        cats.append("missing_msl")
    if not metallib.exists:
        cats.append("missing_metallib")
    else:
        if metallib.size is None or metallib.size <= 0:
            cats.append("zero_byte_metallib")
        if not has_mtlb_header(metallib_path):
            cats.append("invalid_metallib_header")
        if msl.exists and metallib.mtime is not None and msl.mtime is not None and metallib.mtime < msl.mtime:
            cats.append("stale_metallib_older_than_msl")

    if err.exists:
        cats.append("has_metallib_error")
        if (not metallib.exists) or (err.mtime is not None and metallib.mtime is not None and err.mtime >= metallib.mtime):
            cats.append("active_metallib_error")
        else:
            cats.append("stale_metallib_error_older_than_metallib")

    objdump_ok = None
    objdump_error = None
    if check_objdump and metallib.exists and metallib.size and metallib.size > 0:
        objdump_ok, objdump_error = run_objdump(metallib_path, objdump_timeout)
        if not objdump_ok:
            cats.append("objdump_failed")

    bad = [c for c in cats if c not in {"has_metallib_error", "stale_metallib_error_older_than_metallib"}]
    status = "fresh" if not bad else "nonfresh"
    if not cats:
        cats.append("fresh")
    return Row(h, status, cats, msl, metallib, err, objdump_ok, objdump_error)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache-dir", type=Path, required=True)
    ap.add_argument("--hash", action="append", default=[])
    ap.add_argument("--hash-file", action="append", type=Path, default=[])
    ap.add_argument("--all", action="store_true", help="Check all discovered .msl/.metallib hashes in cache dir")
    ap.add_argument("--strict", action="store_true", help="Exit nonzero if any row is nonfresh")
    ap.add_argument("--check-objdump", action="store_true", help="Optionally validate metallib with xcrun metal-objdump")
    ap.add_argument("--objdump-timeout", type=float, default=5.0)
    ap.add_argument("--out", type=Path)
    args = ap.parse_args()

    hashes: list[str] = []
    for raw in args.hash:
        hashes.append(normalize_hash(raw))
    for path in args.hash_file:
        hashes.extend(hashes_from_file(path))
    if args.all or not hashes:
        hashes.extend(discover_hashes(args.cache_dir))
    hashes = sorted(dict.fromkeys(hashes))

    rows = [inspect_hash(args.cache_dir, h, check_objdump=args.check_objdump, objdump_timeout=args.objdump_timeout) for h in hashes]
    counts: dict[str, int] = {}
    for row in rows:
        counts[row.status] = counts.get(row.status, 0) + 1
        for cat in row.categories:
            counts[f"category:{cat}"] = counts.get(f"category:{cat}", 0) + 1
    report = {
        "schema": "metalsharp.m12.metallib-freshness.v1",
        "cache_dir": str(args.cache_dir),
        "total": len(rows),
        "fresh": sum(1 for r in rows if r.status == "fresh"),
        "nonfresh": [r.hash for r in rows if r.status != "fresh"],
        "counts": counts,
        "rows": [asdict(r) for r in rows],
    }
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(report, indent=2) + "\n")
    print(f"summary total={report['total']} fresh={report['fresh']} nonfresh={len(report['nonfresh'])}")
    for row in rows:
        if row.status != "fresh":
            print(f"nonfresh hash={row.hash} categories={','.join(row.categories)}")
    if args.out:
        print(args.out)
    return 1 if args.strict and report["nonfresh"] else 0


if __name__ == "__main__":
    sys.exit(main())
