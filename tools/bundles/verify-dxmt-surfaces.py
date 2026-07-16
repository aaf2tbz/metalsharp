#!/usr/bin/env python3
"""Verify the frozen DXMT surfaces inside the graphics DLL bundle."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ARCHIVE = PROJECT_ROOT / "app" / "bundles" / "metalsharp-graphics-dll.tar.zst"
DEFAULT_MANIFEST = PROJECT_ROOT / "tools" / "bundles" / "dxmt-surfaces.json"
INTERNAL_MANIFEST = Path("Graphics/dll/dxmt-surfaces.json")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_manifest(path: Path) -> dict:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read manifest {path}: {error}") from error
    if manifest.get("schema") != "metalsharp.dxmt.surface-set.v1":
        raise ValueError(f"unsupported DXMT surface manifest schema in {path}")
    if not manifest.get("surface_set_id") or not manifest.get("surfaces"):
        raise ValueError(f"incomplete DXMT surface manifest {path}")
    return manifest


def archive_members(archive: Path) -> list[str]:
    result = subprocess.run(
        ["tar", "--use-compress-program=unzstd", "-tf", str(archive)],
        check=True,
        capture_output=True,
        text=True,
    )
    members = [line.rstrip("/") for line in result.stdout.splitlines() if line.rstrip("/")]
    for member in members:
        path = Path(member)
        if path.is_absolute() or ".." in path.parts:
            raise ValueError(f"unsafe archive member: {member}")
    return members


def verify_archive(archive: Path, manifest_path: Path) -> None:
    if not archive.is_file() or archive.stat().st_size == 0:
        raise ValueError(f"graphics bundle missing or empty: {archive}")
    manifest = load_manifest(manifest_path)
    members = archive_members(archive)
    member_set = set(members)
    if str(INTERNAL_MANIFEST) not in member_set:
        raise ValueError(f"graphics bundle is missing {INTERNAL_MANIFEST}")

    with tempfile.TemporaryDirectory(prefix="metalsharp-dxmt-surfaces-") as temp_name:
        root = Path(temp_name)
        subprocess.run(
            ["tar", "--use-compress-program=unzstd", "-xf", str(archive), "-C", str(root)],
            check=True,
        )
        internal_path = root / INTERNAL_MANIFEST
        internal = load_manifest(internal_path)
        if internal != manifest:
            raise ValueError("bundle DXMT surface manifest differs from the checked-in manifest")

        for surface in manifest["surfaces"]:
            surface_id = surface.get("surface_id", "unknown")
            artifacts = surface.get("artifacts", {})
            bundle_roots = surface.get("bundle_roots", [])
            if not artifacts or not bundle_roots:
                raise ValueError(f"surface {surface_id} has no artifacts or bundle roots")

            for bundle_root_text in bundle_roots:
                bundle_root = Path(bundle_root_text)
                expected_by_directory: dict[Path, set[Path]] = {}
                for relative_text, expected in artifacts.items():
                    relative = Path(relative_text)
                    artifact = root / bundle_root / relative
                    if not artifact.is_file():
                        raise ValueError(f"surface {surface_id} is missing {bundle_root / relative}")
                    actual_size = artifact.stat().st_size
                    actual_sha256 = sha256(artifact)
                    if actual_size != expected.get("size"):
                        raise ValueError(
                            f"surface {surface_id} size mismatch for {bundle_root / relative}: "
                            f"expected {expected.get('size')}, got {actual_size}"
                        )
                    if actual_sha256 != expected.get("sha256"):
                        raise ValueError(
                            f"surface {surface_id} hash mismatch for {bundle_root / relative}: "
                            f"expected {expected.get('sha256')}, got {actual_sha256}"
                        )
                    expected_by_directory.setdefault(relative.parent, set()).add(relative)

                for directory, expected_paths in expected_by_directory.items():
                    lane_dir = root / bundle_root / directory
                    actual_paths = {
                        path.relative_to(root / bundle_root)
                        for path in lane_dir.iterdir()
                        if path.is_file()
                    }
                    directory_expected = {path for path in expected_paths if path.parent == directory}
                    if actual_paths != directory_expected:
                        unexpected = sorted(str(path) for path in actual_paths - directory_expected)
                        missing = sorted(str(path) for path in directory_expected - actual_paths)
                        raise ValueError(
                            f"surface {surface_id} lane contents differ for {bundle_root / directory}: "
                            f"unexpected={unexpected} missing={missing}"
                        )

            print(f"VERIFIED: {surface_id} roots={len(bundle_roots)} artifacts={len(artifacts)}")

    print(f"VERIFIED: {manifest['surface_set_id']} archive={archive}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, default=DEFAULT_ARCHIVE)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    args = parser.parse_args()
    try:
        verify_archive(args.archive, args.manifest)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"DXMT SURFACE INVALID: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
