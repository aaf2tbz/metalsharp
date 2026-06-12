#!/usr/bin/env python3
"""Stage rebuilt DXMT D3D12 artifacts into a MetalSharp runtime."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[3]
DEFAULT_BUILD_DIR = ROOT_DIR / "vendor" / "dxmt" / "build-metalsharp-x64"
DEFAULT_RUNTIME_DIR = Path(os.path.expanduser("~/.metalsharp/runtime/wine/lib/dxmt"))
DEFAULT_PREFIX_DIR = Path(os.path.expanduser("~/.metalsharp/prefix-steam"))
DEFAULT_RESULTS_DIR = ROOT_DIR / "tools" / "d3d12-metal-sdk" / "results"

ARTIFACTS = [
    ("src/d3d12/d3d12.dll", "x86_64-windows/d3d12.dll"),
    ("src/dxgi/dxgi.dll", "x86_64-windows/dxgi.dll"),
    ("src/dxgi/dxgi_dxmt.dll", "x86_64-windows/dxgi_dxmt.dll"),
    ("src/winemetal/winemetal.dll", "x86_64-windows/winemetal.dll"),
    ("src/winemetal/unix/winemetal.so", "x86_64-unix/winemetal.so"),
]

SHARED_WINEMETAL_ARTIFACTS = [
    ("src/winemetal/unix/winemetal.so", "wine/x86_64-unix/winemetal.so"),
]

UNIX_DYLIB_DEPS = [
    "libc++.1.dylib",
    "libc++abi.1.dylib",
    "libunwind.1.dylib",
]


def sha256(path: Path) -> str | None:
    if not path.exists():
        return None
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def file_record(path: Path) -> dict:
    return {
        "path": str(path),
        "exists": path.exists(),
        "size": path.stat().st_size if path.exists() else 0,
        "sha256": sha256(path),
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Stage rebuilt DXMT artifacts into ~/.metalsharp runtime.")
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR)
    parser.add_argument("--runtime-dir", type=Path, default=DEFAULT_RUNTIME_DIR)
    parser.add_argument("--prefix", type=Path, default=DEFAULT_PREFIX_DIR)
    parser.add_argument("--results-dir", type=Path, default=DEFAULT_RESULTS_DIR)
    parser.add_argument("--profile", default="metalsharp")
    parser.add_argument("--dry-run", action="store_true", help="Write the manifest without copying files.")
    parser.add_argument(
        "--include-winemetal",
        action="store_true",
        help="Compatibility no-op: WineMetal is staged by default for M12.",
    )
    parser.add_argument(
        "--no-shared-winemetal",
        action="store_true",
        help="Do not mirror DXMT winemetal.so into lib/wine/x86_64-unix.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    build_dir = args.build_dir
    runtime_dir = args.runtime_dir
    wine_lib_dir = runtime_dir.parent / "wine"
    args.results_dir.mkdir(parents=True, exist_ok=True)

    records: list[dict] = []
    failures: list[str] = []

    artifacts = list(ARTIFACTS)
    if not args.no_shared_winemetal:
        artifacts.extend(SHARED_WINEMETAL_ARTIFACTS)

    for src_rel, dst_rel in artifacts:
        src = build_dir / src_rel
        dst = Path(dst_rel)
        if not dst.is_absolute():
            if dst.parts and dst.parts[0] == "wine":
                dst = runtime_dir.parent / dst
            else:
                dst = runtime_dir / dst
        before = file_record(dst)
        source = file_record(src)

        copied = False
        if not src.exists():
            failures.append(f"missing build artifact: {src}")
        elif not args.dry_run:
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
            copied = True

        after = file_record(dst)
        records.append(
            {
                "source": source,
                "destination_before": before,
                "destination_after": after,
                "copied": copied,
                "match": source["sha256"] is not None and source["sha256"] == after["sha256"],
            }
        )

    unix_src = build_dir / "src/winemetal/unix/winemetal.so"
    try:
        linked = subprocess.check_output(["otool", "-L", str(unix_src)], text=True, stderr=subprocess.DEVNULL)
    except (FileNotFoundError, subprocess.CalledProcessError):
        linked = ""
    linked_deps = [dep for dep in UNIX_DYLIB_DEPS if dep in linked]
    deps_to_stage = UNIX_DYLIB_DEPS if linked_deps else []
    for dep in deps_to_stage:
        dep_candidates = [
            build_dir / "src/winemetal/unix" / dep,
            build_dir / "src/winemetal" / dep,
            build_dir / dep,
        ]
        if os.environ.get("DYLD_LIBRARY_PATH"):
            dep_candidates.extend(Path(part) / dep for part in os.environ["DYLD_LIBRARY_PATH"].split(os.pathsep) if part)
        for root_var in ("METALSHARP_X86_LLVM_ROOT",):
            root = os.environ.get(root_var)
            if root:
                dep_candidates.extend(Path(root).glob(f"*/lib/{dep}"))
        dep_candidates.extend(Path(os.path.expanduser("~/.metalsharp/toolchains")).glob(f"*/lib/{dep}"))
        dep_candidates.extend(Path("/Volumes/AverySSD/toolchains").glob(f"*/lib/{dep}"))
        dep_src = next((candidate for candidate in dep_candidates if candidate.exists()), None)
        if dep_src is None:
            failures.append(f"missing Unix dependency for winemetal.so: {dep}")
            continue
        for dst_dir in [runtime_dir / "x86_64-unix", wine_lib_dir / "x86_64-unix"]:
            dst = dst_dir / dep
            before = file_record(dst)
            copied = False
            if not args.dry_run:
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(dep_src, dst)
                copied = True
            after = file_record(dst)
            source = file_record(dep_src)
            records.append(
                {
                    "source": source,
                    "destination_before": before,
                    "destination_after": after,
                    "copied": copied,
                    "match": source["sha256"] is not None and source["sha256"] == after["sha256"],
                }
            )

    mismatches = [
        record
        for record in records
        if record["source"]["exists"] and not args.dry_run and not record["match"]
    ]
    result = {
        "schema": "metalsharp.d3d12-metal.stage-runtime.v1",
        "profile": args.profile,
        "build_dir": str(build_dir),
        "runtime_dir": str(runtime_dir),
        "dry_run": args.dry_run,
        "include_winemetal": args.include_winemetal,
        "shared_winemetal": not args.no_shared_winemetal,
        "ok": not failures and not mismatches,
        "failure_count": len(failures) + len(mismatches),
        "failures": failures,
        "artifacts": records,
    }

    out_path = args.results_dir / f"stage-runtime-{args.profile}.json"
    out_path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(out_path)

    if not result["ok"]:
        for failure in failures:
            print(f"stage failed: {failure}", file=sys.stderr)
        for mismatch in mismatches:
            print(
                "stage failed: hash mismatch "
                f"{mismatch['source']['path']} -> {mismatch['destination_after']['path']}",
                file=sys.stderr,
            )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
