#!/usr/bin/env python3
"""Lightweight process sampler for M12 performance runs."""
from __future__ import annotations

import argparse
import csv
import os
import subprocess
import time
from pathlib import Path


def read_rss_bytes(pid: int) -> int | None:
    try:
        out = subprocess.check_output(["ps", "-o", "rss=", "-p", str(pid)], text=True, stderr=subprocess.DEVNULL).strip()
        return int(out) * 1024 if out else None
    except Exception:
        return None


def read_threads(pid: int) -> int | None:
    try:
        out = subprocess.check_output(["ps", "-M", str(pid)], text=True, stderr=subprocess.DEVNULL)
        return max(0, len(out.splitlines()) - 1)
    except Exception:
        return None


def read_cpu_percent(pid: int) -> float | None:
    try:
        out = subprocess.check_output(["ps", "-o", "%cpu=", "-p", str(pid)], text=True, stderr=subprocess.DEVNULL).strip()
        return float(out) if out else None
    except Exception:
        return None


def alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--pid", type=int, required=True)
    ap.add_argument("--seconds", type=float, required=True)
    ap.add_argument("--interval-ms", type=int, default=250)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()

    args.out.parent.mkdir(parents=True, exist_ok=True)
    start = time.monotonic()
    interval = max(0.05, args.interval_ms / 1000.0)
    with args.out.open("w", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=["time_ms", "pid", "alive", "cpu_percent", "rss_bytes", "threads"])
        writer.writeheader()
        while True:
            elapsed = time.monotonic() - start
            is_alive = alive(args.pid)
            writer.writerow({
                "time_ms": int(elapsed * 1000),
                "pid": args.pid,
                "alive": int(is_alive),
                "cpu_percent": read_cpu_percent(args.pid) if is_alive else "",
                "rss_bytes": read_rss_bytes(args.pid) if is_alive else "",
                "threads": read_threads(args.pid) if is_alive else "",
            })
            fh.flush()
            if elapsed >= args.seconds or not is_alive:
                break
            time.sleep(interval)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
