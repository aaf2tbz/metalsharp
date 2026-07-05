#!/usr/bin/env python3
"""Guide and stage a user-provided GPTK4 D3DMetal payload for MetalSharp.

This helper is the user-facing wrapper around MetalSharp's offline staging
contract. It does not bundle or download Apple payloads itself; instead it opens
Apple's download page, discovers local GPTK downloads, and invokes the staging
and verification tools once the user has a GPTK .dmg or extracted directory.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path

DOWNLOAD_SEARCH_URL = "https://developer.apple.com/download/all/?q=Game%20Porting%20Toolkit"


def metalsharp_home() -> Path:
    override = os.environ.get("METALSHARP_HOME", "").strip()
    return Path(override).expanduser() if override else Path.home() / ".metalsharp"


DEFAULT_RUNTIME_ROOT = metalsharp_home() / "runtime" / "wine"
TOOL_DIR = Path(__file__).resolve().parent
STAGE_TOOL = TOOL_DIR / "stage-d3dmetal-native-payload.py"
CHECK_TOOL = TOOL_DIR / "check-d3dmetal-native-payload.py"

CANDIDATE_PATTERNS = (
    "*Game*Porting*Toolkit*.dmg",
    "*game*porting*toolkit*.dmg",
    "*GPTK*.dmg",
    "*gptk*.dmg",
    "*Game*Porting*Toolkit*",
    "*game*porting*toolkit*",
    "*GPTK*",
    "*gptk*",
)


def run(args: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=check)


def open_download_page() -> None:
    if sys.platform == "darwin":
        subprocess.run(["open", DOWNLOAD_SEARCH_URL], check=False)
    else:
        print(DOWNLOAD_SEARCH_URL)


def looks_like_gptk_source(path: Path) -> bool:
    if path.is_file() and path.suffix.lower() == ".dmg":
        return True
    if path.is_dir():
        if (path / "redist" / "lib" / "wine").is_dir() and (path / "redist" / "lib" / "external").is_dir():
            return True
        if (path / "lib" / "wine").is_dir() and (path / "lib" / "external").is_dir():
            return True
    return False


def scan_candidates(search_dirs: list[Path]) -> list[Path]:
    found: dict[Path, float] = {}
    for root in search_dirs:
        if not root.exists():
            continue
        for pattern in CANDIDATE_PATTERNS:
            for candidate in root.glob(pattern):
                if looks_like_gptk_source(candidate):
                    try:
                        found[candidate.resolve()] = candidate.stat().st_mtime
                    except OSError:
                        pass
    return [p for p, _ in sorted(found.items(), key=lambda item: item[1], reverse=True)]


def verify(runtime_root: Path, *, json_output: bool = False) -> int:
    cmd = [sys.executable, str(CHECK_TOOL), "--runtime-root", str(runtime_root)]
    if json_output:
        cmd.append("--json")
    result = subprocess.run(cmd)
    return result.returncode


def stage(source: Path, runtime_root: Path, *, force: bool = False) -> int:
    cmd = [sys.executable, str(STAGE_TOOL), str(source), "--runtime-root", str(runtime_root)]
    if force:
        cmd.append("--force")
    result = subprocess.run(cmd)
    if result.returncode != 0:
        return result.returncode
    return verify(runtime_root, json_output=False)


def write_receipt(runtime_root: Path, source: Path, status: str) -> None:
    receipt_dir = runtime_root / "lib" / "d3dmetal_native"
    receipt_dir.mkdir(parents=True, exist_ok=True)
    receipt = {
        "route_id": "d3dmetal_native",
        "provider": "apple_gptk_user_staged",
        "source": str(source),
        "status": status,
        "timestamp": int(time.time()),
        "note": "Payload staged from a user/developer-provided GPTK source; MetalSharp did not download or redistribute Apple binaries.",
    }
    (receipt_dir / "metalsharp-staging-receipt.json").write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Open Apple GPTK download page, discover local GPTK downloads, stage D3DMetal payload, and verify MetalSharp readiness.",
    )
    parser.add_argument("--runtime-root", type=Path, default=DEFAULT_RUNTIME_ROOT, help="Installed MetalSharp Wine runtime root")
    parser.add_argument("--source", type=Path, help="GPTK .dmg or extracted directory to stage")
    parser.add_argument("--open-download-page", action="store_true", help="Open Apple's Game Porting Toolkit download/search page")
    parser.add_argument("--scan", action="store_true", help="List likely local GPTK downloads")
    parser.add_argument("--stage-latest", action="store_true", help="Stage the newest likely local GPTK download found in search dirs")
    parser.add_argument("--search-dir", type=Path, action="append", help="Directory to scan; defaults to ~/Downloads and ~/Desktop")
    parser.add_argument("--force", action="store_true", help="Overwrite an existing staged payload")
    parser.add_argument("--verify", action="store_true", help="Only run the payload verifier")
    parser.add_argument("--json", action="store_true", help="Emit verifier JSON when used with --verify")
    args = parser.parse_args()

    if not STAGE_TOOL.exists() or not CHECK_TOOL.exists():
        print(f"missing staging/check tools in {TOOL_DIR}", file=sys.stderr)
        return 2

    search_dirs = args.search_dir or [Path.home() / "Downloads", Path.home() / "Desktop"]

    if args.open_download_page:
        print(f"Opening Apple download search: {DOWNLOAD_SEARCH_URL}")
        open_download_page()

    if args.verify:
        return verify(args.runtime_root, json_output=args.json)

    if args.scan or args.stage_latest:
        candidates = scan_candidates(search_dirs)
        if not candidates:
            print("No likely GPTK downloads found.")
            print("Download GPTK from Apple's page, then rerun with --scan or --source /path/to/GPTK.dmg")
            return 1 if args.stage_latest else 0
        for idx, candidate in enumerate(candidates, 1):
            print(f"{idx}. {candidate}")
        if args.stage_latest:
            source = candidates[0]
            print(f"Staging newest candidate: {source}")
            rc = stage(source, args.runtime_root, force=args.force)
            write_receipt(args.runtime_root, source, "ready" if rc == 0 else "failed")
            return rc

    if args.source:
        source = args.source.expanduser().resolve()
        if not looks_like_gptk_source(source):
            print(f"Source does not look like a GPTK dmg/extracted redist tree: {source}", file=sys.stderr)
            return 2
        rc = stage(source, args.runtime_root, force=args.force)
        write_receipt(args.runtime_root, source, "ready" if rc == 0 else "failed")
        return rc

    if not (args.open_download_page or args.scan):
        parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
