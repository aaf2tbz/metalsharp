#!/usr/bin/env python3
"""Validate the M12 staged runtime alignment without launching a game.

This is intentionally narrower than the legacy broad probe runner. It checks the
M12 artifact contract end-to-end:
  * runtime/preflight alignment for d3d12/dxgi/dxgi_dxmt/winemetal artifacts
  * Wine override and WINEDLLPATH shape used by M12 launches
  * a tiny PE program can load the staged modules and create D3D12/DXGI objects

No commercial game is launched. A temporary app directory is populated from the
already-staged game-local directory (when --game-dir is provided) or the prefix
system32 fallback, then deleted/replaced on each run.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT / "tools" / "d3d12-metal-sdk"
PROBE_SOURCE = SDK / "probes" / "probe_m12_runtime_alignment" / "probe_m12_runtime_alignment.cpp"
PROBE_EXE = SDK / "out" / "bin" / "probe_m12_runtime_alignment.exe"
ROUTE_DLLS = ["d3d12.dll", "dxgi.dll", "dxgi_dxmt.dll", "winemetal.dll"]
M12_OVERRIDES = "d3d12,dxgi,dxgi_dxmt,winemetal=n,b;gameoverlayrenderer,gameoverlayrenderer64=d"


def sha256(path: Path) -> str | None:
    if not path.exists():
        return None
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def run(command: list[str], *, cwd: Path | None = None, env: dict[str, str] | None = None, timeout: int = 120) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)


def ensure_probe_built(cxx: str) -> dict:
    needs_build = not PROBE_EXE.exists() or (PROBE_EXE.stat().st_mtime < PROBE_SOURCE.stat().st_mtime)
    result: dict = {"path": str(PROBE_EXE), "rebuilt": False, "command": None, "returncode": 0, "stdout": "", "stderr": ""}
    if not needs_build:
        return result
    PROBE_EXE.parent.mkdir(parents=True, exist_ok=True)
    command = [cxx, "-std=c++17", "-O2", "-static", "-static-libgcc", "-static-libstdc++", str(PROBE_SOURCE), "-o", str(PROBE_EXE)]
    proc = run(command, timeout=180)
    result.update({
        "rebuilt": True,
        "command": command,
        "returncode": proc.returncode,
        "stdout": proc.stdout,
        "stderr": proc.stderr,
    })
    return result


def parse_probe_json(stdout: str) -> dict | None:
    start = stdout.find("{")
    end = stdout.rfind("}")
    if start == -1 or end == -1 or end <= start:
        return None
    try:
        return json.loads(stdout[start : end + 1])
    except json.JSONDecodeError:
        return None


def copy_route_artifacts(source_dir: Path, work_dir: Path) -> list[dict]:
    work_dir.mkdir(parents=True, exist_ok=True)
    copied: list[dict] = []
    for name in ROUTE_DLLS:
        src = source_dir / name
        dst = work_dir / name
        if src.exists():
            shutil.copy2(src, dst)
        copied.append({
            "filename": name,
            "source_path": str(src),
            "source_exists": src.exists(),
            "source_sha256": sha256(src),
            "work_path": str(dst),
            "work_exists": dst.exists(),
            "work_sha256": sha256(dst),
            "matches_source": sha256(src) is not None and sha256(src) == sha256(dst),
        })
    shutil.copy2(PROBE_EXE, work_dir / PROBE_EXE.name)
    return copied


def main() -> int:
    ap = argparse.ArgumentParser(description="Run a no-game M12 runtime alignment dry-run mini probe.")
    ap.add_argument("--dxmt-runtime", type=Path, default=Path.home() / ".metalsharp/runtime/wine/lib/dxmt_m12")
    ap.add_argument("--wine-runtime", type=Path, default=Path.home() / ".metalsharp/runtime/wine")
    ap.add_argument("--prefix", type=Path, default=Path.home() / ".metalsharp/prefix-steam")
    ap.add_argument("--game-dir", type=Path, default=None, help="Optional game Win64 directory containing staged M12 route DLLs.")
    ap.add_argument("--results-dir", type=Path, default=SDK / "results" / "m12-runtime-alignment-dry-run")
    ap.add_argument("--profile", default="m12-runtime-alignment")
    ap.add_argument("--cxx", default=os.environ.get("CXX", "x86_64-w64-mingw32-g++"))
    ap.add_argument("--timeout", type=int, default=120)
    args = ap.parse_args()

    args.results_dir.mkdir(parents=True, exist_ok=True)
    work_dir = args.results_dir / "mini-probe-appdir"
    if work_dir.exists():
        shutil.rmtree(work_dir)
    work_dir.mkdir(parents=True)

    preflight_cmd = [
        sys.executable,
        str(SDK / "scripts" / "preflight-runtime-layout.py"),
        "--profile", args.profile,
        "--dxmt-runtime", str(args.dxmt_runtime),
        "--wine-runtime", str(args.wine_runtime),
        "--prefix", str(args.prefix),
        "--results-dir", str(args.results_dir),
    ]
    if args.game_dir:
        preflight_cmd.extend(["--game-dir", str(args.game_dir)])
    preflight = run(preflight_cmd, timeout=args.timeout)
    preflight_json_path = args.results_dir / f"runtime-preflight-{args.profile}.json"
    preflight_json = json.loads(preflight_json_path.read_text()) if preflight_json_path.exists() else None

    build = ensure_probe_built(args.cxx)
    source_dir = args.game_dir if args.game_dir else args.prefix / "drive_c" / "windows" / "system32"
    copied = copy_route_artifacts(source_dir, work_dir) if build.get("returncode") == 0 else []

    wine_bin = args.wine_runtime / "bin" / "metalsharp-wine"
    wine_unix_dir = args.wine_runtime / "lib" / "wine" / "x86_64-unix"
    env = os.environ.copy()
    dyld_parts = [str(wine_unix_dir), str(args.dxmt_runtime / "x86_64-unix")]
    if env.get("DYLD_LIBRARY_PATH"):
        dyld_parts.append(env["DYLD_LIBRARY_PATH"])
    env.update({
        "WINEPREFIX": str(args.prefix),
        "WINEDLLPATH": str(args.dxmt_runtime / "x86_64-windows"),
        "WINEDLLOVERRIDES": M12_OVERRIDES,
        "DXMT_WINEMETAL_UNIXLIB": "winemetal.so",
        "DYLD_LIBRARY_PATH": ":".join(dyld_parts),
        "WINEDEBUG": env.get("WINEDEBUG", "-all"),
    })

    probe_proc = None
    probe_json = None
    probe_cmd = [str(wine_bin), PROBE_EXE.name]
    if build.get("returncode") == 0 and all(item["matches_source"] for item in copied):
        try:
            probe_proc = run(probe_cmd, cwd=work_dir, env=env, timeout=args.timeout)
            probe_json = parse_probe_json(probe_proc.stdout)
        except subprocess.TimeoutExpired as exc:
            probe_proc = subprocess.CompletedProcess(probe_cmd, 124, exc.stdout or "", exc.stderr or "")

    summary = {
        "schema": "metalsharp.m12-runtime-alignment.dry-run.v1",
        "profile": args.profile,
        "timestamp_unix": int(time.time()),
        "pass": bool(
            preflight.returncode == 0
            and preflight_json
            and preflight_json.get("ok") is True
            and build.get("returncode") == 0
            and copied
            and all(item["matches_source"] for item in copied)
            and probe_proc
            and probe_proc.returncode == 0
            and probe_json
            and probe_json.get("pass") is True
        ),
        "source_mode": "game-dir" if args.game_dir else "prefix-system32",
        "source_dir": str(source_dir),
        "work_dir": str(work_dir),
        "runtime": {
            "dxmt_runtime": str(args.dxmt_runtime),
            "wine_runtime": str(args.wine_runtime),
            "prefix": str(args.prefix),
            "game_dir": str(args.game_dir) if args.game_dir else None,
        },
        "expected_env": {
            "WINEDLLOVERRIDES": M12_OVERRIDES,
            "WINEDLLPATH": str(args.dxmt_runtime / "x86_64-windows"),
            "DXMT_WINEMETAL_UNIXLIB": "winemetal.so",
        },
        "preflight": {
            "command": preflight_cmd,
            "returncode": preflight.returncode,
            "stdout": preflight.stdout,
            "stderr": preflight.stderr,
            "json_path": str(preflight_json_path),
            "json": preflight_json,
        },
        "probe_build": build,
        "copied_route_artifacts": copied,
        "probe": {
            "command": probe_cmd,
            "returncode": probe_proc.returncode if probe_proc else None,
            "stdout": probe_proc.stdout if probe_proc else "",
            "stderr": probe_proc.stderr if probe_proc else "",
            "json": probe_json,
        },
    }

    out = args.results_dir / "m12-runtime-alignment-dry-run-summary.json"
    out.write_text(json.dumps(summary, indent=2) + "\n")
    print(out)
    if not summary["pass"]:
        print(json.dumps({"pass": False, "summary": str(out)}, indent=2), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
