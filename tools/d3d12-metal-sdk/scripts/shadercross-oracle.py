#!/usr/bin/env python3
import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path


def sdk_root() -> Path:
    return Path(__file__).resolve().parents[1]


def repo_root() -> Path:
    return sdk_root().parents[1]


def first_existing(paths: list[Path]) -> Path | None:
    for path in paths:
        if path and path.exists():
            return path
    return None


def default_dxc() -> str | None:
    native = shutil.which("dxc")
    if native:
        return native
    env = os.environ.get("DXC_EXE")
    if env:
        return env
    cached = first_existing(
        [
            sdk_root() / "out/bin/dxc.exe",
            sdk_root() / "cache/dxc/v1.9.2602/bin/x64/dxc.exe",
            Path("/Volumes/AverySSD/offload-2026-05-24/tmp/metalsharp-dxc/extracted/bin/x64/dxc.exe"),
        ]
    )
    return str(cached) if cached else None


def default_wine() -> str | None:
    env = os.environ.get("WINE")
    if env:
        return env
    candidates = [
        Path.home() / ".metalsharp/runtime/wine/bin/wine64",
        Path.home() / ".metalsharp/runtime/wine/bin/wine",
    ]
    existing = first_existing(candidates)
    if existing:
        return str(existing)
    return shutil.which("wine64") or shutil.which("wine")


def default_spirv_cross() -> str | None:
    env = os.environ.get("SPIRV_CROSS")
    if env:
        return env
    return shutil.which("spirv-cross")


def is_windows_exe(path: str) -> bool:
    return path.lower().endswith(".exe")


def run(command: list[str], cwd: Path | None = None) -> dict:
    completed = subprocess.run(
        command,
        cwd=str(cwd) if cwd else None,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return {
        "command": command,
        "returncode": completed.returncode,
        "stdout": completed.stdout,
        "stderr": completed.stderr,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Compile an HLSL probe through DXC SPIR-V and SPIRV-Cross MSL. "
            "This is an offline oracle for D3D12->Metal type/layout fixes; it "
            "does not decompile captured DXIL blobs."
        )
    )
    parser.add_argument("--hlsl", required=True, help="HLSL source file.")
    parser.add_argument("--entry", required=True, help="Entry point, e.g. VSMain.")
    parser.add_argument("--profile", required=True, help="DXC profile, e.g. vs_6_6 or ps_6_6.")
    parser.add_argument("--out-dir", default=str(sdk_root() / "results/shadercross-oracle"))
    parser.add_argument("--dxc", default=default_dxc())
    parser.add_argument("--wine", default=default_wine())
    parser.add_argument("--spirv-cross", default=default_spirv_cross())
    parser.add_argument("--extra-dxc", action="append", default=[], help="Extra arg passed to dxc.")
    parser.add_argument("--extra-spirv-cross", action="append", default=[], help="Extra arg passed to spirv-cross.")
    args = parser.parse_args()

    hlsl = Path(args.hlsl).resolve()
    if not hlsl.exists():
        print(f"HLSL source not found: {hlsl}", file=sys.stderr)
        return 2
    if not args.dxc:
        print("dxc not found. Set DXC_EXE or run scripts/fetch-dxc.sh.", file=sys.stderr)
        return 2
    if is_windows_exe(args.dxc) and not args.wine:
        print("Windows dxc.exe was found, but no Wine executable is available.", file=sys.stderr)
        return 2
    if not args.spirv_cross:
        print("spirv-cross not found. Install it or set SPIRV_CROSS.", file=sys.stderr)
        return 2

    out_dir = Path(args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    stem = f"{hlsl.stem}-{args.entry}-{args.profile}".replace(os.sep, "_")
    spirv = out_dir / f"{stem}.spv"
    msl = out_dir / f"{stem}.msl"
    result_path = out_dir / f"{stem}.json"

    dxc_cmd = [
        args.dxc,
        "-T",
        args.profile,
        "-E",
        args.entry,
        "-spirv",
        "-fspv-target-env=vulkan1.2",
        "-fvk-use-dx-layout",
        "-Fo",
        str(spirv),
        str(hlsl),
    ]
    dxc_cmd.extend(args.extra_dxc)
    if is_windows_exe(args.dxc):
        dxc_cmd = [args.wine, args.dxc, *dxc_cmd[1:]]

    spirv_cross_cmd = [
        args.spirv_cross,
        str(spirv),
        "--msl",
        "--msl-version",
        "30000",
        "--output",
        str(msl),
    ]
    spirv_cross_cmd.extend(args.extra_spirv_cross)

    dxc_result = run(dxc_cmd, cwd=repo_root())
    spirv_result = None
    if dxc_result["returncode"] == 0 and spirv.exists():
        spirv_result = run(spirv_cross_cmd, cwd=repo_root())

    result = {
        "schema": "metalsharp.d3d12-metal.shadercross-oracle.v1",
        "ok": dxc_result["returncode"] == 0
        and spirv.exists()
        and spirv_result is not None
        and spirv_result["returncode"] == 0
        and msl.exists(),
        "hlsl": str(hlsl),
        "entry": args.entry,
        "profile": args.profile,
        "spirv": str(spirv),
        "spirv_exists": spirv.exists(),
        "msl": str(msl),
        "msl_exists": msl.exists(),
        "dxc": {
            "command": dxc_result["command"],
            "returncode": dxc_result["returncode"],
            "stdout_tail": dxc_result["stdout"][-4000:],
            "stderr_tail": dxc_result["stderr"][-4000:],
        },
        "spirv_cross": None
        if spirv_result is None
        else {
            "command": spirv_result["command"],
            "returncode": spirv_result["returncode"],
            "stdout_tail": spirv_result["stdout"][-4000:],
            "stderr_tail": spirv_result["stderr"][-4000:],
        },
    }
    result_path.write_text(json.dumps(result, indent=2) + "\n")
    print(result_path)
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
