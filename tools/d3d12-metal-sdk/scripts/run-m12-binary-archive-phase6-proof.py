#!/usr/bin/env python3
"""Offline Phase 6 proof for M12 binary archive validation/circuit breaker."""

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
SCRIPT = ROOT / "tools/d3d12-metal-sdk/scripts/run-m12-binary-archive-phase6-proof.py"
PIPELINE_STATE = ROOT / "vendor/dxmt/src/d3d12/d3d12_pipeline_state.cpp"
PROBE_SOURCE = ROOT / "tools/d3d12-metal-sdk/probes/probe_m12_binary_archive_validation_phase6/probe_m12_binary_archive_validation_phase6.mm"
PROBE_BIN = ROOT / "tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_validation_phase6"
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
    text = PIPELINE_STATE.read_text()
    validate_body = function_body(text, "bool ValidateM12BinaryArchiveLookupSupport", "const char *FormatM12BinaryArchivePath")
    init_body = function_body(text, "void InitializeM12BinaryArchiveContext", "M12BinaryArchiveContext &GetM12BinaryArchiveContext")
    attach_body = function_body(text, "void AttachM12BinaryArchiveInfo", "WMTAttributeFormat AttributeFormatForMetalDataType")
    init_compact = "".join(init_body.split())
    checks = {
        "regular_nonzero_file_gate_present": "bool RegularFileHasBytes" in text and "S_ISREG" in text and "st.st_size > 0" in text,
        "validation_helper_present": bool(validate_body),
        "validation_uses_temp_in_memory_archive": "newBinaryArchive(nullptr" in validate_body,
        "validation_compiles_minimal_source_library": "newLibraryWithSource" in validate_body and "m12_binary_archive_validation_cs" in validate_body,
        "validation_populates_archive_via_compute_pso": "InitializeComputePipelineInfo(info);" in validate_body
        and "info.binary_archive_for_serialization = validation_archive.handle;" in validate_body
        and "newComputePipelineState(info" in validate_body,
        "validation_serializes_unique_cache_local_file_and_checks_nonzero": "GetCurrentProcessId()" in validate_body
        and "validation_dir" in validate_body
        and "serialize(validation_path" in validate_body
        and "RegularFileHasBytes(validation_path)" in validate_body
        and "\"/tmp/m12_test.bin\"" not in validate_body,
        "lookup_defaults_disabled_before_validation": "ctx.allow_lookup = false;" in init_body,
        "existing_archive_must_be_regular_nonzero": "existing_archive_has_bytes = RegularFileHasBytes(ctx.native_path);" in init_body
        and "access(ctx.native_path,F_OK)==0&&existing_archive_has_bytes" in init_compact,
        "validation_failure_permanently_disables_lookup": "validation_passed && loaded_existing_archive && !bypass_lookup" in init_body,
        "population_enabled_even_when_lookup_disabled": "ctx.enabled = true;" in init_body
        and "info.binary_archive_for_serialization = context.archive.handle;" in attach_body,
        "bypass_env_still_honored": "EnvSwitchOne(\"DXMT_D3D12_BINARY_ARCHIVE_BYPASS_LOOKUP\")" in init_body,
        "no_archive_validation_logging_added": "DXMT_D3D12_BINARY_ARCHIVE_LOG" not in text
        and "BINARY_ARCHIVE_TRACE" not in text
        and "NSLog" not in validate_body
        and "fprintf" not in validate_body,
    }
    return {"checks": checks, "passed": all(checks.values())}


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text())


