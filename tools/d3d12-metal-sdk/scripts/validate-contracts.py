#!/usr/bin/env python3
"""Validate D3D12 Metal SDK contract files."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

ROOT_DIR = Path(__file__).resolve().parents[3]
DEFAULT_CONTRACT_ROOT = ROOT_DIR / "tools" / "d3d12-metal-sdk" / "contracts"


REQUIRED_CONTRACTS = [
    "d3d12-metal-contract.json",
    "agility-1.619.3-contract.json",
    "feature-support-contract.json",
    "dxgi-contract.json",
    "unsupported-api-ledger.json",
    "risky-stub-ledger.json",
    "contract-waivers.json",
]


def load(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def require(condition: bool, message: str, errors: list[str]) -> None:
    if not condition:
        errors.append(message)


def validate_evidence(path: Path, data: dict[str, Any], errors: list[str]) -> None:
    evidence = data.get("evidence")
    require(isinstance(evidence, list) and len(evidence) > 0, f"{path}: missing evidence list", errors)
    if isinstance(evidence, list):
        for i, entry in enumerate(evidence):
            require(isinstance(entry, dict), f"{path}: evidence[{i}] must be object", errors)
            if isinstance(entry, dict):
                require(bool(entry.get("kind")), f"{path}: evidence[{i}] missing kind", errors)
                require(bool(entry.get("path")), f"{path}: evidence[{i}] missing path", errors)
                require(bool(entry.get("note")), f"{path}: evidence[{i}] missing note", errors)


def validate_ledgers(path: Path, data: dict[str, Any], errors: list[str]) -> None:
    entries = data.get("entries")
    require(isinstance(entries, list) and len(entries) > 0, f"{path}: ledger entries must be non-empty", errors)
    if isinstance(entries, list):
        for i, entry in enumerate(entries):
            require(isinstance(entry, dict), f"{path}: entries[{i}] must be object", errors)
            if isinstance(entry, dict):
                require(bool(entry.get("api")), f"{path}: entries[{i}] missing api", errors)
                require(bool(entry.get("state")), f"{path}: entries[{i}] missing state", errors)
                require(bool(entry.get("reason") or entry.get("risk")), f"{path}: entries[{i}] missing reason/risk", errors)


def validate_waivers(path: Path, data: dict[str, Any], errors: list[str]) -> None:
    waivers = data.get("waivers")
    require(isinstance(waivers, list) and len(waivers) > 0, f"{path}: waivers must be non-empty", errors)
    if isinstance(waivers, list):
        for i, waiver in enumerate(waivers):
            require(isinstance(waiver, dict), f"{path}: waivers[{i}] must be object", errors)
            if isinstance(waiver, dict):
                require(bool(waiver.get("id")), f"{path}: waivers[{i}] missing id", errors)
                require(bool(waiver.get("kind")), f"{path}: waivers[{i}] missing kind", errors)
                require(bool(waiver.get("target")), f"{path}: waivers[{i}] missing target", errors)
                require(bool(waiver.get("status")), f"{path}: waivers[{i}] missing status", errors)
                require(
                    bool(waiver.get("justification")),
                    f"{path}: waivers[{i}] missing justification",
                    errors,
                )
                require(bool(waiver.get("expires_when")), f"{path}: waivers[{i}] missing expires_when", errors)
                evidence = waiver.get("evidence")
                require(
                    isinstance(evidence, list) and len(evidence) > 0,
                    f"{path}: waivers[{i}] evidence must be non-empty",
                    errors,
                )


def validate_reference_contract(path: Path, data: dict[str, Any], errors: list[str]) -> None:
    validate_evidence(path, data, errors)
    require(isinstance(data.get("summary"), dict), f"{path}: missing summary", errors)
    require(isinstance(data.get("data"), dict), f"{path}: missing imported data object", errors)
    summary = data.get("summary", {})
    if isinstance(summary, dict):
        require(summary.get("interface_count", 0) > 0, f"{path}: interface_count must be > 0", errors)
        require(summary.get("method_count", 0) > 0, f"{path}: method_count must be > 0", errors)


def validate_contracts(root: Path) -> list[str]:
    errors: list[str] = []
    for name in REQUIRED_CONTRACTS:
        path = root / name
        require(path.exists(), f"missing required contract: {path}", errors)
        if not path.exists():
            continue
        try:
            data = load(path)
        except json.JSONDecodeError as exc:
            errors.append(f"{path}: invalid JSON: {exc}")
            continue
        require(isinstance(data, dict), f"{path}: top-level JSON must be object", errors)
        if not isinstance(data, dict):
            continue
        require(bool(data.get("schema")), f"{path}: missing schema", errors)
        if name in {"d3d12-metal-contract.json", "agility-1.619.3-contract.json"}:
            validate_reference_contract(path, data, errors)
        elif name in {"unsupported-api-ledger.json", "risky-stub-ledger.json"}:
            validate_ledgers(path, data, errors)
        elif name == "contract-waivers.json":
            validate_waivers(path, data, errors)
        else:
            validate_evidence(path, data, errors)
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=DEFAULT_CONTRACT_ROOT)
    args = parser.parse_args()

    errors = validate_contracts(args.root)
    if errors:
        for error in errors:
            print(f"[FAIL] {error}")
        return 1
    print(f"[PASS] validated {len(REQUIRED_CONTRACTS)} D3D12 Metal SDK contracts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
