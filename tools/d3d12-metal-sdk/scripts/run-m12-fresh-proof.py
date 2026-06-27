#!/usr/bin/env python3
"""Fresh M12 proof harness runner.

Phase 0 intentionally produces new proof roots and fresh runtime identity logs.
It does not consume previous proof directories, old shader caches, or older probe
outputs as evidence.
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


DEFAULT_LAB_ROOT = Path("/Volumes/AverySSD/MetalSharp-SM6-UE-Lab")
REQUIRED_WINDOWS = ["d3d12.dll", "dxgi.dll", "dxgi_dxmt.dll", "winemetal.dll"]
REQUIRED_UNIX = ["winemetal.so"]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def wine_path_arg(path: Path) -> str:
    resolved = path.expanduser().resolve()
    return "Z:" + str(resolved).replace("/", "\\")


def git_text(repo: Path, *args: str) -> str:
    try:
        return subprocess.check_output(["git", *args], cwd=repo, text=True).strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return ""


def disk_free_gib(path: Path) -> int:
    usage = shutil.disk_usage(path)
    return int(usage.free // (1024**3))


def timestamp() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y%m%d-%H%M%S")


def write_json(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")


def inspect_runtime(runtime_root: Path) -> tuple[list[dict[str, Any]], list[str]]:
    entries: list[dict[str, Any]] = []
    missing: list[str] = []
    for subdir, names in (("x86_64-windows", REQUIRED_WINDOWS), ("x86_64-unix", REQUIRED_UNIX)):
        for name in names:
            path = runtime_root / subdir / name
            exists = path.exists()
            entry: dict[str, Any] = {
                "name": name,
                "role": subdir,
                "path": str(path),
                "exists": exists,
                "size": path.stat().st_size if exists else 0,
                "sha256": sha256(path) if exists else None,
            }
            if not exists:
                missing.append(str(path))
            entries.append(entry)
    return entries, missing


def copy_runtime_to_probe_dir(runtime_root: Path, out_bin: Path) -> list[dict[str, Any]]:
    out_bin.mkdir(parents=True, exist_ok=True)
    copied: list[dict[str, Any]] = []
    for name in REQUIRED_WINDOWS:
        src = runtime_root / "x86_64-windows" / name
        dst = out_bin / name
        shutil.copy2(src, dst)
        copied.append(
            {
                "name": name,
                "source": str(src),
                "destination": str(dst),
                "source_sha256": sha256(src),
                "destination_sha256": sha256(dst),
                "hash_match": sha256(src) == sha256(dst),
            }
        )
    return copied


def build_identity_probe(repo: Path, cxx: str) -> dict[str, Any]:
    sdk = repo / "tools" / "d3d12-metal-sdk"
    source = sdk / "probes" / "probe_m12_runtime_identity" / "probe_m12_runtime_identity.cpp"
    out_bin = sdk / "out" / "bin"
    exe = out_bin / "probe_m12_runtime_identity.exe"
    out_bin.mkdir(parents=True, exist_ok=True)
    cmd = [
        cxx,
        "-std=c++17",
        "-O2",
        "-static",
        "-static-libgcc",
        "-static-libstdc++",
        "-Wall",
        "-Wextra",
        "-Werror",
        str(source),
        "-lole32",
        "-luuid",
        "-o",
        str(exe),
    ]
    proc = subprocess.run(cmd, cwd=repo, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return {
        "command": cmd,
        "returncode": proc.returncode,
        "stdout": proc.stdout,
        "stderr": proc.stderr,
        "exe": str(exe),
        "ok": proc.returncode == 0 and exe.exists(),
        "exe_sha256": sha256(exe) if exe.exists() else None,
    }


def build_fresh_game(repo: Path, cxx: str) -> dict[str, Any]:
    sdk = repo / "tools" / "d3d12-metal-sdk"
    source = sdk / "probes" / "m12_fresh_game" / "m12_fresh_game.cpp"
    out_bin = sdk / "out" / "bin"
    exe = out_bin / "m12_fresh_game.exe"
    out_bin.mkdir(parents=True, exist_ok=True)
    cmd = [
        cxx,
        "-std=c++17",
        "-O2",
        "-static",
        "-static-libgcc",
        "-static-libstdc++",
        "-Wall",
        "-Wextra",
        "-Werror",
        str(source),
        "-lole32",
        "-luuid",
        "-lgdi32",
        "-o",
        str(exe),
    ]
    proc = subprocess.run(cmd, cwd=repo, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return {
        "command": cmd,
        "returncode": proc.returncode,
        "stdout": proc.stdout,
        "stderr": proc.stderr,
        "exe": str(exe),
        "ok": proc.returncode == 0 and exe.exists(),
        "exe_sha256": sha256(exe) if exe.exists() else None,
    }


def parse_loaddll(stderr: str) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    pattern = re.compile(r'Loaded L"([^"]+)" at ([^:]+):\s*(\w+)')
    for line in stderr.splitlines():
        match = pattern.search(line)
        if not match:
            continue
        path = match.group(1)
        leaf = path.replace("/", "\\").split("\\")[-1].lower()
        if leaf in {
            "d3d12.dll",
            "dxgi.dll",
            "dxgi_dxmt.dll",
            "winemetal.dll",
            "probe_m12_runtime_identity.exe",
            "m12_fresh_game.exe",
        }:
            rows.append({"path": path, "address": match.group(2), "kind": match.group(3)})
    return rows


def wine_path_to_host(path: str) -> Path | None:
    normalized = path.replace("\\", "/")
    if len(normalized) >= 3 and normalized[1:3] == ":/":
        drive = normalized[0].lower()
        rest = normalized[3:]
        if drive == "z":
            return Path("/") / rest
    return None


def validate_loaded_runtime(
    parsed: dict[str, Any] | None,
    loaddll_rows: list[dict[str, Any]],
    staged_files: list[dict[str, Any]],
) -> dict[str, Any]:
    expected = {entry["name"].lower(): entry for entry in staged_files}
    modules = (parsed or {}).get("modules", {}) if isinstance(parsed, dict) else {}
    loaded_by_name: dict[str, dict[str, Any]] = {}
    for row in loaddll_rows:
        leaf = row["path"].replace("/", "\\").split("\\")[-1].lower()
        loaded_by_name[leaf] = row

    checks: list[dict[str, Any]] = []
    for name, expected_entry in expected.items():
        module = modules.get(name, {})
        wine_path = module.get("path", "") if isinstance(module, dict) else ""
        host_path = wine_path_to_host(wine_path)
        loaded_row = loaded_by_name.get(name, {})
        destination = Path(expected_entry["destination"]).resolve()
        host_path_resolved = host_path.resolve() if host_path and host_path.exists() else host_path
        path_match = bool(host_path_resolved and host_path_resolved == destination)
        hash_value = sha256(host_path_resolved) if host_path_resolved and host_path_resolved.exists() else None
        hash_match = hash_value == expected_entry["destination_sha256"]
        kind_match = loaded_row.get("kind") == "native"
        checks.append(
            {
                "name": name,
                "wine_path": wine_path,
                "host_path": str(host_path_resolved) if host_path_resolved else None,
                "expected_destination": str(destination),
                "loaded_kind": loaded_row.get("kind"),
                "expected_kind": "native",
                "path_match": path_match,
                "sha256": hash_value,
                "expected_sha256": expected_entry["destination_sha256"],
                "hash_match": hash_match,
                "kind_match": kind_match,
                "ok": path_match and hash_match and kind_match,
            }
        )
    return {"ok": all(check["ok"] for check in checks), "checks": checks}


def validate_unix_bridge(
    parsed: dict[str, Any] | None,
    runtime_root: Path,
    wine_runtime: Path,
    winemetal_log: Path,
) -> dict[str, Any]:
    expected_name = "winemetal.so"
    primary = (runtime_root / "x86_64-unix" / expected_name).resolve()
    candidates = [
        primary,
        (wine_runtime / "lib" / "wine" / "x86_64-unix" / expected_name).resolve(),
    ]
    env_value = ""
    if isinstance(parsed, dict):
        env_value = parsed.get("environment", {}).get("DXMT_WINEMETAL_UNIXLIB", "")
    log_text = winemetal_log.read_text(errors="replace") if winemetal_log.exists() else ""
    primary_hash = sha256(primary) if primary.exists() else None
    candidate_checks = []
    for candidate in candidates:
        candidate_hash = sha256(candidate) if candidate.exists() else None
        candidate_checks.append(
            {
                "path": str(candidate),
                "exists": candidate.exists(),
                "sha256": candidate_hash,
                "matches_primary": candidate_hash is not None and candidate_hash == primary_hash,
            }
        )
    return {
        "expected_name": expected_name,
        "primary_path": str(primary),
        "primary_exists": primary.exists(),
        "primary_sha256": primary_hash,
        "candidate_checks": candidate_checks,
        "probe_env_value": env_value,
        "env_match": env_value == expected_name,
        "debug_log": str(winemetal_log),
        "debug_log_exists": winemetal_log.exists(),
        "debug_log_status_ok": "status=0x00000000" in log_text,
        "debug_log_name_match": f"unixlib={expected_name}" in log_text,
        "ok": primary.exists()
        and env_value == expected_name
        and all((not check["exists"]) or check["matches_primary"] for check in candidate_checks)
        and winemetal_log.exists()
        and "status=0x00000000" in log_text
        and f"unixlib={expected_name}" in log_text,
    }


def validate_loaddll_staged(loaddll_rows: list[dict[str, Any]], staged_files: list[dict[str, Any]]) -> dict[str, Any]:
    expected = {entry["name"].lower(): entry for entry in staged_files}
    loaded_by_name: dict[str, dict[str, Any]] = {}
    for row in loaddll_rows:
        leaf = row["path"].replace("/", "\\").split("\\")[-1].lower()
        loaded_by_name[leaf] = row
    checks: list[dict[str, Any]] = []
    for name, expected_entry in expected.items():
        row = loaded_by_name.get(name, {})
        host_path = wine_path_to_host(row.get("path", "")) if row else None
        host_path_resolved = host_path.resolve() if host_path and host_path.exists() else host_path
        destination = Path(expected_entry["destination"]).resolve()
        path_match = bool(host_path_resolved and host_path_resolved == destination)
        hash_value = sha256(host_path_resolved) if host_path_resolved and host_path_resolved.exists() else None
        checks.append(
            {
                "name": name,
                "loaded_kind": row.get("kind"),
                "expected_kind": "native",
                "host_path": str(host_path_resolved) if host_path_resolved else None,
                "expected_destination": str(destination),
                "path_match": path_match,
                "sha256": hash_value,
                "expected_sha256": expected_entry["destination_sha256"],
                "hash_match": hash_value == expected_entry["destination_sha256"],
                "kind_match": row.get("kind") == "native",
            }
        )
    for check in checks:
        check["ok"] = check["path_match"] and check["hash_match"] and check["kind_match"]
    return {"ok": all(check["ok"] for check in checks), "checks": checks}


def validate_fresh_game_corpus_tsv(corpus_tsv: Path, target_shaders: int = 300, target_textures: int = 300) -> dict[str, Any]:
    summary_path = corpus_tsv.parent / "fresh-corpus-summary.json"
    manifest_path = corpus_tsv.parent / "fresh-corpus-manifest.json"
    summary: dict[str, Any] = {}
    manifest: dict[str, Any] = {}
    summary_error = ""
    manifest_error = ""
    try:
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        summary_error = str(error)
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        manifest_error = str(error)
    summary_ok = bool(
        summary
        and summary.get("schema") == "metalsharp.m12.fresh.corpus-summary.v1"
        and summary.get("ok") is True
        and Path(summary.get("files_tsv", "")).expanduser().resolve() == corpus_tsv
        and Path(summary.get("manifest", "")).expanduser().resolve() == manifest_path
        and summary.get("target_status", {}).get("shader_ok") is True
        and summary.get("target_status", {}).get("texture_ok") is True
    )
    manifest_ok = bool(
        manifest
        and manifest.get("schema") == "metalsharp.m12.fresh.corpus-manifest.v1"
        and manifest.get("proof_root") == summary.get("proof_root")
        and manifest.get("entry_count") == summary.get("entry_count")
        and manifest.get("category_counts") == summary.get("category_counts")
        and manifest.get("source_families") == summary.get("source_families")
        and manifest.get("target_status", {}).get("shader_ok") is True
        and manifest.get("target_status", {}).get("texture_ok") is True
    )

    rows_seen = 0
    shader_hashes_checked = 0
    texture_hashes_checked = 0
    mismatches: list[dict[str, Any]] = []
    missing: list[str] = []
    with corpus_tsv.open("r", encoding="utf-8") as handle:
        header = handle.readline().rstrip("\n").split("\t")
        columns = {name: index for index, name in enumerate(header)}
        required = {"category", "size", "sha256", "destination"}
        if not required.issubset(columns):
            return {
                "ok": False,
                "error": f"missing required columns: {sorted(required - set(columns))}",
                "path": str(corpus_tsv),
            }
        for line in handle:
            fields = line.rstrip("\n").split("\t")
            if len(fields) < len(header):
                continue
            category = fields[columns["category"]]
            if category == "shader":
                if shader_hashes_checked >= target_shaders:
                    continue
            elif category == "texture":
                if texture_hashes_checked >= target_textures:
                    continue
            else:
                continue
            rows_seen += 1
            destination = Path(fields[columns["destination"]])
            expected_sha256 = fields[columns["sha256"]].lower()
            expected_size = int(fields[columns["size"]])
            if not destination.exists():
                missing.append(str(destination))
            else:
                actual_size = destination.stat().st_size
                actual_sha256 = sha256(destination)
                if actual_size != expected_size or actual_sha256 != expected_sha256:
                    mismatches.append(
                        {
                            "path": str(destination),
                            "expected_size": expected_size,
                            "actual_size": actual_size,
                            "expected_sha256": expected_sha256,
                            "actual_sha256": actual_sha256,
                        }
                    )
            if category == "shader":
                shader_hashes_checked += 1
            else:
                texture_hashes_checked += 1
            if shader_hashes_checked >= target_shaders and texture_hashes_checked >= target_textures:
                break
    return {
        "ok": summary_ok
        and manifest_ok
        and not missing
        and not mismatches
        and shader_hashes_checked >= target_shaders
        and texture_hashes_checked >= target_textures,
        "path": str(corpus_tsv),
        "summary_path": str(summary_path),
        "summary_ok": summary_ok,
        "summary_error": summary_error,
        "summary_schema": summary.get("schema"),
        "manifest_path": str(manifest_path),
        "manifest_ok": manifest_ok,
        "manifest_error": manifest_error,
        "manifest_schema": manifest.get("schema"),
        "summary_proof_root": summary.get("proof_root"),
        "summary_source_families": summary.get("source_families", []),
        "rows_seen": rows_seen,
        "target_shaders": target_shaders,
        "target_textures": target_textures,
        "shader_hashes_checked": shader_hashes_checked,
        "texture_hashes_checked": texture_hashes_checked,
        "missing": missing[:20],
        "missing_count": len(missing),
        "mismatches": mismatches[:20],
        "mismatch_count": len(mismatches),
    }


def build_fresh_game_dxil(out_bin: Path, run_dir: Path, wine: Path, prefix: Path) -> dict[str, Any]:
    dxil_dir = run_dir / "fresh-sm6-dxil"
    dxil_dir.mkdir(parents=True, exist_ok=True)
    hlsl_path = dxil_dir / "m12_fresh_sm6_scene.hlsl"
    vs_path = dxil_dir / "m12_fresh_sm6_vs.dxil"
    ps_path = dxil_dir / "m12_fresh_sm6_ps.dxil"

    sdk_dir = out_bin.parent.parent
    fetch_dxc = sdk_dir / "scripts" / "fetch-dxc.sh"
    fetch_proc = subprocess.run([str(fetch_dxc)], cwd=sdk_dir, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    dxc_stage: dict[str, Any] = {
        "fetch_command": [str(fetch_dxc)],
        "fetch_returncode": fetch_proc.returncode,
        "fetch_stdout": fetch_proc.stdout,
        "fetch_stderr": fetch_proc.stderr,
        "files": [],
        "ok": False,
    }
    if fetch_proc.returncode != 0:
        return {
            "ok": False,
            "hlsl": str(hlsl_path),
            "vs_path": str(vs_path),
            "ps_path": str(ps_path),
            "dxc_stage": dxc_stage,
            "vs": {},
            "ps": {},
        }
    dxc_bin_dir = Path(fetch_proc.stdout.strip().splitlines()[-1]).resolve()
    out_bin.mkdir(parents=True, exist_ok=True)
    staged_files = []
    for filename in ["dxc.exe", "dxcompiler.dll", "dxil.dll"]:
        source = dxc_bin_dir / filename
        destination = out_bin / filename
        shutil.copy2(source, destination)
        staged_files.append(
            {
                "source": str(source),
                "destination": str(destination),
                "size": destination.stat().st_size,
                "sha256": sha256(destination),
            }
        )
    dxc_stage["files"] = staged_files
    dxc_stage["ok"] = True

    hlsl_path.write_text(
        """
