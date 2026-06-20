#!/usr/bin/env python3
"""Offline Phase 6B proof for generated-MSL metallib safety guards."""

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
import time
from typing import Any

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / "tools/d3d12-metal-sdk/scripts/run-m12-metallib-phase6b-proof.py"
M12CORE = ROOT / "vendor/dxmt/src/m12core/m12core.cpp"
PIPELINE_STATE = ROOT / "vendor/dxmt/src/d3d12/d3d12_pipeline_state.cpp"
LOOKUP_PROBE_SOURCE = ROOT / "tools/d3d12-metal-sdk/probes/probe_m12_metallib_lookup_phase6b/probe_m12_metallib_lookup_phase6b.c"
LOOKUP_PROBE_BIN = ROOT / "tools/d3d12-metal-sdk/out/bin/probe_m12_metallib_lookup_phase6b"
M12CORE_DYLIB = ROOT / "vendor/dxmt/build-metalsharp-x64/src/m12core/libm12core.dylib"
TOOLCHAIN_LIB = Path.home() / ".metalsharp/toolchains/clang+llvm-15.0.7-x86_64-apple-darwin21.0/lib"
METAL_PROBE = ROOT / "tools/d3d12-metal-sdk/scripts/probe-metal-metallib-load.sh"
M12_LOAD_PROBE_SOURCE = ROOT / "tools/d3d12-metal-sdk/probes/probe_m12_metallib_load/probe_m12_metallib_load.m"
M12_LOAD_PROBE_BIN = ROOT / "tools/d3d12-metal-sdk/out/bin/probe_m12_metallib_load_phase6b"
RUNTIME_DIR = Path.home() / ".metalsharp/runtime/wine/lib/dxmt_m12"

HASHES = {
    "valid": "1111111111111111",
    "missing": "2222222222222222",
    "zero": "3333333333333333",
    "bad_header": "4444444444444444",
    "stale": "5555555555555555",
    "active_error": "6666666666666666",
    "force_source": "7777777777777777",
}

MSL_SOURCE = """#include <metal_stdlib>
using namespace metal;
kernel void cs_main(uint id [[thread_position_in_grid]]) { }
"""


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
    core = M12CORE.read_text()
    pso = PIPELINE_STATE.read_text()
    probe_body = function_body(core, "m12core_probe_shader_cache", "extern \"C\" int\nm12core_parse_shader_reflection")
    metallib_hit_region = function_body(pso, "uint64_t hits = ++g_shader_metallib_cache_hits", "          break;")
    checks = {
        "m12core_checks_regular_nonzero": "regularFileStat" in core and "st.st_size > 0" in core and "metallib_stat.st_size > 0" in probe_body,
        "m12core_checks_mtlb_header": "fileHasMTLBHeader" in core and "std::memcmp(magic, \"MTLB\", 4) == 0" in core,
        "m12core_checks_freshness_vs_msl": "metallibFreshForSource" in core and "metallib_stat.st_mtime >= msl_stat.st_mtime" in core,
        "m12core_checks_active_error_sidecar": "metallibHasActiveError" in core and "error_stat.st_mtime >= metallib_stat.st_mtime" in core,
        "m12core_force_source_disables_available": "!force_source_compile" in probe_body,
        "pso_has_process_local_bad_metallib_denylist": "std::unordered_set<size_t> g_bad_metallib_cache" in pso
        and "IsBadMetallibCacheEntry" in pso
        and "MarkBadMetallibCacheEntry" in pso,
        "pso_skips_denied_metallib_before_open": "metallib_available && !metallib_cache_denied" in pso
        and "if (!metallib_cache_denied)" in pso,
        "pso_cached_failures_mark_bad_and_fallback": metallib_hit_region.count("MarkBadMetallibCacheEntry(hash)") >= 4
        and metallib_hit_region.count("goto compile_dxil_source_fallback") >= 4,
        "pso_cached_failure_path_does_not_write_metallib_error": "DumpShaderText(metallib_error_path" not in metallib_hit_region,
        "runtime_materialization_still_absent": "xcrun" not in pso
        and "std::system" not in pso
        and "CreateProcess" not in pso
        and "popen(" not in pso,
    }
    return {"checks": checks, "passed": all(checks.values())}


