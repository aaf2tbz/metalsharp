#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path


STEAM_WRAPPER_EXPORTS = [
    "WMTSetMetalShaderCachePath",
    "WMTBootstrapRegister",
    "WMTBootstrapLookUp",
    "MTLDevice_newSharedTexture",
    "MTLSharedEvent_createMachPort",
    "MTLDevice_newSharedEventWithMachPort",
    "CreateMetalViewFromHWND",
    "ReleaseMetalView",
]

DXMT_EXPORTS = STEAM_WRAPPER_EXPORTS + [
    "MTLLibrary_newFunctionWithConstants",
    "MTLLibrary_newFunctionWithDescriptor",
    "MTLDevice_newRenderPipelineState",
    "MTLDevice_newComputePipelineState",
    "MTLDevice_newMeshRenderPipelineState",
    "MTLDevice_newTileRenderPipelineState",
    "MTLDevice_newLibrary",
    "MTLDevice_newLibraryWithSource",
]

UNIXLIB_EXPORTS = [
    "__wine_unix_call_funcs",
    "__wine_unix_call_wow64_funcs",
]


def sha256(path: Path) -> str | None:
    if not path.exists():
        return None
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(command: list[str]) -> str:
    return subprocess.check_output(command, text=True, stderr=subprocess.DEVNULL)


def pe_exports(path: Path) -> set[str]:
    if not path.exists():
        return set()
    try:
        output = run(["x86_64-w64-mingw32-objdump", "-p", str(path)])
    except (FileNotFoundError, subprocess.CalledProcessError):
        try:
            output = run(["llvm-objdump", "-p", str(path)])
        except (FileNotFoundError, subprocess.CalledProcessError):
            return set()

    exports: set[str] = set()
    for line in output.splitlines():
        parts = line.strip().split()
        if len(parts) >= 4 and re.match(r"^\[\s*\d+\]$", " ".join(parts[:2])):
            exports.add(parts[-1])
        elif parts and parts[-1].startswith(("WMT", "MTL", "Metal", "CreateMetal", "ReleaseMetal", "NSString", "NS")):
            exports.add(parts[-1])
    return exports


def unix_exports(path: Path) -> set[str]:
    if not path.exists():
        return set()
    candidates = [
        ["nm", "-g", str(path)],
        ["llvm-nm", "-g", str(path)],
    ]
    output = ""
    for command in candidates:
        try:
            output = run(command)
            break
        except (FileNotFoundError, subprocess.CalledProcessError):
            continue

    exports: set[str] = set()
    for line in output.splitlines():
        parts = line.strip().split()
        if parts:
            name = parts[-1]
            exports.add(name)
            if name.startswith("__"):
                exports.add(name[1:])
                exports.add(name[2:])
            elif name.startswith("_"):
                exports.add(name[1:])
    return exports


def inspect(path: Path, role: str, required: list[str], binary_type: str) -> dict:
    exports = unix_exports(path) if binary_type == "unix" else pe_exports(path)
    missing = [name for name in required if name not in exports]
    exists = path.exists()
    return {
        "role": role,
        "path": str(path),
        "binary_type": binary_type,
        "exists": exists,
        "size": path.stat().st_size if exists else 0,
        "sha256": sha256(path),
        "required_exports": required,
        "missing_exports": missing,
        "export_count": len(exports),
        "ok": exists and not missing,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate Winemetal ABI/export compatibility before Steam or game launch."
    )
    parser.add_argument("--profile", default="metalsharp")
    parser.add_argument(
        "--dxmt-runtime",
        default=os.path.expanduser("~/.metalsharp/runtime/wine/lib/dxmt"),
        help="DXMT runtime root containing x86_64-windows and x86_64-unix.",
    )
    parser.add_argument(
        "--wine-runtime",
        default=os.path.expanduser("~/.metalsharp/runtime/wine"),
        help="Wine runtime root containing lib/wine.",
    )
    parser.add_argument(
        "--prefix",
        default=os.path.expanduser("~/.metalsharp/prefix-steam"),
        help="Wine prefix used for Steam/game launches.",
    )
    parser.add_argument("--game-dir", default="", help="Optional game Win64 directory containing staged DLLs.")
    parser.add_argument("--results-dir", default=str(Path(__file__).resolve().parents[1] / "results"))
    parser.add_argument(
        "--optional-prefix",
        action="store_true",
        help="Do not fail if prefix system32/syswow64 winemetal.dll copies do not exist.",
    )
    args = parser.parse_args()

    dxmt_runtime = Path(args.dxmt_runtime)
    wine_runtime = Path(args.wine_runtime)
    prefix = Path(args.prefix)
    results_dir = Path(args.results_dir)
    results_dir.mkdir(parents=True, exist_ok=True)

    entries: list[dict] = [
        inspect(dxmt_runtime / "x86_64-windows" / "winemetal.dll", "dxmt_windows", DXMT_EXPORTS, "pe"),
        inspect(dxmt_runtime / "x86_64-unix" / "winemetal.so", "dxmt_unixlib", UNIXLIB_EXPORTS, "unix"),
        inspect(
            wine_runtime / "lib" / "wine" / "x86_64-windows" / "winemetal.dll",
            "wine_builtin_windows",
            DXMT_EXPORTS,
            "pe",
        ),
        inspect(
            wine_runtime / "lib" / "wine" / "x86_64-unix" / "winemetal.so",
            "wine_builtin_unixlib",
            UNIXLIB_EXPORTS,
            "unix",
        ),
        inspect(
            prefix / "drive_c" / "windows" / "system32" / "winemetal.dll",
            "prefix_system32",
            DXMT_EXPORTS,
            "pe",
        ),
        inspect(
            prefix / "drive_c" / "windows" / "syswow64" / "winemetal.dll",
            "prefix_syswow64",
            STEAM_WRAPPER_EXPORTS,
            "pe",
        ),
    ]

    if args.optional_prefix:
        for entry in entries:
            if entry["role"] in {"prefix_system32", "prefix_syswow64"} and not entry["exists"]:
                entry["ok"] = True
                entry["optional_absent"] = True

    if args.game_dir:
        entries.append(inspect(Path(args.game_dir) / "winemetal.dll", "game_local_winemetal", DXMT_EXPORTS, "pe"))

    failures = [entry for entry in entries if not entry["ok"]]
    result = {
        "schema": "metalsharp.d3d12-metal.winemetal-abi.v1",
        "profile": args.profile,
        "ok": not failures,
        "failure_count": len(failures),
        "required_groups": {
            "steam_wrapper_exports": STEAM_WRAPPER_EXPORTS,
            "dxmt_exports": DXMT_EXPORTS,
            "unixlib_exports": UNIXLIB_EXPORTS,
        },
        "entries": entries,
        "notes": [
            "Steam/global Wine x86_64 copies must preserve Steam wrapper exports plus D3D12 shader/PSO bridge exports.",
            "The 32-bit syswow64 copy is still checked for legacy wrapper exports only.",
            "DXMT/game-local copies must preserve Steam wrapper exports plus D3D12 shader/PSO bridge exports.",
            "A red result here means do not launch Steam or a game; repair DLL staging first.",
        ],
    }
    out_path = results_dir / f"winemetal-abi-{args.profile}.json"
    out_path.write_text(json.dumps(result, indent=2) + "\n")
    print(out_path)

    if failures:
        for failure in failures:
            print(
                f"winemetal ABI failed: {failure['role']} {failure['path']} "
                f"missing={failure['missing_exports']}",
                file=sys.stderr,
            )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
