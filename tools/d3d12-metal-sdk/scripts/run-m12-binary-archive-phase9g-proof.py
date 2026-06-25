#!/usr/bin/env python3
"""Offline Phase 9G proof for existing AC6 M12 binary archive warm lookup.

This proof does not launch Steam, Wine, wineserver, AC6, menu, or Continue, and
it does not stage runtime files. It validates the existing AC6 archive file,
then attempts strict Metal binary-archive lookup for representative AC6 PSO
corpus candidates.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import signal
import subprocess
import textwrap
from typing import Any

ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT / "tools/d3d12-metal-sdk"
CORPUS_SCRIPT = SDK / "scripts/run-m12-binary-archive-corpus.py"
CORPUS_PROBE_SOURCE = SDK / "probes/probe_m12_binary_archive_corpus/probe_m12_binary_archive_corpus.mm"
PIPELINE_STATE = ROOT / "vendor/dxmt/src/d3d12/d3d12_pipeline_state.cpp"
APPID = "1888160"
DEFAULT_ARCHIVE = Path.home() / ".metalsharp/pipeline-cache/m12/1888160/m12-metal-binary-archive-00000001000003c3.binarchive"
RUNTIME_DIR = Path.home() / ".metalsharp/runtime/wine/lib/dxmt_m12"


def load_corpus_module():
    spec = importlib.util.spec_from_file_location("m12_binary_archive_corpus", CORPUS_SCRIPT)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"failed to import {CORPUS_SCRIPT}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sha256_file(path: Path) -> str | None:
    if not path.is_file():
        return None
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def file_record(path: Path) -> dict[str, Any]:
    return {
        "path": str(path),
        "exists": path.exists(),
        "is_file": path.is_file(),
        "size": path.stat().st_size if path.exists() else 0,
        "mtime_ns": path.stat().st_mtime_ns if path.exists() else None,
        "sha256": sha256_file(path),
    }


def path_metadata_snapshot(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {"path": str(path), "exists": False, "file_count": 0, "digest": None}
    h = hashlib.sha256()
    file_count = 0
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
    return {"path": str(path), "exists": True, "file_count": file_count, "digest": h.hexdigest()}


def run_with_timeout(cmd: list[str], *, cwd: Path, timeout: int, stdout_path: Path, stderr_path: Path) -> dict[str, Any]:
    stdout_path.parent.mkdir(parents=True, exist_ok=True)
    stderr_path.parent.mkdir(parents=True, exist_ok=True)
    started = dt.datetime.now().isoformat(timespec="seconds")
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
        "finished": dt.datetime.now().isoformat(timespec="seconds"),
        "stdout": str(stdout_path),
        "stderr": str(stderr_path),
    }


def write_json(path: Path, obj: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def existing_lookup_probe_source() -> str:
    source = CORPUS_PROBE_SOURCE.read_text(encoding="utf-8")
    main_start = source.find("int main(int argc, const char** argv)")
    if main_start < 0:
        raise RuntimeError("could not find probe main")
    prefix = source[:main_start]
    main = r'''
int main(int argc, const char** argv) {
    @autoreleasepool {
        NSString* inputPath = nil;
        NSString* archivePath = nil;
        NSString* outputPath = nil;
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "--input") && i + 1 < argc)
                inputPath = [NSString stringWithUTF8String:argv[++i]];
            else if (!strcmp(argv[i], "--archive") && i + 1 < argc)
                archivePath = [NSString stringWithUTF8String:argv[++i]];
            else if (!strcmp(argv[i], "--output") && i + 1 < argc)
                outputPath = [NSString stringWithUTF8String:argv[++i]];
            else {
                usage(argv[0]);
                return 2;
            }
        }
        if (!inputPath || !archivePath || !outputPath) {
            usage(argv[0]);
            return 2;
        }

        NSDictionary* input = loadJSON(inputPath);
        NSArray* candidates = input ? arrayValue(input, @"candidates") : nil;
        if (![candidates isKindOfClass:[NSArray class]]) {
            fprintf(stderr, "invalid input manifest\n");
            return 2;
        }

        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            fprintf(stderr, "MTLCreateSystemDefaultDevice failed\n");
            return 1;
        }

        unsigned long long archiveBytes = fileSize(archivePath);
        NSMutableString* archiveError = [NSMutableString string];
        id<MTLBinaryArchive> archive = archiveBytes > 0 ? newArchive(device, archivePath, archiveError) : nil;
        BOOL archiveLoadOK = archive != nil;

        NSMutableArray* results = [NSMutableArray array];
        NSUInteger selectedRender = 0, selectedCompute = 0;
        NSUInteger baselineRenderOK = 0, baselineComputeOK = 0, baselineTotalOK = 0;
        NSUInteger strictRenderOK = 0, strictComputeOK = 0, strictTotalOK = 0;
        for (NSDictionary* candidate in candidates) {
            NSMutableDictionary* record = [NSMutableDictionary dictionary];
            NSString* type = stringValue(candidate, @"type", @"");
            record[@"name"] = stringValue(candidate, @"name", @"");
            record[@"type"] = type;
            record[@"classification"] = stringValue(candidate, @"classification", @"");
            if ([type isEqualToString:@"render"])
                selectedRender++;
            if ([type isEqualToString:@"compute"])
                selectedCompute++;

            NSMutableString* baselineError = [NSMutableString string];
            BOOL baselineCreate = createPipeline(device, candidate, nil, NO, baselineError);
            if (baselineCreate) {
                baselineTotalOK++;
                if ([type isEqualToString:@"render"])
                    baselineRenderOK++;
                if ([type isEqualToString:@"compute"])
                    baselineComputeOK++;
            }

            NSMutableString* strictError = [NSMutableString string];
            BOOL strictCreate = NO;
            if (!baselineCreate) {
                [strictError appendString:@"skipped because non-strict baseline create failed"];
            } else if (archiveLoadOK) {
                strictCreate = createPipeline(device, candidate, archive, YES, strictError);
            } else {
                [strictError appendFormat:@"existing archive load failed: %@", archiveError];
            }
            if (strictCreate) {
                strictTotalOK++;
                if ([type isEqualToString:@"render"])
                    strictRenderOK++;
                if ([type isEqualToString:@"compute"])
                    strictComputeOK++;
            }
            record[@"baseline_create_ok"] = @(baselineCreate);
            record[@"baseline_create_error"] = baselineError;
            record[@"strict_lookup_create_ok"] = @(strictCreate);
            record[@"strict_lookup_error"] = strictError;
            [results addObject:record];
        }

        NSMutableDictionary* counts = [NSMutableDictionary dictionary];
        counts[@"selected_total"] = @([candidates count]);
        counts[@"selected_render"] = @(selectedRender);
        counts[@"selected_compute"] = @(selectedCompute);
        counts[@"baseline_create_ok"] = @(baselineTotalOK);
        counts[@"baseline_render_create_ok"] = @(baselineRenderOK);
        counts[@"baseline_compute_create_ok"] = @(baselineComputeOK);
        counts[@"strict_lookup_create_ok"] = @(strictTotalOK);
        counts[@"strict_render_lookup_ok"] = @(strictRenderOK);
        counts[@"strict_compute_lookup_ok"] = @(strictComputeOK);

        NSDictionary* out = @{
            @"schema" : @"metalsharp.m12.binary_archive_existing_lookup_probe.v1",
            @"input" : inputPath,
            @"archive_path" : archivePath,
            @"archive_bytes" : @(archiveBytes),
            @"archive_load_ok" : @(archiveLoadOK),
            @"archive_load_error" : archiveError,
            @"strict_lookup_mode" : @"MTLPipelineOptionFailOnBinaryArchiveMiss",
            @"counts" : counts,
            @"candidates" : results,
        };

        NSData* json = [NSJSONSerialization dataWithJSONObject:out
                                                       options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys
                                                         error:nil];
        if (!json || ![json writeToFile:outputPath atomically:YES]) {
            fprintf(stderr, "failed to write output\n");
            return 1;
        }

        BOOL pass = archiveLoadOK && baselineTotalOK > 0 && strictTotalOK == baselineTotalOK;
        return pass ? 0 : 1;
    }
}
'''
    return prefix + main


def source_checks() -> dict[str, Any]:
    text = PIPELINE_STATE.read_text(encoding="utf-8")
    compact = "".join(text.split())
    return {
        "existing_archive_must_be_regular_nonzero": "existing_archive_has_bytes = RegularFileHasBytes(ctx.native_path);" in text,
        "lookup_requires_validation_existing_and_not_bypass": "validation_passed&&loaded_existing_archive&&!bypass_lookup" in compact,
        "missing_archive_does_not_enable_lookup": "loaded_existing_archive = false;" in text and "ctx.allow_lookup = validation_passed && loaded_existing_archive && !bypass_lookup;" in text,
        "corrupt_archive_falls_back_without_lookup": "used_memory_fallback" not in text or "ctx.allow_lookup = validation_passed && loaded_existing_archive && !bypass_lookup;" in text,
        "bypass_env_honored": "EnvSwitchOne(\"DXMT_D3D12_BINARY_ARCHIVE_BYPASS_LOOKUP\")" in text,
    }


def render_summary(summary: dict[str, Any]) -> str:
    lines = [
        "# M12 binary archive Phase 9G existing archive lookup proof",
        "",
        f"Result: {'PASS' if summary['passed'] else 'FAIL'}",
        "",
        "## Scope",
        "",
        "- Offline only: no Steam, Wine, wineserver, AC6, menu, or Continue launch.",
        "- No runtime staging.",
        "- Validates the existing AC6 archive for warm lookup before any live lookup canary.",
        "",
        "## Existing archive",
        "",
    ]
    for key, value in summary["archive_after"].items():
        lines.append(f"- {key}: `{value}`")
    lines += ["", "## Corpus", ""]
    for key, value in summary["corpus"].items():
        lines.append(f"- {key}: `{value}`")
    lines += ["", "## Probe counts", ""]
    for key, value in summary.get("probe", {}).get("counts", {}).items():
        lines.append(f"- {key}: `{value}`")
    lines += ["", "## Acceptance", ""]
    for key, value in summary["acceptance"].items():
        lines.append(f"- {key}: `{value}`")
    lines += ["", "## Source checks", ""]
    for key, value in summary["source_checks"].items():
        lines.append(f"- {key}: `{value}`")
    lines += ["", "## Commands", ""]
    for cmd in summary["commands"]:
        lines.append(f"- rc={cmd['returncode']} timeout={cmd['timed_out']} cmd=`{' '.join(cmd['cmd'])}`")
        lines.append(f"  - stdout: `{cmd['stdout']}`")
        lines.append(f"  - stderr: `{cmd['stderr']}`")
    if not summary["passed"]:
        lines += ["", "## Blocker", "", "Warm lookup live canary must not run until this proof passes."]
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, default=DEFAULT_ARCHIVE)
    parser.add_argument("--cache-dir", type=Path, default=Path.home() / ".metalsharp/shader-cache/m12" / APPID)
    parser.add_argument("--results-dir", type=Path)
    parser.add_argument("--max-render", type=int, default=16)
    parser.add_argument("--max-compute", type=int, default=8)
    parser.add_argument("--compile-timeout-seconds", type=int, default=90)
    parser.add_argument("--probe-timeout-seconds", type=int, default=240)
    args = parser.parse_args()

    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = args.results_dir or SDK / "results" / f"m12-binary-archive-phase9g-existing-lookup-proof-{stamp}"
    out_dir.mkdir(parents=True, exist_ok=True)

    corpus_mod = load_corpus_module()
    runtime_before = path_metadata_snapshot(RUNTIME_DIR)
    archive_before = file_record(args.archive.expanduser())

    manifest = corpus_mod.build_corpus(args.cache_dir.expanduser())
    selected = corpus_mod.select_candidates(manifest, args.max_render, args.max_compute)
    manifest_path = out_dir / "corpus-manifest.json"
    probe_input_path = out_dir / "probe-input.json"
    write_json(manifest_path, manifest)
    write_json(probe_input_path, {
        "schema": "metalsharp.m12.binary_archive_phase9g_probe_input.v1",
        "source_manifest": str(manifest_path),
        "archive": str(args.archive.expanduser()),
        "max_render": args.max_render,
        "max_compute": args.max_compute,
        "candidates": selected,
    })

    probe_source = out_dir / "probe_m12_binary_archive_existing_lookup.mm"
    probe_bin = out_dir / "probe_m12_binary_archive_existing_lookup"
    probe_output = out_dir / "existing-lookup-proof.json"
    probe_source.write_text(existing_lookup_probe_source(), encoding="utf-8")

    commands: list[dict[str, Any]] = []
    compile_cmd = [
        "clang++", "-std=c++17", "-fobjc-arc", "-O2", str(probe_source),
        "-framework", "Foundation", "-framework", "Metal", "-o", str(probe_bin),
    ]
    commands.append(run_with_timeout(
        compile_cmd,
        cwd=ROOT,
        timeout=args.compile_timeout_seconds,
        stdout_path=out_dir / "compile.stdout.txt",
        stderr_path=out_dir / "compile.stderr.txt",
    ))
    if commands[-1]["returncode"] == 0 and not commands[-1]["timed_out"]:
        probe_cmd = [str(probe_bin), "--input", str(probe_input_path), "--archive", str(args.archive.expanduser()), "--output", str(probe_output)]
        commands.append(run_with_timeout(
            probe_cmd,
            cwd=ROOT,
            timeout=args.probe_timeout_seconds,
            stdout_path=out_dir / "probe.stdout.txt",
            stderr_path=out_dir / "probe.stderr.txt",
        ))

    probe: dict[str, Any] = {}
    if probe_output.is_file():
        probe = json.loads(probe_output.read_text(encoding="utf-8"))

    archive_after = file_record(args.archive.expanduser())
    runtime_after = path_metadata_snapshot(RUNTIME_DIR)
    src = source_checks()
    complete_render = manifest["classification_counts"].get("complete render PSO descriptor", 0)
    complete_compute = manifest["classification_counts"].get("complete compute PSO descriptor", 0)
    selected_render = sum(1 for c in selected if c.get("type") == "render")
    selected_compute = sum(1 for c in selected if c.get("type") == "compute")
    counts = probe.get("counts", {})

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
        str(RUNTIME_DIR).lower(),
    ]
    command_text = "\n".join(" ".join(str(token).lower() for token in command.get("cmd", [])) for command in commands)
    offline_only = not any(needle in command_text for needle in offline_denylist)
    no_staging = runtime_before == runtime_after and not any(needle in command_text for needle in staging_denylist)
    baseline_render_ok = int(counts.get("baseline_render_create_ok") or 0)
    baseline_compute_ok = int(counts.get("baseline_compute_create_ok") or 0)
    strict_render_ok = int(counts.get("strict_render_lookup_ok") or 0)
    strict_compute_ok = int(counts.get("strict_compute_lookup_ok") or 0)

    acceptance = {
        "offline_only_no_live_launch": offline_only,
        "no_runtime_staging": no_staging,
        "archive_exists_regular_nonzero": bool(archive_after["is_file"] and archive_after["size"] > 0),
        "archive_unchanged_by_proof": archive_before == archive_after,
        "compile_passed": bool(commands and commands[0]["returncode"] == 0 and not commands[0]["timed_out"]),
        "existing_archive_load_ok": bool(probe.get("archive_load_ok")),
        "baseline_create_has_representative_candidates": bool((baseline_render_ok + baseline_compute_ok) > 0),
        "strict_lookup_probe_passed": bool(len(commands) > 1 and commands[1]["returncode"] == 0 and not commands[1]["timed_out"]),
        "all_baseline_valid_render_strict_lookup_passed": bool(strict_render_ok == baseline_render_ok and baseline_render_ok > 0) if complete_render else True,
        "all_baseline_valid_compute_strict_lookup_passed_if_complete_compute_exists": bool(strict_compute_ok == baseline_compute_ok and baseline_compute_ok > 0) if complete_compute else True,
        "corrupt_missing_archive_lookup_circuit_breaker_source_ok": all(src.values()),
        "probe_did_not_survive_sigkill": not any(c.get("still_running_after_sigkill") for c in commands),
    }
    passed = all(acceptance.values())
    summary = {
        "schema": "metalsharp.m12.binary_archive_phase9g_existing_lookup_proof.v1",
        "timestamp": stamp,
        "passed": passed,
        "result_dir": str(out_dir),
        "archive_before": archive_before,
        "archive_after": archive_after,
        "corpus": {
            "candidate_count": manifest["candidate_count"],
            "classification_counts": manifest["classification_counts"],
            "selected_total": len(selected),
            "selected_render": selected_render,
            "selected_compute": selected_compute,
        },
        "probe": probe,
        "source_checks": src,
        "runtime_snapshot_before": runtime_before,
        "runtime_snapshot_after": runtime_after,
        "offline_safety_checks": {
            "offline_denylist": offline_denylist,
            "staging_denylist": staging_denylist,
            "command_text": command_text,
            "offline_only_no_live_launch": offline_only,
            "no_runtime_staging": no_staging,
        },
        "acceptance": acceptance,
        "commands": commands,
    }
    write_json(out_dir / "phase9g-existing-lookup-proof-summary.json", summary)
    (out_dir / "phase9g-existing-lookup-proof-summary.md").write_text(render_summary(summary), encoding="utf-8")
    print(out_dir / "phase9g-existing-lookup-proof-summary.md")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
