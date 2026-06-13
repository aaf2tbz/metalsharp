#!/usr/bin/env python3
import json
import sys
from pathlib import Path


def fail(message: str) -> int:
    print(f"shader corpus verification failed: {message}", file=sys.stderr)
    return 1


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("dist/d3d12-metal-shaders")
    manifest_path = root / "manifest.json"
    summary_path = root / "summary.txt"
    tsv_path = root / "compiled-shaders.tsv"

    for path in [manifest_path, summary_path, tsv_path]:
        if not path.is_file() or path.stat().st_size == 0:
            return fail(f"missing required staged file: {path}")

    manifest = json.loads(manifest_path.read_text())
    if manifest.get("schema") != "metalsharp.d3d12-metal.shader-corpus-build.v1":
        return fail("manifest schema mismatch")

    shader_count = int(manifest.get("shader_count") or 0)
    air_count = int(manifest.get("air_count") or 0)
    metallib_count = int(manifest.get("metallib_count") or 0)
    failure_count = int(manifest.get("failure_count") or 0)
    if shader_count <= 0:
        return fail("manifest has no shaders")
    if failure_count != 0:
        return fail(f"manifest reports {failure_count} compile failure(s)")
    if air_count != shader_count:
        return fail(f"manifest air_count={air_count} does not match shader_count={shader_count}")
    if metallib_count != shader_count:
        return fail(f"manifest metallib_count={metallib_count} does not match shader_count={shader_count}")

    missing: list[str] = []
    for shader in manifest.get("shaders", []):
        for key in ["air", "metallib", "log"]:
            path = Path(shader.get(key, ""))
            if not path.is_file() or path.stat().st_size == 0:
                missing.append(str(path))
    if missing:
        return fail("missing staged shader outputs:\n" + "\n".join(missing[:20]))

    print(f"verified D3D12 Metal shader corpus: {shader_count} shaders, {air_count} AIR, {metallib_count} metallib")
    print(f"artifact root: {root}")
    print(f"manifest: {manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
