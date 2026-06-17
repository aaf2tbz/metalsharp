#!/usr/bin/env python3
"""Extract embedded MTLB blobs from D3DMetal/GPTK shader caches.

D3DMetal's cache stores compiled Metal library blobs inside stage/libraries
stores. Observed MTLB container layout has:
  bytes 0..3   = b"MTLB"
  bytes 16..23 = little-endian total blob size

This extractor is intentionally defensive: it validates bounds, deduplicates by
sha256, records every source occurrence, and can run manifest-only unless an
--extract-dir is provided.
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

DEFAULT_SCAN_GLOBS = [
    "shaders.cache/MTLGPUFamilyApple9_0/stage_cache.bin",
    "shaders.cache/32024/libraries.data",
]


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_mtlb_header(blob: bytes) -> dict[str, int]:
    if len(blob) < 88 or not blob.startswith(b"MTLB"):
        return {}
    fields = [int.from_bytes(blob[i:i + 8], "little") for i in range(0, min(len(blob), 96), 8)]
    # Observed MetalLib header u64 fields:
    # 2=file size, 3=function list offset, 4=function list size,
    # 5=public metadata offset, 6=public metadata size,
    # 7=private metadata offset, 8=private metadata size,
    # 9=module list offset, 10=module list size,
    # 11=dynamic header offset, 12=dynamic header size,
    # 13=reflection list offset, 14=reflection list size.
    names = [
        "magic_version_a", "magic_version_b", "file_size",
        "function_list_offset", "function_list_size",
        "public_metadata_offset", "public_metadata_size",
        "private_metadata_offset", "private_metadata_size",
        "module_list_offset", "module_list_size",
        "dynamic_header_offset", "dynamic_header_size",
        "reflection_list_offset", "reflection_list_size",
    ]
    return {name: fields[idx] for idx, name in enumerate(names[:len(fields)])}


def extract_function_names(blob: bytes) -> list[str]:
    header = parse_mtlb_header(blob)
    off = header.get("function_list_offset", 0)
    size = header.get("function_list_size", 0)
    if not off or not size or off + size > len(blob):
        return []
    chunk = blob[off:off + size]
    names: list[str] = []
    start = 0
    while True:
        pos = chunk.find(b"NAME", start)
        if pos < 0:
            break
        start = pos + 4
        if pos + 8 > len(chunk):
            continue
        # Observed NAME record: b"NAME" + u16 byte_len + nul-terminated
        # ASCII name. byte_len includes the trailing nul.
        length = int.from_bytes(chunk[pos + 4:pos + 6], "little")
        name_start = pos + 6
        name_end = name_start + max(0, length - 1)
        if length <= 1 or name_end > len(chunk):
            continue
        raw = chunk[name_start:name_end]
        if not raw or any(c < 0x20 or c >= 0x7f for c in raw):
            continue
        text = raw.decode("ascii", errors="ignore")
        if not re.match(r"^[A-Za-z_][A-Za-z0-9_.$-]*$", text):
            continue
        if text in {"NAME", "TYPE", "HASH", "OFFT", "VERS", "MDSZ", "RFLT", "ENDT"}:
            continue
        if text not in names:
            names.append(text)
    return names


def find_mtlb_blobs(path: Path) -> list[dict[str, Any]]:
    if not path.exists() or not path.is_file():
        return []
    data = path.read_bytes()
    out: list[dict[str, Any]] = []
    start = 0
    while True:
        off = data.find(b"MTLB", start)
        if off < 0:
            break
        start = off + 1
        if off + 24 > len(data):
            continue
        size = int.from_bytes(data[off + 16:off + 24], "little")
        if size < 24 or off + size > len(data):
            continue
        blob = data[off:off + size]
        # Avoid false positives inside arbitrary payloads: the parsed blob must
        # begin with MTLB and contain ordinary Metal section markers near the
        # start or body.
        if not blob.startswith(b"MTLB"):
            continue
        if b"NAME" not in blob[:4096] and b"AIR" not in blob and b"TYPE" not in blob[:4096]:
            continue
        out.append({
            "offset": off,
            "size": size,
            "sha256": sha256_bytes(blob),
            "header": parse_mtlb_header(blob),
            "function_names": extract_function_names(blob),
            "blob": blob,
        })
    return out


def inventory_game(game_root: Path, extract_root: Path | None = None) -> dict[str, Any]:
    unique: dict[str, dict[str, Any]] = {}
    occurrences: list[dict[str, Any]] = []
    for rel in DEFAULT_SCAN_GLOBS:
        path = game_root / rel
        blobs = find_mtlb_blobs(path)
        for blob in blobs:
            sha = blob["sha256"]
            occurrence = {
                "source": rel,
                "offset": blob["offset"],
                "size": blob["size"],
                "sha256": sha,
            }
            occurrences.append(occurrence)
            if sha not in unique:
                record = {
                    "sha256": sha,
                    "size": blob["size"],
                    "first_source": rel,
                    "first_offset": blob["offset"],
                    "occurrence_count": 0,
                    "function_names": blob.get("function_names", []),
                    "header": blob.get("header", {}),
                }
                if extract_root is not None:
                    extract_root.mkdir(parents=True, exist_ok=True)
                    out = extract_root / f"{sha}.metallib"
                    out.write_bytes(blob["blob"])
                    record["path"] = str(out)
                unique[sha] = record
            unique[sha]["occurrence_count"] += 1
    return {
        "cache": str(game_root / "shaders.cache"),
        "scanned": DEFAULT_SCAN_GLOBS,
        "occurrence_count": len(occurrences),
        "unique_count": len(unique),
        "unique_total_bytes": sum(r["size"] for r in unique.values()),
        "unique": sorted(unique.values(), key=lambda r: (r["first_source"], r["first_offset"])),
        "occurrences": occurrences,
    }


def write_markdown(result: dict[str, Any], path: Path) -> None:
    lines = [
        "# D3DMetal extracted metallib inventory",
        "",
        f"- Reference root: `{result['d3dmetal_reference_root']}`",
        f"- Extract root: `{result.get('extract_root') or '<manifest-only>'}`",
        "",
        "| Game | MTLB occurrences | Unique metallibs | Unique bytes |",
        "|---|---:|---:|---:|",
    ]
    for game in result["games"]:
        inv = game["inventory"]
        lines.append(f"| {game['game']} | {inv['occurrence_count']} | {inv['unique_count']} | {inv['unique_total_bytes']} |")
    lines.append("")
    for game in result["games"]:
        inv = game["inventory"]
        lines.append(f"## {game['game']}")
        lines.append("")
        lines.append(f"- Cache: `{inv['cache']}`")
        lines.append(f"- Occurrences: `{inv['occurrence_count']}`")
        lines.append(f"- Unique metallibs: `{inv['unique_count']}`")
        lines.append(f"- Unique bytes: `{inv['unique_total_bytes']}`")
        function_hist: dict[str, int] = {}
        for rec in inv["unique"]:
            for name in rec.get("function_names") or ["<none>"]:
                function_hist[name] = function_hist.get(name, 0) + 1
        lines.append("- Function-name histogram:")
        for name, count in sorted(function_hist.items(), key=lambda item: (-item[1], item[0]))[:20]:
            lines.append(f"  - `{name}`: `{count}`")
        lines.append("- First 20 unique blobs:")
        for rec in inv["unique"][:20]:
            path_text = f" path=`{rec['path']}`" if rec.get("path") else ""
            names = ",".join(rec.get("function_names") or []) or "<none>"
            lines.append(f"  - `{rec['sha256']}` size={rec['size']} funcs=`{names}` first=`{rec['first_source']}@{rec['first_offset']}` occurrences={rec['occurrence_count']}{path_text}")
        lines.append("")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--d3dmetal-reference-root", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--extract", action="store_true", help="write unique .metallib blobs under out-dir/extracted")
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    extract_root = args.out_dir / "extracted" if args.extract else None
    games = []
    for game, appid in GAME_APPIDS.items():
        game_extract = extract_root / game if extract_root is not None else None
        games.append({
            "game": game,
            "appid": appid,
            "inventory": inventory_game(args.d3dmetal_reference_root / game, game_extract),
        })
    result = {
        "schema": "metalsharp.d3dmetal-metallib-extract.v1",
        "d3dmetal_reference_root": str(args.d3dmetal_reference_root),
        "extract_root": str(extract_root) if extract_root is not None else None,
        "games": games,
    }
    (args.out_dir / "manifest.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    write_markdown(result, args.out_dir / "summary.md")
    print(args.out_dir / "manifest.json")
    print(args.out_dir / "summary.md")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
