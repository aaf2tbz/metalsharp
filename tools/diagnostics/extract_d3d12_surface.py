#!/usr/bin/env python3
"""Extract a derived D3D/DXGI compatibility surface from local game installs.

This is intentionally a metadata scanner. It records file layout, PE string
markers, Unity shader container counts, and managed Unity callsite snippets
without copying game binaries or assets into the report.
"""

from __future__ import annotations

import argparse
import datetime as _dt
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


DEFAULT_GAMES = {
    "Sons Of The Forest": Path("/Volumes/AverySSD/SteamLibrary/steamapps/common/Sons Of The Forest"),
    "Subnautica: Below Zero": Path("/Volumes/AverySSD/SteamLibrary/steamapps/common/SubnauticaZero"),
    "ARMORED CORE VI FIRES OF RUBICON": Path(
        "/Volumes/AverySSD/SteamLibrary/steamapps/common/ARMORED CORE VI FIRES OF RUBICON"
    ),
    "ELDEN RING": Path("/Volumes/AverySSD/SteamLibrary/steamapps/common/ELDEN RING"),
    "Ghostrunner": Path("/Volumes/AverySSD/SteamLibrary/steamapps/common/Ghostrunner"),
}

PE_EXTENSIONS = {".exe", ".dll", ".so"}
ASSET_EXTENSIONS = {".assets", ".resource", ".resS"}
STRING_MARKERS = [
    ("D3D12", re.compile(r"D3D12", re.IGNORECASE)),
    ("D3D11", re.compile(r"D3D11", re.IGNORECASE)),
    ("DXGI", re.compile(r"DXGI", re.IGNORECASE)),
    ("D3DCompile", re.compile(r"D3DCompile", re.IGNORECASE)),
    ("D3D12CreateDevice", re.compile(r"D3D12CreateDevice", re.IGNORECASE)),
    ("CreateDXGIFactory", re.compile(r"CreateDXGIFactory", re.IGNORECASE)),
    ("IDXGI", re.compile(r"IDXGI", re.IGNORECASE)),
    ("NvAPI", re.compile(r"NvAPI", re.IGNORECASE)),
    ("AGS", re.compile(r"(?:amd_ags|AmdD3D)", re.IGNORECASE)),
    ("EasyAntiCheat", re.compile(r"EasyAntiCheat", re.IGNORECASE)),
    ("EAC", re.compile(r"(?:\bEAC\b|EasyAntiCheat)", re.IGNORECASE)),
    ("EOS", re.compile(r"(?:\bEOS\b|EasyAntiCheat_EOS|EOSSDK|eossdk)", re.IGNORECASE)),
    ("eossdk", re.compile(r"eossdk", re.IGNORECASE)),
    ("steam_api", re.compile(r"steam_api", re.IGNORECASE)),
    ("d3dcompiler", re.compile(r"d3dcompiler", re.IGNORECASE)),
    ("dxcompiler", re.compile(r"dxcompiler", re.IGNORECASE)),
    ("dxil", re.compile(r"dxil", re.IGNORECASE)),
    ("DXBC", re.compile(r"DXBC", re.IGNORECASE)),
]
MANAGED_TERMS = [
    "ComputeShader",
    "FindKernel",
    "Dispatch",
    "SystemInfo",
    "graphicsDeviceType",
    "graphicsMemorySize",
    "Direct3D",
    "D3D",
    "QualitySettings",
    "GraphicsSettings",
    "ScreenSpace",
    "DX",
]
KERNEL_RE = re.compile(rb"\b(?:K[A-Za-z0-9_]{3,}|ClearToBlack|DepthPyramid|Crest|FindKernel)\b")
SHADER_RE = re.compile(rb"(?:Hidden|Crest|HDRP|Universal Render Pipeline|Legacy Shaders|Nature|Sprites)/[A-Za-z0-9_ ./()-]+")


def run_text(args: list[str], *, timeout: int = 20) -> str:
    try:
        return subprocess.run(args, check=False, capture_output=True, text=True, timeout=timeout).stdout
    except (OSError, subprocess.SubprocessError):
        return ""


def rel(root: Path, path: Path) -> str:
    try:
        return str(path.relative_to(root))
    except ValueError:
        return str(path)


