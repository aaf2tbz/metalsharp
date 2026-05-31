#!/usr/bin/env python3
"""Audit typed DXIL-to-MSL binding manifests.

The typed lowering emits a compact comment manifest near the top of each MSL
file. This script turns those comments into a repeatable Phase 16A check so the
shader corpus can be tested against Metal's direct binding limits before runtime
PSO/root-signature validation.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


HEADER_RE = re.compile(r"^// metalsharp\.binding_manifest\.v1$")
DIRECT_RE = re.compile(
    r"^// direct_buffers=(?P<buffers>\d+) direct_textures=(?P<textures>\d+) "
    r"direct_samplers=(?P<samplers>\d+)$"
)
RANGE_RE = re.compile(
    r"^// range kind=(?P<kind>\w+) space=(?P<space>\d+) "
    r"lower=(?P<lower>\d+) count=(?P<count>\d+)$"
)
NONE_RE = re.compile(r"^// range none$")


def parse_manifest(path: Path) -> dict:
    manifest = {
        "shader": path.stem,
        "path": str(path),
        "present": False,
        "direct_buffers": None,
        "direct_textures": None,
        "direct_samplers": None,
        "ranges": [],
    }
    lines = path.read_text(errors="replace").splitlines()
    for i, line in enumerate(lines[:128]):
        if not HEADER_RE.match(line.strip()):
            continue
        manifest["present"] = True
        if i + 1 < len(lines):
            match = DIRECT_RE.match(lines[i + 1].strip())
            if match:
                manifest["direct_buffers"] = int(match.group("buffers"))
                manifest["direct_textures"] = int(match.group("textures"))
                manifest["direct_samplers"] = int(match.group("samplers"))
        for range_line in lines[i + 2 : i + 96]:
            stripped = range_line.strip()
            if not stripped:
                break
            if NONE_RE.match(stripped):
                break
            match = RANGE_RE.match(stripped)
            if not match:
                continue
            manifest["ranges"].append(
                {
                    "kind": match.group("kind"),
                    "space": int(match.group("space")),
                    "lower": int(match.group("lower")),
                    "count": int(match.group("count")),
                }
            )
        break
    return manifest


def build_audit(msl_dir: Path, limits: dict[str, int]) -> dict:
    manifests = [parse_manifest(path) for path in sorted(msl_dir.glob("*.metal"))]
    violations = []
    range_counts: dict[str, int] = {}
    range_max_binding: dict[str, int] = {}

    for manifest in manifests:
        shader = manifest["shader"]
        if not manifest["present"]:
            violations.append({"shader": shader, "kind": "missing-manifest"})
            continue
        for key, limit_key in (
            ("direct_buffers", "buffers"),
            ("direct_textures", "textures"),
            ("direct_samplers", "samplers"),
        ):
            value = manifest[key]
            if value is None:
                violations.append({"shader": shader, "kind": f"missing-{key}"})
            elif value > limits[limit_key]:
                violations.append(
                    {
                        "shader": shader,
                        "kind": f"{key}-limit",
                        "value": value,
                        "limit": limits[limit_key],
                    }
                )
        for desc_range in manifest["ranges"]:
            kind = desc_range["kind"]
            range_counts[kind] = range_counts.get(kind, 0) + 1
            high = desc_range["lower"] + desc_range["count"]
            range_max_binding[kind] = max(range_max_binding.get(kind, 0), high)

    present = sum(1 for manifest in manifests if manifest["present"])
    return {
        "summary": {
            "shader_count": len(manifests),
            "manifest_count": present,
            "violation_count": len(violations),
            "limits": limits,
            "range_counts": dict(sorted(range_counts.items())),
            "range_max_binding": dict(sorted(range_max_binding.items())),
        },
        "violations": violations,
        "manifests": manifests,
    }


def write_markdown(audit: dict, output: Path) -> None:
    summary = audit["summary"]
    lines = [
        "# Phase 16A Binding Manifest Audit",
        "",
        "Generated from typed DXIL-to-MSL output. This checks the emitted binding manifest surface before runtime root-signature and PSO validation.",
        "",
        "## Summary",
        "",
        f"- Shader files: {summary['shader_count']}",
        f"- Manifests found: {summary['manifest_count']}",
        f"- Violations: {summary['violation_count']}",
        f"- Direct binding limits: buffers <= {summary['limits']['buffers']}, textures <= {summary['limits']['textures']}, samplers <= {summary['limits']['samplers']}",
        "",
        "| Range Kind | Count | Max Binding End |",
        "|---|---:|---:|",
    ]
    kinds = sorted(set(summary["range_counts"]) | set(summary["range_max_binding"]))
    for kind in kinds:
        lines.append(
            f"| `{kind}` | {summary['range_counts'].get(kind, 0)} | "
            f"{summary['range_max_binding'].get(kind, 0)} |"
        )

    lines.extend(["", "## Violations", ""])
    if audit["violations"]:
        lines.extend(["| Shader | Kind | Value | Limit |", "|---|---|---:|---:|"])
        for violation in audit["violations"]:
            lines.append(
                f"| `{violation['shader']}` | `{violation['kind']}` | "
                f"{violation.get('value', '')} | {violation.get('limit', '')} |"
            )
    else:
        lines.append("None.")

    lines.extend(
        [
            "",
            "## Densest Manifests",
            "",
            "| Shader | Ranges | Buffers | Textures | Samplers |",
            "|---|---:|---:|---:|---:|",
        ]
    )
    densest = sorted(
        audit["manifests"],
        key=lambda row: (len(row["ranges"]), row["shader"]),
        reverse=True,
    )[:25]
    for manifest in densest:
        lines.append(
            f"| `{manifest['shader']}` | {len(manifest['ranges'])} | "
            f"{manifest['direct_buffers'] if manifest['direct_buffers'] is not None else ''} | "
            f"{manifest['direct_textures'] if manifest['direct_textures'] is not None else ''} | "
            f"{manifest['direct_samplers'] if manifest['direct_samplers'] is not None else ''} |"
        )
    lines.append("")
    output.write_text("\n".join(lines))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--msl-dir", required=True, type=Path)
    parser.add_argument("--markdown", required=True, type=Path)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--buffer-limit", type=int, default=31)
    parser.add_argument("--texture-limit", type=int, default=8)
    parser.add_argument("--sampler-limit", type=int, default=16)
    args = parser.parse_args()

    audit = build_audit(
        args.msl_dir,
        {
            "buffers": args.buffer_limit,
            "textures": args.texture_limit,
            "samplers": args.sampler_limit,
        },
    )
    args.markdown.parent.mkdir(parents=True, exist_ok=True)
    write_markdown(audit, args.markdown)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(audit, indent=2) + "\n")
    print(
        f"wrote {args.markdown} with {audit['summary']['manifest_count']} manifests "
        f"and {audit['summary']['violation_count']} violations"
    )
    return 0 if audit["summary"]["violation_count"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
