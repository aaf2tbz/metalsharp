#!/usr/bin/env python3
"""Materialize mapped D3DMetal oracle metallibs for selected M12 shaders.

Reads a decompile-d3dmetal-cache-map.py oracle-map.json and the copied
D3DMetal stage_cache.bin. Writes an offline support pack under --out-dir. It
never writes into live M12 caches unless a future script explicitly consumes the
pack.
"""
from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--oracle-map", type=Path, required=True)
    p.add_argument("--game", required=True)
    p.add_argument("--m12-stem", action="append", default=[], help="M12 shader cache stem, e.g. 58539be4844b1dd9. Repeatable.")
    p.add_argument("--out-dir", type=Path, required=True)
    p.add_argument("--objdump", action="store_true")
    return p.parse_args()


def run_objdump(path: Path, out_prefix: Path) -> dict[str, Any]:
    outputs = {}
    commands = {
        "private_headers": ["xcrun", "metal-objdump", "--metallib", "--private-headers", str(path)],
        "reflection": ["xcrun", "metal-objdump", "--metallib", "--reflection", str(path)],
        "build_table_all": ["xcrun", "metal-objdump", "--metallib", "--build-table=all", str(path)],
    }
    for name, cmd in commands.items():
        proc = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
        out = out_prefix.with_suffix(f".{name}.txt")
        err = out_prefix.with_suffix(f".{name}.err.txt")
        out.write_text(proc.stdout or "")
        err.write_text(proc.stderr or "")
        outputs[name] = {"returncode": proc.returncode, "stdout": str(out), "stderr": str(err)}
    return outputs


def main() -> int:
    args = parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    oracle = json.loads(args.oracle_map.read_text())
    game_entry = next((g for g in oracle["games"] if g["game"] == args.game), None)
    if not game_entry:
        raise SystemExit(f"game not found in oracle map: {args.game}")
    stage_cache = Path(game_entry["cache"]) / "stage_cache.bin"
    data = stage_cache.read_bytes()
    wanted = set(args.m12_stem)
    records = []
    for rec in game_entry["linked_records"]:
        stem = (rec.get("m12_name") or "").removesuffix(".dxbc")
        if wanted and stem not in wanted:
            continue
        if not stem:
            continue
        off = int(rec["stage_metallib_offset"])
        size = int(rec["stage_metallib_size"])
        blob = data[off:off + size]
        if len(blob) != size or not blob.startswith(b"MTLB"):
            raise SystemExit(f"bad metallib slice for {stem}: {off}+{size}")
        stem_dir = args.out_dir / args.game / stem
        stem_dir.mkdir(parents=True, exist_ok=True)
        lib_path = stem_dir / f"{rec['stage_metallib_sha256']}.metallib"
        lib_path.write_bytes(blob)
        outputs = run_objdump(lib_path, stem_dir / rec["stage_metallib_sha256"]) if args.objdump else {}
        out_rec = dict(rec)
        out_rec["oracle_metallib"] = str(lib_path)
        out_rec["objdump"] = outputs
        records.append(out_rec)
    manifest = {
        "schema": "metalsharp.d3dmetal-oracle-pack.v1",
        "oracle_map": str(args.oracle_map),
        "game": args.game,
        "stage_cache": str(stage_cache),
        "requested_m12_stems": sorted(wanted),
        "record_count": len(records),
        "records": records,
    }
    (args.out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    lines = ["# D3DMetal oracle support pack", "", f"- game: `{args.game}`", f"- records: `{len(records)}`", ""]
    for rec in records:
        lines.append(f"- m12=`{rec.get('m12_name')}` kind=`{rec.get('shader_kind')}` funcs=`{','.join(rec.get('function_names') or [])}` metallib=`{rec['oracle_metallib']}`")
    (args.out_dir / "summary.md").write_text("\n".join(lines) + "\n")
    print(args.out_dir / "manifest.json")
    print(args.out_dir / "summary.md")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
