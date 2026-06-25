#!/usr/bin/env python3
"""Classify the active renderer path from existing launch artifacts.

Read-only for inputs. Does not launch games. Writes JSON/Markdown reports that
separate launch configuration (for example `pipeline=M12`) from observed renderer
activity (D3D12/D3D11/Vulkan/MoltenVK evidence in logs).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

SCHEMA = "metalsharp.d3d12-metal.launch-renderer-classification.v1"

APPID_TITLES = {
    1888160: "armored-core-vi",
    1245620: "elden-ring",
    1962700: "subnautica-2",
    3527290: "peak",
    3164500: "schedule-i",
}

# Labels intentionally match the Path B roadmap vocabulary.
LABEL_D3D12 = "d3d12-m12"
LABEL_D3D11 = "d3d11-dxmt"
LABEL_VULKAN = "vulkan-moltenvk"
LABEL_MIXED = "mixed"
LABEL_UNKNOWN = "unknown"

STRONG_M12_KEYS = {
    "ue_d3d12_rhi",
    "d3d12_create_device",
    "d3d12_device_created",
    "d3d12_command_queue",
    "m12_dxgi_winemetal_chain",
    "m12_winemetal_core_stack",
    "m12_internal_winemetal",
}
WEAK_AMBIENT_KEYS = {"moltenvk", "vk_instance"}
LEGACY_SPLIT_BRAIN_KEYS = {
    "legacy_required_libm12core",
    "legacy_m12core_required_env",
    "legacy_sidecar_only_m12core",
}


@dataclass(frozen=True)
class PatternSpec:
    key: str
    path: str
    regex: re.Pattern[str]
    weight: int
    description: str


def rx(pattern: str) -> re.Pattern[str]:
    return re.compile(pattern, re.IGNORECASE | re.MULTILINE)


PATTERNS: list[PatternSpec] = [
    # D3D12 / M12. UE's D3D12RHI line is strong active-renderer evidence.
    PatternSpec("ue_d3d12_rhi", "d3d12", rx(r"LogD3D12RHI:.*Creating D3D12 RHI|Creating D3D12 RHI"), 10, "Unreal D3D12 RHI creation"),
    PatternSpec("d3d12_create_device", "d3d12", rx(r"\bD3D12CreateDevice\b"), 8, "D3D12CreateDevice observed"),
    PatternSpec("d3d12_device_created", "d3d12", rx(r"D3D12 device created|D3D12CreateDevice: created device|D3D12CreateDevice SUCCESS"), 9, "D3D12 device creation success"),
    PatternSpec("d3d12_command_queue", "d3d12", rx(r"D3D12CommandQueue|ExecuteCommandLists|ID3D12CommandQueue"), 5, "D3D12 command queue activity"),
    PatternSpec("d3d12_resource", "d3d12", rx(r"D3D12Resource|ID3D12Resource|D3D12DescriptorHeap|D3D12RootSignature"), 4, "D3D12 resource/descriptor/root activity"),
    PatternSpec(
        "m12_dxgi_winemetal_chain",
        "d3d12",
        rx(r"d3d12\.dll.*(?:dxgi_dxmt\.dll|dxgi\.dll).*winemetal\.dll.*winemetal\.so|dxgi_dxmt\.dll.*winemetal\.dll.*winemetal\.so|WINEDLLOVERRIDES=.*(?:d3d12|dxgi).*winemetal"),
        12,
        "D3D12/DXGI to WineMetal M12 runtime chain evidence",
    ),
    PatternSpec(
        "m12_winemetal_core_stack",
        "d3d12",
        rx(r"winemetal\.so.*(?:m12core_lower_dxil_to_msl|WMTM12CoreLowerDXILToMSL)|(?:m12core_lower_dxil_to_msl|WMTM12CoreLowerDXILToMSL)"),
        12,
        "Winemetal-backed M12 DXIL->MSL stack evidence",
    ),
    PatternSpec("m12_internal_winemetal", "d3d12", rx(r"internal:winemetal\.so|dxmt_unix_winemetal_internal_m12core|winemetal\.so.*internal.*m12core"), 10, "Internal m12core-in-winemetal evidence"),
    PatternSpec("legacy_required_libm12core", "legacy_split_brain", rx(r"required.*libm12core\.dylib|dlopen\([^\n)]*libm12core\.dylib|@loader_path/libm12core\.dylib"), 10, "Legacy required libm12core sidecar evidence"),
    PatternSpec("legacy_m12core_required_env", "legacy_split_brain", rx(r"DXMT_M12CORE_REQUIRED\s*=\s*1|DXMT_M12CORE_ENABLE\s*=\s*1"), 10, "Legacy required m12core environment evidence"),
    PatternSpec("legacy_sidecar_only_m12core", "legacy_split_brain", rx(r"sidecar[- ]only.*m12core|m12core.*sidecar[- ]only|--include-m12core-sidecar"), 8, "Legacy sidecar-only m12core path evidence"),
    PatternSpec("m12core_launch", "launch_m12", rx(r"\bm12core\b|DXMT_M12CORE_ENABLE|libm12core"), 1, "M12Core launch/support metadata"),
    # D3D11 / DXMT.
    PatternSpec("d3d11_create_device", "d3d11", rx(r"\bD3D11CreateDevice(?:AndSwapChain)?\b"), 8, "D3D11CreateDevice observed"),
    PatternSpec("d3d11_rhi", "d3d11", rx(r"LogD3D11RHI|D3D11 RHI|Direct3D 11"), 8, "D3D11 renderer/RHI evidence"),
    PatternSpec("dxmt_d3d11", "d3d11", rx(r"dxmt.*d3d11|d3d11.*dxmt"), 4, "DXMT D3D11 evidence"),
    # Vulkan / MoltenVK. Bare MoltenVK/VkInstance lines are ambient/bootstrap
    # evidence in Wine/DXMT processes and do not prove the active renderer.
    PatternSpec("moltenvk", "vulkan", rx(r"\bMoltenVK\b|VK_MVK_moltenvk"), 1, "MoltenVK ambient/bootstrap evidence"),
    PatternSpec("vk_instance", "vulkan", rx(r"Created VkInstance|vkCreateInstance|Vulkan version"), 2, "Vulkan instance bootstrap evidence"),
    PatternSpec("vk_device", "vulkan", rx(r"Created VkDevice|vkCreateDevice|Selected Vulkan device|Using Vulkan"), 8, "Vulkan device/renderer evidence"),
    PatternSpec("unity_vulkan", "vulkan", rx(r"GfxDevice:.*Vulkan|Initialize engine.*Unity|UnityGfxDevice.*Vulkan"), 5, "Unity/Vulkan evidence"),
    # Launch configuration / metadata. These are not active-renderer proof.
    PatternSpec("launch_pipeline_m12", "launch_m12", rx(r"^pipeline=M12\b|\"pipeline\"\s*:\s*\"?m12\"?"), 2, "Launch configured for M12"),
    PatternSpec("launch_backend_dxmt", "launch_m12", rx(r"^graphics_backend=dxmt\b|\"graphics_backend\"\s*:\s*\"dxmt\""), 2, "Launch configured for DXMT backend"),
    PatternSpec("launch_args_dx12", "launch_m12", rx(r"(?:^|[\s\"'])-(?:dx12|d3d12)(?:[\s\"']|$)"), 3, "Launch args request D3D12"),
    PatternSpec("launch_args_dx11", "launch_d3d11", rx(r"(?:^|[\s\"'])-(?:dx11|d3d11)(?:[\s\"']|$)"), 3, "Launch args request D3D11"),
]

TEXT_SUFFIXES = {".log", ".txt", ".json", ".md", ".err", ".stderr", ".stdout"}


def read_text_window(path: Path, limit: int = 2_000_000) -> str:
    try:
        size = path.stat().st_size
        if size <= limit:
            return path.read_text(errors="replace")
        half = limit // 2
        with path.open("rb") as f:
            head = f.read(half)
            f.seek(max(0, size - half))
            tail = f.read(half)
        return (head + b"\n...[truncated middle of large file]...\n" + tail).decode(errors="replace")
    except OSError:
        return ""


def default_log_roots(appid: int) -> list[Path]:
    home = Path.home()
    return [
        home / ".metalsharp" / "compatdata" / str(appid) / "logs",
        home / ".metalsharp" / "logs" / "m12-pipeline" / str(appid),
        home / ".metalsharp" / "pipeline-cache" / "m12" / str(appid),
        home / ".metalsharp" / "bottles" / f"steam_{appid}" / "logs",
    ]


def iter_input_files(inputs: Iterable[Path], max_files: int) -> list[Path]:
    candidates: list[Path] = []
    for raw in inputs:
        path = raw.expanduser()
        if path.is_file():
            candidates.append(path)
        elif path.is_dir():
            for child in path.rglob("*"):
                if not child.is_file():
                    continue
                if child.suffix.lower() in TEXT_SUFFIXES or child.name in {"launch.json", "request.json", "summary.md"}:
                    candidates.append(child)
    unique = sorted(set(candidates), key=lambda p: p.stat().st_mtime if p.exists() else 0, reverse=True)
    return unique[:max_files]


def evidence_for_file(path: Path, text: str) -> tuple[list[dict[str, Any]], dict[str, int]]:
    rows: list[dict[str, Any]] = []
    scores = {"d3d12": 0, "d3d11": 0, "vulkan": 0, "launch_m12": 0, "launch_d3d11": 0, "legacy_split_brain": 0}
    lines = text.splitlines()
    for spec in PATTERNS:
        matches = list(spec.regex.finditer(text))
        if not matches:
            continue
        count = len(matches)
        scores[spec.path] = scores.get(spec.path, 0) + spec.weight * min(count, 5)
        snippets: list[str] = []
        for line in lines:
            if spec.regex.search(line):
                snippets.append(line.strip()[-500:])
            if len(snippets) >= 5:
                break
        rows.append(
            {
                "key": spec.key,
                "path": spec.path,
                "description": spec.description,
                "file": str(path),
                "count": count,
                "weight": spec.weight,
                "score": spec.weight * min(count, 5),
                "snippets": snippets,
            }
        )
    return rows, scores


def choose_classification(scores: dict[str, int], evidence: list[dict[str, Any]]) -> tuple[str, str, list[str]]:
    d3d12 = scores.get("d3d12", 0)
    d3d11 = scores.get("d3d11", 0)
    vulkan = scores.get("vulkan", 0)
    launch_m12 = scores.get("launch_m12", 0)
    launch_d3d11 = scores.get("launch_d3d11", 0)
    legacy_split_brain = scores.get("legacy_split_brain", 0)
    evidence_keys = {str(e.get("key")) for e in evidence}
    active_vulkan = bool(evidence_keys & {"vk_device", "unity_vulkan"})
    vk_instance_only = bool(evidence_keys & {"moltenvk", "vk_instance"}) and not active_vulkan

    warnings: list[str] = []
    if launch_m12 and not d3d12:
        warnings.append("M12/DXMT launch metadata is present, but no direct D3D12 device/RHI evidence was found.")
    if launch_d3d11 and not d3d11:
        warnings.append("D3D11 launch args are present, but no direct D3D11 device evidence was found.")
    if vk_instance_only:
        warnings.append("Only MoltenVK/VkInstance bootstrap evidence was found; no explicit Vulkan device/renderer line proves the active game renderer.")
    if vulkan and (d3d12 or d3d11):
        warnings.append("Vulkan/MoltenVK evidence coexists with D3D evidence; classification prefers direct D3D renderer evidence.")
    if legacy_split_brain:
        warnings.append("Legacy split-brain M12 evidence is present (required libm12core sidecar/env); do not treat it as the unified WineMetal M12 runtime path.")

    strong_paths = sum(1 for value in (d3d12, d3d11, vulkan) if value >= 8)
    if d3d12 >= 10 and d3d12 >= d3d11 + 4:
        confidence = "high" if any(e["key"] in {"ue_d3d12_rhi", "d3d12_device_created", "d3d12_create_device"} for e in evidence) else "medium"
        return LABEL_D3D12, confidence, warnings
    if d3d11 >= 8 and d3d11 >= d3d12 + 4:
        confidence = "high" if any(e["key"] in {"d3d11_create_device", "d3d11_rhi"} for e in evidence) else "medium"
        return LABEL_D3D11, confidence, warnings
    if strong_paths > 1:
        return LABEL_MIXED, "medium", warnings
    if vulkan >= 10 and not d3d12 and not d3d11:
        if active_vulkan:
            return LABEL_VULKAN, "high", warnings
        if launch_m12:
            return LABEL_MIXED, "low", warnings
        return LABEL_VULKAN, "medium", warnings
    if launch_m12 or launch_d3d11:
        return LABEL_UNKNOWN, "low", warnings
    return LABEL_UNKNOWN, "low", warnings


def classify(inputs: list[Path], appid: int | None, profile: str, max_files: int) -> dict[str, Any]:
    files = iter_input_files(inputs, max_files=max_files)
    all_evidence: list[dict[str, Any]] = []
    total_scores = {"d3d12": 0, "d3d11": 0, "vulkan": 0, "launch_m12": 0, "launch_d3d11": 0, "legacy_split_brain": 0}
    scanned: list[dict[str, Any]] = []
    for path in files:
        text = read_text_window(path)
        if not text:
            continue
        evidence, scores = evidence_for_file(path, text)
        for key, value in scores.items():
            total_scores[key] = total_scores.get(key, 0) + value
        if evidence:
            all_evidence.extend(evidence)
        scanned.append({"path": str(path), "size": path.stat().st_size, "evidence_count": len(evidence)})

    label, confidence, warnings = choose_classification(total_scores, all_evidence)
    secondary = []
    for path_key, path_label in [("d3d12", LABEL_D3D12), ("d3d11", LABEL_D3D11), ("vulkan", LABEL_VULKAN)]:
        if total_scores.get(path_key, 0) and path_label != label:
            secondary.append({"label": path_label, "score": total_scores[path_key]})

    def evidence_bucket(keys: set[str]) -> list[dict[str, Any]]:
        return [row for row in sorted(all_evidence, key=lambda row: row["score"], reverse=True) if str(row.get("key")) in keys]

    phase14_evidence = {
        "strong_m12": evidence_bucket(STRONG_M12_KEYS),
        "weak_ambient": evidence_bucket(WEAK_AMBIENT_KEYS),
        "legacy_split_brain": evidence_bucket(LEGACY_SPLIT_BRAIN_KEYS),
    }
    phase14_evidence["unified_m12_supported"] = bool(phase14_evidence["strong_m12"]) and not bool(phase14_evidence["legacy_split_brain"])

    return {
        "schema": SCHEMA,
        "generated_at": int(time.time()),
        "profile": profile,
        "appid": appid,
        "title": APPID_TITLES.get(appid or -1),
        "classification": label,
        "confidence": confidence,
        "scores": total_scores,
        "secondary_paths": secondary,
        "phase14_evidence": phase14_evidence,
        "warnings": warnings,
        "inputs": [str(p) for p in inputs],
        "files_scanned": scanned,
        "evidence": sorted(all_evidence, key=lambda row: row["score"], reverse=True),
    }


def markdown(report: dict[str, Any]) -> str:
    lines = [
        f"# Launch renderer classification: {report['profile']}",
        "",
        f"- classification: `{report['classification']}`",
        f"- confidence: `{report['confidence']}`",
        f"- appid: `{report.get('appid')}`",
        f"- title: `{report.get('title')}`",
        f"- scores: `{json.dumps(report['scores'], sort_keys=True)}`",
        "",
    ]
    phase14 = report.get("phase14_evidence", {})
    lines.extend([
        "## Phase 14 evidence policy",
        "",
        f"- unified_m12_supported: `{phase14.get('unified_m12_supported')}`",
        f"- strong_m12 evidence rows: `{len(phase14.get('strong_m12', []))}`",
        f"- weak_ambient evidence rows: `{len(phase14.get('weak_ambient', []))}`",
        f"- legacy_split_brain evidence rows: `{len(phase14.get('legacy_split_brain', []))}`",
        "",
    ])
    if report["warnings"]:
        lines.extend(["## Warnings", ""])
        for warning in report["warnings"]:
            lines.append(f"- {warning}")
        lines.append("")
    if report["secondary_paths"]:
        lines.extend(["## Secondary evidence", ""])
        for item in report["secondary_paths"]:
            lines.append(f"- `{item['label']}` score `{item['score']}`")
        lines.append("")
    lines.extend(["## Evidence", ""])
    for row in report["evidence"][:40]:
        lines.append(f"### `{row['key']}` / `{row['path']}`")
        lines.append(f"- file: `{row['file']}`")
        lines.append(f"- count: `{row['count']}` score: `{row['score']}`")
        for snippet in row.get("snippets", [])[:5]:
            lines.append(f"  - `{snippet}`")
        lines.append("")
    lines.extend(["## Files scanned", ""])
    for row in report["files_scanned"][:80]:
        lines.append(f"- `{row['path']}` evidence `{row['evidence_count']}`")
    return "\n".join(lines) + "\n"


def run_self_test() -> int:
    cases = [
        ("subnautica", "pipeline=M12\ngraphics_backend=dxmt\n[mvk-info] Created VkInstance\nLogD3D12RHI: Display: Creating D3D12 RHI with Max Feature Level SM6\n", LABEL_D3D12),
        ("unity-vulkan", "pipeline=M12\ngraphics_backend=dxmt\n[mvk-info] MoltenVK version 1.4.1\n[mvk-info] Created VkInstance for Vulkan version 1.0.334\nGfxDevice: creating device client; threaded=1; jobified=0; type=Vulkan\n", LABEL_VULKAN),
        ("d3d11", "D3D11CreateDevice: created device\n", LABEL_D3D11),
        ("unknown-m12", "pipeline=M12\ngraphics_backend=dxmt\n", LABEL_UNKNOWN),
        ("m12-chain", "d3d12.dll -> dxgi_dxmt.dll -> winemetal.dll -> winemetal.so\ninternal:winemetal.so m12core\n", LABEL_D3D12),
        ("moltenvk-only", "[mvk-info] MoltenVK version 1.4.1\n[mvk-info] Created VkInstance for Vulkan version 1.0.334\n", LABEL_UNKNOWN),
        ("legacy-sidecar", "DXMT_M12CORE_REQUIRED=1\nrequired runtime dlopen(libm12core.dylib)\n", LABEL_UNKNOWN),
    ]
    import tempfile

    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        ok = True
        for name, text, expected in cases:
            path = root / f"{name}.log"
            path.write_text(text)
            report = classify([path], None, name, max_files=8)
            actual = report["classification"]
            if actual != expected:
                print(f"SELFTEST FAIL {name}: expected {expected}, got {actual}", file=sys.stderr)
                ok = False
            phase14 = report.get("phase14_evidence", {})
            if name == "m12-chain" and not phase14.get("unified_m12_supported"):
                print("SELFTEST FAIL m12-chain: expected unified_m12_supported", file=sys.stderr)
                ok = False
            if name == "moltenvk-only" and not phase14.get("weak_ambient"):
                print("SELFTEST FAIL moltenvk-only: expected weak ambient evidence", file=sys.stderr)
                ok = False
            if name == "legacy-sidecar" and not phase14.get("legacy_split_brain"):
                print("SELFTEST FAIL legacy-sidecar: expected legacy split-brain evidence", file=sys.stderr)
                ok = False
        if ok:
            print("self-test: PASS")
            return 0
        return 1


def main() -> int:
    ap = argparse.ArgumentParser(description="Classify active renderer path from existing launch artifacts.")
    ap.add_argument("--appid", type=int, default=None, help="Steam appid; adds default MetalSharp log roots.")
    ap.add_argument("--profile", default="", help="Report/profile name. Defaults to title/appid/timestamp.")
    ap.add_argument("--input", action="append", default=[], help="Input file or directory. Repeatable.")
    ap.add_argument("--max-files", type=int, default=24)
    ap.add_argument("--results-dir", type=Path, default=Path(__file__).resolve().parents[1] / "results")
    ap.add_argument("--include-default-roots", action="store_true", help="When --input is also provided, additionally scan default appid log roots.")
    ap.add_argument("--stdout", action="store_true", help="Print JSON report to stdout instead of writing result files.")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        return run_self_test()

    inputs = [Path(p) for p in args.input]
    if args.appid is not None and (not inputs or args.include_default_roots):
        inputs.extend(default_log_roots(args.appid))
    if not inputs:
        ap.error("provide --appid and/or at least one --input path")

    profile = args.profile or APPID_TITLES.get(args.appid or -1) or str(args.appid or int(time.time()))
    report = classify(inputs, args.appid, profile, max_files=args.max_files)

    if args.stdout:
        print(json.dumps(report, indent=2))
        return 0

    args.results_dir.mkdir(parents=True, exist_ok=True)
    safe_profile = re.sub(r"[^A-Za-z0-9_.-]+", "-", profile).strip("-") or "renderer"
    json_path = args.results_dir / f"launch-renderer-classification-{safe_profile}.json"
    md_path = args.results_dir / f"launch-renderer-classification-{safe_profile}.md"
    json_path.write_text(json.dumps(report, indent=2) + "\n")
    md_path.write_text(markdown(report))
    print(md_path)
    print(json_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
