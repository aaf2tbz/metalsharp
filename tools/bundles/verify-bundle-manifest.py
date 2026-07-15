#!/usr/bin/env python3
"""Verify release bundle archives against a generated bundle manifest."""
from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_manifest(path: Path) -> dict[str, tuple[str, int]]:
    rows: dict[str, tuple[str, int]] = {}
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        if not line or line.startswith("#") or line.startswith("asset\t"):
            continue
        fields = line.split("\t")
        if len(fields) < 4:
            raise ValueError(f"invalid manifest row {number}: {line}")
        name, _root, expected_hash, expected_size = fields[:4]
        if name in rows:
            raise ValueError(f"duplicate manifest asset: {name}")
        rows[name] = (expected_hash, int(expected_size))
    if not rows:
        raise ValueError("manifest has no bundle assets")
    return rows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bundle-dir", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--require-all", action="store_true")
    parser.add_argument("asset", nargs="*")
    args = parser.parse_args()

    try:
        rows = read_manifest(args.manifest)
    except (OSError, ValueError) as error:
        print(f"bundle manifest verification failed: {error}", file=sys.stderr)
        return 1

    requested = args.asset or (sorted(rows) if args.require_all else [])
    if not requested:
        print("bundle manifest verification failed: specify assets or --require-all", file=sys.stderr)
        return 1

    failures: list[str] = []
    for name in requested:
        expected = rows.get(name)
        if expected is None:
            failures.append(f"manifest does not list {name}")
            continue
        path = args.bundle_dir / name
        if not path.is_file():
            failures.append(f"missing {name}")
            continue
        expected_hash, expected_size = expected
        actual_size = path.stat().st_size
        if actual_size != expected_size:
            failures.append(f"size mismatch for {name}: expected={expected_size} actual={actual_size}")
            continue
        actual_hash = sha256(path)
        if actual_hash != expected_hash:
            failures.append(f"SHA-256 mismatch for {name}: expected={expected_hash} actual={actual_hash}")

    if failures:
        print("bundle manifest verification failed:\n- " + "\n- ".join(failures), file=sys.stderr)
        return 1
    print(f"verified {len(requested)} bundle assets against {args.manifest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
