#!/usr/bin/env python3
"""Ensure static Electron renderer actions target routes in the C backend contract."""
from __future__ import annotations

import json
from math import prod
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
CONTRACT = ROOT / "contracts" / "electron-backend.v1.json"
RENDERER = ROOT / "app" / "src" / "renderer"
BRIDGE = ROOT / "app" / "src" / "main" / "backend-bridge.ts"
MAIN = ROOT / "app" / "src" / "main" / "index.ts"

# The renderer's generic `api<T>("METHOD", "/route")` calls may contain
# multiline TypeScript types, so only anchor on the call name and the method /
# route pair. Dynamic route construction is intentionally excluded: it must
# still be represented by a static base route in the contract inventory.
CALL = re.compile(
    r"\b(?:api|contractApi)\s*(?:<[^()]*?>)?\s*\(\s*[\"'](GET|POST)[\"']\s*,\s*[\"']([^\"']+)[\"']",
    re.DOTALL,
)
TIMEOUT_ARGUMENT = re.compile(
    r"\b(?:api|contractApi)\s*(?:<[^()]*?>)?\s*\(.*?,\s*(?P<timeout>\d+(?:\s*\*\s*\d+)*)\s*\);",
    re.DOTALL,
)
MAX_TIMEOUT = re.compile(
    r"const\s+MAX_BACKEND_REQUEST_TIMEOUT_MS\s*=\s*(?P<timeout>\d+(?:\s*\*\s*\d+)*)\s*;"
)


def fail(message: str) -> None:
    print(f"Electron/C backend binding check failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def route_path(route: str) -> str:
    return route.split("?", 1)[0]


def evaluate_product(expression: str) -> int:
    return prod(map(int, expression.split("*")))


def main() -> None:
    contract = json.loads(CONTRACT.read_text(encoding="utf-8"))
    inventory = {(item["method"], item["path"]) for item in contract["route_inventory"]}
    used: set[tuple[str, str]] = set()

    renderer_sources = [*RENDERER.rglob("*.vue"), *RENDERER.rglob("*.ts")]
    renderer_text = "\n".join(source.read_text(encoding="utf-8") for source in renderer_sources)
    for source in renderer_sources:
        for method, route in CALL.findall(source.read_text(encoding="utf-8")):
            used.add((method, route_path(route)))

    missing = sorted(used - inventory)
    if missing:
        formatted = ", ".join(f"{method} {path}" for method, path in missing)
        fail(f"renderer actions missing from the C backend route inventory: {formatted}")

    bridge = BRIDGE.read_text(encoding="utf-8")
    main_process = MAIN.read_text(encoding="utf-8")
    max_timeout_match = MAX_TIMEOUT.search(main_process)
    if max_timeout_match is None:
        fail("Electron main process must define MAX_BACKEND_REQUEST_TIMEOUT_MS")
    allowed_timeout = evaluate_product(max_timeout_match.group("timeout"))
    requested_timeouts = [
        evaluate_product(match.group("timeout")) for match in TIMEOUT_ARGUMENT.finditer(renderer_text)
    ]
    longest_requested_timeout = max(requested_timeouts, default=0)
    if longest_requested_timeout > allowed_timeout:
        fail(
            "renderer request timeout exceeds Electron mediation policy: "
            f"requested={longest_requested_timeout} allowed={allowed_timeout}"
        )
    for required in ["randomInt(49152, 65536)", "async requestBinary"]:
        if required not in bridge:
            fail(f"Electron bridge is missing C-backend control-plane hardening: {required}")
    for required in ["protocol.handle(\"metalsharp\"", "isTrustedRenderer", "isAllowedBackendRequest"]:
        if required not in main_process:
            fail(f"Electron main process is missing C-backend request mediation: {required}")
    if "http://127.0.0.1:9274" in main_process:
        fail("Electron main process must not use the predictable C-backend port")

    print(f"Electron/C backend binding and control-plane check passed ({len(used)} static renderer routes).")


if __name__ == "__main__":
    main()
