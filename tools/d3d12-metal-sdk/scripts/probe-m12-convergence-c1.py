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
M12CORE_BUILD_ID_HIGH = 0x0000001A
M12CORE_FEATURE_COMMAND_PACKET_STREAM = 1 << 19
M12CORE_FEATURE_CACHE_COMPATIBILITY_KEYS = 1 << 20
M12CORE_FEATURE_COMMAND_PACKET_SHADOW_RECORDING = 1 << 21
M12CORE_FEATURE_CACHE_INDEX_SHADOW = 1 << 22
M12CORE_FEATURE_NATIVE_HANDLE_REGISTRY = 1 << 23
M12CORE_FEATURE_PACKET_SHAPE_CLASSIFIER = 1 << 24
M12CORE_FEATURE_PROBE_REPLAY_EXECUTOR = 1 << 25
M12CORE_FEATURE_ENCODER_OWNERSHIP_PLANNING = 1 << 26
M12CORE_FEATURE_ROOT_BINDING_CACHE_METADATA = 1 << 27
M12CORE_FEATURE_NATIVE_PRESENT_OWNERSHIP = 1 << 28
M12CORE_FEATURE_CACHE_FIRST_WARM_START = 1 << 29
M12CORE_FEATURE_EXPANDED_NATIVE_REPLAY_COVERAGE = 1 << 30

M12CORE_COMMAND_PACKET_KIND_UNKNOWN = 0
M12CORE_COMMAND_PACKET_KIND_SET_PIPELINE = 1
M12CORE_COMMAND_PACKET_KIND_SET_ROOT_BINDING = 4
M12CORE_COMMAND_PACKET_KIND_SET_RENDER_TARGETS = 5
M12CORE_COMMAND_PACKET_KIND_RESOURCE_BARRIER = 6
M12CORE_COMMAND_PACKET_KIND_DRAW = 10
M12CORE_COMMAND_PACKET_KIND_DISPATCH = 12
M12CORE_COMMAND_PACKET_KIND_COPY = 13
M12CORE_COMMAND_PACKET_KIND_PRESENT = 14
M12CORE_COMMAND_PACKET_FLAG_GRAPHICS = 1 << 0
M12CORE_COMMAND_PACKET_FLAG_COMPUTE = 1 << 1
M12CORE_COMMAND_PACKET_FLAG_COPY = 1 << 2
M12CORE_COMMAND_PACKET_FLAG_PRESENT = 1 << 3
M12CORE_COMMAND_PACKET_FLAG_USES_RESOURCE = 1 << 4
M12CORE_COMMAND_PACKET_FLAG_USES_PIPELINE = 1 << 5
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

M12CORE_HANDLE_KIND_RESOURCE = 1
M12CORE_HANDLE_KIND_PIPELINE = 2
M12CORE_HANDLE_REGISTRY_STATUS_OK = 0
M12CORE_HANDLE_REGISTRY_STATUS_STALE = 3
M12CORE_PACKET_SUPPORT_STATUS_SAFE = 0
M12CORE_PACKET_SUPPORT_STATUS_UNSUPPORTED = 1
M12CORE_PACKET_SUPPORT_STATUS_INVALID = 2
M12CORE_PACKET_UNSUPPORTED_INDIRECT = 1 << 0
M12CORE_PACKET_UNSUPPORTED_COPY = 1 << 1
M12CORE_PACKET_UNSUPPORTED_STALE_HANDLE = 1 << 5
M12CORE_PACKET_UNSUPPORTED_MISSING_NATIVE_ID = 1 << 6

M12CORE_REPLAY_PACKET_EXECUTE_GATE_ENABLED = 1 << 0
M12CORE_REPLAY_PACKET_EXECUTE_ALLOW_PROBE_NATIVE = 1 << 1
M12CORE_REPLAY_PACKET_EXECUTE_SUMMARY_ELIGIBLE = 1 << 1
M12CORE_REPLAY_PACKET_EXECUTE_SUMMARY_NATIVE_EXECUTED = 1 << 2
M12CORE_REPLAY_PACKET_EXECUTE_SUMMARY_WHOLE_LIST_FALLBACK = 1 << 3
M12CORE_REPLAY_PACKET_EXECUTE_FALLBACK_NONE = 0
M12CORE_REPLAY_PACKET_EXECUTE_FALLBACK_GATE_DISABLED = 1
M12CORE_REPLAY_PACKET_EXECUTE_FALLBACK_UNSUPPORTED_SHAPE = 4

M12CORE_ENCODER_OWNERSHIP_GATE_ENABLED = 1 << 0
M12CORE_ENCODER_OWNERSHIP_REPLAY_NATIVE_EXECUTED = 1 << 1
M12CORE_ENCODER_OWNERSHIP_HAS_RENDER_TARGETS = 1 << 2
M12CORE_ENCODER_OWNERSHIP_HAS_RESOURCE_LAYOUT_VALIDATION = 1 << 5
M12CORE_ENCODER_OWNERSHIP_SUMMARY_NATIVE_ENCODER_OWNED = 1 << 0
M12CORE_ENCODER_OWNERSHIP_SUMMARY_ROOT_BINDING_CACHE_WRITTEN = 1 << 1
M12CORE_ENCODER_OWNERSHIP_SUMMARY_LAYOUT_VALIDATION_REQUIRED = 1 << 2
M12CORE_ENCODER_OWNERSHIP_SUMMARY_CACHE_PAYLOAD_REUSE_DISABLED = 1 << 3

M12CORE_NATIVE_PRESENT_OWNERSHIP_GATE_ENABLED = 1 << 0
M12CORE_NATIVE_PRESENT_OWNERSHIP_HAS_SOURCE_TEXTURE = 1 << 1
M12CORE_NATIVE_PRESENT_OWNERSHIP_HAS_DRAWABLE = 1 << 2
M12CORE_NATIVE_PRESENT_OWNERSHIP_USES_RAW_BLIT = 1 << 3
M12CORE_NATIVE_PRESENT_OWNERSHIP_USES_PRESENTER = 1 << 4
M12CORE_NATIVE_PRESENT_OWNERSHIP_PRESENT_EXECUTE_SUPPORTED = 1 << 5
M12CORE_NATIVE_PRESENT_OWNERSHIP_PRESENT_EXECUTED = 1 << 6
M12CORE_NATIVE_PRESENT_OWNERSHIP_PE_PRESENT_ALREADY_SCHEDULED = 1 << 7
M12CORE_NATIVE_PRESENT_OWNERSHIP_FORMAT_SUPPORTED = 1 << 8
M12CORE_NATIVE_PRESENT_OWNERSHIP_SUMMARY_NATIVE_OWNED = 1 << 1
M12CORE_NATIVE_PRESENT_OWNERSHIP_SUMMARY_TRANSPORT_THIN = 1 << 2
M12CORE_NATIVE_PRESENT_OWNERSHIP_SUMMARY_WHOLE_PRESENT_FALLBACK = 1 << 3
M12CORE_NATIVE_PRESENT_OWNERSHIP_SUMMARY_DOUBLE_PRESENT_PREVENTED = 1 << 4
M12CORE_NATIVE_PRESENT_OWNERSHIP_SUMMARY_DOUBLE_BLIT_PREVENTED = 1 << 5
M12CORE_NATIVE_PRESENT_OWNERSHIP_FALLBACK_GATE_DISABLED = 1
M12CORE_NATIVE_PRESENT_OWNERSHIP_FALLBACK_NON_RAW_PATH = 4

