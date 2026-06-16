#!/usr/bin/env python3
"""Phase 3.5: summarize M12 live-hang captures.

Designed for Elden Ring character-creation hang captures. It is intentionally
read-only and works with partial logs/artifacts.
"""
from __future__ import annotations

import argparse
import collections
import json
import re
from pathlib import Path
from typing import Any

DRAWN_PRESENT_RE = re.compile(r"drawn/present\D+(\d+)\D+(\d+)", re.I)
PRESENT_RE = re.compile(r"\b(?:present_count|present)\b\D+(\d+)", re.I)
DRAW_RE = re.compile(r"\bdrawn_present_count\b\D+(\d+)", re.I)
ERROR_RE = re.compile(r"(Error Domain=[^\n]+|MTLCommandBufferError[^\n]*|render_pso_failed[^\n]*|compute_pso_failed[^\n]*|dxil_msl_compile_failed[^\n]*|unsafe_draw[^\n]*|fence[^\n]*wait[^\n]*|wait[^\n]*fence[^\n]*|timeout[^\n]*|hang[^\n]*)", re.I)
COMMAND_RE = re.compile(r"(command\s*(?:buffer|queue|list)[^\n]*|\bsubmit(?:ted)?\b[^\n]*|\bcompleted?\b[^\n]*|\bpresent(?:ed)?\b[^\n]*)", re.I)
COMPLETION_RE = re.compile(r"\bcompleted?\b|command\s*buffer[^\n]*(?:status|completed)", re.I)

DEFAULT_CAPTURE = Path("tools/d3d12-metal-sdk/results/live-captures/elden-ring-character-creation-hung-20260615-221626")


