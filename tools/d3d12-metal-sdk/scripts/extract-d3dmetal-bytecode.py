#!/usr/bin/env python3
"""Extract and compare embedded D3D bytecode containers from copied D3DMetal caches.

This is offline-only. It reads copied GPTK/D3DMetal shader-cache references and
M12 shader-cache directories; it never mutates live shader caches.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any

GAME_APPIDS = {
    "elden-ring": "1245620",
    "armored-core-vi": "1888160",
    "subnautica-2": "1962700",
}

BYTECODE_FILES = [
    "MTLGPUFamilyApple9_0/bytecode_cache.bin",
]

DXBC_LEGACY_CHUNKS = {"SHEX", "SHDR"}
DXIL_SHADER_KINDS = {
    0: "pixel",
    1: "vertex",
    2: "geometry",
    3: "hull",
    4: "domain",
    5: "compute",
    6: "library",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--d3dmetal-reference-root", type=Path, required=True)
    parser.add_argument("--m12-cache-root", type=Path, default=Path.home() / ".metalsharp/shader-cache/m12")
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--extract", action="store_true", help="Write unique .dxbc blobs under out-dir/extracted/<game>/")
    return parser.parse_args()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def dxil_shader_kind(data: bytes, chunk: dict[str, Any]) -> str | None:
    if chunk["fourcc"] != "DXIL":
        return None
    payload = chunk["offset"] + 8
    if payload + 4 > len(data):
        return None
    program_version = int.from_bytes(data[payload:payload + 4], "little")
    return DXIL_SHADER_KINDS.get((program_version >> 16) & 0xffff, "unknown")


def validate_dxbc_at(data: bytes, offset: int) -> tuple[int, list[dict[str, Any]]] | None:
    if offset + 32 > len(data) or data[offset:offset + 4] != b"DXBC":
        return None
    total = int.from_bytes(data[offset + 24:offset + 28], "little")
    chunk_count = int.from_bytes(data[offset + 28:offset + 32], "little")
    if total < 32 or offset + total > len(data) or chunk_count > 64:
        return None
    table_end = offset + 32 + 4 * chunk_count
    if table_end > offset + total:
        return None
    chunks: list[dict[str, Any]] = []
    seen_offsets: set[int] = set()
    for idx in range(chunk_count):
        chunk_off = int.from_bytes(data[offset + 32 + 4 * idx:offset + 36 + 4 * idx], "little")
        if chunk_off in seen_offsets or chunk_off + 8 > total:
            return None
        seen_offsets.add(chunk_off)
        chunk_abs = offset + chunk_off
        fourcc_bytes = data[chunk_abs:chunk_abs + 4]
        try:
            fourcc = fourcc_bytes.decode("ascii")
        except UnicodeDecodeError:
            return None
        chunk_size = int.from_bytes(data[chunk_abs + 4:chunk_abs + 8], "little")
        if chunk_off + 8 + chunk_size > total:
            return None
        chunks.append({"index": idx, "fourcc": fourcc, "offset": chunk_off, "size": chunk_size})
    return total, chunks


def scan_dxbc_containers(path: Path) -> list[dict[str, Any]]:
    data = path.read_bytes()
    out: list[dict[str, Any]] = []
    start = 0
    while True:
        pos = data.find(b"DXBC", start)
        if pos < 0:
            break
        parsed = validate_dxbc_at(data, pos)
        if parsed:
            total, chunks = parsed
            blob = data[pos:pos + total]
            names = [chunk["fourcc"] for chunk in chunks]
            shader_kind = None
            for chunk in chunks:
                if chunk["fourcc"] == "DXIL":
                    shader_kind = dxil_shader_kind(blob, chunk)
                    break
            out.append({
                "offset": pos,
                "size": total,
                "sha256": sha256_bytes(blob),
                "chunk_names": names,
                "chunks": chunks,
                "kind": "dxil" if "DXIL" in names else ("legacy-dxbc" if DXBC_LEGACY_CHUNKS.intersection(names) else "other-dxbc-container"),
                "shader_kind": shader_kind,
                "blob": blob,
            })
            start = pos + total
        else:
            start = pos + 1
    return out


def inventory_d3dmetal_game(game: str, game_root: Path, out_dir: Path | None) -> dict[str, Any]:
    cache = game_root / "shaders.cache"
    unique: dict[str, dict[str, Any]] = {}
    occurrences = 0
    invalid_hits = 0
    for rel in BYTECODE_FILES:
        path = cache / rel
        if not path.is_file():
            continue
        records = scan_dxbc_containers(path)
        occurrences += len(records)
        for rec in records:
            sha = rec["sha256"]
            entry = unique.get(sha)
            if entry is None:
                entry = {
                    "sha256": sha,
                    "size": rec["size"],
                    "kind": rec["kind"],
                    "shader_kind": rec.get("shader_kind"),
                    "chunk_names": rec["chunk_names"],
                    "chunks": rec["chunks"],
                    "first_source": f"shaders.cache/{rel}",
                    "first_offset": rec["offset"],
                    "occurrence_count": 0,
                }
                unique[sha] = entry
                if out_dir:
                    out_dir.mkdir(parents=True, exist_ok=True)
                    (out_dir / f"{sha}.dxbc").write_bytes(rec["blob"])
            entry["occurrence_count"] += 1
    kind_counts: dict[str, int] = {}
    chunk_hist: dict[str, int] = {}
    shader_kind_counts: dict[str, int] = {}
    for rec in unique.values():
        kind_counts[rec["kind"]] = kind_counts.get(rec["kind"], 0) + 1
        shader_kind = rec.get("shader_kind") or "unknown"
        shader_kind_counts[shader_kind] = shader_kind_counts.get(shader_kind, 0) + 1
        for name in rec["chunk_names"]:
            chunk_hist[name] = chunk_hist.get(name, 0) + 1
    return {
        "game": game,
        "cache": str(cache),
        "occurrences": occurrences,
        "unique_count": len(unique),
        "unique_total_bytes": sum(rec["size"] for rec in unique.values()),
        "kind_counts": dict(sorted(kind_counts.items())),
        "shader_kind_counts": dict(sorted(shader_kind_counts.items())),
        "chunk_histogram": dict(sorted(chunk_hist.items(), key=lambda item: (-item[1], item[0]))),
        "unique": sorted(unique.values(), key=lambda rec: (rec["first_source"], rec["first_offset"])),
        "invalid_hits": invalid_hits,
    }


def inventory_m12_game(path: Path) -> dict[str, Any]:
    files = sorted(path.glob("*.dxbc")) if path.is_dir() else []
    by_sha: dict[str, dict[str, Any]] = {}
    kind_counts: dict[str, int] = {}
    for file in files:
        try:
            data = file.read_bytes()
        except OSError:
            continue
        sha = sha256_bytes(data)
        parsed = validate_dxbc_at(data, 0) if data.startswith(b"DXBC") else None
        chunks = parsed[1] if parsed else []
        names = [chunk["fourcc"] for chunk in chunks]
        shader_kind = None
        for chunk in chunks:
            if chunk["fourcc"] == "DXIL":
                shader_kind = dxil_shader_kind(data, chunk)
                break
        kind = "dxil" if "DXIL" in names else ("legacy-dxbc" if DXBC_LEGACY_CHUNKS.intersection(names) else "other-dxbc-container")
        kind_counts[kind] = kind_counts.get(kind, 0) + 1
        by_sha[sha] = {
            "sha256": sha,
            "file": str(file),
            "name": file.name,
            "size": len(data),
            "kind": kind,
            "shader_kind": shader_kind,
            "chunk_names": names,
        }
    return {
        "cache": str(path),
        "file_count": len(files),
        "unique_count": len(by_sha),
        "kind_counts": dict(sorted(kind_counts.items())),
        "by_sha": by_sha,
    }


def write_summary(result: dict[str, Any], path: Path) -> None:
    lines = ["# D3DMetal D3D bytecode extraction and M12 comparison", ""]
    lines.append(f"- D3DMetal reference root: `{result['d3dmetal_reference_root']}`")
    lines.append(f"- M12 cache root: `{result['m12_cache_root']}`")
    lines.append(f"- Extract root: `{result['extract_root'] or '<manifest-only>'}`")
    lines.append("")
    lines.append("This is an offline bytecode-container comparison. A raw SHA match means identical D3D bytecode bytes; a miss does not prove semantic mismatch because M12 cache coverage depends on captured runtime paths and cache-cold state.")
    lines.append("")
    lines.append("| Game | D3DM occurrences | D3DM unique | D3DM DXIL | D3DM legacy DXBC | M12 DXBC files | M12 unique | Raw SHA overlap | D3DM-only | M12-only |")
    lines.append("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
    for game in result["games"]:
        d = game["d3dmetal"]
        m = game["m12"]
        c = game["comparison"]
        lines.append(
            f"| {game['game']} | {d['occurrences']} | {d['unique_count']} | {d['kind_counts'].get('dxil', 0)} | {d['kind_counts'].get('legacy-dxbc', 0)} | {m['file_count']} | {m['unique_count']} | {c['raw_sha_overlap']} | {c['d3dmetal_only']} | {c['m12_only']} |"
        )
    lines.append("")
    for game in result["games"]:
        d = game["d3dmetal"]
        m = game["m12"]
        c = game["comparison"]
        lines.append(f"## {game['game']}")
        lines.append("")
        lines.append(f"- D3DMetal bytecode occurrences: `{d['occurrences']}`")
        lines.append(f"- D3DMetal unique bytecode containers: `{d['unique_count']}`")
        lines.append(f"- D3DMetal kind counts: `{d['kind_counts']}`")
        lines.append(f"- D3DMetal shader-kind counts: `{d.get('shader_kind_counts', {})}`")
        lines.append(f"- Top chunk names: `{dict(list(d['chunk_histogram'].items())[:12])}`")
        lines.append(f"- M12 `.dxbc` files: `{m['file_count']}` unique=`{m['unique_count']}` kinds=`{m['kind_counts']}`")
        lines.append(f"- Raw SHA overlap: `{c['raw_sha_overlap']}`")
        lines.append(f"- D3DMetal-only unique containers: `{c['d3dmetal_only']}`")
        lines.append(f"- M12-only unique containers: `{c['m12_only']}`")
        if c["overlap_examples"]:
            lines.append("- Overlap examples:")
            for rec in c["overlap_examples"][:10]:
                lines.append(f"  - `{rec['sha256']}` d3dm=`{rec['d3dmetal_first_source']}@{rec['d3dmetal_first_offset']}` m12=`{rec['m12_name']}` kind=`{rec['kind']}`")
        lines.append("")
    path.write_text("\n".join(lines) + "\n")


def main() -> int:
    args = parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    extract_root = args.out_dir / "extracted" if args.extract else None
    games: list[dict[str, Any]] = []
    for game, appid in GAME_APPIDS.items():
        d3d_extract = extract_root / game if extract_root else None
        d3dm = inventory_d3dmetal_game(game, args.d3dmetal_reference_root / game, d3d_extract)
        m12 = inventory_m12_game(args.m12_cache_root / appid)
        d3d_shas = {rec["sha256"]: rec for rec in d3dm["unique"]}
        m12_shas = m12["by_sha"]
        overlap = sorted(set(d3d_shas) & set(m12_shas))
        examples = []
        for sha in overlap[:50]:
            examples.append({
                "sha256": sha,
                "kind": d3d_shas[sha]["kind"],
                "d3dmetal_first_source": d3d_shas[sha]["first_source"],
                "d3dmetal_first_offset": d3d_shas[sha]["first_offset"],
                "m12_name": m12_shas[sha]["name"],
            })
        comparison = {
            "raw_sha_overlap": len(overlap),
            "d3dmetal_only": len(set(d3d_shas) - set(m12_shas)),
            "m12_only": len(set(m12_shas) - set(d3d_shas)),
            "overlap_examples": examples,
        }
        # Keep manifest compact: by_sha map is redundant/noisy; unique records are enough.
        m12_compact = {k: v for k, v in m12.items() if k != "by_sha"}
        games.append({"game": game, "appid": appid, "d3dmetal": d3dm, "m12": m12_compact, "comparison": comparison})
    result = {
        "schema": "metalsharp.d3dmetal-bytecode-extract.v1",
        "d3dmetal_reference_root": str(args.d3dmetal_reference_root),
        "m12_cache_root": str(args.m12_cache_root),
        "extract_root": str(extract_root) if extract_root else None,
        "games": games,
    }
    (args.out_dir / "manifest.json").write_text(json.dumps(result, indent=2) + "\n")
    write_summary(result, args.out_dir / "summary.md")
    print(args.out_dir / "manifest.json")
    print(args.out_dir / "summary.md")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
