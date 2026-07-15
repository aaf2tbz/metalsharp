#!/usr/bin/env python3
"""Validate the versioned Electron/backend contract without a language runtime."""
from __future__ import annotations

import json
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
CONTRACT = ROOT / "contracts" / "electron-backend.v1.json"
ROUTER = ROOT / "app" / "src-rust" / "src" / "main.rs"


def fail(message: str) -> None:
    print(f"contract validation failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> None:
    try:
        contract = json.loads(CONTRACT.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        fail(str(exc))

    if contract.get("contract_version") != "1":
        fail("contract_version must be the string '1'")
    status = contract.get("status")
    if not isinstance(status, dict) or status.get("path") != "/status" or status.get("method") != "GET":
        fail("status must describe GET /status")
    if not status.get("legacy_versions", {}).get("0.54.5") == "1":
        fail("the shipped 0.54.5 C backend must remain explicitly mapped to contract v1")

    names: set[str] = set()
    for route in contract.get("conformance_routes", []):
        if not isinstance(route, dict):
            fail("conformance route must be an object")
        name = route.get("name")
        if not isinstance(name, str) or name in names:
            fail(f"conformance route has missing or duplicate name: {name!r}")
        names.add(name)
        if route.get("method") not in {"GET", "POST"} or not str(route.get("path", "")).startswith("/"):
            fail(f"invalid method or path for {name}")
        if route.get("status") != 200 or not route.get("required"):
            fail(f"{name} needs an explicit successful status and response shape")

    required = {"setup", "steam-status", "steam-library", "bottles", "logs", "log-stream", "pipeline-dry-run", "m12-dry-run"}
    if names != required:
        fail(f"conformance coverage mismatch: expected {sorted(required)}, got {sorted(names)}")

    typed = contract.get("typed_routes", {})
    if "POST /game/launch-auto" not in typed or not typed["POST /game/launch-auto"].get("process_launch"):
        fail("launch-auto must remain a typed process-launch route")

    expected_inventory = [
        {"method": method.upper(), "path": path}
        for method, path in sorted(
            set(re.findall(r'\(Method::(Get|Post),\s*"([^"?]+)"\)', ROUTER.read_text())),
            key=lambda value: (value[1], value[0]),
        )
    ]
    if contract.get("route_inventory") != expected_inventory:
        fail("route_inventory is stale; run tools/contracts/generate-electron-backend-route-inventory.py")
    print("Electron/backend contract v1 is valid.")


if __name__ == "__main__":
    main()
