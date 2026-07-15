#!/usr/bin/env python3
"""Normalize the frozen, language-independent backend route inventory."""
from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONTRACT = ROOT / "contracts" / "electron-backend.v1.json"
def main() -> None:
    contract = json.loads(CONTRACT.read_text())
    routes = {(entry["method"], entry["path"]) for entry in contract["route_inventory"]}
    contract["route_inventory"] = [
        {"method": method, "path": path} for method, path in sorted(routes, key=lambda value: (value[1], value[0]))
    ]
    CONTRACT.write_text(json.dumps(contract, indent=2) + "\n")
    print(f"Wrote {len(contract['route_inventory'])} Electron/backend routes.")


if __name__ == "__main__":
    main()
