#!/usr/bin/env python3
"""Classify M12 artifact utilization from launch artifacts and cache deltas.

This script is read-only for all input/cache/game files. It supports two modes:

1. Snapshot mode:
   classify-m12-artifacts.py --appid 1888160 --snapshot-out before.json

2. Classification mode:
   classify-m12-artifacts.py --appid 1888160 --profile ac6 \
     --launch-response launch-response.json --launch-log launch.log \
     --before-snapshot before.json --after-snapshot after.json \
     --results-dir launch-dir

The report intentionally separates:
- staged/requested M12 artifacts from launch response JSON;
- direct log evidence for module/API activity;
- cache/pipeline side effects observed between snapshots.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import time
from collections import Counter
from pathlib import Path
from typing import Any, Iterable

SCHEMA_SNAPSHOT = "metalsharp.d3d12-metal.m12-artifact-snapshot.v1"
SCHEMA_REPORT = "metalsharp.d3d12-metal.m12-artifact-utilization.v1"

APPID_TITLES = {
    1888160: "armored-core-vi",
    1245620: "elden-ring",
    1962700: "subnautica-2",
    3527290: "peak",
    3164500: "schedule-i",
}

TEXT_SUFFIXES = {".log", ".txt", ".json", ".md", ".err", ".stderr", ".stdout"}
CACHE_PATTERNS = ("*.msl", "*.metallib", "*.air", "*.json", "*.dxil_report.txt", "*.module.txt", "*.msc.fail", "*.msl.err.txt", "pso-*.json")

LOG_PATTERNS = {
    "pipeline_m12": re.compile(r"^pipeline=M12\b|\"pipeline\"\s*:\s*\"?m12\"?", re.I | re.M),
    "graphics_backend_dxmt": re.compile(r"^graphics_backend=dxmt\b|\"graphics_backend\"\s*:\s*\"dxmt\"", re.I | re.M),
    "d3d12_create_device": re.compile(r"\bD3D12CreateDevice\b|D3D12CreateDevice: created device|D3D12CreateDevice SUCCESS", re.I),
    "d3d11_create_device": re.compile(r"\bD3D11CreateDevice(?:AndSwapChain)?\b", re.I),
    "dxgi_factory": re.compile(r"CreateDXGIFactory|IDXGIFactory|dxgi", re.I),
    "ue_d3d12_rhi": re.compile(r"LogD3D12RHI:.*Creating D3D12 RHI|Creating D3D12 RHI", re.I),
    "d3d12_queue": re.compile(r"D3D12CommandQueue|ExecuteCommandLists|ID3D12CommandQueue", re.I),
    "winemetal": re.compile(r"winemetal|DXMT_WINEMETAL_UNIXLIB", re.I),
    "m12_winemetal_core_stack": re.compile(
        r"winemetal\.so.*(?:m12core_lower_dxil_to_msl|WMTM12CoreLowerDXILToMSL)|(?:m12core_lower_dxil_to_msl|WMTM12CoreLowerDXILToMSL)|internal:winemetal\.so|dxmt_unix_winemetal_internal_m12core",
        re.I,
    ),
    "m12core": re.compile(r"\bm12core\b|DXMT_M12CORE_ENABLE|libm12core", re.I),
    "moltenvk": re.compile(r"\bMoltenVK\b|VK_MVK_moltenvk", re.I),
    "vk_instance": re.compile(r"Created VkInstance|vkCreateInstance|Vulkan version", re.I),
    "vk_device_or_renderer": re.compile(r"Created VkDevice|vkCreateDevice|GfxDevice:.*Vulkan|Using Vulkan", re.I),
}

ARTIFACT_NAMES = {
    "d3d12.dll",
    "d3d11.dll",
    "d3d10core.dll",
    "dxgi.dll",
    "dxgi_dxmt.dll",
    "winemetal.dll",
    "winemetal.so",
    "libm12core.dylib",
    "nvapi64.dll",
    "nvngx.dll",
}


def sha256_file(path: Path, max_bytes: int | None = None) -> str | None:
    try:
        h = hashlib.sha256()
        with path.open("rb") as f:
            remaining = max_bytes
            while True:
                if remaining is None:
                    chunk = f.read(1024 * 1024)
                elif remaining <= 0:
                    break
                else:
                    chunk = f.read(min(1024 * 1024, remaining))
                    remaining -= len(chunk)
                if not chunk:
                    break
                h.update(chunk)
        return h.hexdigest()
    except OSError:
        return None


def file_entry(path: Path, root: Path) -> dict[str, Any]:
    stat = path.stat()
    rel = str(path.relative_to(root)) if root in path.parents or path == root else path.name
    return {
        "rel": rel,
        "path": str(path),
        "name": path.name,
        "size": stat.st_size,
        "mtime_ns": stat.st_mtime_ns,
        "sha256": sha256_file(path) if stat.st_size <= 25_000_000 else None,
    }


def default_roots(appid: int) -> dict[str, Path]:
    home = Path.home()
    return {
        "shader_cache": home / ".metalsharp" / "shader-cache" / "m12" / str(appid),
        "pipeline_cache": home / ".metalsharp" / "pipeline-cache" / "m12" / str(appid),
        "compat_logs": home / ".metalsharp" / "compatdata" / str(appid) / "logs",
        "m12_logs": home / ".metalsharp" / "logs" / "m12-pipeline" / str(appid),
    }


def iter_files_for_root(root: Path) -> Iterable[Path]:
    if not root.exists():
        return []
    if root.is_file():
        return [root]
    files: list[Path] = []
    for pattern in CACHE_PATTERNS:
        files.extend(path for path in root.rglob(pattern) if path.is_file())
    if root.name == "logs" or "logs" in root.parts:
        files.extend(path for path in root.rglob("*") if path.is_file() and path.suffix.lower() in TEXT_SUFFIXES)
    return sorted(set(files))


def make_snapshot(appid: int, extra_root: list[Path]) -> dict[str, Any]:
    roots = default_roots(appid)
    for idx, root in enumerate(extra_root):
        roots[f"extra_{idx}_{root.name or 'root'}"] = root.expanduser()
    entries: dict[str, list[dict[str, Any]]] = {}
    for label, root in roots.items():
        root = root.expanduser()
        rows: list[dict[str, Any]] = []
        if root.exists():
            for path in iter_files_for_root(root):
                try:
                    rows.append(file_entry(path, root if root.is_dir() else path.parent))
                except OSError:
                    continue
        entries[label] = rows
    return {
        "schema": SCHEMA_SNAPSHOT,
        "generated_at": int(time.time()),
        "appid": appid,
        "title": APPID_TITLES.get(appid),
        "roots": {label: str(path) for label, path in roots.items()},
        "entries": entries,
        "counts": {label: len(rows) for label, rows in entries.items()},
    }


def entry_key(entry: dict[str, Any]) -> str:
    return str(entry.get("rel") or entry.get("path") or entry.get("name"))


def diff_snapshots(before: dict[str, Any] | None, after: dict[str, Any] | None) -> dict[str, Any]:
    if not before or not after:
        return {"available": False, "roots": {}}
    roots: dict[str, Any] = {}
    all_labels = sorted(set(before.get("entries", {})) | set(after.get("entries", {})))
    for label in all_labels:
        b = {entry_key(e): e for e in before.get("entries", {}).get(label, [])}
        a = {entry_key(e): e for e in after.get("entries", {}).get(label, [])}
        new_keys = sorted(set(a) - set(b))
        removed_keys = sorted(set(b) - set(a))
        modified_keys = sorted(k for k in set(a) & set(b) if (a[k].get("sha256"), a[k].get("size"), a[k].get("mtime_ns")) != (b[k].get("sha256"), b[k].get("size"), b[k].get("mtime_ns")))
        roots[label] = {
            "before_count": len(b),
            "after_count": len(a),
            "new_count": len(new_keys),
            "modified_count": len(modified_keys),
            "removed_count": len(removed_keys),
            "new": [a[k] for k in new_keys[:80]],
            "modified": [a[k] for k in modified_keys[:80]],
            "removed": [b[k] for k in removed_keys[:40]],
            "new_by_suffix": dict(Counter(Path(k).suffix or Path(k).name for k in new_keys)),
            "modified_by_suffix": dict(Counter(Path(k).suffix or Path(k).name for k in modified_keys)),
        }
    return {"available": True, "roots": roots}


def read_text_window(path: Path, limit: int = 2_000_000) -> str:
    try:
        size = path.stat().st_size
        if size <= limit:
            return path.read_text(errors="replace")
        half = limit // 2
        with path.open("rb") as f:
            head = f.read(half)
            f.seek(max(0, size - half))
            tail = f.read(half)
        return (head + b"\n...[truncated middle of large file]...\n" + tail).decode(errors="replace")
    except OSError:
        return ""


def load_json(path: Path | None) -> dict[str, Any] | None:
    if not path:
        return None
    try:
        return json.loads(path.expanduser().read_text(errors="replace"))
    except Exception:
        return None


def parse_launch_response(data: dict[str, Any] | None) -> dict[str, Any]:
    if not data:
        return {"available": False}
    recipe = data.get("recipe") if isinstance(data.get("recipe"), dict) else {}
    dlls = recipe.get("dlls") if isinstance(recipe.get("dlls"), list) else []
    rows = []
    for dll in dlls:
        if not isinstance(dll, dict):
            continue
        name = str(dll.get("filename") or Path(str(dll.get("dest_path") or "")).name)
        if name.lower() not in ARTIFACT_NAMES:
            continue
        dest = Path(str(dll.get("dest_path") or ""))
        src = Path(str(dll.get("source_path") or ""))
        rows.append({
            "filename": name,
            "source_path": str(src) if str(src) else None,
            "dest_path": str(dest) if str(dest) else None,
            "source_present_in_response": dll.get("source_present"),
            "dest_exists_now": dest.exists() if str(dest) else None,
            "source_sha256_now": sha256_file(src) if str(src) and src.exists() else None,
            "dest_sha256_now": sha256_file(dest) if str(dest) and dest.exists() else None,
            "source_dest_match_now": (sha256_file(src) == sha256_file(dest)) if str(src) and str(dest) and src.exists() and dest.exists() else None,
        })
    return {
        "available": True,
        "ok": data.get("ok"),
        "pid": data.get("pid"),
        "pipeline": data.get("pipeline"),
        "gameType": data.get("gameType"),
        "backend": data.get("backend") or recipe.get("backend"),
        "launch_log": data.get("launch_log") or data.get("logPath") or data.get("log_path"),
        "env_overrides": data.get("env_overrides") or data.get("envOverrides"),
        "launch_args": recipe.get("launch_args"),
        "exe_name": recipe.get("exe_name"),
        "dlls": rows,
    }


def log_evidence(paths: list[Path]) -> dict[str, Any]:
    rows: list[dict[str, Any]] = []
    totals: dict[str, int] = {key: 0 for key in LOG_PATTERNS}
    for path in paths:
        path = path.expanduser()
        if not path.exists() or not path.is_file():
            continue
        text = read_text_window(path)
        lines = text.splitlines()
        file_rows = []
        for key, pattern in LOG_PATTERNS.items():
            matches = list(pattern.finditer(text))
            if not matches:
                continue
            totals[key] += len(matches)
            snippets = []
            for line in lines:
                if pattern.search(line):
                    snippets.append(line.strip()[-500:])
                if len(snippets) >= 6:
                    break
            file_rows.append({"key": key, "count": len(matches), "snippets": snippets})
        rows.append({"path": str(path), "evidence": file_rows})
    return {"totals": totals, "files": rows}


def classify_utilization(launch: dict[str, Any], logs: dict[str, Any], delta: dict[str, Any]) -> tuple[str, str, list[str]]:
    totals = logs.get("totals", {})
    warnings: list[str] = []
    d3d12_seen = bool(
        totals.get("d3d12_create_device")
        or totals.get("ue_d3d12_rhi")
        or totals.get("d3d12_queue")
        or totals.get("m12_winemetal_core_stack")
    )
    d3d11_seen = bool(totals.get("d3d11_create_device"))
    m12_launch = (str(launch.get("pipeline", "")).lower() == "m12") or bool(totals.get("pipeline_m12"))
    staged = bool(launch.get("dlls"))
    m12_support = bool(totals.get("m12core") or totals.get("winemetal") or totals.get("graphics_backend_dxmt"))
    cache_changed = False
    shader_delta = 0
    pipeline_delta = 0
    if delta.get("available"):
        roots = delta.get("roots", {})
        for label, root in roots.items():
            changed = int(root.get("new_count", 0)) + int(root.get("modified_count", 0))
            if changed:
                cache_changed = True
            if "shader" in label:
                shader_delta += changed
            if "pipeline" in label:
                pipeline_delta += changed
    if d3d12_seen:
        return "active-d3d12-m12", "high", warnings
    if d3d11_seen:
        return "active-d3d11-dxmt", "high", warnings
    if m12_launch and (staged or m12_support or cache_changed):
        if not d3d12_seen:
            warnings.append("M12 staging/support evidence exists, but no direct D3D12CreateDevice/RHI/queue line was found in the provided logs.")
        if cache_changed:
            return "m12-artifacts-used-unproven-api", "medium", warnings
        return "m12-staged-unproven-api", "low", warnings
    if totals.get("vk_device_or_renderer"):
        return "active-vulkan", "high", warnings
    if totals.get("moltenvk") or totals.get("vk_instance"):
        warnings.append("Only MoltenVK/VkInstance bootstrap evidence was found; this is not enough to prove active Vulkan rendering.")
        return "vulkan-bootstrap-only", "low", warnings
    return "unknown", "low", warnings


def build_report(args: argparse.Namespace) -> dict[str, Any]:
    before = load_json(args.before_snapshot)
    after = load_json(args.after_snapshot)
    launch_response = parse_launch_response(load_json(args.launch_response))
    log_paths: list[Path] = []
    if args.launch_log:
        log_paths.append(args.launch_log)
    elif launch_response.get("launch_log"):
        log_paths.append(Path(str(launch_response["launch_log"])))
    log_paths.extend(args.extra_log or [])
    logs = log_evidence(log_paths)
    delta = diff_snapshots(before, after)
    classification, confidence, warnings = classify_utilization(launch_response, logs, delta)
    return {
        "schema": SCHEMA_REPORT,
        "generated_at": int(time.time()),
        "appid": args.appid,
        "title": APPID_TITLES.get(args.appid),
        "profile": args.profile or APPID_TITLES.get(args.appid) or str(args.appid),
        "classification": classification,
        "confidence": confidence,
        "warnings": warnings,
        "launch_response": launch_response,
        "log_evidence": logs,
        "snapshot_delta": delta,
    }


def markdown(report: dict[str, Any]) -> str:
    lines = [
        f"# M12 artifact utilization: {report['profile']}",
        "",
        f"- classification: `{report['classification']}`",
        f"- confidence: `{report['confidence']}`",
        f"- appid: `{report['appid']}`",
        f"- title: `{report.get('title')}`",
        "",
    ]
    if report["warnings"]:
        lines += ["## Warnings", ""]
        for warning in report["warnings"]:
            lines.append(f"- {warning}")
        lines.append("")
    launch = report["launch_response"]
    lines += ["## Launch response", ""]
    for key in ["ok", "pid", "pipeline", "gameType", "backend", "launch_log", "env_overrides", "launch_args", "exe_name"]:
        lines.append(f"- `{key}`: `{launch.get(key)}`")
    lines += ["", "## M12 DLL staging", ""]
    for row in launch.get("dlls", []):
        lines.append(f"- `{row['filename']}` dest_exists=`{row['dest_exists_now']}` source_dest_match=`{row['source_dest_match_now']}` dest=`{row['dest_path']}`")
    lines += ["", "## Log evidence totals", "", f"`{json.dumps(report['log_evidence']['totals'], sort_keys=True)}`", ""]
    for file_row in report["log_evidence"].get("files", []):
        lines.append(f"### `{file_row['path']}`")
        for ev in file_row.get("evidence", []):
            lines.append(f"- `{ev['key']}` count `{ev['count']}`")
            for snippet in ev.get("snippets", [])[:4]:
                lines.append(f"  - `{snippet}`")
        lines.append("")
    delta = report["snapshot_delta"]
    lines += ["## Snapshot delta", ""]
    if not delta.get("available"):
        lines.append("- no before/after snapshots provided")
    else:
        for label, root in delta.get("roots", {}).items():
            lines.append(f"- `{label}` before=`{root['before_count']}` after=`{root['after_count']}` new=`{root['new_count']}` modified=`{root['modified_count']}` removed=`{root['removed_count']}` suffix_new=`{json.dumps(root.get('new_by_suffix', {}), sort_keys=True)}`")
    return "\n".join(lines) + "\n"


def safe_profile(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "-", value).strip("-") or "m12-artifacts"


def main() -> int:
    ap = argparse.ArgumentParser(description="Snapshot/classify M12 artifact utilization without launching games.")
    ap.add_argument("--appid", type=int, required=True)
    ap.add_argument("--profile", default="")
    ap.add_argument("--extra-root", action="append", type=Path, default=[], help="Additional root to include in snapshot mode.")
    ap.add_argument("--snapshot-out", type=Path, help="Write a snapshot JSON and exit.")
    ap.add_argument("--before-snapshot", type=Path)
    ap.add_argument("--after-snapshot", type=Path)
    ap.add_argument("--launch-response", type=Path)
    ap.add_argument("--launch-log", type=Path)
    ap.add_argument("--extra-log", action="append", type=Path, default=[])
    ap.add_argument("--results-dir", type=Path, default=Path(__file__).resolve().parents[1] / "results")
    ap.add_argument("--stdout", action="store_true")
    args = ap.parse_args()

    if args.snapshot_out:
        snapshot = make_snapshot(args.appid, args.extra_root)
        args.snapshot_out.parent.mkdir(parents=True, exist_ok=True)
        args.snapshot_out.write_text(json.dumps(snapshot, indent=2) + "\n")
        print(args.snapshot_out)
        return 0

    report = build_report(args)
    if args.stdout:
        print(json.dumps(report, indent=2))
        return 0

    args.results_dir.mkdir(parents=True, exist_ok=True)
    profile = safe_profile(args.profile or APPID_TITLES.get(args.appid) or str(args.appid))
    json_path = args.results_dir / f"m12-artifact-utilization-{profile}.json"
    md_path = args.results_dir / f"m12-artifact-utilization-{profile}.md"
    json_path.write_text(json.dumps(report, indent=2) + "\n")
    md_path.write_text(markdown(report))
    print(md_path)
    print(json_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
