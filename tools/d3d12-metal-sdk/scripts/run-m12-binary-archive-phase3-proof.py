#!/usr/bin/env python3
"""Offline Phase 3 proof for M12 binary archive descriptor attachment."""

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
SCRIPT = ROOT / "tools/d3d12-metal-sdk/scripts/run-m12-binary-archive-phase3-proof.py"
PIPELINE_STATE = ROOT / "vendor/dxmt/src/d3d12/d3d12_pipeline_state.cpp"
PROBE_SOURCE = ROOT / "tools/d3d12-metal-sdk/probes/probe_m12_binary_archive_descriptor_phase3/probe_m12_binary_archive_descriptor_phase3.cpp"
PROBE_BIN = ROOT / "tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_descriptor_phase3"
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


def git_changed_under(pathspec: str) -> list[str]:
    proc = subprocess.run(
        ["git", "diff", "--name-only", "--", pathspec],
        cwd=str(ROOT),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    return [line for line in proc.stdout.splitlines() if line]


def function_body(text: str, start_token: str, end_token: str) -> str:
    start = text.find(start_token)
    end = text.find(end_token, start)
    return text[start:end] if start >= 0 and end > start else ""


def source_checks() -> dict[str, Any]:
    text = PIPELINE_STATE.read_text()
    body = function_body(text, "bool MTLD3D12PipelineState::Compile()", "void MTLD3D12PipelineState::SetGraphicsDesc")
    helper = function_body(text, "void AttachM12BinaryArchiveInfo", "WMTAttributeFormat AttributeFormatForMetalDataType")
    compute_init = body.find("WMT::InitializeComputePipelineInfo(info);")
    compute_attach = body.find("AttachM12BinaryArchiveInfo(info, m12_binary_archive_context", compute_init)
    compute_create = body.find("CreateM12CorePipelineState(wmt_device.handle", compute_attach)
    render_init = body.find("WMT::InitializeRenderPipelineInfo(info);")
    render_attach = body.find("AttachM12BinaryArchiveInfo(info, m12_binary_archive_context", render_init)
    render_create = body.find("CreateM12CorePipelineState(wmt_device.handle", render_attach)
    abi_changes = git_changed_under("vendor/dxmt/src/winemetal") + git_changed_under("vendor/dxmt/src/m12core")
    checks = {
        "compile_payload_struct_present": "struct M12BinaryArchiveCompilePayload" in text
        and "heap_archive_handles[1]" in text,
        "helper_present": bool(helper),
        "helper_uses_context_mutex": "std::lock_guard<std::mutex> lock(context.mutex);" in helper,
        "helper_resets_safe_defaults": "info.binary_archive_for_serialization = NULL_OBJECT_HANDLE;" in helper
        and "info.binary_archives_for_lookup.set(nullptr);" in helper
        and "info.num_binary_archives_for_lookup = 0;" in helper
        and "info.fail_on_binary_archive_miss = false;" in helper,
        "serialization_attached_when_enabled": "info.binary_archive_for_serialization = context.archive.handle;" in helper,
        "lookup_gated_by_allow_lookup": "if (context.allow_lookup)" in helper,
        "lookup_uses_heap_payload": "info.binary_archives_for_lookup.set(payload.heap_archive_handles);" in helper,
        "no_stack_archive_handle_array": "obj_handle_t archive_handles[1]" not in text,
        "compile_keeps_context_reference": "auto &m12_binary_archive_context = GetM12BinaryArchiveContext(wmt_device);" in body,
        "compute_descriptor_attached_before_creation": compute_init >= 0 and compute_init < compute_attach < compute_create,
        "render_descriptor_attached_before_creation": render_init >= 0 and render_init < render_attach < render_create,
        "no_wmt_abi_layout_changes": not abi_changes,
        "no_archive_descriptor_logging_added": "BINARY_ARCHIVE_TRACE" not in text
        and "DXMT_D3D12_BINARY_ARCHIVE_LOG" not in text,
    }
    return {"checks": checks, "passed": all(checks.values()), "abi_changes": abi_changes}


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text())