M12CORE_CACHE_WARM_START_HAS_COMPATIBILITY_KEY = 1 << 0
M12CORE_CACHE_WARM_START_HAS_INVALIDATION_PROOF = 1 << 1
M12CORE_CACHE_WARM_START_SHADER_CACHE_HIT = 1 << 2
M12CORE_CACHE_WARM_START_PIPELINE_CACHE_HIT = 1 << 3
M12CORE_CACHE_WARM_START_PREWARM_REQUESTED = 1 << 4
M12CORE_CACHE_WARM_START_FORCE_SOURCE_COMPILE = 1 << 5
M12CORE_CACHE_WARM_START_SUMMARY_CACHE_FIRST_ENABLED = 1 << 0
M12CORE_CACHE_WARM_START_SUMMARY_SHADER_WORK_SKIPPED = 1 << 1
M12CORE_CACHE_WARM_START_SUMMARY_PSO_WORK_SKIPPED = 1 << 2
M12CORE_CACHE_WARM_START_SUMMARY_PREWARM_WORK_SKIPPED = 1 << 3
M12CORE_CACHE_WARM_START_SUMMARY_INVALIDATION_PROVEN = 1 << 4
M12CORE_CACHE_WARM_START_SUMMARY_FALLBACK_REQUIRED = 1 << 5
M12CORE_CACHE_WARM_START_FALLBACK_FORCE_SOURCE = 2
M12CORE_CACHE_WARM_START_FALLBACK_MISSING_INVALIDATION_PROOF = 4
M12CORE_CACHE_WARM_START_FALLBACK_CACHE_MISS = 5

M12CORE_REPLAY_COVERAGE_GATE_ENABLED = 1 << 0
M12CORE_REPLAY_COVERAGE_PACKET_STREAM_VALID = 1 << 1
M12CORE_REPLAY_COVERAGE_SHAPE_SAFE = 1 << 2
M12CORE_REPLAY_COVERAGE_PE_COM_FACADE_PRESENT = 1 << 5
M12CORE_REPLAY_COVERAGE_SUMMARY_NATIVE_COVERAGE_PLANNED = 1 << 0
M12CORE_REPLAY_COVERAGE_SUMMARY_PE_FALLBACK_REQUIRED = 1 << 1
M12CORE_REPLAY_COVERAGE_SUMMARY_POLICY_NATIVE = 1 << 2
M12CORE_REPLAY_COVERAGE_SUMMARY_COM_FACADE_PRESERVED = 1 << 3
M12CORE_REPLAY_COVERAGE_SUMMARY_TRANSPORT_THIN = 1 << 4


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


