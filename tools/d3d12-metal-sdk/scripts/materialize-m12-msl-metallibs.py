#!/usr/bin/env python3
"""Materialize persisted M12 MSL sidecars into persisted Metal libraries.

The runtime can persist DXIL->MSL output as <hash>.msl and load <hash>.metallib
with Metal's fast newLibraryWithData path, but Metal's newLibraryWithSource path
only creates an in-process library and cannot be reused by the next game launch.
This utility fills that missing persistence step for warm-cache validation and
prewarm packs.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import shutil
import subprocess
import tempfile
import time
from pathlib import Path
from typing import Iterable


def resolve_xcrun_tool(name: str) -> str:
    try:
        out = subprocess.check_output(["xcrun", "-f", name], text=True).strip()
    except subprocess.CalledProcessError as exc:
        raise SystemExit(f"xcrun tool unavailable: {name}") from exc
    if not out:
        raise SystemExit(f"xcrun tool unavailable: {name}")
    return out


def run(cmd: list[str], timeout: int) -> tuple[int, str, str, float]:
    start = time.monotonic()
    try:
        proc = subprocess.run(cmd, text=True, capture_output=True, timeout=timeout)
        return proc.returncode, proc.stdout, proc.stderr, time.monotonic() - start
    except subprocess.TimeoutExpired as exc:
        stdout = exc.stdout if isinstance(exc.stdout, str) else ""
        stderr = exc.stderr if isinstance(exc.stderr, str) else ""
        return 124, stdout, stderr or f"timed out after {timeout}s", time.monotonic() - start


def normalize_hash(raw: str) -> str:
    h = raw.strip().lower()
    if h.startswith("0x"):
        h = h[2:]
    if h and all(c in "0123456789abcdef" for c in h) and len(h) <= 16:
        return h.zfill(16)
    return h


def iter_msl(cache_dir: Path, hashes: set[str] | None) -> Iterable[Path]:
    if hashes:
        for h in sorted(normalize_hash(v) for v in hashes):
            yield cache_dir / f"{h}.msl"
        return
    yield from sorted(cache_dir.glob("*.msl"))


def dry_run_row(msl: Path) -> dict:
    metallib = msl.with_suffix(".metallib")
    row = {
        "hash": msl.stem,
        "msl": str(msl),
        "metallib": str(metallib),
        "msl_exists": msl.exists(),
        "metallib_exists": metallib.exists(),
        "status": "unknown",
    }
    if not msl.exists():
        row["status"] = "missing_source"
    elif metallib.exists() and metallib.stat().st_size > 0 and metallib.stat().st_mtime >= msl.stat().st_mtime:
        row["status"] = "fresh"
        row["bytes"] = metallib.stat().st_size
    else:
        row["status"] = "would_build"
        row["msl_bytes"] = msl.stat().st_size
        row["metallib_bytes"] = metallib.stat().st_size if metallib.exists() else 0
    return row


def materialize_one(msl: Path, metal_tool: str, metallib_tool: str, timeout: int, force: bool) -> dict:
    stem = msl.stem
    metallib = msl.with_suffix(".metallib")
    err_path = msl.with_suffix(".metallib.err.txt")
    row = {
        "hash": stem,
        "msl": str(msl),
        "metallib": str(metallib),
        "status": "unknown",
        "elapsed_ms": 0,
        "bytes": 0,
    }
    if not msl.exists():
        row["status"] = "missing_source"
        return row
    if metallib.exists() and metallib.stat().st_size > 0 and metallib.stat().st_mtime >= msl.stat().st_mtime and not force:
        row["status"] = "fresh"
        row["bytes"] = metallib.stat().st_size
        return row

    start = time.monotonic()
    with tempfile.TemporaryDirectory(prefix=f"m12-metallib-{stem}-") as td:
        tmp = Path(td)
        air = tmp / f"{stem}.air"
        tmp_metallib = tmp / f"{stem}.metallib"
        metal_cmd = [metal_tool, "-x", "metal", "-std=metal3.1", "-c", str(msl), "-o", str(air)]
        rc, out, err, metal_elapsed = run(metal_cmd, timeout)
        if rc != 0:
            err_path.write_text(err or out or f"metal failed rc={rc}\n")
            row.update({"status": "metal_failed", "elapsed_ms": int((time.monotonic() - start) * 1000), "stderr": err[-4000:]})
            return row
        metallib_cmd = [metallib_tool, str(air), "-o", str(tmp_metallib)]
        rc, out, err, lib_elapsed = run(metallib_cmd, timeout)
        if rc != 0:
            err_path.write_text(err or out or f"metallib failed rc={rc}\n")
            row.update({"status": "metallib_failed", "elapsed_ms": int((time.monotonic() - start) * 1000), "stderr": err[-4000:]})
            return row
        if not tmp_metallib.exists() or tmp_metallib.stat().st_size <= 0:
            err_path.write_text("metallib output missing/empty\n")
            row.update({"status": "empty", "elapsed_ms": int((time.monotonic() - start) * 1000)})
            return row
        final_tmp = metallib.with_suffix(".metallib.tmp")
        shutil.copyfile(tmp_metallib, final_tmp)
        os.replace(final_tmp, metallib)
        try:
            err_path.unlink()
        except FileNotFoundError:
            pass
        row.update({
            "status": "ok",
            "elapsed_ms": int((time.monotonic() - start) * 1000),
            "metal_ms": int(metal_elapsed * 1000),
            "metallib_ms": int(lib_elapsed * 1000),
            "bytes": metallib.stat().st_size,
        })
        return row


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cache-dir", type=Path, required=True)
    ap.add_argument("--hash", action="append", default=[], help="16-hex shader hash to materialize; repeatable. Default: all *.msl")
    ap.add_argument("--hash-file", type=Path, help="newline-separated hashes")
    ap.add_argument("--workers", type=int, default=2)
    ap.add_argument("--timeout", type=int, default=180)
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--strict", action="store_true", help="Return nonzero for missing requested sources as well as compile failures.")
    ap.add_argument("--out", type=Path, help="JSON report path")
    args = ap.parse_args()

    hashes = set(normalize_hash(h) for h in args.hash if h.strip())
    if args.hash_file:
        hashes.update(normalize_hash(line) for line in args.hash_file.read_text().splitlines() if line.strip() and not line.strip().startswith("#"))
    hashes = hashes or None

    cache_dir = args.cache_dir.expanduser().resolve()
    candidates = list(iter_msl(cache_dir, hashes))
    if args.limit > 0:
        candidates = candidates[: args.limit]

    if args.dry_run:
        rows = [dry_run_row(p) for p in candidates]
        counts: dict[str, int] = {}
        for row in rows:
            counts[row["status"]] = counts.get(row["status"], 0) + 1
        report = {"schema": "metalsharp.m12.msl-metallib-materialize.v1", "cache_dir": str(cache_dir), "dry_run": True, "total": len(rows), "counts": counts, "rows": rows}
        print(json.dumps(report, indent=2))
        if args.out:
            args.out.parent.mkdir(parents=True, exist_ok=True)
            args.out.write_text(json.dumps(report, indent=2))
        return 1 if args.strict and counts.get("missing_source", 0) else 0

    metal_tool = resolve_xcrun_tool("metal")
    metallib_tool = resolve_xcrun_tool("metallib")

    rows = []
    counts: dict[str, int] = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.workers)) as ex:
        futs = [ex.submit(materialize_one, p, metal_tool, metallib_tool, args.timeout, args.force) for p in candidates]
        for fut in concurrent.futures.as_completed(futs):
            row = fut.result()
            rows.append(row)
            counts[row["status"]] = counts.get(row["status"], 0) + 1
            print(f"{row['status']:16s} {row['hash']} {row.get('elapsed_ms', 0)}ms {row.get('bytes', 0)}B", flush=True)

    rows.sort(key=lambda r: r["hash"])
    report = {
        "schema": "metalsharp.m12.msl-metallib-materialize.v1",
        "cache_dir": str(cache_dir),
        "metal_tool": metal_tool,
        "metallib_tool": metallib_tool,
        "total": len(rows),
        "counts": counts,
        "rows": rows,
    }
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(report, indent=2))
    print(json.dumps({"total": len(rows), "counts": counts}, indent=2))
    failed = any(k.endswith("failed") or k == "empty" for k in counts)
    missing = counts.get("missing_source", 0) > 0
    return 1 if failed or (args.strict and missing) else 0


if __name__ == "__main__":
    raise SystemExit(main())
