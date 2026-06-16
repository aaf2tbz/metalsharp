#!/usr/bin/env python3
"""Analyze M12 render PSO VS/PS varying mismatch failures from runtime logs.

This is read-only and intended for scratch/offline analysis. It consumes log lines
emitted by DXMT_D3D12_LOG_RENDER_PSO_FAILURE_KEYS=1 and maps shader hashes back
to a corpus root to compare Metal/DXIL reflection state.
"""

from __future__ import annotations

import argparse
import collections
import json
import re
from pathlib import Path
from typing import Any

FAILURE_RE = re.compile(
    r"Render PSO failure key pso=([0-9a-f]+) vs=([0-9a-f]+) ps=([0-9a-f]+) gs=([0-9a-f]+) "
    r"input_elements=(\d+) ia_slot_mask=0x([0-9a-f]+) uses_stage_in=(\d+) "
    r"reflected_descriptor=(\d+) error=(.*)"
)
FRAGMENT_INPUT_RE = re.compile(r"Fragment input\(s\) `([^`]*)`")
USER_ATTR_RE = re.compile(r"\[\[user\(([^)]+)\)\]\]")


def load_json(path: Path) -> dict[str, Any] | None:
    try:
        value = json.loads(path.read_text())
    except Exception:
        return None
    return value if isinstance(value, dict) else None


