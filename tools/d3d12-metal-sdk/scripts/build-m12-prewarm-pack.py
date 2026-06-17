#!/usr/bin/env python3
"""Build a compact M12 oracle/prewarm pack from D3DMetal linkage metadata.

The pack is intentionally metadata-only: it contains pipeline/root/shader keys,
stage linkage, compact root-layout summaries, and a deterministic prewarm order.
It never copies D3DMetal cache payloads, DXBC blobs, extracted metallibs, or M12
shader cache files. Runtime code can use this as a profile-gated hint source
without depending on raw D3DMetal binary compatibility.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any

SCHEMA = "metalsharp.m12-prewarm-pack.v1"
DEFAULT_MAX_PIPELINES = 64

STAGE_BITS = {
    "vertex": 1 << 0,
    "pixel": 1 << 1,
    "geometry": 1 << 2,
    "hull": 1 << 3,
    "domain": 1 << 4,
    "compute": 1 << 5,
    "unknown": 1 << 31,
    None: 1 << 31,
}


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--linkage-manifest", type=Path, required=True)
    p.add_argument("--game", required=True, help="Game slug in the linkage manifest, e.g. armored-core-vi")
    p.add_argument("--out-dir", type=Path, required=True)
    p.add_argument("--max-pipelines", type=int, default=DEFAULT_MAX_PIPELINES)
    p.add_argument(
        "--status",
        action="append",
        default=None,
        help="Accepted root inference status. Repeatable; default unique + unique_psv_resources. Explicit values replace the default set.",
    )
    p.add_argument("--profile", default="offline-profile", help="Profile gate label recorded in the pack")
    return p.parse_args()


def stable_u64(text: str) -> int:
    return int.from_bytes(hashlib.sha256(text.encode("utf-8")).digest()[:8], "little")


def sha256_obj(obj: Any) -> str:
    return hashlib.sha256(json.dumps(obj, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def stage_mask(refs: list[dict[str, Any]]) -> int:
    mask = 0
    for ref in refs:
        mask |= STAGE_BITS.get(ref.get("shader_kind"), STAGE_BITS["unknown"])
    return mask


def compact_root_summary(root: dict[str, Any]) -> dict[str, Any]:
    sig = root.get("structural_signature") or {}
    params = sig.get("parameters") or []
    static_samplers = sig.get("static_samplers") or []
    descriptor_tables = 0
    descriptor_ranges = 0
    root_descriptors = 0
    root_constants = 0
    resource_ranges = 0
    sampler_ranges = 0
    unbounded_ranges = 0
    register_spaces: set[int] = set()
    visibility: set[str] = set()

    for p in params:
        visibility.add(str(p.get("visibility", "all")))
        ptype = p.get("type")
        if ptype == "descriptor_table":
            descriptor_tables += 1
            for rr in p.get("ranges") or []:
                descriptor_ranges += 1
                if rr.get("type") == "sampler":
                    sampler_ranges += 1
                else:
                    resource_ranges += 1
                if rr.get("count") == "unbounded":
                    unbounded_ranges += 1
                if isinstance(rr.get("space"), int):
                    register_spaces.add(rr["space"])
        elif ptype in {"cbv", "srv", "uav"}:
            root_descriptors += 1
            if isinstance(p.get("space"), int):
                register_spaces.add(p["space"])
        elif ptype == "constants":
            root_constants += 1
            if isinstance(p.get("space"), int):
                register_spaces.add(p["space"])

    for s in static_samplers:
        visibility.add(str(s.get("visibility", "all")))
        if isinstance(s.get("space"), int):
            register_spaces.add(s["space"])

    return {
        "record_key": root.get("record_key", ""),
        "compact_signature_sha256": root.get("compact_signature_sha256", ""),
        "structural_signature_sha256": root.get("structural_signature_sha256", ""),
        "num_parameters": root.get("num_parameters", 0),
        "num_static_samplers": root.get("num_static_samplers", 0),
        "descriptor_table_count": descriptor_tables,
        "descriptor_range_count": descriptor_ranges,
        "root_descriptor_count": root_descriptors,
        "root_constant_count": root_constants,
        "static_sampler_count": len(static_samplers),
        "resource_range_count": resource_ranges,
        "sampler_range_count": sampler_ranges,
        "unbounded_range_count": unbounded_ranges,
        "register_space_count": len(register_spaces),
        "visibility": sorted(visibility),
    }


def select_game(manifest: dict[str, Any], game: str) -> dict[str, Any]:
    for row in manifest.get("games") or []:
        if row.get("game") == game:
            return row
    raise SystemExit(f"game not found in linkage manifest: {game}")


def normalized_input_path(path: Path) -> str:
    try:
        return str(path.resolve().relative_to(Path.cwd().resolve()))
    except ValueError:
        return path.resolve().as_posix()


def build_pack(args: argparse.Namespace) -> dict[str, Any]:
    linkage_bytes = args.linkage_manifest.read_bytes()
    linkage = json.loads(linkage_bytes)
    game = select_game(linkage, args.game)
    roots_by_key = {r.get("record_key"): r for r in game.get("root_signatures") or []}
    accepted = set(args.status or ["unique", "unique_psv_resources"])
    pipelines = []
    root_summaries: dict[str, dict[str, Any]] = {}
    shader_keys: set[str] = set()
    shader_hashes: set[str] = set()

    for p in game.get("pipelines") or []:
        if p.get("root_inference_status") not in accepted:
            continue
        roots = [key for key in p.get("inferred_root_signature_keys") or [] if key]
        if not roots:
            continue
        refs = p.get("source_key_roots") or []
        for ref in refs:
            if ref.get("record_key"):
                shader_keys.add(ref["record_key"])
            if ref.get("bytecode_sha256"):
                shader_hashes.add(ref["bytecode_sha256"])
        for key in roots:
            root = roots_by_key.get(key)
            if root:
                root_summaries.setdefault(key, compact_root_summary(root))
        order = len(pipelines)
        pipelines.append({
            "prewarm_order": order,
            "pipeline_key": p.get("record_key", ""),
            "pipeline_key_u64": stable_u64(p.get("record_key", "")),
            "pipeline_offset": p.get("record_offset", 0),
            "root_signature_keys": roots,
            "root_structural_signatures": sorted(set((p.get("root_ref_count_by_structural_signature") or {}).keys()) | {
                roots_by_key[key].get("structural_signature_sha256", "")
                for key in roots
                if key in roots_by_key and roots_by_key[key].get("structural_signature_sha256")
            }),
            "stage_mask": stage_mask(refs),
            "stage_linkage": [
                {
                    "stage": ref.get("shader_kind") or "unknown",
                    "shader_key": ref.get("record_key", ""),
                    "shader_bytecode_sha256": ref.get("bytecode_sha256", ""),
                    "structural_signature_sha256": ref.get("structural_signature_sha256", ""),
                    "function_names": ref.get("function_names") or [],
                }
                for ref in refs
            ],
            "root_inference_status": p.get("root_inference_status", ""),
            "psv_best_score": p.get("psv_best_score", []),
        })
        if len(pipelines) >= args.max_pipelines:
            break

    compact = {
        "schema": SCHEMA,
        "source_schema": linkage.get("schema", ""),
        "source_linkage_manifest": normalized_input_path(args.linkage_manifest),
        "source_linkage_manifest_sha256": hashlib.sha256(linkage_bytes).hexdigest(),
        "game": game.get("game", args.game),
        "appid": game.get("appid", ""),
        "profile": args.profile,
        "offline_profile_gated": True,
        "raw_payloads_included": False,
        "max_pipelines": args.max_pipelines,
        "pipeline_count": len(pipelines),
        "root_signature_count": len(root_summaries),
        "shader_key_count": len(shader_keys),
        "shader_bytecode_hash_count": len(shader_hashes),
        "accepted_statuses": sorted(accepted),
        "roots": [root_summaries[k] for k in sorted(root_summaries)],
        "pipelines": pipelines,
    }
    compact["pack_sha256"] = sha256_obj({
        k: v for k, v in compact.items()
        if k not in {"pack_sha256", "source_linkage_manifest"}
    })
    return compact


def write_summary(pack: dict[str, Any], path: Path) -> None:
    lines = [
        "# M12 compact prewarm pack",
        "",
        f"- schema: `{pack['schema']}`",
        f"- game: `{pack['game']}` appid=`{pack['appid']}`",
        f"- profile: `{pack['profile']}` offline_profile_gated=`{pack['offline_profile_gated']}`",
        f"- raw_payloads_included: `{pack['raw_payloads_included']}`",
        f"- pipelines: `{pack['pipeline_count']}` roots: `{pack['root_signature_count']}` shader_keys: `{pack['shader_key_count']}` shader_hashes: `{pack['shader_bytecode_hash_count']}`",
        f"- pack_sha256: `{pack['pack_sha256']}`",
        "",
        "## First prewarm records",
        "",
    ]
    for row in pack.get("pipelines", [])[:16]:
        stages = ",".join(f"{s['stage']}:{s['shader_key']}" for s in row.get("stage_linkage", []))
        roots = ",".join(row.get("root_signature_keys", []))
        lines.append(f"- order=`{row['prewarm_order']}` pipeline=`{row['pipeline_key']}` roots=`{roots}` stages=`{stages}` status=`{row['root_inference_status']}`")
    path.write_text("\n".join(lines) + "\n")


def main() -> int:
    args = parse_args()
    if args.max_pipelines <= 0:
        raise SystemExit("--max-pipelines must be positive")
    args.out_dir.mkdir(parents=True, exist_ok=True)
    pack = build_pack(args)
    manifest_path = args.out_dir / "prewarm-pack.json"
    manifest_path.write_text(json.dumps(pack, indent=2) + "\n")
    summary_path = args.out_dir / "summary.md"
    write_summary(pack, summary_path)
    print(manifest_path)
    print(summary_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