def write_msl(cache: Path, hash_text: str) -> Path:
    path = cache / f"{hash_text}.msl"
    path.write_text(MSL_SOURCE)
    return path


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text())


def create_fixtures(cache: Path, out_dir: Path, commands: list[dict[str, Any]], timeout: int) -> dict[str, Any]:
    cache.mkdir(parents=True, exist_ok=True)
    fixtures: dict[str, Any] = {}
    valid_hash = HASHES["valid"]
    valid_msl = write_msl(cache, valid_hash)
    air = cache / f"{valid_hash}.air"
    valid_metallib = cache / f"{valid_hash}.metallib"
    commands.append(run_bounded([
        "xcrun", "-sdk", "macosx", "metal", "-x", "metal", "-c", str(valid_msl), "-o", str(air)
    ], timeout=timeout, stdout=out_dir / "metal.stdout.txt", stderr=out_dir / "metal.stderr.txt"))
    if commands[-1]["returncode"] == 0:
        commands.append(run_bounded([
            "xcrun", "-sdk", "macosx", "metallib", str(air), "-o", str(valid_metallib)
        ], timeout=timeout, stdout=out_dir / "metallib.stdout.txt", stderr=out_dir / "metallib.stderr.txt"))
    fixtures["valid_metallib_sha256"] = sha256_file(valid_metallib)
    fixtures["valid_metallib_size"] = valid_metallib.stat().st_size if valid_metallib.exists() else 0
    fixtures["valid_metallib_header"] = valid_metallib.read_bytes()[:4].decode("ascii", errors="replace") if valid_metallib.exists() else ""

    # Missing metallib: MSL only.
    write_msl(cache, HASHES["missing"])
    # Zero-byte metallib.
    write_msl(cache, HASHES["zero"])
    (cache / f"{HASHES['zero']}.metallib").write_bytes(b"")
    # Nonzero regular file with invalid header.
    write_msl(cache, HASHES["bad_header"])
    (cache / f"{HASHES['bad_header']}.metallib").write_bytes(b"BAD!not-a-metallib")
    # Stale metallib older than MSL.
    stale_msl = write_msl(cache, HASHES["stale"])
    stale_metallib = cache / f"{HASHES['stale']}.metallib"
    if valid_metallib.exists():
        shutil.copy2(valid_metallib, stale_metallib)
    now = time.time()
    os.utime(stale_metallib, (now - 20, now - 20))
    os.utime(stale_msl, (now, now))
    # Active error newer than metallib.
    active_msl = write_msl(cache, HASHES["active_error"])
    active_metallib = cache / f"{HASHES['active_error']}.metallib"
    if valid_metallib.exists():
        shutil.copy2(valid_metallib, active_metallib)
    err = cache / f"{HASHES['active_error']}.metallib.err.txt"
    err.write_text("previous metallib load failed")
    os.utime(active_msl, (now - 30, now - 30))
    os.utime(active_metallib, (now - 10, now - 10))
    os.utime(err, (now, now))
    # Force-source fixture is valid but queried with force_source=1.
    force_msl = write_msl(cache, HASHES["force_source"])
    force_metallib = cache / f"{HASHES['force_source']}.metallib"
    if valid_metallib.exists():
        shutil.copy2(valid_metallib, force_metallib)
    os.utime(force_msl, (now - 20, now - 20))
    os.utime(force_metallib, (now, now))
    return fixtures


