#!/usr/bin/env python3
"""Read-only inventory and failure index for M12 shader/PSO corpora."""
from __future__ import annotations

import argparse
import collections
import json
import re
from pathlib import Path
from typing import Any

HASH_RE = re.compile(r"^[0-9a-fA-F]{16}$")
PSO_RE = re.compile(r"^pso-(render|compute)-([0-9a-fA-F]+)\.json$")


def read_text(path: Path, limit: int = 200_000) -> str:
    try:
        data = path.read_bytes()[:limit]
        return data.decode(errors="replace")
    except OSError:
        return ""


def classify_msl_error(text: str) -> str:
    t = text.lower()
    if "implicit conversions between vector types" in t:
        return "msl_vector_type_conversion"
    if "threadgroup" in t and "pointer" in t:
        return "msl_threadgroup_pointer"
    if "int-to-pointer-cast" in t:
        return "msl_int_to_pointer_cast"
    if "address space" in t:
        return "msl_address_space"
    if "no matching function" in t:
        return "msl_no_matching_function"
    if "use of undeclared identifier" in t:
        return "msl_undeclared_identifier"
    if "cannot initialize" in t or "cannot convert" in t:
        return "msl_type_conversion"
    if "program_source" in t and "error:" in t:
        return "msl_compile_error_other"
    return "msl_error_unclassified"


def classify_fail_marker(text: str) -> str:
    t = text.lower()
    for key in [
        "dxil_container_parse",
        "bitcode_parse",
        "dxil_to_msl",
        "metal_library_source",
        "metal_function_lookup",
        "cached_function_lookup",
        "cached_metallib_load",
        "cached_metallib_empty",
        "render_pso",
        "compute_pso",
    ]:
        if key in t:
            return key
    if "mismatch" in t:
        return "mismatch"
    if "unsupported" in t:
        return "unsupported"
    return "fail_unclassified"


def classify_dxil_report(text: str) -> dict[str, Any]:
    unsupported_intrinsics = []
    unsupported_opcodes = []
    diagnostics = []
    for line in text.splitlines():
        low = line.lower()
        if "unsupported intrinsic" in low:
            unsupported_intrinsics.append(line.strip())
        if "unsupported opcode" in low:
            unsupported_opcodes.append(line.strip())
        if "diagnostic" in low or "unsupported" in low:
            diagnostics.append(line.strip())
    return {
        "unsupported_intrinsics": unsupported_intrinsics[:20],
        "unsupported_opcodes": unsupported_opcodes[:20],
        "diagnostics": diagnostics[:40],
    }


def shader_hash_for(path: Path) -> str | None:
    name = path.name
    parts = name.split(".")
    if parts and HASH_RE.match(parts[0]):
        return parts[0].lower()
    return None


def inspect_pso(path: Path) -> dict[str, Any]:
    out: dict[str, Any] = {"path": str(path), "kind": None, "hash": None, "ok": False}
    m = PSO_RE.match(path.name)
    if m:
        out["kind"] = m.group(1)
        out["hash"] = m.group(2).lower()
    try:
        data = json.loads(path.read_text(errors="replace"))
        out["ok"] = True
        out["json_keys"] = sorted(data.keys())[:40] if isinstance(data, dict) else []
        if isinstance(data, dict):
            out["shader_hashes"] = []
            for key in ["shader", "vertex", "fragment", "compute"]:
                value = data.get(key)
                if isinstance(value, dict) and value.get("hash"):
                    out["shader_hashes"].append({"stage": key, "hash": str(value.get("hash"))})
            out["raw_profile"] = data.get("profile")
    except Exception as exc:
        out["error"] = str(exc)
    return out