def count_bytes(path: Path, needle: bytes) -> int:
    count = 0
    try:
        with path.open("rb") as handle:
            while chunk := handle.read(1024 * 1024):
                count += chunk.count(needle)
    except OSError:
        return 0
    return count


def binary_terms(path: Path, pattern: re.Pattern[bytes], limit: int = 60) -> list[str]:
    found: set[str] = set()
    try:
        data = path.read_bytes()
    except OSError:
        return []
    for match in pattern.finditer(data):
        value = match.group(0).decode("utf-8", errors="ignore").strip()
        if value:
            found.add(value)
        if len(found) >= limit:
            break
    return sorted(found)


def walk_files(root: Path) -> list[Path]:
    paths: list[Path] = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [name for name in dirnames if name not in {".git", "__MACOSX"}]
        for filename in filenames:
            paths.append(Path(dirpath) / filename)
    return paths


def scan_pe_file(path: Path) -> dict[str, object]:
    strings = run_text(["strings", "-a", str(path)], timeout=30)
    markers = sorted({label for label, pattern in STRING_MARKERS if pattern.search(strings)})
    interesting_lines = []
    for line in strings.splitlines():
        if any(pattern.search(line) for _, pattern in STRING_MARKERS):
            clean = line.strip()
            if clean and clean not in interesting_lines:
                interesting_lines.append(clean)
            if len(interesting_lines) >= 30:
                break
    file_type = run_text(["file", "-b", str(path)], timeout=10).strip()
    return {
        "fileType": file_type,
        "markers": markers,
        "lines": interesting_lines,
    }


def scan_unity_assets(root: Path, files: list[Path]) -> dict[str, object]:
    dxbc_by_file: list[tuple[str, int]] = []
    dxil_by_file: list[tuple[str, int]] = []
    kernels: set[str] = set()
    shader_names: set[str] = set()

    for path in files:
        if path.suffix not in ASSET_EXTENSIONS:
            continue
        dxbc_count = count_bytes(path, b"DXBC")
        dxil_count = count_bytes(path, b"DXIL")
        if dxbc_count:
            dxbc_by_file.append((rel(root, path), dxbc_count))
            kernels.update(binary_terms(path, KERNEL_RE, limit=120))
            shader_names.update(binary_terms(path, SHADER_RE, limit=120))
        if dxil_count:
            dxil_by_file.append((rel(root, path), dxil_count))

    return {
        "dxbc": sorted(dxbc_by_file, key=lambda item: item[1], reverse=True),
        "dxil": sorted(dxil_by_file, key=lambda item: item[1], reverse=True),
        "kernels": sorted(kernels)[:80],
        "shaderNames": sorted(shader_names)[:80],
    }


def scan_managed(root: Path) -> list[dict[str, object]]:
    ilspy = shutil.which("ilspycmd") or str(Path.home() / ".dotnet" / "tools" / "ilspycmd")
    if not Path(ilspy).exists():
        return []

    managed_dirs = [path for path in root.glob("*_Data/Managed") if path.is_dir()]
    results: list[dict[str, object]] = []
    for managed_dir in managed_dirs:
        assemblies = [
            managed_dir / "Assembly-CSharp.dll",
            managed_dir / "Assembly-CSharp-firstpass.dll",
        ]
        for assembly in assemblies:
            if not assembly.exists():
                continue
            with tempfile.TemporaryDirectory(prefix="metalsharp-ilspy-") as temp_dir:
                out_dir = Path(temp_dir) / "src"
                run_text([ilspy, "-o", str(out_dir), str(assembly)], timeout=120)
                snippets: list[str] = []
                for source in out_dir.rglob("*.cs"):
                    try:
                        lines = source.read_text(errors="ignore").splitlines()
                    except OSError:
                        continue
                    for index, line in enumerate(lines, start=1):
                        if any(term in line for term in MANAGED_TERMS):
                            snippet = f"{source.relative_to(out_dir)}:{index}: {line.strip()}"
                            snippets.append(snippet)
                            if len(snippets) >= 80:
                                break
                    if len(snippets) >= 80:
                        break
                results.append({"assembly": rel(root, assembly), "snippets": snippets})
    return results


