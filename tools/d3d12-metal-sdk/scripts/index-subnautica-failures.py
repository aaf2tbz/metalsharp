#!/usr/bin/env python3
import argparse
import json
import re
from collections import Counter
from pathlib import Path


DEFAULT_LOG_DIR = Path.home() / ".metalsharp" / "compatdata" / "1962700" / "logs"
DEFAULT_CACHE_ROOTS = [
    Path.home() / ".metalsharp" / "shader-cache",
    Path("/Volumes/AverySSD/SteamLibrary/steamapps/common/Subnautica2/.metalsharp-cache/shader-cache"),
    Path("/tmp/dxmt_shader_cache"),
]

INTERESTING_LOG_PATTERNS = {
    "dxil_msl_compile_failed": re.compile(r"DXIL MSL compilation failed", re.IGNORECASE),
    "metal_pso_error": re.compile(r"(newRenderPipelineState|newComputePipelineState|pso/|pipeline).*failed", re.IGNORECASE),
    "readback_zero": re.compile(r"nonzero_pixels=0"),
    "readback_nonzero": re.compile(r"nonzero_pixels=([1-9][0-9]*)"),
    "fullscreen_diag": re.compile(r"diagnostic_fullscreen", re.IGNORECASE),
    "ue_no_window": re.compile(r"PassLoadingScreenWindowBackToGame failed\. No Window", re.IGNORECASE),
    "present": re.compile(r"\bPresent\b|\bpresent\b"),
    "rtv": re.compile(r"\bRTV\b|render target", re.IGNORECASE),
    "barrier": re.compile(r"barrier|RESOURCE_STATE", re.IGNORECASE),
}


def tail(path: Path, limit: int = 4000) -> str:
    try:
        data = path.read_text(errors="replace")
    except OSError:
        return ""
    return data[-limit:]


def collect_logs(log_dir: Path, limit: int) -> list[dict]:
    logs = sorted(log_dir.glob("launch-*.log"), key=lambda p: p.stat().st_mtime, reverse=True)
    rows = []
    for path in logs[:limit]:
        text = tail(path, 80000)
        counts = {
            key: len(pattern.findall(text))
            for key, pattern in INTERESTING_LOG_PATTERNS.items()
        }
        snippets = []
        for line in text.splitlines():
            if any(pattern.search(line) for pattern in INTERESTING_LOG_PATTERNS.values()):
                snippets.append(line[-500:])
        rows.append(
            {
                "path": str(path),
                "mtime": path.stat().st_mtime,
                "size": path.stat().st_size,
                "counts": counts,
                "snippets_tail": snippets[-60:],
            }
        )
    return rows


def collect_shader_sidecars(roots: list[Path]) -> list[dict]:
    sidecars = []
    for root in roots:
        if not root.exists():
            continue
        for pattern in ("*.msl.err.txt", "*.msc.fail", "*.dxil_report.txt", "pso-*.json"):
            for path in root.rglob(pattern):
                entry = {
                    "path": str(path),
                    "kind": pattern,
                    "size": path.stat().st_size,
                    "mtime": path.stat().st_mtime,
                }
                if path.suffix in {".txt", ".fail"} or path.name.endswith(".fail"):
                    entry["tail"] = tail(path, 3000)
                sidecars.append(entry)
    return sorted(sidecars, key=lambda item: item["mtime"], reverse=True)


def write_markdown(path: Path, result: dict) -> None:
    log_totals = Counter()
    for log in result["logs"]:
        log_totals.update(log["counts"])

    lines = [
        "# Subnautica 2 Failure Index",
        "",
        "Generated from local logs and shader sidecars without launching the game.",
        "",
        "## Totals",
        "",
    ]
    for key, count in sorted(log_totals.items()):
        lines.append(f"- `{key}`: {count}")
    lines.extend(
        [
            f"- `shader_sidecars`: {len(result['shader_sidecars'])}",
            "",
            "## Latest Logs",
            "",
        ]
    )
    for log in result["logs"][:8]:
        lines.append(f"### `{log['path']}`")
        active = {key: value for key, value in log["counts"].items() if value}
        lines.append("")
        lines.append(f"- counts: `{json.dumps(active, sort_keys=True)}`")
        if log["snippets_tail"]:
            lines.append("- tail evidence:")
            for snippet in log["snippets_tail"][-8:]:
                lines.append(f"  - `{snippet}`")
        lines.append("")

    lines.extend(["## Latest Shader Sidecars", ""])
    for sidecar in result["shader_sidecars"][:20]:
        lines.append(f"- `{sidecar['kind']}` `{sidecar['path']}`")

    path.write_text("\n".join(lines) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Index Subnautica 2 D3D12/Metal failures from logs and shader sidecars without launching the game."
    )
    parser.add_argument("--log-dir", default=str(DEFAULT_LOG_DIR))
    parser.add_argument("--cache-root", action="append", default=[])
    parser.add_argument("--limit", type=int, default=12)
    parser.add_argument("--results-dir", default=str(Path(__file__).resolve().parents[1] / "results"))
    parser.add_argument("--profile", default="subnautica2")
    args = parser.parse_args()

    log_dir = Path(args.log_dir)
    roots = [Path(root) for root in args.cache_root] if args.cache_root else DEFAULT_CACHE_ROOTS
    results_dir = Path(args.results_dir)
    results_dir.mkdir(parents=True, exist_ok=True)

    result = {
        "schema": "metalsharp.d3d12-metal.subnautica-failure-index.v1",
        "profile": args.profile,
        "log_dir": str(log_dir),
        "cache_roots": [str(root) for root in roots],
        "logs": collect_logs(log_dir, args.limit) if log_dir.exists() else [],
        "shader_sidecars": collect_shader_sidecars(roots),
    }

    json_path = results_dir / f"subnautica-failure-index-{args.profile}.json"
    md_path = results_dir / f"subnautica-failure-index-{args.profile}.md"
    json_path.write_text(json.dumps(result, indent=2) + "\n")
    write_markdown(md_path, result)
    print(md_path)
    print(json_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
