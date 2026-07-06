#!/usr/bin/env python3
"""Mount/extract a user-provided Apple GPTK4 DMG for local D3DMetal staging.

MetalSharp must not redistribute Apple Game Porting Toolkit binaries. This helper
only opens a user/developer-provided DMG locally, copies the GPTK redist payload
out to a caller-provided private directory, and unmounts the image immediately.

The helper centralizes macOS DiskImages handling for GPTK4 so app/backend code no
longer depends on fragile Finder/Terminal flows or ad-hoc mount paths. It uses
`hdiutil attach -plist` as the supported non-interactive DiskImages interface on
macOS, parses the plist for actual mount points, and always detaches in a
finally block.
"""

from __future__ import annotations

import argparse
import contextlib
import json
import os
import plistlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Iterable, Iterator


class GptkDmgError(RuntimeError):
    pass


def redist_root_for_mount(mount: Path) -> Path:
    """Return the GPTK redist root inside an attached image."""
    candidates = [mount / "redist", mount]
    for candidate in candidates:
        if (candidate / "lib" / "wine").is_dir() and (candidate / "lib" / "external").is_dir():
            return candidate
    raise GptkDmgError(f"no GPTK redist layout found under mounted image {mount}")


@contextlib.contextmanager
def attached_dmg(dmg: Path) -> Iterator[list[Path]]:
    """Attach a DMG read-only on a private random mount root and yield mount points."""
    if sys.platform != "darwin":
        raise GptkDmgError("GPTK DMG mounting is only supported on macOS")
    if not dmg.is_file():
        raise GptkDmgError(f"DMG not found: {dmg}")

    with tempfile.TemporaryDirectory(prefix="metalsharp-gptk4-mountroot-") as mount_root:
        cmd = [
            "hdiutil",
            "attach",
            "-plist",
            "-readonly",
            "-nobrowse",
            "-owners",
            "off",
            "-mountRandom",
            mount_root,
            str(dmg),
        ]
        proc = subprocess.run(cmd, capture_output=True, check=False)
        if proc.returncode != 0:
            stderr = proc.stderr.decode(errors="replace").strip()
            stdout = proc.stdout.decode(errors="replace").strip()
            detail = stderr or stdout or f"exit {proc.returncode}"
            raise GptkDmgError(f"failed to attach GPTK DMG {dmg}: {detail}")

        entities: list[dict] = []
        try:
            plist = plistlib.loads(proc.stdout)
            entities = list(plist.get("system-entities", []))
            mount_points = [Path(entity["mount-point"]) for entity in entities if entity.get("mount-point")]
            if not mount_points:
                raise GptkDmgError(f"attached {dmg}, but DiskImages reported no mount point")
            yield mount_points
        finally:
            detached: set[str] = set()
            for entity in reversed(entities):
                target = entity.get("mount-point") or entity.get("dev-entry")
                if not target or target in detached:
                    continue
                detached.add(target)
                subprocess.run(["hdiutil", "detach", target, "-force", "-quiet"], capture_output=True, check=False)


def copy_redist_tree(redist: Path, dest: Path, *, force: bool) -> None:
    if dest.exists():
        if not force:
            raise GptkDmgError(f"destination already exists: {dest}")
        shutil.rmtree(dest)
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp_dest = dest.parent / f".{dest.name}.tmp-{os.getpid()}"
    if tmp_dest.exists():
        shutil.rmtree(tmp_dest)
    shutil.copytree(redist, tmp_dest, symlinks=True)
    os.replace(tmp_dest, dest)


def extract_redist(dmg: Path, dest: Path, *, force: bool) -> Path:
    with attached_dmg(dmg) as mounts:
        errors: list[str] = []
        for mount in mounts:
            try:
                redist = redist_root_for_mount(mount)
                copy_redist_tree(redist, dest, force=force)
                return dest
            except GptkDmgError as exc:
                errors.append(str(exc))
        raise GptkDmgError("; ".join(errors) if errors else f"no mounted volumes found in {dmg}")


def main(argv: Iterable[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dmg", type=Path, help="Apple GPTK4 evaluation DMG path")
    parser.add_argument("--extract-redist", type=Path, help="Copy redist/ contents to this directory, then detach")
    parser.add_argument("--force", action="store_true", help="Overwrite --extract-redist destination")
    parser.add_argument("--json", action="store_true", help="Print machine-readable JSON")
    args = parser.parse_args(list(argv))

    dmg = args.dmg.expanduser().resolve()
    try:
        if args.extract_redist:
            dest = args.extract_redist.expanduser().resolve()
            extracted = extract_redist(dmg, dest, force=args.force)
            payload = {"ok": True, "dmg": str(dmg), "redist": str(extracted)}
        else:
            with attached_dmg(dmg) as mounts:
                found = False
                for mount in mounts:
                    try:
                        redist_root_for_mount(mount)
                        found = True
                        break
                    except GptkDmgError:
                        continue
                if not found:
                    raise GptkDmgError(f"no GPTK redist layout found in {dmg}")
                payload = {"ok": True, "dmg": str(dmg), "redist_layout": "found", "mounts": [str(m) for m in mounts]}
    except Exception as exc:  # pragma: no cover - user-facing CLI errors
        if args.json:
            print(json.dumps({"ok": False, "error": str(exc)}, sort_keys=True), file=sys.stderr)
        else:
            print(f"error: {exc}", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps(payload, sort_keys=True))
    else:
        print(payload.get("redist") or f"validated GPTK redist layout in {payload['dmg']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
