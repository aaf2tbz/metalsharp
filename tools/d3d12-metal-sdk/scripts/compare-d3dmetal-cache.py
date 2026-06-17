#!/usr/bin/env python3
"""
Inventory D3DMetal/GPTK shader cache references and compare them to MetalSharp
M12 DXMT shader-cache products for the same title.

This is intentionally a conservative phase-1 comparator. It does not claim that
D3DMetal binary blobs are ABI-compatible with DXMT metallibs. It records the
cache topology, stable file hashes, and the record counts exposed by D3DMetal's
binary cache headers so later phases can add blob extraction and entry-point
matching without losing provenance.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
from typing import Any

GAME_APPIDS = {
    "elden-ring": "1245620",
    "armored-core-vi": "1888160",
    "subnautica-2": "1962700",
}

D3DM_CACHE_FILES = [
    "MTLGPUFamilyApple9_0/stage_cache.bin",
    "MTLGPUFamilyApple9_0/bytecode_cache.bin",
    "MTLGPUFamilyApple9_0/pipeline_cache.bin",
    "MTLGPUFamilyApple9_0/rootsignature_cache.bin",
    "16777235_403/functions.list",
    "16777235_403/functions.data",
    "32024/libraries.list",
    "32024/libraries.data",
]


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def read_u16_be_count(path: Path) -> int | None:
    """D3DMetal cache bins encode the observed record count in bytes 2..3.

    Examples observed in copied working caches:
      - pipeline_cache.bin: 00 00 05 00 => 5 records
      - rootsignature_cache.bin: 00 00 01 00 => 1 record
      - stage_cache.bin: 00 00 bc 01 => 0x01bc records

    This parser is deliberately named after the observed byte placement rather
    than pretending we have a full private-format schema.
    """
    if not path.exists() or path.stat().st_size < 4:
        return None
    data = path.read_bytes()[:4]
    return int.from_bytes(data[2:4], "little")


def file_record(path: Path, root: Path | None = None) -> dict[str, Any]:
    rec: dict[str, Any] = {
        "path": str(path if root is None else path.relative_to(root)),
        "exists": path.exists(),
    }
    if path.exists() and path.is_file():
        rec.update({
            "size": path.stat().st_size,
            "sha256": sha256(path),
        })
        if path.name.endswith("_cache.bin"):
            rec["observed_record_count"] = read_u16_be_count(path)
    return rec


def inventory_d3dmetal_game(game_root: Path) -> dict[str, Any]:
    cache = game_root / "shaders.cache"
    files = [p for p in cache.rglob("*") if p.is_file()] if cache.exists() else []
    records = [file_record(cache / rel, cache) for rel in D3DM_CACHE_FILES]
    extra = [file_record(p, cache) for p in sorted(files) if str(p.relative_to(cache)) not in D3DM_CACHE_FILES]
    counts = {r["path"]: r.get("observed_record_count") for r in records if r.get("observed_record_count") is not None}
    return {
        "cache": str(cache),
        "exists": cache.exists(),
        "file_count": len(files),
        "total_bytes": sum(p.stat().st_size for p in files),
        "records": records,
        "extra_files": extra,
        "observed_counts": counts,
    }


def inventory_m12_cache(cache: Path) -> dict[str, Any]:
    files = [p for p in cache.rglob("*") if p.is_file()] if cache.exists() else []
    suffix_counts: dict[str, int] = {}
    for p in files:
        # Preserve compound failure suffixes as useful categories.
        name = p.name
        if ".sm50_compile_failed." in name:
            suffix = "sm50_compile_failed." + name.rsplit(".", 1)[-1]
        elif ".dxil_msl_compile_failed." in name:
            suffix = "dxil_msl_compile_failed." + name.rsplit(".", 1)[-1]
        elif name.startswith("pso-render-") and name.endswith(".json"):
            suffix = "pso-render.json"
        elif name.startswith("pso-compute-") and name.endswith(".json"):
            suffix = "pso-compute.json"
        else:
            suffix = name.rsplit(".", 1)[-1] if "." in name else "<none>"
        suffix_counts[suffix] = suffix_counts.get(suffix, 0) + 1
    sample_files = sorted(str(p.relative_to(cache)) for p in files)[:200]
    return {
        "cache": str(cache),
        "exists": cache.exists(),
        "file_count": len(files),
        "total_bytes": sum(p.stat().st_size for p in files),
        "suffix_counts": dict(sorted(suffix_counts.items())),
        "sample_files": sample_files,
    }


def compare_game(game: str, d3dm_root: Path, m12_root: Path) -> dict[str, Any]:
    appid = GAME_APPIDS[game]
    d3dm = inventory_d3dmetal_game(d3dm_root / game)
    m12 = inventory_m12_cache(m12_root / appid)
    d_counts = d3dm.get("observed_counts", {})
    m_suffix = m12.get("suffix_counts", {})
    comparisons = {
        "d3dm_stage_records_vs_m12_msl": {
            "d3dm": d_counts.get("MTLGPUFamilyApple9_0/stage_cache.bin"),
            "m12": m_suffix.get("msl", 0),
        },
        "d3dm_bytecode_records_vs_m12_dxbc": {
            "d3dm": d_counts.get("MTLGPUFamilyApple9_0/bytecode_cache.bin"),
            "m12": m_suffix.get("dxbc", 0),
        },
        "d3dm_pipeline_records_vs_m12_pso_manifests": {
            "d3dm": d_counts.get("MTLGPUFamilyApple9_0/pipeline_cache.bin"),
            "m12_render": m_suffix.get("pso-render.json", 0),
            "m12_compute": m_suffix.get("pso-compute.json", 0),
        },
        "d3dm_rootsignature_records_vs_m12_root_visibility": {
            "d3dm": d_counts.get("MTLGPUFamilyApple9_0/rootsignature_cache.bin"),
            "m12": "not-yet-extracted",
        },
    }
    return {"game": game, "appid": appid, "d3dmetal": d3dm, "m12": m12, "comparisons": comparisons}


def write_markdown(result: dict[str, Any], path: Path) -> None:
    lines: list[str] = []
    lines.append("# D3DMetal vs M12 shader cache inventory")
    lines.append("")
    lines.append(f"- D3DMetal reference root: `{result['d3dmetal_reference_root']}`")
    lines.append(f"- M12 cache root: `{result['m12_cache_root']}`")
    lines.append("")
    lines.append("This is phase 1: topology/count/hash inventory. It is not yet proof of metallib ABI compatibility or semantic equivalence.")
    lines.append("")
    lines.append("## Summary")
    lines.append("")
    lines.append("| Game | D3DM stage | M12 MSL | D3DM bytecode | M12 DXBC | D3DM pipelines | M12 PSO manifests | D3DM bytes | M12 bytes |")
    lines.append("|---|---:|---:|---:|---:|---:|---:|---:|---:|")
    for game in result["games"]:
        c = game["comparisons"]
        d3dm_stage = c["d3dm_stage_records_vs_m12_msl"]["d3dm"]
        m12_msl = c["d3dm_stage_records_vs_m12_msl"]["m12"]
        d3dm_bc = c["d3dm_bytecode_records_vs_m12_dxbc"]["d3dm"]
        m12_dxbc = c["d3dm_bytecode_records_vs_m12_dxbc"]["m12"]
        d3dm_p = c["d3dm_pipeline_records_vs_m12_pso_manifests"]["d3dm"]
        m12_p = c["d3dm_pipeline_records_vs_m12_pso_manifests"]["m12_render"] + c["d3dm_pipeline_records_vs_m12_pso_manifests"]["m12_compute"]
        lines.append(f"| {game['game']} | {d3dm_stage} | {m12_msl} | {d3dm_bc} | {m12_dxbc} | {d3dm_p} | {m12_p} | {game['d3dmetal']['total_bytes']} | {game['m12']['total_bytes']} |")
    lines.append("")
    for game in result["games"]:
        lines.append(f"## {game['game']}")
        lines.append("")
        lines.append("### D3DMetal observed counts")
        for k, v in game["d3dmetal"].get("observed_counts", {}).items():
            lines.append(f"- `{k}`: `{v}`")
        lines.append("")
        lines.append("### M12 suffix counts")
        for k, v in game["m12"].get("suffix_counts", {}).items():
            lines.append(f"- `{k}`: `{v}`")
        lines.append("")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--d3dmetal-reference-root", type=Path, required=True)
    parser.add_argument("--m12-cache-root", type=Path, default=Path.home() / ".metalsharp/shader-cache/m12")
    parser.add_argument("--out-dir", type=Path, required=True)
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    result = {
        "schema": "metalsharp.d3dmetal-m12-cache-compare.v1",
        "d3dmetal_reference_root": str(args.d3dmetal_reference_root),
        "m12_cache_root": str(args.m12_cache_root),
        "games": [compare_game(game, args.d3dmetal_reference_root, args.m12_cache_root) for game in GAME_APPIDS],
    }
    (args.out_dir / "manifest.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    write_markdown(result, args.out_dir / "summary.md")
    print(args.out_dir / "manifest.json")
    print(args.out_dir / "summary.md")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
