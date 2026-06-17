#!/usr/bin/env python3
"""Infer D3DMetal pipeline root-signature layouts via shader bytecode RTS0.

D3DMetal pipeline_cache.bin has not exposed direct root-signature cache keys in
raw scans. This offline helper links records conservatively through data we can
verify:

  rootsignature_cache.bin RTS0 -> canonical structural layout hash
  bytecode_cache.bin embedded RTS0 -> same canonical structural layout hash
  pipeline_cache.bin bytecode-key refs -> root layout(s) used by referenced shaders

It never writes live M12 caches and only emits compact manifests/summaries.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
from typing import Any

GAME_APPIDS = {
    "elden-ring": "1245620",
    "armored-core-vi": "1888160",
    "subnautica-2": "1962700",
}

PSV_RESOURCE_TO_ROOT_RANGE = {
    1: "sampler",
    2: "cbv",
    3: "srv",
    4: "srv",
    5: "srv",
    6: "uav",
    7: "uav",
    8: "uav",
    9: "uav",
}


def load_script(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import {path}")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


HERE = Path(__file__).resolve().parent
CACHE_MAP = load_script(HERE / "decompile-d3dmetal-cache-map.py", "d3dmetal_cache_map")
ROOT_DECODER = load_script(HERE / "decode-d3dmetal-root-signatures.py", "d3dmetal_root_decoder")


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--d3dmetal-reference-root", type=Path, required=True)
    p.add_argument("--out-dir", type=Path, required=True)
    return p.parse_args()


def structural_hash_from_rts0(payload: bytes) -> tuple[str, dict[str, Any]]:
    decoded = ROOT_DECODER.decode_rts0(payload)
    structural = ROOT_DECODER.structural_signature(decoded)
    h = ROOT_DECODER.sha256_bytes(json.dumps(structural, sort_keys=True, separators=(",", ":")).encode())
    return h, structural


def u32(data: bytes, off: int) -> int:
    if off < 0 or off + 4 > len(data):
        raise ValueError(f"u32 out of bounds at {off}")
    return int.from_bytes(data[off:off + 4], "little")


def parse_psv_resources(blob: bytes) -> list[dict[str, Any]]:
    for ch in CACHE_MAP.dxbc_chunks(blob):
        if ch["fourcc"] != "PSV0":
            continue
        p = blob[ch["offset"] + 8:ch["offset"] + 8 + ch["size"]]
        if len(p) < 8:
            return []
        runtime_size = u32(p, 0)
        off = 4 + runtime_size
        if off + 4 > len(p):
            return []
        resource_count = u32(p, off)
        off += 4
        if resource_count == 0:
            return []
        if off + 4 > len(p):
            return []
        bind_size = u32(p, off)
        off += 4
        if bind_size < 16:
            return []
        resources = []
        for i in range(resource_count):
            base = off + i * bind_size
            if base + 16 > len(p):
                break
            res_type_value = u32(p, base)
            root_type = PSV_RESOURCE_TO_ROOT_RANGE.get(res_type_value)
            if not root_type:
                continue
            resources.append({
                "type": root_type,
                "psv_type": res_type_value,
                "space": u32(p, base + 4),
                "lower": u32(p, base + 8),
                "upper": u32(p, base + 12),
            })
        return resources
    return []


def build_stage_index(stages: list[dict[str, Any]], by_key: dict[str, dict[str, Any]]) -> dict[str, list[dict[str, Any]]]:
    out: dict[str, list[dict[str, Any]]] = {}
    for st in stages:
        for key in st.get("candidate_source_keys", []):
            if key in by_key:
                out.setdefault(key, []).append(st)
    return out


def decode_bytecode_roots(cache: Path, bytecode: list[dict[str, Any]], root_by_structural: dict[str, list[dict[str, Any]]], stage_by_key: dict[str, list[dict[str, Any]]]) -> dict[str, dict[str, Any]]:
    data = (cache / "bytecode_cache.bin").read_bytes()
    out: dict[str, dict[str, Any]] = {}
    for bc in bytecode:
        blob = data[bc["record_payload_offset"]:bc["record_payload_offset"] + bc["payload_size"]]
        structural_hash = ""
        structural = None
        for ch in CACHE_MAP.dxbc_chunks(blob):
            if ch["fourcc"] != "RTS0":
                continue
            payload = blob[ch["offset"] + 8:ch["offset"] + 8 + ch["size"]]
            structural_hash, structural = structural_hash_from_rts0(payload)
            break
        root_records = root_by_structural.get(structural_hash, []) if structural_hash else []
        resources = parse_psv_resources(blob)
        stages = stage_by_key.get(bc["record_key"], [])
        out[bc["record_key"]] = {
            "record_key": bc["record_key"],
            "bytecode_ordinal": bc["ordinal"],
            "payload_size": bc["payload_size"],
            "bytecode_sha256": bc["bytecode_sha256"],
            "shader_kind": bc.get("shader_kind"),
            "structural_signature_sha256": structural_hash,
            "root_signature_keys": [r["record_key"] for r in root_records],
            "root_signature_ordinals": [r["ordinal"] for r in root_records],
            "stage_count": len(stages),
            "stage_metallib_sha256": [s["metallib_sha256"] for s in stages[:8]],
            "function_names": sorted({fn for s in stages for fn in s.get("function_names", [])}),
            "psv_resource_bindings": resources,
        }
    return out


def root_capabilities(root: dict[str, Any]) -> list[dict[str, Any]]:
    caps = []
    sig = root["structural_signature"]
    for p in sig["parameters"]:
        visibility = p.get("visibility", "all")
        if p["type"] in ("cbv", "srv", "uav"):
            caps.append({"type": p["type"], "space": p["space"], "lower": p["reg"], "upper": p["reg"], "visibility": visibility, "cost": 1})
        elif p["type"] == "descriptor_table":
            for rr in p.get("ranges", []):
                upper = 1_000_000_000 if rr["count"] == "unbounded" else rr["base"] + rr["count"] - 1
                caps.append({
                    "type": rr["type"],
                    "space": rr["space"],
                    "lower": rr["base"],
                    "upper": upper,
                    "visibility": visibility,
                    "cost": 1_000_000 if upper >= 1_000_000_000 else upper - rr["base"] + 1,
                })
    for sampler in sig.get("static_samplers", []):
        caps.append({"type": "sampler", "space": sampler["space"], "lower": sampler["reg"], "upper": sampler["reg"], "visibility": sampler.get("visibility", "all"), "cost": 0})
    return caps


def cap_covers_resource(cap: dict[str, Any], resource: dict[str, Any]) -> bool:
    stage = resource.get("stage") or "all"
    visibility = cap.get("visibility", "all")
    visibility_ok = visibility == "all" or stage == "all" or visibility == stage
    return (
        visibility_ok
        and cap["type"] == resource["type"]
        and cap["space"] == resource["space"]
        and cap["lower"] <= resource["lower"]
        and resource["upper"] <= cap["upper"]
    )


def infer_psv_resource_roots(refs: list[dict[str, Any]], roots: list[dict[str, Any]]) -> dict[str, Any]:
    resources = []
    for ref in refs:
        stage = ref.get("shader_kind") or "all"
        stage = "all" if stage in ("compute", "unknown", None) else stage
        for res in ref.get("psv_resource_bindings", []):
            staged_res = dict(res)
            staged_res["stage"] = stage
            if staged_res not in resources:
                resources.append(staged_res)
    if not resources:
        return {"status": "none", "roots": [], "resources": []}
    rows = []
    used_types = {r["type"] for r in resources}
    for root in roots:
        caps = root_capabilities(root)
        if not all(any(cap_covers_resource(cap, res) for cap in caps) for res in resources):
            continue
        present_types = {c["type"] for c in caps if c["type"] in ("cbv", "srv", "uav")}
        if any(res["type"] == "sampler" for res in resources):
            present_types.add("sampler")
        extra_types = len(present_types - used_types)
        score = (
            extra_types,
            root["root_signature"]["num_parameters"],
            sum(min(c["cost"], 1000) for c in caps),
        )
        rows.append((score, root))
    rows.sort(key=lambda item: item[0])
    if not rows:
        return {"status": "none", "roots": [], "resources": resources}
    best_score = rows[0][0]
    best = [root for score, root in rows if score == best_score]
    status = "unique_psv_resources" if len(best) == 1 else "ambiguous_psv_resources"
    return {
        "status": status,
        "roots": best,
        "resources": resources,
        "candidate_count": len(rows),
        "best_score": list(best_score),
    }


def infer_pipeline_roots(pipeline: dict[str, Any], bytecode_roots: dict[str, dict[str, Any]], roots: list[dict[str, Any]]) -> dict[str, Any]:
    refs = []
    counts: dict[str, int] = {}
    root_keys: dict[str, set[str]] = {}
    for src in pipeline.get("source_keys", []):
        key = src["record_key"]
        info = bytecode_roots.get(key)
        if not info:
            continue
        h = info.get("structural_signature_sha256") or ""
        refs.append({
            "record_offset": src["record_offset"],
            "record_key": key,
            "shader_kind": info.get("shader_kind"),
            "bytecode_ordinal": info.get("bytecode_ordinal"),
            "payload_size": info.get("payload_size"),
            "bytecode_sha256": info.get("bytecode_sha256"),
            "structural_signature_sha256": h,
            "root_signature_keys": info.get("root_signature_keys", []),
            "function_names": info.get("function_names", []),
            "psv_resource_bindings": info.get("psv_resource_bindings", []),
        })
        if h:
            counts[h] = counts.get(h, 0) + 1
            root_keys.setdefault(h, set()).update(info.get("root_signature_keys", []))
    inferred_hashes = sorted(counts, key=lambda h: (-counts[h], h))
    status = "none" if not inferred_hashes else ("unique" if len(inferred_hashes) == 1 else "ambiguous")
    inferred_keys = sorted(root_keys[inferred_hashes[0]]) if status == "unique" and inferred_hashes[0] in root_keys else []
    psv = {"status": "none", "roots": [], "resources": []}
    if status == "none":
        psv = infer_psv_resource_roots(refs, roots)
        status = psv["status"]
        inferred_keys = [r["record_key"] for r in psv["roots"]]
    return {
        "root_ref_count_by_structural_signature": counts,
        "root_signature_keys_by_structural_signature": {h: sorted(v) for h, v in root_keys.items()},
        "inferred_structural_signature_sha256": inferred_hashes[0] if len(inferred_hashes) == 1 else "",
        "inferred_root_signature_keys": inferred_keys,
        "root_inference_status": status,
        "psv_resource_bindings": psv.get("resources", []),
        "psv_candidate_count": psv.get("candidate_count", 0),
        "psv_best_score": psv.get("best_score", []),
        "source_key_roots": refs,
    }


def process_game(reference_root: Path, game: str, appid: str) -> dict[str, Any]:
    cache = reference_root / game / "shaders.cache" / "MTLGPUFamilyApple9_0"
    roots = ROOT_DECODER.decode_root_file(cache / "rootsignature_cache.bin")
    root_by_structural: dict[str, list[dict[str, Any]]] = {}
    for rec in roots:
        root_by_structural.setdefault(rec["structural_signature_sha256"], []).append(rec)

    bytecode = CACHE_MAP.scan_bytecode_records(cache / "bytecode_cache.bin")
    by_key = {r["record_key"]: r for r in bytecode if r.get("record_key")}
    stages = CACHE_MAP.scan_stage_records(cache / "stage_cache.bin")
    stage_by_key = build_stage_index(stages, by_key)
    bytecode_roots = decode_bytecode_roots(cache, bytecode, root_by_structural, stage_by_key)
    pipelines = CACHE_MAP.scan_pipeline_records(cache / "pipeline_cache.bin", by_key)

    linked_pipelines = []
    status_counts: dict[str, int] = {}
    for p in pipelines:
        inferred = infer_pipeline_roots(p, bytecode_roots, roots)
        status_counts[inferred["root_inference_status"]] = status_counts.get(inferred["root_inference_status"], 0) + 1
        out = dict(p)
        out.update(inferred)
        linked_pipelines.append(out)

    bytecode_matched_roots = sum(1 for info in bytecode_roots.values() if info.get("root_signature_keys"))
    bytecode_with_rts0 = sum(1 for info in bytecode_roots.values() if info.get("structural_signature_sha256"))
    return {
        "game": game,
        "appid": appid,
        "cache": str(cache),
        "summary": {
            "root_signature_records": len(roots),
            "unique_root_structural_signatures": len(root_by_structural),
            "bytecode_records": len(bytecode),
            "bytecode_records_with_rts0": bytecode_with_rts0,
            "bytecode_records_matching_root_cache": bytecode_matched_roots,
            "pipeline_records": len(pipelines),
            "pipeline_root_inference_status_counts": status_counts,
            "pipelines_with_unique_inferred_root": status_counts.get("unique", 0) + status_counts.get("unique_psv_resources", 0),
            "pipelines_with_ambiguous_inferred_root": status_counts.get("ambiguous", 0) + status_counts.get("ambiguous_psv_resources", 0),
        },
        "root_signatures": [
            {
                "ordinal": r["ordinal"],
                "record_key": r["record_key"],
                "compact_signature_sha256": r["compact_signature_sha256"],
                "structural_signature_sha256": r["structural_signature_sha256"],
                "num_parameters": r["root_signature"]["num_parameters"],
                "num_static_samplers": r["root_signature"]["num_static_samplers"],
                "flag_names": r["root_signature"]["flag_names"],
                "structural_signature": r["structural_signature"],
            }
            for r in roots
        ],
        "bytecode_roots_sample": list(bytecode_roots.values())[:300],
        "pipelines": linked_pipelines,
    }


def write_summary(result: dict[str, Any], path: Path) -> None:
    lines = ["# D3DMetal root ↔ bytecode ↔ pipeline linkage", ""]
    lines.append(f"- D3DMetal reference root: `{result['d3dmetal_reference_root']}`")
    lines.append("")
    lines.append("| Game | Roots | Bytecode | Bytecode RTS0 | Bytecode matched to root cache | Pipelines | Unique root pipelines | Ambiguous root pipelines |")
    lines.append("|---|---:|---:|---:|---:|---:|---:|---:|")
    for g in result["games"]:
        s = g["summary"]
        lines.append(f"| {g['game']} | {s['root_signature_records']} | {s['bytecode_records']} | {s['bytecode_records_with_rts0']} | {s['bytecode_records_matching_root_cache']} | {s['pipeline_records']} | {s['pipelines_with_unique_inferred_root']} | {s['pipelines_with_ambiguous_inferred_root']} |")
    lines.append("")
    for g in result["games"]:
        lines.append(f"## {g['game']}")
        lines.append("")
        lines.append(f"- root inference status counts: `{g['summary']['pipeline_root_inference_status_counts']}`")
        lines.append("- First uniquely root-linked pipelines:")
        shown = 0
        for p in g["pipelines"]:
            if p["root_inference_status"] not in ("unique", "unique_psv_resources"):
                continue
            refs = ", ".join(f"{r['shader_kind']}:{r['record_key']}" for r in p["source_key_roots"])
            roots = ",".join(p.get("inferred_root_signature_keys", []))
            suffix = "" if p["root_inference_status"] == "unique" else f" psv_score={p.get('psv_best_score', [])}"
            lines.append(f"  - pipeline=`{p['record_key']}` offset=`{p['record_offset']}` roots=`{roots}` refs=`{refs}` status=`{p['root_inference_status']}`{suffix}")
            shown += 1
            if shown >= 12:
                break
        lines.append("")
    path.write_text("\n".join(lines) + "\n")


def main() -> int:
    args = parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    games = [process_game(args.d3dmetal_reference_root, game, appid) for game, appid in GAME_APPIDS.items()]
    result = {
        "schema": "metalsharp.d3dmetal-root-pipeline-linkage.v1",
        "d3dmetal_reference_root": str(args.d3dmetal_reference_root),
        "games": games,
    }
    (args.out_dir / "manifest.json").write_text(json.dumps(result, indent=2) + "\n")
    write_summary(result, args.out_dir / "summary.md")
    print(args.out_dir / "manifest.json")
    print(args.out_dir / "summary.md")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
