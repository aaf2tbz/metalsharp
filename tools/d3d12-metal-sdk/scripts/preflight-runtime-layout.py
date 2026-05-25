#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path


LEGACY_EXPORTS = [
    "MTLLibrary_newFunctionWithConstants",
    "WMTSetMetalShaderCachePath",
]

DXMT_EXPORTS = LEGACY_EXPORTS + [
    "MTLLibrary_newFunctionWithDescriptor",
]


def sha256(path: Path) -> str | None:
    if not path.exists():
        return None
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def exports_for(path: Path) -> set[str]:
    if not path.exists() or path.suffix.lower() != ".dll":
        return set()
    try:
        output = subprocess.check_output(
            ["x86_64-w64-mingw32-objdump", "-p", str(path)],
            text=True,
            stderr=subprocess.DEVNULL,
        )
    except (FileNotFoundError, subprocess.CalledProcessError):
        return set()
    exports: set[str] = set()
    for line in output.splitlines():
        # objdump export table lines end with the symbol name.
        parts = line.strip().split()
        if parts:
            exports.add(parts[-1])
    return exports


def inspect_file(path: Path, required_exports: list[str]) -> dict:
    file_exports = exports_for(path)
    missing = [name for name in required_exports if name not in file_exports]
    return {
        "path": str(path),
        "exists": path.exists(),
        "size": path.stat().st_size if path.exists() else 0,
        "sha256": sha256(path),
        "required_exports": required_exports,
        "missing_exports": missing,
        "ok": path.exists() and not missing,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate D3D12 Metal SDK runtime layout before game launch."
    )
    parser.add_argument("--profile", default="metalsharp")
    parser.add_argument(
        "--dxmt-runtime",
        default=os.path.expanduser("~/.metalsharp/runtime/wine/lib/dxmt"),
        help="Runtime root containing x86_64-windows and x86_64-unix.",
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
    parser.add_argument(
        "--game-dir",
        default="",
        help="Optional game directory containing game-local staged DLLs.",
    )
    parser.add_argument(
        "--results-dir",
        default=str(Path(__file__).resolve().parents[1] / "results"),
    )
    args = parser.parse_args()

    dxmt_runtime = Path(args.dxmt_runtime)
    wine_runtime = Path(args.wine_runtime)
    prefix = Path(args.prefix)
    results_dir = Path(args.results_dir)
    results_dir.mkdir(parents=True, exist_ok=True)

    entries: list[dict] = []
    entries.append(
        inspect_file(dxmt_runtime / "x86_64-windows" / "winemetal.dll", DXMT_EXPORTS)
        | {"role": "dxmt_windows"}
    )
    entries.append(
        inspect_file(wine_runtime / "lib" / "wine" / "x86_64-windows" / "winemetal.dll", LEGACY_EXPORTS)
        | {"role": "wine_builtin_windows"}
    )
    entries.append(
        inspect_file(prefix / "drive_c" / "windows" / "system32" / "winemetal.dll", LEGACY_EXPORTS)
        | {"role": "prefix_system32"}
    )
    entries.append(
        inspect_file(prefix / "drive_c" / "windows" / "syswow64" / "winemetal.dll", LEGACY_EXPORTS)
        | {"role": "prefix_syswow64"}
    )

    unix_entries = [
        dxmt_runtime / "x86_64-unix" / "winemetal.so",
        wine_runtime / "lib" / "wine" / "x86_64-unix" / "winemetal.so",
    ]
    for path in unix_entries:
        entries.append(
            {
                "role": "unix_winemetal",
                "path": str(path),
                "exists": path.exists(),
                "size": path.stat().st_size if path.exists() else 0,
                "sha256": sha256(path),
                "ok": path.exists() and path.stat().st_size > 0 if path.exists() else False,
            }
        )

    if args.game_dir:
        entries.append(
            inspect_file(Path(args.game_dir) / "winemetal.dll", DXMT_EXPORTS)
            | {"role": "game_local_winemetal"}
        )

    failures = [entry for entry in entries if not entry.get("ok")]
    result = {
        "schema": "metalsharp.d3d12-metal.runtime-preflight.v1",
        "profile": args.profile,
        "ok": not failures,
        "failure_count": len(failures),
        "entries": entries,
        "notes": [
            "Steam/global Wine Winemetal copies must preserve legacy wrapper exports.",
            "DXMT/game-local Winemetal copies must preserve legacy exports and expose new shader bridge exports.",
            "Do not launch Steam or a game while this preflight is red.",
        ],
    }

    out_path = results_dir / f"runtime-preflight-{args.profile}.json"
    out_path.write_text(json.dumps(result, indent=2) + "\n")
    print(out_path)
    if failures:
        for failure in failures:
            print(
                f"preflight failed: {failure.get('role')} {failure.get('path')} "
                f"missing={failure.get('missing_exports', [])}",
                file=sys.stderr,
            )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
