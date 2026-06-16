#!/usr/bin/env python3
"""Phase 3.5: audit Metal NSError / M12 failure context from logs and caches.

Read-only for input logs/caches. Writes JSON/Markdown reports to --results-dir.
"""
from __future__ import annotations

import argparse
import collections
import json
import re
from pathlib import Path
from typing import Any

ERROR_DOMAIN_RE = re.compile(r"Error Domain=([^\s]+) Code=(-?\d+) \"(.*?)\"", re.S)
PROGRAM_RE = re.compile(r"program_source:(\d+):(\d+):\s*(error|warning):\s*(.*)")
HASH_RE = re.compile(r"^[0-9a-fA-F]{16}$")

DEFAULT_INPUTS = [
    ("elden-ring-cache", Path.home() / ".metalsharp/shader-cache/m12/1245620"),
    ("subnautica-2-cache", Path.home() / ".metalsharp/shader-cache/m12/1962700"),
    ("armored-core-vi-cache", Path.home() / ".metalsharp/shader-cache/m12/1888160"),
    ("metalsharp-logs", Path.home() / ".metalsharp/logs"),
    ("elden-ring-compat-logs", Path.home() / ".metalsharp/compatdata/1245620/logs"),
    ("subnautica-2-compat-logs", Path.home() / ".metalsharp/compatdata/1962700/logs"),
    ("armored-core-vi-compat-logs", Path.home() / ".metalsharp/compatdata/1888160/logs"),
]


def read_text(path: Path, limit: int = 1_500_000) -> str:
    """Read small files fully; for large logs include both head and tail.

Metal failures in append-only runtime logs are often near the end. Returning a
head+tail window avoids missing the latest failure while still bounding memory.
"""
    try:
        size = path.stat().st_size
        if size <= limit:
            return path.read_text(errors="replace")
        half = limit // 2
        with path.open("rb") as f:
            head = f.read(half)
            f.seek(max(0, size - half))
            tail = f.read(half)
        return (head + b"\n...[truncated middle of large log]...\n" + tail).decode(errors="replace")
    except OSError:
        return ""


def shader_hash_for(path: Path) -> str | None:
    stem = path.name.split(".", 1)[0]
    return stem.lower() if HASH_RE.match(stem) else None


def classify(text: str) -> str:
    t = text.lower()
    if "call to 'ctz' is ambiguous" in t or 'call to "ctz" is ambiguous' in t:
        return "msl_ctz_ambiguous"
    if "mtlcommandbuffererrordomain" in t and ("code=8" in t or "insufficient memory" in t):
        return "command_buffer_insufficient_memory"
    if "mtlcommandbuffererrordomain" in t and "timeout" in t:
        return "command_buffer_timeout"
    if "mtlcommandbuffererrordomain" in t and "page" in t and "fault" in t:
        return "command_buffer_page_fault"
    if "mtllibraryerrordomain" in t:
        return "metal_library_compile_error"
    if "render_pso_failed" in t or "render pso" in t:
        return "render_pso_failure"
    if "compute_pso_failed" in t or "compute pso" in t:
        return "compute_pso_failure"
    if "dxil_msl_compile_failed" in t:
        return "dxil_msl_compile_failure"
    if "vertex_descriptor_missing" in t:
        return "vertex_descriptor_missing"
    if "vs_ps_varying_mismatch" in t:
        return "vs_ps_varying_mismatch"
    if "error domain=" in t:
        return "apple_nserror_other"
    return "m12_error_context"


def iter_files(root: Path):
    if not root.exists():
        return
    if root.is_file():
        yield root
        return
    for p in root.rglob("*"):
        if not p.is_file():
            continue
        if p.suffix.lower() in {".log", ".txt", ".fail"} or p.name.endswith(".msl.err.txt") or p.name.endswith(".md"):
            yield p


