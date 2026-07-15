#!/usr/bin/env python3
"""Synchronize every MetalSharp release-version surface."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


SEMVER = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")
CMAKE_VERSION = re.compile(r"(project\(metalsharp VERSION )(?P<version>\d+\.\d+\.\d+)")
ELECTRON_LEGACY_MAP = re.compile(
    r"(?P<prefix>LEGACY_CONTRACT_BY_BACKEND_VERSION[^=]*=\s*\{)"
    r"(?P<body>.*?)"
    r"(?P<suffix>\n\};)",
    re.DOTALL,
)


def fail(message: str) -> None:
    print(f"version bump failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def parse_version(value: str) -> str:
    candidate = value.removeprefix("v")
    if SEMVER.fullmatch(candidate) is None:
        fail(f"expected a stable X.Y.Z version, got {value!r}")
    return candidate


def version_key(value: str) -> tuple[int, int, int]:
    parts = tuple(map(int, value.split(".")))
    if len(parts) != 3:
        fail(f"invalid compatibility-map version {value!r}")
    return parts


def encoded_bytes(version: str) -> str:
    return ", ".join(f"0x{byte:02x}" for byte in version.encode("ascii"))


def read_json(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"cannot read {path}: {exc}")
    if not isinstance(value, dict):
        fail(f"{path} must contain a JSON object")
    return value


def write_json(path: Path, value: dict) -> None:
    path.write_text(f"{json.dumps(value, indent=2, ensure_ascii=False)}\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version", help="release version, with or without a leading v")
    parser.add_argument("--root", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()

    root = (args.root or Path(__file__).resolve().parents[2]).resolve()
    target = parse_version(args.version)
    package_path = root / "app/package.json"
    lock_path = root / "app/package-lock.json"
    cmake_path = root / "CMakeLists.txt"
    contract_path = root / "contracts/electron-backend.v1.json"
    electron_contract_path = root / "app/src/shared/backend-contract.ts"

    package = read_json(package_path)
    package_lock = read_json(lock_path)
    current = parse_version(str(package.get("version", "")))
    lock_root = package_lock.get("packages", {}).get("")
    if not isinstance(lock_root, dict):
        fail('app/package-lock.json is missing packages[""]')

    cmake = cmake_path.read_text(encoding="utf-8")
    cmake_match = CMAKE_VERSION.search(cmake)
    if cmake_match is None:
        fail("CMakeLists.txt MetalSharp version is missing")
    current_versions = {
        "app/package.json": current,
        "app/package-lock.json": str(package_lock.get("version", "")),
        'app/package-lock.json packages[""]': str(lock_root.get("version", "")),
        "CMakeLists.txt": cmake_match.group("version"),
    }
    drift = {name: value for name, value in current_versions.items() if value != current}
    if drift:
        fail(f"existing version metadata is already misaligned: expected={current} found={drift}")
    if target != current and version_key(target) <= version_key(current):
        fail(f"target version {target} must be newer than current version {current}")
    if len(target.encode("ascii")) != len(current.encode("ascii")):
        fail(
            "committed C version strings require an equal-length version bump "
            f"({current!r} has {len(current)} bytes, {target!r} has {len(target)}); "
            "regenerate the committed C backend for this boundary"
        )

    runtime_sources = sorted((root / "app/src-c/runtime/c").glob("metalsharp_backend-*.c"))
    test_sources = sorted((root / "app/src-c/tests/c").glob("metalsharp_backend-*.c"))
    if not runtime_sources or not test_sources:
        fail("committed C backend runtime or test sources are missing")
    c_sources = runtime_sources + test_sources
    old_encoded = encoded_bytes(current)
    new_encoded = encoded_bytes(target)
    runtime_occurrences = sum(path.read_text(errors="ignore").count(old_encoded) for path in runtime_sources)
    test_occurrences = sum(path.read_text(errors="ignore").count(old_encoded) for path in test_sources)
    if runtime_occurrences == 0 or test_occurrences == 0:
        fail(
            f"committed C surfaces do not both contain encoded version {current}: "
            f"runtime={runtime_occurrences} tests={test_occurrences}"
        )
    encoded_occurrences = runtime_occurrences + test_occurrences

    contract = read_json(contract_path)
    contract_version = str(contract.get("contract_version", ""))
    status = contract.get("status")
    if not contract_version or not isinstance(status, dict):
        fail("contracts/electron-backend.v1.json is missing contract metadata")
    legacy_versions = status.get("legacy_versions")
    if not isinstance(legacy_versions, dict):
        fail("contracts/electron-backend.v1.json is missing status.legacy_versions")
    legacy_versions[target] = contract_version
    status["legacy_versions"] = dict(sorted(legacy_versions.items(), key=lambda item: version_key(item[0])))

    electron_contract = electron_contract_path.read_text(encoding="utf-8")
    legacy_match = ELECTRON_LEGACY_MAP.search(electron_contract)
    if legacy_match is None:
        fail("app/src/shared/backend-contract.ts legacy contract map is missing")
    electron_versions = set(
        re.findall(r'"(\d+\.\d+\.\d+)"\s*:\s*BACKEND_CONTRACT_VERSION', legacy_match.group("body"))
    )
    electron_versions.update(status["legacy_versions"])
    legacy_body = "".join(
        f'\n  "{version}": BACKEND_CONTRACT_VERSION,'
        for version in sorted(electron_versions, key=version_key)
    )
    electron_contract = (
        electron_contract[: legacy_match.start()]
        + legacy_match.group("prefix")
        + legacy_body
        + legacy_match.group("suffix")
        + electron_contract[legacy_match.end() :]
    )

    package["version"] = target
    package_lock["version"] = target
    lock_root["version"] = target
    cmake = CMAKE_VERSION.sub(rf"\g<1>{target}", cmake, count=1)

    write_json(package_path, package)
    write_json(lock_path, package_lock)
    cmake_path.write_text(cmake, encoding="utf-8")
    write_json(contract_path, contract)
    electron_contract_path.write_text(electron_contract, encoding="utf-8")
    for path in c_sources:
        source = path.read_text(encoding="utf-8", errors="ignore")
        if old_encoded in source:
            path.write_text(source.replace(old_encoded, new_encoded), encoding="utf-8")

    print(
        f"Updated MetalSharp {current} -> {target}: 4 metadata fields, "
        f"2 contract maps, and {encoded_occurrences} committed-C version strings."
    )


if __name__ == "__main__":
    main()