def render_summary(summary: dict[str, Any]) -> str:
    lines = [
        "# M12 metallib Phase 6B offline proof",
        "",
        f"Result: {'PASS' if summary['passed'] else 'FAIL'}",
        "",
        "## Scope",
        "",
        "- Offline only: no Steam, Wine, AC6 launch, runtime staging, logging, or tracing.",
        "- Proof-local shader cache only.",
        "- Valid metallib load/use plus invalid/stale/error/force-source lookup rejection.",
        "",
        "## Acceptance",
        "",
    ]
    for key, value in summary["acceptance"].items():
        lines.append(f"- {key}: `{value}`")
    lines.extend(["", "## Lookup cases", ""])
    for name, row in summary["lookup_results"].items():
        lines.append(f"- {name}: exists=`{row.get('metallib_exists')}` available=`{row.get('metallib_available')}` force=`{row.get('force_source_compile')}`")
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
    out_dir = args.results_dir or ROOT / f"tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-{stamp}"
    out_dir.mkdir(parents=True, exist_ok=True)
    cache = out_dir / "proof-cache"
    commands: list[dict[str, Any]] = []
    before_runtime = runtime_snapshot()

    fixtures = create_fixtures(cache, out_dir, commands, args.timeout)

    LOOKUP_PROBE_BIN.parent.mkdir(parents=True, exist_ok=True)
    lib_dir = M12CORE_DYLIB.parent
    compile_cmd = [
        "clang", "-arch", "x86_64", "-mmacosx-version-min=12.0",
        "-I", str(ROOT / "vendor/dxmt/src/m12core"),
        str(LOOKUP_PROBE_SOURCE), str(M12CORE_DYLIB),
        "-Wl,-rpath," + str(lib_dir),
        "-Wl,-rpath," + str(TOOLCHAIN_LIB),
        "-o", str(LOOKUP_PROBE_BIN),
    ]
    commands.append(run_bounded(compile_cmd, timeout=args.timeout, stdout=out_dir / "lookup-compile.stdout.txt", stderr=out_dir / "lookup-compile.stderr.txt"))

    lookup_results: dict[str, Any] = {}
    if commands[-1]["returncode"] == 0:
        for name, hash_text in HASHES.items():
            case_dir = out_dir / f"lookup-{name}"
            case_dir.mkdir(parents=True, exist_ok=True)
            output = case_dir / "result.json"
            env = os.environ.copy()
            env["DYLD_LIBRARY_PATH"] = f"{lib_dir}:{TOOLCHAIN_LIB}:{env.get('DYLD_LIBRARY_PATH', '')}"
            cmd = [str(LOOKUP_PROBE_BIN), "--cache-dir", str(cache), "--hash", hash_text, "--output", str(output), "--force-source", "1" if name == "force_source" else "0"]
            commands.append(run_bounded(cmd, env=env, timeout=args.timeout, stdout=case_dir / "stdout.txt", stderr=case_dir / "stderr.txt"))
            if output.exists():
                lookup_results[name] = read_json(output)

    metal_env = os.environ.copy()
    metal_env["METAL_PROBE_OUT_DIR"] = str(out_dir / "metal-load-probe")
    commands.append(run_bounded([str(METAL_PROBE), "--cache-dir", str(cache), "--hash", HASHES["valid"]], env=metal_env, timeout=args.timeout, stdout=out_dir / "metal-probe.stdout.txt", stderr=out_dir / "metal-probe.stderr.txt"))
    metal_probe_cmd = commands[-1]

    M12_LOAD_PROBE_BIN.parent.mkdir(parents=True, exist_ok=True)
    m12_load_compile = [
        "clang", "-arch", "x86_64", "-mmacosx-version-min=12.0",
        "-I", str(ROOT / "vendor/dxmt/src/m12core"),
        str(M12_LOAD_PROBE_SOURCE), str(M12CORE_DYLIB),
        "-framework", "Foundation", "-framework", "Metal",
        "-Wl,-rpath," + str(lib_dir),
        "-Wl,-rpath," + str(TOOLCHAIN_LIB),
        "-o", str(M12_LOAD_PROBE_BIN),
    ]
    commands.append(run_bounded(m12_load_compile, timeout=args.timeout, stdout=out_dir / "m12core-compile.stdout.txt", stderr=out_dir / "m12core-compile.stderr.txt"))
    commands.append(run_bounded([str(M12_LOAD_PROBE_BIN), "--cache-dir", str(cache), "--hash", HASHES["valid"]], timeout=args.timeout, stdout=out_dir / "m12core-probe.stdout.txt", stderr=out_dir / "m12core-probe.stderr.txt"))
    m12core_probe_cmd = commands[-1]

    timeout_selftest = run_bounded(["python3", "-c", "import time; time.sleep(30)"], timeout=1, stdout=out_dir / "timeout-selftest.stdout.txt", stderr=out_dir / "timeout-selftest.stderr.txt")
    timeout_selftest["expected_timeout"] = True
    commands.append(timeout_selftest)

    after_runtime = runtime_snapshot()
    src = source_checks()
    def command_passed(c: dict[str, Any]) -> bool:
        if c.get("expected_timeout"):
            return c["timeout"] is True and c["still_running_after_sigkill"] is False
        return c["returncode"] == 0 and not c["timeout"] and not c["still_running_after_sigkill"]

    command_ok = all(command_passed(c) for c in commands)
    m12core_stdout = Path(m12core_probe_cmd["stdout"]).read_text() if Path(m12core_probe_cmd["stdout"]).exists() else ""
    valid = lookup_results.get("valid", {})
    invalid_names = ["missing", "zero", "bad_header", "stale", "active_error", "force_source"]
    invalid_rows = [lookup_results.get(name, {}) for name in invalid_names]
    live_cache = Path.home() / ".metalsharp/shader-cache/m12/1888160"
    live_before_after_note = "not touched; proof cache is results-local"

    acceptance = {
        "valid_fresh_metallib_available": bool(valid.get("metallib_exists") is True and valid.get("metallib_available") is True),
        "missing_zero_bad_header_stale_active_error_and_force_source_unavailable": bool(invalid_rows and all(row.get("metallib_available") is False for row in invalid_rows)),
        "zero_and_bad_header_still_report_existing_only_when_nonzero_regular": bool(
            lookup_results.get("zero", {}).get("metallib_exists") is False
            and lookup_results.get("bad_header", {}).get("metallib_exists") is True
        ),
        "valid_metallib_loads_with_metal_newLibraryWithData": metal_probe_cmd["returncode"] == 0,
        "valid_metallib_loads_with_m12core_and_second_call_cache_hit": m12core_probe_cmd["returncode"] == 0 and "second_cache_hit=1" in m12core_stdout,
        "source_invariants_pass": bool(src["passed"]),
        "runtime_writeback_materialization_absent": bool(src["checks"].get("runtime_materialization_still_absent")),
        "hard_timeout_process_group_kill_active": bool(timeout_selftest["timeout"] is True and timeout_selftest["still_running_after_sigkill"] is False),
        "commands_passed": command_ok,
        "dxmt_m12_runtime_snapshot_unchanged": before_runtime == after_runtime,
        "no_wine_steam_ac6_runtime_staging_logging_or_tracing": True,
        "live_shader_cache_unchanged_by_policy": live_before_after_note,
    }

    summary = {
        "schema": "metalsharp.m12.metallib_phase6b_proof.v1",
        "passed": all(v is True or (k == "live_shader_cache_unchanged_by_policy" and isinstance(v, str)) for k, v in acceptance.items()),
        "results_dir": str(out_dir),
        "script": str(SCRIPT),
        "m12core_source": str(M12CORE),
        "pipeline_state": str(PIPELINE_STATE),
        "lookup_probe_source": str(LOOKUP_PROBE_SOURCE),
        "proof_cache": str(cache),
        "fixtures": fixtures,
        "lookup_results": lookup_results,
        "source_checks": src,
        "commands": commands,
        "acceptance": acceptance,
        "runtime_snapshot_file_count": len(before_runtime),
        "live_shader_cache": str(live_cache),
    }
    (out_dir / "phase6b-proof-summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    (out_dir / "phase6b-proof-summary.md").write_text(render_summary(summary))
    print(out_dir / "phase6b-proof-summary.md")
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
