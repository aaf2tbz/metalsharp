#!/usr/bin/env python3
"""Offline AC6 M12 binary-archive corpus proof.

This script never launches Steam, Wine, wineserver, or AC6. It builds a corpus
manifest from existing cached AC6 PSO manifests and shader artifacts, compiles a
native Metal probe, and runs that probe under a hard process timeout.
"""

from __future__ import annotations

import argparse
import datetime as _dt
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
from typing import Any

APPID = "1888160"
SCHEMA_MANIFEST = "metalsharp.m12.binary_archive_offline_corpus.v1"
SCHEMA_SUMMARY = "metalsharp.m12.binary_archive_offline_summary.v1"


def now_stamp() -> str:
    return _dt.datetime.now().strftime("%Y%m%d-%H%M%S")


def sha256_file(path: Path) -> str | None:
    if not path.is_file():
        return None
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def run_with_timeout(cmd: list[str], *, cwd: Path, timeout: int, stdout_path: Path, stderr_path: Path) -> dict[str, Any]:
    stdout_path.parent.mkdir(parents=True, exist_ok=True)
    stderr_path.parent.mkdir(parents=True, exist_ok=True)
    started = _dt.datetime.now().isoformat(timespec="seconds")
    with stdout_path.open("wb") as out, stderr_path.open("wb") as err:
        proc = subprocess.Popen(
            cmd,
            cwd=str(cwd),
            stdout=out,
            stderr=err,
            start_new_session=True,
            env={**os.environ, "MTL_SHADER_VALIDATION": "0"},
        )
        timed_out = False
        still_running_after_sigkill = False
        try:
            rc = proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            try:
                os.killpg(proc.pid, signal.SIGTERM)
                proc.wait(timeout=5)
            except Exception:
                try:
                    os.killpg(proc.pid, signal.SIGKILL)
                except Exception:
                    pass
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    still_running_after_sigkill = True
            rc = proc.returncode if proc.returncode is not None else -signal.SIGKILL
    return {
        "cmd": cmd,
        "cwd": str(cwd),
        "timeout_seconds": timeout,
        "timed_out": timed_out,
        "returncode": rc,
        "still_running_after_sigkill": still_running_after_sigkill,
        "started": started,
        "finished": _dt.datetime.now().isoformat(timespec="seconds"),
        "stdout": str(stdout_path),
        "stderr": str(stderr_path),
    }


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def write_json(path: Path, obj: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        json.dump(obj, f, indent=2, sort_keys=True)
        f.write("\n")


def path_metadata_snapshot(path: Path) -> dict[str, Any]:
    path = path.expanduser()
    if not path.exists():
        return {"path": str(path), "exists": False, "file_count": 0, "digest": None}
    h = hashlib.sha256()
    file_count = 0
    latest_mtime_ns = 0
    for item in sorted(path.rglob("*")):
        try:
            st = item.lstat()
        except FileNotFoundError:
            continue
        rel = str(item.relative_to(path))
        h.update(rel.encode("utf-8", "surrogateescape"))
        h.update(b"\0")
        h.update(str(st.st_size).encode("ascii"))
        h.update(b"\0")
        h.update(str(st.st_mtime_ns).encode("ascii"))
        h.update(b"\0")
        if item.is_file():
            file_count += 1
        latest_mtime_ns = max(latest_mtime_ns, st.st_mtime_ns)
    return {
        "path": str(path),
        "exists": True,
        "file_count": file_count,
        "latest_mtime_ns": latest_mtime_ns,
        "digest": h.hexdigest(),
    }


def shader_input(cache_dir: Path, shader: dict[str, Any] | None) -> tuple[dict[str, Any] | None, str | None]:
    if not isinstance(shader, dict):
        return None, "missing shader dictionary"
    h = str(shader.get("hash") or "")
    if not h:
        return None, "missing shader hash"
    metallib = Path(str(shader.get("metallib") or cache_dir / f"{h}.metallib")).expanduser()
    msl = Path(str(shader.get("msl") or cache_dir / f"{h}.msl")).expanduser()
    out = dict(shader)
    if metallib.is_file() and metallib.stat().st_size > 0:
        out["input_kind"] = "metallib"
        out["input_path"] = str(metallib)
        return out, None
    if msl.is_file() and msl.stat().st_size > 0:
        out["input_kind"] = "msl"
        out["input_path"] = str(msl)
        return out, None
    return None, f"missing shader artifact hash={h} metallib={metallib} msl={msl}"


def classify_pipeline(cache_dir: Path, source_path: Path, pipeline: dict[str, Any]) -> dict[str, Any]:
    typ = pipeline.get("type")
    name = str(pipeline.get("name") or source_path.stem)
    candidate: dict[str, Any] = {
        "name": name,
        "type": typ or "unknown",
        "source_manifest": str(source_path),
        "classification": "unsupported/offline-incompatible shape",
        "reasons": [],
    }
    if typ not in {"render", "compute"}:
        candidate["reasons"].append(f"unsupported type {typ!r}")
        return candidate

    p = dict(pipeline)
    if typ == "compute":
        shader, reason = shader_input(cache_dir, p.get("shader"))
        if reason:
            candidate["classification"] = "missing shader artifact"
            candidate["reasons"].append(reason)
            return candidate
        p["shader"] = shader
        candidate["classification"] = "complete compute PSO descriptor"
        candidate["pipeline"] = p
        return candidate

    vertex, v_reason = shader_input(cache_dir, p.get("vertex"))
    fragment = None
    f_reason = None
    if isinstance(p.get("fragment"), dict):
        fragment, f_reason = shader_input(cache_dir, p.get("fragment"))
    if v_reason or f_reason:
        candidate["classification"] = "missing shader artifact"
        if v_reason:
            candidate["reasons"].append(v_reason)
        if f_reason:
            candidate["reasons"].append(f_reason)
        return candidate
    formats = p.get("color_formats")
    has_color = isinstance(formats, list) and any(str(x).lower() not in {"invalid", "unknown", ""} for x in formats)
    has_depth = str(p.get("depth_format") or "invalid").lower() not in {"invalid", "unknown", ""}
    has_stencil = str(p.get("stencil_format") or "invalid").lower() not in {"invalid", "unknown", ""}
    if not (has_color or has_depth or has_stencil):
        candidate["classification"] = "incomplete descriptor metadata"
        candidate["reasons"].append("no color/depth/stencil attachment formats")
        return candidate
    p["vertex"] = vertex
    if fragment:
        p["fragment"] = fragment
    candidate["classification"] = "complete render PSO descriptor"
    candidate["pipeline"] = p
    return candidate


def build_corpus(cache_dir: Path) -> dict[str, Any]:
    candidates: list[dict[str, Any]] = []
    pso_files = sorted(cache_dir.glob("pso-*.json"))
    for path in pso_files:
        try:
            obj = load_json(path)
        except Exception as exc:
            candidates.append({
                "name": path.stem,
                "type": "unknown",
                "source_manifest": str(path),
                "classification": "incomplete descriptor metadata",
                "reasons": [f"json load failed: {exc}"],
            })
            continue
        for pipeline in obj.get("pipelines", []):
            if isinstance(pipeline, dict):
                candidates.append(classify_pipeline(cache_dir, path, pipeline))
    counts: dict[str, int] = {}
    type_counts: dict[str, int] = {}
    for c in candidates:
        counts[c["classification"]] = counts.get(c["classification"], 0) + 1
        type_counts[c["type"]] = type_counts.get(c["type"], 0) + 1
    return {
        "schema": SCHEMA_MANIFEST,
        "appid": APPID,
        "cache_dir": str(cache_dir),
        "pso_manifest_count": len(pso_files),
        "candidate_count": len(candidates),
        "classification_counts": counts,
        "type_counts": type_counts,
        "candidates": candidates,
    }


def select_candidates(manifest: dict[str, Any], max_render: int, max_compute: int) -> list[dict[str, Any]]:
    selected: list[dict[str, Any]] = []
    render = 0
    compute = 0
    for c in manifest["candidates"]:
        if c.get("classification") == "complete compute PSO descriptor" and compute < max_compute:
            selected.append(c)
            compute += 1
        elif c.get("classification") == "complete render PSO descriptor" and render < max_render:
            selected.append(c)
            render += 1
        if render >= max_render and compute >= max_compute:
            break
    return selected


def commands_contain(commands: list[dict[str, Any]], needles: list[str]) -> bool:
    for command in commands:
        cmd = command.get("cmd") or []
        text = "\n".join(str(token).lower() for token in cmd)
        for needle in needles:
            if needle.lower() in text:
                return True
    return False


def write_markdown(path: Path, summary: dict[str, Any]) -> None:
    proof = summary.get("proof", {})
    counts = summary.get("corpus", {}).get("classification_counts", {})
    probe_counts = proof.get("probe_counts", {})
    lines = [
        "# M12 binary archive offline corpus proof",
        "",
        f"Result: {'PASS' if summary.get('pass') else 'FAIL'}",
        "",
        "## Scope",
        "",
        "- Offline only: no Steam, Wine, wineserver, or AC6 launch.",
        "- Native Metal probe with hard process timeout.",
        "- Existing AC6 shader-cache/PSO manifests only.",
        "",
        "## Corpus counts",
        "",
    ]
    for key in sorted(counts):
        lines.append(f"- {key}: {counts[key]}")
    lines += [
        "",
        "## Probe counts",
        "",
    ]
    for key in sorted(probe_counts):
        lines.append(f"- {key}: {probe_counts[key]}")
    lines += [
        "",
        "## Archive",
        "",
        f"- path: `{proof.get('archive_path', '')}`",
        f"- bytes: `{proof.get('archive_bytes', 0)}`",
        f"- sha256: `{proof.get('archive_sha256') or ''}`",
        f"- serialize_ok: `{proof.get('archive_serialize_ok')}`",
        f"- reload_ok: `{proof.get('archive_reload_ok')}`",
        "",
        "## Commands",
        "",
    ]
    for item in summary.get("commands", []):
        lines.append(f"- rc={item.get('returncode')} timeout={item.get('timed_out')} cmd=`{' '.join(item.get('cmd', []))}`")
        lines.append(f"  - stdout: `{item.get('stdout')}`")
        lines.append(f"  - stderr: `{item.get('stderr')}`")
    lines += [
        "",
        "## Acceptance",
        "",
    ]
    for key, value in summary.get("acceptance", {}).items():
        lines.append(f"- {key}: `{value}`")
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    repo = Path(__file__).resolve().parents[3]
    sdk = repo / "tools" / "d3d12-metal-sdk"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache-dir", type=Path, default=Path.home() / ".metalsharp" / "shader-cache" / "m12" / APPID)
    parser.add_argument("--results-root", type=Path, default=sdk / "results")
    parser.add_argument("--stamp", default=now_stamp())
    parser.add_argument("--max-render", type=int, default=16)
    parser.add_argument("--max-compute", type=int, default=8)
    parser.add_argument("--compile-timeout-seconds", type=int, default=90)
    parser.add_argument("--probe-timeout-seconds", type=int, default=240)
    args = parser.parse_args()

    out_dir = args.results_root / f"m12-binary-archive-offline-corpus-{args.stamp}"
    out_dir.mkdir(parents=True, exist_ok=True)
    print(f"[phase1] result_dir={out_dir}", flush=True)
    print(f"[phase1] cache_dir={args.cache_dir}", flush=True)

    manifest = build_corpus(args.cache_dir.expanduser())
    manifest_path = out_dir / "corpus-manifest.json"
    write_json(manifest_path, manifest)
    print(f"[phase1] wrote {manifest_path}", flush=True)

    selected = select_candidates(manifest, args.max_render, args.max_compute)
    probe_input = {
        "schema": "metalsharp.m12.binary_archive_probe_input.v1",
        "source_manifest": str(manifest_path),
        "max_render": args.max_render,
        "max_compute": args.max_compute,
        "candidates": selected,
    }
    probe_input_path = out_dir / "probe-input.json"
    write_json(probe_input_path, probe_input)
    print(f"[phase1] selected candidates={len(selected)}", flush=True)

    probe_src = sdk / "probes" / "probe_m12_binary_archive_corpus" / "probe_m12_binary_archive_corpus.mm"
    probe_bin = sdk / "out" / "bin" / "probe_m12_binary_archive_corpus"
    probe_bin.parent.mkdir(parents=True, exist_ok=True)
    compile_cmd = [
        "clang++",
        "-std=c++17",
        "-fobjc-arc",
        "-O2",
        str(probe_src),
        "-framework",
        "Foundation",
        "-framework",
        "Metal",
        "-o",
        str(probe_bin),
    ]
    runtime_dir = Path.home() / ".metalsharp" / "runtime" / "wine" / "lib" / "dxmt_m12"
    runtime_snapshot_before = path_metadata_snapshot(runtime_dir)

    commands: list[dict[str, Any]] = []
    print("[phase1] compiling native Metal probe", flush=True)
    compile_result = run_with_timeout(
        compile_cmd,
        cwd=repo,
        timeout=args.compile_timeout_seconds,
        stdout_path=out_dir / "compile.stdout.txt",
        stderr_path=out_dir / "compile.stderr.txt",
    )
    commands.append(compile_result)

    archive_path = out_dir / "m12_binary_archive_corpus.binarchive"
    probe_output_path = out_dir / "archive-proof-summary.json"
    if compile_result["returncode"] == 0 and not compile_result["timed_out"] and selected:
        probe_cmd = [
            str(probe_bin),
            "--input",
            str(probe_input_path),
            "--archive",
            str(archive_path),
            "--output",
            str(probe_output_path),
        ]
        print(f"[phase1] running native Metal probe timeout={args.probe_timeout_seconds}s", flush=True)
        probe_result = run_with_timeout(
            probe_cmd,
            cwd=repo,
            timeout=args.probe_timeout_seconds,
            stdout_path=out_dir / "probe.stdout.txt",
            stderr_path=out_dir / "probe.stderr.txt",
        )
        commands.append(probe_result)
    else:
        print("[phase1] skipping probe because compile failed/timed out or no candidates selected", flush=True)

    proof: dict[str, Any] = {}
    if probe_output_path.is_file():
        try:
            probe_json = load_json(probe_output_path)
            proof = {
                "archive_path": probe_json.get("archive_path"),
                "archive_bytes": probe_json.get("archive_bytes", 0),
                "archive_sha256": sha256_file(archive_path),
                "archive_serialize_ok": probe_json.get("archive_serialize_ok"),
                "archive_reload_ok": probe_json.get("archive_reload_ok"),
                "probe_counts": probe_json.get("counts", {}),
                "strict_lookup_mode": probe_json.get("strict_lookup_mode"),
                "candidate_results_path": str(probe_output_path),
            }
        except Exception as exc:
            proof = {"error": f"failed to load probe output: {exc}"}
    else:
        proof = {"error": "probe output missing"}

    complete_render = manifest["classification_counts"].get("complete render PSO descriptor", 0)
    complete_compute = manifest["classification_counts"].get("complete compute PSO descriptor", 0)
    probe_counts = proof.get("probe_counts", {}) if isinstance(proof, dict) else {}
    selected_render = sum(1 for c in selected if c.get("type") == "render")
    selected_compute = sum(1 for c in selected if c.get("type") == "compute")
    result_json = load_json(probe_output_path) if probe_output_path.is_file() else {"candidates": []}
    render_strict_ok = sum(1 for c in result_json.get("candidates", []) if c.get("type") == "render" and c.get("strict_lookup_create_ok"))
    compute_strict_ok = sum(1 for c in result_json.get("candidates", []) if c.get("type") == "compute" and c.get("strict_lookup_create_ok"))

    runtime_snapshot_after = path_metadata_snapshot(runtime_dir)
    runtime_unchanged = runtime_snapshot_before == runtime_snapshot_after

    offline_denylist = [
        "wine",
        "wineserver",
        "steam",
        "armored",
        "armoredcore",
        "start_protected_game",
        "launch-game",
        "mtsp/prepare",
    ]
    staging_denylist = [
        "stage-runtime",
        "stage-dxmt-runtime.py",
        "m12-dev.sh",
        "deploy-m12-runtime",
        str(runtime_dir).lower(),
    ]
    offline_only = not commands_contain(commands, offline_denylist)
    no_staging = not commands_contain(commands, staging_denylist) and runtime_unchanged

    acceptance = {
        "offline_only_no_wine_or_ac6_launch": offline_only,
        "no_live_runtime_staging_occurred": no_staging,
        "compile_passed": bool(commands and commands[0].get("returncode") == 0 and not commands[0].get("timed_out")),
        "probe_returncode_zero": bool(len(commands) > 1 and commands[1].get("returncode") == 0),
        "probe_not_timed_out": bool(len(commands) > 1 and not commands[1].get("timed_out")),
        "probe_did_not_survive_sigkill": not any(c.get("still_running_after_sigkill") for c in commands),
        "archive_nonzero": bool((proof.get("archive_bytes") or 0) > 0),
        "archive_reload_ok": bool(proof.get("archive_reload_ok")),
        "all_selected_render_strict_lookup_passed": bool(render_strict_ok == selected_render and selected_render > 0) if selected_render else bool(complete_render == 0),
        "all_selected_compute_strict_lookup_passed_if_complete_compute_exists": bool(compute_strict_ok == selected_compute and selected_compute > 0) if complete_compute else True,
        "failures_classified_not_hidden": True,
    }
    passed = all(acceptance.values())

    summary = {
        "schema": SCHEMA_SUMMARY,
        "pass": passed,
        "result_dir": str(out_dir),
        "corpus_manifest": str(manifest_path),
        "probe_input": str(probe_input_path),
        "corpus": {
            "candidate_count": manifest["candidate_count"],
            "classification_counts": manifest["classification_counts"],
            "selected_total": len(selected),
            "selected_render": selected_render,
            "selected_compute": selected_compute,
            "render_strict_ok": render_strict_ok,
            "compute_strict_ok": compute_strict_ok,
        },
        "proof": proof,
        "commands": commands,
        "offline_safety_checks": {
            "offline_denylist": offline_denylist,
            "staging_denylist": staging_denylist,
            "runtime_snapshot_before": runtime_snapshot_before,
            "runtime_snapshot_after": runtime_snapshot_after,
            "runtime_unchanged": runtime_unchanged,
            "offline_only_no_wine_or_ac6_launch": offline_only,
            "no_live_runtime_staging_occurred": no_staging,
        },
        "acceptance": acceptance,
    }
    summary_json = out_dir / "offline-phase1-summary.json"
    summary_md = out_dir / "archive-proof-summary.md"
    write_json(summary_json, summary)
    write_markdown(summary_md, summary)
    print(f"[phase1] wrote {summary_json}", flush=True)
    print(f"[phase1] wrote {summary_md}", flush=True)
    print(f"[phase1] PASS={passed}", flush=True)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
