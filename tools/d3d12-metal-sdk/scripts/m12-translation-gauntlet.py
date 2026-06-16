#!/usr/bin/env python3
"""Phase 3 M12 DXIL/MSL translation gauntlet.

Read-only by default for live shader caches. Optional Metal repro compiles write
only to the results directory so live caches remain untouched.
"""
from __future__ import annotations

import argparse
import collections
import json
import os
import re
import shutil
import subprocess
from pathlib import Path
from typing import Any

HASH_RE = re.compile(r"^[0-9a-fA-F]{16}$")
PSO_RE = re.compile(r"^pso-(render|compute)-([0-9a-fA-F]+)\.json$")
ERROR_LINE_RE = re.compile(r"program_source:(\d+):(\d+):\s*(error|warning):\s*(.*)")

DEFAULT_CORPORA = {
    "elden-ring-live": Path.home() / ".metalsharp/shader-cache/m12/1245620",
    "subnautica-2-live": Path.home() / ".metalsharp/shader-cache/m12/1962700",
    "schedule-1-live": Path.home() / ".metalsharp/shader-cache/m12/3164500",
    "peak-live": Path.home() / ".metalsharp/shader-cache/m12/3527290",
}


def read_text(path: Path, limit: int = 1_000_000) -> str:
    try:
        return path.read_bytes()[:limit].decode(errors="replace")
    except OSError:
        return ""


def shader_hash_for(path: Path) -> str | None:
    first = path.name.split(".", 1)[0]
    return first.lower() if HASH_RE.match(first) else None


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
    if "atomic" in t and "pointer" in t:
        return "msl_atomic_pointer"
    if "cannot convert between vector and non-scalar" in t:
        return "msl_vector_scalar_conversion"
    if "assigning to 'threadgroup" in t:
        return "msl_threadgroup_assignment"
    if "use of undeclared identifier" in t:
        return "msl_undeclared_identifier"
    if "no matching function" in t:
        return "msl_no_matching_function"
    if "cannot initialize" in t or "cannot convert" in t or "incompatible type" in t:
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
    if "unsupported" in t:
        return "unsupported"
    if "mismatch" in t:
        return "mismatch"
    return "fail_unclassified"


def parse_error_lines(text: str, limit: int = 20) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for line in text.splitlines():
        m = ERROR_LINE_RE.search(line)
        if not m:
            continue
        rows.append({
            "line": int(m.group(1)),
            "column": int(m.group(2)),
            "severity": m.group(3),
            "message": m.group(4).strip(),
        })
        if len(rows) >= limit:
            break
    return rows


def parse_dxil_report(path: Path) -> dict[str, Any]:
    text = read_text(path)
    diagnostics = []
    unsupported_intrinsics = []
    unsupported_opcodes = []
    stage = None
    entry = None
    for line in text.splitlines():
        stripped = line.strip()
        low = stripped.lower()
        if low.startswith("function=") or low.startswith("entry="):
            entry = stripped.split("=", 1)[-1]
        if "stage" in low and "=" in stripped and not stage:
            k, v = stripped.split("=", 1)
            if k.strip().lower() in {"stage", "shader_stage"}:
                stage = v.strip()
        if "unsupported intrinsic" in low:
            unsupported_intrinsics.append(stripped)
        if "unsupported opcode" in low:
            unsupported_opcodes.append(stripped)
        if "diagnostic" in low or "unsupported" in low or "error" in low:
            diagnostics.append(stripped)
    return {
        "exists": path.exists(),
        "stage": stage,
        "entry": entry,
        "unsupported_intrinsics": unsupported_intrinsics[:40],
        "unsupported_opcodes": unsupported_opcodes[:40],
        "diagnostics": diagnostics[:80],
    }