class M12CoreHandleRegistryDesc(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("kind", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("generation", ctypes.c_uint32),
        ("source_key", ctypes.c_uint64),
        ("aux_key0", ctypes.c_uint64),
        ("aux_key1", ctypes.c_uint64),
        ("owner_key", ctypes.c_uint64),
    ]


class M12CoreHandleRegistryResult(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("status", ctypes.c_uint32),
        ("kind", ctypes.c_uint32),
        ("generation", ctypes.c_uint32),
        ("registry_id", ctypes.c_uint64),
        ("handle_key", ctypes.c_uint64),
    ]


class M12CoreHandleValidationDesc(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("expected_kind", ctypes.c_uint32),
        ("expected_generation", ctypes.c_uint32),
        ("reserved", ctypes.c_uint32),
        ("registry_id", ctypes.c_uint64),
    ]


class M12CoreHandleValidationResult(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("status", ctypes.c_uint32),
        ("actual_kind", ctypes.c_uint32),
        ("actual_generation", ctypes.c_uint32),
        ("registry_id", ctypes.c_uint64),
    ]


class M12CorePacketSupportDesc(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("packet_count", ctypes.c_uint32),
        ("queue_type", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("stream_key", ctypes.c_uint64),
        ("packet_sequence_xor", ctypes.c_uint64),
        ("packets", ctypes.POINTER(M12CoreCommandPacket)),
    ]


class M12CorePacketSupportSummary(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("status", ctypes.c_uint32),
        ("unsupported_reason_flags", ctypes.c_uint32),
        ("unsupported_shape_count", ctypes.c_uint32),
        ("invalid_packet_count", ctypes.c_uint32),
        ("stale_handle_count", ctypes.c_uint32),
        ("missing_native_id_count", ctypes.c_uint32),
        ("safe_for_probe_replay", ctypes.c_uint32),
        ("negative_cache_key", ctypes.c_uint64),
        ("shape_key", ctypes.c_uint64),
    ]


class M12CoreReplayPacketExecuteDesc(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("packet_count", ctypes.c_uint32),
        ("queue_type", ctypes.c_uint32),
        ("command_list_index", ctypes.c_uint32),
        ("reserved0", ctypes.c_uint32),
        ("command_list_id", ctypes.c_uint64),
        ("queue_serial", ctypes.c_uint64),
        ("stream_key", ctypes.c_uint64),
        ("packet_sequence_xor", ctypes.c_uint64),
        ("packets", ctypes.POINTER(M12CoreCommandPacket)),
    ]


class M12CoreReplayPacketExecuteSummary(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("status", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("fallback_reason", ctypes.c_uint32),
        ("validation_flags", ctypes.c_uint32),
        ("planned_packet_count", ctypes.c_uint32),
        ("executed_packet_count", ctypes.c_uint32),
        ("binding_packet_count", ctypes.c_uint32),
        ("clear_packet_count", ctypes.c_uint32),
        ("draw_packet_count", ctypes.c_uint32),
        ("dispatch_packet_count", ctypes.c_uint32),
        ("barrier_packet_count", ctypes.c_uint32),
        ("render_target_packet_count", ctypes.c_uint32),
        ("fallback_packet_index", ctypes.c_uint32),
        ("replay_execute_key", ctypes.c_uint64),
        ("native_state_key", ctypes.c_uint64),
    ]


class M12CoreEncoderOwnershipDesc(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("packet_count", ctypes.c_uint32),
        ("queue_type", ctypes.c_uint32),
        ("command_list_index", ctypes.c_uint32),
        ("render_target_count", ctypes.c_uint32),
        ("descriptor_heap_count", ctypes.c_uint32),
        ("reserved0", ctypes.c_uint32),
        ("command_list_id", ctypes.c_uint64),
        ("queue_serial", ctypes.c_uint64),
        ("stream_key", ctypes.c_uint64),
        ("packet_sequence_xor", ctypes.c_uint64),
        ("replay_execute_key", ctypes.c_uint64),
        ("resource_layout_key", ctypes.c_uint64),
        ("packets", ctypes.POINTER(M12CoreCommandPacket)),
    ]


class M12CoreEncoderOwnershipSummary(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("status", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("validation_flags", ctypes.c_uint32),
        ("planned_encoder_count", ctypes.c_uint32),
        ("encoder_open_count", ctypes.c_uint32),
        ("encoder_close_count", ctypes.c_uint32),
        ("render_encoder_count", ctypes.c_uint32),
        ("compute_encoder_count", ctypes.c_uint32),
        ("blit_encoder_count", ctypes.c_uint32),
        ("binding_cache_entry_count", ctypes.c_uint32),
        ("cache_payload_reuse_enabled", ctypes.c_uint32),
        ("resource_layout_validation_required", ctypes.c_uint32),
        ("reserved0", ctypes.c_uint32),
        ("encoder_plan_key", ctypes.c_uint64),
        ("root_binding_cache_key", ctypes.c_uint64),
        ("binding_layout_key", ctypes.c_uint64),
    ]


class M12CoreNativePresentOwnershipDesc(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("width", ctypes.c_uint32),
        ("height", ctypes.c_uint32),
        ("format", ctypes.c_uint32),
        ("buffer_index", ctypes.c_uint32),
        ("buffer_count", ctypes.c_uint32),
        ("sync_interval", ctypes.c_uint32),
        ("present_flags", ctypes.c_uint32),
        ("work_classification", ctypes.c_uint32),
        ("command_buffer_status", ctypes.c_uint32),
        ("reserved0", ctypes.c_uint32),
        ("present_count", ctypes.c_uint64),
        ("present_execute_key", ctypes.c_uint64),
        ("source_texture_key", ctypes.c_uint64),
        ("drawable_texture_key", ctypes.c_uint64),
        ("queue_serial", ctypes.c_uint64),
        ("command_count", ctypes.c_uint64),
        ("draw_count", ctypes.c_uint64),
        ("dispatch_count", ctypes.c_uint64),
        ("clear_count", ctypes.c_uint64),
        ("wait_seq", ctypes.c_uint64),
    ]


class M12CoreNativePresentOwnershipSummary(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("status", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("fallback_reason", ctypes.c_uint32),
        ("validation_flags", ctypes.c_uint32),
        ("planned_blit_count", ctypes.c_uint32),
        ("planned_present_count", ctypes.c_uint32),
        ("pe_present_required", ctypes.c_uint32),
        ("native_present_owned", ctypes.c_uint32),
        ("transport_thin", ctypes.c_uint32),
        ("double_present_prevented", ctypes.c_uint32),
        ("double_blit_prevented", ctypes.c_uint32),
        ("ownership_key", ctypes.c_uint64),
        ("sequencing_key", ctypes.c_uint64),
        ("transport_key", ctypes.c_uint64),
    ]


class M12CoreCacheWarmStartDesc(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("shader_request_count", ctypes.c_uint32),
        ("pipeline_request_count", ctypes.c_uint32),
        ("prewarm_request_count", ctypes.c_uint32),
        ("compatible_shader_hit_count", ctypes.c_uint32),
        ("compatible_pipeline_hit_count", ctypes.c_uint32),
        ("invalidated_entry_count", ctypes.c_uint32),
        ("compatibility_key", ctypes.c_uint64),
        ("invalidation_key", ctypes.c_uint64),
        ("prewarm_pack_key", ctypes.c_uint64),
        ("root_binding_cache_key", ctypes.c_uint64),
        ("pipeline_cache_key", ctypes.c_uint64),
        ("shader_cache_key", ctypes.c_uint64),
    ]


class M12CoreCacheWarmStartSummary(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("status", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("fallback_reason", ctypes.c_uint32),
        ("shader_work_skipped", ctypes.c_uint32),
        ("pipeline_work_skipped", ctypes.c_uint32),
        ("prewarm_work_skipped", ctypes.c_uint32),
        ("fallback_shader_work", ctypes.c_uint32),
        ("fallback_pipeline_work", ctypes.c_uint32),
        ("invalidated_entry_count", ctypes.c_uint32),
        ("cache_hit_count", ctypes.c_uint32),
        ("cache_miss_count", ctypes.c_uint32),
        ("warm_start_key", ctypes.c_uint64),
        ("skip_work_key", ctypes.c_uint64),
        ("invalidation_proof_key", ctypes.c_uint64),
    ]


class M12CoreReplayCoverageDesc(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("packet_count", ctypes.c_uint32),
        ("unsupported_reason_flags", ctypes.c_uint32),
        ("graphics_packet_count", ctypes.c_uint32),
        ("compute_packet_count", ctypes.c_uint32),
        ("copy_packet_count", ctypes.c_uint32),
        ("barrier_packet_count", ctypes.c_uint32),
        ("binding_packet_count", ctypes.c_uint32),
        ("draw_packet_count", ctypes.c_uint32),
        ("dispatch_packet_count", ctypes.c_uint32),
        ("clear_packet_count", ctypes.c_uint32),
        ("render_target_packet_count", ctypes.c_uint32),
        ("invalid_packet_count", ctypes.c_uint32),
        ("stale_handle_count", ctypes.c_uint32),
        ("missing_native_id_count", ctypes.c_uint32),
        ("stream_key", ctypes.c_uint64),
        ("shape_key", ctypes.c_uint64),
        ("replay_execute_key", ctypes.c_uint64),
        ("encoder_plan_key", ctypes.c_uint64),
    ]


class M12CoreReplayCoverageSummary(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("status", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("native_covered_packet_count", ctypes.c_uint32),
        ("pe_fallback_packet_count", ctypes.c_uint32),
        ("unsupported_packet_count", ctypes.c_uint32),
        ("policy_native_count", ctypes.c_uint32),
        ("policy_pe_count", ctypes.c_uint32),
        ("com_facade_preserved", ctypes.c_uint32),
        ("transport_thin", ctypes.c_uint32),
        ("reserved0", ctypes.c_uint32),
        ("reserved1", ctypes.c_uint32),
        ("coverage_key", ctypes.c_uint64),
        ("policy_key", ctypes.c_uint64),
        ("fallback_key", ctypes.c_uint64),
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


def handle_result_to_dict(r: M12CoreHandleRegistryResult) -> dict[str, Any]:
    return {
        "abi_version": r.abi_version,
        "status": r.status,
        "kind": r.kind,
        "generation": r.generation,
        "registry_id": f"0x{r.registry_id:016x}",
        "handle_key": f"0x{r.handle_key:016x}",
    }


def handle_validation_to_dict(r: M12CoreHandleValidationResult) -> dict[str, Any]:
    return {
        "abi_version": r.abi_version,
        "status": r.status,
        "actual_kind": r.actual_kind,
        "actual_generation": r.actual_generation,
        "registry_id": f"0x{r.registry_id:016x}",
    }


def support_to_dict(s: M12CorePacketSupportSummary) -> dict[str, Any]:
    return {
        "abi_version": s.abi_version,
        "status": s.status,
        "unsupported_reason_flags": s.unsupported_reason_flags,
        "unsupported_shape_count": s.unsupported_shape_count,
        "invalid_packet_count": s.invalid_packet_count,
        "stale_handle_count": s.stale_handle_count,
        "missing_native_id_count": s.missing_native_id_count,
        "safe_for_probe_replay": s.safe_for_probe_replay,
        "negative_cache_key": f"0x{s.negative_cache_key:016x}",
        "shape_key": f"0x{s.shape_key:016x}",
    }


def replay_execute_to_dict(s: M12CoreReplayPacketExecuteSummary) -> dict[str, Any]:
    return {
        "abi_version": s.abi_version,
        "status": s.status,
        "flags": s.flags,
        "fallback_reason": s.fallback_reason,
        "validation_flags": s.validation_flags,
        "planned_packet_count": s.planned_packet_count,
        "executed_packet_count": s.executed_packet_count,
        "binding_packet_count": s.binding_packet_count,
        "clear_packet_count": s.clear_packet_count,
        "draw_packet_count": s.draw_packet_count,
        "dispatch_packet_count": s.dispatch_packet_count,
        "barrier_packet_count": s.barrier_packet_count,
        "render_target_packet_count": s.render_target_packet_count,
        "fallback_packet_index": s.fallback_packet_index,
        "replay_execute_key": f"0x{s.replay_execute_key:016x}",
        "native_state_key": f"0x{s.native_state_key:016x}",
    }


def encoder_to_dict(s: M12CoreEncoderOwnershipSummary) -> dict[str, Any]:
    return {
        "abi_version": s.abi_version,
        "status": s.status,
        "flags": s.flags,
        "validation_flags": s.validation_flags,
        "planned_encoder_count": s.planned_encoder_count,
        "encoder_open_count": s.encoder_open_count,
        "encoder_close_count": s.encoder_close_count,
        "render_encoder_count": s.render_encoder_count,
        "compute_encoder_count": s.compute_encoder_count,
        "blit_encoder_count": s.blit_encoder_count,
        "binding_cache_entry_count": s.binding_cache_entry_count,
        "cache_payload_reuse_enabled": s.cache_payload_reuse_enabled,
        "resource_layout_validation_required": s.resource_layout_validation_required,
        "encoder_plan_key": f"0x{s.encoder_plan_key:016x}",
        "root_binding_cache_key": f"0x{s.root_binding_cache_key:016x}",
        "binding_layout_key": f"0x{s.binding_layout_key:016x}",
    }


def present_ownership_to_dict(s: M12CoreNativePresentOwnershipSummary) -> dict[str, Any]:
    return {
        "abi_version": s.abi_version,
        "status": s.status,
        "flags": s.flags,
        "fallback_reason": s.fallback_reason,
        "validation_flags": s.validation_flags,
        "planned_blit_count": s.planned_blit_count,
        "planned_present_count": s.planned_present_count,
        "pe_present_required": s.pe_present_required,
        "native_present_owned": s.native_present_owned,
        "transport_thin": s.transport_thin,
        "double_present_prevented": s.double_present_prevented,
        "double_blit_prevented": s.double_blit_prevented,
        "ownership_key": f"0x{s.ownership_key:016x}",
        "sequencing_key": f"0x{s.sequencing_key:016x}",
        "transport_key": f"0x{s.transport_key:016x}",
    }


def warm_start_to_dict(s: M12CoreCacheWarmStartSummary) -> dict[str, Any]:
    return {
        "abi_version": s.abi_version,
        "status": s.status,
        "flags": s.flags,
        "fallback_reason": s.fallback_reason,
        "shader_work_skipped": s.shader_work_skipped,
        "pipeline_work_skipped": s.pipeline_work_skipped,
        "prewarm_work_skipped": s.prewarm_work_skipped,
        "fallback_shader_work": s.fallback_shader_work,
        "fallback_pipeline_work": s.fallback_pipeline_work,
        "invalidated_entry_count": s.invalidated_entry_count,
        "cache_hit_count": s.cache_hit_count,
        "cache_miss_count": s.cache_miss_count,
        "warm_start_key": f"0x{s.warm_start_key:016x}",
        "skip_work_key": f"0x{s.skip_work_key:016x}",
        "invalidation_proof_key": f"0x{s.invalidation_proof_key:016x}",
    }


def replay_coverage_to_dict(s: M12CoreReplayCoverageSummary) -> dict[str, Any]:
    return {
        "abi_version": s.abi_version,
        "status": s.status,
        "flags": s.flags,
        "native_covered_packet_count": s.native_covered_packet_count,
        "pe_fallback_packet_count": s.pe_fallback_packet_count,
        "unsupported_packet_count": s.unsupported_packet_count,
        "policy_native_count": s.policy_native_count,
        "policy_pe_count": s.policy_pe_count,
        "com_facade_preserved": s.com_facade_preserved,
        "transport_thin": s.transport_thin,
        "coverage_key": f"0x{s.coverage_key:016x}",
        "policy_key": f"0x{s.policy_key:016x}",
        "fallback_key": f"0x{s.fallback_key:016x}",
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
    lib.m12core_register_handle.argtypes = [
        ctypes.POINTER(M12CoreHandleRegistryDesc),
        ctypes.POINTER(M12CoreHandleRegistryResult),
    ]
    lib.m12core_register_handle.restype = ctypes.c_int
    lib.m12core_validate_handle.argtypes = [
        ctypes.POINTER(M12CoreHandleValidationDesc),
        ctypes.POINTER(M12CoreHandleValidationResult),
    ]
    lib.m12core_validate_handle.restype = ctypes.c_int
    lib.m12core_classify_packet_support.argtypes = [
        ctypes.POINTER(M12CorePacketSupportDesc),
        ctypes.POINTER(M12CorePacketSupportSummary),
    ]
    lib.m12core_classify_packet_support.restype = ctypes.c_int
    lib.m12core_execute_replay_packet_stream.argtypes = [
        ctypes.POINTER(M12CoreReplayPacketExecuteDesc),
        ctypes.POINTER(M12CoreReplayPacketExecuteSummary),
    ]
    lib.m12core_execute_replay_packet_stream.restype = ctypes.c_int
    lib.m12core_plan_encoder_ownership.argtypes = [
        ctypes.POINTER(M12CoreEncoderOwnershipDesc),
        ctypes.POINTER(M12CoreEncoderOwnershipSummary),
    ]
    lib.m12core_plan_encoder_ownership.restype = ctypes.c_int
    lib.m12core_plan_native_present_ownership.argtypes = [
        ctypes.POINTER(M12CoreNativePresentOwnershipDesc),
        ctypes.POINTER(M12CoreNativePresentOwnershipSummary),
    ]
    lib.m12core_plan_native_present_ownership.restype = ctypes.c_int
    lib.m12core_plan_cache_warm_start.argtypes = [
        ctypes.POINTER(M12CoreCacheWarmStartDesc),
        ctypes.POINTER(M12CoreCacheWarmStartSummary),
    ]
    lib.m12core_plan_cache_warm_start.restype = ctypes.c_int
    lib.m12core_plan_replay_coverage.argtypes = [
        ctypes.POINTER(M12CoreReplayCoverageDesc),
        ctypes.POINTER(M12CoreReplayCoverageSummary),
    ]
    lib.m12core_plan_replay_coverage.restype = ctypes.c_int

    checks: dict[str, bool] = {}
    version = M12CoreVersion()
    version_rc = lib.m12core_get_version(ctypes.byref(version))
    checks["version_rc"] = version_rc == 0
    checks["build_high"] = version.build_id_high == M12CORE_BUILD_ID_HIGH
    checks["packet_feature"] = bool(version.feature_flags & M12CORE_FEATURE_COMMAND_PACKET_STREAM)
    checks["cache_feature"] = bool(version.feature_flags & M12CORE_FEATURE_CACHE_COMPATIBILITY_KEYS)
    checks["packet_shadow_feature"] = bool(version.feature_flags & M12CORE_FEATURE_COMMAND_PACKET_SHADOW_RECORDING)
    checks["cache_index_shadow_feature"] = bool(version.feature_flags & M12CORE_FEATURE_CACHE_INDEX_SHADOW)
    checks["handle_registry_feature"] = bool(version.feature_flags & M12CORE_FEATURE_NATIVE_HANDLE_REGISTRY)
    checks["shape_classifier_feature"] = bool(version.feature_flags & M12CORE_FEATURE_PACKET_SHAPE_CLASSIFIER)
    checks["probe_replay_executor_feature"] = bool(version.feature_flags & M12CORE_FEATURE_PROBE_REPLAY_EXECUTOR)
    checks["encoder_ownership_feature"] = bool(version.feature_flags & M12CORE_FEATURE_ENCODER_OWNERSHIP_PLANNING)
    checks["root_binding_cache_metadata_feature"] = bool(
        version.feature_flags & M12CORE_FEATURE_ROOT_BINDING_CACHE_METADATA
    )
    checks["native_present_ownership_feature"] = bool(
        version.feature_flags & M12CORE_FEATURE_NATIVE_PRESENT_OWNERSHIP
    )
    checks["cache_first_warm_start_feature"] = bool(
        version.feature_flags & M12CORE_FEATURE_CACHE_FIRST_WARM_START
    )
    checks["expanded_native_replay_coverage_feature"] = bool(
        version.feature_flags & M12CORE_FEATURE_EXPANDED_NATIVE_REPLAY_COVERAGE
    )

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

    handle_desc = M12CoreHandleRegistryDesc(
        abi_version=M12CORE_ABI_VERSION,
        kind=M12CORE_HANDLE_KIND_PIPELINE,
        flags=0,
        generation=1,
        source_key=0xABCDEF,
        aux_key0=0x10,
        aux_key1=0x20,
        owner_key=0x30,
    )
    handle_result = M12CoreHandleRegistryResult()
    handle_rc = lib.m12core_register_handle(ctypes.byref(handle_desc), ctypes.byref(handle_result))
    checks["handle_register_ok"] = (
        handle_rc == 0
        and handle_result.status == M12CORE_HANDLE_REGISTRY_STATUS_OK
        and handle_result.registry_id != 0
    )

    validation_desc = M12CoreHandleValidationDesc(
        abi_version=M12CORE_ABI_VERSION,
        expected_kind=M12CORE_HANDLE_KIND_PIPELINE,
        expected_generation=1,
        reserved=0,
        registry_id=handle_result.registry_id,
    )
    validation_result = M12CoreHandleValidationResult()
    validation_rc = lib.m12core_validate_handle(ctypes.byref(validation_desc), ctypes.byref(validation_result))
    checks["handle_validate_ok"] = (
        validation_rc == 0 and validation_result.status == M12CORE_HANDLE_REGISTRY_STATUS_OK
    )

    stale_desc = M12CoreHandleValidationDesc.from_buffer_copy(validation_desc)
    stale_desc.expected_generation = 2
    stale_result = M12CoreHandleValidationResult()
    stale_rc = lib.m12core_validate_handle(ctypes.byref(stale_desc), ctypes.byref(stale_result))
    checks["handle_stale_detected"] = (
        stale_rc == 0 and stale_result.status == M12CORE_HANDLE_REGISTRY_STATUS_STALE
    )

    safe_packets = (M12CoreCommandPacket * 2)(
        packet(
            M12CORE_COMMAND_PACKET_KIND_SET_PIPELINE,
            11,
            M12CORE_COMMAND_PACKET_FLAG_USES_PIPELINE,
        ),
        packet(
            M12CORE_COMMAND_PACKET_KIND_DRAW,
            12,
            M12CORE_COMMAND_PACKET_FLAG_GRAPHICS | M12CORE_COMMAND_PACKET_FLAG_USES_PIPELINE,
        ),
    )
    safe_packets[0].object_id0 = handle_result.registry_id
    safe_packets[1].object_id0 = handle_result.registry_id
    safe_support_desc = M12CorePacketSupportDesc(
        abi_version=M12CORE_ABI_VERSION,
        packet_count=2,
        queue_type=0,
        flags=0,
        stream_key=valid_summary.stream_key,
        packet_sequence_xor=valid_summary.packet_sequence_xor,
        packets=ctypes.cast(safe_packets, ctypes.POINTER(M12CoreCommandPacket)),
    )
    safe_support = M12CorePacketSupportSummary()
    safe_support_rc = lib.m12core_classify_packet_support(
        ctypes.byref(safe_support_desc), ctypes.byref(safe_support)
    )
    checks["shape_safe"] = (
        safe_support_rc == 0
        and safe_support.status == M12CORE_PACKET_SUPPORT_STATUS_SAFE
        and safe_support.safe_for_probe_replay == 1
    )

    copy_packets = (M12CoreCommandPacket * 1)(
        packet(
            M12CORE_COMMAND_PACKET_KIND_COPY,
            12,
            M12CORE_COMMAND_PACKET_FLAG_COPY | M12CORE_COMMAND_PACKET_FLAG_USES_RESOURCE,
        )
    )
    copy_packets[0].object_id0 = handle_result.registry_id
    copy_support_desc = M12CorePacketSupportDesc.from_buffer_copy(safe_support_desc)
    copy_support_desc.packet_count = 1
    copy_support_desc.packet_sequence_xor ^= 0x12
    copy_support_desc.packets = ctypes.cast(copy_packets, ctypes.POINTER(M12CoreCommandPacket))
    copy_support = M12CorePacketSupportSummary()
    copy_support_rc = lib.m12core_classify_packet_support(
        ctypes.byref(copy_support_desc), ctypes.byref(copy_support)
    )
    checks["shape_unsupported_copy_negative"] = (
        copy_support_rc == 0
        and copy_support.status == M12CORE_PACKET_SUPPORT_STATUS_UNSUPPORTED
        and bool(copy_support.unsupported_reason_flags & M12CORE_PACKET_UNSUPPORTED_COPY)
        and copy_support.negative_cache_key != 0
    )

    stale_packets = (M12CoreCommandPacket * 1)(
        packet(
            M12CORE_COMMAND_PACKET_KIND_DRAW,
            13,
            M12CORE_COMMAND_PACKET_FLAG_GRAPHICS | M12CORE_COMMAND_PACKET_FLAG_USES_PIPELINE,
        )
    )
    stale_packets[0].object_id0 = 0x12345678
    stale_support_desc = M12CorePacketSupportDesc.from_buffer_copy(safe_support_desc)
    stale_support_desc.packet_count = 1
    stale_support_desc.packet_sequence_xor ^= 0x13
    stale_support_desc.packets = ctypes.cast(stale_packets, ctypes.POINTER(M12CoreCommandPacket))
    stale_support = M12CorePacketSupportSummary()
    stale_support_rc = lib.m12core_classify_packet_support(
        ctypes.byref(stale_support_desc), ctypes.byref(stale_support)
    )
    checks["shape_stale_or_missing"] = (
        stale_support_rc == 0
        and stale_support.status == M12CORE_PACKET_SUPPORT_STATUS_UNSUPPORTED
        and bool(stale_support.unsupported_reason_flags & M12CORE_PACKET_UNSUPPORTED_STALE_HANDLE)
    )

    replay_off_desc = M12CoreReplayPacketExecuteDesc(
        abi_version=M12CORE_ABI_VERSION,
        flags=M12CORE_REPLAY_PACKET_EXECUTE_ALLOW_PROBE_NATIVE,
        packet_count=2,
        queue_type=0,
        command_list_index=0,
        reserved0=0,
        command_list_id=0x1234,
        queue_serial=0x57,
        stream_key=valid_summary.stream_key,
        packet_sequence_xor=valid_summary.packet_sequence_xor,
        packets=ctypes.cast(safe_packets, ctypes.POINTER(M12CoreCommandPacket)),
    )
    replay_off = M12CoreReplayPacketExecuteSummary()
    replay_off_rc = lib.m12core_execute_replay_packet_stream(ctypes.byref(replay_off_desc), ctypes.byref(replay_off))
    checks["probe_replay_gate_off_fallback"] = (
        replay_off_rc == 0
        and replay_off.fallback_reason == M12CORE_REPLAY_PACKET_EXECUTE_FALLBACK_GATE_DISABLED
        and bool(replay_off.flags & M12CORE_REPLAY_PACKET_EXECUTE_SUMMARY_WHOLE_LIST_FALLBACK)
    )

    replay_on_desc = M12CoreReplayPacketExecuteDesc.from_buffer_copy(replay_off_desc)
    replay_on_desc.flags |= M12CORE_REPLAY_PACKET_EXECUTE_GATE_ENABLED
    replay_on = M12CoreReplayPacketExecuteSummary()
    replay_on_rc = lib.m12core_execute_replay_packet_stream(ctypes.byref(replay_on_desc), ctypes.byref(replay_on))
    checks["probe_replay_gate_on_executes_safe"] = (
        replay_on_rc == 0
        and replay_on.fallback_reason == M12CORE_REPLAY_PACKET_EXECUTE_FALLBACK_NONE
        and replay_on.executed_packet_count == 2
        and replay_on.binding_packet_count == 1
        and replay_on.draw_packet_count == 1
        and bool(replay_on.flags & M12CORE_REPLAY_PACKET_EXECUTE_SUMMARY_NATIVE_EXECUTED)
    )

    replay_copy_desc = M12CoreReplayPacketExecuteDesc.from_buffer_copy(replay_on_desc)
    replay_copy_desc.packet_count = 1
    replay_copy_desc.packets = ctypes.cast(copy_packets, ctypes.POINTER(M12CoreCommandPacket))
    replay_copy = M12CoreReplayPacketExecuteSummary()
    replay_copy_rc = lib.m12core_execute_replay_packet_stream(ctypes.byref(replay_copy_desc), ctypes.byref(replay_copy))
    checks["probe_replay_unsupported_fallback"] = (
        replay_copy_rc == 0
        and replay_copy.fallback_reason == M12CORE_REPLAY_PACKET_EXECUTE_FALLBACK_UNSUPPORTED_SHAPE
        and replay_copy.executed_packet_count == 0
        and bool(replay_copy.flags & M12CORE_REPLAY_PACKET_EXECUTE_SUMMARY_WHOLE_LIST_FALLBACK)
    )

    encoder_desc = M12CoreEncoderOwnershipDesc(
        abi_version=M12CORE_ABI_VERSION,
        flags=(
            M12CORE_ENCODER_OWNERSHIP_GATE_ENABLED
            | M12CORE_ENCODER_OWNERSHIP_REPLAY_NATIVE_EXECUTED
            | M12CORE_ENCODER_OWNERSHIP_HAS_RENDER_TARGETS
            | M12CORE_ENCODER_OWNERSHIP_HAS_RESOURCE_LAYOUT_VALIDATION
        ),
        packet_count=2,
        queue_type=0,
        command_list_index=0,
        render_target_count=1,
        descriptor_heap_count=0,
        reserved0=0,
        command_list_id=0x1234,
        queue_serial=0x58,
        stream_key=valid_summary.stream_key,
        packet_sequence_xor=valid_summary.packet_sequence_xor,
        replay_execute_key=replay_on.replay_execute_key,
        resource_layout_key=0x515253545556,
        packets=ctypes.cast(safe_packets, ctypes.POINTER(M12CoreCommandPacket)),
    )
    encoder = M12CoreEncoderOwnershipSummary()
    encoder_rc = lib.m12core_plan_encoder_ownership(ctypes.byref(encoder_desc), ctypes.byref(encoder))
    checks["encoder_ownership_plan"] = (
        encoder_rc == 0
        and encoder.planned_encoder_count == 1
        and encoder.render_encoder_count == 1
        and encoder.encoder_open_count == encoder.encoder_close_count == 1
        and bool(encoder.flags & M12CORE_ENCODER_OWNERSHIP_SUMMARY_NATIVE_ENCODER_OWNED)
    )
    checks["root_binding_cache_metadata"] = (
        encoder.binding_cache_entry_count == 1
        and encoder.root_binding_cache_key != 0
        and bool(encoder.flags & M12CORE_ENCODER_OWNERSHIP_SUMMARY_ROOT_BINDING_CACHE_WRITTEN)
    )
    checks["encoder_layout_validation_and_no_payload_reuse"] = (
        encoder.resource_layout_validation_required == 1
        and encoder.cache_payload_reuse_enabled == 0
        and bool(encoder.flags & M12CORE_ENCODER_OWNERSHIP_SUMMARY_LAYOUT_VALIDATION_REQUIRED)
        and bool(encoder.flags & M12CORE_ENCODER_OWNERSHIP_SUMMARY_CACHE_PAYLOAD_REUSE_DISABLED)
    )

    present_base = M12CoreNativePresentOwnershipDesc(
        abi_version=M12CORE_ABI_VERSION,
        flags=(
            M12CORE_NATIVE_PRESENT_OWNERSHIP_HAS_SOURCE_TEXTURE
            | M12CORE_NATIVE_PRESENT_OWNERSHIP_HAS_DRAWABLE
            | M12CORE_NATIVE_PRESENT_OWNERSHIP_USES_RAW_BLIT
            | M12CORE_NATIVE_PRESENT_OWNERSHIP_PRESENT_EXECUTE_SUPPORTED
            | M12CORE_NATIVE_PRESENT_OWNERSHIP_FORMAT_SUPPORTED
        ),
        width=1280,
        height=720,
        format=87,
        buffer_index=0,
        buffer_count=2,
        sync_interval=0,
        present_flags=0,
        work_classification=1,
        command_buffer_status=1,
        reserved0=0,
        present_count=7,
        present_execute_key=0x123456789ABC,
        source_texture_key=0x2222,
        drawable_texture_key=0x3333,
        queue_serial=0x4444,
        command_count=3,
        draw_count=1,
        dispatch_count=0,
        clear_count=1,
        wait_seq=0x5555,
    )
    present_off = M12CoreNativePresentOwnershipSummary()
    present_off_rc = lib.m12core_plan_native_present_ownership(
        ctypes.byref(present_base), ctypes.byref(present_off)
    )
    checks["native_present_gate_off_fallback"] = (
        present_off_rc == 0
        and present_off.fallback_reason == M12CORE_NATIVE_PRESENT_OWNERSHIP_FALLBACK_GATE_DISABLED
        and present_off.pe_present_required == 1
        and bool(present_off.flags & M12CORE_NATIVE_PRESENT_OWNERSHIP_SUMMARY_WHOLE_PRESENT_FALLBACK)
    )

    present_on_desc = M12CoreNativePresentOwnershipDesc.from_buffer_copy(present_base)
    present_on_desc.flags |= M12CORE_NATIVE_PRESENT_OWNERSHIP_GATE_ENABLED
    present_on = M12CoreNativePresentOwnershipSummary()
    present_on_rc = lib.m12core_plan_native_present_ownership(
        ctypes.byref(present_on_desc), ctypes.byref(present_on)
    )
    checks["native_present_raw_owned"] = (
        present_on_rc == 0
        and present_on.fallback_reason == 0
        and present_on.native_present_owned == 1
        and present_on.transport_thin == 1
        and present_on.planned_blit_count == 1
        and present_on.planned_present_count == 1
        and present_on.pe_present_required == 0
        and bool(present_on.flags & M12CORE_NATIVE_PRESENT_OWNERSHIP_SUMMARY_NATIVE_OWNED)
        and bool(present_on.flags & M12CORE_NATIVE_PRESENT_OWNERSHIP_SUMMARY_TRANSPORT_THIN)
    )

    present_presenter_desc = M12CoreNativePresentOwnershipDesc.from_buffer_copy(present_on_desc)
    present_presenter_desc.flags &= ~M12CORE_NATIVE_PRESENT_OWNERSHIP_USES_RAW_BLIT
    present_presenter_desc.flags |= M12CORE_NATIVE_PRESENT_OWNERSHIP_USES_PRESENTER
    present_presenter = M12CoreNativePresentOwnershipSummary()
    present_presenter_rc = lib.m12core_plan_native_present_ownership(
        ctypes.byref(present_presenter_desc), ctypes.byref(present_presenter)
    )
    checks["native_present_presenter_fallback"] = (
        present_presenter_rc == 0
        and present_presenter.fallback_reason == M12CORE_NATIVE_PRESENT_OWNERSHIP_FALLBACK_NON_RAW_PATH
        and present_presenter.pe_present_required == 1
        and bool(present_presenter.flags & M12CORE_NATIVE_PRESENT_OWNERSHIP_SUMMARY_WHOLE_PRESENT_FALLBACK)
    )

    present_executed_desc = M12CoreNativePresentOwnershipDesc.from_buffer_copy(present_on_desc)
    present_executed_desc.flags |= M12CORE_NATIVE_PRESENT_OWNERSHIP_PRESENT_EXECUTED
    present_executed = M12CoreNativePresentOwnershipSummary()
    present_executed_rc = lib.m12core_plan_native_present_ownership(
        ctypes.byref(present_executed_desc), ctypes.byref(present_executed)
    )
    checks["native_present_double_prevented"] = (
        present_executed_rc == 0
        and present_executed.native_present_owned == 1
        and present_executed.planned_blit_count == 0
        and present_executed.planned_present_count == 0
        and present_executed.double_present_prevented == 1
        and present_executed.double_blit_prevented == 1
        and bool(present_executed.flags & M12CORE_NATIVE_PRESENT_OWNERSHIP_SUMMARY_DOUBLE_PRESENT_PREVENTED)
        and bool(present_executed.flags & M12CORE_NATIVE_PRESENT_OWNERSHIP_SUMMARY_DOUBLE_BLIT_PREVENTED)
    )

    warm_desc = M12CoreCacheWarmStartDesc(
        abi_version=M12CORE_ABI_VERSION,
        flags=(
            M12CORE_CACHE_WARM_START_HAS_COMPATIBILITY_KEY
            | M12CORE_CACHE_WARM_START_HAS_INVALIDATION_PROOF
            | M12CORE_CACHE_WARM_START_SHADER_CACHE_HIT
            | M12CORE_CACHE_WARM_START_PIPELINE_CACHE_HIT
            | M12CORE_CACHE_WARM_START_PREWARM_REQUESTED
        ),
        shader_request_count=3,
        pipeline_request_count=2,
        prewarm_request_count=2,
        compatible_shader_hit_count=3,
        compatible_pipeline_hit_count=2,
        invalidated_entry_count=0,
        compatibility_key=0x11110000,
        invalidation_key=0x22220000,
        prewarm_pack_key=0x33330000,
        root_binding_cache_key=0x44440000,
        pipeline_cache_key=0x55550000,
        shader_cache_key=0x66660000,
    )
    warm_hit = M12CoreCacheWarmStartSummary()
    warm_hit_rc = lib.m12core_plan_cache_warm_start(ctypes.byref(warm_desc), ctypes.byref(warm_hit))
    checks["cache_warm_start_skips_compatible_work"] = (
        warm_hit_rc == 0
        and warm_hit.shader_work_skipped == 3
        and warm_hit.pipeline_work_skipped == 2
        and warm_hit.prewarm_work_skipped == 2
        and warm_hit.fallback_shader_work == 0
        and warm_hit.fallback_pipeline_work == 0
        and bool(warm_hit.flags & M12CORE_CACHE_WARM_START_SUMMARY_CACHE_FIRST_ENABLED)
        and bool(warm_hit.flags & M12CORE_CACHE_WARM_START_SUMMARY_INVALIDATION_PROVEN)
        and bool(warm_hit.flags & M12CORE_CACHE_WARM_START_SUMMARY_PREWARM_WORK_SKIPPED)
    )

    warm_invalid_desc = M12CoreCacheWarmStartDesc.from_buffer_copy(warm_desc)
    warm_invalid_desc.flags &= ~M12CORE_CACHE_WARM_START_HAS_INVALIDATION_PROOF
    warm_invalid_desc.invalidated_entry_count = 1
    warm_invalid = M12CoreCacheWarmStartSummary()
    warm_invalid_rc = lib.m12core_plan_cache_warm_start(ctypes.byref(warm_invalid_desc), ctypes.byref(warm_invalid))
    checks["cache_warm_start_invalidation_fallback"] = (
        warm_invalid_rc == 0
        and warm_invalid.fallback_reason == M12CORE_CACHE_WARM_START_FALLBACK_MISSING_INVALIDATION_PROOF
        and warm_invalid.shader_work_skipped == 0
        and warm_invalid.pipeline_work_skipped == 0
        and warm_invalid.fallback_shader_work == 3
        and warm_invalid.fallback_pipeline_work == 2
        and bool(warm_invalid.flags & M12CORE_CACHE_WARM_START_SUMMARY_FALLBACK_REQUIRED)
    )

    warm_force_desc = M12CoreCacheWarmStartDesc.from_buffer_copy(warm_desc)
    warm_force_desc.flags |= M12CORE_CACHE_WARM_START_FORCE_SOURCE_COMPILE
    warm_force = M12CoreCacheWarmStartSummary()
    warm_force_rc = lib.m12core_plan_cache_warm_start(ctypes.byref(warm_force_desc), ctypes.byref(warm_force))
    checks["cache_warm_start_force_source_fallback"] = (
        warm_force_rc == 0
        and warm_force.fallback_reason == M12CORE_CACHE_WARM_START_FALLBACK_FORCE_SOURCE
        and warm_force.shader_work_skipped == 0
        and warm_force.pipeline_work_skipped == 0
        and warm_force.fallback_shader_work == 3
        and warm_force.fallback_pipeline_work == 2
    )

    warm_miss_desc = M12CoreCacheWarmStartDesc.from_buffer_copy(warm_desc)
    warm_miss_desc.flags &= ~(
        M12CORE_CACHE_WARM_START_SHADER_CACHE_HIT | M12CORE_CACHE_WARM_START_PIPELINE_CACHE_HIT
    )
    warm_miss = M12CoreCacheWarmStartSummary()
    warm_miss_rc = lib.m12core_plan_cache_warm_start(ctypes.byref(warm_miss_desc), ctypes.byref(warm_miss))
    checks["cache_warm_start_cache_miss_fallback"] = (
        warm_miss_rc == 0
        and warm_miss.fallback_reason == M12CORE_CACHE_WARM_START_FALLBACK_CACHE_MISS
        and warm_miss.shader_work_skipped == 0
        and warm_miss.pipeline_work_skipped == 0
        and warm_miss.fallback_shader_work == 3
        and warm_miss.fallback_pipeline_work == 2
        and bool(warm_miss.flags & M12CORE_CACHE_WARM_START_SUMMARY_FALLBACK_REQUIRED)
    )

    coverage_desc = M12CoreReplayCoverageDesc(
        abi_version=M12CORE_ABI_VERSION,
        flags=(
            M12CORE_REPLAY_COVERAGE_GATE_ENABLED
            | M12CORE_REPLAY_COVERAGE_PACKET_STREAM_VALID
            | M12CORE_REPLAY_COVERAGE_SHAPE_SAFE
            | M12CORE_REPLAY_COVERAGE_PE_COM_FACADE_PRESENT
        ),
        packet_count=4,
        unsupported_reason_flags=0,
        graphics_packet_count=1,
        compute_packet_count=1,
        copy_packet_count=0,
        barrier_packet_count=1,
        binding_packet_count=1,
        draw_packet_count=1,
        dispatch_packet_count=1,
        clear_packet_count=0,
        render_target_packet_count=0,
        invalid_packet_count=0,
        stale_handle_count=0,
        missing_native_id_count=0,
        stream_key=valid_summary.stream_key,
        shape_key=safe_support.shape_key,
        replay_execute_key=replay_on.replay_execute_key,
        encoder_plan_key=encoder.encoder_plan_key,
    )
    coverage_safe = M12CoreReplayCoverageSummary()
    coverage_safe_rc = lib.m12core_plan_replay_coverage(ctypes.byref(coverage_desc), ctypes.byref(coverage_safe))
    checks["replay_coverage_safe_native_policy"] = (
        coverage_safe_rc == 0
        and coverage_safe.native_covered_packet_count == 4
        and coverage_safe.pe_fallback_packet_count == 0
        and coverage_safe.policy_native_count == 4
        and coverage_safe.com_facade_preserved == 1
        and coverage_safe.transport_thin == 1
        and bool(coverage_safe.flags & M12CORE_REPLAY_COVERAGE_SUMMARY_NATIVE_COVERAGE_PLANNED)
        and bool(coverage_safe.flags & M12CORE_REPLAY_COVERAGE_SUMMARY_POLICY_NATIVE)
        and bool(coverage_safe.flags & M12CORE_REPLAY_COVERAGE_SUMMARY_COM_FACADE_PRESERVED)
    )

    coverage_copy_desc = M12CoreReplayCoverageDesc.from_buffer_copy(coverage_desc)
    coverage_copy_desc.copy_packet_count = 1
    coverage_copy_desc.unsupported_reason_flags = M12CORE_PACKET_UNSUPPORTED_COPY
    coverage_copy = M12CoreReplayCoverageSummary()
    coverage_copy_rc = lib.m12core_plan_replay_coverage(ctypes.byref(coverage_copy_desc), ctypes.byref(coverage_copy))
    checks["replay_coverage_copy_pe_fallback"] = (
        coverage_copy_rc == 0
        and coverage_copy.native_covered_packet_count == 0
        and coverage_copy.pe_fallback_packet_count == 4
        and coverage_copy.policy_pe_count == 4
        and coverage_copy.unsupported_packet_count >= 1
        and coverage_copy.com_facade_preserved == 1
        and bool(coverage_copy.flags & M12CORE_REPLAY_COVERAGE_SUMMARY_PE_FALLBACK_REQUIRED)
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
        "handle": handle_result_to_dict(handle_result),
        "handle_validation": handle_validation_to_dict(validation_result),
        "stale_handle_validation": handle_validation_to_dict(stale_result),
        "safe_shape": support_to_dict(safe_support),
        "copy_shape": support_to_dict(copy_support),
        "stale_shape": support_to_dict(stale_support),
        "probe_replay_gate_off": replay_execute_to_dict(replay_off),
        "probe_replay_gate_on": replay_execute_to_dict(replay_on),
        "probe_replay_unsupported": replay_execute_to_dict(replay_copy),
        "encoder_ownership": encoder_to_dict(encoder),
        "native_present_gate_off": present_ownership_to_dict(present_off),
        "native_present_raw_owned": present_ownership_to_dict(present_on),
        "native_present_presenter_fallback": present_ownership_to_dict(present_presenter),
        "native_present_double_prevented": present_ownership_to_dict(present_executed),
        "cache_warm_start_hit": warm_start_to_dict(warm_hit),
        "cache_warm_start_invalid": warm_start_to_dict(warm_invalid),
        "cache_warm_start_force_source": warm_start_to_dict(warm_force),
        "cache_warm_start_miss": warm_start_to_dict(warm_miss),
        "replay_coverage_safe": replay_coverage_to_dict(coverage_safe),
        "replay_coverage_copy_fallback": replay_coverage_to_dict(coverage_copy),
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
