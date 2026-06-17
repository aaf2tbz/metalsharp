#!/usr/bin/env python3
"""Build and validate an offline CBV/SRV-focused DXIL/DXBC corpus.

This is deliberately game-launch-free. It creates a timestamped, untracked
results tree, optionally fetches shallow open-source HLSL corpora, compiles
focused HLSL fixtures with cached DXC under Wine, imports selected existing
MetalSharp shader-cache DXIL/DXBC blobs, lowers everything through DXMT airconv,
then compiles generated MSL with Apple's Metal compiler and audits for the CBV
row-stride/SRV binding failure class seen in AC6.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

SCRIPT_DIR = Path(__file__).resolve().parent
SDK_ROOT = SCRIPT_DIR.parent
REPO_ROOT = SDK_ROOT.parent.parent
RESULTS_ROOT = SDK_ROOT / "results" / "offline-corpora"
FETCH_ROOT = SDK_ROOT / "cache" / "offline-hlsl-sources"
DXC_EXE = SDK_ROOT / "cache" / "dxc" / "v1.9.2602" / "bin" / "x64" / "dxc.exe"
AIRCONV = REPO_ROOT / "vendor" / "dxmt" / "build-metalsharp-x64" / "src" / "airconv" / "darwin" / "airconv"
WINE = Path.home() / ".metalsharp" / "runtime" / "wine" / "bin" / "wine"

PUBLIC_REPOS = [
    {
        "name": "DirectX-Graphics-Samples",
        "url": "https://github.com/microsoft/DirectX-Graphics-Samples.git",
        "license": "MIT",
        "patterns": ["*.hlsl", "*.hlsli", "*.fx"],
    },
    {
        "name": "DirectXTK12",
        "url": "https://github.com/microsoft/DirectXTK12.git",
        "license": "MIT",
        "patterns": ["*.hlsl", "*.hlsli", "*.fx"],
    },
    {
        "name": "DirectXShaderCompiler",
        "url": "https://github.com/microsoft/DirectXShaderCompiler.git",
        "license": "LLVM/NCSA + MIT-style components",
        "patterns": ["*.hlsl", "*.hlsli", "*.fx"],
    },
    {
        "name": "FidelityFX-SDK",
        "url": "https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git",
        "license": "AMD/MIT-like; verify upstream LICENSE before redistribution",
        "patterns": ["*.hlsl", "*.hlsli"],
    },
]

BINDING_RE = re.compile(
    r"\b(cbuffer|ConstantBuffer\s*<|Texture[123DCube]|RWTexture|Sampler(State|ComparisonState)?|register\s*\([btsu]\d+)",
    re.IGNORECASE,
)

FIXTURES: dict[str, str] = {
    "cbv_srv_rows_vsps.hlsl": r'''
struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD0; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
cbuffer SceneCB : register(b0) {
  float4 row0;
  float4 row1;
  float4 row2;
  float4 row3;
  float4 row4;
  float4 row5;
  float4 row6;
  float4 row7;
  float4 row8;
  float4 row9;
  float4 row10;
  float4 row11;
};
Texture2D<float4> ColorTex : register(t0);
Texture2D<float4> MaskTex : register(t1);
SamplerState LinearClamp : register(s0);
VSOut vs_main(VSIn input) {
  VSOut o;
  o.pos = float4(input.pos.xy * row5.xy + row9.zw, input.pos.z, 1.0);
  o.uv = input.uv;
  return o;
}
float4 ps_main(VSOut input) : SV_Target0 {
  float4 color = ColorTex.Sample(LinearClamp, input.uv);
  float4 mask = MaskTex.Sample(LinearClamp, input.uv * row10.xy + row11.zw);
  return float4(color.rgb * row5.rgb + mask.rgb * row9.rgb + row10.rgb, 1.0);
}
''',
    "cbv_srv_register_spaces.hlsl": r'''
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
cbuffer RootLikeCB : register(b2, space1) {
  float4 c0;
  float4 c1;
  float4 c2;
  float4 c3;
  float4 c4;
  float4 c5;
  float4 c6;
  float4 c7;
  float4 c8;
  float4 c9;
  float4 c10;
  float4 c11;
};
Texture2D<float4> T0 : register(t4, space1);
Texture2D<float4> T1 : register(t5, space1);
SamplerState S0 : register(s3, space1);
VSOut vs_main(uint vid : SV_VertexID) {
  float2 p = float2((vid == 2) ? 3.0 : -1.0, (vid == 1) ? -3.0 : 1.0);
  VSOut o; o.pos = float4(p, 0.0, 1.0); o.uv = p * float2(0.5, -0.5) + 0.5; return o;
}
float4 ps_main(VSOut input) : SV_Target0 {
  float4 a = T0.Sample(S0, input.uv);
  float4 b = T1.Sample(S0, input.uv);
  return pow(abs(a + b * c5 + c9), max(c10, 0.0001));
}
''',
    "cbv_load_legacy_indexing.hlsl": r'''
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
cbuffer BigCB : register(b0) { float4 rows[16]; };
Texture2D<float4> Tex : register(t0);
SamplerState Samp : register(s0);
VSOut vs_main(uint vid : SV_VertexID) {
  float2 p = float2((vid & 1) ? 1.0 : -1.0, (vid & 2) ? -1.0 : 1.0);
  VSOut o; o.pos = float4(p * rows[5].xy, 0.0, 1.0); o.uv = p * 0.5 + 0.5; return o;
}
float4 ps_main(VSOut input) : SV_Target0 {
  uint idx = (uint)(abs(rows[11].x)) & 3;
  return Tex.Sample(Samp, input.uv) * rows[5 + idx] + rows[9] + rows[10];
}
''',
}


def run(cmd: list[str], cwd: Path | None = None, env: dict[str, str] | None = None, timeout: int = 120) -> dict[str, Any]:
    completed = subprocess.run(cmd, cwd=str(cwd) if cwd else None, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    return {"command": cmd, "returncode": completed.returncode, "stdout": completed.stdout, "stderr": completed.stderr}


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def which_xcrun_tool(name: str) -> str | None:
    r = run(["xcrun", "-find", name], timeout=30)
    if r["returncode"] != 0:
        return None
    s = r["stdout"].strip()
    return s or None


def ensure_tool(path: Path, label: str) -> None:
    if not path.exists():
        raise SystemExit(f"missing {label}: {path}")


def fetch_repos(fetch_root: Path, repos: list[dict[str, Any]], limit: int) -> list[dict[str, Any]]:
    fetch_root.mkdir(parents=True, exist_ok=True)
    records = []
    for repo in repos[:limit if limit > 0 else len(repos)]:
        dest = fetch_root / repo["name"]
        if dest.exists():
            records.append({**repo, "path": str(dest), "status": "cached"})
            continue
        cmd = ["git", "clone", "--depth", "1", "--filter=blob:none", repo["url"], str(dest)]
        r = run(cmd, timeout=600)
        records.append({**repo, "path": str(dest), "status": "ok" if r["returncode"] == 0 else "failed", "stderr_tail": r["stderr"][-2000:]})
    return records


def discover_public_hlsl(fetch_root: Path, max_files: int) -> list[Path]:
    files: list[Path] = []
    if not fetch_root.exists():
        return files
    for path in fetch_root.rglob("*"):
        if path.suffix.lower() not in {".hlsl", ".hlsli", ".fx"} or not path.is_file():
            continue
        try:
            text = path.read_text(errors="replace")[:20000]
        except OSError:
            continue
        if BINDING_RE.search(text):
            files.append(path)
        if max_files and len(files) >= max_files:
            break
    return files


def write_fixtures(src_dir: Path) -> list[Path]:
    src_dir.mkdir(parents=True, exist_ok=True)
    out = []
    for name, text in FIXTURES.items():
        p = src_dir / name
        p.write_text(text.strip() + "\n")
        out.append(p)
    return out


def compile_hlsl(hlsl: Path, entry: str, profile: str, out_dir: Path, extra_args: list[str] | None = None) -> dict[str, Any]:
    out_dir.mkdir(parents=True, exist_ok=True)
    safe_parent = re.sub(r"[^A-Za-z0-9_.-]+", "_", hlsl.parent.name)[-48:]
    stem = f"{safe_parent}-{hlsl.stem}-{entry}-{profile}"
    dxbc = out_dir / f"{stem}.dxbc"
    asm = out_dir / f"{stem}.asm.txt"
    cmd = [str(WINE), str(DXC_EXE), "-nologo", "-E", entry, "-T", profile, "-I", str(hlsl.parent)]
    if extra_args:
        cmd.extend(extra_args)
    cmd.extend(["-Fo", str(dxbc), "-Fc", str(asm), str(hlsl)])
    env = os.environ.copy()
    env["WINEDLLOVERRIDES"] = "dxcompiler,dxil=n,b"
    r = run(cmd, cwd=hlsl.parent, env=env, timeout=180)
    ok = r["returncode"] == 0 and dxbc.exists() and dxbc.stat().st_size > 0
    return {"hlsl": str(hlsl), "entry": entry, "profile": profile, "dxbc": str(dxbc), "asm": str(asm), "ok": ok, "returncode": r["returncode"], "stderr_tail": r["stderr"][-2000:], "stdout_tail": r["stdout"][-1000:]}


def compile_fixture_set(fixtures: list[Path], out_dir: Path) -> list[dict[str, Any]]:
    jobs = []
    for hlsl in fixtures:
        text = hlsl.read_text(errors="replace")
        if "vs_main" in text:
            jobs.append(compile_hlsl(hlsl, "vs_main", "vs_6_0", out_dir))
        if "ps_main" in text:
            jobs.append(compile_hlsl(hlsl, "ps_main", "ps_6_0", out_dir))
    return jobs


def infer_public_jobs(hlsl: Path) -> list[tuple[str, str]]:
    try:
        text = hlsl.read_text(errors="replace")
    except OSError:
        return []
    if "#include" in text[:4000]:
        # Keep this corpus deterministic and low-maintenance; generated fixtures
        # and local game shaders cover include-heavy cases after preprocessing.
        return []
    entries = []
    for entry in ("main", "VSMain", "PSMain", "vs_main", "ps_main"):
        if re.search(r"\b" + re.escape(entry) + r"\s*\(", text):
            if entry.lower().startswith("vs") or ("SV_Position" in text and "SV_Target" not in text):
                entries.append((entry, "vs_6_0"))
            if entry.lower().startswith("ps") or "SV_Target" in text or "COLOR" in text:
                entries.append((entry, "ps_6_0"))
            if entry == "main" and not entries:
                entries.append((entry, "ps_6_0"))
            break
    return entries[:1]


def compile_public_hlsl(public_hlsl: list[Path], out_dir: Path, target_successes: int, max_attempts: int) -> list[dict[str, Any]]:
    results = []
    successes = 0
    attempts = 0
    for hlsl in public_hlsl:
        jobs = infer_public_jobs(hlsl)
        if not jobs:
            continue
        for entry, profile in jobs:
            if max_attempts and attempts >= max_attempts:
                return results
            if target_successes and successes >= target_successes:
                return results
            attempts += 1
            result = compile_hlsl(hlsl, entry, profile, out_dir)
            result["public_source"] = True
            results.append(result)
            if result["ok"]:
                successes += 1
    return results


def dxbc_chunks(path: Path) -> list[str]:
    try:
        data = path.read_bytes()
    except OSError:
        return []
    if len(data) < 32 or data[:4] != b"DXBC":
        return []
    import struct

    try:
        count = struct.unpack_from("<I", data, 28)[0]
    except struct.error:
        return []
    chunks = []
    for i in range(count):
        try:
            off = struct.unpack_from("<I", data, 32 + i * 4)[0]
        except struct.error:
            break
        if off + 4 <= len(data):
            chunks.append(data[off : off + 4].decode("latin1", "replace"))
    return chunks


def dxbc_kind(path: Path) -> str:
    chunks = dxbc_chunks(path)
    if not chunks:
        return "unknown"
    if "DXIL" in chunks:
        return "dxil-container"
    if "SHEX" in chunks or "SHDR" in chunks:
        return "legacy-sm4-sm5"
    return "non-dxil-dxbc"


def select_public_legacy_dxbc(fetch_root: Path, out_dir: Path, max_files: int) -> list[dict[str, Any]]:
    out_dir.mkdir(parents=True, exist_ok=True)
    selected = []
    if not fetch_root.exists() or max_files <= 0:
        return selected
    candidates = []
    for path in fetch_root.rglob("*.dxbc"):
        kind = dxbc_kind(path)
        if kind == "legacy-sm4-sm5":
            score = 0
            chunks = dxbc_chunks(path)
            score += 5 if "RDEF" in chunks else 0
            score += 3 if "SFI0" in chunks else 0
            score += 3 if path.name.lower().startswith(("cbuffer", "sample", "srv", "gather", "uav")) else 0
            candidates.append((score, path))
    for _, source in sorted(candidates, reverse=True)[:max_files]:
        rel = source.relative_to(fetch_root)
        safe = "-".join(rel.parts[-4:])
        dest = out_dir / safe
        shutil.copy2(source, dest)
        selected.append({
            "source": str(source),
            "dxbc": str(dest),
            "kind": dxbc_kind(dest),
            "chunks": dxbc_chunks(dest),
            "sha256": sha256(dest),
        })
    return selected


def select_local_dxbc(out_dir: Path, max_files: int, legacy_files: int) -> list[dict[str, Any]]:
    roots = [
        Path.home() / ".metalsharp" / "shader-cache" / "m12" / "1888160",
        Path.home() / ".metalsharp" / "shader-cache" / "m12" / "1245620",
        Path.home() / ".metalsharp" / "shader-cache" / "m12" / "1962700",
    ]
    out_dir.mkdir(parents=True, exist_ok=True)
    selected = []
    for root in roots:
        if not root.exists():
            continue
        candidates: list[tuple[int, str, Path]] = []
        for dxbc in root.glob("*.dxbc"):
            kind = dxbc_kind(dxbc)
            if kind == "unknown":
                continue
            msl = root / f"{dxbc.stem}.msl"
            text = ""
            if msl.exists():
                try:
                    text = msl.read_text(errors="replace")
                except OSError:
                    text = ""
            score = 0
            score += 5 if "buf0" in text or "constant" in text or "device float4" in text else 0
            score += 5 if "texture2d" in text or "sample(" in text else 0
            score += 10 if dxbc.stem in {"58539be4844b1dd9", "8fc08bbc2900719b", "6f0e7d2f3cfff83c"} else 0
            score += 3 if kind != "dxil-container" else 0
            if score:
                candidates.append((score, kind, dxbc))
        legacy = [(s, k, p) for s, k, p in candidates if k != "dxil-container"]
        dxil = [(s, k, p) for s, k, p in candidates if k == "dxil-container"]
        chosen = sorted(legacy, reverse=True)[:legacy_files] + sorted(dxil, reverse=True)[:max_files]
        for _, kind, dxbc in chosen:
            dest = out_dir / f"{root.name}-{kind}-{dxbc.name}"
            shutil.copy2(dxbc, dest)
            selected.append({
                "source": str(dxbc),
                "dxbc": str(dest),
                "kind": kind,
                "chunks": dxbc_chunks(dest),
                "sha256": sha256(dest),
            })
    return selected


def airconv_emit_msl(dxbc: Path, out_dir: Path) -> dict[str, Any]:
    out_dir.mkdir(parents=True, exist_ok=True)
    msl = out_dir / f"{dxbc.stem}.msl"
    env = os.environ.copy()
    env["DYLD_LIBRARY_PATH"] = "/usr/lib" + (":" + env["DYLD_LIBRARY_PATH"] if env.get("DYLD_LIBRARY_PATH") else "")
    r = run([str(AIRCONV), "--emit-msl", "-o", str(msl), str(dxbc)], cwd=REPO_ROOT, env=env, timeout=180)
    ok = r["returncode"] == 0 and msl.exists() and msl.stat().st_size > 0
    return {"dxbc": str(dxbc), "msl": str(msl), "ok": ok, "returncode": r["returncode"], "stderr_tail": r["stderr"][-4000:], "stdout_tail": r["stdout"][-1000:]}


DXBC_OPCODE_NAMES = {
    195: "DEQ",
    199: "DMOV",
    201: "DTOF",
    210: "DDIV-or-double-reciprocal-family",
    211: "DFMA",
    223: "LD_FEEDBACK",
    224: "LD_MS_FEEDBACK",
    225: "LD_UAV_TYPED_FEEDBACK",
    226: "LD_RAW_FEEDBACK",
    227: "LD_STRUCTURED_FEEDBACK",
    234: "CHECK_ACCESS_FULLY_MAPPED",
}


def classify_legacy_failure(dxbc: Path, returncode: int, text: str, opcode: int | None) -> str | None:
    if returncode == 0:
        return None
    if "Hull and domain shader cannot be independently converted" in text:
        return "requires-hs-ds-pair"
    if "unsupported SM5.1 dynamic/nonzero cbuffer descriptor index" in text:
        return "unsupported-sm51-cbuffer-descriptor-index"
    if opcode in {195, 199, 201, 210, 211}:
        return "unsupported-legacy-double-op"
    if "handle_signature_ps" in text or dxbc.name.endswith(("input2.dxbc", "output3.dxbc")):
        return "unsupported-pixel-signature-semantic"
    if opcode is not None:
        return "unhandled-legacy-dxbc-opcode"
    return "legacy-airconv-failure-unclassified"


def airconv_compile_legacy_dxbc(dxbc: Path, out_dir: Path) -> dict[str, Any]:
    out_dir.mkdir(parents=True, exist_ok=True)
    air = out_dir / f"{dxbc.stem}.air"
    env = os.environ.copy()
    env["DYLD_LIBRARY_PATH"] = "/usr/lib" + (":" + env["DYLD_LIBRARY_PATH"] if env.get("DYLD_LIBRARY_PATH") else "")
    r = run([str(AIRCONV), "-o", str(air), str(dxbc)], cwd=REPO_ROOT, env=env, timeout=180)
    text = (r["stdout"] + "\n" + r["stderr"])[-8000:]
    opcode = None
    m = re.search(r"unhandled dxbc instruction\s+(\d+)", text)
    if m:
        opcode = int(m.group(1))
    ok = r["returncode"] == 0 and air.exists() and air.stat().st_size > 0
    return {
        "dxbc": str(dxbc),
        "air": str(air),
        "ok": ok,
        "returncode": r["returncode"],
        "unhandled_opcode": opcode,
        "opcode_name": DXBC_OPCODE_NAMES.get(opcode) if opcode is not None else None,
        "failure_class": classify_legacy_failure(dxbc, r["returncode"], text, opcode),
        "chunks": dxbc_chunks(dxbc),
        "stderr_tail": r["stderr"][-4000:],
        "stdout_tail": r["stdout"][-1000:],
    }


def compile_metal(msl: Path, out_dir: Path, metal_tool: str, metallib_tool: str) -> dict[str, Any]:
    air = out_dir / f"{msl.stem}.air"
    lib = out_dir / f"{msl.stem}.metallib"
    log = out_dir / f"{msl.stem}.metal.log"
    out_dir.mkdir(parents=True, exist_ok=True)
    m = run([metal_tool, "-x", "metal", "-c", str(msl), "-o", str(air)], timeout=180)
    ml = None
    if m["returncode"] == 0 and air.exists():
        ml = run([metallib_tool, str(air), "-o", str(lib)], timeout=180)
    ok = m["returncode"] == 0 and ml is not None and ml["returncode"] == 0 and lib.exists()
    log.write_text(f"metal={m}\nmetallib={ml}\n")
    return {"msl": str(msl), "air": str(air), "metallib": str(lib), "ok": ok, "metal_returncode": m["returncode"], "metallib_returncode": None if ml is None else ml["returncode"], "log": str(log), "stderr_tail": (m["stderr"] + (ml["stderr"] if ml else ""))[-3000:]}


def audit_msl(msl: Path) -> dict[str, Any]:
    text = msl.read_text(errors="replace")
    uses_cb = "cbufferLoad" in text or "buf0" in text or "device float4" in text or "constant" in text
    uses_srv = "texture2d" in text or ".sample(" in text
    bad_stride64 = bool(re.search(r"\*\s*64\]\)|\*\s*64\]", text))
    good_stride16 = bool(re.search(r"\*\s*16\]\)|\*\s*16\]", text))
    return {"msl": str(msl), "uses_cbv_like": uses_cb, "uses_srv_like": uses_srv, "bad_stride64": bad_stride64, "good_stride16": good_stride16, "ok": not bad_stride64}


def write_summary(out_root: Path, manifest: dict[str, Any]) -> None:
    lines = ["# Offline CBV/SRV corpus validation", ""]
    s = manifest["summary"]
    for k in ["compiled_fixture_count", "compiled_fixture_failures", "local_dxbc_count", "airconv_count", "airconv_failures", "metal_count", "metal_failures", "audit_failures"]:
        lines.append(f"- {k}: `{s[k]}`")
    lines.append("")
    lines.append("## Public source seeds")
    for rec in manifest.get("fetch", []):
        lines.append(f"- `{rec['name']}`: `{rec.get('status')}` {rec['url']}")
    lines.append("")
    lines.append("## Key audit")
    lines.append("")
    lines.append("- Failure condition: generated MSL containing `*64` constant-buffer row stride.")
    lines.append("- Success condition: all generated MSL passes Apple Metal compile and no audited MSL has `*64` CBV row stride.")
    out_root.joinpath("summary.md").write_text("\n".join(lines) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", default="")
    parser.add_argument("--fetch", action="store_true", help="Shallow-clone selected open-source HLSL corpora into SDK cache.")
    parser.add_argument("--fetch-limit", type=int, default=4)
    parser.add_argument("--public-hlsl-limit", type=int, default=80)
    parser.add_argument("--public-compile-successes", type=int, default=12)
    parser.add_argument("--public-compile-attempts", type=int, default=80)
    parser.add_argument("--public-legacy-dxbc", type=int, default=32, help="Checked-in public SHEX/SHDR DXBC blobs to import.")
    parser.add_argument("--local-per-game", type=int, default=18, help="DXIL-container local shaders per game.")
    parser.add_argument("--legacy-dxbc-per-game", type=int, default=18, help="Non-DXIL DXBC/local legacy shaders per game.")
    parser.add_argument("--synthetic-only", action="store_true")
    args = parser.parse_args()

    ensure_tool(DXC_EXE, "cached dxc.exe")
    ensure_tool(WINE, "MetalSharp wine")
    ensure_tool(AIRCONV, "DXMT airconv")
    metal = which_xcrun_tool("metal")
    metallib = which_xcrun_tool("metallib")
    if not metal or not metallib:
        raise SystemExit("xcrun metal/metallib unavailable")

    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    out_root = RESULTS_ROOT / (args.name or f"cbv-srv-{stamp}")
    if out_root.exists():
        shutil.rmtree(out_root)
    out_root.mkdir(parents=True)

    fetch_records = fetch_repos(FETCH_ROOT, PUBLIC_REPOS, args.fetch_limit) if args.fetch else []
    public_hlsl = discover_public_hlsl(FETCH_ROOT, args.public_hlsl_limit) if args.fetch else []

    fixtures = write_fixtures(out_root / "hlsl-fixtures")
    fixture_compiles = compile_fixture_set(fixtures, out_root / "compiled-dxil")
    public_compiles = compile_public_hlsl(
        public_hlsl,
        out_root / "compiled-public-dxil",
        args.public_compile_successes if args.fetch else 0,
        args.public_compile_attempts if args.fetch else 0,
    )
    public_legacy = select_public_legacy_dxbc(
        FETCH_ROOT,
        out_root / "public-legacy-dxbc",
        args.public_legacy_dxbc if args.fetch else 0,
    )

    local_selected = [] if args.synthetic_only else select_local_dxbc(out_root / "local-dxbc", args.local_per_game, args.legacy_dxbc_per_game)

    dxil_paths = [Path(r["dxbc"]) for r in fixture_compiles if r["ok"]]
    dxil_paths += [Path(r["dxbc"]) for r in public_compiles if r["ok"]]
    dxil_paths += [Path(r["dxbc"]) for r in local_selected if r.get("kind") == "dxil-container"]
    legacy_paths = [Path(r["dxbc"]) for r in public_legacy]
    legacy_paths += [Path(r["dxbc"]) for r in local_selected if r.get("kind") != "dxil-container"]

    airconv_results = [airconv_emit_msl(p, out_root / "generated-msl") for p in dxil_paths]
    legacy_airconv_results = [airconv_compile_legacy_dxbc(p, out_root / "legacy-air") for p in legacy_paths]
    audits = [audit_msl(Path(r["msl"])) for r in airconv_results if r["ok"]]
    metal_results = [compile_metal(Path(r["msl"]), out_root / "metal-build", metal, metallib) for r in airconv_results if r["ok"]]

    legacy_failure_classes: dict[str, int] = {}
    legacy_opcode_names: dict[str, int] = {}
    for r in legacy_airconv_results:
        if r["ok"]:
            continue
        failure_class = r.get("failure_class") or "unknown"
        legacy_failure_classes[failure_class] = legacy_failure_classes.get(failure_class, 0) + 1
        opcode_name = r.get("opcode_name")
        if opcode_name:
            legacy_opcode_names[opcode_name] = legacy_opcode_names.get(opcode_name, 0) + 1

    summary = {
        "compiled_fixture_count": len(fixture_compiles),
        "compiled_fixture_failures": sum(1 for r in fixture_compiles if not r["ok"]),
        "compiled_public_count": len(public_compiles),
        "compiled_public_successes": sum(1 for r in public_compiles if r["ok"]),
        "compiled_public_failures": sum(1 for r in public_compiles if not r["ok"]),
        "public_legacy_dxbc_count": len(public_legacy),
        "public_true_shex_shdr_count": sum(1 for r in public_legacy if r.get("kind") == "legacy-sm4-sm5"),
        "local_dxbc_count": len(local_selected),
        "local_legacy_dxbc_count": sum(1 for r in local_selected if r.get("kind") != "dxil-container"),
        "local_dxil_container_count": sum(1 for r in local_selected if r.get("kind") == "dxil-container"),
        "local_true_shex_shdr_count": sum(1 for r in local_selected if r.get("kind") == "legacy-sm4-sm5"),
        "public_hlsl_binding_file_count": len(public_hlsl),
        "airconv_count": len(airconv_results),
        "airconv_failures": sum(1 for r in airconv_results if not r["ok"]),
        "legacy_airconv_count": len(legacy_airconv_results),
        "legacy_airconv_successes": sum(1 for r in legacy_airconv_results if r["ok"]),
        "legacy_airconv_failures": sum(1 for r in legacy_airconv_results if not r["ok"]),
        "legacy_unhandled_opcode_count": len({r.get("unhandled_opcode") for r in legacy_airconv_results if r.get("unhandled_opcode") is not None}),
        "legacy_failure_classes": legacy_failure_classes,
        "legacy_opcode_names": legacy_opcode_names,
        "metal_count": len(metal_results),
        "metal_failures": sum(1 for r in metal_results if not r["ok"]),
        "audit_failures": sum(1 for r in audits if not r["ok"]),
        "cbv_like_msl_count": sum(1 for r in audits if r["uses_cbv_like"]),
        "srv_like_msl_count": sum(1 for r in audits if r["uses_srv_like"]),
        "good_stride16_count": sum(1 for r in audits if r["good_stride16"]),
    }
    ok = summary["compiled_fixture_failures"] == 0 and summary["airconv_failures"] == 0 and summary["metal_failures"] == 0 and summary["audit_failures"] == 0 and summary["cbv_like_msl_count"] > 0 and summary["srv_like_msl_count"] > 0 and summary["public_true_shex_shdr_count"] > 0 and summary["legacy_airconv_successes"] > 0
    manifest = {
        "schema": "metalsharp.m12.offline-cbv-srv-corpus.v1",
        "ok": ok,
        "out_root": str(out_root),
        "tools": {"dxc": str(DXC_EXE), "wine": str(WINE), "airconv": str(AIRCONV), "metal": metal, "metallib": metallib},
        "fetch": fetch_records,
        "public_hlsl_binding_files": [str(p) for p in public_hlsl],
        "fixture_compiles": fixture_compiles,
        "public_compiles": public_compiles,
        "public_legacy_dxbc": public_legacy,
        "local_selected": local_selected,
        "airconv": airconv_results,
        "legacy_airconv": legacy_airconv_results,
        "legacy_failure_index": [r for r in legacy_airconv_results if not r["ok"]],
        "audits": audits,
        "metal": metal_results,
        "summary": summary,
    }
    (out_root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    write_summary(out_root, manifest)
    print(out_root / "manifest.json")
    print(out_root / "summary.md")
    print(json.dumps(summary, indent=2))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
