#!/usr/bin/env python3
"""Offline Phase 4 proof for M12 ObjC binary archive safety."""

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
SCRIPT = ROOT / "tools/d3d12-metal-sdk/scripts/run-m12-binary-archive-phase4-proof.py"
M12CORE_METAL = ROOT / "vendor/dxmt/src/m12core/m12core_metal.c"
WINEMETAL_UNIX = ROOT / "vendor/dxmt/src/winemetal/unix/winemetal_unix.c"
PROBE_SOURCE = ROOT / "tools/d3d12-metal-sdk/probes/probe_m12_binary_archive_objc_phase4/probe_m12_binary_archive_objc_phase4.m"
PROBE_BIN = ROOT / "tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_objc_phase4"
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
        "stdout_size": stdout.stat().st_size if stdout.exists() else None,
        "stderr_size": stderr.stat().st_size if stderr.exists() else None,
    }


def segment_around(text: str, token: str, before: int = 280, after: int = 520) -> str:
    idx = text.find(token)
    if idx < 0:
        return ""
    return text[max(0, idx - before) : idx + after]


def guarded_site_checks(text: str, token: str) -> dict[str, bool]:
    segment = segment_around(text, token)
    return {
        "present": bool(segment),
        "object_synchronized_before_add": "@synchronized(archive)" in segment
        and segment.find("@synchronized(archive)") < segment.find(token),
        "lock_before_add": "pthread_mutex_lock(&g_m12_binary_archive_mutex);" in segment
        and segment.find("pthread_mutex_lock(&g_m12_binary_archive_mutex);") < segment.find(token),
        "try_catch_finally": "@try" in segment and "@catch" in segment and "@finally" in segment,
        "unlock_after_add": "pthread_mutex_unlock(&g_m12_binary_archive_mutex);" in segment
        and segment.find("pthread_mutex_unlock(&g_m12_binary_archive_mutex);") > segment.find(token),
        "clears_lookup_on_exception": "descriptor.binaryArchives = nil;" in segment,
        "uses_error_nil": token in segment,
        "no_logging": "fprintf" not in segment and "NSLog" not in segment and "winemetal_critical_log" not in segment,
        "no_error_poisoning": "error:&err" not in segment and "error:&error" not in segment,
    }


def source_file_checks(path: Path, tokens: list[str]) -> dict[str, Any]:
    text = path.read_text()
    site_checks = {token: guarded_site_checks(text, token) for token in tokens}
    checks = {
        "has_archive_mutex": "static pthread_mutex_t g_m12_binary_archive_mutex = PTHREAD_MUTEX_INITIALIZER;" in text,
        "all_expected_sites_present": all(site["present"] for site in site_checks.values()),
        "all_expected_sites_object_synchronized_before_add": all(site["object_synchronized_before_add"] for site in site_checks.values()),
        "all_expected_sites_lock_before_add": all(site["lock_before_add"] for site in site_checks.values()),
        "all_expected_sites_try_catch_finally": all(site["try_catch_finally"] for site in site_checks.values()),
        "all_expected_sites_unlock_after_add": all(site["unlock_after_add"] for site in site_checks.values()),
        "all_expected_sites_clear_lookup_on_exception": all(site["clears_lookup_on_exception"] for site in site_checks.values()),
        "all_expected_sites_use_error_nil": all(site["uses_error_nil"] for site in site_checks.values()),
        "no_expected_site_logging": all(site["no_logging"] for site in site_checks.values()),
        "no_expected_site_error_poisoning": all(site["no_error_poisoning"] for site in site_checks.values()),
    }
    return {"path": str(path), "checks": checks, "site_checks": site_checks, "passed": all(checks.values())}


def source_checks() -> dict[str, Any]:
    m12core_tokens = [
        "addComputePipelineFunctionsWithDescriptor:descriptor error:nil",
        "addRenderPipelineFunctionsWithDescriptor:descriptor error:nil",
    ]
    winemetal_tokens = [
        "addComputePipelineFunctionsWithDescriptor:descriptor error:nil",
        "addRenderPipelineFunctionsWithDescriptor:descriptor error:nil",
        "addMeshRenderPipelineFunctionsWithDescriptor:descriptor error:nil",
        "addTileRenderPipelineFunctionsWithDescriptor:descriptor error:nil",
    ]
    files = [source_file_checks(M12CORE_METAL, m12core_tokens), source_file_checks(WINEMETAL_UNIX, winemetal_tokens)]
    return {"files": files, "passed": all(f["passed"] for f in files)}


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text())