def inventory_corpus(label: str, root: Path) -> dict[str, Any]:
    files = [p for p in root.rglob("*") if p.is_file()] if root.exists() else []
    by_suffix = collections.Counter()
    shader_hashes: set[str] = set()
    pso_render = []
    pso_compute = []
    msl_errors = []
    fail_markers = []
    dxil_reports = []
    modules = []

    for p in files:
        name = p.name
        suffix = p.suffix.lower()
        if name.endswith(".dxil_report.txt"):
            by_suffix[".dxil_report.txt"] += 1
            h = shader_hash_for(p)
            if h:
                shader_hashes.add(h)
            text = read_text(p)
            cls = classify_dxil_report(text)
            if cls["unsupported_intrinsics"] or cls["unsupported_opcodes"] or cls["diagnostics"]:
                dxil_reports.append({"path": str(p), "hash": h, **cls})
            continue
        if name.endswith(".msl.err.txt"):
            by_suffix[".msl.err.txt"] += 1
            h = shader_hash_for(p)
            if h:
                shader_hashes.add(h)
            text = read_text(p)
            msl_errors.append({
                "path": str(p),
                "hash": h,
                "category": classify_msl_error(text),
                "first_lines": text.splitlines()[:12],
            })
            continue
        if name.endswith(".module.txt"):
            by_suffix[".module.txt"] += 1
            h = shader_hash_for(p)
            if h:
                shader_hashes.add(h)
            modules.append(str(p))
            continue
        if name.endswith(".fail"):
            by_suffix[".fail"] += 1
            h = shader_hash_for(p)
            if h:
                shader_hashes.add(h)
            text = read_text(p)
            fail_markers.append({"path": str(p), "hash": h, "category": classify_fail_marker(text), "first_lines": text.splitlines()[:12]})
            continue
        if PSO_RE.match(name):
            pso = inspect_pso(p)
            if pso.get("kind") == "render":
                pso_render.append(pso)
            else:
                pso_compute.append(pso)
            by_suffix["pso-json"] += 1
            continue
        by_suffix[suffix or "<none>"] += 1
        h = shader_hash_for(p)
        if h:
            shader_hashes.add(h)

    error_categories = collections.Counter(e["category"] for e in msl_errors)
    fail_categories = collections.Counter(e["category"] for e in fail_markers)
    return {
        "label": label,
        "root": str(root),
        "exists": root.exists(),
        "file_count": len(files),
        "counts": {
            "dxbc": by_suffix.get(".dxbc", 0),
            "msl": by_suffix.get(".msl", 0),
            "metallib": by_suffix.get(".metallib", 0),
            "json": by_suffix.get(".json", 0),
            "dxil_reports": by_suffix.get(".dxil_report.txt", 0),
            "module_summaries": by_suffix.get(".module.txt", 0),
            "msl_errors": by_suffix.get(".msl.err.txt", 0),
            "fail_markers": by_suffix.get(".fail", 0),
            "pso_render": len(pso_render),
            "pso_compute": len(pso_compute),
            "unique_shader_hashes": len(shader_hashes),
        },
        "suffix_counts": dict(sorted(by_suffix.items())),
        "msl_error_categories": dict(error_categories.most_common()),
        "fail_marker_categories": dict(fail_categories.most_common()),
        "msl_errors": msl_errors[:500],
        "fail_markers": fail_markers[:500],
        "dxil_report_diagnostics": dxil_reports[:500],
        "pso_render_examples": pso_render[:20],
        "pso_compute_examples": pso_compute[:20],
    }


def write_markdown(report: dict[str, Any], path: Path) -> None:
    lines = ["# M12 corpus inventory", "", f"- corpora: `{len(report['corpora'])}`", ""]
    lines += ["## Summary", "", "| label | exists | files | dxbc | msl | metallib | pso render | pso compute | msl errors | fail markers | unique shaders |", "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for c in report["corpora"]:
        counts = c["counts"]
        lines.append(
            f"| `{c['label']}` | `{c['exists']}` | {c['file_count']} | {counts['dxbc']} | {counts['msl']} | {counts['metallib']} | {counts['pso_render']} | {counts['pso_compute']} | {counts['msl_errors']} | {counts['fail_markers']} | {counts['unique_shader_hashes']} |"
        )
    lines.append("")
    lines += ["## Error categories", ""]
    for c in report["corpora"]:
        lines += [f"### {c['label']}", ""]
        if c["msl_error_categories"]:
            lines.append("MSL errors:")
            for k, v in c["msl_error_categories"].items():
                lines.append(f"- `{k}`: {v}")
        else:
            lines.append("MSL errors: none")
        if c["fail_marker_categories"]:
            lines.append("Fail markers:")
            for k, v in c["fail_marker_categories"].items():
                lines.append(f"- `{k}`: {v}")
        else:
            lines.append("Fail markers: none")
        lines.append("")
    lines += ["## MSL error examples", ""]
    for c in report["corpora"]:
        if not c["msl_errors"]:
            continue
        lines += [f"### {c['label']}", ""]
        for e in c["msl_errors"][:20]:
            lines.append(f"- `{e['hash']}` `{e['category']}` `{e['path']}`")
            for line in e["first_lines"][:4]:
                lines.append(f"  - {line[:240]}")
        lines.append("")
    path.write_text("\n".join(lines) + "\n")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", action="append", default=[], help="LABEL=PATH or PATH")
    ap.add_argument("--results-dir", type=Path, default=Path("tools/d3d12-metal-sdk/results/m12-translation-gauntlet"))
    args = ap.parse_args()

    corpora = []
    for item in args.corpus:
        if "=" in item:
            label, path = item.split("=", 1)
        else:
            path = item
            label = Path(path).name
        corpora.append(inventory_corpus(label, Path(path).expanduser()))

    report = {"schema": "metalsharp.m12.corpus-inventory.v1", "corpora": corpora}
    args.results_dir.mkdir(parents=True, exist_ok=True)
    json_path = args.results_dir / "corpus-inventory.json"
    md_path = args.results_dir / "corpus-inventory.md"
    json_path.write_text(json.dumps(report, indent=2) + "\n")
    write_markdown(report, md_path)
    print(md_path)
    print(json_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