def parse_file(label: str, path: Path) -> list[dict[str, Any]]:
    text = read_text(path)
    if not text:
        return []
    interesting = any(s in text for s in [
        "Error Domain=", "MTLCommandBufferError", "MTLLibraryError", "render_pso_failed",
        "compute_pso_failed", "dxil_msl_compile_failed", "vertex_descriptor_missing",
        "vs_ps_varying_mismatch", "call to 'ctz' is ambiguous", "Insufficient Memory",
    ])
    if not interesting and not path.name.endswith(".msl.err.txt"):
        return []

    rows: list[dict[str, Any]] = []
    domain_matches = list(ERROR_DOMAIN_RE.finditer(text))
    if domain_matches:
        if len(domain_matches) > 30:
            selected_matches = domain_matches[:10] + domain_matches[-20:]
        else:
            selected_matches = domain_matches
        for m in selected_matches:
            snippet = text[max(0, m.start() - 500): min(len(text), m.end() + 1200)]
            rows.append({
                "input": label,
                "path": str(path),
                "shader_hash": shader_hash_for(path),
                "category": classify(snippet),
                "apple_error_domain": m.group(1),
                "apple_error_code": int(m.group(2)),
                "apple_error_text": " ".join(m.group(3).split())[:2000],
                "program_diagnostics": [
                    {"line": int(pm.group(1)), "column": int(pm.group(2)), "severity": pm.group(3), "message": pm.group(4).strip()}
                    for pm in PROGRAM_RE.finditer(snippet)
                ][:20],
                "snippet": " ".join(snippet.split())[:2500],
            })
    elif interesting:
        rows.append({
            "input": label,
            "path": str(path),
            "shader_hash": shader_hash_for(path),
            "category": classify(text),
            "apple_error_domain": None,
            "apple_error_code": None,
            "apple_error_text": None,
            "program_diagnostics": [
                {"line": int(pm.group(1)), "column": int(pm.group(2)), "severity": pm.group(3), "message": pm.group(4).strip()}
                for pm in PROGRAM_RE.finditer(text)
            ][:20],
            "snippet": " ".join(text.split())[:2500],
        })
    return rows


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", action="append", default=[], help="LABEL=PATH or PATH. Repeatable.")
    ap.add_argument("--results-dir", type=Path, default=Path("tools/d3d12-metal-sdk/results/phase3.5-apple-metal-docs/metal-errors"))
    args = ap.parse_args()

    inputs = []
    if args.input:
        for raw in args.input:
            if "=" in raw:
                label, path = raw.split("=", 1)
            else:
                path = raw
                label = Path(path).name or "input"
            inputs.append((label, Path(path).expanduser()))
    else:
        inputs = DEFAULT_INPUTS

    rows: list[dict[str, Any]] = []
    seen = set()
    for label, root in inputs:
        for path in iter_files(root) or []:
            key = (label, str(path))
            if key in seen:
                continue
            seen.add(key)
            rows.extend(parse_file(label, path))

    counts = collections.Counter(r["category"] for r in rows)
    by_domain = collections.Counter(str(r.get("apple_error_domain")) for r in rows if r.get("apple_error_domain"))
    missing_full_context = [r for r in rows if r.get("apple_error_domain") and not r.get("shader_hash") and "shader-cache" in r.get("path", "")]

    report = {
        "schema": "metalsharp.m12.phase3_5.metal-errors.v1",
        "inputs": [{"label": l, "path": str(p), "exists": p.exists()} for l, p in inputs],
        "total_events": len(rows),
        "category_counts": dict(counts),
        "apple_error_domain_counts": dict(by_domain),
        "missing_shader_context_in_cache_events": len(missing_full_context),
        "events": rows,
    }
    args.results_dir.mkdir(parents=True, exist_ok=True)
    (args.results_dir / "metal-errors.json").write_text(json.dumps(report, indent=2) + "\n")

    md = ["# M12 Phase 3.5 Metal error audit", "", f"- events: `{len(rows)}`", "", "## Categories"]
    for k, v in counts.most_common():
        md.append(f"- `{k}`: {v}")
    md += ["", "## Apple error domains"]
    for k, v in by_domain.most_common():
        md.append(f"- `{k}`: {v}")
    md += ["", "## Top events"]
    for r in rows[:80]:
        md.append(f"- `{r['category']}` path=`{r['path']}` hash=`{r.get('shader_hash')}` domain=`{r.get('apple_error_domain')}` code=`{r.get('apple_error_code')}`")
        for d in r.get("program_diagnostics", [])[:3]:
            md.append(f"  - {d['severity']} {d['line']}:{d['column']} {d['message']}")
    md += ["", "## Acceptance notes", "", "- AC6 ambiguous `ctz(...)` failures should classify as `msl_ctz_ambiguous`.", "- Command buffer OOM/page-fault/timeout must remain distinct when Apple reports them.", "- Runtime source should add missing queue/list/present/PSO context where this audit only sees generic log text.", ""]
    (args.results_dir / "metal-errors.md").write_text("\n".join(md))
    print(args.results_dir / "metal-errors.md")
    print(args.results_dir / "metal-errors.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
