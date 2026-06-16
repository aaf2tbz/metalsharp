#!/usr/bin/env python3
"""Analyze an M12 performance run directory produced by m12-performance-run.sh."""
from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path


def load_json(path: Path) -> dict:
    try:
        return json.loads(path.read_text())
    except Exception:
        return {}


def summarize_process(path: Path) -> dict:
    if not path.exists():
        return {}
    rows = list(csv.DictReader(path.open()))
    cpus, rss, threads = [], [], []
    for row in rows:
        try:
            if row.get("cpu_percent") not in (None, ""):
                cpus.append(float(row["cpu_percent"]))
            if row.get("rss_bytes") not in (None, ""):
                rss.append(int(float(row["rss_bytes"])))
            if row.get("threads") not in (None, ""):
                threads.append(int(float(row["threads"])))
        except ValueError:
            continue
    def stats(values):
        if not values:
            return {"max": None, "avg": None}
        return {"max": max(values), "avg": sum(values) / len(values)}
    return {
        "samples": len(rows),
        "cpu_percent": stats(cpus),
        "rss_bytes": stats(rss),
        "threads": stats(threads),
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("run_dir", type=Path)
    args = ap.parse_args()
    run = args.run_dir
    bounded = load_json(run / "bounded-summary.json")
    perf = load_json(run / "perf-run.json")
    process = summarize_process(run / "process-samples.csv")
    metrics = bounded.get("metrics", {})
    counts = bounded.get("counts", {})
    pressure = {
        "pso_compiles_total": metrics.get("graphics_pso_compiled", 0) + metrics.get("compute_pso_compiled", 0),
        "new_shader_artifacts": counts.get("new_dxbc", 0) + counts.get("new_msl", 0) + counts.get("new_dxil_reports", 0),
        "new_pso_artifacts": counts.get("new_pso_render", 0) + counts.get("new_pso_compute", 0),
        "failures_total": metrics.get("render_pso_failed", 0) + metrics.get("compute_pso_failed", 0) + metrics.get("dxil_msl_compile_failed", 0) + metrics.get("unsafe_draw_skips", 0),
    }
    summary = {
        "schema": "metalsharp.m12.perf-analysis.v1",
        "run_dir": str(run),
        "profile": perf.get("profile"),
        "scenario": perf.get("scenario"),
        "bounded_summary": str(run / "bounded-summary.json"),
        "metrics": metrics,
        "counts": counts,
        "process": process,
        "pressure": pressure,
    }
    (run / "perf-analysis.json").write_text(json.dumps(summary, indent=2) + "\n")
    md = [
        f"# M12 perf analysis: {perf.get('profile', 'unknown')} / {perf.get('scenario', 'unknown')}",
        "",
        "## Correctness buckets",
        f"- drawn/present: `{metrics.get('drawn_present_count', 0)}/{metrics.get('present_count', 0)}`",
        f"- render_pso_failed: `{metrics.get('render_pso_failed', 0)}`",
        f"- dxil_msl_compile_failed: `{metrics.get('dxil_msl_compile_failed', 0)}`",
        f"- vertex_descriptor_missing: `{metrics.get('vertex_descriptor_missing', 0)}`",
        f"- vs_ps_varying_mismatch: `{metrics.get('vs_ps_varying_mismatch', 0)}`",
        f"- unsafe_draw_skips: `{metrics.get('unsafe_draw_skips', 0)}`",
        "",
        "## Pressure summary",
        f"- PSO compiles total: `{pressure['pso_compiles_total']}`",
        f"- new PSO artifacts: `{pressure['new_pso_artifacts']}`",
        f"- new shader artifacts: `{pressure['new_shader_artifacts']}`",
        f"- total failure-like buckets: `{pressure['failures_total']}`",
        "",
        "## Process sampler",
        f"- samples: `{process.get('samples', 0)}`",
        f"- max CPU %: `{(process.get('cpu_percent') or {}).get('max')}`",
        f"- avg CPU %: `{(process.get('cpu_percent') or {}).get('avg')}`",
        f"- max RSS bytes: `{(process.get('rss_bytes') or {}).get('max')}`",
        f"- max threads: `{(process.get('threads') or {}).get('max')}`",
        "",
    ]
    (run / "perf-analysis.md").write_text("\n".join(md))
    print(run / "perf-analysis.md")
    print(run / "perf-analysis.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
