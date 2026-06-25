#!/usr/bin/env python3
"""Check PE/COFF symbol stack-frame size from objdump disassembly.

Used to keep Wine/Win64 hot-path functions from growing into stack-sensitive
shapes while preserving semantic correctness fixes.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path


def parse_int(value: str) -> int:
    return int(value, 0)


def run_objdump(dll: Path) -> str:
    cmd = ["x86_64-w64-mingw32-objdump", "-d", "-C", str(dll)]
    try:
        return subprocess.run(cmd, text=True, capture_output=True, check=True).stdout
    except FileNotFoundError:
        raise SystemExit("x86_64-w64-mingw32-objdump not found in PATH")
    except subprocess.CalledProcessError as exc:
        sys.stderr.write(exc.stderr)
        raise SystemExit(exc.returncode)


def find_symbol_block(disassembly: str, symbol: str) -> tuple[str, str, int] | None:
    header_re = re.compile(r"^([0-9a-fA-F]+) <([^>]+)>:$", re.MULTILINE)
    matches = list(header_re.finditer(disassembly))
    candidates = []
    for index, match in enumerate(matches):
        demangled = match.group(2)
        if symbol not in demangled:
            continue
        candidates.append((index, match))

    # A substring such as "MTLD3D12SwapChain::Present1" also matches lambdas
    # nested inside Present1. Prefer the real function body unless the caller
    # explicitly asks for a lambda.
    if "lambda" not in symbol:
        non_lambda = [(i, m) for i, m in candidates if "lambda" not in m.group(2)]
        if non_lambda:
            candidates = non_lambda

    for index, match in candidates:
        address = int(match.group(1), 16)
        demangled = match.group(2)
        start = match.start()
        end = matches[index + 1].start() if index + 1 < len(matches) else len(disassembly)
        return demangled, disassembly[start:end], address
    return None


def extract_frame_size(block: str) -> int:
    # Common MinGW x86_64 prologue forms:
    #   sub    $0xb48,%rsp
    #   sub    $0x108,%rsp
    # Leaf/small functions may not allocate a stack frame.
    m = re.search(r"\bsub\s+\$0x([0-9a-fA-F]+),%rsp", block)
    if not m:
        return 0
    return int(m.group(1), 16)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dll", type=Path, help="PE DLL to inspect")
    parser.add_argument("symbol", help="substring of demangled symbol name")
    parser.add_argument("--max-frame", type=parse_int, required=True, help="maximum allowed stack frame, e.g. 0x800")
    parser.add_argument("--json", action="store_true", help="emit JSON summary")
    args = parser.parse_args()

    if not args.dll.exists():
        raise SystemExit(f"DLL not found: {args.dll}")

    disassembly = run_objdump(args.dll)
    found = find_symbol_block(disassembly, args.symbol)
    if not found:
        result = {
            "ok": False,
            "dll": str(args.dll),
            "symbol_query": args.symbol,
            "error": "symbol not found",
        }
        print(json.dumps(result, indent=2) if args.json else f"symbol not found: {args.symbol}")
        return 2

    demangled, block, address = found
    frame_size = extract_frame_size(block)
    ok = frame_size <= args.max_frame
    result = {
        "ok": ok,
        "dll": str(args.dll),
        "symbol_query": args.symbol,
        "symbol": demangled,
        "address": f"0x{address:x}",
        "frame_size": frame_size,
        "frame_size_hex": f"0x{frame_size:x}",
        "max_frame": args.max_frame,
        "max_frame_hex": f"0x{args.max_frame:x}",
    }
    if args.json:
        print(json.dumps(result, indent=2))
    else:
        status = "PASS" if ok else "FAIL"
        print(
            f"{status}: {demangled} frame={frame_size:#x} "
            f"max={args.max_frame:#x} dll={args.dll}"
        )
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
