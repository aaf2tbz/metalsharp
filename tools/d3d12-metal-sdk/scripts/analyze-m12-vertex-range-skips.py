#!/usr/bin/env python3
"""Analyze M12 vertex_range_oob unsafe draw skip logs.

Read-only. Consumes launch logs containing lines like:
  M12 skipping unsafe DrawIndexedInstanced reason=vertex_range_oob ...
"""

from __future__ import annotations

import argparse
import collections
import json
import re
from pathlib import Path
from typing import Any

KV_RE = re.compile(r"([A-Za-z0-9_]+)=([^\s]+)")
HEX_KEYS = {"pso", "gpu"}
INT_KEYS = {
    "slot", "table", "size", "stride", "required", "available", "elems",
    "inst", "start", "base", "start_inst", "indexed", "enc_open",
    "stage_in", "geom_mesh", "tess_fallback", "vs_args", "vs_cb",
    "vs_qwords", "vs_cb_bind", "vs_arg_bind", "ps_args", "ps_cb",
    "ps_qwords", "ps_cb_bind", "ps_arg_bind",
}


def parse_value(key: str, value: str) -> Any:
    if key in HEX_KEYS:
        try:
            return int(value, 16)
        except Exception:
            return value
    if key in INT_KEYS:
        try:
            return int(value, 10)
        except Exception:
            return value
    return value


def parse_log(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for line_no, line in enumerate(path.read_text(errors="ignore").splitlines(), 1):
        if "M12 skipping unsafe" not in line or "vertex_range_oob" not in line:
            continue
        row: dict[str, Any] = {"line": line_no, "raw": line}
        for key, value in KV_RE.findall(line):
            row[key] = parse_value(key, value)
        rows.append(row)
    return rows


def key(row: dict[str, Any], name: str) -> str:
    value = row.get(name, "")
    if isinstance(value, int) and name in HEX_KEYS:
        return f"0x{value:x}"
    return str(value)


def summarize(rows: list[dict[str, Any]]) -> dict[str, Any]:
    def top(field: str, n: int = 20):
        return [{"value": value, "count": count} for value, count in collections.Counter(key(r, field) for r in rows).most_common(n)]

    required_over_available = []
    for row in rows:
        req = row.get("required")
        avail = row.get("available")
        if isinstance(req, int) and isinstance(avail, int):
            required_over_available.append(req - avail)

    return {
        "skip_count": len(rows),
        "top_pso": top("pso"),
        "top_vs": top("vs"),
        "top_ps": top("ps"),
        "top_slot": top("slot"),
        "top_stride": top("stride"),
        "top_indexed": top("indexed"),
        "top_stage_in": top("stage_in"),
        "top_tess_fallback": top("tess_fallback"),
        "max_required_minus_available": max(required_over_available) if required_over_available else None,
        "min_required_minus_available": min(required_over_available) if required_over_available else None,
        "negative_base_count": sum(1 for r in rows if isinstance(r.get("base"), int) and r["base"] < 0),
        "large_start_count": sum(1 for r in rows if isinstance(r.get("start"), int) and r["start"] > 100000),
        "examples": rows[:20],
    }


def markdown(summary: dict[str, Any], log: Path) -> str:
    lines = ["# M12 vertex range skip analysis", "", f"- log: `{log}`", f"- skip_count: `{summary['skip_count']}`", ""]
    for title, field in [
        ("Top PSOs", "top_pso"),
        ("Top VS hashes", "top_vs"),
        ("Top PS hashes", "top_ps"),
        ("Top slots", "top_slot"),
        ("Top strides", "top_stride"),
        ("Indexed", "top_indexed"),
        ("Stage-in", "top_stage_in"),
        ("Tess fallback", "top_tess_fallback"),
    ]:
        lines.append(f"## {title}")
        lines.append("")
        for item in summary[field]:
            lines.append(f"- `{item['value']}` × {item['count']}")
        lines.append("")
    lines.append("## Range deltas")
    lines.append("")
    lines.append(f"- max_required_minus_available: `{summary['max_required_minus_available']}`")
    lines.append(f"- min_required_minus_available: `{summary['min_required_minus_available']}`")
    lines.append(f"- negative_base_count: `{summary['negative_base_count']}`")
    lines.append(f"- large_start_count: `{summary['large_start_count']}`")
    lines.append("")
    lines.append("## Examples")
    lines.append("")
    for row in summary["examples"][:10]:
        bits = []
        for k in ["pso", "vs", "ps", "slot", "size", "stride", "required", "available", "elems", "start", "base", "indexed", "stage_in", "tess_fallback"]:
            if k in row:
                v = key(row, k)
                bits.append(f"{k}={v}")
        lines.append("- " + " ".join(bits))
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--log", required=True, type=Path)
    parser.add_argument("--json", required=True, type=Path)
    parser.add_argument("--markdown", required=True, type=Path)
    args = parser.parse_args()

    rows = parse_log(args.log)
    summary = summarize(rows)
    output = {
        "schema": "metalsharp.m12.vertex-range-skip-analysis.v1",
        "log": str(args.log),
        "rows": rows,
        "summary": summary,
    }
    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps(output, indent=2, sort_keys=True) + "\n")
    args.markdown.write_text(markdown(summary, args.log))
    print(args.json)
    print(args.markdown)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
