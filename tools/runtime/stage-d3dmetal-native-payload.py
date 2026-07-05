#!/usr/bin/env python3
"""Stage a MetalSharp `d3dmetal_native` payload from a GPTK source.

The D3DMetal translator payload (D3DMetal.framework + libd3dshared.dylib + PE
and Unix route DLLs) is Apple-proprietary, licensed under Apple's Game Porting
Toolkit SLA. MetalSharp NEVER redistributes it. This tool stages a
user/developer-provided GPTK source into MetalSharp's owned contract layout so
the `d3dmetal_native` route can consume it, and writes a manifest recording
provenance and the Apple SLA status.

Sources accepted:
  - A mounted GPTK DMG or extracted directory with redist/lib/{wine,external}
  - A .dmg path (mounted read-only, auto-unmounted on exit)

Layout produced under <runtime>/lib/d3dmetal_native/:
  manifest.json
  x86_64-windows/<pe dlls>
  x86_64-unix/<unix .so symlinks>
  external/libd3dshared.dylib
  external/D3DMetal.framework/...

Offline-only. Does not download anything.

Usage:
    stage-d3dmetal-native-payload.py <gptk_dmg_or_dir> [--runtime-root ~/.metalsharp/runtime/wine]

By default, staging also applies MetalSharp's local GPTK4 compatibility
transform with patch-d3dmetal-native-payload.py. This modifies only the local
user-staged payload and never commits or redistributes Apple binaries.
"""

from __future__ import annotations

import argparse
import contextlib
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable

DEFAULT_RUNTIME_ROOT = Path.home() / ".metalsharp" / "runtime" / "wine"
PAYLOAD_REL = Path("lib") / "d3dmetal_native"

REQUIRED_PE_DLLS = ["d3d10.dll", "d3d11.dll", "d3d12.dll", "dxgi.dll", "nvapi64.dll", "nvngx-on-metalfx.dll"]
OPTIONAL_PE_DLLS = ["d3d10_1.dll", "d3d10core.dll", "atidxx64.dll", "nvngx.dll"]
REQUIRED_FRAMEWORK_RESOURCES = ["default.metallib", "libdxccontainer.dylib", "libdxcompiler.dylib",
                                "libdxilconv.dylib", "libmetalirconverter.dylib"]


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


@contextlib.contextmanager
def mounted_dmg(dmg: Path):
    """Mount a DMG read-only (nobrowse), yield the redist root, unmount on exit."""
    mountpoint = Path(f"/tmp/ms-gptk-stage-{os.getpid()}")
    mountpoint.mkdir(parents=True, exist_ok=True)
    proc = subprocess.run(
        ["hdiutil", "attach", "-readonly", "-nobrowse", "-mountpoint", str(mountpoint), str(dmg)],
        capture_output=True, text=True, check=False,
    )
    if proc.returncode != 0:
        mountpoint.rmdir()
        raise RuntimeError(f"failed to mount {dmg}: {proc.stderr.strip()}")
    try:
        redist = mountpoint / "redist"
        if not (redist / "lib").exists() and (mountpoint / "lib").exists():
            redist = mountpoint  # already the redist root
        if not (redist / "lib" / "wine").exists():
            raise RuntimeError(f"no GPTK redist layout (redist/lib/wine) found in {dmg}")
        yield redist
    finally:
        subprocess.run(["hdiutil", "detach", str(mountpoint), "-force"], capture_output=True)
        with contextlib.suppress(OSError):
            mountpoint.rmdir()


def resolve_source(src: Path):
    """Return (redist_root, cleanup_ctx) where redist_root/lib/{wine,external} exists."""
    if src.is_dir():
        redist = src
        if not (redist / "lib" / "wine").exists() and (redist / "redist" / "lib" / "wine").exists():
            redist = redist / "redist"
        if not (redist / "lib" / "wine").exists():
            raise RuntimeError(f"{src} is not a GPTK redist layout (expected lib/wine)")
        return redist, None
    if src.suffix.lower() == ".dmg":
        return src, mounted_dmg(src)
    raise RuntimeError(f"{src} is neither a directory nor a .dmg")


def copy_preserving(src: Path, dst: Path) -> None:
    """Copy file or symlink (preserving symlinks), preserving mode."""
    dst.parent.mkdir(parents=True, exist_ok=True)
    if src.is_symlink():
        target = os.readlink(src)
        dst.unlink(missing_ok=True)
        os.symlink(target, dst)
    else:
        shutil.copy2(src, dst)


