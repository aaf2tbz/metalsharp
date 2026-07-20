#!/usr/bin/env python3
"""Validate `configs/mtsp-rules.toml` is well-formed.

This is a lightweight validator that runs anywhere Python + `toml` is available,
without needing the Rust toolchain. It checks:

1. The TOML parses.
2. No duplicate `[overrides.APPID]` top-level sections.
3. Every override has a non-empty `name` string and a `pipeline` string.
4. Every `pipeline` value is one of the known pipeline ids.
5. Sub-tables (e.g. `[overrides.APPID.diagnostics]`) only use the keys we expect
   (`dependencies`, `env`, `diagnostics`) — anything else is a typo.
6. The default M12 rule is the proven Elden Ring dependency/diagnostic surface
   plus the generic direct-executable DXMT M12 launch contract.

Exit code 0 on success, 1 on any validation error.
"""

from __future__ import annotations

import re
import sys
from collections import Counter
from pathlib import Path

try:
    import toml  # type: ignore
except ImportError:
    print(
        "error: Python module `toml` is not installed. Install it with:\n"
        "  pip3 install --user toml",
        file=sys.stderr,
    )
    sys.exit(2)

ROOT = Path(__file__).resolve().parents[2]
RULES_PATH = ROOT / "configs" / "mtsp-rules.toml"

VALID_PIPELINES = {
    "m9",
    "m10",
    "m10_32",
    "m11",
    "m11_32",
    "m12",
    "dxmt",
    "fna_arm64",
    "fna_x86",
    "wine_bare",
    "steam",
    "mac_steam",
    "mac_native",
    "wine_managed",
    "managed",
}

VALID_SUB_KEYS = {"dependencies", "env", "diagnostics", "exe_args"}

M12_LAUNCH_CONTRACT = {
    "pipeline": "m12",
    "wine_binary": "bin/metalsharp-wine",
    "graphics_backend": "dxmt",
    "runtime_lane": "dxmt_m12",
    "winemetal_unixlib": "winemetal.so",
    "windows_dll_path": "lib/dxmt_m12/x86_64-windows",
    "unix_library_path": "lib/dxmt_m12/x86_64-unix:lib/wine/x86_64-unix",
    "dll_overrides": (
        "winemetal,d3d12,dxgi,dxgi_dxmt,d3d11,d3d10core=n,b;"
        "gameoverlayrenderer,gameoverlayrenderer64=d"
    ),
    "steam_client": "background",
    "launch_mode": "direct_executable",
}


def fail(errors: list[str]) -> None:
    print(
        f"mtsp-rules TOML validation FAILED ({len(errors)} error(s)):",
        file=sys.stderr,
    )
    for err in errors[:50]:
        print(f"  - {err}", file=sys.stderr)
    if len(errors) > 50:
        print(f"  ... ({len(errors) - 50} more)", file=sys.stderr)
    sys.exit(1)


