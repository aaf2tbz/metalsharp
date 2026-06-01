#!/usr/bin/env python3
"""Audit captured D3D12 compute PSO manifests for Phase 16C readiness."""

from __future__ import annotations

import argparse
import json
import re
from collections import Counter
from pathlib import Path


THREADGROUP_RE = re.compile(r"^threadgroup_size=(?P<x>\d+),(?P<y>\d+),(?P<z>\d+)$")
UNSUPPORTED_INTRINSICS_RE = re.compile(r"^unsupported_intrinsics=(?P<count>\d+)$")
UNSUPPORTED_OPCODES_RE = re.compile(r"^unsupported_opcodes=(?P<count>\d+)$")


def load_first_json_object(path: Path) -> tuple[dict | None, str, str]:
    text = path.read_text(errors="replace")
    decoder = json.JSONDecoder()
    try:
        data, end = decoder.raw_decode(text)
    except json.JSONDecodeError as exc:
        return None, "", str(exc)
    return data, text[end:].strip(), ""


def parse_dxil_report(path: Path) -> dict:
    report = {
        "path": str(path),
        "present": path.exists(),
        "threadgroup_size": None,
        "unsupported_intrinsics": None,
        "unsupported_opcodes": None,
    }
    if not path.exists():
        return report
    for line in path.read_text(errors="replace").splitlines():
        match = THREADGROUP_RE.match(line)
        if match:
            report["threadgroup_size"] = [
                int(match.group("x")),
                int(match.group("y")),
                int(match.group("z")),
            ]
            continue
        match = UNSUPPORTED_INTRINSICS_RE.match(line)
        if match:
            report["unsupported_intrinsics"] = int(match.group("count"))
            continue
        match = UNSUPPORTED_OPCODES_RE.match(line)
        if match:
            report["unsupported_opcodes"] = int(match.group("count"))
            continue
    return report


def audit_manifest(path: Path, report_dir: Path) -> tuple[dict, list[dict], list[dict]]:
    row = {
        "path": str(path),
        "name": path.stem,
        "type": "",
        "cs_hash": "",
        "cs_bytes": 0,
        "threadgroup_size": [],
        "report_threadgroup_size": None,
        "has_compute_function": False,
        "has_report": False,
        "malformed_tail": False,
    }
    violations: list[dict] = []
    warnings: list[dict] = []

    data, malformed_tail, json_error = load_first_json_object(path)
    if json_error:
        violations.append({"manifest": str(path), "kind": "malformed-json", "detail": json_error})
        return row, violations, warnings
    if malformed_tail:
        row["malformed_tail"] = True
        warnings.append({"manifest": str(path), "kind": "malformed-tail", "detail": malformed_tail[:120]})

    pipelines = data.get("pipelines") if isinstance(data, dict) else None
    if not pipelines:
        violations.append({"manifest": str(path), "kind": "missing-pipeline", "detail": ""})
        return row, violations, warnings

    pipeline = pipelines[0]
    d3d12 = pipeline.get("d3d12") or {}
    metal = pipeline.get("metal") or {}
    shader = pipeline.get("shader") or {}
    threadgroup_size = pipeline.get("threadgroup_size") or []
    row.update(
        {
            "name": pipeline.get("name", path.stem),
            "type": pipeline.get("type", ""),
            "cs_hash": d3d12.get("cs_hash", ""),
            "cs_bytes": int(d3d12.get("cs_bytes") or 0),
            "threadgroup_size": threadgroup_size,
            "has_compute_function": bool(metal.get("compute_function", 0)),
        }
    )

    def add(kind: str, detail: str) -> None:
        violations.append({"manifest": str(path), "name": row["name"], "kind": kind, "detail": detail})

    def warn(kind: str, detail: str) -> None:
        warnings.append({"manifest": str(path), "name": row["name"], "kind": kind, "detail": detail})

    if row["type"] != "compute":
        add("not-compute-pso", f"type={row['type']}")
    if not row["cs_hash"]:
        add("missing-cs-hash", "D3D12 compute shader hash is empty")
    if row["cs_bytes"] <= 0:
        add("missing-cs-bytecode", f"cs_bytes={row['cs_bytes']}")
    if not shader:
        add("missing-shader-object", "manifest has no shader object")
    else:
        if not shader.get("metallib"):
            add("missing-metallib", "shader.metallib is empty")
        if not shader.get("function"):
            add("missing-function-name", "shader.function is empty")
    if not row["has_compute_function"]:
        add("missing-metal-compute-function", "metal.compute_function is zero or absent")

    if not isinstance(threadgroup_size, list) or len(threadgroup_size) != 3:
        add("invalid-threadgroup-size", f"threadgroup_size={threadgroup_size}")
    else:
        normalized = [int(value) for value in threadgroup_size]
        row["threadgroup_size"] = normalized
        if any(value <= 0 for value in normalized):
            add("invalid-threadgroup-size", f"threadgroup_size={normalized}")

    if row["cs_hash"]:
        report = parse_dxil_report(report_dir / f"{row['cs_hash']}.dxil_report.txt")
        row["has_report"] = report["present"]
        row["report_threadgroup_size"] = report["threadgroup_size"]
        if not report["present"]:
            warn("missing-dxil-report", row["cs_hash"])
        else:
            if report["threadgroup_size"] and row["threadgroup_size"]:
                if report["threadgroup_size"] != row["threadgroup_size"]:
                    add(
                        "threadgroup-size-mismatch",
                        f"manifest={row['threadgroup_size']} report={report['threadgroup_size']}",
                    )
            if report["unsupported_intrinsics"]:
                add("unsupported-intrinsics", str(report["unsupported_intrinsics"]))
            if report["unsupported_opcodes"]:
                add("unsupported-opcodes", str(report["unsupported_opcodes"]))

    return row, violations, warnings


