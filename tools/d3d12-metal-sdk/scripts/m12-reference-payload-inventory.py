#!/usr/bin/env python3
"""Inventory local M12 reference payloads: Agility SDK and DXC caches."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def sha256(path: Path) -> str | None:
    if not path.exists() or not path.is_file():
        return None
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def inventory_agility(root: Path) -> list[dict]:
    rows = []
    if not root.exists():
        return rows
    for version_dir in sorted(p for p in root.iterdir() if p.is_dir()):
        bin_dir = version_dir / "build/native/bin/x64"
        rows.append({
            "kind": "agility",
            "version": version_dir.name,
            "root": str(version_dir),
            "x64_bin": str(bin_dir),
            "D3D12Core_exists": (bin_dir / "D3D12Core.dll").exists(),
            "d3d12SDKLayers_exists": (bin_dir / "d3d12SDKLayers.dll").exists(),
            "D3D12Core_sha256": sha256(bin_dir / "D3D12Core.dll"),
            "d3d12SDKLayers_sha256": sha256(bin_dir / "d3d12SDKLayers.dll"),
        })
    return rows


def inventory_dxc(root: Path) -> list[dict]:
    rows = []
    if not root.exists():
        return rows
    for p in sorted(root.rglob("*")):
        if not p.is_file():
            continue
        if p.name.lower() in {"dxc", "dxc.exe", "dxcompiler.dll", "dxil.dll"} or p.suffix.lower() in {".zip", ".tar", ".gz"}:
            rows.append({"kind": "dxc", "path": str(p), "size": p.stat().st_size, "sha256": sha256(p)})
    return rows


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--agility-root", type=Path, action="append", default=[])
    ap.add_argument("--dxc-root", type=Path, action="append", default=[])
    ap.add_argument("--results-dir", type=Path, default=Path("tools/d3d12-metal-sdk/results/m12-reference-payloads/latest"))
    args = ap.parse_args()
    if not args.agility_root:
        args.agility_root = [Path.home() / ".metalsharp/runtime/redist/agility", Path("tools/d3d12-metal-sdk/out/agility")]
    if not args.dxc_root:
        args.dxc_root = [Path("tools/d3d12-metal-sdk/cache/dxc"), Path("tools/d3d12-metal-sdk/out/bin")]
    agility = []
    for root in args.agility_root:
        agility.extend(inventory_agility(root.expanduser()))
    dxc = []
    for root in args.dxc_root:
        dxc.extend(inventory_dxc(root.expanduser()))
    report = {"schema": "metalsharp.m12.reference-payload-inventory.v1", "agility": agility, "dxc": dxc}
    args.results_dir.mkdir(parents=True, exist_ok=True)
    (args.results_dir / "reference-payload-inventory.json").write_text(json.dumps(report, indent=2) + "\n")
    md = ["# M12 reference payload inventory", "", "## Agility", "", "| version | core | layers | root |", "|---|---:|---:|---|"]
    for r in agility:
        md.append(f"| `{r['version']}` | `{r['D3D12Core_exists']}` | `{r['d3d12SDKLayers_exists']}` | `{r['root']}` |")
    md += ["", "## DXC", "", "| path | size | sha256 |", "|---|---:|---|"]
    for r in dxc:
        md.append(f"| `{r['path']}` | {r['size']} | `{r['sha256']}` |")
    (args.results_dir / "reference-payload-inventory.md").write_text("\n".join(md) + "\n")
    print(args.results_dir / "reference-payload-inventory.md")
    print(args.results_dir / "reference-payload-inventory.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