def copy_tree_preserving(src: Path, dst: Path) -> None:
    for root, dirs, files in os.walk(src, followlinks=False):
        root_path = Path(root)
        rel = root_path.relative_to(src)
        (dst / rel).mkdir(parents=True, exist_ok=True)
        # preserve symlinks to directories as symlinks
        keep_dirs = []
        for d in dirs:
            s = root_path / d
            if s.is_symlink():
                target = os.readlink(s)
                t = dst / rel / d
                t.unlink(missing_ok=True)
                os.symlink(target, t)
            else:
                keep_dirs.append(d)
        dirs[:] = keep_dirs
        for f in files:
            copy_preserving(root_path / f, dst / rel / f)


def build_manifest(payload_dir: Path, source: Path, runtime_root: Path) -> dict:
    pe_dir = payload_dir / "x86_64-windows"
    unix_dir = payload_dir / "x86_64-unix"
    ext_dir = payload_dir / "external"
    compat_receipt_path = payload_dir / "metalsharp-d3dmetal-compat-patches.json"

    pe_hashes = {}
    known_pe = REQUIRED_PE_DLLS + OPTIONAL_PE_DLLS
    for dll in sorted(known_pe + [d.name for d in pe_dir.glob("*.dll") if d.name not in known_pe]):
        f = pe_dir / dll
        if f.exists() and not f.is_symlink():
            pe_hashes[dll] = {"sha256": sha256_file(f), "size": f.stat().st_size}

    unix_entries = {}
    for so in sorted(unix_dir.glob("*.so")):
        unix_entries[so.name] = {"target": os.readlink(so) if so.is_symlink() else None,
                                 "sha256": sha256_file(so) if not so.is_symlink() else None}

    shared = ext_dir / "libd3dshared.dylib"
    fw = ext_dir / "D3DMetal.framework" / "Versions" / "A"
    external = {
        "libd3dshared.dylib": {"sha256": sha256_file(shared), "size": shared.stat().st_size} if shared.exists() else None,
        "D3DMetal.framework": {
            "binary": {"sha256": sha256_file(fw / "D3DMetal"), "size": (fw / "D3DMetal").stat().st_size} if (fw / "D3DMetal").exists() else None,
            "resources": {r: {"present": (fw / "Resources" / r).exists()} for r in REQUIRED_FRAMEWORK_RESOURCES},
        },
    }

    compat_transform = None
    if compat_receipt_path.exists():
        try:
            receipt = json.loads(compat_receipt_path.read_text())
            compat_transform = {
                "receipt": compat_receipt_path.name,
                "patches": [p.get("id") for p in receipt.get("patches", [])],
                "applied_at": receipt.get("applied_at"),
            }
        except json.JSONDecodeError:
            compat_transform = {"receipt": compat_receipt_path.name, "error": "invalid json"}

    return {
        "schema_version": 1,
        "route_id": "d3dmetal_native",
        # GPTK 4 beta 1 ships Apple D3DMetal 4.0; track the source label.
        "version": "4.0.0-beta1",
        "source_type": "developer_seeded",
        "source_path": str(source),
        "architecture": "x86_64",
        "minimum_macos": "14.0",
        # Apple-proprietary: must not be redistributed. Usable locally under the
        # GPTK SLA only. The MetalSharp public bundle NEVER contains these binaries.
        "license_status": "proprietary_apple_gptk_not_redistributable",
        "license_note": "Apple Game Porting Toolkit SLA. For local developer use only; not for redistribution by MetalSharp.",
        "staged_at": datetime.now(timezone.utc).isoformat(),
        "staged_by": os.environ.get("USER", "unknown"),
        "runtime_root": str(runtime_root),
        "pe_dlls": pe_hashes,
        "unix_modules": unix_entries,
        "external": external,
        "compatibility_transform": compat_transform,
    }


