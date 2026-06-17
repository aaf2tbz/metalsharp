#!/usr/bin/env python3
"""Analyze an M12 performance run directory produced by m12-performance-run.sh."""
from __future__ import annotations

import argparse
import csv
import json
import re
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


PSO_PRESSURE_RE = re.compile(r"PSO_PRESSURE\s+(?P<kind>\w+)\s+(?P<rest>.*)")
KV_RE = re.compile(r"(?P<key>[A-Za-z0-9_]+)=(?P<value>[^\s]+)")


def parse_int(value: str):
    try:
        return int(value, 0)
    except Exception:
        return None


def summarize_pso_pressure(run: Path, bounded: dict) -> dict:
    summary = {
        "graphics_requests": 0,
        "graphics_unique": 0,
        "graphics_repeated": 0,
        "compute_requests": 0,
        "compute_unique": 0,
        "compute_repeated": 0,
        "metal_render_creates": 0,
        "metal_compute_creates": 0,
        "render_pipeline_cache_hits": 0,
        "render_pipeline_cache_misses": 0,
        "compute_pipeline_cache_hits": 0,
        "compute_pipeline_cache_misses": 0,
        "shader_memory_cache_hits": 0,
        "shader_memory_cache_misses": 0,
        "shader_metallib_cache_hits": 0,
        "shader_metallib_cache_misses": 0,
        "compile_waits": 0,
        "compile_wait_ns_total": 0,
        "source_files": [],
    }
    files = [p for p in run.rglob("*.log") if p.is_file()]
    files += [p for p in run.rglob("*.txt") if p.is_file() and "launch" in p.name.lower()]
    for src in bounded.get("log_sources", []):
        path = Path(src.get("path", ""))
        if path.is_file():
            files.append(path)
    launch_log = Path(bounded.get("launch_log", ""))
    if launch_log.is_file():
        files.append(launch_log)
    seen = set()
    for path in files:
        if path in seen:
            continue
        seen.add(path)
        try:
            text = path.read_text(errors="ignore")
        except Exception:
            continue
        matched = False
        for line in text.splitlines():
            m = PSO_PRESSURE_RE.search(line)
            if not m:
                continue
            matched = True
            kind = m.group("kind")
            kv = {km.group("key"): km.group("value") for km in KV_RE.finditer(m.group("rest"))}
            if kind == "graphics_request":
                summary["graphics_requests"] = max(summary["graphics_requests"], parse_int(kv.get("total", "0")) or 0)
                summary["graphics_unique"] = max(summary["graphics_unique"], parse_int(kv.get("unique", "0")) or 0)
                summary["graphics_repeated"] = max(summary["graphics_repeated"], parse_int(kv.get("repeated", "0")) or 0)
            elif kind == "compute_request":
                summary["compute_requests"] = max(summary["compute_requests"], parse_int(kv.get("total", "0")) or 0)
                summary["compute_unique"] = max(summary["compute_unique"], parse_int(kv.get("unique", "0")) or 0)
                summary["compute_repeated"] = max(summary["compute_repeated"], parse_int(kv.get("repeated", "0")) or 0)
            elif kind == "metal_render_create":
                summary["metal_render_creates"] = max(summary["metal_render_creates"], parse_int(kv.get("total", "0")) or 0)
            elif kind == "metal_compute_create":
                summary["metal_compute_creates"] = max(summary["metal_compute_creates"], parse_int(kv.get("total", "0")) or 0)
            elif kind == "render_pipeline_cache_hit":
                summary["render_pipeline_cache_hits"] = max(summary["render_pipeline_cache_hits"], parse_int(kv.get("hits", "0")) or 0)
            elif kind == "render_pipeline_cache_miss":
                summary["render_pipeline_cache_misses"] = max(summary["render_pipeline_cache_misses"], parse_int(kv.get("misses", "0")) or 0)
            elif kind == "compute_pipeline_cache_hit":
                summary["compute_pipeline_cache_hits"] = max(summary["compute_pipeline_cache_hits"], parse_int(kv.get("hits", "0")) or 0)
            elif kind == "compute_pipeline_cache_miss":
                summary["compute_pipeline_cache_misses"] = max(summary["compute_pipeline_cache_misses"], parse_int(kv.get("misses", "0")) or 0)
            elif kind == "compile_wait":
                summary["compile_waits"] = max(summary["compile_waits"], parse_int(kv.get("waits", "0")) or 0)
                summary["compile_wait_ns_total"] = max(
                    summary["compile_wait_ns_total"], parse_int(kv.get("total_wait_ns", "0")) or 0
                )
            if "hits" in kv:
                summary["shader_memory_cache_hits"] = max(summary["shader_memory_cache_hits"], parse_int(kv["hits"]) or 0)
            if "misses" in kv:
                summary["shader_memory_cache_misses"] = max(summary["shader_memory_cache_misses"], parse_int(kv["misses"]) or 0)
            if "metallib_hits" in kv:
                summary["shader_metallib_cache_hits"] = max(summary["shader_metallib_cache_hits"], parse_int(kv["metallib_hits"]) or 0)
            if "metallib_misses" in kv:
                summary["shader_metallib_cache_misses"] = max(summary["shader_metallib_cache_misses"], parse_int(kv["metallib_misses"]) or 0)
        if matched:
            summary["source_files"].append(str(path))
    return summary


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("run_dir", type=Path)
    args = ap.parse_args()
    run = args.run_dir
    bounded = load_json(run / "bounded-summary.json")
    perf = load_json(run / "perf-run.json")
    process = summarize_process(run / "process-samples.csv")
    pso_pressure = summarize_pso_pressure(run, bounded)
    metrics = bounded.get("metrics", {})
    counts = bounded.get("counts", {})
    pressure = {
        "pso_compiles_total": metrics.get("graphics_pso_compiled", 0) + metrics.get("compute_pso_compiled", 0),
        "new_shader_artifacts": counts.get("new_dxbc", 0) + counts.get("new_msl", 0) + counts.get("new_dxil_reports", 0),
        "new_pso_artifacts": counts.get("new_pso_render", 0) + counts.get("new_pso_compute", 0),
        "pso_request_repeats": pso_pressure.get("graphics_repeated", 0) + pso_pressure.get("compute_repeated", 0),
        "metal_pipeline_creates": pso_pressure.get("metal_render_creates", 0) + pso_pressure.get("metal_compute_creates", 0),
        "metal_pipeline_cache_hits": pso_pressure.get("render_pipeline_cache_hits", 0) + pso_pressure.get("compute_pipeline_cache_hits", 0),
        "metal_pipeline_cache_misses": pso_pressure.get("render_pipeline_cache_misses", 0) + pso_pressure.get("compute_pipeline_cache_misses", 0),
        "compile_wait_ms_total": (pso_pressure.get("compile_wait_ns_total", 0) or 0) / 1_000_000,
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
        "pso_pressure": pso_pressure,
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
        f"- graphics PSO requests unique/repeated/total: `{pso_pressure['graphics_unique']}/{pso_pressure['graphics_repeated']}/{pso_pressure['graphics_requests']}`",
        f"- compute PSO requests unique/repeated/total: `{pso_pressure['compute_unique']}/{pso_pressure['compute_repeated']}/{pso_pressure['compute_requests']}`",
        f"- Metal pipeline creates render/compute: `{pso_pressure['metal_render_creates']}/{pso_pressure['metal_compute_creates']}`",
        f"- Metal pipeline cache hits/misses render: `{pso_pressure['render_pipeline_cache_hits']}/{pso_pressure['render_pipeline_cache_misses']}`",
        f"- Metal pipeline cache hits/misses compute: `{pso_pressure['compute_pipeline_cache_hits']}/{pso_pressure['compute_pipeline_cache_misses']}`",
        f"- shader cache hits/misses memory: `{pso_pressure['shader_memory_cache_hits']}/{pso_pressure['shader_memory_cache_misses']}`",
        f"- shader cache hits/misses metallib: `{pso_pressure['shader_metallib_cache_hits']}/{pso_pressure['shader_metallib_cache_misses']}`",
        f"- compile waits: `{pso_pressure['compile_waits']}` total_ms=`{pressure['compile_wait_ms_total']}`",
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