def read_text(path: Path, limit: int = 2_000_000) -> str:
    """Read small capture files fully; for large logs include head and tail.

Hang/no-progress evidence is usually at the end of a capture log, while launch
context is at the beginning, so keep both within a bounded read window.
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
        return (head + b"\n...[truncated middle of large capture log]...\n" + tail).decode(errors="replace")
    except OSError:
        return ""


def iter_files(root: Path):
    if not root.exists():
        return
    if root.is_file():
        yield root
        return
    for p in sorted(root.rglob("*")):
        if p.is_file() and (p.suffix.lower() in {".txt", ".md", ".log", ".json"} or "log" in p.name.lower()):
            yield p


def extract_metrics(path: Path, text: str) -> dict[str, Any]:
    drawn_present = [(int(m.group(1)), int(m.group(2))) for m in DRAWN_PRESENT_RE.finditer(text)]
    presents = [p for _, p in drawn_present]
    presents.extend(int(m.group(1)) for m in PRESENT_RE.finditer(text) if m.group(1).isdigit())
    draws = [d for d, _ in drawn_present]
    draws.extend(int(m.group(1)) for m in DRAW_RE.finditer(text) if m.group(1).isdigit())
    errors = [" ".join(m.group(1).split())[:500] for m in ERROR_RE.finditer(text)]
    commands = [" ".join(m.group(1).split())[:500] for m in COMMAND_RE.finditer(text)]
    return {
        "path": str(path),
        "present_numbers": presents[-20:],
        "max_present_number": max(presents) if presents else None,
        "draw_numbers": draws[-20:],
        "max_draw_number": max(draws) if draws else None,
        "error_or_wait_lines": errors[-80:],
        "command_progress_lines": commands[-80:],
    }


def classify(rows: list[dict[str, Any]]) -> list[str]:
    cats = []
    all_errors = "\n".join("\n".join(r["error_or_wait_lines"]) for r in rows).lower()
    all_cmd = "\n".join("\n".join(r["command_progress_lines"]) for r in rows).lower()
    max_present = max([r["max_present_number"] for r in rows if r["max_present_number"] is not None], default=None)
    if max_present is not None:
        cats.append("present_progress_observed")
    if "mtlcommandbuffererrordomain" in all_errors:
        cats.append("apple_command_buffer_error_observed")
    if "insufficient memory" in all_errors:
        cats.append("command_buffer_insufficient_memory")
    if "timeout" in all_errors:
        cats.append("timeout_or_wait_observed")
    if "fence" in all_errors or "wait" in all_errors:
        cats.append("wait_or_fence_evidence")
    if "render_pso_failed" in all_errors or "compute_pso_failed" in all_errors or "dxil_msl_compile_failed" in all_errors:
        cats.append("pipeline_or_translation_failure_near_capture")
    if all_cmd and not COMPLETION_RE.search(all_cmd):
        cats.append("submission_without_completion_evidence")
    if not cats:
        cats.append("insufficient_command_buffer_evidence")
    return cats


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("capture", nargs="?", type=Path, default=DEFAULT_CAPTURE)
    ap.add_argument("--results-dir", type=Path, default=Path("tools/d3d12-metal-sdk/results/phase3.5-apple-metal-docs/live-hang"))
    args = ap.parse_args()

    rows = []
    for path in iter_files(args.capture) or []:
        text = read_text(path)
        if not text:
            continue
        m = extract_metrics(path, text)
        if m["present_numbers"] or m["draw_numbers"] or m["error_or_wait_lines"] or m["command_progress_lines"]:
            rows.append(m)

    max_present = max([r["max_present_number"] for r in rows if r["max_present_number"] is not None], default=None)
    max_draw = max([r["max_draw_number"] for r in rows if r["max_draw_number"] is not None], default=None)
    categories = classify(rows)
    line_counts = collections.Counter()
    for r in rows:
        for line in r["error_or_wait_lines"]:
            low = line.lower()
            if "error domain=" in low:
                line_counts["apple_error"] += 1
            if "fence" in low or "wait" in low:
                line_counts["wait_fence"] += 1
            if "pso" in low or "dxil" in low:
                line_counts["pipeline_translation"] += 1

    report = {
        "schema": "metalsharp.m12.phase3_5.live-hang-analysis.v1",
        "capture": str(args.capture),
        "capture_exists": args.capture.exists(),
        "categories": categories,
        "max_present_number": max_present,
        "max_draw_number": max_draw,
        "line_counts": dict(line_counts),
        "files": rows,
        "required_runtime_logging_gaps": [
            "command_buffer_label", "command_buffer_status", "command_buffer_error_domain_code_userInfo",
            "encoder_info", "queue_id", "command_list_id", "last_submitted_serial", "last_completed_serial",
            "present_count_at_submit", "fence_event_wait_state",
        ],
    }
    args.results_dir.mkdir(parents=True, exist_ok=True)
    (args.results_dir / "live-hang-analysis.json").write_text(json.dumps(report, indent=2) + "\n")

    md = ["# M12 Phase 3.5 live-hang analysis", "", f"- capture: `{args.capture}`", f"- exists: `{args.capture.exists()}`", f"- categories: {', '.join(f'`{c}`' for c in categories)}", f"- max present number seen: `{max_present}`", f"- max draw number seen: `{max_draw}`", "", "## Evidence files"]
    for r in rows:
        md.append(f"- `{r['path']}` present_max=`{r['max_present_number']}` draw_max=`{r['max_draw_number']}` errors/waits={len(r['error_or_wait_lines'])} command_lines={len(r['command_progress_lines'])}")
        for line in r["error_or_wait_lines"][-5:]:
            md.append(f"  - {line}")
    md += ["", "## Runtime logging gaps to close", ""]
    for gap in report["required_runtime_logging_gaps"]:
        md.append(f"- `{gap}`")
    md += ["", "## Interpretation", "", "This analyzer can identify whether a capture has present progress, Apple command-buffer errors, waits/fences, or pipeline failures. If it reports `insufficient_command_buffer_evidence`, runtime logging must be extended before the Elden Ring character-creation hang can be reduced to a queue/wait/command-buffer state.", ""]
    (args.results_dir / "live-hang-analysis.md").write_text("\n".join(md))
    print(args.results_dir / "live-hang-analysis.md")
    print(args.results_dir / "live-hang-analysis.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
