#!/usr/bin/env python3
"""Refresh the checked-in route inventory from the backend router source."""
from __future__ import annotations

import json
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
CONTRACT = ROOT / "contracts" / "electron-backend.v1.json"
ROUTER = ROOT / "app" / "src-rust" / "src" / "main.rs"
ROUTE = re.compile(r'\(Method::(Get|Post),\s*"([^"?]+)"\)')


def inventory() -> list[dict[str, str]]:
    return [
        {"method": method.upper(), "path": path}
        for method, path in sorted(set(ROUTE.findall(ROUTER.read_text())), key=lambda value: (value[1], value[0]))
    ]


def main() -> None:
    contract = json.loads(CONTRACT.read_text())
    contract["route_inventory"] = inventory()
    CONTRACT.write_text(json.dumps(contract, indent=2) + "\n")
    print(f"Wrote {len(contract['route_inventory'])} Electron/backend routes.")


if __name__ == "__main__":
    main()
