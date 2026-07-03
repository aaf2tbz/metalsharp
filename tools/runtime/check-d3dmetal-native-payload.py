#!/usr/bin/env python3
"""Verify a MetalSharp `d3dmetal_native` payload against its manifest contract.

The `d3dmetal_native` route consumes an Apple D3DMetal translator payload
(D3DMetal.framework + libd3dshared.dylib + PE/Unix route DLLs) repackaged under
MetalSharp's contract. The payload binaries themselves are Apple-proprietary
(under Apple's Game Porting Toolkit SLA) and are NEVER redistributed by
MetalSharp; they are staged from a user/developer-provided GPTK source by
`stage-d3dmetal-native-payload.py`. This verifier checks that a staged payload
is complete, x86_64, and that the Wine host ABI is ready.

Offline-only: no network access. Exits 0 only when status == ready.

Usage:
    check-d3dmetal-native-payload.py --runtime-root ~/.metalsharp/runtime/wine [--json]
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

DEFAULT_RUNTIME_ROOT = Path.home() / ".metalsharp" / "runtime" / "wine"
PAYLOAD_DIR_NAME = Path("lib") / "d3dmetal_native"
MANIFEST_NAME = "manifest.json"

# x86_64-only — the D3DMetal translator stack is Mach-O x86_64 (see roadmap).
REQUIRED_ARCH = "x86_64"

# Required PE route DLLs (thin PE thunk into the Unix translator).
REQUIRED_PE_DLLS = ["d3d10.dll", "d3d11.dll", "d3d12.dll", "dxgi.dll", "nvapi64.dll", "nvngx-on-metalfx.dll"]
OPTIONAL_PE_DLLS = ["atidxx64.dll", "nvngx.dll"]  # present only for some payloads

# Required framework sidecars (the shader compiler / metallib contract).
REQUIRED_FRAMEWORK_RESOURCES = [
    "default.metallib",
    "libdxccontainer.dylib",
    "libdxcompiler.dylib",
    "libdxilconv.dylib",
    "libmetalirconverter.dylib",
]

# Verifier result states (the roadmap Phase 3 gate contract).
STATE_READY = "ready"
STATE_PAYLOAD_MISSING = "payload_missing"
STATE_WRONG_ARCHITECTURE = "wrong_architecture"
STATE_SIDECAR_MISSING = "sidecar_missing"
STATE_HOST_ABI_MISSING = "host_abi_missing"
STATE_UNSUPPORTED_OS = "unsupported_os"
STATE_LICENSE_NOT_APPROVED = "license_not_approved"


@dataclass
class CheckResult:
    state: str
    reason: str
    details: dict

    @property
    def ready(self) -> bool:
        return self.state == STATE_READY


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def file_arch(path: Path) -> str:
    """Return the architecture of a Mach-O/PE file via `file`, or '' on failure."""
    try:
        out = subprocess.run(["file", "-b", str(path)], capture_output=True, text=True, check=False).stdout.lower()
    except FileNotFoundError:
        return ""
    if "pe32+" in out and "x86-64" in out:
        return "x86_64-pe"
    if "mach-o" in out and "x86_64" in out:
        return "x86_64"
    if "mach-o" in out and "arm64" in out:
        return "arm64"
    return out.strip()[:60]


def load_manifest(payload_dir: Path) -> dict | None:
    mf = payload_dir / MANIFEST_NAME
    if not mf.exists():
        return None
    try:
        return json.loads(mf.read_text())
    except json.JSONDecodeError:
        return None


def host_abi_ready(runtime_root: Path) -> tuple[bool, str]:
    """Run the D3DMetal host-ABI shim checker (Phase 2 gate) against this runtime."""
    checker = Path(__file__).resolve().parent / "check-d3dmetal-shim-abi.py"
    if not checker.exists():
        return False, "shim-abi checker not found"
    try:
        proc = subprocess.run(
            [sys.executable, str(checker), "--runtime-root", str(runtime_root), "--json"],
            capture_output=True, text=True, check=False,
        )
    except Exception as exc:  # pragma: no cover - defensive
        return False, f"shim-abi checker failed: {exc}"
    if proc.returncode != 0 and not proc.stdout:
        return False, f"shim-abi checker exited {proc.returncode}"
    try:
        payload = json.loads(proc.stdout)
    except json.JSONDecodeError:
        return False, "shim-abi checker produced no JSON"
    ready = bool(payload.get("summary", {}).get("native_d3dmetal_host_abi_ready"))
    return ready, "ok" if ready else "native_d3dmetal_host_abi_ready is false"


def verify(payload_dir: Path, runtime_root: Path, *, skip_host_abi: bool = False) -> CheckResult:
    if not payload_dir.exists():
        return CheckResult(STATE_PAYLOAD_MISSING, f"{payload_dir} does not exist", {"payload_dir": str(payload_dir)})

    manifest = load_manifest(payload_dir)
    if manifest is None:
        return CheckResult(STATE_PAYLOAD_MISSING, f"{MANIFEST_NAME} missing or invalid in {payload_dir}", {})

    # License gate: Apple-proprietary payloads must never be treated as
    # redistributable MetalSharp assets. A staged payload is usable locally
    # under its own SLA; the verifier still reports readiness, but records status.
    license_status = str(manifest.get("license_status", "")).lower()
    if license_status == "blocked":
        return CheckResult(STATE_LICENSE_NOT_APPROVED, "manifest license_status is 'blocked'", {})

    # OS floor (D3DMetal requires Sonoma+ for non-native code region support).
    macos_major = int(platform.mac_ver()[0].split(".")[0] or "0") if platform.system() == "Darwin" else 0
    min_macos = str(manifest.get("minimum_macos", "14"))
    try:
        required_major = int(min_macos.split(".")[0])
    except ValueError:
        required_major = 14
    if macos_major and macos_major < required_major:
        return CheckResult(STATE_UNSUPPORTED_OS, f"macOS {macos_major} < required {min_macos}", {"macos": macos_major, "minimum": min_macos})

    # PE DLLs
    pe_dir = payload_dir / "x86_64-windows"
    missing_pe = [d for d in REQUIRED_PE_DLLS if not (pe_dir / d).exists()]
    if missing_pe:
        return CheckResult(STATE_PAYLOAD_MISSING, f"missing PE route DLLs: {missing_pe}", {"missing": missing_pe})

    bad_arch = []
    for d in REQUIRED_PE_DLLS:
        arch = file_arch(pe_dir / d)
        if "x86" not in arch:
            bad_arch.append((d, arch))
    if bad_arch:
        return CheckResult(STATE_WRONG_ARCHITECTURE, f"PE DLLs not x86_64: {bad_arch}", {"bad_arch": bad_arch})

    # external/ — libd3dshared.dylib + D3DMetal.framework
    ext_dir = payload_dir / "external"
    shared = ext_dir / "libd3dshared.dylib"
    if not shared.exists():
        return CheckResult(STATE_SIDECAR_MISSING, "external/libd3dshared.dylib missing", {})
    if "x86_64" not in file_arch(shared):
        return CheckResult(STATE_WRONG_ARCHITECTURE, "libd3dshared.dylib not x86_64", {})

    fw = ext_dir / "D3DMetal.framework" / "Versions" / "A"
    fw_bin = fw / "D3DMetal"
    if not fw_bin.exists():
        return CheckResult(STATE_SIDECAR_MISSING, "D3DMetal.framework/Versions/A/D3DMetal missing", {})
    if "x86_64" not in file_arch(fw_bin):
        return CheckResult(STATE_WRONG_ARCHITECTURE, "D3DMetal framework binary not x86_64", {})

    res_dir = fw / "Resources"
    missing_sidecars = [r for r in REQUIRED_FRAMEWORK_RESOURCES if not (res_dir / r).exists()]
    if missing_sidecars:
        return CheckResult(STATE_SIDECAR_MISSING, f"missing framework resources: {missing_sidecars}", {"missing": missing_sidecars})

    # Unix sidecar modules (symlinks to libd3dshared.dylib are expected).
    unix_dir = payload_dir / "x86_64-unix"
    expected_unix = [Path(p).with_suffix(".so").name for p in REQUIRED_PE_DLLS]
    missing_unix = []
    broken_unix = []
    for s in expected_unix:
        p = unix_dir / s
        if not p.is_symlink() and not p.exists():
            missing_unix.append(s)
        elif p.is_symlink() and not p.exists():
            broken_unix.append((s, os.readlink(p)))
    if missing_unix:
        return CheckResult(STATE_SIDECAR_MISSING, f"missing unix modules: {missing_unix}", {"missing": missing_unix})
    if broken_unix:
        return CheckResult(STATE_SIDECAR_MISSING, f"broken unix symlinks (target not found): {broken_unix}", {"broken": broken_unix})

    # Host ABI (Phase 2 gate) — required for the payload to actually run.
    if not skip_host_abi:
        ok, why = host_abi_ready(runtime_root)
        if not ok:
            return CheckResult(STATE_HOST_ABI_MISSING, why, {})

    return CheckResult(STATE_READY, "d3dmetal_native payload is complete and x86_64", {
        "pe_dlls": REQUIRED_PE_DLLS,
        "framework": str(fw),
        "license_status": license_status or "unspecified",
        "source_type": str(manifest.get("source_type", "unspecified")),
    })


def main(argv: Iterable[str]) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--runtime-root", type=Path, default=DEFAULT_RUNTIME_ROOT, help="Wine runtime root")
    p.add_argument("--payload-dir", type=Path, default=None, help="Override payload dir (default <runtime>/lib/d3dmetal_native)")
    p.add_argument("--skip-host-abi", action="store_true", help="Skip the Wine host-ABI shim check")
    p.add_argument("--json", action="store_true", help="Emit JSON")
    args = p.parse_args(list(argv))

    runtime_root = args.runtime_root.expanduser().resolve()
    payload_dir = args.payload_dir or (runtime_root / PAYLOAD_DIR_NAME)
    result = verify(payload_dir, runtime_root, skip_host_abi=args.skip_host_abi)

    payload = {
        "runtime_root": str(runtime_root),
        "payload_dir": str(payload_dir),
        "state": result.state,
        "ready": result.ready,
        "reason": result.reason,
        "details": result.details,
    }
    if args.json:
        print(json.dumps(payload, indent=2, sort_keys=True))
    else:
        print(f"d3dmetal_native payload: {payload_dir}")
        print(f"  state : {result.state}")
        print(f"  ready : {result.ready}")
        print(f"  reason: {result.reason}")
        if result.details:
            for k, v in result.details.items():
                print(f"  {k}: {v}")
    return 0 if result.ready else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
