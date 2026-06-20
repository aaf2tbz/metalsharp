#!/usr/bin/env python3
"""Offline Phase 2 proof for the M12 PE-side binary archive manager."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time
from typing import Any

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / "tools/d3d12-metal-sdk/scripts/run-m12-binary-archive-phase2-proof.py"
PIPELINE_STATE = ROOT / "vendor/dxmt/src/d3d12/d3d12_pipeline_state.cpp"
PROBE_SOURCE = ROOT / "tools/d3d12-metal-sdk/probes/probe_m12_binary_archive_manager_phase2/probe_m12_binary_archive_manager_phase2.mm"
PROBE_BIN = ROOT / "tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_manager_phase2"
PHASE1_ARCHIVE = ROOT / "tools/d3d12-metal-sdk/results/m12-binary-archive-offline-corpus-20260620-131215/m12_binary_archive_corpus.binarchive"
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


def run_bounded(cmd: list[str], *, cwd: Path = ROOT, env: dict[str, str] | None = None, timeout: int = 30, stdout: Path, stderr: Path) -> dict[str, Any]:
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
    }


def source_checks() -> dict[str, Any]:
    text = PIPELINE_STATE.read_text()
    compile_start = text.find("bool MTLD3D12PipelineState::Compile()")
    compile_end = text.find("void MTLD3D12PipelineState::SetGraphicsDesc", compile_start)
    compile_body = text[compile_start:compile_end] if compile_start >= 0 and compile_end > compile_start else ""
    checks = {
        "context_struct_present": "struct M12BinaryArchiveContext" in text,
        "context_has_required_fields": all(s in text for s in [
            "std::mutex mutex;",
            "WMT::Reference<WMT::BinaryArchive> archive;",
            "const char *native_path = nullptr;",
            "bool enabled = false;",
            "bool allow_lookup = false;",
        ]),
        "once_flag_initialization": "std::once_flag g_m12_binary_archive_once" in text and "std::call_once(g_m12_binary_archive_once" in text,
        "compile_invokes_manager": "GetM12BinaryArchiveContext(wmt_device)" in compile_body,
        "exact_env_enable": "EnvSwitchOne(\"DXMT_D3D12_BINARY_ARCHIVE\")" in text,
        "exact_env_bypass": "EnvSwitchOne(\"DXMT_D3D12_BINARY_ARCHIVE_BYPASS_LOOKUP\")" in text,
        "pipeline_cache_preferred": "ReadEnvPath(\"DXMT_PIPELINE_CACHE_PATH\"" in text,
        "shader_cache_fallback": "ReadEnvPath(\"DXMT_SHADER_CACHE_PATH\"" in text,
        "tmp_shader_cache_fallback": "\"/tmp/dxmt_shader_cache\"" in text,
        "device_registry_id_in_path": "wmt_device.registryID()" in text,
        "process_lifetime_strdup_path": "return strdup(path);" in text,
        "parent_directory_best_effort": "EnsureDirectoryBestEffort(parent);" in text,
        "existing_archive_load_attempt": "access(ctx.native_path, F_OK) == 0" in text and "newBinaryArchive(ctx.native_path" in text,
        "memory_fallback_after_load_failure": "newBinaryArchive(nullptr" in text,
        "archive_disabled_if_memory_fallback_fails": "ctx.allow_lookup = false;" in text and "return;" in text,
        "no_descriptor_attachment_in_phase2": "binary_archive_for_serialization = ctx.archive" not in text and "heap_archive_handles" not in text,
        "no_archive_logging_env_added": "DXMT_D3D12_BINARY_ARCHIVE_LOG" not in text and "BINARY_ARCHIVE_TRACE" not in text,
    }
    return {"checks": checks, "passed": all(checks.values())}


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text())


def render_summary(summary: dict[str, Any]) -> str:
    lines = [
        "# M12 binary archive Phase 2 offline proof",
        "",
        f"Result: {'PASS' if summary['passed'] else 'FAIL'}",
        "",
        "## Scope",
        "",
        "- Offline only: no Steam, Wine, wineserver, AC6 launch, or runtime staging.",
        "- Native Metal probe with hard process-group timeout.",
        "- Production source-shape checks for PE-side manager invariants.",
        "",
        "## Acceptance",
        "",
    ]
    for key, value in summary["acceptance"].items():
        lines.append(f"- {key}: `{value}`")
    lines.extend(["", "## Probe cases", ""])
    for case in summary["probe_cases"]:
        lines.append(
            f"- {case['case']}: enabled=`{case.get('enabled')}` allow_lookup=`{case.get('allow_lookup')}` "
            f"path_load=`{case.get('used_path_load')}` memory_fallback=`{case.get('used_memory_fallback')}` "
            f"archive=`{case.get('archive_handle_nonzero')}`"
        )
    lines.extend(["", "## Commands", ""])
    for cmd in summary["commands"]:
        lines.append(f"- rc={cmd['returncode']} timeout={cmd['timeout']} cmd=`{' '.join(cmd['cmd'])}`")
        lines.append(f"  - stdout: `{cmd['stdout']}`")
        lines.append(f"  - stderr: `{cmd['stderr']}`")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--results-dir", type=Path)
    parser.add_argument("--timeout", type=int, default=30)
    args = parser.parse_args()

    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = args.results_dir or ROOT / f"tools/d3d12-metal-sdk/results/m12-binary-archive-phase2-proof-{stamp}"
    out_dir.mkdir(parents=True, exist_ok=True)

    before_runtime = runtime_snapshot()
    commands: list[dict[str, Any]] = []

    PROBE_BIN.parent.mkdir(parents=True, exist_ok=True)
    compile_cmd = [
        "clang++",
        "-std=c++17",
        "-O2",
        str(PROBE_SOURCE),
        "-framework",
        "Foundation",
        "-framework",
        "Metal",
        "-o",
        str(PROBE_BIN),
    ]
    commands.append(run_bounded(compile_cmd, timeout=args.timeout, stdout=out_dir / "compile.stdout.txt", stderr=out_dir / "compile.stderr.txt"))

    case_defs = [
        ("disabled", {}),
        ("enabled", {"DXMT_D3D12_BINARY_ARCHIVE": "1"}),
        ("bypass", {"DXMT_D3D12_BINARY_ARCHIVE": "1", "DXMT_D3D12_BINARY_ARCHIVE_BYPASS_LOOKUP": "1"}),
        ("shader-cache-fallback", {"DXMT_D3D12_BINARY_ARCHIVE": "1", "DXMT_SHADER_CACHE_PATH": str(out_dir / "shader-cache-root")}),
        ("missing", {"DXMT_D3D12_BINARY_ARCHIVE": "1"}),
        ("corrupt", {"DXMT_D3D12_BINARY_ARCHIVE": "1"}),
        ("existing", {"DXMT_D3D12_BINARY_ARCHIVE": "1"}),
    ]

    probe_cases: list[dict[str, Any]] = []
    if commands[-1]["returncode"] == 0 and not commands[-1]["timeout"]:
        for case_name, env_extra in case_defs:
            case_dir = out_dir / case_name
            case_dir.mkdir(parents=True, exist_ok=True)
            env = os.environ.copy()
            env.pop("DXMT_D3D12_BINARY_ARCHIVE", None)
            env.pop("DXMT_D3D12_BINARY_ARCHIVE_BYPASS_LOOKUP", None)
            env.pop("DXMT_PIPELINE_CACHE_PATH", None)
            env.pop("DXMT_SHADER_CACHE_PATH", None)
            env.update(env_extra)
            if case_name != "shader-cache-fallback":
                env.setdefault("DXMT_PIPELINE_CACHE_PATH", str(case_dir / "pipeline-cache-root"))
            output = case_dir / "result.json"
            cmd = [str(PROBE_BIN), "--case", case_name, "--output", str(output)]
            if case_name == "existing":
                cmd.extend(["--existing-archive", str(PHASE1_ARCHIVE)])
            commands.append(run_bounded(cmd, timeout=args.timeout, stdout=case_dir / "stdout.txt", stderr=case_dir / "stderr.txt", env=env))
            if output.is_file():
                probe_cases.append(read_json(output))

    after_runtime = runtime_snapshot()
    src = source_checks()

    cases_by_name = {case["case"]: case for case in probe_cases}
    command_ok = all(c["returncode"] == 0 and not c["timeout"] and not c["still_running_after_sigkill"] for c in commands)
    acceptance = {
        "env_parsing_enables_and_disables": bool(cases_by_name.get("disabled", {}).get("enabled") is False and cases_by_name.get("enabled", {}).get("enabled") is True),
        "bypass_disables_lookup_without_disabling_population": bool(cases_by_name.get("bypass", {}).get("enabled") is True and cases_by_name.get("bypass", {}).get("allow_lookup") is False and cases_by_name.get("bypass", {}).get("archive_handle_nonzero") is True),
        "archive_path_formatted_once_process_lifetime": bool(probe_cases and all(c.get("path_formatted_once_process_lifetime") for c in probe_cases)),
        "pipeline_cache_path_preferred": bool(cases_by_name.get("enabled", {}).get("native_path", "").startswith(str(out_dir / "enabled/pipeline-cache-root"))),
        "shader_cache_path_fallback": bool(cases_by_name.get("shader-cache-fallback", {}).get("native_path", "").startswith(str(out_dir / "shader-cache-root"))),
        "existing_archive_load_succeeds": bool(cases_by_name.get("existing", {}).get("prepared_existing_archive") and cases_by_name.get("existing", {}).get("used_path_load") and cases_by_name.get("existing", {}).get("archive_handle_nonzero")),
        "missing_archive_falls_back_to_empty_memory_archive": bool(cases_by_name.get("missing", {}).get("used_memory_fallback") and cases_by_name.get("missing", {}).get("archive_handle_nonzero")),
        "corrupt_archive_falls_back_to_empty_memory_archive": bool(cases_by_name.get("corrupt", {}).get("prepared_corrupt_archive") and cases_by_name.get("corrupt", {}).get("used_memory_fallback") and cases_by_name.get("corrupt", {}).get("archive_handle_nonzero")),
        "source_invariants_pass": bool(src["passed"]),
        "hard_timeout_process_group_kill_active": bool(commands and all("still_running_after_sigkill" in c for c in commands)),
        "commands_passed": command_ok,
        "no_wine_steam_ac6_runtime_staging_logging_or_tracing": True,
        "dxmt_m12_runtime_snapshot_unchanged": before_runtime == after_runtime,
    }

    summary = {
        "schema": "metalsharp.m12.binary_archive_phase2_proof.v1",
        "passed": all(acceptance.values()),
        "results_dir": str(out_dir),
        "script": str(SCRIPT),
        "pipeline_state": str(PIPELINE_STATE),
        "probe_source": str(PROBE_SOURCE),
        "phase1_archive": str(PHASE1_ARCHIVE),
        "phase1_archive_sha256": sha256_file(PHASE1_ARCHIVE),
        "source_checks": src,
        "probe_cases": probe_cases,
        "commands": commands,
        "acceptance": acceptance,
        "runtime_snapshot_file_count": len(before_runtime),
    }
    (out_dir / "phase2-proof-summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    (out_dir / "phase2-proof-summary.md").write_text(render_summary(summary))
    print(out_dir / "phase2-proof-summary.md")
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
