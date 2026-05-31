#!/usr/bin/env python3
"""Audit captured D3D12 render PSO manifests for Phase 16B readiness."""

from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path


def load_first_json_object(path: Path) -> tuple[dict | None, str, str]:
    text = path.read_text(errors="replace")
    decoder = json.JSONDecoder()
    try:
        data, end = decoder.raw_decode(text)
    except json.JSONDecodeError as exc:
        return None, "", str(exc)
    return data, text[end:].strip(), ""


def active_color_formats(pipeline: dict) -> list[str]:
    color_formats = pipeline.get("color_formats")
    if not isinstance(color_formats, list):
        return []
    d3d12 = pipeline.get("d3d12") or {}
    rt_count = int(d3d12.get("num_render_targets") or 0)
    return [str(fmt) for fmt in color_formats[:rt_count]]


def audit_manifest(path: Path) -> tuple[dict, list[dict], list[dict]]:
    row = {
        "path": str(path),
        "name": path.stem,
        "type": "",
        "vs_hash": "",
        "ps_hash": "",
        "gs_hash": "",
        "num_render_targets": 0,
        "color_formats": [],
        "depth_format": "unknown",
        "stencil_format": "unknown",
        "sample_count": 0,
        "input_elements": 0,
        "has_pixel_shader": False,
        "has_geometry_shader": False,
        "uses_stage_in": False,
        "uses_geometry_mesh": False,
        "rasterization_enabled": False,
        "malformed_tail": False,
    }
    violations: list[dict] = []
    warnings: list[dict] = []

    data, malformed_tail, json_error = load_first_json_object(path)
    if json_error:
        violations.append({"manifest": str(path), "kind": "malformed-json", "detail": json_error})
        return row, violations, warnings
    if malformed_tail:
        row["malformed_tail"] = True
        warnings.append({"manifest": str(path), "kind": "malformed-tail", "detail": malformed_tail[:120]})

    pipelines = data.get("pipelines") if isinstance(data, dict) else None
    if not pipelines:
        violations.append({"manifest": str(path), "kind": "missing-pipeline", "detail": ""})
        return row, violations, warnings

    pipeline = pipelines[0]
    d3d12 = pipeline.get("d3d12") or {}
    metal = pipeline.get("metal") or {}
    row.update(
        {
            "name": pipeline.get("name", path.stem),
            "type": pipeline.get("type", ""),
            "vs_hash": d3d12.get("vs_hash", ""),
            "ps_hash": d3d12.get("ps_hash", ""),
            "gs_hash": d3d12.get("gs_hash", ""),
            "num_render_targets": int(d3d12.get("num_render_targets") or 0),
            "color_formats": [str(fmt) for fmt in pipeline.get("color_formats", [])],
            "depth_format": pipeline.get("depth_format", "unknown"),
            "stencil_format": pipeline.get("stencil_format", "unknown"),
            "sample_count": int(pipeline.get("sample_count") or 0),
            "input_elements": int(d3d12.get("input_elements") or 0),
            "has_pixel_shader": bool(d3d12.get("ps_bytes", 0)),
            "has_geometry_shader": bool(d3d12.get("gs_bytes", 0)),
            "uses_stage_in": bool(metal.get("uses_stage_in", False)),
            "uses_geometry_mesh": bool(metal.get("uses_geometry_mesh", False)),
            "rasterization_enabled": bool(metal.get("rasterization_enabled", False)),
        }
    )

    def add(kind: str, detail: str) -> None:
        violations.append({"manifest": str(path), "name": row["name"], "kind": kind, "detail": detail})

    def warn(kind: str, detail: str) -> None:
        warnings.append({"manifest": str(path), "name": row["name"], "kind": kind, "detail": detail})

    if row["type"] != "render":
        add("not-render-pso", f"type={row['type']}")

    if not pipeline.get("vertex"):
        add("missing-vertex-shader", "render PSO has no vertex shader object")
    if row["has_pixel_shader"] and not pipeline.get("fragment"):
        add("missing-fragment-shader", "D3D12 PS bytecode is present but manifest has no fragment shader")
    if not row["has_pixel_shader"] and pipeline.get("fragment"):
        warn("unexpected-fragment-shader", "manifest has a fragment shader but D3D12 PS byte count is zero")

    if row["has_geometry_shader"] or row["uses_geometry_mesh"]:
        add(
            "geometry-pso",
            f"gs_bytes_present={row['has_geometry_shader']} uses_geometry_mesh={row['uses_geometry_mesh']}",
        )

    active_colors = active_color_formats(pipeline)
    if row["num_render_targets"] > 8:
        add("too-many-render-targets", f"num_render_targets={row['num_render_targets']}")
    if len(active_colors) < row["num_render_targets"]:
        add(
            "missing-color-formats",
            f"num_render_targets={row['num_render_targets']} color_formats={len(row['color_formats'])}",
        )
    for index, fmt in enumerate(active_colors):
        if fmt in {"", "invalid", "unknown"}:
            add("invalid-active-color-format", f"rt[{index}]={fmt}")

    if row["num_render_targets"] == 0:
        non_invalid = [fmt for fmt in row["color_formats"] if fmt not in {"", "invalid", "unknown"}]
        if non_invalid:
            warn("color-format-with-zero-render-targets", ",".join(non_invalid[:8]))

    dsv_format = int(d3d12.get("dsv_format") or 0)
    has_depth_or_stencil = row["depth_format"] not in {"", "invalid", "unknown"} or row["stencil_format"] not in {
        "",
        "invalid",
        "unknown",
    }
    if dsv_format and not has_depth_or_stencil:
        add("missing-depth-stencil-format", f"dsv_format={dsv_format}")
    if not dsv_format and has_depth_or_stencil:
        warn(
            "depth-stencil-without-dsv",
            f"depth={row['depth_format']} stencil={row['stencil_format']}",
        )

    if row["sample_count"] < 1:
        add("invalid-sample-count", f"sample_count={row['sample_count']}")
    elif row["sample_count"] not in {1, 2, 4, 8, 16, 32, 64}:
        warn("unusual-sample-count", f"sample_count={row['sample_count']}")

    if row["input_elements"] > 0 and not row["uses_stage_in"]:
        warn("input-layout-without-stage-in", f"input_elements={row['input_elements']}")
    if row["num_render_targets"] > 0 and not row["rasterization_enabled"]:
        warn("render-targets-with-rasterization-disabled", f"num_render_targets={row['num_render_targets']}")

    return row, violations, warnings