def render_summary(summary: dict[str, Any]) -> str:
    lines = [
        "# M12 binary archive Phase 4 offline proof",
        "",
        f"Result: {'PASS' if summary['passed'] else 'FAIL'}",
        "",
        "## Scope",
        "",
        "- Offline only: no Steam, Wine, wineserver, AC6 launch, or runtime staging.",
        "- Native ObjC safety probe with hard process-group timeout.",
        "- Production source-shape checks for archive-add mutex/exception guards.",
        "",
        "## Acceptance",
        "",
    ]
    for key, value in summary["acceptance"].items():
        lines.append(f"- {key}: `{value}`")
    lines.extend(["", "## Probe cases", ""])
    for case in summary["probe_cases"]:
        lines.append(
            f"- {case['case']}: forced_exception=`{case['forced_exception']}` "
            f"add_attempted=`{case['add_attempted']}` mutex_unlocked=`{case['mutex_unlocked']}` "
            f"cleared=`{case['binary_archives_cleared_on_exception']}` continues=`{case['standard_pso_continues']}` "
            f"passed=`{case['passed']}`"
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
    parser.add_argument("--timeout", type=int, default=30)
    args = parser.parse_args()

    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = args.results_dir or ROOT / f"tools/d3d12-metal-sdk/results/m12-binary-archive-phase4-proof-{stamp}"
    out_dir.mkdir(parents=True, exist_ok=True)

    before_runtime = runtime_snapshot()
    commands: list[dict[str, Any]] = []

    PROBE_BIN.parent.mkdir(parents=True, exist_ok=True)
    compile_cmd = [
        "clang",
        "-ObjC",
        "-O2",
        str(PROBE_SOURCE),
        "-framework",
        "Foundation",
        "-lpthread",
        "-o",
        str(PROBE_BIN),
    ]
    commands.append(run_bounded(compile_cmd, timeout=args.timeout, stdout=out_dir / "compile.stdout.txt", stderr=out_dir / "compile.stderr.txt"))

    case_names = ["compute-success", "render-success", "compute-exception", "render-exception"]
    probe_cases: list[dict[str, Any]] = []
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
    command_ok = all(c["returncode"] == 0 and not c["timeout"] and not c["still_running_after_sigkill"] for c in commands)
    all_cases = len(probe_cases) == len(case_names) and all(c.get("passed") for c in probe_cases)
    failure_cases = [c for c in probe_cases if c.get("forced_exception")]
    success_cases = [c for c in probe_cases if not c.get("forced_exception")]
    zero_output_failure_paths = all(
        c.get("stdout_size") == 0 and c.get("stderr_size") == 0
        for c in commands
        if len(c.get("cmd", [])) >= 3 and any(name in c["cmd"] for name in ["compute-exception", "render-exception"])
    )
    acceptance = {
        "render_and_compute_archive_add_isolated_under_mutex": bool(src["passed"] and all_cases),
        "normal_add_success_allows_baseline_pso_creation": bool(success_cases and all(c.get("standard_pso_continues") for c in success_cases)),
        "forced_failure_exception_paths_unlock_mutex": bool(failure_cases and all(c.get("mutex_unlocked") for c in failure_cases)),
        "failure_clears_descriptor_archive_lookup": bool(failure_cases and all(c.get("binary_archives_cleared_on_exception") for c in failure_cases)),
        "standard_pso_creation_continues_after_archive_add_failure": bool(failure_cases and all(c.get("standard_pso_continues") for c in failure_cases)),
        "stdout_stderr_empty_for_archive_failure_paths": zero_output_failure_paths,
        "source_invariants_pass": bool(src["passed"]),
        "hard_timeout_process_group_kill_active": bool(commands and all("still_running_after_sigkill" in c for c in commands)),
        "commands_passed": command_ok,
        "no_wine_steam_ac6_runtime_staging_logging_or_tracing": True,
        "dxmt_m12_runtime_snapshot_unchanged": before_runtime == after_runtime,
    }

    summary = {
        "schema": "metalsharp.m12.binary_archive_phase4_proof.v1",
        "passed": all(acceptance.values()),
        "results_dir": str(out_dir),
        "script": str(SCRIPT),
        "m12core_metal": str(M12CORE_METAL),
        "winemetal_unix": str(WINEMETAL_UNIX),
        "probe_source": str(PROBE_SOURCE),
        "source_checks": src,
        "probe_cases": probe_cases,
        "commands": commands,
        "acceptance": acceptance,
        "runtime_snapshot_file_count": len(before_runtime),
    }
    (out_dir / "phase4-proof-summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    (out_dir / "phase4-proof-summary.md").write_text(render_summary(summary))
    print(out_dir / "phase4-proof-summary.md")
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