def main() -> int:
    if not RULES_PATH.exists():
        print(f"error: rules file not found: {RULES_PATH}", file=sys.stderr)
        return 1

    raw = RULES_PATH.read_text()
    errors: list[str] = []

    # 1. Parse the TOML.
    try:
        data = toml.loads(raw)
    except Exception as exc:
        fail([f"TOML parse error: {exc}"])
        return 1

    overrides = data.get("overrides", {})
    pipeline_defaults = data.get("pipeline_defaults", {})

    # 2. No duplicate `[overrides.APPID]` top-level sections.
    #    The `toml` parser already raises on duplicates, but we also do a
    #    line-scan so the error message is clear if a future parser silently
    #    keeps the last definition.
    section_re = re.compile(r"^\[overrides\.(\d+)\]\s*$")
    section_counts: Counter[int] = Counter()
    for line in raw.splitlines():
        m = section_re.match(line)
        if m:
            section_counts[int(m.group(1))] += 1
    duplicates = sorted(
        appid for appid, count in section_counts.items() if count > 1
    )
    for appid in duplicates:
        errors.append(
            f"[overrides.{appid}] defined {section_counts[appid]} times"
        )
    if duplicates:
        # Skip the per-entry checks — the parsed map only has one of the
        # duplicates, so iterating it would give a misleading "OK" report.
        fail(errors)
        return 1

    # 3 & 4. Every override has name + pipeline; pipeline is valid.
    for appid, rule in overrides.items():
        if not isinstance(rule, dict):
            errors.append(f"[overrides.{appid}] is not a table")
            continue
        name = rule.get("name", "")
        pipeline = rule.get("pipeline", "")
        if not isinstance(name, str) or not name.strip():
            errors.append(f"[overrides.{appid}] missing or empty `name`")
        if not isinstance(pipeline, str) or not pipeline:
            errors.append(f"[overrides.{appid}] missing `pipeline`")
        elif pipeline not in VALID_PIPELINES:
            errors.append(
                f"[overrides.{appid}] unknown pipeline {pipeline!r}"
            )

    # 5. Sub-table header check — only `[overrides.NNNN.{section}]` where
    #    `section` is in VALID_SUB_KEYS is allowed. Anything else is a typo
    #    (e.g. `[overrides.123.deps]` instead of `.dependencies`).
    sub_re = re.compile(r"^\[overrides\.(\d+)\.([a-z_]+)\]\s*$")
    seen_sub_keys: dict[int, set[str]] = {}
    for line in raw.splitlines():
        m = sub_re.match(line)
        if m:
            appid = int(m.group(1))
            key = m.group(2)
            if key not in VALID_SUB_KEYS:
                errors.append(
                    f"[overrides.{appid}.{key}] is not a recognized sub-table"
                )
            seen_sub_keys.setdefault(appid, set()).add(key)

    # 5b. Per-game `exe_args` (top-level list of non-empty strings) — used by
    #     the C launcher to pass engine-specific flags (e.g. `-force-glcore`,
    #     `-gl`, `+set`). Must reject shell metacharacters so the runtime
    #     cannot be tricked into evaluating untrusted input.
    exe_args_disallowed = set(";&|`$'\"\n\r")
    for appid, rule in overrides.items():
        if not isinstance(rule, dict):
            continue
        exe_args = rule.get("exe_args", [])
        if isinstance(exe_args, str) or not isinstance(exe_args, list):
            errors.append(
                f"[overrides.{appid}] `exe_args` must be a list of strings"
            )
            continue
        if not exe_args:
            continue
        for index, entry in enumerate(exe_args):
            if not isinstance(entry, str):
                errors.append(
                    f"[overrides.{appid}] `exe_args[{index}]` must be a string"
                )
                continue
            if entry == "":
                errors.append(
                    f"[overrides.{appid}] `exe_args[{index}]` must be non-empty"
                )
                continue
            bad = [c for c in entry if c in exe_args_disallowed or ord(c) < 0x20]
            if bad:
                errors.append(
                    f"[overrides.{appid}] `exe_args[{index}]` contains "
                    f"disallowed characters: {bad!r}"
                )

    # 6. A user-selected M12 bottle must inherit Elden Ring's proven runtime
    #    surface without inheriting its app-specific EAC executable mapping.
    m12_default = pipeline_defaults.get("m12", {})
    if not isinstance(m12_default, dict):
        errors.append("[pipeline_defaults.m12] is not a table")
    else:
        for key, expected in M12_LAUNCH_CONTRACT.items():
            if m12_default.get(key) != expected:
                errors.append(
                    f"[pipeline_defaults.m12] `{key}` must be {expected!r}"
                )

        elden = overrides.get("1245620", overrides.get(1245620, {}))
        for section, key in (
            ("dependencies", "components"),
            ("diagnostics", "check_dlls"),
        ):
            default_values = m12_default.get(section, {}).get(key)
            elden_values = elden.get(section, {}).get(key)
            if default_values != elden_values:
                errors.append(
                    f"[pipeline_defaults.m12.{section}] `{key}` must match "
                    f"[overrides.1245620.{section}]"
                )

        if "exe_names" in m12_default:
            errors.append(
                "[pipeline_defaults.m12] must not inherit Elden Ring's EAC executable mapping"
            )

    if errors:
        fail(errors)

    print(
        f"mtsp-rules TOML OK: {len(overrides)} override entries, "
        f"{len(section_counts)} top-level sections, "
        f"{sum(len(v) for v in seen_sub_keys.values())} sub-tables; "
        "default M12 contract matches Elden Ring."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