def build_audit(corpus: Path) -> dict:
    rows = []
    violations = []
    warnings = []
    for path in sorted(corpus.glob("pso-render-*.json")):
        row, row_violations, row_warnings = audit_manifest(path)
        rows.append(row)
        violations.extend(row_violations)
        warnings.extend(row_warnings)

    color_tuples = Counter(tuple(row["color_formats"]) for row in rows)
    rt_counts = Counter(row["num_render_targets"] for row in rows)
    violation_counts = Counter(item["kind"] for item in violations)
    warning_counts = Counter(item["kind"] for item in warnings)

    return {
        "summary": {
            "render_pso_count": len(rows),
            "violation_count": len(violations),
            "warning_count": len(warnings),
            "render_target_counts": dict(sorted(rt_counts.items())),
            "top_color_format_tuples": [
                {"formats": list(formats), "count": count}
                for formats, count in color_tuples.most_common(20)
            ],
            "violation_counts": dict(sorted(violation_counts.items())),
            "warning_counts": dict(sorted(warning_counts.items())),
        },
        "violations": violations,
        "warnings": warnings,
        "rows": rows,
    }


def write_markdown(audit: dict, output: Path) -> None:
    summary = audit["summary"]
    lines = [
        "# Phase 16B Graphics PSO Manifest Audit",
        "",
        "Generated from captured `pso-render-*.json` manifests. This checks whether the recorded D3D12 graphics PSO state is structurally ready for Metal PSO creation.",
        "",
        "## Summary",
        "",
        f"- Render PSOs: {summary['render_pso_count']}",
        f"- Violations: {summary['violation_count']}",
        f"- Warnings: {summary['warning_count']}",
        "",
        "## Violation Counts",
        "",
    ]
    if summary["violation_counts"]:
        for kind, count in summary["violation_counts"].items():
            lines.append(f"- `{kind}`: {count}")
    else:
        lines.append("None.")

    lines.extend(["", "## Warning Counts", ""])
    if summary["warning_counts"]:
        for kind, count in summary["warning_counts"].items():
            lines.append(f"- `{kind}`: {count}")
    else:
        lines.append("None.")

    lines.extend(["", "## Render Target Counts", ""])
    for rt_count, count in summary["render_target_counts"].items():
        lines.append(f"- `{rt_count}` render targets: `{count}`")

    lines.extend(["", "## Top Color Format Tuples", ""])
    for entry in summary["top_color_format_tuples"]:
        label = ", ".join(entry["formats"]) if entry["formats"] else "none"
        lines.append(f"- `{label}`: `{entry['count']}`")

    lines.extend(["", "## First Violations", ""])
    if audit["violations"]:
        lines.extend(["| Manifest | Kind | Detail |", "|---|---|---|"])
        for violation in audit["violations"][:100]:
            lines.append(
                f"| `{Path(violation['manifest']).name}` | `{violation['kind']}` | "
                f"{violation.get('detail', '').replace('|', '\\|')} |"
            )
        if len(audit["violations"]) > 100:
            lines.append(f"| ... | ... | {len(audit['violations']) - 100} more omitted |")
    else:
        lines.append("None.")
    lines.append("")
    output.write_text("\n".join(lines))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", required=True, type=Path)
    parser.add_argument("--markdown", required=True, type=Path)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()

    audit = build_audit(args.corpus)
    args.markdown.parent.mkdir(parents=True, exist_ok=True)
    write_markdown(audit, args.markdown)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(audit, indent=2) + "\n")
    print(
        f"wrote {args.markdown} with {audit['summary']['render_pso_count']} render PSOs "
        f"and {audit['summary']['violation_count']} violations"
    )
    return 0 if audit["summary"]["violation_count"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