def scan_game(name: str, root: Path, *, include_managed: bool) -> dict[str, object]:
    files = walk_files(root) if root.exists() else []
    pe_files = [path for path in files if path.suffix.lower() in PE_EXTENSIONS]
    unity_data_dirs = [rel(root, path) for path in root.glob("*_Data") if path.is_dir()]
    pe_report = [(rel(root, path), scan_pe_file(path)) for path in pe_files]

    return {
        "name": name,
        "root": str(root),
        "exists": root.exists(),
        "fileCount": len(files),
        "unityDataDirs": unity_data_dirs,
        "peFiles": pe_report,
        "unityAssets": scan_unity_assets(root, files),
        "managed": scan_managed(root) if include_managed else [],
    }


def write_report(games: list[dict[str, object]], output: Path) -> None:
    lines = [
        "# D3D12 Static Extraction Matrix",
        "",
        "Local compatibility extraction only. This report records derived renderer, shader, kernel, managed callsite, and import facts; it does not copy game code or assets.",
        "",
    ]
    for game in games:
        lines.extend(
            [
                f"## {game['name']}",
                "",
                f"- Root: `{game['root']}`",
                f"- Exists: `{game['exists']}`",
                f"- Files scanned: `{game['fileCount']}`",
                f"- Unity data dirs: {', '.join(f'`{item}`' for item in game['unityDataDirs']) or '`none`'}",
                "",
                "### PE / launcher surface",
                "",
            ]
        )
        for path, info in game["peFiles"]:
            markers = ", ".join(f"`{marker}`" for marker in info["markers"]) or "`none`"
            lines.extend([f"- `{path}`", f"  - Type: `{info['fileType']}`", f"  - Markers: {markers}"])
            if info["lines"]:
                lines.append("  - Sample lines:")
                for line in info["lines"][:12]:
                    lines.append(f"    - `{line[:180]}`")
        if not game["peFiles"]:
            lines.append("- `none`")

        assets = game["unityAssets"]
        dxbc_total = sum(count for _, count in assets["dxbc"])
        dxil_total = sum(count for _, count in assets["dxil"])
        lines.extend(
            [
                "",
                "### Unity shader surface",
                "",
                f"- DXBC containers: `{dxbc_total}` across `{len(assets['dxbc'])}` files",
                f"- DXIL containers: `{dxil_total}` across `{len(assets['dxil'])}` files",
                "- Top DXBC files:",
            ]
        )
        for path, count in assets["dxbc"][:12]:
            lines.append(f"  - `{path}`: `{count}`")
        if not assets["dxbc"]:
            lines.append("  - `none`")
        if assets["kernels"]:
            lines.append("- Kernel / feature terms:")
            for item in assets["kernels"][:50]:
                lines.append(f"  - `{item}`")
        if assets["shaderNames"]:
            lines.append("- Shader names:")
            for item in assets["shaderNames"][:50]:
                lines.append(f"  - `{item}`")

        lines.extend(["", "### Managed Unity callsites", ""])
        if game["managed"]:
            for managed in game["managed"]:
                lines.append(f"- `{managed['assembly']}`")
                if managed["snippets"]:
                    for snippet in managed["snippets"][:40]:
                        lines.append(f"  - `{snippet[:220]}`")
                else:
                    lines.append("  - `no matching managed graphics callsites found`")
        else:
            lines.append("- `none`")
        lines.append("")

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", action="append", help="Custom game entry as NAME=ABSOLUTE_PATH")
    parser.add_argument("--no-managed", action="store_true", help="Skip ILSpy managed assembly extraction.")
    parser.add_argument("--output", type=Path, help="Report path. Defaults to ~/.metalsharp/logs/d3d12-static-extraction-<timestamp>.md")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    games = dict(DEFAULT_GAMES)
    if args.game:
        games = {}
        for value in args.game:
            if "=" not in value:
                raise SystemExit(f"invalid --game value {value!r}; expected NAME=PATH")
            name, path = value.split("=", 1)
            games[name.strip()] = Path(path).expanduser()

    timestamp = _dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    output = args.output or (Path.home() / ".metalsharp" / "logs" / f"d3d12-static-extraction-{timestamp}.md")
    write_report([scan_game(name, root, include_managed=not args.no_managed) for name, root in games.items()], output)
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
