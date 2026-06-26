#!/usr/bin/env python3
"""Audit generated M12 MSL for semantic fallback hazards.

This is intentionally stricter than "does Metal compile?".  It catches
translation fallbacks that can compile but are semantically unsafe: unresolved
SSA pre-declarations, executable uses of those unresolved values, unlowered
`// call ...` placeholders, and unsupported/fatal markers.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any

UNRESOLVED_PREDECL_RE = re.compile(r"\bv(?P<id>\d+)\b.*// unresolved value pre-decl")
CALL_PLACEHOLDER_RE = re.compile(r"//\s*call\s+")
UNSUPPORTED_RE = re.compile(r"\bunsupported\b", re.IGNORECASE)
FATAL_RE = re.compile(r"\bfatal\b", re.IGNORECASE)


def line_samples(lines: list[str], pattern: re.Pattern[str], limit: int) -> list[dict[str, Any]]:
    samples: list[dict[str, Any]] = []
    for index, line in enumerate(lines, 1):
        if pattern.search(line):
            samples.append({"line": index, "text": line.rstrip()})
            if len(samples) >= limit:
                break
    return samples


def audit_one(path: Path, sample_limit: int) -> dict[str, Any]:
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()

    unresolved_ids: list[int] = []
    unresolved_line = -1
    unresolved_samples: list[dict[str, Any]] = []
    for index, line in enumerate(lines):
        match = UNRESOLVED_PREDECL_RE.search(line)
        if not match:
            continue
        value_id = int(match.group("id"))
        unresolved_ids.append(value_id)
        unresolved_line = max(unresolved_line, index)
        if len(unresolved_samples) < sample_limit:
            unresolved_samples.append({"line": index + 1, "id": value_id, "text": line.rstrip()})

    # Shader input/output declarations can reuse vN names before the function
    # predecl region.  Only references after the unresolved predecl block are
    # executable/default-value hazards.
    executable_unresolved: list[dict[str, Any]] = []
    body_lines = lines[unresolved_line + 1 :] if unresolved_line >= 0 else []
    for value_id in unresolved_ids:
        value_re = re.compile(rf"\bv{value_id}\b")
        for offset, line in enumerate(body_lines, unresolved_line + 2):
            if value_re.search(line):
                executable_unresolved.append({"id": value_id, "line": offset, "text": line.rstrip()})
                break

    call_placeholders = line_samples(lines, CALL_PLACEHOLDER_RE, sample_limit)
    unsupported_markers = line_samples(lines, UNSUPPORTED_RE, sample_limit)
    fatal_markers = line_samples(lines, FATAL_RE, sample_limit)

    return {
        "path": str(path),
        "shader": path.stem,
        "line_count": len(lines),
        "unresolved_predecl_count": len(unresolved_ids),
        "unresolved_predecl_ids": unresolved_ids[:128],
        "unresolved_predecl_samples": unresolved_samples,
        "executable_unresolved_count": len(executable_unresolved),
        "executable_unresolved_samples": executable_unresolved[:sample_limit],
        "call_placeholder_count": sum(1 for line in lines if CALL_PLACEHOLDER_RE.search(line)),
        "call_placeholder_samples": call_placeholders,
        "unsupported_marker_count": sum(1 for line in lines if UNSUPPORTED_RE.search(line)),
        "unsupported_marker_samples": unsupported_markers,
        "fatal_marker_count": sum(1 for line in lines if FATAL_RE.search(line)),
        "fatal_marker_samples": fatal_markers,
    }


def write_markdown(summary: dict[str, Any], path: Path) -> None:
    lines = [
        "# M12 MSL semantic hazard audit",
        "",
        f"MSL files: `{summary['msl_count']}`",
        f"OK: `{summary['ok']}`",
        f"unresolved_predecl_total: `{summary['unresolved_predecl_total']}`",
        f"executable_unresolved_total: `{summary['executable_unresolved_total']}`",
        f"call_placeholder_total: `{summary['call_placeholder_total']}`",
        f"unsupported_marker_total: `{summary['unsupported_marker_total']}`",
        f"fatal_marker_total: `{summary['fatal_marker_total']}`",
        "",
        "## Hazard files",
        "",
    ]
    hazards = summary["hazard_files"]
    if not hazards:
        lines.append("- none")
    else:
        for item in hazards[:100]:
            lines.append(
                "- "
                f"`{item['shader']}` unresolved={item['unresolved_predecl_count']} "
                f"exec_unresolved={item['executable_unresolved_count']} "
                f"call_placeholder={item['call_placeholder_count']} "
                f"unsupported={item['unsupported_marker_count']} fatal={item['fatal_marker_count']}"
            )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Audit M12-generated MSL for semantic fallback hazards.")
    parser.add_argument("--msl-dir", type=Path, required=True, help="Directory containing generated .msl files.")
    parser.add_argument("--output", type=Path, help="Path for JSON summary. Defaults to <msl-dir>/semantic-hazard-audit.json")
    parser.add_argument("--markdown", type=Path, help="Optional markdown summary path.")
    parser.add_argument("--sample-limit", type=int, default=12)
    parser.add_argument("--allow-unreferenced-predecl", action="store_true", help="Do not fail only because unresolved predecls exist if they are never referenced after the predecl region.")
    args = parser.parse_args()

    msl_files = sorted(args.msl_dir.glob("*.msl"))
    results = [audit_one(path, args.sample_limit) for path in msl_files]

    hazard_files = [
        result
        for result in results
        if result["executable_unresolved_count"]
        or result["call_placeholder_count"]
        or result["unsupported_marker_count"]
        or result["fatal_marker_count"]
        or (result["unresolved_predecl_count"] and not args.allow_unreferenced_predecl)
    ]

    summary: dict[str, Any] = {
        "schema": "metalsharp.m12.msl-semantic-hazard-audit.v1",
        "msl_dir": str(args.msl_dir),
        "msl_count": len(msl_files),
        "ok": len(msl_files) > 0 and not hazard_files,
        "allow_unreferenced_predecl": args.allow_unreferenced_predecl,
        "unresolved_predecl_total": sum(r["unresolved_predecl_count"] for r in results),
        "executable_unresolved_total": sum(r["executable_unresolved_count"] for r in results),
        "call_placeholder_total": sum(r["call_placeholder_count"] for r in results),
        "unsupported_marker_total": sum(r["unsupported_marker_count"] for r in results),
        "fatal_marker_total": sum(r["fatal_marker_count"] for r in results),
        "hazard_file_count": len(hazard_files),
        "hazard_files": hazard_files,
        "files": results,
    }

    output = args.output or (args.msl_dir / "semantic-hazard-audit.json")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    if args.markdown:
        args.markdown.parent.mkdir(parents=True, exist_ok=True)
        write_markdown(summary, args.markdown)
    print(output)
    return 0 if summary["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
