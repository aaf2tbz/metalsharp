#!/usr/bin/env python3
"""Offline Phase 5B proof for generated-MSL -> .metallib cache usability.

This proof intentionally does not launch Steam/Wine/AC6, stage a runtime, or
modify the live shader cache.  It copies selected AC6 generated .msl sidecars to
a temporary proof cache, materializes .metallib files, and proves those artifacts
load through both Metal and the M12Core shader-function path.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import threading
import time
from typing import Any

ROOT = Path(__file__).resolve().parents[3]
SCRIPTS = ROOT / "tools/d3d12-metal-sdk/scripts"
RESULTS = ROOT / "tools/d3d12-metal-sdk/results"
DEFAULT_AC6_CACHE = Path.home() / ".metalsharp/shader-cache/m12/1888160"
RUNTIME_DIR = Path.home() / ".metalsharp/runtime/wine/lib/dxmt_m12"

MATERIALIZE_SCRIPT = SCRIPTS / "materialize-m12-msl-metallibs.py"
FRESHNESS_SCRIPT = SCRIPTS / "verify-m12-metallib-freshness.py"
RUNTIME_CONTRACT_SCRIPT = SCRIPTS / "verify-m12-metallib-runtime-contract.py"
METAL_PROBE_SCRIPT = SCRIPTS / "probe-metal-metallib-load.sh"
M12_PROBE_SCRIPT = SCRIPTS / "probe-m12-metallib-load.sh"
PIPELINE_STATE = ROOT / "vendor/dxmt/src/d3d12/d3d12_pipeline_state.cpp"

HEX16 = re.compile(r"^[0-9a-fA-F]{16}$")
SUMMARY_RE = re.compile(r"summary total=(\d+) failures=(\d+)")
FORBIDDEN_COMMAND_TOKENS = {
    "wine",
    "wine64",
    "wineserver",
    "steam",
    "steam.exe",
    "armoredcore6.exe",
    "start_protected_game.exe",
    "stage-dxmt-runtime.py",
    "stage-game-metal-validation.py",
    "m12-bounded-launch.sh",
}
FORBIDDEN_COMMAND_SUBSTRINGS = ("/steam/launch-game", "/mtsp/prepare")


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


def file_snapshot(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {"exists": False}
    st = path.stat()
    return {"exists": True, "size": st.st_size, "mtime_ns": st.st_mtime_ns, "sha256": sha256_file(path)}


def selected_cache_snapshot(cache_dir: Path, hashes: list[str]) -> dict[str, dict[str, Any]]:
    out: dict[str, dict[str, Any]] = {}
    for h in hashes:
        for suffix in (".msl", ".metallib", ".metallib.err.txt"):
            path = cache_dir / f"{h}{suffix}"
            out[path.name] = file_snapshot(path)
    return out


def normalize_hash(raw: str) -> str:
    h = raw.strip().lower()
    if h.startswith("0x"):
        h = h[2:]
    if not h or len(h) > 16 or any(c not in "0123456789abcdef" for c in h):
        raise ValueError(f"invalid shader hash: {raw!r}")
    return h.zfill(16)


def read_hashes(path: Path) -> list[str]:
    hashes: list[str] = []
    for line in path.read_text(errors="replace").splitlines():
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        hashes.append(normalize_hash(s))
    return hashes


def discover_hashes(cache_dir: Path, limit: int) -> list[str]:
    candidates: list[tuple[int, str]] = []
    for path in cache_dir.glob("*.msl"):
        if not HEX16.fullmatch(path.stem):
            continue
        try:
            size = path.stat().st_size
        except OSError:
            continue
        if size <= 0:
            continue
        candidates.append((size, path.stem.lower()))
    # Prefer larger generated shaders as a bounded stress sample, but keep the
    # sample deterministic and record it in the proof artifacts.
    selected = [h for _, h in sorted(candidates, key=lambda v: (-v[0], v[1]))[:limit]]
    return selected


def copy_sample(cache_dir: Path, proof_cache: Path, hashes: list[str]) -> list[dict[str, Any]]:
    proof_cache.mkdir(parents=True, exist_ok=True)
    rows: list[dict[str, Any]] = []
    for h in hashes:
        src = cache_dir / f"{h}.msl"
        dst = proof_cache / f"{h}.msl"
        row: dict[str, Any] = {
            "hash": h,
            "source": str(src),
            "proof_copy": str(dst),
            "source_exists": src.is_file(),
            "source_sha256": sha256_file(src),
            "source_size": src.stat().st_size if src.is_file() else None,
        }
        if src.is_file():
            shutil.copy2(src, dst)
            row["copy_sha256"] = sha256_file(dst)
            row["copy_size"] = dst.stat().st_size
        rows.append(row)
    return rows


def pump_stream(src: Any, dst_file: Any, dst_console: Any) -> None:
    for chunk in iter(src.readline, ""):
        dst_file.write(chunk)
        dst_file.flush()
        dst_console.write(chunk)
        dst_console.flush()
    src.close()


def run_bounded(
    cmd: list[str],
    *,
    stdout: Path,
    stderr: Path,
    cwd: Path = ROOT,
    env: dict[str, str] | None = None,
    timeout: int = 180,
) -> dict[str, Any]:
    stdout.parent.mkdir(parents=True, exist_ok=True)
    stderr.parent.mkdir(parents=True, exist_ok=True)
    print(f"$ {' '.join(cmd)}", flush=True)
    start = time.monotonic()
    with stdout.open("w") as out, stderr.open("w") as err:
        proc = subprocess.Popen(
            cmd,
            cwd=str(cwd),
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            start_new_session=True,
            bufsize=1,
        )
        assert proc.stdout is not None
        assert proc.stderr is not None
        out_thread = threading.Thread(target=pump_stream, args=(proc.stdout, out, sys.stdout), daemon=True)
        err_thread = threading.Thread(target=pump_stream, args=(proc.stderr, err, sys.stderr), daemon=True)
        out_thread.start()
        err_thread.start()
        timed_out = False
        still_running_after_sigkill = False
        try:
            rc = proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            print(f"timeout after {timeout}s; SIGTERM process group {proc.pid}", file=sys.stderr, flush=True)
            try:
                os.killpg(proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                rc = proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                print(f"still running; SIGKILL process group {proc.pid}", file=sys.stderr, flush=True)
                try:
                    os.killpg(proc.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                try:
                    rc = proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    still_running_after_sigkill = True
                    rc = -signal.SIGKILL
        out_thread.join(timeout=2)
        err_thread.join(timeout=2)
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


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text())


def command_plan_is_offline(commands: list[dict[str, Any]]) -> bool:
    for entry in commands:
        parts = [str(p) for p in entry.get("cmd", [])]
        joined = " ".join(parts).lower()
        if any(token in joined for token in FORBIDDEN_COMMAND_SUBSTRINGS):
            return False
        for part in parts:
            name = Path(part).name.lower()
            if name in FORBIDDEN_COMMAND_TOKENS:
                return False
    return True


def parse_probe_summary(output_path: Path) -> dict[str, Any]:
    text = output_path.read_text(errors="replace") if output_path.is_file() else ""
    m = SUMMARY_RE.search(text)
    ok_lines = [line for line in text.splitlines() if line.startswith("ok hash=")]
    return {
        "output": str(output_path),
        "summary_found": bool(m),
        "total": int(m.group(1)) if m else 0,
        "failures": int(m.group(2)) if m else None,
        "ok_lines": len(ok_lines),
        "second_cache_hits": sum("second_cache_hit=1" in line for line in ok_lines),
        "text_tail": text[-2000:],
    }


def render_summary(summary: dict[str, Any]) -> str:
    lines = [
        "# M12 generated-MSL metallib Phase 5B offline proof",
        "",
        f"Result: {'PASS' if summary['passed'] else 'FAIL'}",
        "",
        "## Scope",
        "",
        "- Offline only: no Steam, Wine, wineserver, AC6 launch, or runtime staging.",
        "- Uses a proof cache copied from AC6 generated MSL sidecars; live shader cache is not mutated.",
        "- Proves .metallib write/persist plus actual load/use through Metal and M12Core.",
        "- Does not rewrite or perturb the proven DXIL/HLSL -> generated-MSL path.",
        "",
        "## Selected hashes",
        "",
    ]
    for row in summary["sample_rows"]:
        lines.append(f"- `{row['hash']}` size={row.get('source_size')} sha256=`{row.get('source_sha256')}`")
    lines.extend(["", "## Acceptance", ""])
    for key, value in summary["acceptance"].items():
        lines.append(f"- {key}: `{value}`")
    lines.extend(["", "## Load proof", ""])
    lines.append(f"- Direct Metal probe: `{summary['metal_probe']}`")
    lines.append(f"- M12Core probe: `{summary['m12core_probe']}`")
    lines.extend(["", "## Timeout self-test", ""])
    timeout_selftest = summary.get("timeout_selftest", {})
    if timeout_selftest:
        lines.append(f"- rc={timeout_selftest.get('returncode')} timeout={timeout_selftest.get('timeout')} still_running_after_sigkill={timeout_selftest.get('still_running_after_sigkill')}")
        lines.append(f"  - stdout: `{timeout_selftest.get('stdout')}` size={timeout_selftest.get('stdout_size')}")
        lines.append(f"  - stderr: `{timeout_selftest.get('stderr')}` size={timeout_selftest.get('stderr_size')}")
    lines.extend(["", "## Commands", ""])
    for cmd in summary["commands"]:
        lines.append(f"- rc={cmd['returncode']} timeout={cmd['timeout']} cmd=`{' '.join(cmd['cmd'])}`")
        lines.append(f"  - stdout: `{cmd['stdout']}` size={cmd.get('stdout_size')}")
        lines.append(f"  - stderr: `{cmd['stderr']}` size={cmd.get('stderr_size')}")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache-dir", type=Path, default=DEFAULT_AC6_CACHE)
    parser.add_argument("--results-dir", type=Path)
    parser.add_argument("--hash", action="append", default=[])
    parser.add_argument("--hash-file", type=Path)
    parser.add_argument("--limit", type=int, default=8)
    parser.add_argument("--workers", type=int, default=2)
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()

    cache_dir = args.cache_dir.expanduser().resolve()
    if not cache_dir.is_dir():
        raise SystemExit(f"missing cache dir: {cache_dir}")

    hashes: list[str] = [normalize_hash(h) for h in args.hash]
    if args.hash_file:
        hashes.extend(read_hashes(args.hash_file.expanduser().resolve()))
    if not hashes:
        hashes = discover_hashes(cache_dir, args.limit)
    hashes = sorted(dict.fromkeys(hashes))
    if not hashes:
        raise SystemExit(f"no .msl shader hashes selected from {cache_dir}")

    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = (args.results_dir or RESULTS / f"m12-metallib-phase5b-proof-{stamp}").resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    proof_cache = out_dir / "proof-cache"
    hash_file = out_dir / "selected-hashes.txt"
    hash_file.write_text("\n".join(hashes) + "\n")

    before_runtime = runtime_snapshot()
    before_selected_cache = selected_cache_snapshot(cache_dir, hashes)
    sample_rows = copy_sample(cache_dir, proof_cache, hashes)
    sample_ok = all(row.get("source_exists") and row.get("copy_sha256") == row.get("source_sha256") for row in sample_rows)

    commands: list[dict[str, Any]] = []

    materialize_json = out_dir / "materialize.json"
    commands.append(run_bounded(
        [
            sys.executable,
            str(MATERIALIZE_SCRIPT),
            "--cache-dir",
            str(proof_cache),
            "--hash-file",
            str(hash_file),
            "--workers",
            str(args.workers),
            "--timeout",
            str(args.timeout),
            "--strict",
            "--out",
            str(materialize_json),
        ],
        stdout=out_dir / "materialize.stdout.txt",
        stderr=out_dir / "materialize.stderr.txt",
        timeout=max(args.timeout * max(1, len(hashes)), args.timeout),
    ))

    freshness_json = out_dir / "freshness.json"
    commands.append(run_bounded(
        [
            sys.executable,
            str(FRESHNESS_SCRIPT),
            "--cache-dir",
            str(proof_cache),
            "--hash-file",
            str(hash_file),
            "--strict",
            "--out",
            str(freshness_json),
        ],
        stdout=out_dir / "freshness.stdout.txt",
        stderr=out_dir / "freshness.stderr.txt",
        timeout=args.timeout,
    ))

    contract_json = out_dir / "runtime-contract.json"
    commands.append(run_bounded(
        [
            sys.executable,
            str(RUNTIME_CONTRACT_SCRIPT),
            "--source",
            str(PIPELINE_STATE),
            "--json",
            str(contract_json),
        ],
        stdout=out_dir / "runtime-contract.stdout.txt",
        stderr=out_dir / "runtime-contract.stderr.txt",
        timeout=args.timeout,
    ))

    metal_probe_dir = out_dir / "metal-probe"
    metal_env = os.environ.copy()
    metal_env["METAL_PROBE_OUT_DIR"] = str(metal_probe_dir)
    commands.append(run_bounded(
        [str(METAL_PROBE_SCRIPT), "--cache-dir", str(proof_cache), "--hash-file", str(hash_file)],
        stdout=out_dir / "metal-probe.stdout.txt",
        stderr=out_dir / "metal-probe.stderr.txt",
        env=metal_env,
        timeout=args.timeout,
    ))

    m12_probe_dir = out_dir / "m12core-probe"
    m12_env = os.environ.copy()
    m12_env["M12_PROBE_OUT_DIR"] = str(m12_probe_dir)
    commands.append(run_bounded(
        [str(M12_PROBE_SCRIPT), "--cache-dir", str(proof_cache), "--hash-file", str(hash_file)],
        stdout=out_dir / "m12core-probe.stdout.txt",
        stderr=out_dir / "m12core-probe.stderr.txt",
        env=m12_env,
        timeout=args.timeout,
    ))

    timeout_selftest = run_bounded(
        [sys.executable, "-c", "import time; time.sleep(30)"],
        stdout=out_dir / "timeout-selftest.stdout.txt",
        stderr=out_dir / "timeout-selftest.stderr.txt",
        timeout=1,
    )

    after_runtime = runtime_snapshot()
    after_selected_cache = selected_cache_snapshot(cache_dir, hashes)

    materialize = read_json(materialize_json) if materialize_json.is_file() else {}
    freshness = read_json(freshness_json) if freshness_json.is_file() else {}
    contract = read_json(contract_json) if contract_json.is_file() else {}
    metal_probe = parse_probe_summary(metal_probe_dir / "probe-output.txt")
    m12core_probe = parse_probe_summary(m12_probe_dir / "probe-output.txt")

    materialize_counts = materialize.get("counts", {})
    materialized_ok = (
        materialize.get("total") == len(hashes)
        and materialize_counts.get("missing_source", 0) == 0
        and not any(k.endswith("failed") or k == "empty" for k in materialize_counts)
        and materialize_counts.get("ok", 0) + materialize_counts.get("fresh", 0) == len(hashes)
    )
    freshness_ok = freshness.get("total") == len(hashes) and freshness.get("fresh") == len(hashes) and not freshness.get("nonfresh")
    metal_load_ok = metal_probe["summary_found"] and metal_probe["total"] == len(hashes) and metal_probe["failures"] == 0 and metal_probe["ok_lines"] == len(hashes)
    m12core_load_ok = (
        m12core_probe["summary_found"]
        and m12core_probe["total"] == len(hashes)
        and m12core_probe["failures"] == 0
        and m12core_probe["ok_lines"] == len(hashes)
        and m12core_probe["second_cache_hits"] == len(hashes)
    )
    commands_ok = all(c["returncode"] == 0 and not c["timeout"] and not c["still_running_after_sigkill"] for c in commands)
    timeout_kill_ok = bool(timeout_selftest["timeout"] and timeout_selftest["returncode"] != 0 and not timeout_selftest["still_running_after_sigkill"])

    acceptance = {
        "selected_real_ac6_generated_msl_sidecars": sample_ok,
        "proof_cache_only_live_shader_cache_not_mutated": sample_ok and before_selected_cache == after_selected_cache,
        "metallib_materialization_succeeded": materialized_ok,
        "metallibs_are_fresh_nonzero_with_mtlb_header": freshness_ok,
        "metallibs_load_with_metal_newLibraryWithData": metal_load_ok,
        "metallibs_load_with_m12core_shader_function_path": m12core_load_ok,
        "m12core_second_load_hits_function_cache": bool(m12core_probe["second_cache_hits"] == len(hashes)),
        "dxmt_runtime_contract_prefers_metallib_before_msl_dxil_fallback": bool(contract.get("ok")),
        "commands_passed": commands_ok,
        "hard_timeout_process_group_kill_active": timeout_kill_ok,
        "no_wine_steam_ac6_runtime_staging_commands_requested": command_plan_is_offline(commands),
        "dxmt_m12_runtime_snapshot_unchanged": before_runtime == after_runtime,
    }

    summary = {
        "schema": "metalsharp.m12.metallib_phase5b_proof.v1",
        "passed": all(acceptance.values()),
        "results_dir": str(out_dir),
        "source_cache_dir": str(cache_dir),
        "proof_cache_dir": str(proof_cache),
        "selected_hash_file": str(hash_file),
        "selected_hashes": hashes,
        "sample_rows": sample_rows,
        "live_cache_before": before_selected_cache,
        "live_cache_after": after_selected_cache,
        "materialize": materialize,
        "freshness": freshness,
        "runtime_contract": contract,
        "metal_probe": metal_probe,
        "m12core_probe": m12core_probe,
        "commands": commands,
        "timeout_selftest": timeout_selftest,
        "acceptance": acceptance,
        "runtime_snapshot_file_count": len(before_runtime),
    }
    (out_dir / "phase5b-proof-summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    (out_dir / "phase5b-proof-summary.md").write_text(render_summary(summary))
    print(out_dir / "phase5b-proof-summary.md")
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
