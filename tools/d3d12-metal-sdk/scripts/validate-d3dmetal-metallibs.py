#!/usr/bin/env python3
"""Validate extracted D3DMetal metallibs with Apple Metal tooling.

This script intentionally reads only offline copied D3DMetal cache artifacts. It
never mutates live GPTK/D3DMetal or M12 shader caches.
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--extract-root", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--sample-per-game", type=int, default=25)
    return parser.parse_args()


def run_objdump(path: Path) -> dict[str, Any]:
    metal_objdump = shutil.which("metal-objdump")
    if metal_objdump is None:
        xcrun = shutil.which("xcrun")
        if xcrun is None:
            return {"ok": False, "error": "neither metal-objdump nor xcrun found"}
        cmd = [xcrun, "metal-objdump", "--private-headers", str(path)]
    else:
        cmd = [metal_objdump, "--private-headers", str(path)]
    proc = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
    stdout = proc.stdout or ""
    ok = proc.returncode == 0 and "file format metallib" in stdout and "MetalLib Header:" in stdout
    header: dict[str, str] = {}
    for line in stdout.splitlines():
        if ":" not in line:
            continue
        key, value = line.split(":", 1)
        key = key.strip()
        value = value.strip()
        if key in {
            "MagicWord", "FileType", "Platform", "PlatformMajor", "FileSize",
            "FunctionListOffset", "FunctionListSize", "ModuleListOffset", "ModuleListSize",
            "ReflectionListOffset", "ReflectionListSize",
        }:
            header[key] = value
    return {
        "ok": ok,
        "returncode": proc.returncode,
        "header": header,
        "stderr": proc.stderr.strip(),
    }


def main() -> int:
    args = parse_args()
    manifest = json.loads(args.manifest.read_text())
    args.out_dir.mkdir(parents=True, exist_ok=True)

    results: dict[str, Any] = {
        "manifest": str(args.manifest),
        "extract_root": str(args.extract_root),
        "sample_per_game": args.sample_per_game,
        "games": {},
    }
    lines = ["# D3DMetal metallib validation", ""]
    lines.append(f"- Manifest: `{args.manifest}`")
    lines.append(f"- Extract root: `{args.extract_root}`")
    lines.append(f"- Sample per game: `{args.sample_per_game}`")
    lines.append("")
    lines.append("| Game | Sampled | Tool-valid | With function names | First failure |")
    lines.append("|---|---:|---:|---:|---|")

    game_items: list[tuple[str, dict[str, Any]]] = []
    raw_games = manifest.get("games", {})
    if isinstance(raw_games, dict):
        game_items = [(str(game), inv) for game, inv in raw_games.items()]
    elif isinstance(raw_games, list):
        for item in raw_games:
            if not isinstance(item, dict):
                continue
            game = str(item.get("game", "unknown"))
            inv = item.get("inventory", item)
            if isinstance(inv, dict):
                game_items.append((game, inv))

    for game, inv in game_items:
        game_root = args.extract_root / game
        records = list(inv.get("unique", []))
        # Prefer stage-cache shader-like entries with recognizable function names,
        # then fall back to any extracted metallib.
        records.sort(key=lambda rec: (
            0 if rec.get("function_names") else 1,
            0 if "MTLGPUFamilyApple9_0/stage_cache.bin" in rec.get("first_source", "") else 1,
            rec.get("sha256", ""),
        ))
        sampled = []
        first_failure = ""
        valid = 0
        with_names = 0
        for rec in records[:args.sample_per_game]:
            sha = rec.get("sha256")
            path = game_root / f"{sha}.metallib"
            entry = {
                "sha256": sha,
                "path": str(path),
                "size": rec.get("size"),
                "first_source": rec.get("first_source"),
                "first_offset": rec.get("first_offset"),
                "function_names": rec.get("function_names", []),
                "exists": path.is_file(),
            }
            if path.is_file():
                tool = run_objdump(path)
                entry["tool"] = tool
                if tool.get("ok"):
                    valid += 1
                elif not first_failure:
                    first_failure = f"{sha}: {tool.get('stderr') or 'objdump failed'}"
            elif not first_failure:
                first_failure = f"{sha}: missing extracted blob"
            if entry["function_names"]:
                with_names += 1
            sampled.append(entry)
        results["games"][game] = {
            "sampled": len(sampled),
            "tool_valid": valid,
            "with_function_names": with_names,
            "first_failure": first_failure,
            "samples": sampled,
        }
        lines.append(f"| {game} | {len(sampled)} | {valid} | {with_names} | {first_failure or '-'} |")

    (args.out_dir / "validation.json").write_text(json.dumps(results, indent=2) + "\n")
    (args.out_dir / "summary.md").write_text("\n".join(lines) + "\n")
    print(args.out_dir / "validation.json")
    print(args.out_dir / "summary.md")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
