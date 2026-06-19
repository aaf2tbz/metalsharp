#!/usr/bin/env python3
from __future__ import annotations

import json
import os
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / "tools/d3d12-metal-sdk/scripts/verify-m12-metallib-freshness.py"


def write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="m12-freshness-test-") as td:
        root = Path(td)
        hashes = {
            "fresh": "0000000000000001",
            "missing": "0000000000000002",
            "stale": "0000000000000003",
            "active_error": "0000000000000004",
            "zero": "0000000000000005",
            "bad_header": "0000000000000006",
            "old_error": "0000000000000007",
        }
        now = time.time()

        # Fresh: msl older, metallib newer, MTLB header.
        write(root / f"{hashes['fresh']}.msl", b"metal source")
        write(root / f"{hashes['fresh']}.metallib", b"MTLBfresh")
        os.utime(root / f"{hashes['fresh']}.msl", (now - 20, now - 20))
        os.utime(root / f"{hashes['fresh']}.metallib", (now - 10, now - 10))

        # Missing metallib.
        write(root / f"{hashes['missing']}.msl", b"metal source")

        # Stale metallib older than msl.
        write(root / f"{hashes['stale']}.msl", b"new source")
        write(root / f"{hashes['stale']}.metallib", b"MTLBold")
        os.utime(root / f"{hashes['stale']}.metallib", (now - 30, now - 30))
        os.utime(root / f"{hashes['stale']}.msl", (now - 5, now - 5))

        # Active error newer than metallib.
        write(root / f"{hashes['active_error']}.msl", b"source")
        write(root / f"{hashes['active_error']}.metallib", b"MTLBdata")
        write(root / f"{hashes['active_error']}.metallib.err.txt", b"failed")
        os.utime(root / f"{hashes['active_error']}.metallib", (now - 10, now - 10))
        os.utime(root / f"{hashes['active_error']}.metallib.err.txt", (now - 1, now - 1))

        # Zero byte metallib.
        write(root / f"{hashes['zero']}.msl", b"source")
        write(root / f"{hashes['zero']}.metallib", b"")

        # Invalid header.
        write(root / f"{hashes['bad_header']}.msl", b"source")
        write(root / f"{hashes['bad_header']}.metallib", b"BAD!data")

        # Old error is informational only; should still be fresh.
        write(root / f"{hashes['old_error']}.msl", b"source")
        write(root / f"{hashes['old_error']}.metallib", b"MTLBdata")
        write(root / f"{hashes['old_error']}.metallib.err.txt", b"old failure")
        os.utime(root / f"{hashes['old_error']}.msl", (now - 30, now - 30))
        os.utime(root / f"{hashes['old_error']}.metallib.err.txt", (now - 20, now - 20))
        os.utime(root / f"{hashes['old_error']}.metallib", (now - 10, now - 10))

        out = root / "report.json"
        cp = subprocess.run(
            [str(SCRIPT), "--cache-dir", str(root), "--all", "--strict", "--out", str(out)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        if cp.returncode == 0:
            raise AssertionError("strict checker should fail crafted nonfresh cache")
        data = json.loads(out.read_text())
        rows = {r["hash"]: r for r in data["rows"]}
        assert rows[hashes["fresh"]]["status"] == "fresh"
        assert rows[hashes["old_error"]]["status"] == "fresh"
        assert "missing_metallib" in rows[hashes["missing"]]["categories"]
        assert "stale_metallib_older_than_msl" in rows[hashes["stale"]]["categories"]
        assert "active_metallib_error" in rows[hashes["active_error"]]["categories"]
        assert "zero_byte_metallib" in rows[hashes["zero"]]["categories"]
        assert "invalid_metallib_header" in rows[hashes["bad_header"]]["categories"]
        print("verify-m12-metallib-freshness self-test: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
