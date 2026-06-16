#!/usr/bin/env python3
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


def default_corpus_roots() -> list[Path]:
    roots = [
        Path.home() / ".metalsharp" / "shader-cache",
        Path("/Volumes/AverySSD/SteamLibrary/steamapps/common/Subnautica2/.metalsharp-cache/shader-cache"),
        Path("/tmp/dxmt_shader_cache"),
    ]
    return [root for root in roots if root.exists()]


def is_generated_geometry_mesh_sidecar(path: Path) -> bool:
    return path.name.endswith(".geom.gsmesh.dxbc")


def discover_dxbc_files(roots: list[Path]) -> tuple[list[Path], list[Path]]:
    files: list[Path] = []
    skipped: list[Path] = []
    for root in roots:
        for path in root.rglob("*.dxbc"):
            files.append(path)
    return sorted(set(files)), sorted(set(skipped))


def run_converter(tool: str, dxbc: Path, force: bool) -> dict:
    metallib = dxbc.with_suffix(".metallib")
    reflection = dxbc.with_suffix(".json")
    layout = dxbc.with_suffix(".vertex-layout.json")
    is_geometry_mesh = is_generated_geometry_mesh_sidecar(dxbc)
    uses_stage_in = layout.exists() and not is_geometry_mesh
    stage_in = dxbc.with_suffix(".stageIn.metallib")
    fail_marker = dxbc.with_suffix(".msc.fail")

    if fail_marker.exists() and force:
        fail_marker.unlink()

    if metallib.exists() and reflection.exists() and (not uses_stage_in or stage_in.exists()) and not force:
        return {
            "dxbc": str(dxbc),
            "status": "cached",
            "metallib": str(metallib),
            "reflection": str(reflection),
            "stage_in": str(stage_in) if uses_stage_in else "",
            "uses_gs_ts_emulation": uses_stage_in,
        }

    command = [
        tool,
        "-o",
        str(metallib),
        str(dxbc),
        f"--output-reflection-file={reflection}",
        "--deployment-os=macOS",
        "--minimum-os-build-version=15.0.0",
    ]
    if uses_stage_in:
        command.extend(["--enable-gs-ts-emulation", "--vertex-stage-in", f"--vertex-input-layout-file={layout}"])

    completed = subprocess.run(
        command,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    ok = completed.returncode == 0 and metallib.exists() and reflection.exists() and (
        not uses_stage_in or stage_in.exists()
    )
    fail_marker_error = ""
    if not ok:
        try:
            fail_marker.parent.mkdir(parents=True, exist_ok=True)
            fail_marker.write_text(
                f"command={' '.join(command)}\n"
                f"returncode={completed.returncode}\n"
                f"stdout={completed.stdout}\n"
                f"stderr={completed.stderr}\n"
            )
        except OSError as error:
            fail_marker_error = str(error)
    return {
        "dxbc": str(dxbc),
        "status": "ok" if ok else "failed",
        "returncode": completed.returncode,
        "metallib": str(metallib),
        "metallib_exists": metallib.exists(),
        "reflection": str(reflection),
        "reflection_exists": reflection.exists(),
        "stage_in": str(stage_in) if uses_stage_in else "",
        "stage_in_exists": stage_in.exists() if uses_stage_in else False,
        "fail_marker": str(fail_marker) if not ok else "",
        "fail_marker_error": fail_marker_error,
        "uses_gs_ts_emulation": uses_stage_in,
        "stdout_tail": completed.stdout[-1000:],
        "stderr_tail": completed.stderr[-1000:],
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Replay dumped D3D12 shader corpus through MetalShaderConverter without launching a game."
    )
    parser.add_argument("--corpus", action="append", default=[], help="Directory containing .dxbc files.")
    parser.add_argument("--tool", default=os.environ.get("METAL_SHADER_CONVERTER", "metal-shaderconverter"))
    parser.add_argument("--profile", default="metalsharp")
    parser.add_argument("--results-dir", default=str(Path(__file__).resolve().parents[1] / "results"))
    parser.add_argument("--limit", type=int, default=0, help="Optional max number of shaders to replay.")
    parser.add_argument("--force", action="store_true", help="Re-run converter even if outputs exist.")
    parser.add_argument("--allow-empty", action="store_true", help="Return success when no corpus exists.")
    args = parser.parse_args()

    roots = [Path(path) for path in args.corpus] if args.corpus else default_corpus_roots()
    dxbc_files, skipped_sidecars = discover_dxbc_files(roots)
    if args.limit > 0:
        dxbc_files = dxbc_files[: args.limit]

    results_dir = Path(args.results_dir)
    results_dir.mkdir(parents=True, exist_ok=True)
    out_path = results_dir / f"shader-corpus-replay-{args.profile}.json"

    if not dxbc_files:
        result = {
            "schema": "metalsharp.d3d12-metal.shader-corpus-replay.v1",
            "profile": args.profile,
            "ok": bool(args.allow_empty),
            "empty": True,
            "roots": [str(root) for root in roots],
            "message": "No .dxbc shader corpus found. Capture shaders before treating this as render-ready.",
            "shaders": [],
            "skipped_generated_sidecars": [str(path) for path in skipped_sidecars],
        }
        out_path.write_text(json.dumps(result, indent=2) + "\n")
        print(out_path)
        return 0 if args.allow_empty else 1

    shaders = [run_converter(args.tool, dxbc, args.force) for dxbc in dxbc_files]
    failures = [shader for shader in shaders if shader["status"] == "failed"]
    result = {
        "schema": "metalsharp.d3d12-metal.shader-corpus-replay.v1",
        "profile": args.profile,
        "ok": not failures,
        "empty": False,
        "roots": [str(root) for root in roots],
        "shader_count": len(shaders),
        "skipped_generated_sidecar_count": len(skipped_sidecars),
        "failure_count": len(failures),
        "gs_ts_emulation_count": sum(1 for shader in shaders if shader.get("uses_gs_ts_emulation")),
        "skipped_generated_sidecars": [str(path) for path in skipped_sidecars],
        "shaders": shaders,
    }
    out_path.write_text(json.dumps(result, indent=2) + "\n")
    print(out_path)
    if failures:
        for failure in failures[:20]:
            print(f"shader replay failed: {failure['dxbc']}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
