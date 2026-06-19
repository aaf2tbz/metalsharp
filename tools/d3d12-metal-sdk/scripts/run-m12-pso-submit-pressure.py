#!/usr/bin/env python3
"""Compile and run the native M12 PSO + command-buffer pressure probe.

The runner enforces a wall-clock timeout.  On timeout it captures external
`sample` and batch `lldb` thread stacks for the probe process, then terminates
only that probe process.  It does not touch Steam, Wine, the backend, or live
shader caches.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT / "tools" / "d3d12-metal-sdk"
PROBE_SRC = SDK / "probes" / "probe_m12_pso_submit_pressure" / "probe_m12_pso_submit_pressure.mm"
PROBE_BIN = SDK / "out" / "native" / "probe_m12_pso_submit_pressure"
M12CORE_DIR = ROOT / "vendor" / "dxmt" / "build-metalsharp-x64" / "src" / "m12core"
M12_WINE_UNIX_DIR = Path(os.environ.get("M12_WINE_UNIX_DIR", str(Path.home() / ".metalsharp" / "runtime" / "wine" / "lib" / "dxmt_m12" / "x86_64-unix")))


def stamp() -> str:
    return dt.datetime.now().strftime("%Y%m%d-%H%M%S")


def run(cmd: list[str], *, timeout: int | None = None, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout, env=env)


def compile_probe() -> None:
    PROBE_BIN.parent.mkdir(parents=True, exist_ok=True)
    dylib = M12CORE_DIR / "libm12core.dylib"
    if not dylib.exists():
        raise SystemExit(f"missing libm12core.dylib; build m12core first: {dylib}")
    cmd = [
        "clang++",
        "-arch",
        "x86_64",
        "-std=c++17",
        "-fobjc-arc",
        f"-I{ROOT / 'vendor/dxmt/src/m12core'}",
        f"-I{ROOT / 'vendor/dxmt/src/winemetal'}",
        "-framework",
        "AppKit",
        "-framework",
        "Foundation",
        "-framework",
        "Metal",
        "-framework",
        "QuartzCore",
        str(PROBE_SRC),
        f"-L{M12CORE_DIR}",
        "-lm12core",
        f"-Wl,-rpath,{M12CORE_DIR}",
        f"-Wl,-rpath,{M12_WINE_UNIX_DIR}",
        "-Wl,-rpath,/usr/lib",
        "-o",
        str(PROBE_BIN),
    ]
    completed = run(cmd, timeout=120)
    if completed.returncode != 0:
        sys.stderr.write(completed.stdout)
        sys.stderr.write(completed.stderr)
        raise SystemExit(completed.returncode)


def capture_timeout(pid: int, out_dir: Path) -> None:
    sample = shutil.which("sample")
    if sample:
        with (out_dir / "sample.txt").open("w") as f:
            subprocess.run([sample, str(pid), "5"], stdout=f, stderr=subprocess.STDOUT, timeout=15)
    lldb = shutil.which("lldb")
    if lldb:
        with (out_dir / "lldb-bt-all.txt").open("w") as out, (out_dir / "lldb-bt-all.stderr").open("w") as err:
            subprocess.run(
                [lldb, "-p", str(pid), "-b", "-o", "thread backtrace all", "-o", "detach", "-o", "quit"],
                stdout=out,
                stderr=err,
                timeout=25,
            )


def run_one(args: argparse.Namespace, mode: str, queue_depth: int | None = None) -> tuple[Path, int, bool]:
    label = mode if queue_depth is None else f"{mode}-q{queue_depth}"
    out_dir = args.results_dir / f"m12-pso-submit-pressure-{label}-{stamp()}"
    out_dir.mkdir(parents=True, exist_ok=True)
    result_json = out_dir / "result.json"
    stdout_path = out_dir / "stdout.txt"
    stderr_path = out_dir / "stderr.txt"

    env = os.environ.copy()
    env["DYLD_LIBRARY_PATH"] = f"{M12CORE_DIR}:{M12_WINE_UNIX_DIR}:{env.get('DYLD_LIBRARY_PATH', '')}"

    cmd = [
        str(PROBE_BIN),
        "--manifest",
        str(args.manifest),
        "--output",
        str(result_json),
        "--mode",
        "pso-cmdb" if mode in {"pso-cmdb", "queue-depth"} else mode,
        "--pso-workers",
        str(args.pso_workers),
        "--iterations",
        str(args.iterations),
        "--queue-depth",
        str(queue_depth if queue_depth is not None else args.queue_depth),
        "--duration-ms",
        str(args.duration_ms),
        "--slot-wait-ms",
        str(args.slot_wait_ms),
    ]
    if args.max_pipelines:
        cmd += ["--max-pipelines", str(args.max_pipelines)]
    if args.min_command_buffers:
        cmd += ["--min-command-buffers", str(args.min_command_buffers)]
    if args.drawable_window_count != 1:
        cmd += ["--drawable-window-count", str(args.drawable_window_count)]
    if args.drawable_churn_interval:
        cmd += ["--drawable-churn-interval", str(args.drawable_churn_interval)]
    if args.salt_cache_keys:
        cmd.append("--salt-cache-keys")
    if args.onscreen_window:
        cmd.append("--onscreen-window")
    if args.display_sync:
        cmd.append("--display-sync")

    (out_dir / "command.txt").write_text(" ".join(cmd) + "\n")
    proc = subprocess.Popen(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
    timed_out = False
    try:
        stdout, stderr = proc.communicate(timeout=args.timeout_sec)
    except subprocess.TimeoutExpired:
        timed_out = True
        capture_timeout(proc.pid, out_dir)
        proc.terminate()
        try:
            stdout, stderr = proc.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            stdout, stderr = proc.communicate(timeout=10)
    stdout_path.write_text(stdout or "")
    stderr_path.write_text(stderr or "")

    summary = [
        f"# M12 PSO submit pressure run: {label}",
        "",
        f"- manifest: `{args.manifest}`",
        f"- result dir: `{out_dir}`",
        f"- return code: {proc.returncode}",
        f"- timed out: {timed_out}",
        f"- queue depth: {queue_depth if queue_depth is not None else args.queue_depth}",
        f"- pso workers: {args.pso_workers}",
        f"- iterations: {args.iterations}",
        f"- slot wait ms: {args.slot_wait_ms} (`0` means direct waitUntilCompleted)",
    ]
    if result_json.exists():
        try:
            result = json.loads(result_json.read_text())
            summary.extend([
                "",
                "## Result counters",
                "",
                f"- ok: {result.get('ok')}",
                f"- elapsed_ms: {result.get('elapsed_ms')}",
                f"- pso_ok: {result.get('pso_ok')}",
                f"- pso_failed: {result.get('pso_failed')}",
                f"- pso_cache_hits: {result.get('pso_cache_hits')}",
                f"- shader_failed: {result.get('shader_failed')}",
                f"- command_buffers_committed: {result.get('command_buffers_committed')}",
                f"- command_buffers_completed: {result.get('command_buffers_completed')}",
                f"- command_buffers_error_status: {result.get('command_buffers_error_status')}",
                f"- render_pool_count: {result.get('render_pool_count')}",
                f"- render_passes_encoded: {result.get('render_passes_encoded')}",
                f"- render_passes_skipped: {result.get('render_passes_skipped')}",
                f"- render_passes_failed: {result.get('render_passes_failed')}",
                f"- window_mode: {result.get('window_mode')}",
                f"- display_sync: {result.get('display_sync')}",
                f"- min_command_buffers: {result.get('min_command_buffers')}",
                f"- drawable_window_count: {result.get('drawable_window_count')}",
                f"- drawable_churn_interval: {result.get('drawable_churn_interval')}",
                f"- drawables_requested: {result.get('drawables_requested')}",
                f"- drawables_acquired: {result.get('drawables_acquired')}",
                f"- drawable_passes_encoded: {result.get('drawable_passes_encoded')}",
                f"- drawable_passes_failed: {result.get('drawable_passes_failed')}",
                f"- drawable_presents: {result.get('drawable_presents')}",
                f"- drawable_nil: {result.get('drawable_nil')}",
                f"- slot_timeouts: {result.get('slot_timeouts')}",
            ])
        except Exception as exc:
            summary.append(f"- result parse error: {exc}")
    if timed_out:
        summary.extend([
            "",
            "## Timeout diagnostics",
            "",
            "The probe exceeded the bounded timeout. External stacks were captured when available:",
            "",
            f"- sample: `{out_dir / 'sample.txt'}`",
            f"- lldb: `{out_dir / 'lldb-bt-all.txt'}`",
        ])
    (out_dir / "summary.md").write_text("\n".join(summary) + "\n")
    return out_dir, proc.returncode if proc.returncode is not None else -1, timed_out


def main() -> int:
    parser = argparse.ArgumentParser(description="Run the native M12 PSO + command-buffer pressure harness with timeout diagnostics.")
    parser.add_argument("--manifest", type=Path, required=True, help="Corpus manifest.json from build-ac6-pso-pressure-corpus.py")
    parser.add_argument("--results-dir", type=Path, default=SDK / "results", help="Directory for run outputs.")
    parser.add_argument("--mode", choices=["baseline", "storm", "pso-cmdb", "pso-render-cmdb", "pso-drawable-cmdb", "queue-depth"], default="pso-cmdb")
    parser.add_argument("--pso-workers", type=int, default=4)
    parser.add_argument("--iterations", type=int, default=1)
    parser.add_argument("--queue-depth", type=int, default=8)
    parser.add_argument("--queue-depths", default="4,8,16,32", help="Comma-separated queue depths for --mode queue-depth.")
    parser.add_argument("--duration-ms", type=int, default=30000)
    parser.add_argument("--slot-wait-ms", type=int, default=0, help="0 calls waitUntilCompleted; >0 polls completion handler for N ms.")
    parser.add_argument("--timeout-sec", type=int, default=60)
    parser.add_argument("--max-pipelines", type=int, default=0)
    parser.add_argument("--min-command-buffers", type=int, default=0, help="Keep the command loop running until at least N command buffers have been committed, even after PSO workers finish.")
    parser.add_argument("--drawable-window-count", type=int, default=1, help="Number of CAMetalLayer-backed windows/layers to cycle through in drawable mode.")
    parser.add_argument("--drawable-churn-interval", type=int, default=0, help="Resize/churn the CAMetalLayer drawable every N command buffers in drawable mode.")
    parser.add_argument("--salt-cache-keys", action="store_true", help="Force per-worker/per-iteration M12Core pipeline-cache misses.")
    parser.add_argument("--onscreen-window", action="store_true", help="Use a tiny visible borderless CAMetalLayer window instead of an offscreen/near-transparent one.")
    parser.add_argument("--display-sync", action="store_true", help="Enable CAMetalLayer displaySync for WindowServer/display-link pressure.")
    parser.add_argument("--no-build", action="store_true")
    args = parser.parse_args()

    if not args.manifest.exists():
        raise SystemExit(f"manifest not found: {args.manifest}")
    if not args.no_build:
        compile_probe()
    args.results_dir.mkdir(parents=True, exist_ok=True)

    runs: list[tuple[Path, int, bool]] = []
    if args.mode == "queue-depth":
        depths = [int(item) for item in args.queue_depths.split(",") if item.strip()]
        for depth in depths:
            runs.append(run_one(args, "queue-depth", depth))
    else:
        runs.append(run_one(args, args.mode))

    index = {
        "schema": "metalsharp.m12.pso-submit-pressure-run-index.v1",
        "runs": [{"dir": str(path), "returncode": rc, "timed_out": timed_out} for path, rc, timed_out in runs],
    }
    index_path = args.results_dir / f"m12-pso-submit-pressure-index-{stamp()}.json"
    index_path.write_text(json.dumps(index, indent=2) + "\n")
    print(index_path)
    return 1 if any(rc != 0 or timed_out for _, rc, timed_out in runs) else 0


if __name__ == "__main__":
    raise SystemExit(main())
