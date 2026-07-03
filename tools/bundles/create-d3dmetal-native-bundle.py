#!/usr/bin/env python3
"""Build a MetalSharp `d3dmetal_native` contract bundle.

Because the D3DMetal translator payload (D3DMetal.framework,
libd3dshared.dylib, and the PE/Unix route DLLs) is Apple-proprietary and
licensed under Apple's Game Porting Toolkit SLA, MetalSharp NEVER redistributes
the binaries. This builder emits an EMPTY contract bundle:

  - the manifest schema template (no binary hashes)
  - the user-provided payload staging instructions
  - pointers to the verifier/stager tools

A developer or end user then stages the actual payload from their own GPTK
source using `tools/runtime/stage-d3dmetal-native-payload.py`. If a payload is
already staged locally, `--from-staged` can snapshot a manifest-only bundle
(referencing the staged hashes) for development tracking — but the bundle
itself never contains Apple binaries.

Offline-only.

Usage:
    create-d3dmetal-native-bundle.py [--out dist/bundles] [--from-staged ~/.metalsharp/runtime/wine/lib/d3dmetal_native]
"""

from __future__ import annotations

import argparse
import json
import sys
import tarfile
import tempfile
from pathlib import Path
from typing import Iterable

PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUT = PROJECT_ROOT / "dist" / "bundles"
BUNDLE_NAME = "metalsharp-d3dmetal-native-contract.tar.zst"

CONTRACT_MANIFEST = {
    "schema_version": 1,
    "route_id": "d3dmetal_native",
    "version": "contract-only",
    "source_type": "contract",  # no binaries shipped
    "architecture": "x86_64",
    "minimum_macos": "14.0",
    "license_status": "contract_only",
    "license_note": (
        "MetalSharp ships only the d3dmetal_native payload CONTRACT and tooling, "
        "never the Apple D3DMetal binaries (Apple Game Porting Toolkit SLA). "
        "Stage the payload from a user/developer GPTK source via "
        "tools/runtime/stage-d3dmetal-native-payload.py."
    ),
    "payload_layout": {
        "lib/d3dmetal_native/manifest.json": "this contract, filled with hashes after staging",
        "lib/d3dmetal_native/x86_64-windows/": "PE route DLLs (d3d10/11/12, dxgi, nvapi64, nvngx-on-metalfx)",
        "lib/d3dmetal_native/x86_64-unix/": "Unix .so symlinks -> external/libd3dshared.dylib",
        "lib/d3dmetal_native/external/libd3dshared.dylib": "Apple D3DMetal shared translator",
        "lib/d3dmetal_native/external/D3DMetal.framework/": "Apple D3DMetal framework + compiler sidecars",
    },
    "required_pe_dlls": ["d3d10.dll", "d3d11.dll", "d3d12.dll", "dxgi.dll", "nvapi64.dll", "nvngx-on-metalfx.dll"],
    "required_framework_resources": ["default.metallib", "libdxccontainer.dylib", "libdxcompiler.dylib",
                                     "libdxilconv.dylib", "libmetalirconverter.dylib"],
    "verifier": "tools/runtime/check-d3dmetal-native-payload.py",
    "stager": "tools/runtime/stage-d3dmetal-native-payload.py",
    "host_abi_gate": "tools/runtime/check-d3dmetal-shim-abi.py --strict",
}

PAYLOAD_INSTRUCTIONS = """\
# d3dmetal_native payload — staging instructions

MetalSharp ships the d3dmetal_native payload CONTRACT and tooling only.
The Apple D3DMetal binaries are NOT bundled (Apple Game Porting Toolkit SLA).

## To stage a payload locally (developer or end user)

You need a Game Porting Toolkit source (the GPTK .dmg or an extracted redist/):

    tools/runtime/stage-d3dmetal-native-payload.py /path/to/gptk.dmg
    # or: /path/to/extracted-redist/

Then verify it:

    tools/runtime/check-d3dmetal-native-payload.py --runtime-root ~/.metalsharp/runtime/wine --json

The Wine host ABI (winemac/win32u/ntdll rebuilt with the D3DMetal host shim,
Phase 2) must also be present:

    tools/runtime/check-d3dmetal-shim-abi.py --strict

## Redistribution

Do NOT redistribute the staged Apple binaries. This contract bundle contains
only the schema, tooling pointers, and these instructions.
"""


