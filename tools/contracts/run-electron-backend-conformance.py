#!/usr/bin/env python3
"""Run the language-neutral HTTP compatibility suite against a backend artifact."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from urllib.error import URLError
from urllib.request import Request, urlopen


ROOT = Path(__file__).resolve().parents[2]
CONTRACT = ROOT / "contracts" / "electron-backend.v1.json"


def fail(message: str) -> None:
    print(f"conformance failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def request(port: int, method: str, path: str) -> tuple[int, dict]:
    req = Request(f"http://127.0.0.1:{port}{path}", method=method)
    with urlopen(req, timeout=3) as response:
        body = json.loads(response.read().decode("utf-8"))
        if not isinstance(body, dict):
            fail(f"{method} {path} returned a non-object JSON response")
        return response.status, body


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("backend", type=Path)
    args = parser.parse_args()
    backend = args.backend.resolve()
    if not backend.is_file() or not os.access(backend, os.X_OK):
        fail(f"backend is not executable: {backend}")

    contract = json.loads(CONTRACT.read_text())
    port = free_port()
    home = Path(tempfile.mkdtemp(prefix="metalsharp-contract-"))
    # Keep host Steam/GOG libraries out of the language-neutral contract gate.
    # Otherwise /steam/library can scan a developer's real, potentially large
    # library and serialize every later conformance request behind it.
    env = {**os.environ, "HOME": str(home), "METALSHARP_HOME": str(home), "METALSHARP_PORT": str(port)}
    process = subprocess.Popen([str(backend)], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        deadline = time.monotonic() + 12
        status_body: dict | None = None
        while time.monotonic() < deadline:
            try:
                _, status_body = request(port, "GET", "/status")
                break
            except (URLError, ConnectionError, TimeoutError):
                time.sleep(0.15)
        if status_body is None:
            output = process.stdout.read() if process.stdout else ""
            fail(f"backend did not become ready: {output}")

        status_spec = contract["status"]
        missing = [key for key in status_spec["required"] if key not in status_body]
        if missing:
            fail(f"/status missing required keys: {missing}")
        reported_contract = status_body.get(status_spec["contract_version_field"])
        expected = contract["contract_version"]
        if reported_contract is None:
            reported_contract = status_spec.get("legacy_versions", {}).get(status_body.get("version"))
        if reported_contract != expected:
            fail(f"/status reported incompatible contract {reported_contract!r}; expected {expected!r}")

        for route in contract["conformance_routes"]:
            code, body = request(port, route["method"], route["path"])
            if code != route["status"]:
                fail(f"{route['name']} returned HTTP {code}, expected {route['status']}")
            missing = [key for key in route["required"] if key not in body]
            if missing:
                fail(f"{route['name']} missing required keys: {missing}")
        print(f"Electron/backend contract v{expected} passed against {backend.name} in isolated METALSHARP_HOME.")
    finally:
        process.terminate()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
        shutil.rmtree(home, ignore_errors=True)


if __name__ == "__main__":
    main()
