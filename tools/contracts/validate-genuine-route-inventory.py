#!/usr/bin/env python3
"""Verify every frozen Electron route is registered by the hand-written C backend."""

from __future__ import annotations

import json
import re
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CONTRACT = ROOT / "contracts" / "electron-backend.v1.json"
SOURCES = ROOT / "app" / "src-c" / "runtime" / "genuine"
REGISTER = re.compile(
    r'http_server_register\s*\(\s*[^,]+,\s*"(GET|POST)",\s*"([^"?]+)"',
    re.MULTILINE,
)


def main() -> int:
    contract = json.loads(CONTRACT.read_text())
    expected = {(item["method"], item["path"]) for item in contract["route_inventory"]}
    registrations: list[tuple[str, str]] = []
    for source in sorted(SOURCES.glob("*.c")):
        registrations.extend(REGISTER.findall(source.read_text()))
    actual = set(registrations)
    missing = sorted(expected - actual)
    unexpected = sorted(actual - expected)
    duplicates = sorted((route, count) for route, count in Counter(registrations).items() if count > 1)
    if missing or unexpected:
        for method, path in missing:
            print(f"missing route: {method} {path}")
        for method, path in unexpected:
            print(f"unexpected route: {method} {path}")
        return 1
    print(
        f"Hand-written backend registers all {len(expected)} frozen routes "
        f"({len(duplicates)} intentionally replaced duplicate bindings)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