struct VSIn {
  float3 position : POSITION;
  float4 color : COLOR0;
};
struct PSIn {
  float4 position : SV_POSITION;
  float4 color : COLOR0;
};
PSIn VSMain(VSIn input) {
  PSIn output;
  output.position = float4(input.position, 1.0);
  output.color = input.color;
  return output;
}
float4 PSMain(PSIn input) : SV_TARGET {
  return float4(1.0, 0.0, 1.0, 1.0);
}
""".lstrip(),
        encoding="utf-8",
    )

    env = os.environ.copy()
    for key in list(env):
        if key.startswith("DXMT_"):
            env.pop(key, None)
    env.update({"WINEPREFIX": str(prefix), "WINEDEBUG": "-all"})

    commands = {
        "vs": [
            str(wine),
            "dxc.exe",
            "-T",
            "vs_6_0",
            "-E",
            "VSMain",
            "-Qstrip_debug",
            "-Fo",
            wine_path_arg(vs_path),
            wine_path_arg(hlsl_path),
        ],
        "ps": [
            str(wine),
            "dxc.exe",
            "-T",
            "ps_6_0",
            "-E",
            "PSMain",
            "-Qstrip_debug",
            "-Fo",
            wine_path_arg(ps_path),
            wine_path_arg(hlsl_path),
        ],
    }
    results: dict[str, Any] = {}
    ok = True
    for name, cmd in commands.items():
        proc = subprocess.run(cmd, cwd=out_bin, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        stdout_path = dxil_dir / f"dxc-{name}.stdout.txt"
        stderr_path = dxil_dir / f"dxc-{name}.stderr.txt"
        stdout_path.write_text(proc.stdout)
        stderr_path.write_text(proc.stderr)
        output = vs_path if name == "vs" else ps_path
        output_ok = proc.returncode == 0 and output.exists() and output.stat().st_size > 0
        ok = ok and output_ok
        results[name] = {
            "command": cmd,
            "returncode": proc.returncode,
            "stdout": str(stdout_path),
            "stderr": str(stderr_path),
            "output": str(output),
            "output_size": output.stat().st_size if output.exists() else 0,
            "output_sha256": sha256(output) if output.exists() else "",
            "ok": output_ok,
        }
    return {
        "ok": ok,
        "hlsl": str(hlsl_path),
        "vs_path": str(vs_path),
        "dxc_stage": dxc_stage,
        "ps_path": str(ps_path),
        "vs": results.get("vs", {}),
        "ps": results.get("ps", {}),
    }


def cache_file_record(path: Path) -> dict[str, Any]:
    return {
        "path": str(path),
        "exists": path.exists(),
        "size": path.stat().st_size if path.exists() else 0,
        "sha256": sha256(path) if path.exists() else "",
    }


def validate_presented_shader_cache(shader_cache_dir: Path, stderr_text: str, d3d12_json: dict[str, Any], visible_frames: int) -> dict[str, Any]:
    errors: list[str] = []
    visible_scene = d3d12_json.get("visible_scene", {}) if d3d12_json else {}
    visible_vertices = int(visible_scene.get("vertices_per_frame", 0) or 0)
    dxil_draws = re.findall(r"M12 swapchain DrawInstanced encoded v=3 i=1 .*?vs=([0-9a-f]{16}) ps=([0-9a-f]{16})", stderr_text)
    sm5_pattern = rf"M12 swapchain DrawInstanced encoded v={visible_vertices} i=1 .*?vs=([0-9a-f]{{16}}) ps=([0-9a-f]{{16}})"
    sm5_draws = re.findall(sm5_pattern, stderr_text) if visible_vertices else []
    dxil_unique_draws = sorted(set(dxil_draws))
    sm5_unique_draws = sorted(set(sm5_draws))
    if not dxil_draws:
        errors.append("missing_dxil_presented_draw_hashes")
    if not sm5_draws:
        errors.append("missing_sm5_presented_draw_hashes")
    if len(dxil_unique_draws) > 1:
        errors.append("unexpected_multiple_dxil_presented_shader_pairs")
    if len(sm5_unique_draws) > 1:
        errors.append("unexpected_multiple_sm5_presented_shader_pairs")
    dxil_vs, dxil_ps = dxil_unique_draws[0] if dxil_unique_draws else ("", "")
    sm5_vs, sm5_ps = sm5_unique_draws[0] if sm5_unique_draws else ("", "")

    required_paths: list[Path] = []
    for shader_hash in [dxil_vs, dxil_ps]:
        if shader_hash:
            required_paths.extend(
                shader_cache_dir / f"{shader_hash}{suffix}"
                for suffix in [".dxbc", ".module.txt", ".dxil_report.txt", ".msl"]
            )
    required_paths.extend([shader_cache_dir / "dxmt_sm50_vs_main.metallib", shader_cache_dir / "dxmt_sm50_ps_main.metallib"])
    required_files = [cache_file_record(path) for path in required_paths]
    for record in required_files:
        if not record["exists"] or int(record["size"] or 0) <= 0:
            errors.append(f"missing_or_empty_cache_file:{record['path']}")

    msl_checks: dict[str, Any] = {}
    if dxil_ps:
        ps_msl = shader_cache_dir / f"{dxil_ps}.msl"
        text = ps_msl.read_text(errors="replace") if ps_msl.exists() else ""
        msl_checks["dxil_ps_magenta_constants"] = all(
            token in text for token in ["result.x = 1.0f;", "result.y = 0;", "result.z = 1.0f;", "result.w = 1.0f;"]
        )
        if not msl_checks["dxil_ps_magenta_constants"]:
            errors.append("dxil_ps_msl_missing_magenta_constants")
    if dxil_vs:
        vs_msl = shader_cache_dir / f"{dxil_vs}.msl"
        text = vs_msl.read_text(errors="replace") if vs_msl.exists() else ""
        msl_checks["dxil_vs_clip_w_one"] = "out.position.w = 1.0f;" in text
        msl_checks["dxil_vs_vertex_pull"] = "m12_load_vertex_attr" in text
        if not msl_checks["dxil_vs_clip_w_one"]:
            errors.append("dxil_vs_msl_missing_clip_w_one")
        if not msl_checks["dxil_vs_vertex_pull"]:
            errors.append("dxil_vs_msl_missing_vertex_pull")

    pso_files = sorted(shader_cache_dir.glob("pso-render-*.json"))
    pso_records = [cache_file_record(path) for path in pso_files]
    pipelines: list[dict[str, Any]] = []
    for path in pso_files:
        try:
            for pipeline in json.loads(path.read_text()).get("pipelines", []):
                copy = dict(pipeline)
                copy["manifest"] = str(path)
                pipelines.append(copy)
        except Exception as error:  # noqa: BLE001 - report malformed cache evidence.
            errors.append(f"invalid_pso_manifest:{path}:{error}")
    dxil_pso = next(
        (
            p
            for p in pipelines
            if p.get("d3d12", {}).get("vs_hash") == dxil_vs and p.get("d3d12", {}).get("ps_hash") == dxil_ps
        ),
        None,
    )
    sm5_pso = next(
        (
            p
            for p in pipelines
            if p.get("d3d12", {}).get("vs_hash") == sm5_vs and p.get("d3d12", {}).get("ps_hash") == sm5_ps
        ),
        None,
    )
    if not dxil_pso:
        errors.append("missing_dxil_presented_pso_manifest")
    if not sm5_pso:
        errors.append("missing_sm5_presented_pso_manifest")
    metallib_policy: dict[str, Any] = {}
    sm5_metallibs = [shader_cache_dir / "dxmt_sm50_vs_main.metallib", shader_cache_dir / "dxmt_sm50_ps_main.metallib"]
    for name, pso in [("dxil", dxil_pso), ("sm5", sm5_pso)]:
        if not pso:
            continue
        if pso.get("type") != "render":
            errors.append(f"{name}_pso_not_render")
        if pso.get("color_formats", [None])[0] != "rgba8unorm":
            errors.append(f"{name}_pso_not_rgba8unorm")
        if int(pso.get("d3d12", {}).get("input_elements", 0) or 0) != 2:
            errors.append(f"{name}_pso_missing_two_input_elements")
        if pso.get("metal", {}).get("rasterization_enabled") is not True:
            errors.append(f"{name}_pso_rasterization_disabled")
        if int(pso.get("metal", {}).get("vertex_function", 0) or 0) == 0:
            errors.append(f"{name}_pso_missing_vertex_function")
        if int(pso.get("metal", {}).get("fragment_function", 0) or 0) == 0:
            errors.append(f"{name}_pso_missing_fragment_function")
        referenced_metallibs = [
            Path(str(pso.get(stage, {}).get("metallib", "")))
            for stage in ["vertex", "fragment"]
            if pso.get(stage, {}).get("metallib")
        ]
        referenced_records = [cache_file_record(path) for path in referenced_metallibs]
        referenced_all_present = bool(referenced_records) and all(
            record["exists"] and int(record["size"] or 0) > 0 for record in referenced_records
        )
        if referenced_all_present:
            metallib_policy[name] = {"ok": True, "policy": "pso_referenced_metallibs_present", "referenced": referenced_records}
        elif name == "dxil" and msl_checks.get("dxil_ps_magenta_constants") and msl_checks.get("dxil_vs_clip_w_one"):
            metallib_policy[name] = {
                "ok": True,
                "policy": "runtime_source_compile_from_cached_msl_no_hash_metallib_file",
                "referenced": referenced_records,
                "required_msl_hashes": [dxil_vs, dxil_ps],
            }
        elif name == "sm5" and all(path.exists() and path.stat().st_size > 0 for path in sm5_metallibs):
            metallib_policy[name] = {
                "ok": True,
                "policy": "runtime_sm5_metallib_files_present",
                "referenced": referenced_records,
                "metallibs": [cache_file_record(path) for path in sm5_metallibs],
            }
        else:
            metallib_policy[name] = {"ok": False, "policy": "unproven_metallib_source", "referenced": referenced_records}
            errors.append(f"{name}_metallib_policy_unproven")

    presented_log_required = min(visible_frames, 16)
    present_tie_ok = (
        len(dxil_draws) >= presented_log_required
        and len(sm5_draws) >= presented_log_required
        and len(dxil_unique_draws) == 1
        and len(sm5_unique_draws) == 1
    )
    if not present_tie_ok:
        errors.append("insufficient_presented_shader_hash_logs")
    ok = not errors
    return {
        "ok": ok,
        "path": str(shader_cache_dir),
        "dxil_presented_hashes": {
            "vs": dxil_vs,
            "ps": dxil_ps,
            "logged_draws": len(dxil_draws),
            "unique_pairs": [[vs, ps] for vs, ps in dxil_unique_draws],
        },
        "sm5_presented_hashes": {
            "vs": sm5_vs,
            "ps": sm5_ps,
            "logged_draws": len(sm5_draws),
            "unique_pairs": [[vs, ps] for vs, ps in sm5_unique_draws],
            "vertices": visible_vertices,
        },
        "presented_log_required": presented_log_required,
        "present_tie_ok": present_tie_ok,
        "required_files": required_files,
        "pso_manifests": pso_records,
        "dxil_pso_manifest": dxil_pso,
        "sm5_pso_manifest": sm5_pso,
        "msl_checks": msl_checks,
        "metallib_policy": metallib_policy,
        "errors": errors,
    }


def run_fresh_game(
    repo: Path,
    proof_dir: Path,
    wine: Path,
    wine_runtime: Path,
    prefix: Path,
    runtime_root: Path,
    staged_files: list[dict[str, Any]],
    corpus_tsv: Path,
    visible_frames: int,
) -> dict[str, Any]:
    sdk = repo / "tools" / "d3d12-metal-sdk"
    out_bin = sdk / "out" / "bin"
    exe = out_bin / "m12_fresh_game.exe"
    run_dir = proof_dir / "phase1-visible-game"
    run_dir.mkdir(parents=True, exist_ok=True)

    dyld_parts = [
        str(runtime_root / "x86_64-unix"),
        str(wine_runtime / "lib" / "wine" / "x86_64-unix"),
    ]
    if os.environ.get("DYLD_LIBRARY_PATH"):
        dyld_parts.append(os.environ["DYLD_LIBRARY_PATH"])

    corpus_hash_validation = validate_fresh_game_corpus_tsv(corpus_tsv)
    corpus_input_dir = run_dir / "corpus-input"
    corpus_input_dir.mkdir(parents=True, exist_ok=True)
    copied_corpus_artifacts: list[dict[str, Any]] = []
    for artifact in [corpus_tsv, corpus_tsv.parent / "fresh-corpus-summary.json", corpus_tsv.parent / "fresh-corpus-manifest.json"]:
        if artifact.exists():
            destination = corpus_input_dir / artifact.name
            shutil.copy2(artifact, destination)
            copied_corpus_artifacts.append(
                {
                    "source": str(artifact),
                    "destination": str(destination),
                    "sha256": sha256(destination),
                    "size": destination.stat().st_size,
                }
            )

    dxil_artifacts = build_fresh_game_dxil(out_bin, run_dir, wine, prefix)

    shader_cache_dir = run_dir / "shader-cache-fresh"
    shader_cache_dir.mkdir(parents=True, exist_ok=True)

    env = os.environ.copy()
    for key in list(env):
        if key.startswith("DXMT_"):
            env.pop(key, None)
    env.update(
        {
            "WINEPREFIX": str(prefix),
            "WINEDLLPATH": str(runtime_root / "x86_64-windows"),
            "WINEDLLOVERRIDES": "d3d12,dxgi,dxgi_dxmt,winemetal=n,b;gameoverlayrenderer,gameoverlayrenderer64=d",
            "DYLD_LIBRARY_PATH": ":".join(dyld_parts),
            "DXMT_WINEMETAL_UNIXLIB": "winemetal.so",
            "DXMT_WINEMETAL_DEBUG": "1",
            "DXMT_LOG_LEVEL": "info",
            "DXMT_LOG_PATH": str(run_dir),
            "DXMT_SHADER_CACHE_PATH": str(shader_cache_dir),
            "D3D12_METAL_SDK_PROFILE": "m12-fresh-visible-game",
            "DXMT_D3D12_PRESENT_LOG_INTERVAL": "1",
            "M12_FRESH_CORPUS_TSV": str(corpus_tsv),
            "M12_FRESH_DXIL_VS": dxil_artifacts["vs_path"],
            "M12_FRESH_DXIL_PS": dxil_artifacts["ps_path"],
            "M12_FRESH_VISIBLE_FRAMES": str(visible_frames),
            "WINEDEBUG": "+loaddll",
        }
    )
    cmd = [str(wine), exe.name]
    proc = subprocess.run(cmd, cwd=out_bin, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout_path = run_dir / "m12_fresh_game.stdout.json"
    stderr_path = run_dir / "m12_fresh_game.stderr.txt"
    stdout_path.write_text(proc.stdout)
    stderr_path.write_text(proc.stderr)

    parsed: dict[str, Any] | None = None
    parse_error = ""
    try:
        parsed = json.loads(proc.stdout)
    except json.JSONDecodeError as error:
        parse_error = str(error)
    loaddll_rows = parse_loaddll(proc.stderr)
    runtime_match = validate_loaddll_staged(loaddll_rows, staged_files)
    drawn_present_count = proc.stderr.count("classification=drawn")
    draw_line_count = len(re.findall(r"draws=[1-9]", proc.stderr))
    present_draw_counts = [
        int(match.group(1))
        for match in re.finditer(r"M12 present backbuffer work count=\d+ .*? draws=(\d+).*?classification=drawn", proc.stderr)
    ]
    dxil_draw_encoded_count = len(re.findall(r"M12 swapchain DrawInstanced encoded v=3 i=1", proc.stderr))
    dxil_vertex_pull_snapshot_count = len(
        re.findall(
            r"M12 vertex-pull snapshot draw=DrawInstanced v=3 i=1 .*?slot_mask=0x1.*?bound_vbs=1",
            proc.stderr,
        )
    )
    dxil_draw_skipped = bool(
        re.search(r"M12 swapchain DrawInstanced skipped v=3\s+i=1|DrawInstanced\s+SKIPPED\s+v=3\s+i=1", proc.stderr,
                  re.IGNORECASE)
    )
    render_encoder_encode_failed = bool(re.search(r"M12 render encoder encode failed", proc.stderr, re.IGNORECASE))
    stderr_assertion = "std::__condvar" in proc.stderr or "Assertion" in proc.stderr
    frames_presented = 0
    if parsed:
        frames_presented = int(parsed.get("d3d12_window", {}).get("frames_presented", 0) or 0)
    corpus_json = parsed.get("corpus", {}) if parsed else {}
    d3d12_json = parsed.get("d3d12_window", {}) if parsed else {}
    adapter_json = d3d12_json.get("adapter_report", {}) if d3d12_json else {}
    gpu_textures_json = d3d12_json.get("gpu_textures", {}) if d3d12_json else {}
    heap_alias_json = d3d12_json.get("heap_alias", {}) if d3d12_json else {}
    uav_barrier_json = d3d12_json.get("uav_barrier", {}) if d3d12_json else {}
    shader_cache_validation = validate_presented_shader_cache(shader_cache_dir, proc.stderr, d3d12_json, visible_frames)
    texture_payload_bytes_required = 300 * 16 * 16 * 4
    game_json_ok = bool(
        parsed
        and parsed.get("pass") is True
        and corpus_json.get("ok") is True
        and int(corpus_json.get("texture_payloads_captured", 0) or 0) >= 300
        and int(corpus_json.get("texture_payload_bytes_from_files", 0) or 0) >= texture_payload_bytes_required
        and d3d12_json.get("ok") is True
        and adapter_json.get("ok") is True
        and adapter_json.get("EnumAdapters1") == "0x00000000"
        and adapter_json.get("GetDesc1") == "0x00000000"
        and int(adapter_json.get("vendor_id", 0) or 0) != 0
        and (int(adapter_json.get("dedicated_video_memory", 0) or 0) + int(adapter_json.get("shared_system_memory", 0) or 0)) > 0
        and adapter_json.get("adapter_luid_nonzero") is True
        and adapter_json.get("device_luid_nonzero") is True
        and adapter_json.get("luid_matches_device") is True
        and d3d12_json.get("visible_scene", {}).get("ok") is True
        and d3d12_json.get("visible_scene", {}).get("present_ok") is True
        and d3d12_json.get("visible_scene", {}).get("sm5_stamp_source") == "DXBC_SM5_DYNAMIC_STAMP_SENTINEL_OVERWRITE"
        and int(d3d12_json.get("visible_scene", {}).get("sm5_stamp_quads_per_frame", 0) or 0) == 2
        and int(d3d12_json.get("visible_scene", {}).get("sm5_stamp_samples_checked", 0) or 0) == visible_frames
        and int(d3d12_json.get("visible_scene", {}).get("sm5_stamp_matches", 0) or 0) == visible_frames
        and d3d12_json.get("dxil_scene", {}).get("ok") is True
        and d3d12_json.get("dxil_scene", {}).get("CreateDxilVertexBuffer") == "0x00000000"
        and d3d12_json.get("dxil_scene", {}).get("vertex_source") == "POSITION_GREEN_SENTINEL_VERTEX_BUFFER_PS_MAGENTA_OVERLAY"
        and int(d3d12_json.get("dxil_scene", {}).get("draw_calls", 0) or 0) == visible_frames
        and int(d3d12_json.get("dxil_scene", {}).get("vertices_per_draw", 0) or 0) == 3
        and d3d12_json.get("dxil_readback", {}).get("ok") is True
        and d3d12_json.get("dxil_readback", {}).get("CreateReadbackBuffer") == "0x00000000"
        and int(d3d12_json.get("dxil_readback", {}).get("copy_commands", 0) or 0) == visible_frames
        and int(d3d12_json.get("dxil_readback", {}).get("sentinel_writes", 0) or 0) == visible_frames
        and int(d3d12_json.get("dxil_readback", {}).get("samples_checked", 0) or 0) == visible_frames
        and int(d3d12_json.get("dxil_readback", {}).get("magenta_samples", 0) or 0) == visible_frames
        and gpu_textures_json.get("ok") is True
        and gpu_textures_json.get("present_ok") is True
        and int(gpu_textures_json.get("texture_payloads_uploaded", 0) or 0) >= 300
        and int(gpu_textures_json.get("texture_payload_bytes_from_files", 0) or 0) >= texture_payload_bytes_required
        and int(gpu_textures_json.get("present_backbuffer_sentinel_copies", 0) or 0) == visible_frames
        and int(gpu_textures_json.get("present_copy_commands", 0) or 0) == visible_frames
        and int(gpu_textures_json.get("present_samples_checked", 0) or 0) == visible_frames
        and int(gpu_textures_json.get("present_sample_matches", 0) or 0) == visible_frames
        and gpu_textures_json.get("present_rgba") == gpu_textures_json.get("present_expected_rgba")
        and heap_alias_json.get("ok") is True
        and heap_alias_json.get("present_ok") is True
        and heap_alias_json.get("CreateHeap") == "0x00000000"
        and heap_alias_json.get("CreatePlacedResourceA") == "0x00000000"
        and heap_alias_json.get("CreatePlacedResourceB") == "0x00000000"
        and int(heap_alias_json.get("gpu_virtual_address_a", 0) or 0) != 0
        and int(heap_alias_json.get("gpu_virtual_address_b", 0) or 0) != 0
        and heap_alias_json.get("gpu_virtual_addresses_match") is True
        and int(heap_alias_json.get("copy_before_alias_commands", 0) or 0) == 1
        and int(heap_alias_json.get("aliasing_barriers", 0) or 0) == 1
        and int(heap_alias_json.get("copy_alias_overlap_commands", 0) or 0) == 1
        and int(heap_alias_json.get("copy_after_alias_commands", 0) or 0) == 1
        and int(heap_alias_json.get("transition_barriers", 0) or 0) == 4
        and heap_alias_json.get("readback_before_alias_ok") is True
        and heap_alias_json.get("readback_alias_overlap_ok") is True
        and heap_alias_json.get("readback_after_alias_ok") is True
        and int(heap_alias_json.get("present_copy_commands", 0) or 0) == visible_frames
        and int(heap_alias_json.get("present_samples_checked", 0) or 0) == visible_frames
        and int(heap_alias_json.get("present_sample_matches", 0) or 0) == visible_frames
        and heap_alias_json.get("present_rgba") == heap_alias_json.get("present_expected_rgba")
        and uav_barrier_json.get("ok") is True
        and uav_barrier_json.get("present_ok") is True
        and uav_barrier_json.get("proof_scope") == "dependent_uav_dispatch_visibility_with_explicit_uav_barriers"
        and uav_barrier_json.get("D3DCompile_loaded") is True
        and uav_barrier_json.get("CSWrite_cs_5_0") == "0x00000000"
        and uav_barrier_json.get("CSTransform_cs_5_0") == "0x00000000"
        and uav_barrier_json.get("D3D12SerializeRootSignature") == "0x00000000"
        and uav_barrier_json.get("CreateRootSignature") == "0x00000000"
        and uav_barrier_json.get("CreateComputePipelineStateWrite") == "0x00000000"
        and uav_barrier_json.get("CreateComputePipelineStateTransform") == "0x00000000"
        and uav_barrier_json.get("CreateUavBuffer") == "0x00000000"
        and uav_barrier_json.get("fixed_footprint_ok") is True
        and int(uav_barrier_json.get("row_pitch", 0) or 0) == 256
        and int(uav_barrier_json.get("footprint_bytes", 0) or 0) >= 4096
        and int(uav_barrier_json.get("uav_gpu_virtual_address", 0) or 0) != 0
        and int(uav_barrier_json.get("root_uav_sets", 0) or 0) == 2
        and int(uav_barrier_json.get("root_constant_sets", 0) or 0) == 0
        and int(uav_barrier_json.get("dispatch_commands", 0) or 0) == 2
        and int(uav_barrier_json.get("dispatch_write_commands", 0) or 0) == 1
        and int(uav_barrier_json.get("dispatch_read_transform_commands", 0) or 0) == 1
        and int(uav_barrier_json.get("dispatch_x", 0) or 0) == 16
        and int(uav_barrier_json.get("dispatch_y", 0) or 0) == 16
        and int(uav_barrier_json.get("uav_barriers", 0) or 0) == 2
        and int(uav_barrier_json.get("transition_barriers", 0) or 0) == 1
        and uav_barrier_json.get("compute_readback_ok") is True
        and int(uav_barrier_json.get("compute_pixels_checked", 0) or 0) == 256
        and int(uav_barrier_json.get("compute_pixel_matches", 0) or 0) == 256
        and int(uav_barrier_json.get("present_copy_commands", 0) or 0) == visible_frames
        and int(uav_barrier_json.get("present_samples_checked", 0) or 0) == visible_frames
        and int(uav_barrier_json.get("present_sample_matches", 0) or 0) == visible_frames
        and int(uav_barrier_json.get("present_pixels_checked", 0) or 0) == visible_frames * 256
        and int(uav_barrier_json.get("present_pixel_matches", 0) or 0) == visible_frames * 256
        and uav_barrier_json.get("present_rgba") == uav_barrier_json.get("present_expected_rgba")
    )
    result = {
        "command": cmd,
        "cwd": str(out_bin),
        "returncode": proc.returncode,
        "stdout": str(stdout_path),
        "stderr": str(stderr_path),
        "json_parse_error": parse_error,
        "game_json": parsed,
        "loaddll": loaddll_rows,
        "runtime_match": runtime_match,
        "dxil_artifacts": dxil_artifacts,
        "shader_cache_dir": str(shader_cache_dir),
        "shader_cache_validation": shader_cache_validation,
        "copied_corpus_artifacts": copied_corpus_artifacts,
        "corpus_hash_validation": corpus_hash_validation,
        "drawn_present_count": drawn_present_count,
        "draw_line_count": draw_line_count,
        "present_draw_counts": present_draw_counts,
        "dxil_draw_encoded_count": dxil_draw_encoded_count,
        "dxil_draw_encoded_required": min(frames_presented, 16),
        "dxil_draw_encoded_log_budget_note": "DXMT swapchain DrawInstanced encoded logs are a capped sample; long runs additionally require every present to report draws>=2.",
        "dxil_vertex_pull_snapshot_count": dxil_vertex_pull_snapshot_count,
        "dxil_vertex_pull_snapshot_required": min(frames_presented, 12),
        "dxil_vertex_pull_snapshot_note": "DXMT vertex-pull snapshot logs are capped; proof requires the DXIL overlay draw to have v=3, slot_mask=0x1, and bound_vbs=1.",
        "dxil_draw_skipped": dxil_draw_skipped,
        "render_encoder_encode_failed": render_encoder_encode_failed,
        "frames_presented": frames_presented,
        "stderr_assertion": stderr_assertion,
        "ok": proc.returncode == 0
        and dxil_artifacts["ok"]
        and game_json_ok
        and corpus_hash_validation["ok"]
        and shader_cache_validation["ok"]
        and runtime_match["ok"]
        and frames_presented == visible_frames
        and drawn_present_count == frames_presented
        and len(present_draw_counts) == frames_presented
        and all(draws >= 2 for draws in present_draw_counts)
        and dxil_draw_encoded_count >= min(frames_presented, 16)
        and dxil_vertex_pull_snapshot_count >= min(frames_presented, 12)
        and not dxil_draw_skipped
        and not render_encoder_encode_failed
        and draw_line_count >= visible_frames
        and not stderr_assertion,
    }
    write_json(run_dir / "phase1-visible-game-summary.json", result)
    return result


def run_identity_probe(
    repo: Path,
    proof_dir: Path,
    wine: Path,
    wine_runtime: Path,
    prefix: Path,
    runtime_root: Path,
    staged_files: list[dict[str, Any]],
) -> dict[str, Any]:
    sdk = repo / "tools" / "d3d12-metal-sdk"
    out_bin = sdk / "out" / "bin"
    exe = out_bin / "probe_m12_runtime_identity.exe"
    run_dir = proof_dir / "phase0-runtime-identity"
    run_dir.mkdir(parents=True, exist_ok=True)

    unix_bridge = (runtime_root / "x86_64-unix" / "winemetal.so").resolve()
    dyld_parts = [
        str(runtime_root / "x86_64-unix"),
        str(wine_runtime / "lib" / "wine" / "x86_64-unix"),
    ]
    if os.environ.get("DYLD_LIBRARY_PATH"):
        dyld_parts.append(os.environ["DYLD_LIBRARY_PATH"])

    env = os.environ.copy()
    env.update(
        {
            "WINEPREFIX": str(prefix),
            "WINEDLLPATH": str(runtime_root / "x86_64-windows"),
            "WINEDLLOVERRIDES": "d3d12,dxgi,dxgi_dxmt,winemetal=n,b;gameoverlayrenderer,gameoverlayrenderer64=d",
            "DYLD_LIBRARY_PATH": ":".join(dyld_parts),
            "DXMT_WINEMETAL_UNIXLIB": unix_bridge.name,
            "DXMT_WINEMETAL_DEBUG": "1",
            "DXMT_LOG_PATH": str(run_dir),
            "D3D12_METAL_SDK_PROFILE": "m12-fresh-proof",
            "WINEDEBUG": "+loaddll",
        }
    )
    cmd = [str(wine), exe.name]
    proc = subprocess.run(cmd, cwd=out_bin, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout_path = run_dir / "probe_m12_runtime_identity.stdout.json"
    stderr_path = run_dir / "probe_m12_runtime_identity.stderr.txt"
    stdout_path.write_text(proc.stdout)
    stderr_path.write_text(proc.stderr)

    parsed: dict[str, Any] | None = None
    parse_error = ""
    try:
        parsed = json.loads(proc.stdout)
    except json.JSONDecodeError as error:
        parse_error = str(error)

    loaddll_rows = parse_loaddll(proc.stderr)
    runtime_match = validate_loaded_runtime(parsed, loaddll_rows, staged_files)
    unix_bridge_match = validate_unix_bridge(parsed, runtime_root, wine_runtime, run_dir / "winemetal-pe-debug.log")
    result = {
        "command": cmd,
        "cwd": str(out_bin),
        "returncode": proc.returncode,
        "stdout": str(stdout_path),
        "stderr": str(stderr_path),
        "json_parse_error": parse_error,
        "probe_json": parsed,
        "loaddll": loaddll_rows,
        "runtime_match": runtime_match,
        "unix_bridge_match": unix_bridge_match,
        "ok": proc.returncode == 0
        and bool(parsed and parsed.get("pass") is True)
        and runtime_match["ok"]
        and unix_bridge_match["ok"],
    }
    write_json(run_dir / "phase0-runtime-identity-summary.json", result)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="Run the fresh M12 proof harness gates.")
    parser.add_argument("--repo", default=str(Path(__file__).resolve().parents[3]))
    parser.add_argument("--proof-root", default="")
    parser.add_argument("--lab-root", default=str(DEFAULT_LAB_ROOT))
    parser.add_argument("--runtime-root", default=os.path.expanduser("~/.metalsharp/runtime/wine/lib/dxmt_m12"))
    parser.add_argument("--wine-runtime", default=os.path.expanduser("~/.metalsharp/runtime/wine"))
    parser.add_argument("--wine", default=os.path.expanduser("~/.metalsharp/runtime/wine/bin/metalsharp-wine"))
    parser.add_argument("--prefix", default=os.path.expanduser("~/.metalsharp/prefix-steam"))
    parser.add_argument("--min-free-gib", type=int, default=50)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "x86_64-w64-mingw32-g++"))
    parser.add_argument("--corpus-tsv", default=os.environ.get("M12_FRESH_CORPUS_TSV", ""))
    parser.add_argument("--visible-frames", type=int, default=600)
    parser.add_argument("--skip-game", action="store_true", help="Skip the visible m12_fresh_game.exe phase.")
    parser.add_argument("--skip-run", action="store_true", help="Build and manifest only; do not execute Wine.")
    args = parser.parse_args()

    repo = Path(args.repo).expanduser().resolve()
    lab_root = Path(args.lab_root).expanduser()
    runtime_root = Path(args.runtime_root).expanduser()
    wine_runtime = Path(args.wine_runtime).expanduser()
    wine = Path(args.wine).expanduser()
    prefix = Path(args.prefix).expanduser()
    proof_root = Path(args.proof_root).expanduser() if args.proof_root else (
        lab_root / "06-results" / "in-progress" / f"m12-fresh-proof-game-harness-{timestamp()}"
    )

    if not lab_root.exists():
        print(f"lab root is missing or unmounted: {lab_root}", file=sys.stderr)
        return 2
    free_gib = disk_free_gib(lab_root)
    disk_guard = {
        "path": str(lab_root),
        "free_gib": free_gib,
        "min_free_gib": args.min_free_gib,
        "ok": free_gib >= args.min_free_gib,
    }
    if not disk_guard["ok"]:
        print(f"disk guard failed: {free_gib} GiB free < {args.min_free_gib} GiB", file=sys.stderr)
        return 2

    lab_resolved = lab_root.resolve()
    proof_resolved = proof_root.resolve(strict=False)
    if not (str(proof_resolved) == str(lab_resolved) or str(proof_resolved).startswith(str(lab_resolved) + os.sep)):
        print(f"proof root must be under lab root: {proof_root} not under {lab_root}", file=sys.stderr)
        return 2
    proof_root.mkdir(parents=True, exist_ok=False)

    runtime_entries, missing_runtime = inspect_runtime(runtime_root)
    manifest = {
        "schema": "metalsharp.m12.fresh.proof-run-manifest.v1",
        "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "policy": {
            "fresh_artifacts_only": True,
            "prior_proof_artifacts_allowed": False,
            "live_game_launch_requires_user_approval": True,
        },
        "repo": {
            "path": str(repo),
            "branch": git_text(repo, "branch", "--show-current"),
            "commit": git_text(repo, "rev-parse", "HEAD"),
            "status_short": git_text(repo, "status", "--short"),
        },
        "proof_root": str(proof_root),
        "disk_guard": disk_guard,
        "runtime_root": str(runtime_root),
        "wine_runtime": str(wine_runtime),
        "wine": str(wine),
        "prefix": str(prefix),
        "runtime_files": runtime_entries,
        "missing_runtime_files": missing_runtime,
    }
    write_json(proof_root / "proof-run-manifest.json", manifest)

    if missing_runtime:
        print("missing runtime files:", file=sys.stderr)
        for item in missing_runtime:
            print(f"  {item}", file=sys.stderr)
        return 3
    if not wine.exists():
        print(f"missing wine binary: {wine}", file=sys.stderr)
        return 4
    if not prefix.exists():
        print(f"missing prefix: {prefix}", file=sys.stderr)
        return 5

    build = build_identity_probe(repo, args.cxx)
    write_json(proof_root / "phase0-build-runtime-identity-probe.json", build)
    if not build["ok"]:
        print(build["stderr"], file=sys.stderr)
        return 6

    game_build = build_fresh_game(repo, args.cxx)
    write_json(proof_root / "phase1-build-m12-fresh-game.json", game_build)
    if not game_build["ok"]:
        print(game_build["stderr"], file=sys.stderr)
        return 9

    staged = copy_runtime_to_probe_dir(runtime_root, repo / "tools" / "d3d12-metal-sdk" / "out" / "bin")
    write_json(
        proof_root / "phase0-staged-runtime-hashes.json",
        {"files": staged, "ok": all(x["hash_match"] for x in staged)},
    )
    if not all(x["hash_match"] for x in staged):
        return 7

    identity_result: dict[str, Any] | None = None
    visible_game_result: dict[str, Any] | None = None
    if not args.skip_run:
        identity_result = run_identity_probe(repo, proof_root, wine, wine_runtime, prefix, runtime_root, staged)
        if not args.skip_game:
            if not args.corpus_tsv:
                print("--corpus-tsv is required unless --skip-game or --skip-run is set", file=sys.stderr)
                return 10
            corpus_tsv = Path(args.corpus_tsv).expanduser().resolve()
            if not corpus_tsv.exists():
                print(f"missing corpus TSV: {corpus_tsv}", file=sys.stderr)
                return 11
            visible_game_result = run_fresh_game(
                repo,
                proof_root,
                wine,
                wine_runtime,
                prefix,
                runtime_root,
                staged,
                corpus_tsv,
                args.visible_frames,
            )

    summary = {
        "schema": "metalsharp.m12.fresh.phase0-summary.v1",
        "ok": disk_guard["ok"]
        and not missing_runtime
        and build["ok"]
        and game_build["ok"]
        and all(x["hash_match"] for x in staged)
        and (args.skip_run or bool(identity_result and identity_result["ok"]))
        and (args.skip_run or args.skip_game or bool(visible_game_result and visible_game_result["ok"])),
        "proof_root": str(proof_root),
        "disk_guard": disk_guard,
        "build_ok": build["ok"],
        "game_build_ok": game_build["ok"],
        "runtime_stage_ok": all(x["hash_match"] for x in staged),
        "identity_probe_ok": None if args.skip_run else bool(identity_result and identity_result["ok"]),
        "visible_game_ok": None
        if args.skip_run or args.skip_game
        else bool(visible_game_result and visible_game_result["ok"]),
        "artifacts": {
            "manifest": str(proof_root / "proof-run-manifest.json"),
            "build": str(proof_root / "phase0-build-runtime-identity-probe.json"),
            "staged_hashes": str(proof_root / "phase0-staged-runtime-hashes.json"),
            "identity": str(proof_root / "phase0-runtime-identity" / "phase0-runtime-identity-summary.json")
            if not args.skip_run
            else None,
            "visible_game": str(proof_root / "phase1-visible-game" / "phase1-visible-game-summary.json")
            if not args.skip_run and not args.skip_game
            else None,
        },
    }
    write_json(proof_root / "phase0-summary.json", summary)
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0 if summary["ok"] else 8


if __name__ == "__main__":
    raise SystemExit(main())
