#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path


LEGACY_EXPORTS = [
    "WMTSetMetalShaderCachePath",
    "WMTBootstrapRegister",
    "WMTBootstrapLookUp",
]

DXMT_EXPORTS = LEGACY_EXPORTS + [
    "MTLLibrary_newFunctionWithConstants",
    "MTLLibrary_newFunctionWithDescriptor",
]

D3D12_EXPORTS = [
    "D3D12CreateDevice",
    "D3D12CreateRootSignatureDeserializer",
    "D3D12CreateVersionedRootSignatureDeserializer",
    "D3D12SerializeRootSignature",
    "D3D12SerializeVersionedRootSignature",
]

DXGI_EXPORTS = [
    "CreateDXGIFactory",
    "CreateDXGIFactory1",
    "CreateDXGIFactory2",
]


def sha256(path: Path) -> str | None:
    if not path.exists():
        return None
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def export_table_for(path: Path) -> tuple[set[str], set[int]]:
    if not path.exists() or path.suffix.lower() != ".dll":
        return set(), set()
    try:
        output = subprocess.check_output(
            ["x86_64-w64-mingw32-objdump", "-p", str(path)],
            text=True,
            stderr=subprocess.DEVNULL,
        )
    except (FileNotFoundError, subprocess.CalledProcessError):
        return set(), set()
    exports: set[str] = set()
    ordinals: set[int] = set()
    for line in output.splitlines():
        ordinal_match = re.search(r"\+base\[\s*(\d+)\]", line)
        if ordinal_match:
            ordinals.add(int(ordinal_match.group(1)))
        name_match = re.search(r"\+base\[\s*\d+\]\s+[0-9a-fA-F]+\s+([A-Za-z_][A-Za-z0-9_@?$]*)$", line)
        if name_match:
            exports.add(name_match.group(1))
    return exports, ordinals


def inspect_file(path: Path, required_exports: list[str], required_ordinals: list[int] | None = None) -> dict:
    required_ordinals = required_ordinals or []
    file_exports, file_ordinals = export_table_for(path)
    missing = [name for name in required_exports if name not in file_exports]
    missing_ordinals = [ordinal for ordinal in required_ordinals if ordinal not in file_ordinals]
    return {
        "path": str(path),
        "exists": path.exists(),
        "size": path.stat().st_size if path.exists() else 0,
        "sha256": sha256(path),
        "required_exports": required_exports,
        "required_ordinals": required_ordinals,
        "missing_exports": missing,
        "missing_ordinals": missing_ordinals,
        "ok": path.exists() and not missing and not missing_ordinals,
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

    abi_command = [
        sys.executable,
        str(Path(__file__).with_name("check-winemetal-abi.py")),
        "--profile",
        args.profile,
        "--results-dir",
        str(results_dir),
        "--dxmt-runtime",
        str(dxmt_runtime),
        "--wine-runtime",
        str(wine_runtime),
        "--prefix",
        str(prefix),
    ]
    if args.game_dir:
        abi_command.extend(["--game-dir", args.game_dir])
    abi_status = subprocess.run(abi_command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    abi_result_path = results_dir / f"winemetal-abi-{args.profile}.json"

    entries: list[dict] = []
    entries.append(
        inspect_file(dxmt_runtime / "x86_64-windows" / "d3d12.dll", D3D12_EXPORTS, [101])
        | {"role": "dxmt_windows_d3d12"}
    )
    entries.append(
        inspect_file(dxmt_runtime / "x86_64-windows" / "dxgi.dll", DXGI_EXPORTS, [9, 10, 11])
        | {"role": "dxmt_windows_dxgi_bootstrap"}
    )
    entries.append(
        inspect_file(dxmt_runtime / "x86_64-windows" / "dxgi_dxmt.dll", DXGI_EXPORTS, [9, 10, 11])
        | {"role": "dxmt_windows_dxgi_real"}
    )
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
        "ok": not failures and abi_status.returncode == 0,
        "failure_count": len(failures),
        "winemetal_abi": {
            "ok": abi_status.returncode == 0,
            "result": str(abi_result_path),
            "stdout": abi_status.stdout.strip(),
            "stderr": abi_status.stderr.strip(),
        },
        "entries": entries,
        "notes": [
            "DXMT d3d12.dll must expose D3D12CreateDevice by name and ordinal 101.",
            "DXGI bootstrap and real DXMT DLLs must expose factory exports by name and ordinals 9, 10, and 11.",
            "Steam/global Wine Winemetal copies must preserve legacy wrapper exports.",
            "DXMT/game-local Winemetal copies must preserve legacy exports and expose new shader bridge exports.",
            "Do not launch Steam or a game while this preflight is red.",
        ],
    }

    out_path = results_dir / f"runtime-preflight-{args.profile}.json"
    out_path.write_text(json.dumps(result, indent=2) + "\n")
    print(out_path)
    if failures or abi_status.returncode != 0:
        if abi_status.stderr:
            print(abi_status.stderr, file=sys.stderr, end="")
        for failure in failures:
            print(
                f"preflight failed: {failure.get('role')} {failure.get('path')} "
                f"missing={failure.get('missing_exports', [])} "
                f"missing_ordinals={failure.get('missing_ordinals', [])}",
                file=sys.stderr,
            )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
