#!/usr/bin/env python3
"""Verify package metadata, committed C, and an optional release tag agree."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def fail(message: str) -> None:
    print(message, file=sys.stderr)
    raise SystemExit(1)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tag")
    args = parser.parse_args()

    package = json.loads((ROOT / "app/package.json").read_text())
    package_lock = json.loads((ROOT / "app/package-lock.json").read_text())
    cmake_text = (ROOT / "CMakeLists.txt").read_text()
    cmake_match = re.search(r"project\(metalsharp VERSION ([0-9]+\.[0-9]+\.[0-9]+)", cmake_text)
    if cmake_match is None:
        fail("CMakeLists.txt MetalSharp version is missing")

    versions = {
        "app/package.json": package["version"],
        "app/package-lock.json": package_lock["version"],
        "app/package-lock.json packages root": package_lock["packages"][""]["version"],
        "CMakeLists.txt": cmake_match.group(1),
    }
    expected = package["version"]
    mismatched = {path: version for path, version in versions.items() if version != expected}
    if mismatched:
        fail(f"version metadata mismatch: expected={expected} mismatched={mismatched}")

    if args.tag is not None and args.tag.removeprefix("v") != expected:
        fail(f"release tag {args.tag} does not match committed version {expected}")

    encoded = ", ".join(f"0x{byte:02x}" for byte in expected.encode())
    runtime_c = "\n".join(path.read_text(errors="ignore") for path in sorted((ROOT / "app/src-c/runtime/c").glob("*.c")))
    if encoded not in runtime_c:
        fail(f"committed C backend does not contain version {expected}")

    print(f"MetalSharp version alignment verified: {expected}")


if __name__ == "__main__":
    main()