def inspect_pso(path: Path) -> list[dict[str, Any]]:
    try:
        data = json.loads(path.read_text(errors="replace"))
    except Exception:
        return []
    pipelines = data.get("pipelines") if isinstance(data, dict) else None
    if not isinstance(pipelines, list):
        pipelines = [data] if isinstance(data, dict) else []
    rows = []
    for p in pipelines:
        if not isinstance(p, dict):
            continue
        d3d12 = p.get("d3d12") if isinstance(p.get("d3d12"), dict) else {}
        shader = p.get("shader") if isinstance(p.get("shader"), dict) else {}
        hashes = []
        for key in ["cs_hash", "vs_hash", "ps_hash", "gs_hash", "ds_hash", "hs_hash"]:
            value = d3d12.get(key)
            if isinstance(value, str) and HASH_RE.match(value):
                hashes.append({"slot": key, "hash": value.lower()})
        value = shader.get("hash")
        if isinstance(value, str) and HASH_RE.match(value):
            hashes.append({"slot": "shader.hash", "hash": value.lower()})
        rows.append({
            "path": str(path),
            "type": p.get("type"),
            "name": p.get("name"),
            "hashes": hashes,
            "threadgroup_size": p.get("threadgroup_size"),
        })
    return rows


def compile_msl(source: Path, out_dir: Path, metal_tool: str, metallib_tool: str) -> dict[str, Any]:
    h = shader_hash_for(source) or source.stem
    air = out_dir / "air" / f"{h}.air"
    metallib = out_dir / "metallib" / f"{h}.metallib"
    log = out_dir / "logs" / f"{h}.log"
    for p in [air.parent, metallib.parent, log.parent]:
        p.mkdir(parents=True, exist_ok=True)
    metal_cmd = [
        metal_tool,
        "-x", "metal",
        "-std=metal3.0",
        "-Wno-unused-variable",
        "-Wno-unused-function",
        "-Wno-implicit-function-declaration",
        "-Wno-incompatible-pointer-types",
        "-Wno-int-conversion",
        "-c", str(source),
        "-o", str(air),
    ]
    metal = subprocess.run(metal_cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    metallib_result = None
    ok = metal.returncode == 0 and air.exists() and air.stat().st_size > 0
    metallib_cmd = [metallib_tool, str(air), "-o", str(metallib)]
    if ok:
        metallib_result = subprocess.run(metallib_cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        ok = metallib_result.returncode == 0 and metallib.exists() and metallib.stat().st_size > 0
    log.write_text(
        "metal_command=" + " ".join(metal_cmd) + "\n"
        f"metal_returncode={metal.returncode}\n"
        "metal_stdout:\n" + metal.stdout + "\n"
        "metal_stderr:\n" + metal.stderr + "\n"
        "metallib_command=" + " ".join(metallib_cmd) + "\n"
        f"metallib_returncode={metallib_result.returncode if metallib_result else ''}\n"
        "metallib_stdout:\n" + (metallib_result.stdout if metallib_result else "") + "\n"
        "metallib_stderr:\n" + (metallib_result.stderr if metallib_result else "") + "\n"
    )
    return {
        "hash": h,
        "source": str(source),
        "ok": ok,
        "metal_returncode": metal.returncode,
        "metallib_returncode": metallib_result.returncode if metallib_result else None,
        "air": str(air),
        "metallib": str(metallib),
        "log": str(log),
        "stderr_tail": (metal.stderr + (metallib_result.stderr if metallib_result else ""))[-4000:],
    }


def scan_corpus(label: str, root: Path) -> dict[str, Any]:
    files = [p for p in root.rglob("*") if p.is_file()] if root.exists() else []
    suffix_counts = collections.Counter()
    pso_by_hash: dict[str, list[dict[str, Any]]] = collections.defaultdict(list)
    pso_rows = []
    for path in files:
        if path.name.endswith(".dxil_report.txt"):
            suffix_counts[".dxil_report.txt"] += 1
        elif path.name.endswith(".msl.err.txt"):
            suffix_counts[".msl.err.txt"] += 1
        elif path.name.endswith(".module.txt"):
            suffix_counts[".module.txt"] += 1
        elif path.name.endswith(".fail"):
            suffix_counts[".fail"] += 1
        elif PSO_RE.match(path.name):
            suffix_counts["pso-json"] += 1
            rows = inspect_pso(path)
            pso_rows.extend(rows)
            for row in rows:
                for hv in row.get("hashes", []):
                    pso_by_hash[hv["hash"]].append({k: row.get(k) for k in ["path", "type", "name", "threadgroup_size"]})
        else:
            suffix_counts[path.suffix.lower() or "<none>"] += 1

    msl_errors = []
    for err_path in sorted(root.rglob("*.msl.err.txt")) if root.exists() else []:
        h = shader_hash_for(err_path)
        text = read_text(err_path)
        msl_path = root / f"{h}.msl" if h else err_path.with_suffix("")
        dxbc_path = root / f"{h}.dxbc" if h else err_path.with_suffix(".dxbc")
        report_path = root / f"{h}.dxil_report.txt" if h else err_path.with_suffix(".dxil_report.txt")
        msl_errors.append({
            "hash": h,
            "path": str(err_path),
            "category": classify_msl_error(text),
            "error_lines": parse_error_lines(text),
            "has_msl": msl_path.exists(),
            "msl": str(msl_path),
            "has_dxbc": dxbc_path.exists(),
            "dxbc": str(dxbc_path),
            "has_dxil_report": report_path.exists(),
            "dxil_report": str(report_path),
            "dxil": parse_dxil_report(report_path) if report_path.exists() else {"exists": False},
            "pso_usage": pso_by_hash.get(h or "", [])[:20],
            "first_lines": text.splitlines()[:10],
        })

    fail_markers = []
    for fail_path in sorted(root.rglob("*.fail")) if root.exists() else []:
        text = read_text(fail_path)
        fail_markers.append({
            "hash": shader_hash_for(fail_path),
            "path": str(fail_path),
            "category": classify_fail_marker(text),
            "first_lines": text.splitlines()[:10],
        })

    shader_hashes = {h for p in files if (h := shader_hash_for(p))}
    return {
        "label": label,
        "root": str(root),
        "exists": root.exists(),
        "file_count": len(files),
        "counts": {
            "dxbc": suffix_counts.get(".dxbc", 0),
            "msl": suffix_counts.get(".msl", 0),
            "metallib": suffix_counts.get(".metallib", 0),
            "json": suffix_counts.get(".json", 0),
            "dxil_reports": suffix_counts.get(".dxil_report.txt", 0),
            "module_summaries": suffix_counts.get(".module.txt", 0),
            "msl_errors": suffix_counts.get(".msl.err.txt", 0),
            "fail_markers": suffix_counts.get(".fail", 0),
            "pso_json": suffix_counts.get("pso-json", 0),
            "unique_shader_hashes": len(shader_hashes),
        },
        "suffix_counts": dict(sorted(suffix_counts.items())),
        "msl_error_categories": dict(collections.Counter(e["category"] for e in msl_errors).most_common()),
        "fail_marker_categories": dict(collections.Counter(e["category"] for e in fail_markers).most_common()),
        "msl_errors": msl_errors,
        "fail_markers": fail_markers,
        "pso_count": len(pso_rows),
    }


def write_markdown(report: dict[str, Any], path: Path) -> None:
    lines = ["# M12 Phase 3 translation gauntlet", "", f"- corpora: `{len(report['corpora'])}`", f"- repro enabled: `{report['repro']['enabled']}`", ""]
    lines += ["## Summary", "", "| corpus | exists | files | dxbc | msl | metallib | dxil reports | msl errors | fail markers | pso json | unique shaders |", "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for c in report["corpora"]:
        counts = c["counts"]
        lines.append(f"| `{c['label']}` | `{c['exists']}` | {c['file_count']} | {counts['dxbc']} | {counts['msl']} | {counts['metallib']} | {counts['dxil_reports']} | {counts['msl_errors']} | {counts['fail_markers']} | {counts['pso_json']} | {counts['unique_shader_hashes']} |")
    lines.append("")
    lines += ["## MSL error categories", ""]
    for c in report["corpora"]:
        lines += [f"### {c['label']}", ""]
        if c["msl_error_categories"]:
            for k, v in c["msl_error_categories"].items():
                lines.append(f"- `{k}`: {v}")
        else:
            lines.append("- none")
        lines.append("")
    lines += ["## Top MSL failures", ""]
    for c in report["corpora"]:
        if not c["msl_errors"]:
            continue
        lines += [f"### {c['label']}", ""]
        for e in c["msl_errors"][:30]:
            usage = ", ".join(sorted({u.get("type") or "?" for u in e.get("pso_usage", [])})) or "no-pso-json-link"
            first = e["error_lines"][0]["message"] if e.get("error_lines") else (e["first_lines"][0] if e.get("first_lines") else "")
            lines.append(f"- `{e['hash']}` `{e['category']}` pso=`{usage}` msl=`{e['has_msl']}` dxbc=`{e['has_dxbc']}` report=`{e['has_dxil_report']}`")
            if first:
                lines.append(f"  - {first[:240]}")
        lines.append("")
    if report["repro"]["enabled"]:
        lines += ["## Scratch Metal repro", "", f"- attempted: `{report['repro']['attempted']}`", f"- failed: `{report['repro']['failed']}`", ""]
        for r in report["repro"].get("results", [])[:40]:
            lines.append(f"- `{r['hash']}` ok=`{r['ok']}` log=`{r['log']}`")
    path.write_text("\n".join(lines) + "\n")


def parse_corpus_args(items: list[str]) -> list[tuple[str, Path]]:
    if not items:
        return list(DEFAULT_CORPORA.items())
    out = []
    for item in items:
        if "=" in item:
            label, raw = item.split("=", 1)
        else:
            raw = item
            label = Path(raw).name
        out.append((label, Path(raw).expanduser()))
    return out


def resolve_xcrun_tool(name: str) -> str:
    found = shutil.which(name)
    if found:
        return found
    xcrun = shutil.which("xcrun")
    if not xcrun:
        return name
    proc = subprocess.run([xcrun, "-f", name], text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    return proc.stdout.strip() if proc.returncode == 0 and proc.stdout.strip() else name


def main() -> int:
    ap = argparse.ArgumentParser(description="Run read-only M12 DXIL/MSL translation gauntlet over shader corpora.")
    ap.add_argument("--corpus", action="append", default=[], help="LABEL=PATH or PATH. Defaults to four live M12 caches.")
    ap.add_argument("--results-dir", type=Path, default=Path("tools/d3d12-metal-sdk/results/m12-translation-gauntlet") / "phase3")
    ap.add_argument("--repro-metal", action="store_true", help="Recompile failed .msl sources to scratch results dir only.")
    ap.add_argument("--repro-limit", type=int, default=40, help="Max failed MSL sources to repro per full run.")
    ap.add_argument("--focus", default="subnautica-2-live", help="Preferred corpus label for repro ordering.")
    args = ap.parse_args()

    args.results_dir.mkdir(parents=True, exist_ok=True)
    corpora = [scan_corpus(label, root) for label, root in parse_corpus_args(args.corpus)]
    repro: dict[str, Any] = {"enabled": bool(args.repro_metal), "attempted": 0, "failed": 0, "results": []}
    if args.repro_metal:
        metal_tool = resolve_xcrun_tool("metal")
        metallib_tool = resolve_xcrun_tool("metallib")
        ordered = sorted(corpora, key=lambda c: 0 if c["label"] == args.focus else 1)
        remaining = args.repro_limit
        for corpus in ordered:
            if remaining <= 0:
                break
            for err in corpus["msl_errors"]:
                if remaining <= 0:
                    break
                if not err.get("has_msl"):
                    continue
                result = compile_msl(Path(err["msl"]), args.results_dir / "scratch-metal-repro" / corpus["label"], metal_tool, metallib_tool)
                result["corpus"] = corpus["label"]
                result["runtime_category"] = err["category"]
                repro["results"].append(result)
                repro["attempted"] += 1
                repro["failed"] += 0 if result["ok"] else 1
                remaining -= 1

    report = {"schema": "metalsharp.m12.translation-gauntlet.v1", "corpora": corpora, "repro": repro}
    json_path = args.results_dir / "translation-gauntlet.json"
    md_path = args.results_dir / "translation-gauntlet.md"
    json_path.write_text(json.dumps(report, indent=2) + "\n")
    write_markdown(report, md_path)
    print(md_path)
    print(json_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
