#!/usr/bin/env python3
"""Inspect a MetalSharp Wine runtime for CrossOver-style D3DMetal/DXMT host ABI hooks.

This is intentionally a static checker. It does not execute Wine or load the
runtime modules. By default it reports readiness and exits 0 so developers can
run it against today's partial runtime. Pass --strict to fail when native
D3DMetal-required hooks are missing.

Phase 1 baseline note: the D3DMetal host ABI probes currently look for the
CrossOver oracle names (CX_ACTIVE_GRAPHICS_BACKEND, WineMetalLayer,
client_surface_present, ...). In Phase 2 these are ported into MetalSharp Wine
11.5 under MetalSharp-owned names (MS_ACTIVE_GRAPHICS_BACKEND etc.); the probe
needles are updated then. CrossOver is only ever an oracle for behavior.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

DEFAULT_RUNTIME_ROOT = Path.home() / ".metalsharp" / "runtime" / "wine"
MODULE_RELATIVE_PATHS = {
    "winemac": Path("lib/wine/x86_64-unix/winemac.so"),
    "win32u": Path("lib/wine/x86_64-unix/win32u.so"),
    "ntdll": Path("lib/wine/x86_64-unix/ntdll.so"),
    "winevulkan": Path("lib/wine/x86_64-unix/winevulkan.so"),
}


@dataclass(frozen=True)
class Probe:
    module: str
    name: str
    needle: str
    source: str = "strings"  # strings or nm
    required_for: str = "native_d3dmetal"  # baseline, dxmt, dxvk, native_d3dmetal


PROBES: tuple[Probe, ...] = (
    # Existing DXMT/MoltenVK baseline: these should generally be present in the
    # current Wine 2.0 runtime and prove the module has the older macOS host path.
    Probe("winemac", "macdrv client-surface present function", "macdrv_client_surface_present", required_for="baseline"),
    Probe("winemac", "MoltenVK surface creation path", "Failed to create MoltenVK surface", required_for="dxvk"),
    Probe("win32u", "client_surface_create export", "_client_surface_create", source="nm", required_for="baseline"),
    Probe("win32u", "NtGdiDdDDIQueryAdapterInfo export", "_NtGdiDdDDIQueryAdapterInfo", source="nm", required_for="baseline"),
    Probe("winevulkan", "winevulkan unix call table", "___wine_unix_call_libs", source="nm", required_for="dxvk"),
    Probe("winevulkan", "WineVulkan wow64 unix call table", "___wine_unix_call_wow64_libs", source="nm", required_for="dxvk"),
    Probe("ntdll", "ntdll unix call dispatcher", "__wine_unix_call_dispatcher", required_for="baseline"),

    # CrossOver-style D3DMetal/DXMT host ABI. Missing entries here explain why
    # D3DMetal payload copying is not enough.
    Probe("winemac", "WineMetalLayer nextDrawable hook", "WineMetalLayer"),
    Probe("winemac", "CLIENT_SURFACE_PRESENTED event", "CLIENT_SURFACE_PRESENTED"),
    Probe("winemac", "D3DMetal client-surface storage", "d3dmetal_client_surface"),
    Probe("winemac", "macdrv client surface presented handler", "macdrv_client_surface_presented"),
    Probe("winemac", "get D3DMetal client surface helper", "macdrv_get_view_d3dmetal_client_surface"),
    Probe("winemac", "set D3DMetal client surface helper", "macdrv_set_view_d3dmetal_client_surface"),
    Probe("win32u", "active graphics backend environment gate", "CX_ACTIVE_GRAPHICS_BACKEND"),
    Probe("ntdll", "libd3dshared path discovery", "libd3dshared.dylib"),
)


@dataclass
class ModuleEvidence:
    path: Path
    exists: bool
    file_type: str = ""
    nm_text: str = ""
    strings_text: str = ""


def run_command(args: list[str]) -> str:
    try:
        return subprocess.run(args, check=False, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT).stdout
    except FileNotFoundError:
        return ""


def collect_module(path: Path) -> ModuleEvidence:
    if not path.exists():
        return ModuleEvidence(path=path, exists=False)
    return ModuleEvidence(
        path=path,
        exists=True,
        file_type=run_command(["file", str(path)]).strip(),
        nm_text=run_command(["nm", "-gU", str(path)]),
        strings_text=run_command(["strings", "-a", str(path)]),
    )


def contains(evidence: ModuleEvidence, probe: Probe) -> bool:
    haystack = evidence.nm_text if probe.source == "nm" else evidence.strings_text
    return probe.needle in haystack


def summarize(results: list[dict[str, object]]) -> dict[str, object]:
    by_class: dict[str, dict[str, int]] = {}
    for row in results:
        cls = str(row["required_for"])
        bucket = by_class.setdefault(cls, {"present": 0, "missing": 0})
        bucket["present" if row["present"] else "missing"] += 1
    native_missing = [row for row in results if row["required_for"] == "native_d3dmetal" and not row["present"]]
    baseline_missing = [row for row in results if row["required_for"] == "baseline" and not row["present"]]
    dxvk_missing = [row for row in results if row["required_for"] == "dxvk" and not row["present"]]
    return {
        "baseline_ready": not baseline_missing,
        "dxvk_winevulkan_ready": not dxvk_missing,
        "native_d3dmetal_host_abi_ready": not native_missing,
        "counts": by_class,
        "missing_native_d3dmetal": [row["name"] for row in native_missing],
    }


def emit_text(runtime_root: Path, modules: dict[str, ModuleEvidence], results: list[dict[str, object]], summary: dict[str, object]) -> None:
    print(f"Runtime root: {runtime_root}")
    print("Modules:")
    for name, evidence in modules.items():
        state = "present" if evidence.exists else "missing"
        print(f"  - {name}: {state} ({evidence.path})")
        if evidence.file_type:
            print(f"    {evidence.file_type}")
    print("\nSummary:")
    print(f"  baseline_ready: {summary['baseline_ready']}")
    print(f"  dxvk_winevulkan_ready: {summary['dxvk_winevulkan_ready']}")
    print(f"  native_d3dmetal_host_abi_ready: {summary['native_d3dmetal_host_abi_ready']}")
    print("\nProbe results:")
    for row in results:
        mark = "ok" if row["present"] else "missing"
        print(f"  [{mark}] {row['required_for']}: {row['module']} :: {row['name']} ({row['needle']})")
    if summary["missing_native_d3dmetal"]:
        print("\nNative D3DMetal blockers:")
        for item in summary["missing_native_d3dmetal"]:
            print(f"  - {item}")


def main(argv: Iterable[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-root", type=Path, default=DEFAULT_RUNTIME_ROOT, help="Wine runtime root (default: ~/.metalsharp/runtime/wine)")
    parser.add_argument("--json", action="store_true", help="Emit JSON instead of text")
    parser.add_argument("--strict", action="store_true", help="Exit non-zero if baseline or native D3DMetal ABI probes are missing")
    args = parser.parse_args(list(argv))

    runtime_root = args.runtime_root.expanduser().resolve()
    modules = {name: collect_module(runtime_root / rel) for name, rel in MODULE_RELATIVE_PATHS.items()}

    results: list[dict[str, object]] = []
    for probe in PROBES:
        evidence = modules[probe.module]
        present = evidence.exists and contains(evidence, probe)
        results.append(
            {
                "module": probe.module,
                "path": str(evidence.path),
                "name": probe.name,
                "needle": probe.needle,
                "source": probe.source,
                "required_for": probe.required_for,
                "present": present,
            }
        )

    summary = summarize(results)
    payload = {
        "runtime_root": str(runtime_root),
        "modules": {
            name: {"path": str(e.path), "exists": e.exists, "file_type": e.file_type}
            for name, e in modules.items()
        },
        "summary": summary,
        "probes": results,
    }

    if args.json:
        print(json.dumps(payload, indent=2, sort_keys=True))
    else:
        emit_text(runtime_root, modules, results, summary)

    if args.strict and (not summary["baseline_ready"] or not summary["native_d3dmetal_host_abi_ready"]):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
