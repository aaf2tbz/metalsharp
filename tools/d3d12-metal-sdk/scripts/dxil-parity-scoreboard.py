#!/usr/bin/env python3
"""Build an old/new DXIL-to-MSL parity scoreboard.

The script compares old-converter ground-truth MSL with typed-lowering MSL and
Metal compiler errors. It intentionally takes all paths as arguments so the
large shader corpus can stay outside the repository.
"""

from __future__ import annotations

import argparse
import difflib
import json
import re
from pathlib import Path


ERROR_RE = re.compile(r"error: (.*)")


def first_error(err_path: Path) -> str:
    if not err_path.exists():
        return ""
    for line in err_path.read_text(errors="replace").splitlines():
        match = ERROR_RE.search(line)
        if match:
            return match.group(1).strip()
    return ""


def category_for(error: str) -> str:
    if "member reference base type" in error:
        return "scalar-member-access"
    if "device char *" in error or "threadgroup char *" in error:
        return "pointer-resource-leakage"
    if "texture2d" in error and "subscript" in error:
        return "texture-object-misuse"
    if "undeclared identifier" in error:
        return "unresolved-ssa"
    if "no matching constructor" in error:
        return "constructor-mismatch"
    if "call to 'min' is ambiguous" in error or "call to 'max' is ambiguous" in error:
        return "min-max-overload"
    if "contextually convertible to 'bool'" in error:
        return "vector-bool-condition"
    if "pointer to integer" in error or "assigning to 'float' from incompatible type" in error:
        return "pointer-resource-leakage"
    return "other"


def diff_stats(old_path: Path, new_path: Path) -> tuple[int | None, int | None, int | None]:
    if not old_path.exists() or not new_path.exists():
        return None, None, None
    old_lines = old_path.read_text(errors="replace").splitlines()
    new_lines = new_path.read_text(errors="replace").splitlines()
    changed = 0
    for line in difflib.unified_diff(old_lines, new_lines, n=0):
        if line.startswith(("+++", "---", "@@")):
            continue
        if line.startswith(("+", "-")):
            changed += 1
    return len(old_lines), len(new_lines), changed


def build_scoreboard(old_dir: Path, new_dir: Path, error_dir: Path) -> dict:
    rows = []
    for err_path in sorted(error_dir.glob("*.err")):
        error = first_error(err_path)
        if not error:
            continue
        shader_hash = err_path.stem
        old_path = old_dir / f"{shader_hash}.msl"
        new_path = new_dir / f"{shader_hash}.metal"
        old_lines, new_lines, diff_changed = diff_stats(old_path, new_path)
        rows.append(
            {
                "shader": shader_hash,
                "old_status": "pass" if old_path.exists() else "missing",
                "new_status": "metal-fail",
                "category": category_for(error),
                "first_error": error,
                "old_lines": old_lines,
                "new_lines": new_lines,
                "diff_changed_lines": diff_changed,
            }
        )

    categories: dict[str, int] = {}
    for row in rows:
        categories[row["category"]] = categories.get(row["category"], 0) + 1

    return {
        "summary": {
            "new_failures": len(rows),
            "old_ground_truth_available": sum(1 for row in rows if row["old_status"] == "pass"),
            "categories": dict(sorted(categories.items(), key=lambda item: (-item[1], item[0]))),
        },
        "rows": rows,
    }


def write_markdown(scoreboard: dict, output: Path) -> None:
    summary = scoreboard["summary"]
    rows = scoreboard["rows"]
    lines = [
        "# Phase 15 Old/New DXIL-to-MSL Parity Scoreboard",
        "",
        "Generated from the local 766-shader corpus. The old converter remains production ground truth; new typed lowering stays experimental until it reaches parity.",
        "",
        "## Summary",
        "",
        f"- New typed-lowering Metal failures: {summary['new_failures']}",
        f"- Old-converter ground truth available: {summary['old_ground_truth_available']}",
        "",
        "| Category | Count |",
        "|---|---:|",
    ]
    for category, count in summary["categories"].items():
        lines.append(f"| `{category}` | {count} |")
    lines.extend(
        [
            "",
            "## Failing Shaders",
            "",
            "| Shader | Old | New | Category | First Metal Error | Diff Lines |",
            "|---|---|---|---|---|---:|",
        ]
    )
    for row in rows:
        error = row["first_error"].replace("|", "\\|")
        diff_changed = "" if row["diff_changed_lines"] is None else str(row["diff_changed_lines"])
        lines.append(
            f"| `{row['shader']}` | {row['old_status']} | {row['new_status']} | "
            f"`{row['category']}` | {error} | {diff_changed} |"
        )
    lines.append("")
    output.write_text("\n".join(lines))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--old-dir", required=True, type=Path)
    parser.add_argument("--new-dir", required=True, type=Path)
    parser.add_argument("--error-dir", required=True, type=Path)
    parser.add_argument("--markdown", required=True, type=Path)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()

    scoreboard = build_scoreboard(args.old_dir, args.new_dir, args.error_dir)
    args.markdown.parent.mkdir(parents=True, exist_ok=True)
    write_markdown(scoreboard, args.markdown)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(scoreboard, indent=2) + "\n")
    print(
        f"wrote {args.markdown} with {scoreboard['summary']['new_failures']} new failures "
        f"({scoreboard['summary']['old_ground_truth_available']} with old ground truth)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
