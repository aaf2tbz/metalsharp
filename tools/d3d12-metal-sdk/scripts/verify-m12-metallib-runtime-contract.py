#!/usr/bin/env python3
"""Verify DXMT's D3D12 DXIL shader path keeps compiled Metal artifacts first.

This is a source-contract gate, not a formatter.  It intentionally checks the
control-flow shape around CompileShader's DXIL fallback:

  probe cache policy / paths
  try opening <hash>.metallib
  if (!mf) { cached .msl source fallback, then DXIL->MSL lowering }
  else/use-mf { m12core M12CORE_SHADER_FUNCTION_INPUT_METALLIB, then WMT fallback }

The metallib success block is textually after the !mf fallback block, so simple
line-order checks are insufficient; this script verifies branch containment.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
from dataclasses import dataclass
from typing import Any


@dataclass
class Check:
    id: str
    ok: bool
    detail: str
    line: int | None = None


def line_of(text: str, idx: int | None) -> int | None:
    if idx is None or idx < 0:
        return None
    return text.count("\n", 0, idx) + 1


def find_after(text: str, needle: str, start: int = 0) -> int:
    return text.find(needle, start)


def block_end(text: str, open_brace_idx: int) -> int | None:
    depth = 0
    i = open_brace_idx
    in_line_comment = False
    in_block_comment = False
    in_string = False
    in_char = False
    escape = False
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if in_line_comment:
            if ch == "\n":
                in_line_comment = False
        elif in_block_comment:
            if ch == "*" and nxt == "/":
                in_block_comment = False
                i += 1
        elif in_string:
            if escape:
                escape = False
            elif ch == "\\":
                escape = True
            elif ch == '"':
                in_string = False
        elif in_char:
            if escape:
                escape = False
            elif ch == "\\":
                escape = True
            elif ch == "'":
                in_char = False
        else:
            if ch == "/" and nxt == "/":
                in_line_comment = True
                i += 1
            elif ch == "/" and nxt == "*":
                in_block_comment = True
                i += 1
            elif ch == '"':
                in_string = True
            elif ch == "'":
                in_char = True
            elif ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return i + 1
        i += 1
    return None


def verify(path: pathlib.Path) -> tuple[list[Check], dict[str, Any]]:
    text = path.read_text()
    checks: list[Check] = []

    probe_idx = find_after(text, "WMTM12CoreProbeShaderCache")
    metallib_open_idx = find_after(text, 'mf = fopen(metallib_path, "rb")', probe_idx if probe_idx >= 0 else 0)
    fallback_if_idx = find_after(text, "if (!mf) {", metallib_open_idx if metallib_open_idx >= 0 else 0)
    fallback_open = text.find("{", fallback_if_idx) if fallback_if_idx >= 0 else -1
    fallback_end = block_end(text, fallback_open) if fallback_open >= 0 else None
    fallback_block = text[fallback_if_idx:fallback_end] if fallback_end else ""
    hit_region = text[fallback_end:] if fallback_end else ""

    checks.append(Check(
        "cache-lookup-before-metallib-open",
        probe_idx >= 0 and metallib_open_idx > probe_idx,
        "WMTM12CoreProbeShaderCache precedes the .metallib fopen policy/open",
        line_of(text, probe_idx),
    ))
    checks.append(Check(
        "metallib-open-before-fallback-branch",
        metallib_open_idx >= 0 and fallback_if_idx > metallib_open_idx,
        "DXMT attempts to open <hash>.metallib before entering the !mf fallback branch",
        line_of(text, metallib_open_idx),
    ))
    checks.append(Check(
        "fallback-branch-parsed",
        fallback_end is not None and fallback_end > fallback_if_idx,
        "The if (!mf) fallback block is parseable by brace matching",
        line_of(text, fallback_if_idx),
    ))

    pre_fallback = text[metallib_open_idx:fallback_if_idx] if metallib_open_idx >= 0 and fallback_if_idx >= 0 else ""
    checks.append(Check(
        "no-msl-source-before-fallback-guard",
        "M12CORE_SHADER_FUNCTION_INPUT_MSL_SOURCE" not in pre_fallback and "ReadShaderText(msl_path" not in pre_fallback,
        "Cached .msl source path is not reachable until metallib open fails (!mf)",
        line_of(text, fallback_if_idx),
    ))
    checks.append(Check(
        "no-dxil-lowering-before-fallback-guard",
        "DXILContainer::parse" not in pre_fallback and "LowerDXILToMSLWithM12Core" not in pre_fallback,
        "DXIL parsing/lowering is not reachable until metallib open fails (!mf)",
        line_of(text, fallback_if_idx),
    ))

    checks.append(Check(
        "cached-msl-inside-fallback",
        "ReadShaderText(msl_path" in fallback_block and "M12CORE_SHADER_FUNCTION_INPUT_MSL_SOURCE" in fallback_block,
        "Cached .msl source reuse is contained inside the !mf fallback branch",
        line_of(text, fallback_block.find("ReadShaderText(msl_path") + fallback_if_idx) if "ReadShaderText(msl_path" in fallback_block else None,
    ))
    checks.append(Check(
        "dxil-lowering-inside-fallback",
        "DXILContainer::parse" in fallback_block and "LowerDXILToMSLWithM12Core" in fallback_block,
        "DXIL parsing/lowering is contained inside the !mf fallback branch",
        line_of(text, fallback_block.find("DXILContainer::parse") + fallback_if_idx) if "DXILContainer::parse" in fallback_block else None,
    ))

    metallib_input_rel = hit_region.find("M12CORE_SHADER_FUNCTION_INPUT_METALLIB")
    wmt_fallback_rel = hit_region.find("wmt_device.newLibrary(dispatch_data, err)")
    checks.append(Check(
        "m12core-metallib-before-wmt-fallback",
        metallib_input_rel >= 0 and wmt_fallback_rel > metallib_input_rel,
        "On metallib hit, DXMT calls M12Core with M12CORE_SHADER_FUNCTION_INPUT_METALLIB before WMT newLibrary fallback",
        line_of(text, (fallback_end or 0) + metallib_input_rel) if metallib_input_rel >= 0 else None,
    ))
    checks.append(Check(
        "metallib-hit-records-counter",
        "g_shader_metallib_cache_hits" in hit_region and "M12CORE_COUNTER_SHADER_METALLIB_CACHE_HITS" in hit_region,
        "Metallib-hit path records hit counters before loading the library",
        line_of(text, (fallback_end or 0) + hit_region.find("g_shader_metallib_cache_hits")) if "g_shader_metallib_cache_hits" in hit_region else None,
    ))

    details = {
        "source": str(path),
        "indices": {
            "cache_lookup_line": line_of(text, probe_idx),
            "metallib_open_line": line_of(text, metallib_open_idx),
            "fallback_branch_line": line_of(text, fallback_if_idx),
            "fallback_end_line": line_of(text, fallback_end),
            "m12core_metallib_input_line": line_of(text, (fallback_end or 0) + metallib_input_rel) if metallib_input_rel >= 0 else None,
            "wmt_metallib_fallback_line": line_of(text, (fallback_end or 0) + wmt_fallback_rel) if wmt_fallback_rel >= 0 else None,
        },
    }
    return checks, details


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--source",
        default="vendor/dxmt/src/d3d12/d3d12_pipeline_state.cpp",
        help="Path to d3d12_pipeline_state.cpp",
    )
    ap.add_argument("--json", dest="json_path", help="Optional JSON report path")
    args = ap.parse_args()

    path = pathlib.Path(args.source)
    checks, details = verify(path)
    ok = all(c.ok for c in checks)
    report = {
        "ok": ok,
        **details,
        "checks": [c.__dict__ for c in checks],
    }
    if args.json_path:
        out = pathlib.Path(args.json_path)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(report, indent=2) + "\n")
    for c in checks:
        status = "ok" if c.ok else "FAIL"
        loc = f":{c.line}" if c.line else ""
        print(f"{status} {c.id}{loc} - {c.detail}")
    print(f"summary ok={str(ok).lower()} checks={len(checks)}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
