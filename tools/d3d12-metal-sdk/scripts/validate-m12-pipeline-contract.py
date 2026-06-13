#!/usr/bin/env python3
"""Validate the M12 D3D12 launch/logging contract against the local tree."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


ROOT_DIR = Path(__file__).resolve().parents[3]
DEFAULT_CONTRACT = (
    ROOT_DIR
    / "tools"
    / "d3d12-metal-sdk"
    / "contracts"
    / "m12-pipeline-contract.json"
)

REQUIRED_ENV_KEYS = {
    "WINEDLLOVERRIDES",
    "WINEDLLPATH",
    "DXMT_WINEMETAL_UNIXLIB",
    "DXMT_LOG_PATH",
    "DXMT_LOG_FILE",
    "METALSHARP_M12_LOG_DIR",
    "METALSHARP_SHADER_CACHE_PATH",
    "DXMT_PIPELINE_CACHE_PATH",
}

REQUIRED_STAGE_IDS = {
    "recipe-and-route",
    "launch-env",
    "runtime-logging",
    "dxgi-entry",
    "d3d12-entry",
    "shader-engine",
    "developer-stress-exe",
}

REQUIRED_GATE_IDS = {
    "pipeline-contract",
    "rust-launch-env",
    "runtime-layout",
    "shader-engine",
    "m12-check",
    "stress-game",
}

NATIVE_LOG_SOURCES = [
    "vendor/dxmt/src/dxgi",
    "vendor/dxmt/src/d3d12",
    "vendor/dxmt/src/airconv",
    "vendor/dxmt/src/winemetal",
    "vendor/dxmt/src/util/log",
]


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        data = json.load(f)
    if not isinstance(data, dict):
        raise ValueError(f"{path}: top-level JSON must be an object")
    return data


def require(condition: bool, message: str, errors: list[str]) -> None:
    if not condition:
        errors.append(message)


def read_rel(path: str, errors: list[str]) -> str:
    target = ROOT_DIR / path
    if not target.exists():
        errors.append(f"missing source path: {path}")
        return ""
    if not target.is_file():
        errors.append(f"source path is not a file: {path}")
        return ""
    return target.read_text(encoding="utf-8", errors="replace")


def validate_contract_shape(data: dict[str, Any], errors: list[str]) -> None:
    require(data.get("schema") == "metalsharp.d3d12.m12-pipeline.v1", "unexpected or missing schema", errors)

    required_env = data.get("required_env")
    require(isinstance(required_env, list) and bool(required_env), "required_env must be a non-empty list", errors)
    if isinstance(required_env, list):
        keys = {entry.get("key") for entry in required_env if isinstance(entry, dict)}
        missing = sorted(REQUIRED_ENV_KEYS - keys)
        require(not missing, f"required_env missing keys: {', '.join(missing)}", errors)
        for entry in required_env:
            if not isinstance(entry, dict):
                errors.append("required_env entries must be objects")
                continue
            require(bool(entry.get("key")), "required_env entry missing key", errors)
            require(bool(entry.get("value_contains")), f"{entry.get('key')}: missing value_contains", errors)
            require(bool(entry.get("proves")), f"{entry.get('key')}: missing proves", errors)

    stages = data.get("pipeline_stages")
    require(isinstance(stages, list) and bool(stages), "pipeline_stages must be a non-empty list", errors)
    if isinstance(stages, list):
        ids = {entry.get("id") for entry in stages if isinstance(entry, dict)}
        missing = sorted(REQUIRED_STAGE_IDS - ids)
        require(not missing, f"pipeline_stages missing ids: {', '.join(missing)}", errors)
        for stage in stages:
            if not isinstance(stage, dict):
                errors.append("pipeline_stages entries must be objects")
                continue
            require(bool(stage.get("id")), "pipeline stage missing id", errors)
            require(bool(stage.get("source")), f"{stage.get('id')}: missing source", errors)
            require(bool(stage.get("flow")), f"{stage.get('id')}: missing flow", errors)
            source = stage.get("source")
            if isinstance(source, str) and source:
                require((ROOT_DIR / source).exists(), f"{stage.get('id')}: source path does not exist: {source}", errors)

    gates = data.get("dev_gates")
    require(isinstance(gates, list) and bool(gates), "dev_gates must be a non-empty list", errors)
    if isinstance(gates, list):
        ids = {entry.get("id") for entry in gates if isinstance(entry, dict)}
        missing = sorted(REQUIRED_GATE_IDS - ids)
        require(not missing, f"dev_gates missing ids: {', '.join(missing)}", errors)
        for gate in gates:
            if not isinstance(gate, dict):
                errors.append("dev_gates entries must be objects")
                continue
            require(bool(gate.get("id")), "dev gate missing id", errors)
            require(bool(gate.get("command")), f"{gate.get('id')}: missing command", errors)
            require(bool(gate.get("pinpoints")), f"{gate.get('id')}: missing pinpoints", errors)


def validate_source_contract(data: dict[str, Any], errors: list[str]) -> None:
    launcher = read_rel("app/src-rust/src/mtsp/launcher.rs", errors)
    engine = read_rel("app/src-rust/src/mtsp/engine.rs", errors)
    log_cpp = read_rel("vendor/dxmt/src/util/log/log.cpp", errors)
    log_hpp = read_rel("vendor/dxmt/src/util/log/log.hpp", errors)

    for pattern in [
        "fn m12_log_env_pairs",
        "METALSHARP_M12_LOG_DIR",
        "DXMT_LOG_PATH",
        "DXMT_LOG_FILE",
        "m12.log",
        "M12 is the D3D12 contract path",
    ]:
        require(pattern in launcher, f"launcher missing `{pattern}`", errors)

    require("M12 is the DXMT D3D12 contract path" in engine, "engine missing M12 contract comment", errors)
    require("DXMT_WINEMETAL_UNIXLIB" in launcher, "launcher missing WineMetal unixlib env", errors)
    require("m12_game_local_env_pairs" in launcher, "launcher missing app-local M12 sidecar env", errors)

    for pattern in ["DXMT_LOG_FILE", "openDiagnosticLog", "std::ios_base::app"]:
        require(pattern in log_cpp + log_hpp, f"DXMT logger missing `{pattern}`", errors)

    for comment in data.get("source_comments", []):
        if not isinstance(comment, dict):
            errors.append("source_comments entries must be objects")
            continue
        path = str(comment.get("path") or "")
        pattern = str(comment.get("must_contain") or "")
        require(bool(path), "source_comments entry missing path", errors)
        require(bool(pattern), f"{path}: source_comments entry missing must_contain", errors)
        if path and pattern:
            require(pattern in read_rel(path, errors), f"{path}: missing source comment `{pattern}`", errors)


def validate_native_log_routing(data: dict[str, Any], errors: list[str]) -> None:
    forbidden = [str(item) for item in data.get("forbidden_native_log_paths", [])]
    require(bool(forbidden), "forbidden_native_log_paths must be non-empty", errors)

    combined_parts: list[str] = []
    for rel in NATIVE_LOG_SOURCES:
        root = ROOT_DIR / rel
        require(root.exists(), f"missing native source root: {rel}", errors)
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if path.suffix in {".c", ".cpp", ".hpp", ".h", ".mm"}:
                combined_parts.append(path.read_text(encoding="utf-8", errors="replace"))
    combined = "\n".join(combined_parts)

    for pattern in forbidden:
        require(pattern not in combined, f"forbidden native log path still present: {pattern}", errors)

    for pattern in [
        'dxmt::openDiagnosticLog("dxmt-dxgi-trace.log")',
        'dxmt::openDiagnosticLog("dxmt-d3d12-trace.log")',
        'dxmt_dxil_open_trace_log("dxmt-dxil-trace.log")',
        'dxmt_airconv_open_trace_log("dxmt-inline-options.log")',
        "winemetal_open_log",
        "bootstrap_log_path",
    ]:
        require(pattern in combined, f"native diagnostic routing missing `{pattern}`", errors)


def validate_evidence(data: dict[str, Any], errors: list[str]) -> None:
    evidence = data.get("evidence")
    require(isinstance(evidence, list) and bool(evidence), "evidence must be a non-empty list", errors)
    if not isinstance(evidence, list):
        return
    for entry in evidence:
        if not isinstance(entry, dict):
            errors.append("evidence entries must be objects")
            continue
        path = entry.get("path")
        require(bool(entry.get("kind")), "evidence entry missing kind", errors)
        require(bool(path), "evidence entry missing path", errors)
        require(bool(entry.get("note")), "evidence entry missing note", errors)
        if isinstance(path, str) and path:
            require((ROOT_DIR / path).exists(), f"evidence path does not exist: {path}", errors)


def validate(data: dict[str, Any]) -> dict[str, Any]:
    errors: list[str] = []
    validate_contract_shape(data, errors)
    validate_source_contract(data, errors)
    validate_native_log_routing(data, errors)
    validate_evidence(data, errors)
    return {
        "schema": "metalsharp.d3d12.m12-pipeline.audit.v1",
        "ok": not errors,
        "summary": {
            "required_env_count": len(data.get("required_env", [])) if isinstance(data.get("required_env"), list) else 0,
            "stage_count": len(data.get("pipeline_stages", [])) if isinstance(data.get("pipeline_stages"), list) else 0,
            "gate_count": len(data.get("dev_gates", [])) if isinstance(data.get("dev_gates"), list) else 0,
            "error_count": len(errors),
        },
        "errors": errors,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--contract", type=Path, default=DEFAULT_CONTRACT)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()

    audit = validate(load_json(args.contract))
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8")

    if audit["ok"]:
        print(
            "[PASS] M12 pipeline contract: "
            f"{audit['summary']['required_env_count']} env keys, "
            f"{audit['summary']['stage_count']} stages, "
            f"{audit['summary']['gate_count']} gates"
        )
        return 0

    for error in audit["errors"]:
        print(f"[FAIL] {error}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
