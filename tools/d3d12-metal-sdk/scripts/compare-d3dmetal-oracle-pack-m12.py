#!/usr/bin/env python3
"""Compare a materialized D3DMetal oracle pack against M12 MSL bindings."""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--oracle-pack", type=Path, required=True, help="manifest.json from materialize-d3dmetal-oracle-pack.py")
    p.add_argument("--m12-cache-dir", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    return p.parse_args()


def extract_reflection_json(path: Path) -> dict[str, Any] | None:
    text = path.read_text(errors="replace")
    start = text.find("{")
    end = text.rfind("}")
    if start < 0 or end < start:
        return None
    try:
        return json.loads(text[start:end + 1])
    except json.JSONDecodeError:
        return None


def summarize_reflection(reflection: dict[str, Any] | None) -> dict[str, Any]:
    if not reflection:
        return {"function": None, "args": [], "returns": [], "arg_counts": {}}
    nodes = reflection.get("nodes") or []
    funcs = [n for n in nodes if n.get("node_type") in {"VertexFunction", "FragmentFunction", "KernelFunction"}]
    function = funcs[0]["node"].get("name") if funcs else None
    args = []
    returns = []
    counts: dict[str, int] = {}
    for n in nodes:
        nt = n.get("node_type")
        node = n.get("node") or {}
        counts[nt] = counts.get(nt, 0) + 1
        if nt and (nt.endswith("Arg") or nt in {"BufferArg", "TextureArg", "SamplerArg", "IndirectBufferArg"}):
            loc = node.get("location_index")
            cnt = node.get("location_count")
            args.append({
                "node_type": nt,
                "name": node.get("name"),
                "type_name": node.get("type_name"),
                "location": loc.get("value") if isinstance(loc, dict) else None,
                "count": cnt.get("value") if isinstance(cnt, dict) else None,
                "address_space": node.get("address_space"),
                "access": node.get("access_qualifier"),
                "unused": node.get("unused"),
            })
        if nt and nt.endswith("Ret"):
            returns.append({"node_type": nt, "name": node.get("name"), "type_name": node.get("type_name")})
    return {"function": function, "args": args, "returns": returns, "arg_counts": counts}


def parse_m12_bindings(msl: Path) -> dict[str, Any]:
    if not msl.exists():
        return {"exists": False, "bindings": [], "manifest": []}
    text = msl.read_text(errors="replace")
    bindings = []
    for m in re.finditer(r"([A-Za-z0-9_:<> ,*&]+?)\s+(\w+)\s*\[\[(buffer|texture|sampler)\((\d+)\)\]\]", text):
        bindings.append({"decl": " ".join(m.group(1).split()), "name": m.group(2), "kind": m.group(3), "slot": int(m.group(4))})
    manifest = [line.strip()[3:] for line in text.splitlines() if line.startswith("// ") and ("metalsharp.binding_manifest" in line or line.startswith("// direct_") or line.startswith("// range "))]
    fn = None
    m = re.search(r"\b(vertex|fragment|kernel)\s+[^\n]+\s+(\w+)\s*\(", text)
    if m:
        fn = {"stage": m.group(1), "name": m.group(2)}
    return {"exists": True, "function": fn, "bindings": bindings, "manifest": manifest}


def main() -> int:
    args = parse_args()
    pack = json.loads(args.oracle_pack.read_text())
    lines = ["# D3DMetal oracle pack vs M12 bindings", "", f"- pack: `{args.oracle_pack}`", f"- m12 cache: `{args.m12_cache_dir}`", f"- records: `{pack['record_count']}`", ""]
    rows = []
    details = []
    for rec in pack["records"]:
        stem = (rec.get("m12_name") or "").removesuffix(".dxbc")
        m12 = parse_m12_bindings(args.m12_cache_dir / f"{stem}.msl")
        refl_path = None
        for item in (rec.get("objdump") or {}).values():
            if isinstance(item, dict) and item.get("stdout", "").endswith(".reflection.txt"):
                refl_path = Path(item["stdout"])
                break
        refl = summarize_reflection(extract_reflection_json(refl_path) if refl_path and refl_path.exists() else None)
        d3d_args = refl["args"]
        active_d3d_args = [a for a in d3d_args if a.get("unused") is not True]
        m12_bindings = m12["bindings"]
        rows.append((stem, rec.get("shader_kind"), ",".join(rec.get("function_names") or []), refl.get("function"), len(active_d3d_args), len(d3d_args), len(m12_bindings)))
        details.append((stem, rec, refl, m12, active_d3d_args))
    lines.append("| M12 shader | Kind | D3DMetal function | Reflection function | Active/total D3DMetal args | M12 bindings |")
    lines.append("|---|---|---|---|---:|---:|")
    for stem, kind, funcs, rfn, active, total, m12_count in rows:
        lines.append(f"| `{stem}` | `{kind}` | `{funcs}` | `{rfn}` | {active}/{total} | {m12_count} |")
    for stem, rec, refl, m12, active in details:
        lines.extend(["", f"## {stem}", "", f"- D3DMetal metallib: `{rec.get('oracle_metallib')}`", f"- D3DMetal function: `{refl.get('function')}`", f"- M12 function: `{(m12.get('function') or {}).get('name')}`", "", "### Active D3DMetal reflection args", ""])
        for a in active:
            lines.append(f"- {a['node_type']} loc={a['location']} count={a['count']} name=`{a['name']}` type=`{a['type_name']}` access=`{a['access']}` addr=`{a['address_space']}`")
        lines.extend(["", "### M12 MSL bindings", ""])
        for b in m12.get("bindings") or []:
            lines.append(f"- {b['kind']}({b['slot']}) `{b['name']}`: `{b['decl']}`")
        if m12.get("manifest"):
            lines.extend(["", "### M12 binding manifest comments", ""])
            for line in m12["manifest"]:
                lines.append(f"- {line}")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines) + "\n")
    print(args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
