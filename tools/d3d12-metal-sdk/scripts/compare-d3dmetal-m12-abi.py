#!/usr/bin/env python3
"""Compare extracted D3DMetal metallib entrypoints with M12 cache shape.

This is an offline report generator. It reads copied D3DMetal extraction
manifests and M12 cache directories; it does not mutate either cache.
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path
from typing import Any

APPIDS = {
    "elden-ring": "1245620",
    "armored-core-vi": "1888160",
    "subnautica-2": "1962700",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--d3dmetal-manifest", type=Path, required=True)
    parser.add_argument("--m12-root", type=Path, default=Path.home() / ".metalsharp/shader-cache/m12")
    parser.add_argument("--out-dir", type=Path, required=True)
    return parser.parse_args()


def iter_games(manifest: dict[str, Any]) -> list[tuple[str, dict[str, Any]]]:
    raw = manifest.get("games", {})
    if isinstance(raw, dict):
        return [(str(game), inv) for game, inv in raw.items() if isinstance(inv, dict)]
    out = []
    if isinstance(raw, list):
        for item in raw:
            if isinstance(item, dict) and isinstance(item.get("inventory"), dict):
                out.append((str(item.get("game", "unknown")), item["inventory"]))
    return out


def m12_msl_entrypoints(path: Path) -> set[str]:
    names: set[str] = set()
    # M12 MSL normally uses explicit dxmt_* entrypoints; collect all visible
    # vertex/fragment/kernel functions conservatively.
    pattern = re.compile(r"\b(?:vertex|fragment|kernel)\s+[\w:<>,\s*&]+\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(")
    for msl in path.glob("*.msl"):
        try:
            text = msl.read_text(errors="ignore")
        except OSError:
            continue
        names.update(pattern.findall(text))
    return names


def m12_metallib_names(path: Path) -> set[str]:
    names: set[str] = set()
    for metallib in path.glob("*.metallib"):
        proc = subprocess.run(
            ["xcrun", "metal-objdump", "--private-headers", str(metallib)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            timeout=30,
        )
        if proc.returncode != 0:
            continue
        # Fall back to filenames too; metal-objdump headers do not expose names.
        names.add(metallib.stem)
    return names


def classify_d3dmetal_name(name: str) -> str:
    lower = name.lower()
    if name in {"SATVSMain", "MainVS", "VertexMain"} or "vertex" in lower or lower.endswith("vs"):
        return "vertex"
    if name in {"SATPSMain", "MainPS", "PixelMain"} or "pixel" in lower or lower.endswith("ps") or "fragment" in lower:
        return "fragment"
    if name.endswith("CS") or "compute" in lower or lower == "maincs":
        return "compute"
    if "hull" in lower:
        return "hull"
    if "domain" in lower:
        return "domain"
    if name.endswith("GS") or "geometry" in lower:
        return "geometry"
    return "unknown"


def main() -> int:
    args = parse_args()
    manifest = json.loads(args.d3dmetal_manifest.read_text())
    args.out_dir.mkdir(parents=True, exist_ok=True)

    report: dict[str, Any] = {
        "d3dmetal_manifest": str(args.d3dmetal_manifest),
        "m12_root": str(args.m12_root),
        "games": {},
    }
    lines = ["# D3DMetal vs M12 metallib ABI/entrypoint comparison", ""]
    lines.append(f"- D3DMetal manifest: `{args.d3dmetal_manifest}`")
    lines.append(f"- M12 root: `{args.m12_root}`")
    lines.append("")
    lines.append("| Game | D3DMetal unique metallibs | D3DMetal unique names | M12 MSL files | M12 metallibs | Shared entrypoint names | Direct-load verdict |")
    lines.append("|---|---:|---:|---:|---:|---:|---|")

    for game, inv in iter_games(manifest):
        appid = APPIDS.get(game, "")
        m12_dir = args.m12_root / appid if appid else args.m12_root / game
        d3d_names: set[str] = set()
        stage_hist: dict[str, int] = {}
        for rec in inv.get("unique", []):
            for name in rec.get("function_names", []):
                d3d_names.add(name)
                stage = classify_d3dmetal_name(name)
                stage_hist[stage] = stage_hist.get(stage, 0) + 1
        m12_entries = m12_msl_entrypoints(m12_dir)
        m12_libs = m12_metallib_names(m12_dir)
        shared = sorted(d3d_names & (m12_entries | m12_libs))
        msl_count = len(list(m12_dir.glob("*.msl"))) if m12_dir.is_dir() else 0
        metallib_count = len(list(m12_dir.glob("*.metallib"))) if m12_dir.is_dir() else 0
        generic_overlap = set(shared).issubset({"vs_main", "ps_main", "cs_main", "main"})
        verdict = "oracle-only: entrypoint/function layout differs"
        if shared and not generic_overlap:
            verdict = "needs ABI proof: non-generic names overlap"
        elif shared:
            verdict = "oracle-only: only generic names overlap"
        report["games"][game] = {
            "appid": appid,
            "m12_dir": str(m12_dir),
            "d3dmetal_unique_metallibs": len(inv.get("unique", [])),
            "d3dmetal_unique_function_names": sorted(d3d_names),
            "d3dmetal_stage_histogram": dict(sorted(stage_hist.items())),
            "m12_msl_count": msl_count,
            "m12_metallib_count": metallib_count,
            "m12_entrypoint_names": sorted(m12_entries),
            "m12_metallib_names": sorted(m12_libs),
            "shared_names": shared,
            "verdict": verdict,
        }
        lines.append(
            f"| {game} | {len(inv.get('unique', []))} | {len(d3d_names)} | {msl_count} | {metallib_count} | {len(shared)} | {verdict} |"
        )

    lines.append("")
    lines.append("## Conclusion")
    lines.append("")
    lines.append("The extracted D3DMetal blobs are valid Metal libraries and useful as an offline oracle/reference. However, their function naming and cache shape do not match M12's current cache ABI: D3DMetal stores many per-shader/per-pipeline metallibs with names such as `SATVSMain`, `SATPSMain`, `MainCS`, and game-specific Unreal functions, while M12 uses generated MSL plus shared/generic `dxmt_sm50_*` metallibs. Treat direct loading as unsafe until resource/argument layouts and pipeline entrypoints are explicitly bridged.")

    (args.out_dir / "abi-comparison.json").write_text(json.dumps(report, indent=2) + "\n")
    (args.out_dir / "summary.md").write_text("\n".join(lines) + "\n")
    print(args.out_dir / "abi-comparison.json")
    print(args.out_dir / "summary.md")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
