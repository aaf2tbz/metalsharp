#!/usr/bin/env python3
"""Replay extracted D3DMetal DXIL bytecode through DXMT airconv and Metal.

Offline-only: reads copied D3DMetal cache references plus the M12 cache root. It
never mutates live shader caches. Success artifacts are compact by default;
failed shaders keep their DXBC/MSL/logs for diagnosis.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import tempfile
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[3]
AIRCONV = REPO_ROOT / "vendor/dxmt/build-metalsharp-x64/src/airconv/darwin/airconv"
GAME_APPIDS = {
    "elden-ring": "1245620",
    "armored-core-vi": "1888160",
    "subnautica-2": "1962700",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True, help="Manifest from extract-d3dmetal-bytecode.py")
    parser.add_argument("--m12-cache-root", type=Path, default=Path.home() / ".metalsharp/shader-cache/m12")
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--mode", choices=["all", "d3dmetal-only", "overlap"], default="d3dmetal-only")
    parser.add_argument("--only-game", choices=list(GAME_APPIDS), default="", help="Validate just one game for small phased passes")
    parser.add_argument("--start-per-game", type=int, default=0, help="Skip N selected records per game before applying max-per-game")
    parser.add_argument("--max-per-game", type=int, default=0, help="0 means no limit")
    parser.add_argument("--skip-metal", action="store_true", help="Only run airconv --emit-msl")
    parser.add_argument("--keep-successes", action="store_true")
    parser.add_argument("--include-unsupported-stages", action="store_true", help="Attempt Metal compile for DXIL geometry/hull/domain/library shaders")
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--workers", type=int, default=1)
    return parser.parse_args()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def m12_shas(cache: Path) -> set[str]:
    out: set[str] = set()
    if not cache.is_dir():
        return out
    for path in cache.glob("*.dxbc"):
        try:
            out.add(sha256_file(path))
        except OSError:
            pass
    return out


def run(cmd: list[str], timeout: int, env: dict[str, str] | None = None) -> dict[str, Any]:
    try:
        proc = subprocess.run(
            cmd,
            cwd=REPO_ROOT,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
        )
        return {"returncode": proc.returncode, "stdout": proc.stdout, "stderr": proc.stderr, "timeout": False}
    except subprocess.TimeoutExpired as exc:
        return {
            "returncode": 124,
            "stdout": exc.stdout.decode("utf-8", "replace") if isinstance(exc.stdout, bytes) else (exc.stdout or ""),
            "stderr": exc.stderr.decode("utf-8", "replace") if isinstance(exc.stderr, bytes) else (exc.stderr or ""),
            "timeout": True,
        }


def read_blob(reference_root: Path, rec: dict[str, Any]) -> bytes:
    first_source = str(rec["first_source"])
    if reference_root.name == "shaders.cache" and first_source.startswith("shaders.cache/"):
        source = reference_root / first_source.removeprefix("shaders.cache/")
    else:
        source = reference_root / first_source
    off = int(rec["first_offset"])
    size = int(rec["size"])
    with source.open("rb") as f:
        f.seek(off)
        data = f.read(size)
    if len(data) != size or not data.startswith(b"DXBC"):
        raise ValueError(f"invalid blob slice {source}@{off}+{size}")
    digest = sha256_bytes(data)
    if digest != rec["sha256"]:
        raise ValueError(f"sha mismatch {source}@{off}: {digest} != {rec['sha256']}")
    return data


SUPPORTED_SHADER_KINDS = {"vertex", "pixel", "compute"}


def choose_records(game: str, records: list[dict[str, Any]], m12: set[str], mode: str, start: int, limit: int, include_unsupported_stages: bool) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    chosen: list[dict[str, Any]] = []
    skipped: list[dict[str, Any]] = []
    eligible_seen = 0
    for rec in records:
        sha = rec["sha256"]
        in_m12 = sha in m12
        if mode == "d3dmetal-only" and in_m12:
            continue
        if mode == "overlap" and not in_m12:
            continue
        shader_kind = rec.get("shader_kind") or "unknown"
        if not include_unsupported_stages and shader_kind not in SUPPORTED_SHADER_KINDS:
            skipped.append(rec)
            continue
        if eligible_seen < start:
            eligible_seen += 1
            continue
        chosen.append(rec)
        eligible_seen += 1
        if limit and len(chosen) >= limit:
            break
    return chosen, skipped


def classify_failure(text: str) -> str:
    lower = text.lower()
    if "xpc_error_connection_interrupted" in lower or "interrupted connection" in lower:
        return "metal-compiler-xpc-interrupted"
    if "airconv" in lower and "assert" in lower:
        return "airconv-assertion"
    if "unsupported" in lower:
        return "unsupported-translation-feature"
    if "timeout" in lower:
        return "timeout"
    if "error:" in lower:
        return "compiler-error"
    return "unclassified"


def validate_one(game: str, reference_root: Path, rec: dict[str, Any], out_dir: Path, skip_metal: bool, keep_successes: bool, timeout: int) -> dict[str, Any]:
    sha = rec["sha256"]
    fail_dir = out_dir / "failures" / game / sha
    success_dir = out_dir / "successes" / game if keep_successes else None
    work_parent = success_dir if keep_successes else Path(tempfile.mkdtemp(prefix="d3dmetal-bytecode-", dir=str(out_dir)))
    work_parent.mkdir(parents=True, exist_ok=True)
    dxbc = work_parent / f"{sha}.dxbc"
    msl = work_parent / f"{sha}.msl"
    air = work_parent / f"{sha}.air"
    try:
        dxbc.write_bytes(read_blob(reference_root, rec))
        env = os.environ.copy()
        env["DYLD_LIBRARY_PATH"] = "/usr/lib" + (":" + env["DYLD_LIBRARY_PATH"] if env.get("DYLD_LIBRARY_PATH") else "")
        airconv = run([str(AIRCONV), "--emit-msl", "-o", str(msl), str(dxbc)], timeout=timeout, env=env)
        airconv_ok = airconv["returncode"] == 0 and msl.exists() and msl.stat().st_size > 0
        metal = None
        metal_ok = True
        if airconv_ok and not skip_metal:
            metal = run(["xcrun", "metal", "-x", "metal", "-c", str(msl), "-o", str(air)], timeout=timeout)
            metal_ok = metal["returncode"] == 0 and air.exists() and air.stat().st_size > 0
        ok = airconv_ok and metal_ok
        text = (airconv.get("stdout", "") + airconv.get("stderr", "") + ((metal or {}).get("stdout", "") if metal else "") + ((metal or {}).get("stderr", "") if metal else ""))
        result = {
            "game": game,
            "sha256": sha,
            "kind": rec.get("kind"),
            "shader_kind": rec.get("shader_kind"),
            "size": rec.get("size"),
            "first_source": rec.get("first_source"),
            "first_offset": rec.get("first_offset"),
            "ok": ok,
            "airconv_ok": airconv_ok,
            "metal_ok": metal_ok,
            "airconv_returncode": airconv["returncode"],
            "metal_returncode": None if metal is None else metal["returncode"],
            "failure_class": None if ok else classify_failure(text),
            "stderr_tail": text[-4000:] if not ok else "",
        }
        if not ok:
            fail_dir.mkdir(parents=True, exist_ok=True)
            (fail_dir / dxbc.name).write_bytes(dxbc.read_bytes())
            if msl.exists():
                (fail_dir / msl.name).write_bytes(msl.read_bytes())
            (fail_dir / "airconv.stdout.txt").write_text(airconv.get("stdout", ""))
            (fail_dir / "airconv.stderr.txt").write_text(airconv.get("stderr", ""))
            if metal:
                (fail_dir / "metal.stdout.txt").write_text(metal.get("stdout", ""))
                (fail_dir / "metal.stderr.txt").write_text(metal.get("stderr", ""))
            result["failure_dir"] = str(fail_dir)
        return result
    finally:
        if not keep_successes:
            try:
                for p in work_parent.glob("*"):
                    p.unlink()
                work_parent.rmdir()
            except OSError:
                pass


def write_summary(out_dir: Path, manifest: dict[str, Any]) -> None:
    lines = ["# D3DMetal bytecode translation validation", ""]
    s = manifest["summary"]
    for key in ["mode", "skip_metal", "selected", "skipped_unsupported_stage", "ok", "airconv_failures", "metal_failures", "failure_count"]:
        lines.append(f"- {key}: `{s[key]}`")
    lines.append("")
    lines.append("| Game | Selected | Skipped unsupported | OK | Airconv failures | Metal failures |")
    lines.append("|---|---:|---:|---:|---:|---:|")
    for game in manifest["games"]:
        lines.append(f"| {game['game']} | {game['selected']} | {game['skipped_unsupported_stage']} | {game['ok']} | {game['airconv_failures']} | {game['metal_failures']} |")
    failures = [r for game in manifest["games"] for r in game["results"] if not r["ok"]]
    if failures:
        lines.append("")
        lines.append("## Failures")
        for r in failures[:50]:
            lines.append(f"- `{r['game']}` `{r['sha256']}` stage=`{r.get('shader_kind')}` class=`{r['failure_class']}` airconv={r['airconv_returncode']} metal={r['metal_returncode']} source=`{r['first_source']}@{r['first_offset']}` dir=`{r.get('failure_dir','')}`")
    out_dir.joinpath("summary.md").write_text("\n".join(lines) + "\n")


def main() -> int:
    args = parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    source_manifest = json.loads(args.manifest.read_text())
    if not AIRCONV.is_file():
        raise SystemExit(f"missing airconv: {AIRCONV}")
    games_out = []
    total_selected = total_ok = airconv_failures = metal_failures = total_skipped = 0
    for game_entry in source_manifest["games"]:
        game = game_entry["game"]
        if args.only_game and game != args.only_game:
            continue
        appid = GAME_APPIDS[game]
        reference_root = Path(game_entry["d3dmetal"]["cache"])
        m12 = m12_shas(args.m12_cache_root / appid)
        records, skipped = choose_records(game, game_entry["d3dmetal"]["unique"], m12, args.mode, args.start_per_game, args.max_per_game, args.include_unsupported_stages)
        if args.workers > 1 and records:
            results_by_sha: dict[str, dict[str, Any]] = {}
            with ThreadPoolExecutor(max_workers=args.workers) as pool:
                future_map = {
                    pool.submit(validate_one, game, reference_root, rec, args.out_dir, args.skip_metal, args.keep_successes, args.timeout): rec
                    for rec in records
                }
                for future in as_completed(future_map):
                    rec = future_map[future]
                    try:
                        results_by_sha[rec["sha256"]] = future.result()
                    except Exception as exc:
                        results_by_sha[rec["sha256"]] = {
                            "game": game,
                            "sha256": rec["sha256"],
                            "kind": rec.get("kind"),
                            "shader_kind": rec.get("shader_kind"),
                            "size": rec.get("size"),
                            "first_source": rec.get("first_source"),
                            "first_offset": rec.get("first_offset"),
                            "ok": False,
                            "airconv_ok": False,
                            "metal_ok": False,
                            "airconv_returncode": -1,
                            "metal_returncode": None,
                            "failure_class": "validator-exception",
                            "stderr_tail": str(exc),
                        }
            results = [results_by_sha[rec["sha256"]] for rec in records]
        else:
            results = [validate_one(game, reference_root, rec, args.out_dir, args.skip_metal, args.keep_successes, args.timeout) for rec in records]
        selected = len(results)
        ok = sum(1 for r in results if r["ok"])
        af = sum(1 for r in results if not r["airconv_ok"])
        mf = sum(1 for r in results if r["airconv_ok"] and not r["metal_ok"])
        total_selected += selected
        total_skipped += len(skipped)
        total_ok += ok
        airconv_failures += af
        metal_failures += mf
        skipped_kind_counts: dict[str, int] = {}
        for rec in skipped:
            shader_kind = rec.get("shader_kind") or "unknown"
            skipped_kind_counts[shader_kind] = skipped_kind_counts.get(shader_kind, 0) + 1
        games_out.append({"game": game, "selected": selected, "skipped_unsupported_stage": len(skipped), "skipped_kind_counts": dict(sorted(skipped_kind_counts.items())), "ok": ok, "airconv_failures": af, "metal_failures": mf, "results": results})
    summary = {
        "mode": args.mode,
        "skip_metal": args.skip_metal,
        "only_game": args.only_game,
        "start_per_game": args.start_per_game,
        "max_per_game": args.max_per_game,
        "selected": total_selected,
        "skipped_unsupported_stage": total_skipped,
        "ok": total_ok,
        "airconv_failures": airconv_failures,
        "metal_failures": metal_failures,
        "failure_count": total_selected - total_ok,
    }
    out = {
        "schema": "metalsharp.d3dmetal-bytecode-translation-validation.v1",
        "source_manifest": str(args.manifest),
        "m12_cache_root": str(args.m12_cache_root),
        "airconv": str(AIRCONV),
        "summary": summary,
        "games": games_out,
    }
    args.out_dir.joinpath("manifest.json").write_text(json.dumps(out, indent=2) + "\n")
    write_summary(args.out_dir, out)
    print(args.out_dir / "manifest.json")
    print(args.out_dir / "summary.md")
    return 0 if summary["failure_count"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
