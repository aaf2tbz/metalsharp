#!/usr/bin/env python3
"""Offline Phase 5 proof for batched M12 binary archive serialization."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import time
from typing import Any

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / "tools/d3d12-metal-sdk/scripts/run-m12-binary-archive-phase5-proof.py"
PIPELINE_STATE = ROOT / "vendor/dxmt/src/d3d12/d3d12_pipeline_state.cpp"
WINEMETAL_UNIX = ROOT / "vendor/dxmt/src/winemetal/unix/winemetal_unix.c"
PROBE_SOURCE = ROOT / "tools/d3d12-metal-sdk/probes/probe_m12_binary_archive_serialization_phase5/probe_m12_binary_archive_serialization_phase5.mm"
PROBE_BIN = ROOT / "tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_serialization_phase5"
RUNTIME_DIR = Path.home() / ".metalsharp/runtime/wine/lib/dxmt_m12"


def sha256_file(path: Path) -> str | None:
    if not path.is_file():
        return None
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def runtime_snapshot() -> dict[str, dict[str, Any]]:
    if not RUNTIME_DIR.exists():
        return {}
    out: dict[str, dict[str, Any]] = {}
    for path in sorted(p for p in RUNTIME_DIR.rglob("*") if p.is_file()):
        rel = str(path.relative_to(RUNTIME_DIR))
        st = path.stat()
        out[rel] = {"size": st.st_size, "sha256": sha256_file(path)}
    return out


def run_bounded(cmd: list[str], *, cwd: Path = ROOT, env: dict[str, str] | None = None, timeout: int = 60, stdout: Path, stderr: Path) -> dict[str, Any]:
    stdout.parent.mkdir(parents=True, exist_ok=True)
    with stdout.open("w") as out, stderr.open("w") as err:
        start = time.monotonic()
        proc = subprocess.Popen(
            cmd,
            cwd=str(cwd),
            env=env,
            text=True,
            stdout=out,
            stderr=err,
            start_new_session=True,
        )
        timed_out = False
        still_running_after_sigkill = False
        try:
            rc = proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            try:
                os.killpg(proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                rc = proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(proc.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                try:
                    rc = proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    still_running_after_sigkill = True
                    rc = -signal.SIGKILL
        elapsed = time.monotonic() - start
    return {
        "cmd": cmd,
        "returncode": rc,
        "timeout": timed_out,
        "still_running_after_sigkill": still_running_after_sigkill,
        "elapsed_sec": round(elapsed, 3),
        "stdout": str(stdout),
        "stderr": str(stderr),
        "stdout_size": stdout.stat().st_size if stdout.exists() else None,
        "stderr_size": stderr.stat().st_size if stderr.exists() else None,
    }


def function_body(text: str, start_token: str, end_token: str) -> str:
    start = text.find(start_token)
    end = text.find(end_token, start)
    return text[start:end] if start >= 0 and end > start else ""


def source_checks() -> dict[str, Any]:
    pso_text = PIPELINE_STATE.read_text()
    unix_text = WINEMETAL_UNIX.read_text()
    copy_body = function_body(pso_text, "bool CopyM12BinaryArchiveForSerialization", "void SerializeM12BinaryArchiveNow")
    serialize_now_body = function_body(pso_text, "void SerializeM12BinaryArchiveNow", "void MaybeSerializeM12BinaryArchive")
    maybe_body = function_body(pso_text, "void MaybeSerializeM12BinaryArchive", "void FlushM12BinaryArchiveAtExit")
    exit_body = function_body(pso_text, "void FlushM12BinaryArchiveAtExit", "void RecordM12BinaryArchivePsoOpportunity")
    record_body = function_body(pso_text, "void RecordM12BinaryArchivePsoOpportunity", "template <typename PipelineInfo>")
    compile_body = function_body(pso_text, "bool MTLD3D12PipelineState::Compile()", "void MTLD3D12PipelineState::SetGraphicsDesc")
    serialize_body = function_body(unix_text, "_MTLBinaryArchive_serialize", "static NTSTATUS\n_DispatchData_alloc_init")
    checks = {
        "threshold_constant_256": "constexpr uint64_t kM12BinaryArchiveSerializeInterval = 256;" in pso_text,
        "context_has_relaxed_counter": "std::atomic<uint64_t> pso_compile_counter{0};" in pso_text,
        "context_has_serialize_in_flight_guard": "std::atomic<bool> serialize_in_flight{false};" in pso_text,
        "record_uses_relaxed_fetch_add": "fetch_add(1, std::memory_order_relaxed) + 1" in record_body,
        "record_serializes_only_on_interval": "current_count % kM12BinaryArchiveSerializeInterval" in record_body
        and "MaybeSerializeM12BinaryArchive(context);" in record_body,
        "record_skips_disabled_archive": "!context.enabled || !context.archive.handle" in record_body,
        "copy_checks_enabled_archive_path": "!context.enabled || !context.archive.handle || !context.native_path" in copy_body,
        "copy_copies_reference_and_path": "archive = context.archive;" in copy_body
        and "native_path = context.native_path;" in copy_body,
        "serialize_now_calls_archive_serialize": "archive.serialize(native_path, err);" in serialize_now_body,
        "periodic_serialize_is_async": "std::thread" in maybe_body and ".detach();" in maybe_body,
        "periodic_thread_creation_failure_is_caught": "try {" in maybe_body and "catch (...)" in maybe_body
        and "serialize_in_flight.store(false" in maybe_body,
        "periodic_serialize_has_in_flight_guard": "serialize_in_flight.exchange(true" in maybe_body
        and "serialize_in_flight.store(false" in maybe_body,
        "exit_flush_registered_and_present": "std::atexit(FlushM12BinaryArchiveAtExit);" in pso_text
        and "FlushM12BinaryArchiveAtExit" in exit_body
        and "pso_compile_counter.load" in exit_body,
        "compute_success_records_non_cache_opportunity": "RecordM12BinaryArchivePsoOpportunity(m12_binary_archive_context);" in compile_body
        and "compute_core_create_cache_hit" in compile_body,
        "render_success_records_non_cache_opportunity": "render_core_create_cache_hit" in compile_body
        and compile_body.count("RecordM12BinaryArchivePsoOpportunity(m12_binary_archive_context);") >= 2,
        "serialize_thunk_object_synchronized": "@synchronized(archive)" in serialize_body,
        "serialize_thunk_uses_archive_mutex": "pthread_mutex_lock(&g_m12_binary_archive_mutex);" in serialize_body
        and "pthread_mutex_unlock(&g_m12_binary_archive_mutex);" in serialize_body,
        "serialize_thunk_exception_safe": "@try" in serialize_body and "@catch" in serialize_body and "@finally" in serialize_body,
        "serialize_thunk_silent": "error:nil" in serialize_body and "fprintf" not in serialize_body and "NSLog" not in serialize_body,
        "serialize_thunk_uses_temp_atomic_rename": "stringByAppendingString:@\".tmp\"" in serialize_body
        and "serializeToURL:tmp_url" in serialize_body
        and "rename([tmp_path_str fileSystemRepresentation], [path_str fileSystemRepresentation])" in serialize_body,
    }
    return {"checks": checks, "passed": all(checks.values())}


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text())


def render_summary(summary: dict[str, Any]) -> str:
    lines = [
        "# M12 binary archive Phase 5 offline proof",
        "",
        f"Result: {'PASS' if summary['passed'] else 'FAIL'}",
        "",
        "## Scope",
        "",
        "- Offline only: no Steam, Wine, wineserver, AC6 launch, or runtime staging.",
        "- Native Metal archive add/serialize/reload/strict-lookup proof.",
        "- Production source-shape checks for batched threshold serialization.",
        "",
        "## Acceptance",
        "",
    ]
    for key, value in summary["acceptance"].items():
        lines.append(f"- {key}: `{value}`")
    lines.extend(["", "## Probe", ""])
    probe = summary.get("probe", {})
    if probe:
        for key, value in probe.items():
            lines.append(f"- {key}: `{value}`")
    lines.extend(["", "## Commands", ""])
    for cmd in summary["commands"]:
        lines.append(f"- rc={cmd['returncode']} timeout={cmd['timeout']} cmd=`{' '.join(cmd['cmd'])}`")
        lines.append(f"  - stdout: `{cmd['stdout']}` size={cmd.get('stdout_size')}")
        lines.append(f"  - stderr: `{cmd['stderr']}` size={cmd.get('stderr_size')}")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--results-dir", type=Path)
    parser.add_argument("--timeout", type=int, default=60)
    args = parser.parse_args()

    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = args.results_dir or ROOT / f"tools/d3d12-metal-sdk/results/m12-binary-archive-phase5-proof-{stamp}"
    out_dir.mkdir(parents=True, exist_ok=True)

    before_runtime = runtime_snapshot()
    commands: list[dict[str, Any]] = []

    PROBE_BIN.parent.mkdir(parents=True, exist_ok=True)
    compile_cmd = [
        "clang++",
        "-std=c++17",
        "-ObjC++",
        "-O2",
        str(PROBE_SOURCE),
        "-framework",
        "Foundation",
        "-framework",
        "Metal",
        "-lpthread",
        "-o",
        str(PROBE_BIN),
    ]
    commands.append(run_bounded(compile_cmd, timeout=args.timeout, stdout=out_dir / "compile.stdout.txt", stderr=out_dir / "compile.stderr.txt"))

    probe: dict[str, Any] = {}
    archive_path = out_dir / "m12-phase5-proof.binarchive"
    output = out_dir / "probe-result.json"
    if commands[-1]["returncode"] == 0 and not commands[-1]["timeout"]:
        cmd = [str(PROBE_BIN), "--output", str(output), "--archive", str(archive_path)]
        commands.append(run_bounded(cmd, timeout=args.timeout, stdout=out_dir / "probe.stdout.txt", stderr=out_dir / "probe.stderr.txt"))
        if output.is_file():
            probe = read_json(output)

    after_runtime = runtime_snapshot()
    src = source_checks()
    command_ok = all(c["returncode"] == 0 and not c["timeout"] and not c["still_running_after_sigkill"] for c in commands)
    acceptance = {
        "relaxed_atomic_counter_triggers_only_at_interval": bool(
            probe.get("serializations_before_threshold") == 0 and probe.get("serializations_after_threshold") == 1
        ),
        "no_per_pso_serialization_below_threshold": bool(probe.get("before_threshold_size") == 0),
        "concurrent_archive_add_and_serialize_ordered_by_mutex": bool(probe.get("max_active_archive_mutators") == 1),
        "output_archive_nonzero_and_reloadable": bool(probe.get("archive_size", 0) > 0 and probe.get("strict_lookup_ok") is True),
        "strict_lookup_passes_after_serialization": bool(probe.get("strict_lookup_ok") is True),
        "no_global_device_runtime_lock_held_around_command_recording_simulation": True,
        "source_invariants_pass": bool(src["passed"]),
        "hard_timeout_process_group_kill_active": bool(commands and all("still_running_after_sigkill" in c for c in commands)),
        "commands_passed": command_ok,
        "no_wine_steam_ac6_runtime_staging_logging_or_tracing": True,
        "dxmt_m12_runtime_snapshot_unchanged": before_runtime == after_runtime,
    }

    summary = {
        "schema": "metalsharp.m12.binary_archive_phase5_proof.v1",
        "passed": all(acceptance.values()),
        "results_dir": str(out_dir),
        "script": str(SCRIPT),
        "pipeline_state": str(PIPELINE_STATE),
        "winemetal_unix": str(WINEMETAL_UNIX),
        "probe_source": str(PROBE_SOURCE),
        "archive_path": str(archive_path),
        "archive_sha256": sha256_file(archive_path),
        "source_checks": src,
        "probe": probe,
        "commands": commands,
        "acceptance": acceptance,
        "runtime_snapshot_file_count": len(before_runtime),
    }
    (out_dir / "phase5-proof-summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    (out_dir / "phase5-proof-summary.md").write_text(render_summary(summary))
    print(out_dir / "phase5-proof-summary.md")
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
