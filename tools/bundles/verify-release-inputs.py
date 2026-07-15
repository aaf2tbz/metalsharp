#!/usr/bin/env python3
"""Verify every release bundle against the checked-in immutable lockfile."""
from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path


def digest(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bundle-dir", type=Path, required=True)
    parser.add_argument("--lock", type=Path, default=Path(__file__).with_name("release-inputs.lock.tsv"))
    parser.add_argument("--require-all", action="store_true")
    parser.add_argument("--asset", action="append", default=[])
    args = parser.parse_args()
    failures: list[str] = []
    checked = 0
    requested = set(args.asset)
    found: set[str] = set()
    for line in args.lock.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        source, name, expected_hash, expected_size, _provenance = line.split(r"\t", 4)
        if requested and name not in requested:
            continue
        found.add(name)
        path = args.bundle_dir / name
        if not path.is_file():
            if args.require_all:
                failures.append(f"missing {name} ({source})")
            continue
        if path.stat().st_size != int(expected_size):
            failures.append(f"size mismatch for {name}")
            continue
        if digest(path) != expected_hash:
            failures.append(f"SHA-256 mismatch for {name}")
            continue
        checked += 1
    missing_lock_entries = requested - found
    if missing_lock_entries:
        failures.extend(f"asset is not locked: {name}" for name in sorted(missing_lock_entries))
    if failures:
        print("release input verification failed:\n- " + "\n- ".join(failures), file=sys.stderr)
        return 1
    print(f"verified {checked} immutable release inputs from {args.lock}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
