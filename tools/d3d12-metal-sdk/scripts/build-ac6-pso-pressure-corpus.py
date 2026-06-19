#!/usr/bin/env python3
"""Build an offline AC6 PSO/command-buffer pressure corpus from live M12 cache.

This is intentionally read-only with respect to ~/.metalsharp.  It copies the
cached PSO manifests and referenced shader artifacts into a timestamped /tmp
corpus so the pressure harness can run without mutating live game caches.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import shutil
from pathlib import Path
from typing import Any


def now_stamp() -> str:
    return dt.datetime.now().strftime("%Y%m%d-%H%M%S")


def load_json(path: Path) -> dict[str, Any] | None:
    try:
        data = json.loads(path.read_text())
    except Exception:
        return None
    return data if isinstance(data, dict) else None


def shader_hash(shader: Any) -> str | None:
    if not isinstance(shader, dict):
        return None
    value = shader.get("hash")
    if isinstance(value, str) and value:
        return value.lower()
    metallib = shader.get("metallib")
    if isinstance(metallib, str) and metallib:
        stem = Path(metallib).stem
        if stem:
            return stem.lower()
    return None


def copy_if_present(src: Path, dst: Path) -> bool:
    if not src.exists() or not src.is_file():
        return False
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    return True


def remap_shader(shader: Any, cache_dir: Path, shader_dir: Path, copied: set[Path], missing: list[str]) -> Any:
    if not isinstance(shader, dict):
        return shader
    out = dict(shader)
    h = shader_hash(shader)
    if not h:
        return out
    out["original_metallib"] = shader.get("metallib", "")
    for stale_key in ("msl", "metallib", "reflection", "module", "dxil_report"):
        out.pop(stale_key, None)
    out["hash"] = h
    for ext, key in ((".msl", "msl"), (".metallib", "metallib"), (".json", "reflection"), (".module.txt", "module"), (".dxil_report.txt", "dxil_report")):
        src = cache_dir / f"{h}{ext}"
        dst = shader_dir / f"{h}{ext}"
        if copy_if_present(src, dst):
            copied.add(dst)
            out[key] = str(dst)
        elif ext in (".msl", ".metallib"):
            # Keep the missing source visible.  MSL is enough for the new M12Core path;
            # metallib may be absent in current AC6 captures.
            if ext == ".msl" and not (cache_dir / f"{h}.metallib").exists():
                missing.append(f"{h}{ext}")
    return out


def remap_pipeline(pipeline: dict[str, Any], cache_dir: Path, shader_dir: Path, copied: set[Path], missing: list[str]) -> dict[str, Any]:
    out = dict(pipeline)
    for key in ("shader", "vertex", "fragment"):
        if key in out:
            out[key] = remap_shader(out[key], cache_dir, shader_dir, copied, missing)
    for key in ("vertex_linked_functions", "fragment_linked_functions"):
        value = out.get(key)
        if isinstance(value, list):
            out[key] = [remap_shader(item, cache_dir, shader_dir, copied, missing) for item in value]
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description="Copy AC6 M12 PSO/shader cache entries into an offline pressure corpus.")
    parser.add_argument("--appid", default="1888160", help="Steam appid cache directory to inventory (default: AC6 1888160).")
    parser.add_argument("--cache-dir", type=Path, help="Override M12 shader cache directory.")
    parser.add_argument("--output-dir", type=Path, help="Corpus output directory; default is /tmp/m12-ac6-pso-pressure-corpus-<timestamp>.")
    parser.add_argument("--limit", type=int, default=0, help="Limit copied PSO manifest count for smoke testing.")
    parser.add_argument("--include-compute", action="store_true", default=True, help="Include pso-compute manifests (default: true).")
    parser.add_argument("--render-only", action="store_true", help="Only include pso-render manifests.")
    parser.add_argument("--keep-incomplete", action="store_true", help="Keep pipelines whose referenced shader source/metallib is missing. By default they are inventoried but omitted from the runnable harness manifest.")
    args = parser.parse_args()

    cache_dir = args.cache_dir or (Path.home() / ".metalsharp" / "shader-cache" / "m12" / args.appid)
    if not cache_dir.exists():
        raise SystemExit(f"cache directory not found: {cache_dir}")

    out_dir = args.output_dir or Path(f"/tmp/m12-ac6-pso-pressure-corpus-{now_stamp()}")
    shader_dir = out_dir / "shaders"
    pso_dir = out_dir / "pso-render"
    compute_dir = out_dir / "pso-compute"
    out_dir.mkdir(parents=True, exist_ok=True)
    shader_dir.mkdir(parents=True, exist_ok=True)
    pso_dir.mkdir(parents=True, exist_ok=True)
    if not args.render_only:
        compute_dir.mkdir(parents=True, exist_ok=True)

    patterns = ["pso-render-*.json"]
    if not args.render_only and args.include_compute:
        patterns.append("pso-compute-*.json")

    manifest_paths: list[Path] = []
    for pattern in patterns:
        manifest_paths.extend(sorted(cache_dir.glob(pattern), key=lambda p: p.stat().st_mtime, reverse=True))
    if args.limit > 0:
        manifest_paths = manifest_paths[: args.limit]

    pipelines: list[dict[str, Any]] = []
    source_manifests: list[str] = []
    copied_shaders: set[Path] = set()
    missing_shader_inputs: list[str] = []
    type_counts: dict[str, int] = {}
    incomplete_pipelines: list[dict[str, Any]] = []

    def pipeline_missing_inputs(pipeline: dict[str, Any]) -> list[str]:
        missing: list[str] = []
        for key in ("shader", "vertex", "fragment"):
            shader = pipeline.get(key)
            if isinstance(shader, dict) and not shader.get("msl") and not shader.get("metallib"):
                missing.append(str(shader.get("hash") or key))
        return missing

    for src in manifest_paths:
        data = load_json(src)
        if not data or not isinstance(data.get("pipelines"), list):
            continue
        dst_dir = compute_dir if src.name.startswith("pso-compute-") else pso_dir
        dst = dst_dir / src.name
        shutil.copy2(src, dst)
        source_manifests.append(str(dst))
        for pipeline in data["pipelines"]:
            if not isinstance(pipeline, dict):
                continue
            remapped = remap_pipeline(pipeline, cache_dir, shader_dir, copied_shaders, missing_shader_inputs)
            remapped["captured_manifest"] = str(dst)
            missing_for_pipeline = pipeline_missing_inputs(remapped)
            if missing_for_pipeline:
                incomplete_pipelines.append({
                    "name": remapped.get("name", ""),
                    "type": remapped.get("type", ""),
                    "captured_manifest": str(dst),
                    "missing": missing_for_pipeline,
                })
                if not args.keep_incomplete:
                    continue
            pipelines.append(remapped)
            ptype = str(remapped.get("type", "unknown"))
            type_counts[ptype] = type_counts.get(ptype, 0) + 1

    # Copy broad metadata if present without making it a requirement.
    metadata_copied: list[str] = []
    for extra in sorted(cache_dir.glob("*.pipeline-cache*")) + sorted(cache_dir.glob("*pipeline*cache*")):
        if extra.is_file():
            dst = out_dir / "metadata" / extra.name
            if copy_if_present(extra, dst):
                metadata_copied.append(str(dst))

    manifest = {
        "schema": "metalsharp.m12.ac6-pso-pressure-corpus.v1",
        "appid": args.appid,
        "source_cache_dir": str(cache_dir),
        "output_dir": str(out_dir),
        "source_manifest_count": len(source_manifests),
        "pipeline_count": len(pipelines),
        "pipeline_type_counts": type_counts,
        "shader_artifact_count": len(copied_shaders),
        "missing_shader_inputs": sorted(set(missing_shader_inputs)),
        "incomplete_pipeline_count": len(incomplete_pipelines),
        "incomplete_pipelines": incomplete_pipelines,
        "metadata": metadata_copied,
        "source_manifests": source_manifests,
        "pipelines": pipelines,
    }
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    summary = [
        "# AC6 M12 PSO pressure corpus",
        "",
        f"- source cache: `{cache_dir}`",
        f"- output: `{out_dir}`",
        f"- PSO manifests copied: {len(source_manifests)}",
        f"- runnable pipelines: {len(pipelines)}",
        f"- runnable pipeline types: {type_counts}",
        f"- incomplete pipelines inventoried but omitted: {len(incomplete_pipelines) if not args.keep_incomplete else 0}",
        f"- shader artifacts copied: {len(copied_shaders)}",
        f"- missing required shader source/metallib entries: {len(set(missing_shader_inputs))}",
        "",
        "This corpus is a copy. Live shader caches were not modified.",
    ]
    if missing_shader_inputs:
        summary.extend(["", "## Missing shader inputs", ""])
        summary.extend(f"- `{item}`" for item in sorted(set(missing_shader_inputs))[:100])
        if len(set(missing_shader_inputs)) > 100:
            summary.append(f"- ... {len(set(missing_shader_inputs)) - 100} more")
    (out_dir / "summary.md").write_text("\n".join(summary) + "\n")

    print(out_dir)
    return 0 if pipelines else 1


if __name__ == "__main__":
    raise SystemExit(main())
