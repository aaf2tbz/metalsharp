#!/usr/bin/env python3
"""Phase 3.5: audit M12 render PSO vertex descriptor evidence.

Compares D3D12 input-layout manifests with generated MSL attribute/stage_in hints.
Read-only for shader caches.
"""
from __future__ import annotations

import argparse
import collections
import json
import re
from pathlib import Path
from typing import Any

HASH_RE = re.compile(r"^[0-9a-fA-F]{16}$")
ATTR_RE = re.compile(r"\[\[\s*attribute\((\d+)\)\s*\]\]")
STAGE_IN_RE = re.compile(r"\[\[\s*stage_in\s*\]\]")

DEFAULT_CORPORA = [
    ("elden-ring-live", Path.home() / ".metalsharp/shader-cache/m12/1245620"),
    ("subnautica-2-live", Path.home() / ".metalsharp/shader-cache/m12/1962700"),
    ("armored-core-vi-live", Path.home() / ".metalsharp/shader-cache/m12/1888160"),
    ("elden-ring-scratch", Path("/Volumes/AverySSD/MetalSharp-M12-CorpusLab/elden-ring-scratch/stable-20260615-192733")),
]


def read_text(path: Path, limit: int = 1_000_000) -> str:
    try:
        return path.read_bytes()[:limit].decode(errors="replace")
    except OSError:
        return ""


def load_json(path: Path) -> dict[str, Any] | None:
    try:
        data = json.loads(path.read_text(errors="replace"))
        return data if isinstance(data, dict) else None
    except Exception:
        return None


def msl_info(path: Path) -> dict[str, Any]:
    text = read_text(path)
    attrs = sorted({int(m.group(1)) for m in ATTR_RE.finditer(text)})
    return {
        "exists": path.exists(),
        "attributes": attrs,
        "attribute_count": len(attrs),
        "uses_stage_in": bool(STAGE_IN_RE.search(text)),
    }


def build_msl_index(root: Path) -> dict[str, Path]:
    index: dict[str, Path] = {}
    if not root.exists():
        return index
    for p in root.rglob("*.msl"):
        first = p.name.split(".", 1)[0]
        if HASH_RE.match(first):
            index.setdefault(first.lower(), p)
    return index


def iter_pipelines(root: Path):
    if not root.exists():
        return
    for path in sorted(root.rglob("pso-render-*.json")):
        data = load_json(path)
        if not data:
            continue
        pipes = data.get("pipelines")
        if not isinstance(pipes, list):
            pipes = [data]
        for p in pipes:
            if isinstance(p, dict):
                yield path, p