def parse_failures(log: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for line in log.read_text(errors="ignore").splitlines():
        m = FAILURE_RE.search(line)
        if not m:
            continue
        error = m.group(9)
        inputs_match = FRAGMENT_INPUT_RE.search(error)
        fragment_inputs = []
        if inputs_match:
            fragment_inputs = [part.strip().removeprefix("user(").removesuffix(")") for part in inputs_match.group(1).split(",")]
        rows.append(
            {
                "pso": m.group(1),
                "vs": m.group(2),
                "ps": m.group(3),
                "gs": m.group(4),
                "input_elements": int(m.group(5)),
                "ia_slot_mask": int(m.group(6), 16),
                "uses_stage_in": m.group(7) == "1",
                "reflected_descriptor": m.group(8) == "1",
                "error": error,
                "fragment_inputs": fragment_inputs,
            }
        )
    return rows


def state_items(reflection: dict[str, Any] | None, key: str) -> list[dict[str, Any]]:
    if not reflection:
        return []
    state = reflection.get("state")
    if not isinstance(state, dict):
        return []
    value = state.get(key)
    return value if isinstance(value, list) else []


def semantic_names(items: list[dict[str, Any]]) -> set[str]:
    out: set[str] = set()
    for item in items:
        name = item.get("name")
        if isinstance(name, str) and name:
            out.add(name.lower())
    return out


def indexed_semantic_names(items: list[dict[str, Any]]) -> set[str]:
    out: set[str] = set()
    for item in items:
        name = item.get("name")
        index = item.get("index")
        if isinstance(name, str) and name:
            out.add(name.lower())
        if isinstance(index, int):
            out.add(f"locn{index}")
    return out


def msl_user_attrs(path: Path) -> set[str]:
    if not path.exists():
        return set()
    text = path.read_text(errors="ignore")
    return {m.group(1).lower() for m in USER_ATTR_RE.finditer(text)}


def classify(row: dict[str, Any], corpus: Path) -> dict[str, Any]:
    vs_json = corpus / f"{row['vs']}.json"
    ps_json = corpus / f"{row['ps']}.json"
    vs_ref = load_json(vs_json)
    ps_ref = load_json(ps_json)
    vs_outputs = state_items(vs_ref, "vertex_outputs")
    ps_inputs = state_items(ps_ref, "fragment_inputs") or state_items(ps_ref, "pixel_inputs")
    vs_names = indexed_semantic_names(vs_outputs)
    ps_names = indexed_semantic_names(ps_inputs)
    requested = {str(x).lower() for x in row["fragment_inputs"]}
    vs_msl_attrs = msl_user_attrs(corpus / f"{row['vs']}.msl")
    ps_msl_attrs = msl_user_attrs(corpus / f"{row['ps']}.msl")
    return {
        **row,
        "vs_json_exists": vs_json.exists(),
        "ps_json_exists": ps_json.exists(),
        "vs_entry": vs_ref.get("EntryPoint") if vs_ref else None,
        "ps_entry": ps_ref.get("EntryPoint") if ps_ref else None,
        "vs_output_count": len(vs_outputs),
        "ps_input_count": len(ps_inputs),
        "vs_output_names": sorted(vs_names)[:64],
        "ps_input_names": sorted(ps_names)[:64],
        "requested_fragment_inputs": sorted(requested),
        "requested_missing_from_vs_reflection": sorted(requested - vs_names),
        "ps_inputs_missing_from_vs_reflection": sorted(ps_names - vs_names),
        "requested_missing_from_vs_msl": sorted(requested - vs_msl_attrs),
        "requested_present_in_ps_msl": sorted(requested & ps_msl_attrs),
        "vs_msl_user_attrs_sample": sorted(vs_msl_attrs)[:64],
        "ps_msl_user_attrs_sample": sorted(ps_msl_attrs)[:64],
    }


def markdown_report(rows: list[dict[str, Any]], classified: list[dict[str, Any]]) -> str:
    lines: list[str] = []
    lines.append("# M12 VS/PS varying failure analysis")
    lines.append("")
    lines.append(f"- failure_key_count: `{len(rows)}`")
    lines.append(f"- classified_count: `{len(classified)}`")
    lines.append("")
    for label, pred in [
        ("all", lambda r: True),
        ("reflected_descriptor", lambda r: r["reflected_descriptor"]),
        ("non_reflected_descriptor", lambda r: not r["reflected_descriptor"]),
    ]:
        subset = [r for r in classified if pred(r)]
        lines.append(f"## {label}")
        lines.append("")
        lines.append(f"- count: `{len(subset)}`")
        lines.append(f"- missing_vs_json: `{sum(not r['vs_json_exists'] for r in subset)}`")
        lines.append(f"- missing_ps_json: `{sum(not r['ps_json_exists'] for r in subset)}`")
        top_vs = collections.Counter(r["vs"] for r in subset).most_common(10)
        top_req = collections.Counter(tuple(r["requested_fragment_inputs"]) for r in subset).most_common(10)
        lines.append("- top_vs: " + ", ".join(f"`{h}`×{n}" for h, n in top_vs))
        lines.append("- top_requested_inputs:")
        for inputs, count in top_req:
            lines.append(f"  - `{','.join(inputs)}` × {count}")
        lines.append("")
    lines.append("## Representative reflected descriptor failures")
    lines.append("")
    for row in [r for r in classified if r["reflected_descriptor"]][:20]:
        lines.append(f"### pso `{row['pso']}`")
        lines.append(f"- vs: `{row['vs']}` entry=`{row['vs_entry']}` outputs={row['vs_output_count']}")
        lines.append(f"- ps: `{row['ps']}` entry=`{row['ps_entry']}` inputs={row['ps_input_count']}")
        lines.append(f"- requested: `{','.join(row['requested_fragment_inputs'])}`")
        lines.append(f"- missing_from_vs_reflection: `{','.join(row['requested_missing_from_vs_reflection'])}`")
        lines.append(f"- missing_from_vs_msl: `{','.join(row['requested_missing_from_vs_msl'])}`")
        lines.append(f"- vs_outputs: `{','.join(row['vs_output_names'][:24])}`")
        lines.append(f"- ps_inputs: `{','.join(row['ps_input_names'][:24])}`")
        lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--log", required=True, type=Path)
    parser.add_argument("--corpus", required=True, type=Path)
    parser.add_argument("--json", required=True, type=Path)
    parser.add_argument("--markdown", required=True, type=Path)
    args = parser.parse_args()

    rows = parse_failures(args.log)
    classified = [classify(row, args.corpus) for row in rows]
    output = {
        "schema": "metalsharp.m12.varying-failure-analysis.v1",
        "log": str(args.log),
        "corpus": str(args.corpus),
        "failure_key_count": len(rows),
        "classified": classified,
    }
    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps(output, indent=2, sort_keys=True) + "\n")
    args.markdown.write_text(markdown_report(rows, classified) + "\n")
    print(args.json)
    print(args.markdown)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