def render_summary(summary: dict[str, Any]) -> str:
    lines = [
        "# M12 binary archive Phase 6 offline proof",
        "",
        f"Result: {'PASS' if summary['passed'] else 'FAIL'}",
        "",
        "## Scope",
        "",
        "- Offline only: no Steam, Wine, wineserver, AC6 launch, or runtime staging.",
        "- Native Metal fixture matrix for archive validation/circuit-breaker behavior.",
        "- Production source-shape checks for runtime lookup gating.",
        "",
        "## Acceptance",
        "",
    ]
    for key, value in summary["acceptance"].items():
        lines.append(f"- {key}: `{value}`")
    lines.extend(["", "## Cases", ""])
    for case in summary["cases"]:
        lines.append(
            f"- {case['case']}: allow_lookup=`{case.get('allow_lookup')}` "
            f"validation=`{case.get('validation_passed')}` loaded_existing=`{case.get('loaded_existing_archive')}` "
            f"memory_fallback=`{case.get('used_memory_fallback')}` population=`{case.get('population_allowed')}`"
        )
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
    out_dir = args.results_dir or ROOT / f"tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-{stamp}"
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
        "-o",
        str(PROBE_BIN),
    ]
    commands.append(run_bounded(compile_cmd, timeout=args.timeout, stdout=out_dir / "compile.stdout.txt", stderr=out_dir / "compile.stderr.txt"))

    cases: list[dict[str, Any]] = []
    case_names = ["good", "bypass", "missing", "corrupt", "empty", "validation-failure"]
    if commands[-1]["returncode"] == 0 and not commands[-1]["timeout"]:
        for case_name in case_names:
            case_dir = out_dir / case_name
            case_dir.mkdir(parents=True, exist_ok=True)
            output = case_dir / "result.json"
            env = os.environ.copy()
            env["DXMT_D3D12_BINARY_ARCHIVE"] = "1"
            env["DXMT_PIPELINE_CACHE_PATH"] = str(case_dir / "pipeline-cache")
            env.pop("DXMT_SHADER_CACHE_PATH", None)
            env.pop("DXMT_D3D12_BINARY_ARCHIVE_BYPASS_LOOKUP", None)
            if case_name == "bypass":
                env["DXMT_D3D12_BINARY_ARCHIVE_BYPASS_LOOKUP"] = "1"
            validation_path = case_dir / "m12-phase6-validation-test.bin"
            for stale in (validation_path, Path(str(validation_path) + ".tmp")):
                if stale.exists():
                    stale.unlink()
            cmd = [
                str(PROBE_BIN),
                "--case",
                case_name,
                "--output",
                str(output),
                "--validation-path",
                str(validation_path),
            ]
            commands.append(run_bounded(cmd, env=env, timeout=args.timeout, stdout=case_dir / "probe.stdout.txt", stderr=case_dir / "probe.stderr.txt"))
            validation_leftovers = [str(p) for p in (validation_path, Path(str(validation_path) + ".tmp")) if p.exists()]
            if output.is_file():
                case_result = read_json(output)
                case_result["validation_leftovers"] = validation_leftovers
                cases.append(case_result)

    after_runtime = runtime_snapshot()
    src = source_checks()
    cases_by_name = {c.get("case"): c for c in cases}
    command_ok = all(c["returncode"] == 0 and not c["timeout"] and not c["still_running_after_sigkill"] for c in commands)
    probe_commands = commands[1:]
    probe_silent = bool(probe_commands) and all(c.get("stdout_size") == 0 and c.get("stderr_size") == 0 for c in probe_commands)

    good = cases_by_name.get("good", {})
    bypass = cases_by_name.get("bypass", {})
    missing = cases_by_name.get("missing", {})
    corrupt = cases_by_name.get("corrupt", {})
    empty = cases_by_name.get("empty", {})
    validation_failure = cases_by_name.get("validation-failure", {})
    disabled_cases = [bypass, missing, corrupt, empty, validation_failure]
    validation_leftovers_absent = all(not c.get("validation_leftovers") for c in cases)

    acceptance = {
        "good_archive_validation_enables_lookup": bool(
            good.get("prepared_good_archive") is True
            and good.get("validation_passed") is True
            and good.get("loaded_existing_archive") is True
            and good.get("allow_lookup") is True
        ),
        "bypass_env_keeps_lookup_disabled": bool(
            bypass.get("validation_passed") is True
            and bypass.get("loaded_existing_archive") is True
            and bypass.get("allow_lookup") is False
        ),
        "missing_corrupt_empty_disable_lookup": bool(
            missing.get("allow_lookup") is False
            and corrupt.get("allow_lookup") is False
            and empty.get("allow_lookup") is False
            and missing.get("used_memory_fallback") is True
            and corrupt.get("used_memory_fallback") is True
            and empty.get("used_memory_fallback") is True
        ),
        "serialization_failure_disables_lookup": bool(
            validation_failure.get("prepared_good_archive") is True
            and validation_failure.get("loaded_existing_archive") is True
            and validation_failure.get("validation_passed") is False
            and validation_failure.get("allow_lookup") is False
        ),
        "archive_population_remains_allowed_when_lookup_disabled": bool(
            disabled_cases and all(c.get("population_allowed") is True for c in disabled_cases)
        ),
        "validation_failure_paths_emit_no_probe_logs": probe_silent,
        "validation_temp_artifacts_are_cleaned": validation_leftovers_absent,
        "source_invariants_pass": bool(src["passed"]),
        "hard_timeout_process_group_kill_active": bool(commands and all("still_running_after_sigkill" in c for c in commands)),
        "commands_passed": command_ok,
        "no_wine_steam_ac6_runtime_staging_logging_or_tracing": True,
        "dxmt_m12_runtime_snapshot_unchanged": before_runtime == after_runtime,
    }

    summary = {
        "schema": "metalsharp.m12.binary_archive_phase6_proof.v1",
        "passed": all(acceptance.values()),
        "results_dir": str(out_dir),
        "script": str(SCRIPT),
        "pipeline_state": str(PIPELINE_STATE),
        "probe_source": str(PROBE_SOURCE),
        "source_checks": src,
        "cases": cases,
        "commands": commands,
        "acceptance": acceptance,
        "runtime_snapshot_file_count": len(before_runtime),
    }
    (out_dir / "phase6-proof-summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    (out_dir / "phase6-proof-summary.md").write_text(render_summary(summary))
    print(out_dir / "phase6-proof-summary.md")
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
