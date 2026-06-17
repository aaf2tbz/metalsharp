#!/usr/bin/env python3
"""Verify frozen M12 runtime and optional game-local DLL hashes."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

DLLS = [
    "d3d12.dll",
    "d3d11.dll",
    "dxgi.dll",
    "dxgi_dxmt.dll",
    "d3d10core.dll",
    "winemetal.dll",
    "nvapi64.dll",
    "nvngx.dll",
]

KNOWN_WORKING = {
    "d3d12.dll": "8cdcec40588018dafaa3cdd1cfb140c1fd7edba6f1160cd7559d61be8b946500",
    "d3d11.dll": "15785d6c75d4ae5f0e3ab9b61f56b534d933ab6b691ffbc02eae30cee25b5bbc",
    "dxgi.dll": "dc800838673b2e2236f775889a7c464ba72403a92a926b8073d742b28563ef24",
    "dxgi_dxmt.dll": "f276cbaaf2308e8faff6a48cf739f885d2609a18a44ac9637867c9aae23bee1a",
    "d3d10core.dll": "630c5d990da0d95a95f0a6458b7e9156073cf8fa335708b92a25babde80bfbe8",
    "winemetal.dll": "7f8cc745406440b3b262588d4fb397c0f028593916b613c638226d460327fa85",
    "nvapi64.dll": "0ad95777863b97a5757adfecc45e4ee55195a9befd05878cbc975a4f6c8ac841",
    "nvngx.dll": "3c47b6f378fa0f75cd3002791a59e63c899a0421d013f1cadc89012a6fe6fd82",
}

GAME_DIRS = {
    "elden-ring": Path("/Volumes/AverySSD/SteamLibrary/steamapps/common/ELDEN RING/Game"),
    "subnautica-2": Path("/Volumes/AverySSD/SteamLibrary/steamapps/common/Subnautica2"),
    "schedule-1": Path("/Volumes/AverySSD/SteamLibrary/steamapps/common/Schedule I"),
    "peak": Path("/Volumes/AverySSD/SteamLibrary/steamapps/common/PEAK"),
}


def sha256(path: Path) -> str | None:
    if not path.exists():
        return None
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def inspect_dir(label: str, directory: Path, expected: dict[str, str] | None, required: bool) -> dict:
    files = []
    ok = True
    for dll in DLLS:
        path = directory / dll
        actual = sha256(path)
        exp = expected.get(dll) if expected else None
        exists = actual is not None
        matches = actual == exp if exp else None
        file_ok = exists and (matches is not False)
        if required and not file_ok:
            ok = False
        files.append({
            "dll": dll,
            "path": str(path),
            "exists": exists,
            "sha256": actual,
            "expected": exp,
            "matches_expected": matches,
            "ok": file_ok,
        })
    return {"label": label, "directory": str(directory), "required": required, "ok": ok, "files": files}


def parse_expected_override(value: str) -> tuple[str, str]:
    if "=" not in value:
        raise argparse.ArgumentTypeError("expected override must be DLL=SHA256")
    dll, digest = value.split("=", 1)
    dll = dll.strip()
    digest = digest.strip().lower()
    if dll not in DLLS:
        raise argparse.ArgumentTypeError(f"unsupported DLL in expected override: {dll}")
    if len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
        raise argparse.ArgumentTypeError(f"invalid SHA-256 digest for {dll}: {digest}")
    return dll, digest


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--runtime-dir", type=Path, default=Path.home() / ".metalsharp/runtime/wine/lib/dxmt_m12/x86_64-windows")
    ap.add_argument("--game", choices=sorted(GAME_DIRS), action="append", default=[])
    ap.add_argument("--game-dir", type=Path, action="append", default=[])
    ap.add_argument("--label", default="m12-runtime-hash-check")
    ap.add_argument("--json", type=Path, default=None)
    ap.add_argument("--markdown", type=Path, default=None)
    ap.add_argument("--strict", action="store_true", help="Fail if any checked dir differs from expected hashes.")
    ap.add_argument(
        "--expected",
        type=parse_expected_override,
        action="append",
        default=[],
        metavar="DLL=SHA256",
        help="Override an expected hash for this invocation. Repeat for multiple DLLs.",
    )
    args = ap.parse_args()

    expected_hashes = dict(KNOWN_WORKING)
    for dll, digest in args.expected:
        expected_hashes[dll] = digest

    checks = [inspect_dir("runtime", args.runtime_dir, expected_hashes, True)]
    for game in args.game:
        checks.append(inspect_dir(game, GAME_DIRS[game], expected_hashes, args.strict))
    for idx, game_dir in enumerate(args.game_dir):
        checks.append(inspect_dir(f"game-dir-{idx+1}", game_dir, expected_hashes, args.strict))

    report = {
        "schema": "metalsharp.m12.runtime-hashes.v1",
        "label": args.label,
        "strict": args.strict,
        "known_working_d3d12": KNOWN_WORKING["d3d12.dll"],
        "expected_hashes": expected_hashes,
        "checks": checks,
        "ok": all(c["ok"] for c in checks),
    }

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2) + "\n")
    if args.markdown:
        args.markdown.parent.mkdir(parents=True, exist_ok=True)
        lines = [f"# M12 runtime hash check: {args.label}", "", f"- ok: `{report['ok']}`", f"- strict: `{args.strict}`", ""]
        for check in checks:
            lines += [f"## {check['label']}", "", f"- directory: `{check['directory']}`", f"- ok: `{check['ok']}`", "", "| dll | exists | matches | sha256 |", "|---|---:|---:|---|"]
            for f in check["files"]:
                lines.append(f"| `{f['dll']}` | `{f['exists']}` | `{f['matches_expected']}` | `{f['sha256']}` |")
            lines.append("")
        args.markdown.write_text("\n".join(lines))
    print(json.dumps(report, indent=2))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
