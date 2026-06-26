#!/usr/bin/env python3
"""Fresh M12 proof harness runner.

Phase 0 intentionally produces new proof roots and fresh runtime identity logs.
It does not consume previous proof directories, old shader caches, or older probe
outputs as evidence.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any


DEFAULT_LAB_ROOT = Path("/Volumes/AverySSD/MetalSharp-SM6-UE-Lab")
REQUIRED_WINDOWS = ["d3d12.dll", "dxgi.dll", "dxgi_dxmt.dll", "winemetal.dll"]
REQUIRED_UNIX = ["winemetal.so"]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_text(repo: Path, *args: str) -> str:
    try:
        return subprocess.check_output(["git", *args], cwd=repo, text=True).strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return ""


def disk_free_gib(path: Path) -> int:
    usage = shutil.disk_usage(path)
    return int(usage.free // (1024**3))


def timestamp() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y%m%d-%H%M%S")


def write_json(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")


def inspect_runtime(runtime_root: Path) -> tuple[list[dict[str, Any]], list[str]]:
    entries: list[dict[str, Any]] = []
    missing: list[str] = []
    for subdir, names in (("x86_64-windows", REQUIRED_WINDOWS), ("x86_64-unix", REQUIRED_UNIX)):
        for name in names:
            path = runtime_root / subdir / name
            exists = path.exists()
            entry: dict[str, Any] = {
                "name": name,
                "role": subdir,
                "path": str(path),
                "exists": exists,
                "size": path.stat().st_size if exists else 0,
                "sha256": sha256(path) if exists else None,
            }
            if not exists:
                missing.append(str(path))
            entries.append(entry)
    return entries, missing


def copy_runtime_to_probe_dir(runtime_root: Path, out_bin: Path) -> list[dict[str, Any]]:
    out_bin.mkdir(parents=True, exist_ok=True)
    copied: list[dict[str, Any]] = []
    for name in REQUIRED_WINDOWS:
        src = runtime_root / "x86_64-windows" / name
        dst = out_bin / name
        shutil.copy2(src, dst)
        copied.append(
            {
                "name": name,
                "source": str(src),
                "destination": str(dst),
                "source_sha256": sha256(src),
                "destination_sha256": sha256(dst),
                "hash_match": sha256(src) == sha256(dst),
            }
        )
    return copied


def build_identity_probe(repo: Path, cxx: str) -> dict[str, Any]:
    sdk = repo / "tools" / "d3d12-metal-sdk"
    source = sdk / "probes" / "probe_m12_runtime_identity" / "probe_m12_runtime_identity.cpp"
    out_bin = sdk / "out" / "bin"
    exe = out_bin / "probe_m12_runtime_identity.exe"
    out_bin.mkdir(parents=True, exist_ok=True)
    cmd = [
        cxx,
        "-std=c++17",
        "-O2",
        "-static",
        "-static-libgcc",
        "-static-libstdc++",
        "-Wall",
        "-Wextra",
        "-Werror",
        str(source),
        "-lole32",
        "-luuid",
        "-o",
        str(exe),
    ]
    proc = subprocess.run(cmd, cwd=repo, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return {
        "command": cmd,
        "returncode": proc.returncode,
        "stdout": proc.stdout,
        "stderr": proc.stderr,
        "exe": str(exe),
        "ok": proc.returncode == 0 and exe.exists(),
        "exe_sha256": sha256(exe) if exe.exists() else None,
    }


def parse_loaddll(stderr: str) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    pattern = re.compile(r'Loaded L"([^"]+)" at ([^:]+):\s*(\w+)')
    for line in stderr.splitlines():
        match = pattern.search(line)
        if not match:
            continue
        path = match.group(1)
        leaf = path.replace("/", "\\").split("\\")[-1].lower()
        if leaf in {"d3d12.dll", "dxgi.dll", "dxgi_dxmt.dll", "winemetal.dll", "probe_m12_runtime_identity.exe"}:
            rows.append({"path": path, "address": match.group(2), "kind": match.group(3)})
    return rows


def wine_path_to_host(path: str) -> Path | None:
    normalized = path.replace("\\", "/")
    if len(normalized) >= 3 and normalized[1:3] == ":/":
        drive = normalized[0].lower()
        rest = normalized[3:]
        if drive == "z":
            return Path("/") / rest
    return None


def validate_loaded_runtime(
    parsed: dict[str, Any] | None,
    loaddll_rows: list[dict[str, Any]],
    staged_files: list[dict[str, Any]],
) -> dict[str, Any]:
    expected = {entry["name"].lower(): entry for entry in staged_files}
    modules = (parsed or {}).get("modules", {}) if isinstance(parsed, dict) else {}
    loaded_by_name: dict[str, dict[str, Any]] = {}
    for row in loaddll_rows:
        leaf = row["path"].replace("/", "\\").split("\\")[-1].lower()
        loaded_by_name[leaf] = row

    checks: list[dict[str, Any]] = []
    for name, expected_entry in expected.items():
        module = modules.get(name, {})
        wine_path = module.get("path", "") if isinstance(module, dict) else ""
        host_path = wine_path_to_host(wine_path)
        loaded_row = loaded_by_name.get(name, {})
        destination = Path(expected_entry["destination"]).resolve()
        host_path_resolved = host_path.resolve() if host_path and host_path.exists() else host_path
        path_match = bool(host_path_resolved and host_path_resolved == destination)
        hash_value = sha256(host_path_resolved) if host_path_resolved and host_path_resolved.exists() else None
        hash_match = hash_value == expected_entry["destination_sha256"]
        kind_match = loaded_row.get("kind") == "native"
        checks.append(
            {
                "name": name,
                "wine_path": wine_path,
                "host_path": str(host_path_resolved) if host_path_resolved else None,
                "expected_destination": str(destination),
                "loaded_kind": loaded_row.get("kind"),
                "expected_kind": "native",
                "path_match": path_match,
                "sha256": hash_value,
                "expected_sha256": expected_entry["destination_sha256"],
                "hash_match": hash_match,
                "kind_match": kind_match,
                "ok": path_match and hash_match and kind_match,
            }
        )
    return {"ok": all(check["ok"] for check in checks), "checks": checks}


def validate_unix_bridge(
    parsed: dict[str, Any] | None,
    runtime_root: Path,
    wine_runtime: Path,
    winemetal_log: Path,
) -> dict[str, Any]:
    expected_name = "winemetal.so"
    primary = (runtime_root / "x86_64-unix" / expected_name).resolve()
    candidates = [
        primary,
        (wine_runtime / "lib" / "wine" / "x86_64-unix" / expected_name).resolve(),
    ]
    env_value = ""
    if isinstance(parsed, dict):
        env_value = parsed.get("environment", {}).get("DXMT_WINEMETAL_UNIXLIB", "")
    log_text = winemetal_log.read_text(errors="replace") if winemetal_log.exists() else ""
    primary_hash = sha256(primary) if primary.exists() else None
    candidate_checks = []
    for candidate in candidates:
        candidate_hash = sha256(candidate) if candidate.exists() else None
        candidate_checks.append(
            {
                "path": str(candidate),
                "exists": candidate.exists(),
                "sha256": candidate_hash,
                "matches_primary": candidate_hash is not None and candidate_hash == primary_hash,
            }
        )
    return {
        "expected_name": expected_name,
        "primary_path": str(primary),
        "primary_exists": primary.exists(),
        "primary_sha256": primary_hash,
        "candidate_checks": candidate_checks,
        "probe_env_value": env_value,
        "env_match": env_value == expected_name,
        "debug_log": str(winemetal_log),
        "debug_log_exists": winemetal_log.exists(),
        "debug_log_status_ok": "status=0x00000000" in log_text,
        "debug_log_name_match": f"unixlib={expected_name}" in log_text,
        "ok": primary.exists()
        and env_value == expected_name
        and all((not check["exists"]) or check["matches_primary"] for check in candidate_checks)
        and winemetal_log.exists()
        and "status=0x00000000" in log_text
        and f"unixlib={expected_name}" in log_text,
    }


def run_identity_probe(
    repo: Path,
    proof_dir: Path,
    wine: Path,
    wine_runtime: Path,
    prefix: Path,
    runtime_root: Path,
    staged_files: list[dict[str, Any]],
) -> dict[str, Any]:
    sdk = repo / "tools" / "d3d12-metal-sdk"
    out_bin = sdk / "out" / "bin"
    exe = out_bin / "probe_m12_runtime_identity.exe"
    run_dir = proof_dir / "phase0-runtime-identity"
    run_dir.mkdir(parents=True, exist_ok=True)

    unix_bridge = (runtime_root / "x86_64-unix" / "winemetal.so").resolve()
    dyld_parts = [
        str(runtime_root / "x86_64-unix"),
        str(wine_runtime / "lib" / "wine" / "x86_64-unix"),
    ]
    if os.environ.get("DYLD_LIBRARY_PATH"):
        dyld_parts.append(os.environ["DYLD_LIBRARY_PATH"])

    env = os.environ.copy()
    env.update(
        {
            "WINEPREFIX": str(prefix),
            "WINEDLLPATH": str(runtime_root / "x86_64-windows"),
            "WINEDLLOVERRIDES": "d3d12,dxgi,dxgi_dxmt,winemetal=n,b;gameoverlayrenderer,gameoverlayrenderer64=d",
            "DYLD_LIBRARY_PATH": ":".join(dyld_parts),
            "DXMT_WINEMETAL_UNIXLIB": unix_bridge.name,
            "DXMT_WINEMETAL_DEBUG": "1",
            "DXMT_LOG_PATH": str(run_dir),
            "D3D12_METAL_SDK_PROFILE": "m12-fresh-proof",
            "WINEDEBUG": "+loaddll",
        }
    )
    cmd = [str(wine), exe.name]
    proc = subprocess.run(cmd, cwd=out_bin, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout_path = run_dir / "probe_m12_runtime_identity.stdout.json"
    stderr_path = run_dir / "probe_m12_runtime_identity.stderr.txt"
    stdout_path.write_text(proc.stdout)
    stderr_path.write_text(proc.stderr)

    parsed: dict[str, Any] | None = None
    parse_error = ""
    try:
        parsed = json.loads(proc.stdout)
    except json.JSONDecodeError as error:
        parse_error = str(error)

    loaddll_rows = parse_loaddll(proc.stderr)
    runtime_match = validate_loaded_runtime(parsed, loaddll_rows, staged_files)
    unix_bridge_match = validate_unix_bridge(parsed, runtime_root, wine_runtime, run_dir / "winemetal-pe-debug.log")
    result = {
        "command": cmd,
        "cwd": str(out_bin),
        "returncode": proc.returncode,
        "stdout": str(stdout_path),
        "stderr": str(stderr_path),
        "json_parse_error": parse_error,
        "probe_json": parsed,
        "loaddll": loaddll_rows,
        "runtime_match": runtime_match,
        "unix_bridge_match": unix_bridge_match,
        "ok": proc.returncode == 0
        and bool(parsed and parsed.get("pass") is True)
        and runtime_match["ok"]
        and unix_bridge_match["ok"],
    }
    write_json(run_dir / "phase0-runtime-identity-summary.json", result)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="Run the fresh M12 proof harness gates.")
    parser.add_argument("--repo", default=str(Path(__file__).resolve().parents[3]))
    parser.add_argument("--proof-root", default="")
    parser.add_argument("--lab-root", default=str(DEFAULT_LAB_ROOT))
    parser.add_argument("--runtime-root", default=os.path.expanduser("~/.metalsharp/runtime/wine/lib/dxmt_m12"))
    parser.add_argument("--wine-runtime", default=os.path.expanduser("~/.metalsharp/runtime/wine"))
    parser.add_argument("--wine", default=os.path.expanduser("~/.metalsharp/runtime/wine/bin/metalsharp-wine"))
    parser.add_argument("--prefix", default=os.path.expanduser("~/.metalsharp/prefix-steam"))
    parser.add_argument("--min-free-gib", type=int, default=50)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "x86_64-w64-mingw32-g++"))
    parser.add_argument("--skip-run", action="store_true", help="Build and manifest only; do not execute Wine.")
    args = parser.parse_args()

    repo = Path(args.repo).expanduser().resolve()
    lab_root = Path(args.lab_root).expanduser()
    runtime_root = Path(args.runtime_root).expanduser()
    wine_runtime = Path(args.wine_runtime).expanduser()
    wine = Path(args.wine).expanduser()
    prefix = Path(args.prefix).expanduser()
    proof_root = Path(args.proof_root).expanduser() if args.proof_root else (
        lab_root / "06-results" / "in-progress" / f"m12-fresh-proof-game-harness-{timestamp()}"
    )

    if not lab_root.exists():
        print(f"lab root is missing or unmounted: {lab_root}", file=sys.stderr)
        return 2
    free_gib = disk_free_gib(lab_root)
    disk_guard = {
        "path": str(lab_root),
        "free_gib": free_gib,
        "min_free_gib": args.min_free_gib,
        "ok": free_gib >= args.min_free_gib,
    }
    if not disk_guard["ok"]:
        print(f"disk guard failed: {free_gib} GiB free < {args.min_free_gib} GiB", file=sys.stderr)
        return 2

    lab_resolved = lab_root.resolve()
    proof_resolved = proof_root.resolve(strict=False)
    if not (str(proof_resolved) == str(lab_resolved) or str(proof_resolved).startswith(str(lab_resolved) + os.sep)):
        print(f"proof root must be under lab root: {proof_root} not under {lab_root}", file=sys.stderr)
        return 2
    proof_root.mkdir(parents=True, exist_ok=False)

    runtime_entries, missing_runtime = inspect_runtime(runtime_root)
    manifest = {
        "schema": "metalsharp.m12.fresh.proof-run-manifest.v1",
        "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "policy": {
            "fresh_artifacts_only": True,
            "prior_proof_artifacts_allowed": False,
            "live_game_launch_requires_user_approval": True,
        },
        "repo": {
            "path": str(repo),
            "branch": git_text(repo, "branch", "--show-current"),
            "commit": git_text(repo, "rev-parse", "HEAD"),
            "status_short": git_text(repo, "status", "--short"),
        },
        "proof_root": str(proof_root),
        "disk_guard": disk_guard,
        "runtime_root": str(runtime_root),
        "wine_runtime": str(wine_runtime),
        "wine": str(wine),
        "prefix": str(prefix),
        "runtime_files": runtime_entries,
        "missing_runtime_files": missing_runtime,
    }
    write_json(proof_root / "proof-run-manifest.json", manifest)

    if missing_runtime:
        print("missing runtime files:", file=sys.stderr)
        for item in missing_runtime:
            print(f"  {item}", file=sys.stderr)
        return 3
    if not wine.exists():
        print(f"missing wine binary: {wine}", file=sys.stderr)
        return 4
    if not prefix.exists():
        print(f"missing prefix: {prefix}", file=sys.stderr)
        return 5

    build = build_identity_probe(repo, args.cxx)
    write_json(proof_root / "phase0-build-runtime-identity-probe.json", build)
    if not build["ok"]:
        print(build["stderr"], file=sys.stderr)
        return 6

    staged = copy_runtime_to_probe_dir(runtime_root, repo / "tools" / "d3d12-metal-sdk" / "out" / "bin")
    write_json(
        proof_root / "phase0-staged-runtime-hashes.json",
        {"files": staged, "ok": all(x["hash_match"] for x in staged)},
    )
    if not all(x["hash_match"] for x in staged):
        return 7

    identity_result: dict[str, Any] | None = None
    if not args.skip_run:
        identity_result = run_identity_probe(repo, proof_root, wine, wine_runtime, prefix, runtime_root, staged)

    summary = {
        "schema": "metalsharp.m12.fresh.phase0-summary.v1",
        "ok": disk_guard["ok"] and not missing_runtime and build["ok"] and all(x["hash_match"] for x in staged)
        and (args.skip_run or bool(identity_result and identity_result["ok"])),
        "proof_root": str(proof_root),
        "disk_guard": disk_guard,
        "build_ok": build["ok"],
        "runtime_stage_ok": all(x["hash_match"] for x in staged),
        "identity_probe_ok": None if args.skip_run else bool(identity_result and identity_result["ok"]),
        "artifacts": {
            "manifest": str(proof_root / "proof-run-manifest.json"),
            "build": str(proof_root / "phase0-build-runtime-identity-probe.json"),
            "staged_hashes": str(proof_root / "phase0-staged-runtime-hashes.json"),
            "identity": str(proof_root / "phase0-runtime-identity" / "phase0-runtime-identity-summary.json")
            if not args.skip_run
            else None,
        },
    }
    write_json(proof_root / "phase0-summary.json", summary)
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0 if summary["ok"] else 8


if __name__ == "__main__":
    raise SystemExit(main())
