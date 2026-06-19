#!/usr/bin/env python3
"""Offline self-test for materialize-m12-msl-metallibs.py.

Uses a temporary cache only. Does not launch games or modify live shader caches.
"""
from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / "tools" / "d3d12-metal-sdk" / "scripts" / "materialize-m12-msl-metallibs.py"

VALID_MSL = r"""
#include <metal_stdlib>
using namespace metal;
vertex float4 vs_main(uint vid [[vertex_id]]) {
  return float4(float(vid & 1u), float((vid >> 1u) & 1u), 0.0, 1.0);
}
fragment float4 ps_main() {
  return float4(1.0, 0.0, 1.0, 1.0);
}
""".strip() + "\n"

INVALID_MSL = "this is not valid metal source\n"


def run(args: list[str], *, expect: int | None = None) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run([sys.executable, str(SCRIPT), *args], text=True, capture_output=True)
    if expect is not None and proc.returncode != expect:
        raise AssertionError(
            f"expected rc={expect}, got {proc.returncode}\ncmd={args}\nstdout={proc.stdout}\nstderr={proc.stderr}"
        )
    return proc


def load(path: Path) -> dict:
    return json.loads(path.read_text())


def assert_no_air(cache: Path) -> None:
    leaked = list(cache.glob("*.air"))
    if leaked:
        raise AssertionError(f"unexpected .air files in cache: {leaked}")


def main() -> int:
    if not shutil.which("xcrun"):
        print("SKIP: xcrun not available")
        return 0
    if not SCRIPT.exists():
        raise AssertionError(f"missing script: {SCRIPT}")

    valid_short = "ab"
    valid = "00000000000000ab"
    missing_short = "def"
    missing = "0000000000000def"
    invalid = "0000000000000bad"

    with tempfile.TemporaryDirectory(prefix="m12-materialize-test-") as td:
        cache = Path(td) / "cache"
        cache.mkdir()
        (cache / f"{valid}.msl").write_text(VALID_MSL)
        (cache / f"{invalid}.msl").write_text(INVALID_MSL)

        # Dry-run reports normalized hashes and missing sources; strict dry-run fails.
        dry = Path(td) / "dry.json"
        run(["--cache-dir", str(cache), "--hash", valid_short, "--hash", missing_short, "--dry-run", "--strict", "--out", str(dry)], expect=1)
        dry_report = load(dry)
        assert dry_report["counts"].get("would_build") == 1, dry_report
        assert dry_report["counts"].get("missing_source") == 1, dry_report
        assert {row["hash"] for row in dry_report["rows"]} == {valid, missing}, dry_report

        # Non-strict materialization compiles valid inputs and reports missing requested hashes.
        report = Path(td) / "materialize.json"
        run(["--cache-dir", str(cache), "--hash", valid_short, "--hash", missing_short, "--out", str(report)], expect=0)
        j = load(report)
        assert j["counts"].get("ok") == 1, j
        assert j["counts"].get("missing_source") == 1, j
        metallib = cache / f"{valid}.metallib"
        assert metallib.exists() and metallib.stat().st_size > 0, metallib
        assert not (cache / f"{missing}.metallib").exists()
        assert_no_air(cache)

        # Strict materialization fails when an explicit source is missing.
        strict = Path(td) / "strict.json"
        run(["--cache-dir", str(cache), "--hash", missing_short, "--strict", "--out", str(strict)], expect=1)
        assert load(strict)["counts"].get("missing_source") == 1

        # A second pass sees the existing metallib as fresh.
        fresh = Path(td) / "fresh.json"
        run(["--cache-dir", str(cache), "--hash", valid_short, "--out", str(fresh)], expect=0)
        assert load(fresh)["counts"].get("fresh") == 1, load(fresh)

        # Force rebuild produces a new ok result.
        old_mtime = metallib.stat().st_mtime_ns
        time.sleep(0.05)
        forced = Path(td) / "forced.json"
        run(["--cache-dir", str(cache), "--hash", valid_short, "--force", "--out", str(forced)], expect=0)
        assert load(forced)["counts"].get("ok") == 1, load(forced)
        assert metallib.stat().st_mtime_ns > old_mtime
        assert_no_air(cache)

        # Invalid MSL fails, writes an error file, and leaves no final metallib.
        failed = Path(td) / "failed.json"
        run(["--cache-dir", str(cache), "--hash", invalid, "--out", str(failed)], expect=1)
        fj = load(failed)
        assert fj["counts"].get("metal_failed") == 1, fj
        assert (cache / f"{invalid}.metallib.err.txt").exists()
        assert not (cache / f"{invalid}.metallib").exists()
        assert_no_air(cache)

    print("materialize-m12-msl-metallibs self-test: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
