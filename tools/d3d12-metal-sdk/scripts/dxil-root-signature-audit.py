#!/usr/bin/env python3
"""Compare typed DXIL binding manifests with runtime root-signature reports.

Phase 16A needs a repeatable check between the shader-side binding surface and
the runtime root signature that will feed Metal argument binding. The typed
lowering manifest audit proves each generated MSL file has a compact binding
manifest; this script consumes that JSON plus runtime ``*.dxil_report.txt``
files and reports whether each manifest range is covered by the D3D12 root
signature seen during PSO creation.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


HASH_RE = re.compile(r"^hash=0x(?P<hash>[0-9a-fA-F]+)$")
KIND_RE = re.compile(r"^kind=(?P<kind>\w+)\((?P<value>\d+)\)$")
PRESENT_RE = re.compile(r"^\s+present=(?P<present>[01])$")
PARAMETER_RE = re.compile(
    r"^\s+parameter\[(?P<index>\d+)\] type=(?P<type>\w+) "
    r"visibility=(?P<visibility>\w+) register_space=(?P<space>\d+) "
    r"register=(?P<register>\d+) descriptors=(?P<descriptors>\d+) "
    r"range_type=(?P<range_type>\w+) table_ranges=(?P<table_ranges>\d+)$"
)
RANGE_RE = re.compile(
    r"^\s+range\[(?P<index>\d+)\] type=(?P<type>\w+) space=(?P<space>\d+) "
    r"base=(?P<base>\d+) count=(?P<count>\d+) offset=(?P<offset>\d+)$"
)
STATIC_SAMPLER_RE = re.compile(
    r"^\s+static_sampler\[(?P<index>\d+)\] visibility=(?P<visibility>\w+) "
    r"space=(?P<space>\d+) register=(?P<register>\d+) "
    r"sampler_gpu=0x(?P<sampler_gpu>[0-9a-fA-F]+) "
    r"sampler_cube_gpu=0x(?P<sampler_cube_gpu>[0-9a-fA-F]+) "
    r"lod_bias_bits=0x(?P<lod_bias_bits>[0-9a-fA-F]+)$"
)

UINT32_MAX = 0xFFFFFFFF


def parse_report(path: Path) -> dict:
    report = {
        "path": str(path),
        "hash": path.stem.replace(".dxil_report", ""),
        "kind": "unknown",
        "root_present": False,
        "ranges": [],
        "static_samplers": [],
    }
    in_root = False
    current_visibility = "all"
    for line in path.read_text(errors="replace").splitlines():
        match = HASH_RE.match(line)
        if match:
            report["hash"] = match.group("hash").lower().lstrip("0") or "0"
            continue
        match = KIND_RE.match(line)
        if match:
            report["kind"] = match.group("kind")
            continue
        if line == "root_signature:":
            in_root = True
            continue
        if not in_root:
            continue
        if not line.strip():
            in_root = False
            continue
        match = PRESENT_RE.match(line)
        if match:
            report["root_present"] = match.group("present") == "1"
            continue
        match = PARAMETER_RE.match(line)
        if match:
            current_visibility = match.group("visibility")
            continue
        match = RANGE_RE.match(line)
        if match:
            report["ranges"].append(
                {
                    "kind": match.group("type"),
                    "space": int(match.group("space")),
                    "base": int(match.group("base")),
                    "count": int(match.group("count")),
                    "visibility": current_visibility,
                }
            )
            continue
        match = STATIC_SAMPLER_RE.match(line)
        if match:
            report["static_samplers"].append(
                {
                    "kind": "sampler",
                    "space": int(match.group("space")),
                    "register": int(match.group("register")),
                    "visibility": match.group("visibility"),
                }
            )
            continue
    return report


def load_reports(report_dir: Path) -> dict[str, dict]:
    reports = {}
    for path in sorted(report_dir.glob("*.dxil_report.txt")):
        report = parse_report(path)
        reports[report["hash"]] = report
    return reports


def shader_visibility(kind: str) -> set[str]:
    if kind == "vertex":
        return {"all", "vertex"}
    if kind == "pixel":
        return {"all", "pixel"}
    if kind == "geometry":
        return {"all", "geometry"}
    if kind == "hull":
        return {"all", "hull"}
    if kind == "domain":
        return {"all", "domain"}
    return {"all"}


def interval_covers(base: int, count: int, lower: int, needed: int) -> bool:
    if base > lower:
        return False
    if count == UINT32_MAX:
        return True
    return lower + needed <= base + count


def range_covered(manifest_range: dict, report: dict) -> bool:
    expected_visibility = shader_visibility(report["kind"])
    lower = manifest_range["lower"]
    count = manifest_range["count"]
    kind = manifest_range["kind"]
    space = manifest_range["space"]

    for root_range in report["ranges"]:
        if root_range["kind"] != kind or root_range["space"] != space:
            continue
        if root_range["visibility"] not in expected_visibility:
            continue
        if interval_covers(root_range["base"], root_range["count"], lower, count):
            return True

    if kind != "sampler":
        return False

    covered = set()
    for sampler in report["static_samplers"]:
        if sampler["space"] != space:
            continue
        if sampler["visibility"] not in expected_visibility:
            continue
        register = sampler["register"]
        if lower <= register < lower + count:
            covered.add(register)
    return len(covered) == count


def normalize_hash(shader: str) -> str:
    return shader.lower().removeprefix("0x").lstrip("0") or "0"


def build_audit(manifest_audit: dict, reports: dict[str, dict]) -> dict:
    violations = []
    compared = 0
    root_present = 0
    missing_reports = 0
    coverage_by_kind: dict[str, int] = {}

    for manifest in manifest_audit.get("manifests", []):
        shader = manifest["shader"]
        report = reports.get(normalize_hash(shader))
        if not report:
            missing_reports += 1
            violations.append({"shader": shader, "kind": "missing-report"})
            continue
        compared += 1
        if not report["root_present"]:
            violations.append(
                {
                    "shader": shader,
                    "kind": "missing-root-signature",
                    "report": report["path"],
                }
            )
            continue
        root_present += 1
        for manifest_range in manifest.get("ranges", []):
            if range_covered(manifest_range, report):
                coverage_by_kind[manifest_range["kind"]] = (
                    coverage_by_kind.get(manifest_range["kind"], 0) + 1
                )
                continue
            violations.append(
                {
                    "shader": shader,
                    "kind": "uncovered-range",
                    "range_kind": manifest_range["kind"],
                    "space": manifest_range["space"],
                    "lower": manifest_range["lower"],
                    "count": manifest_range["count"],
                    "report": report["path"],
                }
            )

    return {
        "summary": {
            "manifest_count": len(manifest_audit.get("manifests", [])),
            "report_count": len(reports),
            "compared_shader_count": compared,
            "root_signature_present_count": root_present,
            "missing_report_count": missing_reports,
            "violation_count": len(violations),
            "covered_ranges_by_kind": dict(sorted(coverage_by_kind.items())),
        },
        "violations": violations,
    }


def write_markdown(audit: dict, output: Path) -> None:
    summary = audit["summary"]
    lines = [
        "# Phase 16A Root Signature ABI Audit",
        "",
        "Generated from typed binding manifests and runtime DXIL compile reports. A shader passes this audit only when every manifest range is covered by its runtime root signature.",
        "",
        "## Summary",
        "",
        f"- Manifest shaders: {summary['manifest_count']}",
        f"- Runtime reports: {summary['report_count']}",
        f"- Compared shaders: {summary['compared_shader_count']}",
        f"- Reports with root signatures: {summary['root_signature_present_count']}",
        f"- Missing reports: {summary['missing_report_count']}",
        f"- Violations: {summary['violation_count']}",
        "",
        "| Covered Range Kind | Count |",
        "|---|---:|",
    ]
    for kind, count in summary["covered_ranges_by_kind"].items():
        lines.append(f"| `{kind}` | {count} |")
    if not summary["covered_ranges_by_kind"]:
        lines.append("| none | 0 |")

    lines.extend(["", "## Violations", ""])
    if audit["violations"]:
        lines.extend(
            [
                "| Shader | Kind | Range | Report |",
                "|---|---|---|---|",
            ]
        )
        for violation in audit["violations"][:200]:
            range_text = ""
            if violation["kind"] == "uncovered-range":
                range_text = (
                    f"{violation['range_kind']} space={violation['space']} "
                    f"lower={violation['lower']} count={violation['count']}"
                )
            report = violation.get("report", "")
            lines.append(
                f"| `{violation['shader']}` | `{violation['kind']}` | "
                f"{range_text} | `{report}` |"
            )
        if len(audit["violations"]) > 200:
            lines.append(
                f"| ... | ... | ... | {len(audit['violations']) - 200} more violations omitted |"
            )
    else:
        lines.append("None.")
    lines.append("")
    output.write_text("\n".join(lines))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest-json", required=True, type=Path)
    parser.add_argument("--report-dir", required=True, type=Path)
    parser.add_argument("--markdown", required=True, type=Path)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()

    manifest_audit = json.loads(args.manifest_json.read_text())
    reports = load_reports(args.report_dir)
    audit = build_audit(manifest_audit, reports)

    args.markdown.parent.mkdir(parents=True, exist_ok=True)
    write_markdown(audit, args.markdown)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(audit, indent=2) + "\n")

    print(
        f"wrote {args.markdown} with {audit['summary']['compared_shader_count']} "
        f"compared shaders and {audit['summary']['violation_count']} violations"
    )
    return 0 if audit["summary"]["violation_count"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
