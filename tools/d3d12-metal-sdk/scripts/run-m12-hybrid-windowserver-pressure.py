#!/usr/bin/env python3
"""Run native AC6 PSO/drawable pressure concurrently with Wine/DXMT swapchain pressure.

This is an offline WindowServer-focused harness: it does not launch games.  It
combines the native AC6 corpus pressure probe with the Wine-side D3D12 windowed
swapchain probe so Metal compiler/pipeline work, CAMetalLayer presents, and the
DXMT/winemetal Wine bridge overlap in time.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT / "tools" / "d3d12-metal-sdk"


def stamp() -> str:
    return dt.datetime.now().strftime("%Y%m%d-%H%M%S")


def start_logged(name: str, cmd: list[str], out_dir: Path, env: dict[str, str]) -> tuple[str, subprocess.Popen[str], object, object]:
    stdout = (out_dir / f"{name}.stdout.txt").open("w")
    stderr = (out_dir / f"{name}.stderr.txt").open("w")
    (out_dir / f"{name}.command.txt").write_text(" ".join(cmd) + "\n")
    proc = subprocess.Popen(cmd, cwd=ROOT, text=True, stdout=stdout, stderr=stderr, env=env)
    return name, proc, stdout, stderr


def wait_bounded(children: list[tuple[str, subprocess.Popen[str], object, object]], timeout_sec: int) -> list[dict[str, object]]:
    deadline = time.time() + timeout_sec
    results: list[dict[str, object]] = []
    for name, proc, stdout, stderr in children:
        timed_out = False
        try:
            rc = proc.wait(timeout=max(1, deadline - time.time()))
        except subprocess.TimeoutExpired:
            timed_out = True
            proc.terminate()
            try:
                rc = proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
                rc = proc.wait(timeout=10)
        stdout.close()
        stderr.close()
        results.append({"name": name, "pid": proc.pid, "returncode": rc, "timed_out": timed_out})
    return results


def main() -> int:
    parser = argparse.ArgumentParser(description="Run hybrid native+Wine offline WindowServer pressure.")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--results-dir", type=Path, default=SDK / "results")
    parser.add_argument("--timeout-sec", type=int, default=180)
    parser.add_argument("--native-max-pipelines", type=int, default=1024)
    parser.add_argument("--native-min-command-buffers", type=int, default=4000)
    parser.add_argument("--native-pso-workers", type=int, default=8)
    parser.add_argument("--native-window-count", type=int, default=4)
    parser.add_argument("--wine-stress-frames", type=int, default=2000)
    parser.add_argument("--onscreen-window", action="store_true")
    parser.add_argument("--no-build", action="store_true")
    args = parser.parse_args()

    out_dir = args.results_dir / f"m12-hybrid-native-wine-windowserver-{stamp()}"
    out_dir.mkdir(parents=True, exist_ok=True)

    native_cmd = [
        str(SDK / "scripts" / "run-m12-pso-submit-pressure.py"),
        "--manifest", str(args.manifest),
        "--mode", "pso-drawable-cmdb",
        "--pso-workers", str(args.native_pso_workers),
        "--iterations", "1",
        "--max-pipelines", str(args.native_max_pipelines),
        "--queue-depth", "8",
        "--duration-ms", "60000",
        "--timeout-sec", str(args.timeout_sec),
        "--slot-wait-ms", "0",
        "--salt-cache-keys",
        "--display-sync",
        "--drawable-window-count", str(args.native_window_count),
        "--drawable-churn-interval", "32",
        "--min-command-buffers", str(args.native_min_command_buffers),
    ]
    if args.onscreen_window:
        native_cmd.append("--onscreen-window")
    if args.no_build:
        native_cmd.append("--no-build")

    wine_results = out_dir / "wine-swapchain"
    wine_cmd = [
        str(SDK / "scripts" / "run-probes.sh"),
        "--profile", "metalsharp",
        "--swapchain-only",
        "--results-dir", str(wine_results),
    ]
    wine_env = os.environ.copy()
    wine_env["M12_PRESENT_WINDOWED_STRESS_FRAMES"] = str(args.wine_stress_frames)

    children = [
        start_logged("native", native_cmd, out_dir, os.environ.copy()),
        start_logged("wine", wine_cmd, out_dir, wine_env),
    ]
    results = wait_bounded(children, args.timeout_sec)

    index = {
        "schema": "metalsharp.m12.hybrid-windowserver-pressure.v1",
        "manifest": str(args.manifest),
        "results_dir": str(out_dir),
        "children": results,
        "wine_stress_frames": args.wine_stress_frames,
        "native_max_pipelines": args.native_max_pipelines,
        "native_min_command_buffers": args.native_min_command_buffers,
        "native_window_count": args.native_window_count,
    }
    (out_dir / "index.json").write_text(json.dumps(index, indent=2) + "\n")
    print(out_dir)
    return 1 if any(child["returncode"] != 0 or child["timed_out"] for child in results) else 0


if __name__ == "__main__":
    raise SystemExit(main())
