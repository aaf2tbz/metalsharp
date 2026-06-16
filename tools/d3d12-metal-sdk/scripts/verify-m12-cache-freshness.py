#!/usr/bin/env python3
"""Phase 3.5: verify M12 shader/cache freshness invariants.

This is an offline/read-only checker for live caches. It does not regenerate or
delete cache files. It identifies stale/error-adjacent artifacts that should be
invalidated by runtime cache-key policy.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import json
import os
import platform
import subprocess
from pathlib import Path
from typing import Any

DEFAULT_CORPORA = [
    ("elden-ring-live", Path.home() / ".metalsharp/shader-cache/m12/1245620"),
    ("subnautica-2-live", Path.home() / ".metalsharp/shader-cache/m12/1962700"),
    ("armored-core-vi-live", Path.home() / ".metalsharp/shader-cache/m12/1888160"),
    ("elden-ring-scratch", Path("/Volumes/AverySSD/MetalSharp-M12-CorpusLab/elden-ring-scratch/stable-20260615-192733")),
]

DLLS = ["d3d12.dll", "dxgi.dll", "dxcompiler.dll", "dxil.dll"]
RUNTIME_DIR_CANDIDATES = [
    Path.home() / ".metalsharp/runtime/wine/lib/dxmt_m12/x86_64-windows",
    Path.home() / ".metalsharp/runtime/wine/lib/dxmt-m12/x86_64-windows",
]
def default_runtime_dir() -> Path:
    env = os.environ.get("M12_RUNTIME_DIR") or os.environ.get("DXMT_M12_RUNTIME_DIR")
    if env:
        return Path(env).expanduser()
    with_d3d12 = [p for p in RUNTIME_DIR_CANDIDATES if (p / "d3d12.dll").exists()]
    if with_d3d12:
        return max(with_d3d12, key=lambda p: (p / "d3d12.dll").stat().st_mtime)
    return next((p for p in RUNTIME_DIR_CANDIDATES if p.exists()), RUNTIME_DIR_CANDIDATES[0])


RUNTIME_DIR = default_runtime_dir()


def sha256(path: Path) -> str | None:
    try:
        h = hashlib.sha256()
        with path.open("rb") as f:
            for chunk in iter(lambda: f.read(1024 * 1024), b""):
                h.update(chunk)
        return h.hexdigest()
    except OSError:
        return None


def git_commit() -> str | None:
    try:
        return subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    except Exception:
        return None


def stat(path: Path) -> dict[str, Any] | None:
    try:
        s = path.stat()
        return {"size": s.st_size, "mtime": s.st_mtime, "sha256": sha256(path)}
    except OSError:
        return None


def build_pso_shader_index(root: Path) -> dict[str, list[str]]:
    """Map shader hashes referenced inside PSO manifests to manifest paths."""
    index: dict[str, list[str]] = collections.defaultdict(list)
    if not root.exists():
        return index
    for p in root.rglob("pso-*.json"):
        try:
            data = json.loads(p.read_text(errors="replace"))
        except Exception:
            continue
        stack: list[Any] = [data]
        found: set[str] = set()
        while stack:
            item = stack.pop()
            if isinstance(item, dict):
                for k, v in item.items():
                    if isinstance(v, str) and len(v) == 16 and all(c in "0123456789abcdefABCDEF" for c in v):
                        found.add(v.lower())
                    elif isinstance(v, (dict, list)):
                        stack.append(v)
            elif isinstance(item, list):
                stack.extend(item)
        for h in found:
            index[h].append(str(p))
    return index


def build_artifact_index(root: Path) -> dict[str, dict[str, list[Path]]]:
    index: dict[str, dict[str, list[Path]]] = collections.defaultdict(lambda: collections.defaultdict(list))
    if not root.exists():
        return index
    suffixes = ["dxbc", "msl", "metallib", "dxil_report.txt", "module.txt", "msl.err.txt", "fail"]
    for p in root.rglob("*"):
        if not p.is_file():
            continue
        first = p.name.split(".", 1)[0]
        if len(first) != 16 or not all(c in "0123456789abcdefABCDEF" for c in first):
            continue
        for ext in suffixes:
            if p.name == f"{first}.{ext}":
                index[first.lower()][ext].append(p)
                break
    return index


def inspect_hash(root: Path, h: str, pso_index: dict[str, list[str]], artifact_index: dict[str, dict[str, list[Path]]]) -> dict[str, Any]:
    artifact_paths = artifact_index.get(h, {})
    files = {ext: max(paths, key=lambda p: p.stat().st_mtime) for ext, paths in artifact_paths.items() if paths}
    pso_refs = sorted(pso_index.get(h, []))
    present = {k: stat(p) for k, p in files.items() if p.exists()}
    cats: list[str] = []
    if "dxbc" in present and "msl" not in present:
        cats.append("dxbc_without_msl")
    if "msl" in present and "dxil_report.txt" not in present:
        cats.append("msl_without_dxil_report")
    if "msl.err.txt" in present:
        cats.append("has_msl_error")
        if "msl" in present:
            # Error newer than or equal to source means the last observed compile still failed.
            if present["msl.err.txt"]["mtime"] >= present["msl"]["mtime"]:
                cats.append("active_msl_error")
            else:
                cats.append("stale_msl_error_older_than_msl")
    if "fail" in present:
        cats.append("has_fail_marker")
    if "metallib" in present and "msl" in present:
        if present["metallib"]["mtime"] < present["msl"]["mtime"]:
            cats.append("metallib_older_than_msl")
    if "metallib" in present and "msl.err.txt" in present and present["msl.err.txt"]["mtime"] >= present["metallib"]["mtime"]:
        cats.append("metallib_with_newer_error")
    if "dxbc" in present and not pso_refs and "msl.err.txt" in present:
        cats.append("failing_shader_without_pso_link")
    if not cats:
        cats.append("ok_or_uncompiled")
    return {"hash": h, "files": {k: {"path": str(files[k]), **v} for k, v in present.items()}, "pso_refs": pso_refs[:20], "duplicate_artifacts": {k: [str(p) for p in paths] for k, paths in artifact_paths.items() if len(paths) > 1}, "categories": cats}


def inspect_corpus(label: str, root: Path) -> dict[str, Any]:
    artifact_index = build_artifact_index(root)
    hashes = set(artifact_index.keys())
    pso_index = build_pso_shader_index(root)
    hashes.update(pso_index.keys())
    rows = [inspect_hash(root, h, pso_index, artifact_index) for h in sorted(hashes)]
    counts = collections.Counter(c for r in rows for c in r["categories"])
    return {"label": label, "root": str(root), "exists": root.exists(), "shader_hashes": len(rows), "category_counts": dict(counts), "rows": rows}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", action="append", default=[], help="LABEL=PATH or PATH. Repeatable.")
    ap.add_argument("--runtime-dir", type=Path, default=RUNTIME_DIR)
    ap.add_argument("--results-dir", type=Path, default=Path("tools/d3d12-metal-sdk/results/phase3.5-apple-metal-docs/cache-freshness"))
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

    runtime_hashes = {name: {"path": str(args.runtime_dir / name), "sha256": sha256(args.runtime_dir / name)} for name in DLLS}
    env_context = {
        "git_commit": git_commit(),
        "platform": platform.platform(),
        "metalsharp_m12_defaults": {
            "DXMT_D3D12_TYPED_STAGE_IN_VERTEX_DESCRIPTOR": os.environ.get("DXMT_D3D12_TYPED_STAGE_IN_VERTEX_DESCRIPTOR", "default-on-in-runtime"),
            "DXMT_D3D12_FORCE_DXIL_SOURCE_COMPILE": os.environ.get("DXMT_D3D12_FORCE_DXIL_SOURCE_COMPILE", "default-on-in-runtime"),
            "DXMT_D3D12_VERTEX_RANGE_SAFE_DRAW": os.environ.get("DXMT_D3D12_VERTEX_RANGE_SAFE_DRAW", "default-on-in-runtime"),
            "DXMT_D3D12_PSO_WORKERS": os.environ.get("DXMT_D3D12_PSO_WORKERS", "default-1-in-runtime"),
            "DXMT_ASYNC_PIPELINE_COMPILE": os.environ.get("DXMT_ASYNC_PIPELINE_COMPILE", "default-on-in-runtime"),
        },
        "required_cache_key_fields": [
            "device_identity", "os_version", "metal_family_or_sdk", "translator_commit_or_epoch",
            "dxbc_sha256", "generated_msl_sha256", "entry_point", "function_constants",
            "render_or_compute_descriptor_state", "vertex_descriptor_state",
            "attachment_formats", "sample_count", "root_signature_hash",
        ],
    }
    reports = [inspect_corpus(label, root) for label, root in corpora]
    args.results_dir.mkdir(parents=True, exist_ok=True)
    report = {"schema": "metalsharp.m12.phase3_5.cache-freshness.v1", "runtime_hashes": runtime_hashes, "context": env_context, "corpora": reports}
    (args.results_dir / "cache-freshness.json").write_text(json.dumps(report, indent=2) + "\n")

    md = ["# M12 Phase 3.5 cache freshness audit", "", "## Runtime hashes"]
    for name, info in runtime_hashes.items():
        md.append(f"- `{name}` `{info['sha256']}` path=`{info['path']}`")
    md += ["", "## Corpus summary", "", "| corpus | exists | shaders | categories |", "|---|---:|---:|---|"]
    for cr in reports:
        cats = ", ".join(f"`{k}`={v}" for k, v in sorted(cr["category_counts"].items()))
        md.append(f"| `{cr['label']}` | `{cr['exists']}` | {cr['shader_hashes']} | {cats or '-'} |")
    md += ["", "## Active/stale examples"]
    for cr in reports:
        shown = 0
        for r in cr["rows"]:
            if r["categories"] == ["ok_or_uncompiled"]:
                continue
            md.append(f"- `{cr['label']}` `{r['hash']}` categories={','.join(r['categories'])}")
            shown += 1
            if shown >= 12:
                break
    md += ["", "## Required cache key contract", ""]
    for field in env_context["required_cache_key_fields"]:
        md.append(f"- `{field}`")
    md += ["", "## Interpretation", "", "- `active_msl_error` means the latest observed compile for that MSL still failed and should not be hidden by drawn/present counts.", "- `metallib_older_than_msl` means descriptor/source freshness logic should rebuild the metallib before trusting warm-cache behavior.", "- Runtime cache keys should include descriptor-affecting state from Apple's `MTLBinaryArchive`/pipeline descriptor model, not only shader hashes.", ""]
    (args.results_dir / "cache-freshness.md").write_text("\n".join(md))
    print(args.results_dir / "cache-freshness.md")
    print(args.results_dir / "cache-freshness.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
