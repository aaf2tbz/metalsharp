#!/usr/bin/env python3
"""Build a D3DMetal cache oracle map from copied shader caches.

The observed D3DMetal cache files store per-record metadata before embedded
payloads. Bytecode records contain a 16-byte cache key immediately before the
DXBC size/payload. Stage records contain the same 16-byte source bytecode key in
metadata before the embedded MTLB. This script links those records and reports
which known-good D3DMetal metallibs correspond to which source D3D bytecode.

Offline-only: reads copied caches and writes results under --out-dir.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path
from typing import Any

GAME_APPIDS = {
    "elden-ring": "1245620",
    "armored-core-vi": "1888160",
    "subnautica-2": "1962700",
}

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
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--d3dmetal-reference-root", type=Path, required=True)
    p.add_argument("--m12-cache-root", type=Path, default=Path.home() / ".metalsharp/shader-cache/m12")
    p.add_argument("--out-dir", type=Path, required=True)
    return p.parse_args()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_mtlb_size(data: bytes, off: int) -> int | None:
    if off + 24 > len(data) or data[off:off + 4] != b"MTLB":
        return None
    size = int.from_bytes(data[off + 16:off + 24], "little")
    if size >= 32 and off + size <= len(data):
        return size
    return None


def dxbc_chunks(blob: bytes) -> list[dict[str, Any]]:
    if len(blob) < 32 or blob[:4] != b"DXBC":
        return []
    total = int.from_bytes(blob[24:28], "little")
    count = int.from_bytes(blob[28:32], "little")
    if total != len(blob) or count > 64 or 32 + 4 * count > total:
        return []
    chunks = []
    for i in range(count):
        off = int.from_bytes(blob[32 + i * 4:36 + i * 4], "little")
        if off + 8 > total:
            return []
        fourcc = blob[off:off + 4].decode("ascii", "replace")
        size = int.from_bytes(blob[off + 4:off + 8], "little")
        if off + 8 + size > total:
            return []
        chunks.append({"index": i, "fourcc": fourcc, "offset": off, "size": size})
    return chunks


def dxil_shader_kind(blob: bytes, chunks: list[dict[str, Any]]) -> str | None:
    for chunk in chunks:
        if chunk["fourcc"] != "DXIL":
            continue
        payload = chunk["offset"] + 8
        if payload + 4 > len(blob):
            return None
        version = int.from_bytes(blob[payload:payload + 4], "little")
        return DXIL_SHADER_KINDS.get((version >> 16) & 0xffff, "unknown")
    return None


def extract_function_names_from_mtlb(blob: bytes) -> list[str]:
    if len(blob) < 88 or blob[:4] != b"MTLB":
        return []
    function_off = int.from_bytes(blob[24:32], "little")
    function_size = int.from_bytes(blob[32:40], "little")
    if function_off <= 0 or function_size <= 0 or function_off + function_size > len(blob):
        return []
    chunk = blob[function_off:function_off + function_size]
    names: list[str] = []
    start = 0
    while True:
        pos = chunk.find(b"NAME", start)
        if pos < 0:
            break
        start = pos + 4
        if pos + 6 > len(chunk):
            continue
        length = int.from_bytes(chunk[pos + 4:pos + 6], "little")
        name_start = pos + 6
        name_end = name_start + max(0, length - 1)
        if length <= 1 or name_end > len(chunk):
            continue
        raw = chunk[name_start:name_end]
        if not raw or any(c < 0x20 or c >= 0x7f for c in raw):
            continue
        text = raw.decode("ascii", "ignore")
        if not re.match(r"^[A-Za-z_][A-Za-z0-9_.$-]*$", text):
            continue
        if text not in names:
            names.append(text)
    return names


def scan_bytecode_records(path: Path) -> list[dict[str, Any]]:
    data = path.read_bytes()
    out = []
    start = 0
    while True:
        off = data.find(b"DXBC", start)
        if off < 0:
            break
        size = int.from_bytes(data[off + 24:off + 28], "little") if off + 28 <= len(data) else 0
        blob = data[off:off + size] if size >= 32 and off + size <= len(data) else b""
        chunks = dxbc_chunks(blob)
        if chunks:
            key_off = off - 20
            key = data[key_off:key_off + 16].hex() if key_off >= 0 else ""
            out.append({
                "ordinal": len(out),
                "record_payload_offset": off,
                "payload_size": size,
                "record_key": key,
                "bytecode_sha256": sha256_bytes(blob),
                "chunk_names": [c["fourcc"] for c in chunks],
                "shader_kind": dxil_shader_kind(blob, chunks),
            })
            start = off + size
        else:
            start = off + 1
    return out


def scan_stage_records(path: Path) -> list[dict[str, Any]]:
    data = path.read_bytes()
    out = []
    start = 0
    while True:
        off = data.find(b"MTLB", start)
        if off < 0:
            break
        size = parse_mtlb_size(data, off)
        if not size:
            start = off + 1
            continue
        blob = data[off:off + size]
        meta_start = max(0, off - 160)
        meta = data[meta_start:off]
        keys = []
        # Observed source-bytecode key is a 16-byte binary key followed by a
        # little-endian metadata tag and the source DXBC payload size.
        # AC6/Elden often use tag 3; Subnautica 2 uses several tags including
        # 2, 0x1b, and 0x22. Candidates are filtered against real bytecode keys.
        for idx in range(16, max(16, len(meta) - 8)):
            tag = int.from_bytes(meta[idx:idx + 4], "little")
            source_size = int.from_bytes(meta[idx + 4:idx + 8], "little")
            if tag <= 128 and 32 <= source_size <= 4 * 1024 * 1024:
                keys.append(meta[idx - 16:idx].hex())
        filename = ""
        name_start = data.rfind(b"air_lib_", meta_start, off)
        if name_start < 0:
            name_start = data.rfind(b"stage_in_", meta_start, off)
        if name_start >= 0:
            raw = data[name_start:off].split(b"\x00", 1)[0]
            filename = raw.decode("ascii", "ignore")
        out.append({
            "ordinal": len(out),
            "metallib_offset": off,
            "metallib_size": size,
            "metallib_sha256": sha256_bytes(blob),
            "candidate_source_keys": sorted(set(keys)),
            "function_names": extract_function_names_from_mtlb(blob),
            "filename": filename,
        })
        start = off + size
    return out


def scan_pipeline_records(path: Path, by_key: dict[str, dict[str, Any]]) -> list[dict[str, Any]]:
    if not path.exists():
        return []
    data = path.read_bytes()
    out = []
    pos = 0
    while pos + 40 <= len(data):
        span = int.from_bytes(data[pos + 24:pos + 32], "little")
        payload_size = int.from_bytes(data[pos + 32:pos + 40], "little")
        if span < 64 or span > 1024 * 1024 or pos + span > len(data):
            break
        rec = data[pos:pos + span]
        record_key = rec[40:56].hex() if len(rec) >= 56 else ""
        source_keys = []
        seen = set()
        for idx in range(0, max(0, len(rec) - 15)):
            key = rec[idx:idx + 16].hex()
            if key in by_key and key not in seen:
                seen.add(key)
                src = by_key[key]
                source_keys.append({
                    "record_offset": idx,
                    "record_key": key,
                    "bytecode_ordinal": src["ordinal"],
                    "bytecode_sha256": src["bytecode_sha256"],
                    "shader_kind": src.get("shader_kind"),
                })
        out.append({
            "ordinal": len(out),
            "record_offset": pos,
            "record_span": span,
            "payload_size": payload_size,
            "record_key": record_key,
            "source_keys": source_keys,
        })
        pos += span
    return out


def m12_bytecode_index(path: Path) -> dict[str, dict[str, Any]]:
    out = {}
    if not path.is_dir():
        return out
    for p in path.glob("*.dxbc"):
        try:
            blob = p.read_bytes()
        except OSError:
            continue
        out[sha256_bytes(blob)] = {"name": p.name, "path": str(p), "size": len(blob)}
    return out


def write_summary(result: dict[str, Any], path: Path) -> None:
    lines = ["# D3DMetal cache oracle map", ""]
    lines.append(f"- D3DMetal reference root: `{result['d3dmetal_reference_root']}`")
    lines.append(f"- M12 cache root: `{result['m12_cache_root']}`")
    lines.append("")
    lines.append("| Game | Bytecode records | Stage metallibs | Pipeline records | Linked stages | Linked source bytecodes | M12 raw bytecode overlap | Unlinked stages |")
    lines.append("|---|---:|---:|---:|---:|---:|---:|---:|")
    for g in result["games"]:
        s = g["summary"]
        lines.append(f"| {g['game']} | {s['bytecode_records']} | {s['stage_metallibs']} | {s['pipeline_records']} | {s['linked_stage_records']} | {s['linked_bytecode_records']} | {s['m12_raw_bytecode_overlap']} | {s['unlinked_stage_records']} |")
    lines.append("")
    for g in result["games"]:
        s = g["summary"]
        lines.append(f"## {g['game']}")
        lines.append("")
        for k in ["bytecode_records", "stage_metallibs", "pipeline_records", "pipeline_records_with_bytecode_keys", "pipeline_bytecode_key_refs", "linked_stage_records", "linked_bytecode_records", "m12_raw_bytecode_overlap", "unlinked_stage_records"]:
            lines.append(f"- {k}: `{s[k]}`")
        lines.append(f"- shader_kind_counts: `{s['shader_kind_counts']}`")
        lines.append(f"- top_function_names: `{s['top_function_names']}`")
        lines.append("- First pipeline records with bytecode keys:")
        for rec in [p for p in g.get("pipeline_records_sample", []) if p.get("source_keys")][:10]:
            refs = ", ".join(f"{s['shader_kind']}:{s['record_key']}" for s in rec["source_keys"])
            lines.append(f"  - pipeline=`{rec['record_key']}` offset=`{rec['record_offset']}` refs=`{refs}`")
        lines.append("- First linked records:")
        for rec in g["linked_records"][:10]:
            lines.append(f"  - bytecode_key=`{rec['record_key']}` stage=`{rec['stage_metallib_sha256'][:16]}` funcs=`{','.join(rec['function_names']) or '<none>'}` dxil=`{rec['bytecode_sha256'][:16]}` kind=`{rec.get('shader_kind')}` m12=`{rec.get('m12_name','')}`")
        lines.append("")
    path.write_text("\n".join(lines) + "\n")


def main() -> int:
    args = parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    games = []
    for game, appid in GAME_APPIDS.items():
        cache = args.d3dmetal_reference_root / game / "shaders.cache" / "MTLGPUFamilyApple9_0"
        bytecode = scan_bytecode_records(cache / "bytecode_cache.bin")
        stages = scan_stage_records(cache / "stage_cache.bin")
        by_key = {r["record_key"]: r for r in bytecode if r["record_key"]}
        pipelines = scan_pipeline_records(cache / "pipeline_cache.bin", by_key)
        m12 = m12_bytecode_index(args.m12_cache_root / appid)
        linked = []
        linked_bytecode_keys = set()
        unlinked = 0
        for st in stages:
            src = None
            for key in st["candidate_source_keys"]:
                if key in by_key:
                    src = by_key[key]
                    break
            if not src:
                unlinked += 1
                continue
            linked_bytecode_keys.add(src["record_key"])
            linked.append({
                "record_key": src["record_key"],
                "bytecode_ordinal": src["ordinal"],
                "stage_ordinal": st["ordinal"],
                "bytecode_sha256": src["bytecode_sha256"],
                "shader_kind": src.get("shader_kind"),
                "stage_metallib_sha256": st["metallib_sha256"],
                "stage_metallib_offset": st["metallib_offset"],
                "stage_metallib_size": st["metallib_size"],
                "function_names": st["function_names"],
                "stage_filename": st["filename"],
                "m12_name": m12.get(src["bytecode_sha256"], {}).get("name", ""),
            })
        kind_counts = {}
        for r in bytecode:
            k = r.get("shader_kind") or "unknown"
            kind_counts[k] = kind_counts.get(k, 0) + 1
        func_counts = {}
        for st in stages:
            for fn in st["function_names"]:
                func_counts[fn] = func_counts.get(fn, 0) + 1
        summary = {
            "bytecode_records": len(bytecode),
            "stage_metallibs": len(stages),
            "pipeline_records": len(pipelines),
            "pipeline_records_with_bytecode_keys": sum(1 for p in pipelines if p["source_keys"]),
            "pipeline_bytecode_key_refs": sum(len(p["source_keys"]) for p in pipelines),
            "linked_stage_records": len(linked),
            "linked_bytecode_records": len(linked_bytecode_keys),
            "m12_raw_bytecode_overlap": sum(1 for r in bytecode if r["bytecode_sha256"] in m12),
            "unlinked_stage_records": unlinked,
            "shader_kind_counts": dict(sorted(kind_counts.items())),
            "top_function_names": dict(sorted(func_counts.items(), key=lambda item: (-item[1], item[0]))[:20]),
        }
        games.append({
            "game": game,
            "appid": appid,
            "cache": str(cache),
            "summary": summary,
            "bytecode_records": bytecode[:200],
            "stage_records": stages[:200],
            "pipeline_records_sample": pipelines[:200],
            "pipeline_records": pipelines,
            "linked_records": linked,
        })
    result = {
        "schema": "metalsharp.d3dmetal-cache-oracle-map.v1",
        "d3dmetal_reference_root": str(args.d3dmetal_reference_root),
        "m12_cache_root": str(args.m12_cache_root),
        "games": games,
    }
    (args.out_dir / "oracle-map.json").write_text(json.dumps(result, indent=2) + "\n")
    write_summary(result, args.out_dir / "summary.md")
    print(args.out_dir / "oracle-map.json")
    print(args.out_dir / "summary.md")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