def audit_pipeline(root: Path, path: Path, p: dict[str, Any], msl_index: dict[str, Path]) -> dict[str, Any]:
    d3d12 = p.get("d3d12") if isinstance(p.get("d3d12"), dict) else {}
    layout = p.get("input_layout") if isinstance(p.get("input_layout"), dict) else {}
    elements = layout.get("elements") if isinstance(layout.get("elements"), list) else []
    vs_hash = str(d3d12.get("vs_hash") or "").lower()
    ps_hash = str(d3d12.get("ps_hash") or "").lower()
    vs_msl = msl_index.get(vs_hash) if HASH_RE.match(vs_hash) and vs_hash != "0000000000000000" else None
    info = msl_info(vs_msl) if vs_msl else {"exists": False, "attributes": [], "attribute_count": 0, "uses_stage_in": False}

    expected_registers = sorted({int(e.get("register")) for e in elements if isinstance(e, dict) and isinstance(e.get("register"), int) and not e.get("system_value")})
    expected_count = len(expected_registers)
    observed_attrs = info.get("attributes", [])
    categories: list[str] = []
    if expected_count and not info.get("exists"):
        categories.append("missing_vertex_msl")
    if expected_count and info.get("uses_stage_in") and not observed_attrs:
        categories.append("stage_in_without_attribute_regex_match")
    if observed_attrs and expected_registers:
        missing = sorted(set(expected_registers) - set(observed_attrs))
        extra = sorted(set(observed_attrs) - set(expected_registers))
        if missing:
            categories.append("missing_attribute")
        if extra:
            categories.append("extra_attribute")
    else:
        missing = expected_registers if expected_count and info.get("exists") and info.get("uses_stage_in") else []
        extra = []

    slot_strides: dict[str, int] = {}
    slot_classes: dict[str, set[str]] = collections.defaultdict(set)
    offset_issues = []
    step_issues = []
    for e in elements:
        if not isinstance(e, dict):
            continue
        slot = str(e.get("slot"))
        slot_classes[slot].add(str(e.get("class") or e.get("input_slot_class")))
        offset = e.get("offset")
        if not isinstance(offset, int) or offset < 0:
            offset_issues.append(e)
        if (e.get("class") == "per_instance" or e.get("input_slot_class") == 1) and int(e.get("step_rate") or 0) <= 0:
            step_issues.append(e)
    if offset_issues:
        categories.append("wrong_or_missing_offset")
    if step_issues:
        categories.append("wrong_step_rate")

    if not categories:
        categories.append("ok_or_vertex_pulling")

    return {
        "manifest": str(path),
        "name": p.get("name"),
        "pso_hash": path.stem.replace("pso-render-", ""),
        "vs_hash": vs_hash,
        "ps_hash": ps_hash,
        "input_element_count": len(elements),
        "expected_registers": expected_registers,
        "vs_msl": str(vs_msl) if vs_msl else None,
        "vs_msl_exists": info.get("exists"),
        "vs_uses_stage_in": info.get("uses_stage_in"),
        "observed_msl_attributes": observed_attrs,
        "missing_attributes": missing,
        "extra_attributes": extra,
        "slot_mask": layout.get("slot_mask"),
        "slot_classes": {k: sorted(v) for k, v in slot_classes.items()},
        "color_formats": p.get("color_formats"),
        "depth_format": p.get("depth_format"),
        "stencil_format": p.get("stencil_format"),
        "sample_count": p.get("sample_count"),
        "categories": categories,
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", action="append", default=[], help="LABEL=PATH or PATH. Repeatable.")
    ap.add_argument("--results-dir", type=Path, default=Path("tools/d3d12-metal-sdk/results/phase3.5-apple-metal-docs/vertex-descriptors"))
    args = ap.parse_args()
    corpora = []
    if args.corpus:
        for raw in args.corpus:
            if "=" in raw:
                label, path = raw.split("=", 1)
            else:
                path = raw
                label = Path(path).name or "corpus"
            corpora.append((label, Path(path).expanduser()))
    else:
        corpora = DEFAULT_CORPORA

    corpus_reports = []
    all_rows = []
    for label, root in corpora:
        msl_index = build_msl_index(root)
        rows = [audit_pipeline(root, pso_path, pipe, msl_index) for pso_path, pipe in (iter_pipelines(root) or [])]
        counts = collections.Counter(c for r in rows for c in r["categories"])
        corpus_reports.append({
            "label": label,
            "root": str(root),
            "exists": root.exists(),
            "render_pso_count": len(rows),
            "category_counts": dict(counts),
            "rows": rows,
        })
        all_rows.extend((label, r) for r in rows)

    args.results_dir.mkdir(parents=True, exist_ok=True)
    report = {"schema": "metalsharp.m12.phase3_5.vertex-descriptor-audit.v1", "corpora": corpus_reports}
    (args.results_dir / "vertex-descriptors.json").write_text(json.dumps(report, indent=2) + "\n")

    md = ["# M12 Phase 3.5 vertex descriptor audit", "", "## Summary", "", "| corpus | exists | render PSOs | categories |", "|---|---:|---:|---|"]
    for cr in corpus_reports:
        cats = ", ".join(f"`{k}`={v}" for k, v in sorted(cr["category_counts"].items()))
        md.append(f"| `{cr['label']}` | `{cr['exists']}` | {cr['render_pso_count']} | {cats or '-'} |")
    md += ["", "## Non-OK examples"]
    for label, r in all_rows:
        if r["categories"] == ["ok_or_vertex_pulling"]:
            continue
        md.append(f"- `{label}` `{r['pso_hash']}` categories={','.join(r['categories'])} vs=`{r['vs_hash']}` elements={r['input_element_count']} missing={r['missing_attributes']} extra={r['extra_attributes']} manifest=`{r['manifest']}`")
        if len(md) > 180:
            break
    md += ["", "## Interpretation", "", "- `ok_or_vertex_pulling` means the manifest is internally sane and either no explicit `[[attribute(n)]]` markers were needed/found or the generated MSL appears compatible with the manifest.", "- Any future runtime `vertex_descriptor_missing` or `vs_ps_varying_mismatch` should be correlated with this report, reflected Metal attributes, and final `MTLVertexDescriptor` dumps.", ""]
    (args.results_dir / "vertex-descriptors.md").write_text("\n".join(md))
    print(args.results_dir / "vertex-descriptors.md")
    print(args.results_dir / "vertex-descriptors.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