def apply_compat_transform(payload_dir: Path) -> None:
    patcher = Path(__file__).resolve().with_name("patch-d3dmetal-native-payload.py")
    if not patcher.exists():
        raise RuntimeError(f"compatibility patcher missing: {patcher}")
    proc = subprocess.run([sys.executable, str(patcher), str(payload_dir)], capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        stderr = proc.stderr.strip() or proc.stdout.strip()
        raise RuntimeError(f"D3DMetal compatibility transform failed: {stderr}")


def stage(source: Path, runtime_root: Path, force: bool, apply_compat_patch: bool = True) -> Path:
    payload_dir = runtime_root / PAYLOAD_REL
    if payload_dir.exists() and not force:
        raise RuntimeError(f"{payload_dir} already exists (use --force to overwrite)")

    redist_root, ctx = resolve_source(source)
    with ctx or _nullctx() as mounted_redist_root:
        if mounted_redist_root is not None:
            redist_root = mounted_redist_root
        pe_src = redist_root / "lib" / "wine" / "x86_64-windows"
        unix_src = redist_root / "lib" / "wine" / "x86_64-unix"
        ext_src = redist_root / "lib" / "external"

        missing = [s for s, p in [("pe", pe_src), ("unix", unix_src), ("external", ext_src)] if not p.exists()]
        if missing:
            raise RuntimeError(f"GPTK source missing layout dirs: {missing}")

        if payload_dir.exists():
            shutil.rmtree(payload_dir)
        (payload_dir / "x86_64-windows").mkdir(parents=True)
        (payload_dir / "x86_64-unix").mkdir(parents=True)
        (payload_dir / "external").mkdir(parents=True)

        for dll in REQUIRED_PE_DLLS:
            src = pe_src / dll
            if not src.exists():
                raise RuntimeError(f"source missing required PE DLL: {dll}")
            copy_preserving(src, payload_dir / "x86_64-windows" / dll)
        # Optional PE DLLs (including D3D10.1/core compatibility members) and any
        # future payload DLLs are carried over if present.
        for extra in pe_src.glob("*.dll"):
            if extra.name not in REQUIRED_PE_DLLS and not (payload_dir / "x86_64-windows" / extra.name).exists():
                copy_preserving(extra, payload_dir / "x86_64-windows" / extra.name)

        # Unix modules are symlinks to libd3dshared.dylib. In the GPTK layout they
        # point at ../../external/libd3dshared.dylib (lib/wine/x86_64-unix -> lib/external);
        # in the MetalSharp d3dmetal_native/ layout the target is ../external/ (one up).
        # Rewrite the symlink so it resolves correctly in the staged tree.
        for so in unix_src.glob("*.so"):
            target = os.readlink(so) if so.is_symlink() else None
            if target and target.endswith("libd3dshared.dylib"):
                target = "../external/libd3dshared.dylib"
            dst_so = payload_dir / "x86_64-unix" / so.name
            if target:
                dst_so.unlink(missing_ok=True)
                os.symlink(target, dst_so)
            else:
                copy_preserving(so, dst_so)

        copy_tree_preserving(ext_src, payload_dir / "external")

        if apply_compat_patch:
            apply_compat_transform(payload_dir)

        manifest = build_manifest(payload_dir, source, runtime_root)
        (payload_dir / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")

    return payload_dir


@contextlib.contextmanager
def _nullctx():
    yield


def main(argv: Iterable[str]) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("source", type=Path, help="GPTK .dmg or extracted redist/ directory")
    p.add_argument("--runtime-root", type=Path, default=DEFAULT_RUNTIME_ROOT, help="Wine runtime root")
    p.add_argument("--force", action="store_true", help="Overwrite an existing staged payload")
    p.add_argument("--skip-compat-patch", action="store_true", help="Stage raw payload without applying MetalSharp's local GPTK4 compatibility transform")
    args = p.parse_args(list(argv))

    runtime_root = args.runtime_root.expanduser().resolve()
    source = args.source.expanduser().resolve()
    if not source.exists():
        print(f"error: source not found: {source}", file=sys.stderr)
        return 2

    try:
        payload_dir = stage(source, runtime_root, args.force, apply_compat_patch=not args.skip_compat_patch)
    except Exception as exc:  # pragma: no cover - user-facing errors
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"staged d3dmetal_native payload -> {payload_dir}")
    print(f"  source      : {source}")
    print(f"  license     : proprietary_apple_gptk_not_redistributable (NOT bundled by MetalSharp)")
    pe = payload_dir / "x86_64-windows"
    fw = payload_dir / "external" / "D3DMetal.framework" / "Versions" / "A"
    print(f"  pe dlls     : {len(list(pe.glob('*.dll')))}")
    print(f"  unix modules: {len(list((payload_dir / 'x86_64-unix').glob('*.so')))}")
    receipt = payload_dir / "metalsharp-d3dmetal-compat-patches.json"
    print(f"  framework   : {'present' if (fw / 'D3DMetal').exists() else 'MISSING'}")
    print(f"  compat patch: {'applied' if receipt.exists() else 'skipped'}")
    print("next: verify with check-d3dmetal-native-payload.py --runtime-root", str(runtime_root))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
