#!/usr/bin/env python3
"""Probe the Core Convergence C1 libm12core ABI.

This is a native macOS probe, not a Windows D3D12 launch.  It validates that the
new packet-stream and cache-compatibility C ABI is exported, scalar/POD-shaped
from Python ctypes' perspective, and behaves deterministically on synthetic
inputs before C2 feeds it real PE command streams.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import pathlib
import platform
import sys
from typing import Any

M12CORE_ABI_VERSION = 1
M12CORE_BUILD_ID_LOW = 0x4D313243
M12CORE_BUILD_ID_HIGH = 0x00000014
M12CORE_FEATURE_COMMAND_PACKET_STREAM = 1 << 19
M12CORE_FEATURE_CACHE_COMPATIBILITY_KEYS = 1 << 20

M12CORE_COMMAND_PACKET_KIND_UNKNOWN = 0
M12CORE_COMMAND_PACKET_KIND_SET_RENDER_TARGETS = 5
M12CORE_COMMAND_PACKET_KIND_RESOURCE_BARRIER = 6
M12CORE_COMMAND_PACKET_KIND_DRAW = 10
M12CORE_COMMAND_PACKET_KIND_DISPATCH = 12
M12CORE_COMMAND_PACKET_KIND_PRESENT = 14
M12CORE_COMMAND_PACKET_FLAG_GRAPHICS = 1 << 0
M12CORE_COMMAND_PACKET_FLAG_COMPUTE = 1 << 1
M12CORE_COMMAND_PACKET_FLAG_PRESENT = 1 << 3
M12CORE_COMMAND_PACKET_FLAG_UNSUPPORTED = 1 << 8
M12CORE_COMMAND_PACKET_STREAM_STATUS_OK = 0
M12CORE_COMMAND_PACKET_STREAM_STATUS_INVALID = 1
M12CORE_COMMAND_PACKET_STREAM_STATUS_UNSUPPORTED = 2

M12CORE_CACHE_ARTIFACT_UNKNOWN = 0
M12CORE_CACHE_ARTIFACT_RENDER_PSO = 2
M12CORE_CACHE_COMPAT_HAS_APPID = 1 << 0
M12CORE_CACHE_COMPAT_HAS_DEVICE = 1 << 1
M12CORE_CACHE_COMPAT_HAS_RUNTIME = 1 << 2
M12CORE_CACHE_COMPAT_HAS_TRANSLATOR = 1 << 3
M12CORE_CACHE_COMPAT_HAS_ROOT_BINDING = 1 << 4
M12CORE_CACHE_COMPAT_HAS_PIPELINE = 1 << 5
M12CORE_CACHE_COMPAT_HAS_METAL = 1 << 6
M12CORE_CACHE_COMPAT_HAS_SCHEMA = 1 << 7
M12CORE_CACHE_COMPAT_STATUS_OK = 0
M12CORE_CACHE_COMPAT_STATUS_INVALID = 1
M12CORE_CACHE_COMPAT_STATUS_MISSING_DIMENSION = 2


class M12CoreVersion(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("feature_flags", ctypes.c_uint32),
        ("build_id_low", ctypes.c_uint32),
        ("build_id_high", ctypes.c_uint32),
    ]


class M12CoreCommandPacketHeader(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("kind", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("payload_qwords", ctypes.c_uint32),
        ("sequence", ctypes.c_uint64),
    ]


class M12CoreCommandPacket(ctypes.Structure):
    _fields_ = [
        ("header", M12CoreCommandPacketHeader),
        ("object_id0", ctypes.c_uint64),
        ("object_id1", ctypes.c_uint64),
        ("object_id2", ctypes.c_uint64),
        ("object_id3", ctypes.c_uint64),
        ("value0", ctypes.c_uint64),
        ("value1", ctypes.c_uint64),
        ("value2", ctypes.c_uint64),
        ("value3", ctypes.c_uint64),
    ]


class M12CoreCommandPacketStreamDesc(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("packet_count", ctypes.c_uint32),
        ("queue_type", ctypes.c_uint32),
        ("command_list_index", ctypes.c_uint32),
        ("command_list_id", ctypes.c_uint64),
        ("queue_serial", ctypes.c_uint64),
        ("packets", ctypes.POINTER(M12CoreCommandPacket)),
    ]


class M12CoreCommandPacketStreamSummary(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("status", ctypes.c_uint32),
        ("summary_flags", ctypes.c_uint32),
        ("unsupported_packet_count", ctypes.c_uint32),
        ("invalid_packet_count", ctypes.c_uint32),
        ("graphics_packet_count", ctypes.c_uint32),
        ("compute_packet_count", ctypes.c_uint32),
        ("copy_packet_count", ctypes.c_uint32),
        ("present_packet_count", ctypes.c_uint32),
        ("barrier_packet_count", ctypes.c_uint32),
        ("draw_packet_count", ctypes.c_uint32),
        ("dispatch_packet_count", ctypes.c_uint32),
        ("clear_packet_count", ctypes.c_uint32),
        ("binding_packet_count", ctypes.c_uint32),
        ("reserved", ctypes.c_uint32 * 2),
        ("stream_key", ctypes.c_uint64),
        ("packet_sequence_xor", ctypes.c_uint64),
    ]


class M12CoreCacheCompatibilityDesc(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("artifact_kind", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("schema_version", ctypes.c_uint32),
        ("appid", ctypes.c_uint64),
        ("profile_key", ctypes.c_uint64),
        ("device_key", ctypes.c_uint64),
        ("os_metal_key", ctypes.c_uint64),
        ("core_build_key", ctypes.c_uint64),
        ("core_feature_flags", ctypes.c_uint64),
        ("translator_key", ctypes.c_uint64),
        ("root_binding_key", ctypes.c_uint64),
        ("pipeline_key", ctypes.c_uint64),
        ("artifact_key", ctypes.c_uint64),
    ]


class M12CoreCacheCompatibilityKey(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("status", ctypes.c_uint32),
        ("missing_flags", ctypes.c_uint32),
        ("artifact_kind", ctypes.c_uint32),
        ("compatibility_key", ctypes.c_uint64),
        ("invalidation_key", ctypes.c_uint64),
    ]


def default_lib_candidates(repo: pathlib.Path) -> list[pathlib.Path]:
    return [
        pathlib.Path.home() / ".metalsharp/runtime/wine/lib/dxmt_m12/x86_64-unix/libm12core.dylib",
        repo / "vendor/dxmt/build-metalsharp-x64/src/m12core/libm12core.dylib",
    ]


def packet(kind: int, sequence: int, flags: int = 0, value0: int = 0) -> M12CoreCommandPacket:
    p = M12CoreCommandPacket()
    p.header.abi_version = M12CORE_ABI_VERSION
    p.header.kind = kind
    p.header.flags = flags
    p.header.sequence = sequence
    p.value0 = value0
    return p


def summary_to_dict(s: M12CoreCommandPacketStreamSummary) -> dict[str, Any]:
    return {
        "abi_version": s.abi_version,
        "status": s.status,
        "summary_flags": s.summary_flags,
        "unsupported_packet_count": s.unsupported_packet_count,
        "invalid_packet_count": s.invalid_packet_count,
        "graphics_packet_count": s.graphics_packet_count,
        "compute_packet_count": s.compute_packet_count,
        "copy_packet_count": s.copy_packet_count,
        "present_packet_count": s.present_packet_count,
        "barrier_packet_count": s.barrier_packet_count,
        "draw_packet_count": s.draw_packet_count,
        "dispatch_packet_count": s.dispatch_packet_count,
        "clear_packet_count": s.clear_packet_count,
        "binding_packet_count": s.binding_packet_count,
        "stream_key": f"0x{s.stream_key:016x}",
        "packet_sequence_xor": f"0x{s.packet_sequence_xor:016x}",
    }


def cache_key_to_dict(k: M12CoreCacheCompatibilityKey) -> dict[str, Any]:
    return {
        "abi_version": k.abi_version,
        "status": k.status,
        "missing_flags": k.missing_flags,
        "artifact_kind": k.artifact_kind,
        "compatibility_key": f"0x{k.compatibility_key:016x}",
        "invalidation_key": f"0x{k.invalidation_key:016x}",
    }


def run_probe(lib_path: pathlib.Path) -> tuple[bool, dict[str, Any]]:
    lib = ctypes.CDLL(str(lib_path))
    lib.m12core_get_version.argtypes = [ctypes.POINTER(M12CoreVersion)]
    lib.m12core_get_version.restype = ctypes.c_int
    lib.m12core_validate_command_packet_stream.argtypes = [
        ctypes.POINTER(M12CoreCommandPacketStreamDesc),
        ctypes.POINTER(M12CoreCommandPacketStreamSummary),
    ]
    lib.m12core_validate_command_packet_stream.restype = ctypes.c_int
    lib.m12core_make_cache_compatibility_key.argtypes = [
        ctypes.POINTER(M12CoreCacheCompatibilityDesc),
        ctypes.POINTER(M12CoreCacheCompatibilityKey),
    ]
    lib.m12core_make_cache_compatibility_key.restype = ctypes.c_int

    checks: dict[str, bool] = {}
    version = M12CoreVersion()
    version_rc = lib.m12core_get_version(ctypes.byref(version))
    checks["version_rc"] = version_rc == 0
    checks["build_high"] = version.build_id_high == M12CORE_BUILD_ID_HIGH
    checks["packet_feature"] = bool(version.feature_flags & M12CORE_FEATURE_COMMAND_PACKET_STREAM)
    checks["cache_feature"] = bool(version.feature_flags & M12CORE_FEATURE_CACHE_COMPATIBILITY_KEYS)

    packets = (M12CoreCommandPacket * 5)(
        packet(M12CORE_COMMAND_PACKET_KIND_SET_RENDER_TARGETS, 1, M12CORE_COMMAND_PACKET_FLAG_GRAPHICS),
        packet(M12CORE_COMMAND_PACKET_KIND_RESOURCE_BARRIER, 2),
        packet(M12CORE_COMMAND_PACKET_KIND_DRAW, 3, M12CORE_COMMAND_PACKET_FLAG_GRAPHICS),
        packet(M12CORE_COMMAND_PACKET_KIND_DISPATCH, 4, M12CORE_COMMAND_PACKET_FLAG_COMPUTE),
        packet(M12CORE_COMMAND_PACKET_KIND_PRESENT, 5, M12CORE_COMMAND_PACKET_FLAG_PRESENT),
    )
    desc = M12CoreCommandPacketStreamDesc(
        abi_version=M12CORE_ABI_VERSION,
        packet_count=5,
        queue_type=0,
        command_list_index=0,
        command_list_id=0x1234,
        queue_serial=0x55,
        packets=ctypes.cast(packets, ctypes.POINTER(M12CoreCommandPacket)),
    )
    valid_summary = M12CoreCommandPacketStreamSummary()
    valid_rc = lib.m12core_validate_command_packet_stream(ctypes.byref(desc), ctypes.byref(valid_summary))
    checks["valid_stream_rc"] = valid_rc == 0
    checks["valid_stream_status"] = valid_summary.status == M12CORE_COMMAND_PACKET_STREAM_STATUS_OK
    checks["valid_stream_counts"] = (
        valid_summary.draw_packet_count == 1
        and valid_summary.dispatch_packet_count == 1
        and valid_summary.present_packet_count == 1
        and valid_summary.barrier_packet_count == 1
        and valid_summary.stream_key != 0
    )

    unsupported_packets = (M12CoreCommandPacket * 1)(
        packet(M12CORE_COMMAND_PACKET_KIND_DRAW, 7, M12CORE_COMMAND_PACKET_FLAG_UNSUPPORTED)
    )
    unsupported_desc = M12CoreCommandPacketStreamDesc(
        abi_version=M12CORE_ABI_VERSION,
        packet_count=1,
        queue_type=0,
        command_list_index=0,
        command_list_id=0x1234,
        queue_serial=0x56,
        packets=ctypes.cast(unsupported_packets, ctypes.POINTER(M12CoreCommandPacket)),
    )
    unsupported_summary = M12CoreCommandPacketStreamSummary()
    unsupported_rc = lib.m12core_validate_command_packet_stream(
        ctypes.byref(unsupported_desc), ctypes.byref(unsupported_summary)
    )
    checks["unsupported_stream"] = (
        unsupported_rc == 0
        and unsupported_summary.status == M12CORE_COMMAND_PACKET_STREAM_STATUS_UNSUPPORTED
        and unsupported_summary.unsupported_packet_count == 1
    )

    invalid_desc = M12CoreCommandPacketStreamDesc(
        abi_version=M12CORE_ABI_VERSION,
        packet_count=1,
        queue_type=0,
        command_list_index=0,
        command_list_id=0,
        queue_serial=0,
        packets=None,
    )
    invalid_summary = M12CoreCommandPacketStreamSummary()
    invalid_rc = lib.m12core_validate_command_packet_stream(ctypes.byref(invalid_desc), ctypes.byref(invalid_summary))
    checks["invalid_stream"] = (
        invalid_rc == 0
        and invalid_summary.status == M12CORE_COMMAND_PACKET_STREAM_STATUS_INVALID
        and invalid_summary.invalid_packet_count == 1
    )

    full_flags = (
        M12CORE_CACHE_COMPAT_HAS_APPID
        | M12CORE_CACHE_COMPAT_HAS_DEVICE
        | M12CORE_CACHE_COMPAT_HAS_RUNTIME
        | M12CORE_CACHE_COMPAT_HAS_TRANSLATOR
        | M12CORE_CACHE_COMPAT_HAS_ROOT_BINDING
        | M12CORE_CACHE_COMPAT_HAS_PIPELINE
        | M12CORE_CACHE_COMPAT_HAS_METAL
        | M12CORE_CACHE_COMPAT_HAS_SCHEMA
    )
    cache_desc = M12CoreCacheCompatibilityDesc(
        abi_version=M12CORE_ABI_VERSION,
        artifact_kind=M12CORE_CACHE_ARTIFACT_RENDER_PSO,
        flags=full_flags,
        schema_version=1,
        appid=1888160,
        profile_key=0xAC6,
        device_key=0xA17,
        os_metal_key=0xF00D,
        core_build_key=(M12CORE_BUILD_ID_LOW << 32) | M12CORE_BUILD_ID_HIGH,
        core_feature_flags=int(version.feature_flags),
        translator_key=0x51525354,
        root_binding_key=0x1111,
        pipeline_key=0x2222,
        artifact_key=0x3333,
    )
    cache_key = M12CoreCacheCompatibilityKey()
    cache_rc = lib.m12core_make_cache_compatibility_key(ctypes.byref(cache_desc), ctypes.byref(cache_key))
    checks["cache_key_ok"] = (
        cache_rc == 0
        and cache_key.status == M12CORE_CACHE_COMPAT_STATUS_OK
        and cache_key.compatibility_key != 0
        and cache_key.invalidation_key != 0
    )

    changed_desc = M12CoreCacheCompatibilityDesc.from_buffer_copy(cache_desc)
    changed_desc.translator_key ^= 0x100
    changed_key = M12CoreCacheCompatibilityKey()
    changed_rc = lib.m12core_make_cache_compatibility_key(ctypes.byref(changed_desc), ctypes.byref(changed_key))
    checks["cache_key_changes"] = (
        changed_rc == 0
        and changed_key.status == M12CORE_CACHE_COMPAT_STATUS_OK
        and changed_key.compatibility_key != cache_key.compatibility_key
        and changed_key.invalidation_key != cache_key.invalidation_key
    )

    missing_desc = M12CoreCacheCompatibilityDesc.from_buffer_copy(cache_desc)
    missing_desc.flags &= ~M12CORE_CACHE_COMPAT_HAS_TRANSLATOR
    missing_desc.translator_key = 0
    missing_key = M12CoreCacheCompatibilityKey()
    missing_rc = lib.m12core_make_cache_compatibility_key(ctypes.byref(missing_desc), ctypes.byref(missing_key))
    checks["cache_missing_dimension"] = (
        missing_rc == 0
        and missing_key.status == M12CORE_CACHE_COMPAT_STATUS_MISSING_DIMENSION
        and bool(missing_key.missing_flags & M12CORE_CACHE_COMPAT_HAS_TRANSLATOR)
    )

    unknown_desc = M12CoreCacheCompatibilityDesc.from_buffer_copy(cache_desc)
    unknown_desc.artifact_kind = M12CORE_CACHE_ARTIFACT_UNKNOWN
    unknown_key = M12CoreCacheCompatibilityKey()
    unknown_rc = lib.m12core_make_cache_compatibility_key(ctypes.byref(unknown_desc), ctypes.byref(unknown_key))
    checks["cache_unknown_invalid"] = (
        unknown_rc == 0 and unknown_key.status == M12CORE_CACHE_COMPAT_STATUS_INVALID
    )

    result: dict[str, Any] = {
        "schema": "metalsharp.m12.convergence-c1-probe.v1",
        "lib": str(lib_path),
        "ok": all(checks.values()),
        "checks": checks,
        "version": {
            "abi_version": version.abi_version,
            "feature_flags": f"0x{version.feature_flags:08x}",
            "build_id_low": f"0x{version.build_id_low:08x}",
            "build_id_high": f"0x{version.build_id_high:08x}",
        },
        "valid_stream": summary_to_dict(valid_summary),
        "unsupported_stream": summary_to_dict(unsupported_summary),
        "invalid_stream": summary_to_dict(invalid_summary),
        "cache_key": cache_key_to_dict(cache_key),
        "changed_cache_key": cache_key_to_dict(changed_key),
        "missing_cache_key": cache_key_to_dict(missing_key),
        "unknown_cache_key": cache_key_to_dict(unknown_key),
    }
    return bool(result["ok"]), result


def maybe_reexec_x86_64() -> None:
    if platform.machine() not in {"arm64", "arm64e", "aarch64"}:
        return
    if os.environ.get("M12_CONVERGENCE_C1_PROBE_ROSETTA") == "1":
        return
    python = pathlib.Path("/usr/bin/python3")
    if not python.exists():
        return
    env = dict(os.environ)
    env["M12_CONVERGENCE_C1_PROBE_ROSETTA"] = "1"
    os.execvpe("arch", ["arch", "-x86_64", str(python), __file__, *sys.argv[1:]], env)


def main() -> int:
    maybe_reexec_x86_64()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lib", type=pathlib.Path, help="Path to libm12core.dylib")
    parser.add_argument("--json", type=pathlib.Path, help="Write JSON result to this path")
    args = parser.parse_args()

    repo = pathlib.Path(__file__).resolve().parents[3]
    candidates = [args.lib] if args.lib else default_lib_candidates(repo)
    lib_path = next((p for p in candidates if p and p.exists()), None)
    if not lib_path:
        print(json.dumps({"ok": False, "error": "libm12core.dylib not found", "candidates": [str(p) for p in candidates if p]}))
        return 2

    ok, result = run_probe(lib_path)
    text = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(text)
    print(text, end="")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
