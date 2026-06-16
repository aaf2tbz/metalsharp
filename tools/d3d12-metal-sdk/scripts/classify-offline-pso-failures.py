#!/usr/bin/env python3
"""Classify offline Metal PSO factory results into stable repair buckets.

The offline PSO factory intentionally separates real Metal PSO failures from
incomplete/offline-incompatible capture rows. This classifier makes that split
explicit so runtime work can target one bucket at a time without treating
missing artifacts as rendering regressions.
"""

from __future__ import annotations

import argparse
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


def bucket_for(row: dict[str, Any]) -> str:
    status = str(row.get("status") or "")
    error = str(row.get("error") or row.get("message") or "")
    if row.get("ok") and not row.get("skipped"):
        return "ok"
    if status == "incomplete_capture_skipped" or "metallib not readable" in error:
        return "incomplete_capture"
    if status == "zero_vertex_output_skipped" or "vertex shader's return type is void" in error:
        return "zero_vertex_output"
    if "cannot be read using" in error and "Vertex attribute" in error:
        return "vertex_attribute_format_mismatch"
    if "missing from the vertex descriptor" in error:
        return "vertex_attribute_missing"
    if "mismatching vertex shader output" in error or "Fragment input" in error:
        return "vs_ps_varying_mismatch"
    if "no vertex descriptor was set" in error:
        return "missing_vertex_descriptor"
    if "depthAttachmentPixelFormat" in error or "shader writes to depth" in error:
        return "depth_output_without_attachment"
    if row.get("skipped"):
        return f"skipped:{status or 'unknown'}"
    if not row.get("ok"):
        return f"failure:{status or 'unknown'}"
    return "unknown"


def load_manifest_pipeline_index(corpus: Path) -> dict[str, dict[str, Any]]:
    index: dict[str, dict[str, Any]] = {}
    if not corpus.exists():
        return index
    for path in corpus.glob("pso-*.json"):
        try:
            data = json.loads(path.read_text())
        except Exception:
            continue
        for pipeline in data.get("pipelines") or []:
            name = pipeline.get("name")
            if isinstance(name, str):
                entry = dict(pipeline)
                entry["manifest_path"] = str(path)
                index[name] = entry
    return index


def summarize(path: Path, corpus: Path | None) -> dict[str, Any]:
    data = json.loads(path.read_text())
    pipelines = data.get("pipelines") or []
    manifest_index = load_manifest_pipeline_index(corpus) if corpus else {}
    counts: Counter[str] = Counter()
    examples: dict[str, list[dict[str, Any]]] = defaultdict(list)
    shader_hashes: dict[str, Counter[str]] = defaultdict(Counter)

    for row in pipelines:
        if not isinstance(row, dict):
            continue
        bucket = bucket_for(row)
        counts[bucket] += 1
        if len(examples[bucket]) < 10:
            examples[bucket].append(row)
        name = row.get("name")
        manifest = manifest_index.get(name) if isinstance(name, str) else None
        if manifest:
            d3d12 = manifest.get("d3d12") or {}
            for key in ("vs_hash", "ps_hash", "gs_hash"):
                value = d3d12.get(key)
                if isinstance(value, str) and value != "0000000000000000":
                    shader_hashes[bucket][f"{key}:{value}"] += 1

    return {
        "schema": "metalsharp.d3d12-metal.offline-pso-classification.v1",
        "input": str(path),
        "corpus": str(corpus) if corpus else None,
        "profile": data.get("profile"),
        "ok": data.get("ok"),
        "pipeline_count": data.get("pipeline_count", len(pipelines)),
        "failure_count": data.get("failure_count"),
        "skipped_count": data.get("skipped_count"),
        "buckets": [
            {
                "bucket": bucket,
                "count": count,
                "top_shader_hashes": [
                    {"shader": shader, "count": n}
                    for shader, n in shader_hashes[bucket].most_common(20)
                ],
                "examples": examples[bucket],
            }
            for bucket, count in counts.most_common()
        ],
    }


def write_markdown(summary: dict[str, Any], path: Path) -> None:
    lines = [
        f"# Offline PSO classification: {summary.get('profile') or ''}",
        "",
        f"- input: `{summary['input']}`",
        f"- corpus: `{summary.get('corpus')}`",
        f"- ok: `{summary.get('ok')}`",
        f"- pipeline_count: `{summary.get('pipeline_count')}`",
        f"- failure_count: `{summary.get('failure_count')}`",
        f"- skipped_count: `{summary.get('skipped_count')}`",
        "",
        "## Buckets",
        "",
        "| bucket | count |",
        "|---|---:|",
    ]
    for bucket in summary["buckets"]:
        lines.append(f"| `{bucket['bucket']}` | {bucket['count']} |")
    for bucket in summary["buckets"]:
        lines += ["", f"## `{bucket['bucket']}`", "", "### Top shader hashes"]
        for shader in bucket.get("top_shader_hashes", [])[:10]:
            lines.append(f"- `{shader['shader']}`: {shader['count']}")
        lines += ["", "### Examples"]
        for example in bucket.get("examples", [])[:5]:
            msg = example.get("error") or example.get("message") or example.get("status")
            lines.append(f"- `{example.get('name')}` `{example.get('status')}` — {msg}")
    path.write_text("\n".join(lines) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("result", type=Path, help="offline-pso-factory JSON result")
    parser.add_argument("--corpus", type=Path, default=None)
    parser.add_argument("--out", type=Path, default=None)
    args = parser.parse_args()

    summary = summarize(args.result, args.corpus)
    out = args.out or args.result.with_name(args.result.stem + "-classification.json")
    out.write_text(json.dumps(summary, indent=2) + "\n")
    write_markdown(summary, out.with_suffix(".md"))
    print(out)
    print(out.with_suffix(".md"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
