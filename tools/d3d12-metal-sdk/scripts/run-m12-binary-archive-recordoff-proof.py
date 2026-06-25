#!/usr/bin/env python3
"""Offline proof for M12 binary archive population record-off mode."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
from pathlib import Path
import subprocess
import time
from typing import Any

ROOT = Path(__file__).resolve().parents[3]
PIPELINE_STATE = ROOT / "vendor/dxmt/src/d3d12/d3d12_pipeline_state.cpp"
BACKEND_MAIN = ROOT / "app/src-rust/src/main.rs"
BUILD_DIR = ROOT / "vendor/dxmt/build-metalsharp-x64"
APP_RUST = ROOT / "app/src-rust"


def run_bounded(cmd: list[str], *, timeout: int, stdout: Path, stderr: Path, cwd: Path = ROOT) -> dict[str, Any]:
    stdout.parent.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["PATH"] = f"{Path.home() / '.cargo/bin'}:{env.get('PATH', '')}"
    start = time.monotonic()
    with stdout.open("w") as out, stderr.open("w") as err:
        try:
            proc = subprocess.run(cmd, cwd=str(cwd), stdout=out, stderr=err, text=True, timeout=timeout, check=False, env=env)
            rc = proc.returncode
            timed_out = False
        except subprocess.TimeoutExpired:
            rc = -1
            timed_out = True
    return {
        "cmd": cmd,
        "cwd": str(cwd),
        "returncode": rc,
        "timeout": timed_out,
        "elapsed_sec": round(time.monotonic() - start, 3),
        "stdout": str(stdout),
        "stderr": str(stderr),
        "stdout_size": stdout.stat().st_size if stdout.exists() else None,
        "stderr_size": stderr.stat().st_size if stderr.exists() else None,
    }


def function_body(text: str, start_token: str, end_token: str | None = None) -> str:
    start = text.find(start_token)
    if start < 0:
        return ""
    if end_token is None:
        return text[start:]
    end = text.find(end_token, start + len(start_token))
    return text[start:end] if end > start else ""


def source_checks() -> dict[str, Any]:
    pipeline = PIPELINE_STATE.read_text()
    backend = BACKEND_MAIN.read_text()

    ctx_struct = function_body(pipeline, "struct M12BinaryArchiveContext", "struct M12BinaryArchiveCompilePayload")
    init_body = function_body(pipeline, "void InitializeM12BinaryArchiveContext", "M12BinaryArchiveContext &GetM12BinaryArchiveContext")
    record_body = function_body(pipeline, "void RecordM12BinaryArchivePsoOpportunity", "template <typename PipelineInfo>")
    attach_body = function_body(pipeline, "void AttachM12BinaryArchiveInfo", "WMTAttributeFormat AttributeFormatForMetalDataType")
    allow_body = function_body(backend, "fn allowed_launch_env_override", "fn apply_launch_env_overrides")
    override_body = function_body(backend, "fn apply_launch_env_overrides", "/// Minimal percent-decoding")
    test_body = function_body(backend, "fn m12core_launch_overrides_map_to_dxmt_loader_env", "#[test]\n    fn m12_hang_trace_profile")

    checks = {
        "offline_only_no_launch_or_stage": True,
        "context_tracks_record_off_mode": "bool population_record_off = false;" in ctx_struct,
        "record_off_requires_runtime_population":
            "const bool population_record_off = allow_population && EnvSwitchOne(\"DXMT_D3D12_BINARY_ARCHIVE_POPULATE_RECORD_OFF\");" in init_body,
        "init_resets_record_off_false": "ctx.population_record_off = false;" in init_body,
        "init_persists_record_off_mode": "ctx.population_record_off = population_record_off;" in init_body,
        "record_reads_record_off_under_lock":
            "bool population_record_off = false;" in record_body
            and "population_record_off = context.population_record_off;" in record_body,
        "record_off_returns_before_counter_increment":
            (
                "if (population_record_off)\n    return;\n\n  uint64_t current_count =\n      context.pso_compile_counter.fetch_add" in record_body
                or "if (population_record_off || population_record_kind_off)\n    return;\n\n  uint64_t current_count =\n      context.pso_compile_counter.fetch_add" in record_body
            ),
        "record_still_requires_allow_population": "!context.enabled || !context.archive.handle || !context.allow_population" in record_body,
        "descriptor_attachment_still_controlled_by_no_attach":
            "if (context.allow_population && !context.population_no_attach)\n    info.binary_archive_for_serialization = context.archive.handle;" in attach_body,
        "backend_allows_record_off_override": "METALSHARP_M12_BINARY_ARCHIVE_POPULATE_RECORD_OFF" in allow_body,
        "backend_maps_record_off_override": "DXMT_D3D12_BINARY_ARCHIVE_POPULATE_RECORD_OFF" in override_body,
        "backend_test_covers_record_off_override":
            "METALSHARP_M12_BINARY_ARCHIVE_POPULATE_RECORD_OFF" in test_body
            and "DXMT_D3D12_BINARY_ARCHIVE_POPULATE_RECORD_OFF" in test_body,
        "no_new_population_logging_env":
            "DXMT_D3D12_BINARY_ARCHIVE_LOG" not in pipeline + backend
            and "BINARY_ARCHIVE_TRACE" not in pipeline + backend,
    }
    return {"checks": checks, "passed": all(checks.values())}


def render_summary(summary: dict[str, Any]) -> str:
    lines = [
        "# M12 binary archive population record-off proof",
        "",
        f"Result: {'PASS' if summary['passed'] else 'FAIL'}",
        "",
        "## Scope",
        "",
        "- Offline only: no Steam, Wine game, AC6 launch, or runtime staging.",
        "- Adds `DXMT_D3D12_BINARY_ARCHIVE_POPULATE_RECORD_OFF=1` for the split after context-off passed and no-attach failed.",
        "- Intended live discriminator: `ctx.allow_population=true`, descriptor serialization attachment disabled with no-attach, and population opportunity bookkeeping disabled before counter increment.",
        "",
        "## Acceptance",
        "",
    ]
    for key, value in summary["acceptance"].items():
        lines.append(f"- {key}: `{value}`")
    lines += ["", "## Source checks", ""]
    for key, value in summary["source"]["checks"].items():
        lines.append(f"- {key}: `{value}`")
    lines += ["", "## Commands", ""]
    for cmd in summary["commands"]:
        lines.append(f"- rc={cmd['returncode']} timeout={cmd['timeout']} cwd=`{cmd['cwd']}` cmd=`{' '.join(cmd['cmd'])}`")
        lines.append(f"  - stdout: `{cmd['stdout']}` size={cmd.get('stdout_size')}")
        lines.append(f"  - stderr: `{cmd['stderr']}` size={cmd.get('stderr_size')}")
    lines += [
        "",
        "## Interpretation",
        "",
        "If the future no-attach+record-off menu canary passes, the no-attach failure is caused by `RecordM12BinaryArchivePsoOpportunity` bookkeeping/counter path. If it fails, `ctx.allow_population=true` itself or descriptor-side setup before record is implicated.",
    ]
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--results-dir", type=Path)
    parser.add_argument("--timeout", type=int, default=600)
    args = parser.parse_args()

    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = args.results_dir or ROOT / f"tools/d3d12-metal-sdk/results/m12-binary-archive-recordoff-proof-{stamp}"
    out_dir.mkdir(parents=True, exist_ok=True)

    commands = [
        run_bounded(
            ["ninja", "-C", str(BUILD_DIR), "src/d3d12/d3d12.dll"],
            timeout=args.timeout,
            stdout=out_dir / "build-d3d12.stdout.txt",
            stderr=out_dir / "build-d3d12.stderr.txt",
        ),
        run_bounded(
            ["cargo", "test", "m12core_launch_overrides_map_to_dxmt_loader_env", "--release"],
            timeout=args.timeout,
            stdout=out_dir / "cargo-test.stdout.txt",
            stderr=out_dir / "cargo-test.stderr.txt",
            cwd=APP_RUST,
        ),
    ]

    src = source_checks()
    commands_ok = all(cmd["returncode"] == 0 and not cmd["timeout"] for cmd in commands)
    acceptance = {
        "offline_only_no_launch_or_stage": src["checks"].get("offline_only_no_launch_or_stage", False),
        "source_checks_passed": src["passed"],
        "touched_targets_build_or_test": commands_ok,
        "record_off_requires_population": src["checks"].get("record_off_requires_runtime_population", False),
        "record_off_blocks_counter_before_increment": src["checks"].get("record_off_returns_before_counter_increment", False),
        "descriptor_attachment_still_separate": src["checks"].get("descriptor_attachment_still_controlled_by_no_attach", False),
        "launcher_override_is_mapped": src["checks"].get("backend_maps_record_off_override", False),
    }
    summary = {
        "schema": "metalsharp.m12.binary_archive_recordoff_proof.v1",
        "timestamp": stamp,
        "passed": all(acceptance.values()),
        "acceptance": acceptance,
        "source": src,
        "commands": commands,
    }

    (out_dir / "recordoff-proof-summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    (out_dir / "recordoff-proof-summary.md").write_text(render_summary(summary))
    print(out_dir / "recordoff-proof-summary.md")
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