def build_audit(corpus: Path, report_dir: Path) -> dict:
    rows = []
    violations = []
    warnings = []
    for path in sorted(corpus.glob("pso-compute-*.json")):
        row, row_violations, row_warnings = audit_manifest(path, report_dir)
        rows.append(row)
        violations.extend(row_violations)
        warnings.extend(row_warnings)

    violation_counts = Counter(item["kind"] for item in violations)
    warning_counts = Counter(item["kind"] for item in warnings)
    threadgroup_counts = Counter(tuple(row["threadgroup_size"]) for row in rows)
    with_reports = sum(1 for row in rows if row["has_report"])

    return {
        "summary": {
            "compute_pso_count": len(rows),
            "dxil_report_count": with_reports,
            "violation_count": len(violations),
            "warning_count": len(warnings),
            "threadgroup_size_counts": [
                {"threadgroup_size": list(size), "count": count}
                for size, count in sorted(threadgroup_counts.items())
            ],
            "violation_counts": dict(sorted(violation_counts.items())),
            "warning_counts": dict(sorted(warning_counts.items())),
        },
        "violations": violations,
        "warnings": warnings,
        "rows": rows,
    }


def write_markdown(audit: dict, output: Path) -> None:
    summary = audit["summary"]
    lines = [
        "# Phase 16C Compute PSO Manifest Audit",
        "",
        "Generated from captured `pso-compute-*.json` manifests and matching DXIL compile reports. This checks compute shader presence, Metal function capture, threadgroup size, and unsupported DXIL diagnostics.",
        "",
        "## Summary",
        "",
        f"- Compute PSOs: {summary['compute_pso_count']}",
        f"- Matching DXIL reports: {summary['dxil_report_count']}",
        f"- Violations: {summary['violation_count']}",
        f"- Warnings: {summary['warning_count']}",
        "",
        "## Threadgroup Sizes",
        "",
    ]
    for entry in summary["threadgroup_size_counts"]:
        size = ",".join(str(value) for value in entry["threadgroup_size"])
        lines.append(f"- `{size}`: `{entry['count']}`")

    lines.extend(["", "## Violation Counts", ""])
    if summary["violation_counts"]:
        for kind, count in summary["violation_counts"].items():
            lines.append(f"- `{kind}`: {count}")
    else:
        lines.append("None.")

    lines.extend(["", "## Warning Counts", ""])
    if summary["warning_counts"]:
        for kind, count in summary["warning_counts"].items():
            lines.append(f"- `{kind}`: {count}")
    else:
        lines.append("None.")

    lines.extend(["", "## First Violations", ""])
    if audit["violations"]:
        lines.extend(["| Manifest | Kind | Detail |", "|---|---|---|"])
        for violation in audit["violations"][:100]:
            detail = violation.get("detail", "").replace("|", "\\|")
            lines.append(
                f"| `{Path(violation['manifest']).name}` | `{violation['kind']}` | {detail} |"
            )
        if len(audit["violations"]) > 100:
            lines.append(f"| ... | ... | {len(audit['violations']) - 100} more omitted |")
    else:
        lines.append("None.")
    lines.append("")
    output.write_text("\n".join(lines))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", required=True, type=Path)
    parser.add_argument("--report-dir", type=Path)
    parser.add_argument("--markdown", required=True, type=Path)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()

    report_dir = args.report_dir or args.corpus
    audit = build_audit(args.corpus, report_dir)
    args.markdown.parent.mkdir(parents=True, exist_ok=True)
    write_markdown(audit, args.markdown)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(audit, indent=2) + "\n")
    print(
        f"wrote {args.markdown} with {audit['summary']['compute_pso_count']} compute PSOs "
        f"and {audit['summary']['violation_count']} violations"
    )
    return 0 if audit["summary"]["violation_count"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