def write_contract_tree(out_dir: Path, from_staged: Path | None) -> Path:
    layout = out_dir / "d3dmetal_native"
    if layout.exists():
        import shutil
        shutil.rmtree(layout)
    (layout / "lib" / "d3dmetal_native").mkdir(parents=True)

    manifest = dict(CONTRACT_MANIFEST)
    receipt = None
    if from_staged is not None:
        staged_mf = from_staged / "manifest.json"
        if staged_mf.exists():
            # Snapshot the staged manifest (hashes only — no binaries copied)
            staged = json.loads(staged_mf.read_text())
            manifest["staged_snapshot"] = {
                "pe_dlls": staged.get("pe_dlls", {}),
                "unix_modules": staged.get("unix_modules", {}),
                "external": staged.get("external", {}),
                "source_type": staged.get("source_type"),
                "license_status": staged.get("license_status"),
            }
            receipt = {
                "bundle_kind": "contract_with_staged_manifest_snapshot",
                "staged_path": str(from_staged),
                "license": "Apple GPTK SLA — binaries NOT included in this bundle, manifest hashes only",
            }
        else:
            print(f"warning: --from-staged {from_staged} has no manifest.json; emitting contract-only")
    (layout / "lib" / "d3dmetal_native" / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    (layout / "lib" / "d3dmetal_native" / "STAGING.md").write_text(PAYLOAD_INSTRUCTIONS)
    if receipt:
        (layout / "lib" / "d3dmetal_native" / "receipt.json").write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n")
    return layout


def make_zst(root_payload: Path, archive: Path) -> None:
    """Create a zstd-compressed tar of the d3dmetal_native/ tree."""
    import subprocess
    archive.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(suffix=".tar", delete=False) as tmp:
        tmp_tar = Path(tmp.name)
    try:
        with tarfile.open(tmp_tar, "w") as tf:
            tf.add(root_payload, arcname="d3dmetal_native")
        # zstd via the system compressor (zstd is a MetalSharp dep)
        with archive.open("wb") as out:
            proc = subprocess.run(["zstd", "-q", "-19", "-T0", "-c", str(tmp_tar)], stdout=out, stderr=subprocess.PIPE)
            if proc.returncode != 0:
                raise RuntimeError(f"zstd failed: {proc.stderr.decode(errors='replace')}")
    finally:
        tmp_tar.unlink(missing_ok=True)


def main(argv: Iterable[str]) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--out", type=Path, default=DEFAULT_OUT, help="Output dir for the bundle")
    p.add_argument("--from-staged", type=Path, default=None,
                   help="Snapshot the manifest from an already-staged payload (hashes only; binaries never copied)")
    p.add_argument("--bundle-name", default=BUNDLE_NAME)
    args = p.parse_args(list(argv))

    from_staged = args.from_staged.expanduser().resolve() if args.from_staged else None
    if from_staged is not None and not from_staged.exists():
        print(f"error: --from-staged {from_staged} does not exist",)
        return 2

    with tempfile.TemporaryDirectory(prefix="ms-d3dmetal-bundle-") as tmp:
        layout = write_contract_tree(Path(tmp), from_staged)
        archive = args.out / args.bundle_name
        make_zst(layout, archive)

    print(f"built: {archive}")
    print(f"  kind    : contract-only ({'with staged manifest snapshot' if from_staged else 'empty contract'})")
    print(f"  license : Apple GPTK binaries NEVER included — staging tool provided separately")
    print(f"  verify  : tar -I zstd -tf {archive.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
