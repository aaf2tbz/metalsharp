#!/usr/bin/env python3
"""Materialize a standalone M12/DXGI bootstrap directory.

Copies the D3D12/DXGI/WineMetal runtime cohort into a game-local directory so
standalone Windows executables resolve ``dxgi.dll`` next to the executable and
that bootstrap can load the required same-directory ``dxgi_dxmt.dll`` sidecar.
It also stages UE/Agility-style ``.\D3D12\x64\`` sidecars when present.
"""
from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path
from typing import Iterable

REQUIRED_RUNTIME_DLLS = [
    "dxgi.dll",
    "dxgi_dxmt.dll",
    "d3d12.dll",
    "d3d11.dll",
    "d3d10core.dll",
    "winemetal.dll",
]

AGILITY_SIDECARS = [
    "D3D12Core.dll",
    "d3d12SDKLayers.dll",
    "D3D12StateObjectCompiler.dll",
    "dxil.dll",
]

# Source contract for probe_standalone_dxgi_bootstrap: standalone staging covers
# the UE/Agility hint literal `.\\D3D12\\x64\\` for D3D12Core.dll and
# d3d12SDKLayers.dll sidecars.
UE_AGILITY_HINT = ".\\D3D12\\x64\\"
MANIFEST_NAME = "standalone-stage.json"


def copy_if_present(src: Path, dst: Path, *, dry_run: bool) -> bool:
    if not src.exists():
        return False
    if not dry_run:
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
    return True


def materialize(runtime_dir: Path, output_dir: Path, *, dry_run: bool = False) -> dict:
    windows_dir = runtime_dir / "x86_64-windows"
    staged: list[dict] = []
    missing: list[str] = []

    for dll in REQUIRED_RUNTIME_DLLS:
        src = windows_dir / dll
        dst = output_dir / dll
        ok = copy_if_present(src, dst, dry_run=dry_run)
        (staged if ok else missing).append(
            {"name": dll, "src": str(src), "dst": str(dst)} if ok else dll
        )

    # UE-style Agility sidecars are commonly probed under .\D3D12\x64\, with
    # parent/local fallback.  Preserve that exact layout for standalone games.
    agility_root = output_dir / "D3D12" / "x64"
    root = windows_dir / "D3D12"
    for dll in AGILITY_SIDECARS:
        candidates: Iterable[Path] = (
            root / "x64" / dll,
            root / dll,
            windows_dir / dll,
        )
        copied = False
        for src in candidates:
            if copy_if_present(src, agility_root / dll, dry_run=dry_run):
                resolved = src.resolve()
                staged.append(
                    {
                        "name": dll,
                        "src": str(src),
                        "dst": str(agility_root / dll),
                        "source_parent": str(resolved.parent),
                    }
                )
                copied = True
                break
        if not copied and dll in {"D3D12Core.dll", "d3d12SDKLayers.dll"}:
            missing.append(f"{UE_AGILITY_HINT}{dll}")

    manifest = {
        "schema": "metalsharp.d3d12-metal.standalone-stage.v1",
        "runtime_dir": str(runtime_dir),
        "windows_dir": str(windows_dir),
        "output_dir": str(output_dir),
        "dxgi_bootstrap": "dxgi.dll",
        "dxgi_sidecar": "dxgi_dxmt.dll",
        "required_runtime_dlls": REQUIRED_RUNTIME_DLLS,
        "ue_agility_hint": UE_AGILITY_HINT,
        "agility_sidecars": AGILITY_SIDECARS,
        "staged": staged,
        "missing": missing,
        "pass": not missing,
        "dry_run": dry_run,
    }
    if not dry_run:
        output_dir.mkdir(parents=True, exist_ok=True)
        (output_dir / MANIFEST_NAME).write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description="Stage a standalone M12 DXGI/D3D12 runtime cohort.")
    parser.add_argument("--runtime-dir", type=Path, default=Path.home() / ".metalsharp" / "runtime" / "wine" / "lib" / "dxmt_m12")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--stdout", action="store_true")
    args = parser.parse_args()

    manifest = materialize(args.runtime_dir, args.output_dir, dry_run=args.dry_run)
    if args.stdout or args.dry_run:
        print(json.dumps(manifest, indent=2))
    return 0 if manifest["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
