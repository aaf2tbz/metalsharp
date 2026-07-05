#!/usr/bin/env python3
"""Apply MetalSharp's D3DMetal/GPTK4 payload compatibility transform.

The D3DMetal PE DLLs are Apple-proprietary payload files staged locally by the
user/developer. MetalSharp must not redistribute modified copies. This tool
therefore patches only a local staged payload directory, after staging, and
records a receipt with before/after hashes.

The transform keeps Wine-side runtime changes narrow: for known GPTK4 PE thunk
DLLs, force create/thread entrypoints through the already-present unix-call
fallback paths instead of direct D3DMetal native callbacks that can return with
Darwin pthread TLS/GS state dirty under Rosetta.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable


PAYLOAD_REL = Path("lib") / "d3dmetal_native"
RECEIPT_NAME = "metalsharp-d3dmetal-compat-patches.json"


@dataclass(frozen=True)
class PatchSpec:
    dll: str
    patch_id: str
    description: str
    pattern: bytes
    offset: int
    old: bytes
    new: bytes


PATCHES: tuple[PatchSpec, ...] = (
    PatchSpec(
        dll="dxgi.dll",
        patch_id="dxgi-thunk-thread-force-unixcall",
        description="Force Thunk_Thread through the existing unix-call fallback instead of GFXT_ThreadCallback.",
        pattern=bytes.fromhex("4883ec284989c8488b05524300014885c074074c89c1ffd0eb12"),
        offset=17,
        old=bytes.fromhex("7407"),
        new=bytes.fromhex("eb07"),
    ),
    PatchSpec(
        dll="dxgi.dll",
        patch_id="dxgi-createfactory2-force-unixcall",
        description="Force CreateDXGIFactory2 through the existing unix-call fallback.",
        pattern=bytes.fromhex("488b05cd4100014885c07407"),
        offset=10,
        old=bytes.fromhex("7407"),
        new=bytes.fromhex("eb07"),
    ),
    PatchSpec(
        dll="d3d12.dll",
        patch_id="d3d12-createdevice-force-unixcall",
        description="Force D3D12CreateDevice through the existing unix-call fallback.",
        pattern=bytes.fromhex("488b05323800004885c07411"),
        offset=10,
        old=bytes.fromhex("7411"),
        new=bytes.fromhex("eb11"),
    ),
    PatchSpec(
        dll="d3d11.dll",
        patch_id="d3d11-createdevice-force-unixcall",
        description="Force D3D11CreateDevice through the existing unix-call fallback.",
        pattern=bytes.fromhex("488b055a2c00004885c0740a"),
        offset=10,
        old=bytes.fromhex("740a"),
        new=bytes.fromhex("eb0a"),
    ),
)


class PatchError(RuntimeError):
    pass


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def resolve_payload_dir(path: Path) -> Path:
    path = path.expanduser().resolve()
    if (path / "x86_64-windows").is_dir():
        return path
    candidate = path / PAYLOAD_REL
    if (candidate / "x86_64-windows").is_dir():
        return candidate
    raise PatchError(f"no d3dmetal_native payload found at {path} (expected x86_64-windows/)")


def find_unique(data: bytes, pattern: bytes, label: str) -> int:
    first = data.find(pattern)
    if first < 0:
        raise PatchError(f"{label}: expected GPTK4 byte pattern not found")
    second = data.find(pattern, first + 1)
    if second >= 0:
        raise PatchError(f"{label}: byte pattern is not unique")
    return first


def apply_spec(payload_dir: Path, spec: PatchSpec, *, dry_run: bool, backup_dir: Path | None) -> dict:
    pe_path = payload_dir / "x86_64-windows" / spec.dll
    if not pe_path.is_file():
        raise PatchError(f"{spec.dll}: missing {pe_path}")

    data = bytearray(pe_path.read_bytes())
    before_sha = sha256_bytes(data)
    try:
        start = find_unique(data, spec.pattern, spec.patch_id)
    except PatchError:
        patched_pattern = bytearray(spec.pattern)
        patched_pattern[spec.offset : spec.offset + len(spec.old)] = spec.new
        start = find_unique(data, bytes(patched_pattern), spec.patch_id)
    patch_at = start + spec.offset
    current = bytes(data[patch_at : patch_at + len(spec.old)])

    if current == spec.new:
        return {
            "id": spec.patch_id,
            "dll": spec.dll,
            "description": spec.description,
            "status": "already_applied",
            "file_offset": patch_at,
            "before_sha256": before_sha,
            "after_sha256": before_sha,
        }
    if current != spec.old:
        raise PatchError(
            f"{spec.patch_id}: unsupported bytes at 0x{patch_at:x}: "
            f"expected {spec.old.hex()} or {spec.new.hex()}, found {current.hex()}"
        )

    after = bytearray(data)
    after[patch_at : patch_at + len(spec.old)] = spec.new
    after_sha = sha256_bytes(after)

    if not dry_run:
        if backup_dir is not None:
            backup_dir.mkdir(parents=True, exist_ok=True)
            backup_path = backup_dir / spec.dll
            if not backup_path.exists():
                shutil.copy2(pe_path, backup_path)
        pe_path.write_bytes(after)

    return {
        "id": spec.patch_id,
        "dll": spec.dll,
        "description": spec.description,
        "status": "would_apply" if dry_run else "applied",
        "file_offset": patch_at,
        "old_bytes": spec.old.hex(),
        "new_bytes": spec.new.hex(),
        "before_sha256": before_sha,
        "after_sha256": after_sha,
    }


def patch_payload(payload_dir: Path, *, dry_run: bool = False, no_backup: bool = False) -> dict:
    payload_dir = resolve_payload_dir(payload_dir)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    backup_dir = None if no_backup or dry_run else payload_dir / ".metalsharp-compat-backups" / stamp

    results = [apply_spec(payload_dir, spec, dry_run=dry_run, backup_dir=backup_dir) for spec in PATCHES]
    receipt = {
        "schema_version": 1,
        "tool": Path(__file__).name,
        "applied_at": datetime.now(timezone.utc).isoformat(),
        "payload_dir": str(payload_dir),
        "dry_run": dry_run,
        "backup_dir": str(backup_dir) if backup_dir else None,
        "patches": results,
        "license_note": "Local compatibility transform only. Do not redistribute Apple D3DMetal/GPTK payload binaries.",
    }

    if not dry_run:
        (payload_dir / RECEIPT_NAME).write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n")
    return receipt


def main(argv: Iterable[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("payload_or_runtime_root", type=Path, help="Payload dir or runtime root containing lib/d3dmetal_native")
    parser.add_argument("--check", action="store_true", help="Validate/apply virtually without modifying files")
    parser.add_argument("--no-backup", action="store_true", help="Do not save pre-patch backups next to the payload")
    parser.add_argument("--json", action="store_true", help="Emit the full JSON receipt")
    args = parser.parse_args(list(argv))

    try:
        receipt = patch_payload(args.payload_or_runtime_root, dry_run=args.check, no_backup=args.no_backup)
    except PatchError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps(receipt, indent=2, sort_keys=True))
    else:
        state = "validated" if args.check else "patched"
        print(f"d3dmetal_native payload {state}: {receipt['payload_dir']}")
        for patch in receipt["patches"]:
            print(f"  {patch['id']}: {patch['status']} ({patch['dll']} @ 0x{patch['file_offset']:x})")
        if receipt.get("backup_dir"):
            print(f"  backup: {receipt['backup_dir']}")
        if not args.check:
            print(f"  receipt: {Path(receipt['payload_dir']) / RECEIPT_NAME}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
