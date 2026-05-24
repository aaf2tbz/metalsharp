#!/usr/bin/env python3
"""Generate first-class D3D12 Metal SDK contract files.

The source mapping files live outside the repo because they are generated from
the Windows/Agility SDK and macOS Metal headers. This script imports them into a
repo-owned contract shape with metadata and validation-friendly summaries.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


DEFAULT_D3D12_MAP = Path("/Volumes/AverySSD/metalsharp/metal-api-table/final/d3d12_to_metal_map.json")
DEFAULT_AGILITY_MAP = Path(
    "/Volumes/AverySSD/metalsharp/metal-api-table/final/agility_sdk_d3d12_to_metal_map.json"
)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, sort_keys=True)
        f.write("\n")


def difficulty_counts(interfaces: dict[str, Any]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for iface in interfaces.values():
        for method in iface.get("methods", {}).values():
            difficulty = method.get("difficulty", "unknown")
            counts[difficulty] = counts.get(difficulty, 0) + 1
    return dict(sorted(counts.items()))


def method_count(interfaces: dict[str, Any]) -> int:
    return sum(len(iface.get("methods", {})) for iface in interfaces.values())


def make_d3d12_contract(source: Path, data: dict[str, Any]) -> dict[str, Any]:
    interfaces = data.get("interfaces", {})
    return {
        "schema": "metalsharp.d3d12-metal.contract.v1",
        "source": {
            "path": str(source),
            "sha256": sha256(source),
            "kind": "generated_d3d12_to_metal_map",
        },
        "evidence": [
            {
                "kind": "source_map",
                "path": str(source),
                "note": "Generated from DXMT source analysis and macOS Metal SDK headers.",
            }
        ],
        "summary": {
            "interface_count": len(interfaces),
            "method_count": method_count(interfaces),
            "format_count": len(data.get("format_map", {})),
            "topology_count": len(data.get("topology_map", {})),
            "heap_type_count": len(data.get("heap_type_map", {})),
            "difficulty_counts": difficulty_counts(interfaces),
        },
        "contract_state": "reference_import",
        "data": data,
    }


def make_agility_contract(source: Path, data: dict[str, Any]) -> dict[str, Any]:
    return {
        "schema": "metalsharp.d3d12-metal.agility-contract.v1",
        "agility_sdk_version": "1.619.3",
        "source": {
            "path": str(source),
            "sha256": sha256(source),
            "kind": "generated_agility_sdk_d3d12_to_metal_map",
        },
        "evidence": [
            {
                "kind": "source_map",
                "path": str(source),
                "note": "Generated from Microsoft.Direct3D.D3D12 NuGet package v1.619.3 and Metal SDK headers.",
            }
        ],
        "summary": {
            "interface_count": len(data),
            "method_count": method_count(data),
            "difficulty_counts": difficulty_counts(data),
        },
        "contract_state": "reference_import",
        "data": data,
    }


def feature_support_contract() -> dict[str, Any]:
    return {
        "schema": "metalsharp.d3d12-metal.feature-support.v1",
        "purpose": "Keep D3D12 feature reports aligned with executable implementation reality.",
        "evidence": [
            {
                "kind": "source",
                "path": "vendor/dxmt/src/d3d12/d3d12_device.cpp",
                "note": "MTLD3D12Device::CheckFeatureSupport is the current runtime implementation.",
            },
            {
                "kind": "probe_todo",
                "path": "tools/d3d12-metal-sdk/probes/probe_device_caps",
                "note": "Phase 4 will prove these reports without launching a game.",
            },
        ],
        "features": {
            "D3D12_FEATURE_FEATURE_LEVELS": {
                "state": "stub_risky",
                "expected_minimum": "12_0",
                "current_target": "12_1",
                "risk": "UE5 may reject the adapter if this disagrees with shader model or options queries.",
            },
            "D3D12_FEATURE_SHADER_MODEL": {
                "state": "stub_risky",
                "current_target": "6_6",
                "risk": "Must be backed by actual DXIL/SM6 shader path probes before treated as native.",
            },
            "D3D12_FEATURE_D3D12_OPTIONS": {
                "state": "stub_risky",
                "required_fields": ["ResourceBindingTier", "ROVsSupported", "ConservativeRasterizationTier"],
                "risk": "Over-reporting can push games into unimplemented render paths.",
            },
            "D3D12_FEATURE_D3D12_OPTIONS1": {
                "state": "stub_risky",
                "required_fields": ["WaveOps", "WaveLaneCountMin", "WaveLaneCountMax", "Int64ShaderOps"],
                "risk": "Current Subnautica evidence reports wave ops but atomic64 remains unsupported.",
            },
            "D3D12_FEATURE_D3D12_OPTIONS9": {
                "state": "stub_safe",
                "required_fields": ["AtomicInt64OnTypedResourceSupported", "AtomicInt64OnGroupSharedSupported"],
                "risk": "Should stay conservative unless atomic64 is actually implemented.",
            },
            "D3D12_FEATURE_D3D12_OPTIONS11": {
                "state": "stub_safe",
                "required_fields": ["AtomicInt64OnDescriptorHeapResourceSupported"],
                "risk": "Should stay conservative unless descriptor-heap atomic64 is implemented.",
            },
        },
    }


def dxgi_contract() -> dict[str, Any]:
    return {
        "schema": "metalsharp.d3d12-metal.dxgi-contract.v1",
        "purpose": "Define DXGI factory, adapter, output, swapchain, and present behavior required by D3D12 games.",
        "evidence": [
            {
                "kind": "source",
                "path": "vendor/dxmt/src/dxgi/dxgi_factory.cpp",
                "note": "Current factory implementation includes IDXGIFactory7 QueryInterface support.",
            },
            {
                "kind": "source",
                "path": "vendor/dxmt/src/dxgi/dxgi_adapter.cpp",
                "note": "Adapter descriptions and unknown QueryInterface behavior live here.",
            },
            {
                "kind": "probe_todo",
                "path": "tools/d3d12-metal-sdk/probes/probe_dxgi_factory",
                "note": "Phase 5 will prove factory and adapter behavior.",
            },
        ],
        "interfaces": {
            "IDXGIFactory7": {
                "state": "stub_risky",
                "required_behavior": [
                    "QueryInterface succeeds for IDXGIFactory through IDXGIFactory7",
                    "EnumAdapterByGpuPreference returns the Metal-backed adapter",
                    "RegisterAdaptersChangedEvent behavior is explicit and probe-covered",
                ],
            },
            "IDXGIAdapter": {
                "state": "stub_risky",
                "required_behavior": [
                    "GetDesc and GetDesc1 return stable fields",
                    "QueryInterface failures log the exact IID and return E_NOINTERFACE",
                ],
            },
            "IDXGISwapChain": {
                "state": "stub_risky",
                "required_behavior": [
                    "GetBuffer returns the renderable backbuffer for the current frame",
                    "Present uses the intended backbuffer/drawable lifecycle",
                    "Present count progresses after successful presents",
                ],
            },
        },
    }


def unsupported_ledger() -> dict[str, Any]:
    return {
        "schema": "metalsharp.d3d12-metal.unsupported-ledger.v1",
        "purpose": "Track explicit unsupported APIs so games are not misled by optimistic success returns.",
        "entries": [
            {
                "api": "D3D12 ray tracing tiers",
                "state": "unsupported",
                "reason": "No DXR-to-Metal acceleration structure or shader table implementation is proven.",
                "evidence": ["tools/d3d12-metal-sdk/contracts/feature-support-contract.json"],
            },
            {
                "api": "D3D12 mesh shader tiers",
                "state": "unsupported",
                "reason": "No mesh/amplification shader translation path is proven.",
                "evidence": ["tools/d3d12-metal-sdk/contracts/feature-support-contract.json"],
            },
            {
                "api": "D3D12 sampler feedback tier",
                "state": "unsupported",
                "reason": "No sampler feedback emulation or Metal-backed equivalent is proven.",
                "evidence": ["tools/d3d12-metal-sdk/contracts/feature-support-contract.json"],
            },
        ],
    }


def risky_stub_ledger() -> dict[str, Any]:
    return {
        "schema": "metalsharp.d3d12-metal.risky-stub-ledger.v1",
        "purpose": "Track startup-unblocking behavior that must be replaced or proven before broad compatibility claims.",
        "entries": [
            {
                "api": "D3D12_FEATURE_SHADER_MODEL reports SM 6.6",
                "state": "stub_risky",
                "risk": "Games may compile or load SM6 shaders that exceed the proven DXIL-to-Metal path.",
                "next_probe": "tools/d3d12-metal-sdk/probes/probe_shaders",
            },
            {
                "api": "D3D12_FEATURE_D3D12_OPTIONS1 WaveOps reports supported",
                "state": "stub_risky",
                "risk": "Wave intrinsic support must be verified through shader probes, not only feature reports.",
                "next_probe": "tools/d3d12-metal-sdk/probes/probe_device_caps",
            },
            {
                "api": "IDXGIFactory7 RegisterAdaptersChangedEvent",
                "state": "stub_risky",
                "risk": "Returning the wrong HRESULT can change UE5 adapter handling.",
                "next_probe": "tools/d3d12-metal-sdk/probes/probe_dxgi_factory",
            },
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--d3d12-map", type=Path, default=DEFAULT_D3D12_MAP)
    parser.add_argument("--agility-map", type=Path, default=DEFAULT_AGILITY_MAP)
    parser.add_argument("--out", type=Path, default=Path("tools/d3d12-metal-sdk/contracts"))
    args = parser.parse_args()

    d3d12_data = load_json(args.d3d12_map)
    agility_data = load_json(args.agility_map)

    write_json(args.out / "d3d12-metal-contract.json", make_d3d12_contract(args.d3d12_map, d3d12_data))
    write_json(args.out / "agility-1.619.3-contract.json", make_agility_contract(args.agility_map, agility_data))
    write_json(args.out / "feature-support-contract.json", feature_support_contract())
    write_json(args.out / "dxgi-contract.json", dxgi_contract())
    write_json(args.out / "unsupported-api-ledger.json", unsupported_ledger())
    write_json(args.out / "risky-stub-ledger.json", risky_stub_ledger())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