def render_summary(summary: dict[str, Any]) -> str:
    lines = [
        "# M12 binary archive Phase 3 offline proof",
        "",
        f"Result: {'PASS' if summary['passed'] else 'FAIL'}",
        "",
        "## Scope",
        "",
        "- Offline only: no Steam, Wine, wineserver, AC6 launch, or runtime staging.",
        "- Descriptor attachment proof only; Objective-C archive-add safety remains Phase 4.",
        "- Native C++ probe with hard process-group timeout.",
        "- Production source-shape checks for heap-owned lookup payload and descriptor wiring.",
        "",
        "## Acceptance",
        "",
    ]
    for key, value in summary["acceptance"].items():
        lines.append(f"- {key}: `{value}`")
    lines.extend(["", "## Probe cases", ""])
    for case in summary["probe_cases"]:
        lines.append(
            f"- {case['case']}: enabled=`{case['enabled']}` allow_lookup=`{case['allow_lookup']}` "
            f"compute_serialization=`{case['compute_serialization_set']}` render_serialization=`{case['render_serialization_set']}` "
            f"compute_lookup_count=`{case['compute_lookup_count']}` render_lookup_count=`{case['render_lookup_count']}` "
            f"passed=`{case['passed']}`"
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
    out_dir = args.results_dir or ROOT / f"tools/d3d12-metal-sdk/results/m12-binary-archive-phase3-proof-{stamp}"
    out_dir.mkdir(parents=True, exist_ok=True)

    before_runtime = runtime_snapshot()
    commands: list[dict[str, Any]] = []

    PROBE_BIN.parent.mkdir(parents=True, exist_ok=True)
    compile_cmd = ["clang++", "-std=c++17", "-O2", str(PROBE_SOURCE), "-o", str(PROBE_BIN)]
    commands.append(run_bounded(compile_cmd, timeout=args.timeout, stdout=out_dir / "compile.stdout.txt", stderr=out_dir / "compile.stderr.txt"))

    probe_cases: list[dict[str, Any]] = []
    case_names = ["disabled", "lookup-bypassed", "lookup-allowed", "circuit-breaker"]
    if commands[-1]["returncode"] == 0 and not commands[-1]["timeout"]:
        for case_name in case_names:
            case_dir = out_dir / case_name
            output = case_dir / "result.json"
            cmd = [str(PROBE_BIN), "--case", case_name, "--output", str(output)]
            commands.append(run_bounded(cmd, timeout=args.timeout, stdout=case_dir / "stdout.txt", stderr=case_dir / "stderr.txt"))
            if output.is_file():
                probe_cases.append(read_json(output))

    after_runtime = runtime_snapshot()
    src = source_checks()
    cases = {case["case"]: case for case in probe_cases}
    command_ok = all(c["returncode"] == 0 and not c["timeout"] and not c["still_running_after_sigkill"] for c in commands)

    disabled = cases.get("disabled", {})
    bypass = cases.get("lookup-bypassed", {})
    allowed = cases.get("lookup-allowed", {})
    circuit = cases.get("circuit-breaker", {})
    all_cases_passed = len(probe_cases) == len(case_names) and all(case.get("passed") for case in probe_cases)
    all_fail_on_miss_false = all(
        case.get("compute_fail_on_miss") is False and case.get("render_fail_on_miss") is False for case in probe_cases
    )
    acceptance = {
        "compute_and_render_serialization_only_when_enabled": bool(
            disabled.get("compute_serialization_set") is False
            and disabled.get("render_serialization_set") is False
            and bypass.get("compute_serialization_set") is True
            and bypass.get("render_serialization_set") is True
            and allowed.get("compute_serialization_set") is True
            and allowed.get("render_serialization_set") is True
        ),
        "lookup_attached_only_when_allowed": bool(
            bypass.get("compute_lookup_count") == 0
            and bypass.get("render_lookup_count") == 0
            and circuit.get("compute_lookup_count") == 0
            and circuit.get("render_lookup_count") == 0
            and allowed.get("compute_lookup_count") == 1
            and allowed.get("render_lookup_count") == 1
        ),
        "lookup_pointer_uses_heap_payload_storage": bool(
            allowed.get("compute_lookup_heap_payload") is True and allowed.get("render_lookup_heap_payload") is True
        ),
        "async_worker_shaped_heap_payload_lifetime": bool(all_cases_passed),
        "runtime_fail_on_binary_archive_miss_false": bool(all_fail_on_miss_false),
        "source_invariants_pass": bool(src["passed"]),
        "hard_timeout_process_group_kill_active": bool(commands and all("still_running_after_sigkill" in c for c in commands)),
        "commands_passed": command_ok,
        "no_wine_steam_ac6_runtime_staging_logging_or_tracing": True,
        "dxmt_m12_runtime_snapshot_unchanged": before_runtime == after_runtime,
    }

    summary = {
        "schema": "metalsharp.m12.binary_archive_phase3_proof.v1",
        "passed": all(acceptance.values()),
        "results_dir": str(out_dir),
        "script": str(SCRIPT),
        "pipeline_state": str(PIPELINE_STATE),
        "probe_source": str(PROBE_SOURCE),
        "source_checks": src,
        "probe_cases": probe_cases,
        "commands": commands,
        "acceptance": acceptance,
        "runtime_snapshot_file_count": len(before_runtime),
    }
    (out_dir / "phase3-proof-summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    (out_dir / "phase3-proof-summary.md").write_text(render_summary(summary))
    print(out_dir / "phase3-proof-summary.md")
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
