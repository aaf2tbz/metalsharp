#!/usr/bin/env python3
"""Decode D3DMetal cached D3D12 root signatures into descriptor layouts.

This is an offline-only oracle helper. It reads copied D3DMetal
rootsignature_cache.bin files and writes compact JSON/Markdown manifests under
--out-dir. It does not write to live M12 shader caches.

The RTS0 payload uses the serialized D3D12 root signature layout with relative
offsets. Version 1 records use D3D12 1.0 descriptor ranges/root descriptors;
version 2 records use D3D12 1.1 descriptor range/root descriptor flags.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path
from typing import Any

GAME_APPIDS = {
    "elden-ring": "1245620",
    "armored-core-vi": "1888160",
    "subnautica-2": "1962700",
}

PARAM_TYPES = {
    0: "descriptor_table",
    1: "32bit_constants",
    2: "cbv",
    3: "srv",
    4: "uav",
}

RANGE_TYPES = {
    0: "srv",
    1: "uav",
    2: "cbv",
    3: "sampler",
}

VISIBILITY = {
    0: "all",
    1: "vertex",
    2: "hull",
    3: "domain",
    4: "geometry",
    5: "pixel",
    6: "amplification",
    7: "mesh",
}

ROOT_FLAGS = {
    0x1: "allow_input_assembler_input_layout",
    0x2: "deny_vertex_shader_root_access",
    0x4: "deny_hull_shader_root_access",
    0x8: "deny_domain_shader_root_access",
    0x10: "deny_geometry_shader_root_access",
    0x20: "deny_pixel_shader_root_access",
    0x40: "allow_stream_output",
    0x80: "local_root_signature",
    0x100: "deny_amplification_shader_root_access",
    0x200: "deny_mesh_shader_root_access",
}

RANGE_FLAGS = {
    0x0: "none",
    0x1: "descriptors_volatile",
    0x2: "data_volatile",
    0x4: "data_static_while_set_at_execute",
    0x8: "data_static",
    0x10000: "descriptors_static_keeping_buffer_bounds_checks",
}

ROOT_DESCRIPTOR_FLAGS = {
    0x0: "none",
    0x2: "data_volatile",
    0x4: "data_static_while_set_at_execute",
    0x8: "data_static",
}

STATIC_FILTERS = {
    0x0: "min_mag_mip_point",
    0x15: "min_mag_mip_linear",
    0x55: "anisotropic",
    0x80: "comparison_min_mag_mip_point",
    0x95: "comparison_min_mag_mip_linear",
    0xd5: "comparison_anisotropic",
}

ADDRESS_MODES = {
    1: "wrap",
    2: "mirror",
    3: "clamp",
    4: "border",
    5: "mirror_once",
}

COMPARISON_FUNCS = {
    1: "never",
    2: "less",
    3: "equal",
    4: "less_equal",
    5: "greater",
    6: "not_equal",
    7: "greater_equal",
    8: "always",
}

BORDER_COLORS = {
    0: "transparent_black",
    1: "opaque_black",
    2: "opaque_white",
}


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--d3dmetal-reference-root", type=Path, required=True)
    p.add_argument("--out-dir", type=Path, required=True)
    return p.parse_args()


def u32(data: bytes, off: int) -> int:
    if off < 0 or off + 4 > len(data):
        raise ValueError(f"u32 out of bounds at {off}")
    return int.from_bytes(data[off:off + 4], "little")


def f32(data: bytes, off: int) -> float:
    if off < 0 or off + 4 > len(data):
        raise ValueError(f"f32 out of bounds at {off}")
    return struct.unpack_from("<f", data, off)[0]


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def flags_to_names(value: int, table: dict[int, str]) -> list[str]:
    if value == 0 and 0 in table:
        return [table[0]]
    names = [name for bit, name in sorted(table.items()) if bit and value & bit]
    known = 0
    for bit in table:
        known |= bit
    unknown = value & ~known
    if unknown:
        names.append(f"unknown_0x{unknown:x}")
    return names or (["none"] if value == 0 else [f"0x{value:x}"])


def dxbc_chunks(blob: bytes) -> list[dict[str, Any]]:
    if len(blob) < 32 or blob[:4] != b"DXBC":
        return []
    total = u32(blob, 24)
    count = u32(blob, 28)
    if total != len(blob) or count > 64 or 32 + 4 * count > total:
        return []
    chunks = []
    for i in range(count):
        off = u32(blob, 32 + i * 4)
        if off + 8 > total:
            return []
        fourcc = blob[off:off + 4].decode("ascii", "replace")
        size = u32(blob, off + 4)
        if off + 8 + size > total:
            return []
        chunks.append({"index": i, "fourcc": fourcc, "offset": off, "size": size})
    return chunks


def scan_root_records(path: Path) -> list[dict[str, Any]]:
    data = path.read_bytes()
    records: list[dict[str, Any]] = []
    start = 0
    while True:
        off = data.find(b"DXBC", start)
        if off < 0:
            break
        if off + 28 > len(data):
            break
        size = int.from_bytes(data[off + 24:off + 28], "little")
        blob = data[off:off + size] if size >= 32 and off + size <= len(data) else b""
        chunks = dxbc_chunks(blob)
        if chunks and any(c["fourcc"] == "RTS0" for c in chunks):
            key_off = off - 20
            key = data[key_off:key_off + 16].hex() if key_off >= 0 else ""
            records.append({
                "ordinal": len(records),
                "record_payload_offset": off,
                "payload_size": size,
                "record_key": key,
                "dxbc_sha256": sha256_bytes(blob),
                "blob": blob,
            })
            start = off + size
        else:
            start = off + 1
    return records


def decode_range(data: bytes, off: int, version: int) -> dict[str, Any]:
    range_type = u32(data, off)
    count = u32(data, off + 4)
    base = u32(data, off + 8)
    space = u32(data, off + 12)
    if version >= 2:
        flags = u32(data, off + 16)
        table_offset = u32(data, off + 20)
    else:
        flags = None
        table_offset = u32(data, off + 16)
    return {
        "type": RANGE_TYPES.get(range_type, f"unknown_{range_type}"),
        "type_value": range_type,
        "num_descriptors": count if count != 0xffffffff else "unbounded",
        "base_shader_register": base,
        "register_space": space,
        "offset_in_descriptors_from_table_start": table_offset if table_offset != 0xffffffff else "append",
        **({"flags": flags, "flag_names": flags_to_names(flags, RANGE_FLAGS)} if flags is not None else {}),
    }


def decode_static_sampler(data: bytes, off: int) -> dict[str, Any]:
    filter_value = u32(data, off)
    address_u = u32(data, off + 4)
    address_v = u32(data, off + 8)
    address_w = u32(data, off + 12)
    comparison = u32(data, off + 24)
    border = u32(data, off + 28)
    shader_register = u32(data, off + 40)
    register_space = u32(data, off + 44)
    visibility = u32(data, off + 48)
    return {
        "filter": STATIC_FILTERS.get(filter_value, f"0x{filter_value:x}"),
        "filter_value": filter_value,
        "address_u": ADDRESS_MODES.get(address_u, f"unknown_{address_u}"),
        "address_v": ADDRESS_MODES.get(address_v, f"unknown_{address_v}"),
        "address_w": ADDRESS_MODES.get(address_w, f"unknown_{address_w}"),
        "mip_lod_bias": f32(data, off + 16),
        "max_anisotropy": u32(data, off + 20),
        "comparison_func": COMPARISON_FUNCS.get(comparison, f"unknown_{comparison}"),
        "border_color": BORDER_COLORS.get(border, f"unknown_{border}"),
        "min_lod": f32(data, off + 32),
        "max_lod": f32(data, off + 36),
        "shader_register": shader_register,
        "register_space": register_space,
        "visibility": VISIBILITY.get(visibility, f"unknown_{visibility}"),
        "visibility_value": visibility,
    }


def decode_rts0(payload: bytes) -> dict[str, Any]:
    if len(payload) < 24:
        raise ValueError("RTS0 payload too small")
    version = u32(payload, 0)
    num_params = u32(payload, 4)
    params_off = u32(payload, 8)
    num_static = u32(payload, 12)
    static_off = u32(payload, 16)
    flags = u32(payload, 20)
    params = []
    range_stride = 24 if version >= 2 else 20
    root_desc_stride = 12 if version >= 2 else 8
    for index in range(num_params):
        param_off = params_off + index * 12
        param_type = u32(payload, param_off)
        visibility = u32(payload, param_off + 4)
        body_off = u32(payload, param_off + 8)
        param: dict[str, Any] = {
            "index": index,
            "type": PARAM_TYPES.get(param_type, f"unknown_{param_type}"),
            "type_value": param_type,
            "visibility": VISIBILITY.get(visibility, f"unknown_{visibility}"),
            "visibility_value": visibility,
            "body_offset": body_off,
        }
        if param_type == 0:
            num_ranges = u32(payload, body_off)
            ranges_off = u32(payload, body_off + 4)
            ranges = [decode_range(payload, ranges_off + i * range_stride, version) for i in range(num_ranges)]
            param.update({"num_ranges": num_ranges, "ranges_offset": ranges_off, "ranges": ranges})
        elif param_type == 1:
            param.update({
                "shader_register": u32(payload, body_off),
                "register_space": u32(payload, body_off + 4),
                "num_32bit_values": u32(payload, body_off + 8),
            })
        elif param_type in (2, 3, 4):
            param.update({
                "shader_register": u32(payload, body_off),
                "register_space": u32(payload, body_off + 4),
            })
            if root_desc_stride == 12:
                desc_flags = u32(payload, body_off + 8)
                param.update({"flags": desc_flags, "flag_names": flags_to_names(desc_flags, ROOT_DESCRIPTOR_FLAGS)})
        params.append(param)
    static_samplers = [decode_static_sampler(payload, static_off + i * 52) for i in range(num_static)]
    return {
        "version": version,
        "num_parameters": num_params,
        "parameters_offset": params_off,
        "num_static_samplers": num_static,
        "static_samplers_offset": static_off,
        "flags": flags,
        "flag_names": flags_to_names(flags, ROOT_FLAGS),
        "parameters": params,
        "static_samplers": static_samplers,
    }


def compact_signature(decoded: dict[str, Any]) -> dict[str, Any]:
    params = []
    for p in decoded["parameters"]:
        item = {"type": p["type"], "visibility": p["visibility"]}
        if p["type"] == "descriptor_table":
            item["ranges"] = [
                {
                    "type": r["type"],
                    "count": r["num_descriptors"],
                    "base": r["base_shader_register"],
                    "space": r["register_space"],
                    "offset": r["offset_in_descriptors_from_table_start"],
                    **({"flags": r.get("flag_names", [])} if "flag_names" in r else {}),
                }
                for r in p.get("ranges", [])
            ]
        elif p["type"] == "32bit_constants":
            item.update({"reg": p.get("shader_register"), "space": p.get("register_space"), "values": p.get("num_32bit_values")})
        elif p["type"] in ("cbv", "srv", "uav"):
            item.update({"reg": p.get("shader_register"), "space": p.get("register_space"), **({"flags": p.get("flag_names", [])} if "flag_names" in p else {})})
        params.append(item)
    return {
        "version": decoded["version"],
        "flags": decoded["flag_names"],
        "parameters": params,
        "static_samplers": [
            {"reg": s["shader_register"], "space": s["register_space"], "visibility": s["visibility"], "filter": s["filter"], "addr": [s["address_u"], s["address_v"], s["address_w"]]}
            for s in decoded.get("static_samplers", [])
        ],
    }


def structural_signature(decoded: dict[str, Any]) -> dict[str, Any]:
    """Return a semantic layout signature across v1/v2 serialization details.

    Root-cache entries and shader-embedded RTS0 chunks may encode the same D3D12
    layout with different serialized versions, range flags, and explicit versus
    append descriptor offsets. This signature preserves register spaces,
    registers, counts, visibilities, root order, root flags, and static sampler
    placement, but normalizes descriptor offsets and intentionally ignores v1.1
    volatility/staticness flags for layout matching.
    """
    params = []
    for p in decoded["parameters"]:
        item = {"type": p["type"], "visibility": p["visibility"]}
        if p["type"] == "descriptor_table":
            ranges = []
            cursor = 0
            for rr in p.get("ranges", []):
                raw_offset = rr["offset_in_descriptors_from_table_start"]
                offset = cursor if raw_offset == "append" else raw_offset
                ranges.append({
                    "type": rr["type"],
                    "count": rr["num_descriptors"],
                    "base": rr["base_shader_register"],
                    "space": rr["register_space"],
                    "offset": offset,
                })
                if isinstance(offset, int) and isinstance(rr["num_descriptors"], int):
                    cursor = offset + rr["num_descriptors"]
            item["ranges"] = ranges
        elif p["type"] == "32bit_constants":
            item.update({"reg": p.get("shader_register"), "space": p.get("register_space"), "values": p.get("num_32bit_values")})
        elif p["type"] in ("cbv", "srv", "uav"):
            item.update({"reg": p.get("shader_register"), "space": p.get("register_space")})
        params.append(item)
    return {
        "flags": decoded["flag_names"],
        "parameters": params,
        "static_samplers": [
            {"reg": s["shader_register"], "space": s["register_space"], "visibility": s["visibility"], "filter": s["filter"], "addr": [s["address_u"], s["address_v"], s["address_w"]]}
            for s in decoded.get("static_samplers", [])
        ],
    }


def decode_root_file(path: Path) -> list[dict[str, Any]]:
    out = []
    for rec in scan_root_records(path):
        blob = rec.pop("blob")
        chunks = dxbc_chunks(blob)
        rts = next(c for c in chunks if c["fourcc"] == "RTS0")
        payload = blob[rts["offset"] + 8:rts["offset"] + 8 + rts["size"]]
        decoded = decode_rts0(payload)
        compact = compact_signature(decoded)
        compact_hash = sha256_bytes(json.dumps(compact, sort_keys=True, separators=(",", ":")).encode())
        structural = structural_signature(decoded)
        structural_hash = sha256_bytes(json.dumps(structural, sort_keys=True, separators=(",", ":")).encode())
        out.append({
            **rec,
            "rts0_size": len(payload),
            "root_signature": decoded,
            "compact_signature": compact,
            "compact_signature_sha256": compact_hash,
            "structural_signature": structural,
            "structural_signature_sha256": structural_hash,
        })
    return out


def write_summary(result: dict[str, Any], path: Path) -> None:
    lines = ["# D3DMetal root signature decode", ""]
    lines.append(f"- D3DMetal reference root: `{result['d3dmetal_reference_root']}`")
    lines.append("")
    lines.append("| Game | Root signatures | Unique compact layouts | Versions | Static samplers | Root params | Descriptor ranges | Root descriptors |")
    lines.append("|---|---:|---:|---|---:|---:|---:|---:|")
    for g in result["games"]:
        s = g["summary"]
        lines.append(f"| {g['game']} | {s['root_signature_records']} | {s['unique_compact_signatures']} | `{s['versions']}` | {s['static_samplers']} | {s['root_parameters']} | {s['descriptor_ranges']} | {s['root_descriptors']} |")
    lines.append("")
    for g in result["games"]:
        lines.append(f"## {g['game']}")
        lines.append("")
        for rec in g["root_signatures"][:20]:
            rs = rec["root_signature"]
            lines.append(f"- key=`{rec['record_key']}` compact=`{rec['compact_signature_sha256'][:16]}` structural=`{rec['structural_signature_sha256'][:16]}` version=`{rs['version']}` params=`{rs['num_parameters']}` static_samplers=`{rs['num_static_samplers']}` flags=`{','.join(rs['flag_names'])}`")
            for p in rs["parameters"][:8]:
                if p["type"] == "descriptor_table":
                    ranges = "; ".join(f"{r['type']} {r['base_shader_register']} space{r['register_space']} count={r['num_descriptors']} off={r['offset_in_descriptors_from_table_start']}" for r in p.get("ranges", []))
                    lines.append(f"  - root[{p['index']}] table vis={p['visibility']} {ranges}")
                elif p["type"] == "32bit_constants":
                    lines.append(f"  - root[{p['index']}] constants vis={p['visibility']} b{p['shader_register']} space{p['register_space']} values={p['num_32bit_values']}")
                else:
                    lines.append(f"  - root[{p['index']}] {p['type']} vis={p['visibility']} reg={p.get('shader_register')} space={p.get('register_space')} flags={','.join(p.get('flag_names', []))}")
            if rs["num_parameters"] > 8:
                lines.append(f"  - ... {rs['num_parameters'] - 8} more root params")
        if len(g["root_signatures"]) > 20:
            lines.append(f"- ... {len(g['root_signatures']) - 20} more root signatures in manifest.json")
        lines.append("")
    path.write_text("\n".join(lines) + "\n")


def main() -> int:
    args = parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    games = []
    for game, appid in GAME_APPIDS.items():
        path = args.d3dmetal_reference_root / game / "shaders.cache" / "MTLGPUFamilyApple9_0" / "rootsignature_cache.bin"
        roots = decode_root_file(path) if path.exists() else []
        versions: dict[str, int] = {}
        unique = set()
        static_count = 0
        param_count = 0
        range_count = 0
        root_desc_count = 0
        for rec in roots:
            rs = rec["root_signature"]
            versions[str(rs["version"])] = versions.get(str(rs["version"]), 0) + 1
            unique.add(rec["structural_signature_sha256"])
            static_count += rs["num_static_samplers"]
            param_count += rs["num_parameters"]
            for p in rs["parameters"]:
                if p["type"] == "descriptor_table":
                    range_count += len(p.get("ranges", []))
                elif p["type"] in ("cbv", "srv", "uav"):
                    root_desc_count += 1
        games.append({
            "game": game,
            "appid": appid,
            "rootsignature_cache": str(path),
            "summary": {
                "root_signature_records": len(roots),
                "unique_compact_signatures": len(unique),
                "versions": versions,
                "static_samplers": static_count,
                "root_parameters": param_count,
                "descriptor_ranges": range_count,
                "root_descriptors": root_desc_count,
            },
            "root_signatures": roots,
        })
    result = {
        "schema": "metalsharp.d3dmetal-root-signature-decode.v1",
        "d3dmetal_reference_root": str(args.d3dmetal_reference_root),
        "games": games,
    }
    (args.out_dir / "manifest.json").write_text(json.dumps(result, indent=2) + "\n")
    write_summary(result, args.out_dir / "summary.md")
    print(args.out_dir / "manifest.json")
    print(args.out_dir / "summary.md")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
