#!/usr/bin/env python3
"""Ensure static Electron renderer actions target routes in the C backend contract."""
from __future__ import annotations

import json
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
CONTRACT = ROOT / "contracts" / "electron-backend.v1.json"
RENDERER = ROOT / "app" / "src" / "renderer"

# The renderer's generic `api<T>("METHOD", "/route")` calls may contain
# multiline TypeScript types, so only anchor on the call name and the method /
# route pair. Dynamic route construction is intentionally excluded: it must
# still be represented by a static base route in the contract inventory.
CALL = re.compile(
    r"\b(?:api|contractApi)\s*(?:<[^()]*?>)?\s*\(\s*[\"'](GET|POST)[\"']\s*,\s*[\"']([^\"']+)[\"']",
    re.DOTALL,
)


def fail(message: str) -> None:
    print(f"Electron/C backend binding check failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def route_path(route: str) -> str:
    return route.split("?", 1)[0]


def main() -> None:
    contract = json.loads(CONTRACT.read_text(encoding="utf-8"))
    inventory = {(item["method"], item["path"]) for item in contract["route_inventory"]}
    used: set[tuple[str, str]] = set()

    for source in RENDERER.rglob("*.vue"):
        for method, route in CALL.findall(source.read_text(encoding="utf-8")):
            used.add((method, route_path(route)))

    missing = sorted(used - inventory)
    if missing:
        formatted = ", ".join(f"{method} {path}" for method, path in missing)
        fail(f"renderer actions missing from the C backend route inventory: {formatted}")

    print(f"Electron/C backend binding check passed ({len(used)} static renderer routes).")


if __name__ == "__main__":
    main()
