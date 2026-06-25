#!/usr/bin/env python3
"""Offline AC6 shader-cache DXBC/DXIL lowering scanner.

Runs the native airconv --emit-msl path against captured shader cache files with
per-file timeouts and records timing/output-size/error metadata. This does not
launch a game and does not require runtime logging.
"""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
import time
from pathlib import Path


def run_one(airconv: Path, path: Path, out_dir: Path, timeout_s: float) -> dict:
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / f"{path.name}.msl"
    err = out_dir / f"{path.name}.stderr.txt"
    start = time.monotonic()
    status = "unknown"
    rc = None
    stderr_text = ""
    try:
        proc = subprocess.run(
            [str(airconv), "--emit-msl", str(path), "-o", str(out)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=timeout_s,
        )
        rc = proc.returncode
        stderr_text = proc.stderr[-12000:]
        status = "ok" if rc == 0 and out.exists() and out.stat().st_size > 0 else "failed"
    except subprocess.TimeoutExpired as exc:
        status = "timeout"
        stderr_text = (exc.stderr or "") if isinstance(exc.stderr, str) else ""
    elapsed = time.monotonic() - start
    if stderr_text:
        err.write_text(stderr_text, encoding="utf-8", errors="replace")
    return {
        "file": str(path),
        "name": path.name,
        "stem": path.stem,
        "input_size": path.stat().st_size,
        "status": status,
        "returncode": rc,
        "elapsed_s": round(elapsed, 4),
        "output_size": out.stat().st_size if out.exists() else 0,
        "stderr_path": str(err) if stderr_text else "",
        "output_path": str(out) if out.exists() else "",
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", type=Path, default=Path.home() / ".metalsharp/shader-cache/m12/1888160")
    ap.add_argument("--airconv", type=Path, required=True)
    ap.add_argument("--out-dir", type=Path, required=True)
    ap.add_argument("--timeout", type=float, default=8.0)
    ap.add_argument("--limit", type=int, default=0)
    args = ap.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    files = sorted(args.cache.glob("*.dxbc"), key=lambda p: p.stat().st_size, reverse=True)
    if args.limit:
        files = files[: args.limit]

    rows = []
    for i, path in enumerate(files, 1):
        row = run_one(args.airconv, path, args.out_dir / "msl", args.timeout)
        row["index"] = i
        rows.append(row)
        print(f"[{i}/{len(files)}] {row['status']:7s} {row['elapsed_s']:8.3f}s in={row['input_size']:8d} out={row['output_size']:8d} {path.name}", flush=True)

    json_path = args.out_dir / "scan-results.json"
    csv_path = args.out_dir / "scan-results.csv"
    json_path.write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
    with csv_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()) if rows else ["file"])
        writer.writeheader()
        writer.writerows(rows)

    by_elapsed = sorted(rows, key=lambda r: r["elapsed_s"], reverse=True)[:20]
    by_size = sorted(rows, key=lambda r: r["input_size"], reverse=True)[:20]
    bad = [r for r in rows if r["status"] != "ok"]
    lines = [
        "# AC6 offline airconv lowering scan",
        "",
        f"cache: `{args.cache}`",
        f"airconv: `{args.airconv}`",
        f"timeout_s: `{args.timeout}`",
        f"files: `{len(rows)}`",
        f"ok: `{sum(1 for r in rows if r['status'] == 'ok')}`",
        f"failed_or_timeout: `{len(bad)}`",
        "",
        "## Failed / timeout",
        "",
    ]
    if bad:
        for r in bad:
            lines.append(f"- `{r['status']}` `{r['elapsed_s']}`s `{r['name']}` input={r['input_size']} stderr=`{r['stderr_path']}`")
    else:
        lines.append("- none")
    lines += ["", "## Slowest", ""]
    for r in by_elapsed:
        lines.append(f"- `{r['elapsed_s']}`s `{r['name']}` input={r['input_size']} output={r['output_size']} status={r['status']}")
    lines += ["", "## Largest inputs", ""]
    for r in by_size:
        lines.append(f"- input={r['input_size']} `{r['name']}` elapsed={r['elapsed_s']}s output={r['output_size']} status={r['status']}")
    (args.out_dir / "scan-summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(args.out_dir / "scan-summary.md")
    return 0 if not bad else 2


if __name__ == "__main__":
    raise SystemExit(main())
