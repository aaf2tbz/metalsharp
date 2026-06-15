#!/usr/bin/env python3
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
SDK_ROOT = SCRIPT_DIR.parent
PROJECT_ROOT = SDK_ROOT.parent.parent
DEFAULT_CORPUS_ROOT = SDK_ROOT / "shader-corpus"
DEFAULT_OUT_DIR = PROJECT_ROOT / "dist" / "d3d12-metal-shaders"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def resolve_xcrun_tool(name: str) -> str:
    completed = run(["xcrun", "-find", name])
    if completed.returncode != 0:
        raise RuntimeError(f"unable to resolve {name} with xcrun: {completed.stderr.strip()}")
    tool = completed.stdout.strip()
    if not tool:
        raise RuntimeError(f"xcrun returned an empty path for {name}")
    return tool


def discover_msl(corpus_root: Path) -> dict[str, list[Path]]:
    corpora: dict[str, list[Path]] = {}
    if not corpus_root.is_dir():
        return corpora

    for corpus in sorted(path for path in corpus_root.iterdir() if path.is_dir()):
        msl_dir = corpus / "msl"
        shaders = sorted(msl_dir.glob("*.msl")) if msl_dir.is_dir() else []
        if shaders:
            corpora[corpus.name] = shaders
    return corpora


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def compile_shader(msl: Path, corpus_name: str, out_root: Path, metal_tool: str, metallib_tool: str) -> dict:
    stem = msl.stem
    air = out_root / "shader-corpus" / corpus_name / "air" / f"{stem}.air"
    metallib = out_root / "shader-corpus" / corpus_name / "metallib" / f"{stem}.metallib"
    log = out_root / "shader-corpus" / corpus_name / "logs" / f"{stem}.log"
    air.parent.mkdir(parents=True, exist_ok=True)
    metallib.parent.mkdir(parents=True, exist_ok=True)
    log.parent.mkdir(parents=True, exist_ok=True)

    metal_cmd = [metal_tool, "-x", "metal", "-c", str(msl), "-o", str(air)]
    metal = run(metal_cmd)
    metallib_cmd = [metallib_tool, str(air), "-o", str(metallib)]
    metallib_result: subprocess.CompletedProcess[str] | None = None

    ok = metal.returncode == 0 and air.is_file() and air.stat().st_size > 0
    if ok:
        metallib_result = run(metallib_cmd)
        ok = metallib_result.returncode == 0 and metallib.is_file() and metallib.stat().st_size > 0

    log_lines = [
        f"msl={msl}",
        f"air={air}",
        f"metallib={metallib}",
        f"metal_command={' '.join(metal_cmd)}",
        f"metal_returncode={metal.returncode}",
        "metal_stdout:",
        metal.stdout,
        "metal_stderr:",
        metal.stderr,
    ]
    if metallib_result is not None:
        log_lines.extend(
            [
                f"metallib_command={' '.join(metallib_cmd)}",
                f"metallib_returncode={metallib_result.returncode}",
                "metallib_stdout:",
                metallib_result.stdout,
                "metallib_stderr:",
                metallib_result.stderr,
            ]
        )
    write_text(log, "\n".join(log_lines).rstrip() + "\n")

    return {
        "corpus": corpus_name,
        "source": str(msl),
        "air": str(air),
        "metallib": str(metallib),
        "log": str(log),
        "status": "ok" if ok else "failed",
        "metal_returncode": metal.returncode,
        "metallib_returncode": metallib_result.returncode if metallib_result is not None else None,
        "air_sha256": sha256(air) if air.is_file() else None,
        "metallib_sha256": sha256(metallib) if metallib.is_file() else None,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Build committed D3D12 MSL shader corpora into AIR and metallib outputs.")
    parser.add_argument("--corpus-root", type=Path, default=DEFAULT_CORPUS_ROOT)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    parser.add_argument("--clean", action="store_true", help="Remove the output directory before building.")
    parser.add_argument("--allow-empty", action="store_true", help="Succeed when no committed MSL corpus exists.")
    parser.add_argument("--limit", type=int, default=0, help="Optional max shaders to compile, for local smoke tests only.")
    args = parser.parse_args()

    if shutil.which("xcrun") is None:
        print("xcrun is required to compile Metal shader corpora", file=sys.stderr)
        return 1
    try:
        metal_tool = resolve_xcrun_tool("metal")
        metallib_tool = resolve_xcrun_tool("metallib")
    except RuntimeError as error:
        print(str(error), file=sys.stderr)
        return 1

    corpora = discover_msl(args.corpus_root)
    if args.limit > 0:
        remaining = args.limit
        limited: dict[str, list[Path]] = {}
        for corpus, shaders in corpora.items():
            if remaining <= 0:
                break
            limited[corpus] = shaders[:remaining]
            remaining -= len(limited[corpus])
        corpora = limited

    shader_count = sum(len(shaders) for shaders in corpora.values())
    if shader_count == 0 and not args.allow_empty:
        print(f"No committed MSL shaders found under {args.corpus_root}", file=sys.stderr)
        return 1

    if args.clean and args.out_dir.exists():
        shutil.rmtree(args.out_dir)
    args.out_dir.mkdir(parents=True, exist_ok=True)

    results: list[dict] = []
    for corpus, shaders in corpora.items():
        for shader in shaders:
            result = compile_shader(shader, corpus, args.out_dir, metal_tool, metallib_tool)
            results.append(result)
            if result["status"] != "ok":
                print(f"shader compile failed: {shader}", file=sys.stderr)

    failures = [result for result in results if result["status"] != "ok"]
    manifest = {
        "schema": "metalsharp.d3d12-metal.shader-corpus-build.v1",
        "corpus_root": str(args.corpus_root),
        "output_root": str(args.out_dir),
        "corpus_count": len(corpora),
        "shader_count": shader_count,
        "air_count": sum(1 for result in results if result["air_sha256"]),
        "metallib_count": sum(1 for result in results if result["metallib_sha256"]),
        "failure_count": len(failures),
        "corpora": [
            {
                "name": corpus,
                "shader_count": len(shaders),
                "air_dir": str(args.out_dir / "shader-corpus" / corpus / "air"),
                "metallib_dir": str(args.out_dir / "shader-corpus" / corpus / "metallib"),
            }
            for corpus, shaders in corpora.items()
        ],
        "shaders": results,
    }
    write_text(args.out_dir / "manifest.json", json.dumps(manifest, indent=2) + "\n")

    tsv_lines = ["corpus\tsource\tair\tair_sha256\tmetallib\tmetallib_sha256\tstatus"]
    for result in results:
        tsv_lines.append(
            "\t".join(
                [
                    result["corpus"],
                    result["source"],
                    result["air"],
                    result["air_sha256"] or "",
                    result["metallib"],
                    result["metallib_sha256"] or "",
                    result["status"],
                ]
            )
        )
    write_text(args.out_dir / "compiled-shaders.tsv", "\n".join(tsv_lines) + "\n")

    summary = (
        f"corpus_count={len(corpora)}\n"
        f"shader_count={shader_count}\n"
        f"air_count={manifest['air_count']}\n"
        f"metallib_count={manifest['metallib_count']}\n"
        f"failure_count={len(failures)}\n"
    )
    write_text(args.out_dir / "summary.txt", summary)
    print(summary, end="")
    print(args.out_dir / "manifest.json")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
