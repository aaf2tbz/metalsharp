#!/usr/bin/env python3
"""Build a metadata-only M12 metallib prewarm manifest.

No shader payloads are copied.  The output is a compact hash manifest plus local
cache readiness metadata for offline materialization/freshness gates.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

HEX_RE = re.compile(r"^(?:0x)?([0-9a-fA-F]{1,16})")
SCHEMA = "metalsharp.m12.metallib-prewarm-manifest.v1"


def normalize_hash(text: str) -> str:
    m = HEX_RE.match(text.strip())
    if not m:
        raise ValueError(f"invalid hash: {text!r}")
    return m.group(1).lower().zfill(16)


def read_hash_file(path: Path) -> list[str]:
    out: list[str] = []
    for line in path.read_text(errors="replace").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        out.append(normalize_hash(stripped))
    return out


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


@dataclass
class Entry:
    order: int
    hash: str
    msl_exists: bool
    metallib_exists: bool
    metallib_fresh: bool
    metallib_size: int | None
    metallib_header_ok: bool


def stat_entry(cache_dir: Path, h: str, order: int) -> Entry:
    msl = cache_dir / f"{h}.msl"
    metallib = cache_dir / f"{h}.metallib"
    msl_exists = msl.exists()
    metallib_exists = metallib.exists()
    size = metallib.stat().st_size if metallib_exists else None
    fresh = False
    header_ok = False
    if metallib_exists:
        try:
            header_ok = metallib.read_bytes()[:4] == b"MTLB"
        except OSError:
            header_ok = False
        if msl_exists:
            fresh = metallib.stat().st_mtime >= msl.stat().st_mtime and bool(size and size > 0) and header_ok
        else:
            fresh = bool(size and size > 0) and header_ok
    return Entry(order, h, msl_exists, metallib_exists, fresh, size, header_ok)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache-dir", type=Path, required=True)
    ap.add_argument("--hash-file", action="append", type=Path, required=True)
    ap.add_argument("--game", default="elden-ring")
    ap.add_argument("--appid", default="1245620")
    ap.add_argument("--profile", default="elden-ring-type-ab-hot-window")
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()

    hashes: list[str] = []
    sources = []
    for path in args.hash_file:
        values = read_hash_file(path)
        sources.append({"path": str(path), "count": len(values), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
        hashes.extend(values)
    hashes = list(dict.fromkeys(hashes))
    entries = [stat_entry(args.cache_dir, h, i) for i, h in enumerate(hashes)]
    manifest = {
        "schema": SCHEMA,
        "game": args.game,
        "appid": args.appid,
        "profile": args.profile,
        "cache_dir": str(args.cache_dir),
        "offline_profile_gated": True,
        "raw_payloads_included": False,
        "source_hash_files": sources,
        "hash_count": len(entries),
        "ready_count": sum(1 for e in entries if e.metallib_fresh),
        "missing_msl": [e.hash for e in entries if not e.msl_exists],
        "missing_metallib": [e.hash for e in entries if not e.metallib_exists],
        "nonfresh_metallib": [e.hash for e in entries if not e.metallib_fresh],
        "entries": [asdict(e) for e in entries],
    }
    manifest["manifest_sha256"] = sha256_text(json.dumps({k: v for k, v in manifest.items() if k != "manifest_sha256"}, sort_keys=True, separators=(",", ":")))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"summary hashes={manifest['hash_count']} ready={manifest['ready_count']} nonfresh={len(manifest['nonfresh_metallib'])}")
    print(args.out)
    return 0 if manifest["ready_count"] == manifest["hash_count"] else 1


if __name__ == "__main__":
    sys.exit(main())
