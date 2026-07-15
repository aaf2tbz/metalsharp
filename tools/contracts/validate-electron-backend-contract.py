#!/usr/bin/env python3
"""Validate the versioned Electron/backend contract without a language runtime."""
from __future__ import annotations

import json
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
CONTRACT = ROOT / "contracts" / "electron-backend.v1.json"
ELECTRON_CONTRACT = ROOT / "app" / "src" / "shared" / "backend-contract.ts"


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
    for version in ("0.54.5", "0.55.0", "0.55.1"):
        if status.get("legacy_versions", {}).get(version) != "1":
            fail(f"the shipped {version} C backend must remain explicitly mapped to contract v1")

    try:
        electron_source = ELECTRON_CONTRACT.read_text()
    except OSError as exc:
        fail(str(exc))
    legacy_map = re.search(
        r"LEGACY_CONTRACT_BY_BACKEND_VERSION[^=]*=\s*\{(?P<body>.*?)\};",
        electron_source,
        re.DOTALL,
    )
    if legacy_map is None:
        fail("Electron legacy contract map is missing")
    electron_versions = set(
        re.findall(r'"([^"]+)"\s*:\s*BACKEND_CONTRACT_VERSION', legacy_map.group("body"))
    )
    contract_versions = set(status.get("legacy_versions", {}))
    if electron_versions != contract_versions:
        fail(
            "Electron legacy contract map does not match the wire contract: "
            f"Electron={sorted(electron_versions)}, contract={sorted(contract_versions)}"
        )

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

    inventory = contract.get("route_inventory")
    if not isinstance(inventory, list) or len(inventory) != 264:
        fail("route_inventory must contain the 264 frozen C-backend routes")
    inventory_keys = [(route.get("path"), route.get("method")) for route in inventory if isinstance(route, dict)]
    if len(inventory_keys) != len(inventory) or len(set(inventory_keys)) != len(inventory):
        fail("route_inventory contains invalid or duplicate entries")
    if inventory_keys != sorted(inventory_keys):
        fail("route_inventory must remain sorted by path and method")
    print("Electron/backend contract v1 is valid.")


if __name__ == "__main__":
    main()
