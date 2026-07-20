#!/usr/bin/env python3
"""Exercise renderer-critical state transitions against the hand-written backend."""

from __future__ import annotations

import argparse
import json
import os
import signal
import socket
import subprocess
import tempfile
import time
import urllib.request
from pathlib import Path
from typing import Any


def free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def call(port: int, method: str, path: str, body: dict[str, Any] | None = None) -> dict[str, Any]:
    data = json.dumps(body or {}).encode() if method == "POST" else None
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}{path}", data=data, method=method, headers={"Content-Type": "application/json"}
    )
    with urllib.request.urlopen(req, timeout=5) as response:
        parsed = json.loads(response.read())
    assert isinstance(parsed, dict), (method, path, parsed)
    return parsed


def start(binary: Path, home: Path, port: int) -> subprocess.Popen[bytes]:
    env = os.environ.copy()
    env.update({"HOME": str(home), "METALSHARP_HOME": str(home), "METALSHARP_PORT": str(port)})
    proc = subprocess.Popen(
        [str(binary)], env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            raise AssertionError(f"backend exited with {proc.returncode}")
        try:
            if call(port, "GET", "/status").get("ok"):
                return proc
        except Exception:
            time.sleep(0.1)
    raise AssertionError("backend did not become ready")


def stop(proc: subprocess.Popen[bytes]) -> None:
    if proc.poll() is not None:
        return
    proc.send_signal(signal.SIGTERM)
    proc.wait(timeout=5)
    assert proc.returncode == 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("backend", type=Path)
    args = parser.parse_args()
    binary = args.backend.resolve()
    assert binary.is_file() and os.access(binary, os.X_OK)

    with tempfile.TemporaryDirectory(prefix="metalsharp-electron-flow-") as tmp:
        home = Path(tmp)
        port = free_port()
        proc = start(binary, home, port)
        try:
            status = call(port, "GET", "/status")
            assert status["ok"] is True and status["metalsharp_home"] == str(home)

            initial = call(port, "GET", "/setup/state")
            assert initial["ok"] is True and initial["savedCompleted"] is False
            saved = call(
                port,
                "POST",
                "/setup/save",
                {"step": 2, "deviceName": "Parity Test", "completed": True},
            )
            assert saved["ok"] is True and saved["deviceName"] == "Parity Test"
            renamed = call(port, "POST", "/setup/save", {"deviceName": "Renamed Test"})
            assert renamed["deviceName"] == "Renamed Test" and renamed["savedCompleted"] is True

            config = call(port, "POST", "/config", {"graphicsRuntimeLogs": True})
            assert config["ok"] is True and config["graphicsRuntimeLogs"] is True
            assert call(port, "GET", "/config")["graphics_runtime_logs"] is True

            installer = call(port, "POST", "/setup/install-all")
            assert installer == {"ok": True}
            install_progress = call(port, "GET", "/setup/install-progress")
            assert install_progress["status"] in {"starting", "installing"}
            assert install_progress["total"] == 15
            assert call(port, "GET", "/setup/installing")["installing"] is True

            migration = call(port, "POST", "/update/migrate/start")
            assert migration["ok"] is True
            progress = call(port, "GET", "/update/migrate/progress")
            assert progress["status"] == "running"
            assert progress["step"] in {0, 1} and progress["total"] == 8
            assert progress["message"] == (
                "Starting MetalSharp migration..."
                if progress["step"] == 0
                else "Ensuring extract tools (zstd) are available..."
            )

            killed = call(port, "POST", "/kill", {"pid": 99999999})
            assert killed == {"ok": True, "pid": 99999999}
            assert call(port, "GET", "/status")["ok"] is True
        finally:
            stop(proc)

        # Setup state is durable across backend restarts.
        proc = start(binary, home, port)
        try:
            restored = call(port, "GET", "/setup/state")
            assert restored["deviceName"] == "Renamed Test"
            assert restored["savedCompleted"] is True
            assert call(port, "GET", "/config")["graphicsRuntimeLogs"] is True
            persisted_migration = call(port, "GET", "/update/migrate/progress")
            assert persisted_migration["status"] == "running" and persisted_migration["total"] == 8
        finally:
            stop(proc)

    print("Electron-critical backend flows passed without hanging.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
