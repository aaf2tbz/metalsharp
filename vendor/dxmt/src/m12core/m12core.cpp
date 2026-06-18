#include "m12core.h"

#include "airconv_public.h"
#include "dxil/dxil_container.hpp"
#include "dxil/dxil_to_msl.hpp"
#include "dxil/llvm_bitcode.hpp"
#include "dxil/msl_lowering.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>
#include <sys/stat.h>

/*
 * Native M12 core bootstrap
 *
 * Phase 1 proved that dxmt_m12 can stage, load, version-check, and fall back
 * from a unified native core library before shader/PSO/binding ownership is
 * migrated.  Phase 2 starts moving diagnostic ownership by adding native core
 * counter storage.  Rendering still lives in the existing PE/DXMT files; future
 * refactor passes should move one ownership domain at a time and leave comments
 * at each compatibility seam.
 */

namespace {

constexpr uint32_t kBuildIdLow = M12CORE_BUILD_ID_LOW;
constexpr uint32_t kBuildIdHigh = M12CORE_BUILD_ID_HIGH;

std::atomic<uint64_t> g_counters[M12CORE_COUNTER_COUNT] = {};

bool validCounter(uint32_t counter_id) {
  return counter_id < M12CORE_COUNTER_COUNT;
}

uint32_t readLe32(const uint8_t *data) {
  uint32_t value = 0;
  std::memcpy(&value, data, sizeof(value));
  return value;
}

bool shaderContainsDxil(const void *bytecode, uint64_t bytecode_size) {
  if (!bytecode || bytecode_size < 32)
    return false;

  const auto *data = static_cast<const uint8_t *>(bytecode);
  if (readLe32(data) != 0x43425844u) // 'DXBC'
    return false;

  uint32_t container_size = readLe32(data + 24);
  uint32_t chunk_count = readLe32(data + 28);
  if (container_size > bytecode_size || chunk_count > 4096)
    return false;

  uint64_t table_end = 32ull + (uint64_t)chunk_count * sizeof(uint32_t);
  if (table_end > bytecode_size)
    return false;

  for (uint32_t i = 0; i < chunk_count; i++) {
    uint32_t offset = readLe32(data + 32ull + (uint64_t)i * sizeof(uint32_t));
    if ((uint64_t)offset + 8 > bytecode_size)
      continue;
    if (readLe32(data + offset) == 0x4c495844u) // 'DXIL'
      return true;
  }
  return false;
}

uint32_t legacyShaderTypeValue(uint32_t stage) {
  /* Match the existing PE-side `ShaderType` enum values so enabling the native
   * helper does not invalidate live shader/metallib cache filenames.  The ABI
   * still exposes stable M12CORE_SHADER_STAGE_* values; this mapping is only
   * for preserving the current hash namespace during the migration.
   */
  switch (stage) {
  case M12CORE_SHADER_STAGE_VERTEX:
    return 0;
  case M12CORE_SHADER_STAGE_PIXEL:
    return 1;
  case M12CORE_SHADER_STAGE_COMPUTE:
    return 2;
  case M12CORE_SHADER_STAGE_HULL:
    return 3;
  case M12CORE_SHADER_STAGE_DOMAIN:
    return 4;
  case M12CORE_SHADER_STAGE_GEOMETRY:
    return 5;
  default:
    return 0;
  }
}

void copyRoot(char *out, size_t out_size, const char *cache_root) {
  const char *root =
      (cache_root && cache_root[0]) ? cache_root : "/tmp/dxmt_shader_cache";
  std::snprintf(out, out_size, "%s", root);
  out[out_size - 1] = '\0';
  while (std::strlen(out) > 1) {
    size_t len = std::strlen(out);
    if (out[len - 1] != '/' && out[len - 1] != '\\')
      break;
    out[len - 1] = '\0';
  }
}

void formatCachePath(char *out, size_t out_size, const char *root,
                     const char *suffix) {
  std::snprintf(out, out_size, "%s/%s", root, suffix);
  out[out_size - 1] = '\0';
}

uint64_t hashShaderBytecode(const void *bytecode, uint64_t bytecode_size,
                            uint32_t stage) {
  uint64_t hash = 0;
  hash = hash * 131 + legacyShaderTypeValue(stage);
  /* Preserve the current vertex shader namespace marker from the PE cache key.
   * Input-layout normalization will move later; keeping this marker now avoids
   * conflating old vertex keys with the new native-core key namespace.
   */
  if (stage == M12CORE_SHADER_STAGE_VERTEX)
    hash = hash * 131 + 0x4d3132506833ull;
  if (bytecode && bytecode_size > 0) {
    const auto *data = static_cast<const uint8_t *>(bytecode);
    for (uint64_t i = 0; i < bytecode_size; i++)
      hash = hash * 131 + data[i];
  }
  return hash;
}

} // namespace

extern "C" int m12core_get_version(M12CoreVersion *out_version) {
  if (!out_version)
    return 1;

  out_version->abi_version = M12CORE_ABI_VERSION;
  out_version->feature_flags = M12CORE_FEATURE_ALL;
  out_version->build_id_low = kBuildIdLow;
  out_version->build_id_high = kBuildIdHigh;
  return 0;
}

extern "C" const char *m12core_build_string(void) {
  return "libm12core convergence-c3-c35 handle-shape-shadow abi=1";
}

extern "C" int m12core_record_counter(uint32_t counter_id, uint64_t delta) {
  if (!validCounter(counter_id))
    return 1;
  g_counters[counter_id].fetch_add(delta, std::memory_order_relaxed);
  return 0;
}

extern "C" int m12core_get_counters(M12CoreCounterSnapshot *out_snapshot) {
  if (!out_snapshot)
    return 1;

  out_snapshot->abi_version = M12CORE_ABI_VERSION;
  out_snapshot->counter_count = M12CORE_COUNTER_COUNT;
  for (uint32_t i = 0; i < M12CORE_COUNTER_COUNT; i++)
    out_snapshot->values[i] = g_counters[i].load(std::memory_order_relaxed);
  return 0;
}

extern "C" void m12core_reset_counters(void) {
  for (auto &counter : g_counters)
    counter.store(0, std::memory_order_relaxed);
}

extern "C" int
m12core_hash_shader_bytecode(const void *bytecode, uint64_t bytecode_size,
                             uint32_t stage,
                             M12CoreShaderBytecodeInfo *out_info) {
  if (!out_info)
    return 1;

  out_info->abi_version = M12CORE_ABI_VERSION;
  out_info->stage = stage;
  out_info->bytecode_hash = hashShaderBytecode(bytecode, bytecode_size, stage);
  out_info->bytecode_size = bytecode_size;
  out_info->contains_dxil =
      shaderContainsDxil(bytecode, bytecode_size) ? 1u : 0u;
  out_info->reserved = 0;
  return 0;
}

extern "C" int m12core_shader_contains_dxil(const void *bytecode,
                                            uint64_t bytecode_size,
                                            uint32_t *out_contains_dxil) {
  if (!out_contains_dxil)
    return 1;
  *out_contains_dxil = shaderContainsDxil(bytecode, bytecode_size) ? 1u : 0u;
  return 0;
}

bool regularFileExists(const char *path) {
  struct stat st;
  return path && path[0] && stat(path, &st) == 0 && S_ISREG(st.st_mode) &&
         st.st_size > 0;
}

void pipelineHashCombine(uint64_t &hash, uint64_t value) {
  hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
}

bool validHandleKind(uint32_t kind) {
  return kind >= M12CORE_HANDLE_KIND_RESOURCE &&
         kind <= M12CORE_HANDLE_KIND_SWAPCHAIN_IMAGE;
}

uint64_t makeHandleKey(const M12CoreHandleRegistryDesc &desc) {
  uint64_t key = 0x4d3132484e444cull; // "M12HNDL" marker.
  pipelineHashCombine(key, desc.kind);
  pipelineHashCombine(key, desc.flags);
  pipelineHashCombine(key, desc.generation);
  pipelineHashCombine(key, desc.source_key);
  pipelineHashCombine(key, desc.aux_key0);
  pipelineHashCombine(key, desc.aux_key1);
  pipelineHashCombine(key, desc.owner_key);
  return key;
}

uint64_t makeRegistryId(uint32_t kind, uint32_t generation,
                        uint64_t handle_key) {
  /* Scalar shadow ID layout: marker/kind/generation/hash.  It is stable and
   * validateable without storing ownership-bearing native objects.
   */
  return 0xa500000000000000ull | ((uint64_t)(kind & 0xffu) << 48) |
         ((uint64_t)(generation & 0xffffu) << 32) |
         (handle_key & 0xffffffffull);
}

uint32_t registryIdKind(uint64_t registry_id) {
  return (uint32_t)((registry_id >> 48) & 0xffu);
}

uint32_t registryIdGeneration(uint64_t registry_id) {
  return (uint32_t)((registry_id >> 32) & 0xffffu);
}

bool registryIdHasMarker(uint64_t registry_id) {
  return (registry_id & 0xff00000000000000ull) == 0xa500000000000000ull;
}

bool packetKindRequiresNativeId(uint32_t kind, uint32_t flags) {
  (void)flags;
  switch (kind) {
  case M12CORE_COMMAND_PACKET_KIND_SET_PIPELINE:
  case M12CORE_COMMAND_PACKET_KIND_SET_ROOT_SIGNATURE:
  case M12CORE_COMMAND_PACKET_KIND_CLEAR_RTV:
  case M12CORE_COMMAND_PACKET_KIND_CLEAR_DSV:
  case M12CORE_COMMAND_PACKET_KIND_CLEAR_UAV:
  case M12CORE_COMMAND_PACKET_KIND_DRAW:
  case M12CORE_COMMAND_PACKET_KIND_DRAW_INDEXED:
  case M12CORE_COMMAND_PACKET_KIND_DISPATCH:
  case M12CORE_COMMAND_PACKET_KIND_COPY:
    return true;
  default:
    return false;
  }
}

dxmt::dxil::MSLShader toRuntimeMSLShader(dxmt::dxil::TypedMSLShader &&typed) {
  dxmt::dxil::MSLShader shader;
  shader.source = std::move(typed.source);
  shader.entry_point = std::move(typed.entry_point);
  shader.tg_size[0] = typed.tg_size[0];
  shader.tg_size[1] = typed.tg_size[1];
  shader.tg_size[2] = typed.tg_size[2];
  shader.unsupported_intrinsics = typed.unsupported_intrinsics;
  shader.unsupported_opcodes = typed.unsupported_opcodes;
  shader.diagnostics = std::move(typed.diagnostics);
  shader.diagnostics.push_back("MSLLowering runtime path active in libm12core");
  return shader;
}

void copySm50Reflection(const MTL_SHADER_REFLECTION &src,
                        M12CoreSM50ShaderReflection *dst) {
  if (!dst)
    return;
  dst->abi_version = M12CORE_ABI_VERSION;
  dst->constant_buffer_table_bind_index = src.ConstanttBufferTableBindIndex;
  dst->argument_buffer_bind_index = src.ArgumentBufferBindIndex;
  dst->num_constant_buffers = src.NumConstantBuffers;
  dst->num_arguments = src.NumArguments;
  dst->stage_payload[0] = src.ThreadgroupSize[0];
  dst->stage_payload[1] = src.ThreadgroupSize[1];
  dst->stage_payload[2] = src.ThreadgroupSize[2];
  dst->constant_buffer_slot_mask = src.ConstantBufferSlotMask;
  dst->sampler_slot_mask = src.SamplerSlotMask;
  dst->uav_slot_mask = src.UAVSlotMask;
  dst->srv_slot_mask_hi = src.SRVSlotMaskHi;
  dst->srv_slot_mask_lo = src.SRVSlotMaskLo;
  dst->num_output_element = src.NumOutputElement;
  dst->threads_per_patch = src.ThreadsPerPatch;
  dst->argument_table_qwords = src.ArgumentTableQwords;
}

void copySm50Argument(const MTL_SM50_SHADER_ARGUMENT &src,
                      M12CoreSM50ShaderArgument &dst) {
  dst.type = (uint32_t)src.Type;
  dst.binding_slot = src.SM50BindingSlot;
  dst.register_space = src.SM50RegisterSpace;
  dst.flags = (uint32_t)src.Flags;
  dst.structure_ptr_offset = src.StructurePtrOffset;
  dst.size_in_vec4 = src.SizeInVec4;
}

void copyMslVertexInputs(const M12CoreDXILToMSLDesc *desc,
                         dxmt::dxil::MSLLoweringOptions &options) {
  if (!desc || !desc->vertex_inputs || !desc->vertex_input_count)
    return;
  const uint32_t count = std::min(desc->vertex_input_count, 64u);
  options.vertex_inputs.reserve(count);
  for (uint32_t i = 0; i < count; i++) {
    const auto &src = desc->vertex_inputs[i];
    dxmt::dxil::MSLVertexInputElement dst = {};
    dst.shader_register = src.shader_register;
    dst.table_index = src.table_index;
    dst.input_slot = src.input_slot;
    dst.aligned_byte_offset = src.aligned_byte_offset;
    dst.dxgi_format = src.dxgi_format;
    dst.metal_format = src.metal_format;
    dst.per_instance = src.per_instance != 0;
    dst.instance_step_rate =
        src.instance_step_rate ? src.instance_step_rate : 1;
    dst.table_indexing_mode =
        src.table_indexing_mode == 1
            ? dxmt::dxil::MSLVertexTableIndexingMode::RawSlot
            : dxmt::dxil::MSLVertexTableIndexingMode::CompactBySlotMask;
    dst.system_value = src.system_value != 0;
    options.vertex_inputs.push_back(dst);
  }
}

extern "C" int
m12core_format_shader_cache_paths(const char *cache_root, uint64_t shader_hash,
                                  M12CoreShaderCachePaths *out_paths) {
  if (!out_paths)
    return 1;

  /* Phase 3.1: native ownership of shader cache lookup policy.
   * This is intentionally limited to deterministic path/key formatting.  The
   * PE side still performs file IO and Metal function creation, so a bad policy
   * change remains easy to revert without touching compiler ownership.
   */
  char root[M12CORE_SHADER_CACHE_PATH_CAPACITY];
  char suffix[128];
  copyRoot(root, sizeof(root), cache_root);

  out_paths->abi_version = M12CORE_ABI_VERSION;
  out_paths->path_capacity = M12CORE_SHADER_CACHE_PATH_CAPACITY;
  out_paths->shader_hash = shader_hash;
  std::snprintf(suffix, sizeof(suffix), "%016llx",
                (unsigned long long)shader_hash);
  formatCachePath(out_paths->cache_path, sizeof(out_paths->cache_path), root,
                  suffix);

  std::snprintf(suffix, sizeof(suffix), "%016llx.dxbc",
                (unsigned long long)shader_hash);
  formatCachePath(out_paths->dxbc_path, sizeof(out_paths->dxbc_path), root,
                  suffix);
  std::snprintf(suffix, sizeof(suffix), "%016llx.metallib",
                (unsigned long long)shader_hash);
  formatCachePath(out_paths->metallib_path, sizeof(out_paths->metallib_path),
                  root, suffix);
  std::snprintf(suffix, sizeof(suffix), "%016llx.json",
                (unsigned long long)shader_hash);
  formatCachePath(out_paths->reflection_path,
                  sizeof(out_paths->reflection_path), root, suffix);
  std::snprintf(suffix, sizeof(suffix), "%016llx.module.txt",
                (unsigned long long)shader_hash);
  formatCachePath(out_paths->module_summary_path,
                  sizeof(out_paths->module_summary_path), root, suffix);
  std::snprintf(suffix, sizeof(suffix), "%016llx.dxil_report.txt",
                (unsigned long long)shader_hash);
  formatCachePath(out_paths->dxil_report_path,
                  sizeof(out_paths->dxil_report_path), root, suffix);
  std::snprintf(suffix, sizeof(suffix), "%016llx.metallib.err.txt",
                (unsigned long long)shader_hash);
  formatCachePath(out_paths->metallib_error_path,
                  sizeof(out_paths->metallib_error_path), root, suffix);
  return 0;
}

void parseReflectionText(const char *text,
                         M12CoreShaderReflectionSummary *out_summary) {
  if (!text || !out_summary)
    return;

  const char *entry = std::strstr(text, "\"EntryPoint\"");
  if (entry) {
    const char *q1 = std::strchr(entry + 13, '"');
    const char *q2 = q1 ? std::strchr(q1 + 1, '"') : nullptr;
    if (q1 && q2 && q2 > q1 + 1) {
      size_t len = (size_t)(q2 - q1 - 1);
      if (len >= M12CORE_SHADER_ENTRY_POINT_CAPACITY)
        len = M12CORE_SHADER_ENTRY_POINT_CAPACITY - 1;
      std::memcpy(out_summary->entry_point, q1 + 1, len);
      out_summary->entry_point[len] = 0;
      out_summary->has_entry_point = 1;
    }
  }

  const char *tg = std::strstr(text, "\"tg_size\"");
  if (tg) {
    unsigned x = 1, y = 1, z = 1;
    if (std::sscanf(tg, "\"tg_size\": [%u, %u, %u]", &x, &y, &z) == 3 ||
        std::sscanf(tg, "\"tg_size\":[%u,%u,%u]", &x, &y, &z) == 3) {
      out_summary->threadgroup_size[0] = x;
      out_summary->threadgroup_size[1] = y;
      out_summary->threadgroup_size[2] = z;
      out_summary->has_threadgroup_size = 1;
    }
  }
}

extern "C" int
m12core_probe_shader_cache(const char *cache_root, uint64_t shader_hash,
                           uint32_t force_source_compile,
                           M12CoreShaderCacheLookup *out_lookup) {
  if (!out_lookup)
    return 1;

  /* Phase 3.2: native ownership of shader cache lookup results.  The core now
   * decides whether a metallib cache entry is usable, but PE-side code still
   * opens/reads the file and creates Metal libraries.  This keeps IO/object
   * lifetime out of the ABI until the full shader compiler migration lands.
   */
  out_lookup->abi_version = M12CORE_ABI_VERSION;
  out_lookup->force_source_compile = force_source_compile ? 1u : 0u;
  if (m12core_format_shader_cache_paths(cache_root, shader_hash,
                                        &out_lookup->paths) != 0)
    return 1;
  out_lookup->metallib_exists =
      regularFileExists(out_lookup->paths.metallib_path) ? 1u : 0u;
  out_lookup->metallib_available =
      (!force_source_compile && out_lookup->metallib_exists) ? 1u : 0u;
  return 0;
}

extern "C" int
m12core_parse_shader_reflection(const char *reflection_text,
                                uint64_t reflection_text_size,
                                M12CoreShaderReflectionSummary *out_summary) {
  if (!out_summary)
    return 1;

  /* Phase 3.3: native reflection summary parsing.  This intentionally parses
   * the small cache-side JSON summary only; SM50/airconv reflection objects and
   * argument binding arrays stay on the compatibility path until their lifetime
   * model is migrated explicitly.
   */
  out_summary->abi_version = M12CORE_ABI_VERSION;
  out_summary->has_entry_point = 0;
  out_summary->has_threadgroup_size = 0;
  out_summary->reserved = 0;
  out_summary->entry_point[0] = 0;
  out_summary->threadgroup_size[0] = 1;
  out_summary->threadgroup_size[1] = 1;
  out_summary->threadgroup_size[2] = 1;

  if (!reflection_text || reflection_text_size == 0)
    return 0;

  char local[4096];
  uint64_t copy_size = reflection_text_size;
  if (copy_size >= sizeof(local))
    copy_size = sizeof(local) - 1;
  std::memcpy(local, reflection_text, (size_t)copy_size);
  local[copy_size] = 0;
  parseReflectionText(local, out_summary);
  return 0;
}

extern "C" int m12core_lower_dxil_to_msl(const M12CoreDXILToMSLDesc *desc,
                                         char *out_source,
                                         uint64_t out_source_capacity,
                                         M12CoreDXILToMSLResult *out_result) {
  if (!out_result)
    return 1;

  std::memset(out_result, 0, sizeof(*out_result));
  out_result->abi_version = M12CORE_ABI_VERSION;
  out_result->threadgroup_size[0] = 1;
  out_result->threadgroup_size[1] = 1;
  out_result->threadgroup_size[2] = 1;

  if (!desc || desc->abi_version != M12CORE_ABI_VERSION ||
      !desc->dxil_container || desc->dxil_container_size == 0) {
    out_result->status = M12CORE_DXIL_TO_MSL_STATUS_INVALID;
    return 0;
  }

  /* Phase 3 DXIL->MSL ownership seam.  libm12core now parses the DXIL
   * container, LLVM bitcode, and vertex-input lowering metadata to generate MSL
   * source.  The PE side still writes diagnostics/cache files and asks the
   * shader-function ABI to create Metal objects so each ownership transfer
   * stays independently reviewable.
   */
  auto container = dxmt::dxil::DXILContainer::parse(
      desc->dxil_container, (size_t)desc->dxil_container_size);
  if (!container) {
    out_result->status = M12CORE_DXIL_TO_MSL_STATUS_CONTAINER_PARSE_FAILED;
    return 0;
  }

  const auto &shader_info = container->shader();
  auto module = dxmt::dxil::BitcodeReader::parse(shader_info.bitcode.data,
                                                 shader_info.bitcode.size);
  if (!module) {
    out_result->status = M12CORE_DXIL_TO_MSL_STATUS_BITCODE_PARSE_FAILED;
    return 0;
  }

  dxmt::dxil::MSLLoweringOptions lowering_options = {};
  copyMslVertexInputs(desc, lowering_options);
  auto typed_msl =
      dxmt::dxil::MSLLowering::lower(*module, shader_info, lowering_options);
  auto msl_result =
      typed_msl ? std::optional<dxmt::dxil::MSLShader>(
                      std::in_place, toRuntimeMSLShader(std::move(*typed_msl)))
                : dxmt::dxil::DXILToMSL::convert(*module, shader_info);
  if (!msl_result) {
    out_result->status = M12CORE_DXIL_TO_MSL_STATUS_LOWERING_FAILED;
    return 0;
  }

  out_result->required_source_size = msl_result->source.size();
  std::snprintf(out_result->entry_point, sizeof(out_result->entry_point), "%s",
                msl_result->entry_point.c_str());
  out_result->threadgroup_size[0] = msl_result->tg_size[0];
  out_result->threadgroup_size[1] = msl_result->tg_size[1];
  out_result->threadgroup_size[2] = msl_result->tg_size[2];
  out_result->unsupported_intrinsics = msl_result->unsupported_intrinsics;
  out_result->unsupported_opcodes = msl_result->unsupported_opcodes;
  out_result->used_typed_lowering = typed_msl ? 1u : 0u;

  if (!out_source || out_source_capacity <= msl_result->source.size()) {
    out_result->status = M12CORE_DXIL_TO_MSL_STATUS_OUTPUT_TOO_SMALL;
    if (out_source && out_source_capacity)
      out_source[0] = 0;
    return 0;
  }

  std::memcpy(out_source, msl_result->source.data(), msl_result->source.size());
  out_source[msl_result->source.size()] = 0;
  out_result->status = M12CORE_DXIL_TO_MSL_STATUS_OK;
  return 0;
}

extern "C" int m12core_reflect_sm50_shader(
    const void *bytecode, uint64_t bytecode_size, uint32_t options,
    M12CoreSM50ShaderReflection *out_reflection,
    M12CoreSM50ShaderArgument *out_constant_buffers,
    uint32_t constant_buffer_capacity, M12CoreSM50ShaderArgument *out_arguments,
    uint32_t argument_capacity, M12CoreSM50ReflectionResult *out_result) {
  if (!out_result)
    return 1;

  std::memset(out_result, 0, sizeof(*out_result));
  out_result->abi_version = M12CORE_ABI_VERSION;
  if (out_reflection)
    std::memset(out_reflection, 0, sizeof(*out_reflection));

  if (!bytecode || bytecode_size == 0 || !out_reflection) {
    out_result->status = M12CORE_SM50_REFLECTION_STATUS_INVALID;
    return 0;
  }

  /* Phase 3 reflection compatibility seam.  The native core now owns SM50
   * reflection and argument extraction, but returns POD copies that the PE side
   * maps back to its existing D3D12 binding structs.  Actual root/descriptor
   * binding plan ownership remains Phase 5.
   */
  sm50_error_t err = nullptr;
  sm50_shader_t shader = nullptr;
  MTL_SHADER_REFLECTION reflection = {};
  if (SM50InitializeWithOptions(bytecode, (size_t)bytecode_size, options,
                                &shader, &reflection, &err)) {
    if (err)
      SM50FreeError(err);
    out_result->status = M12CORE_SM50_REFLECTION_STATUS_INIT_FAILED;
    return 0;
  }

  copySm50Reflection(reflection, out_reflection);
  out_result->required_constant_buffers = reflection.NumConstantBuffers;
  out_result->required_arguments = reflection.NumArguments;

  if ((reflection.NumConstantBuffers &&
       (!out_constant_buffers ||
        constant_buffer_capacity < reflection.NumConstantBuffers)) ||
      (reflection.NumArguments &&
       (!out_arguments || argument_capacity < reflection.NumArguments))) {
    SM50Destroy(shader);
    out_result->status = M12CORE_SM50_REFLECTION_STATUS_OUTPUT_TOO_SMALL;
    return 0;
  }

  std::vector<MTL_SM50_SHADER_ARGUMENT> cbs(reflection.NumConstantBuffers);
  std::vector<MTL_SM50_SHADER_ARGUMENT> args(reflection.NumArguments);
  if (reflection.NumConstantBuffers || reflection.NumArguments)
    SM50GetArgumentsInfo(shader, cbs.empty() ? nullptr : cbs.data(),
                         args.empty() ? nullptr : args.data());
  for (uint32_t i = 0; i < reflection.NumConstantBuffers; i++)
    copySm50Argument(cbs[i], out_constant_buffers[i]);
  for (uint32_t i = 0; i < reflection.NumArguments; i++)
    copySm50Argument(args[i], out_arguments[i]);

  SM50Destroy(shader);
  out_result->status = M12CORE_SM50_REFLECTION_STATUS_OK;
  return 0;
}

extern "C" int
m12core_make_pipeline_cache_key(const M12CorePipelineCacheKeyInput *input,
                                M12CorePipelineCacheKey *out_key) {
  if (!input || !out_key || input->abi_version != M12CORE_ABI_VERSION)
    return 1;

  /* Phase 4 compatibility entry point retained for already-migrated callers.
   * New render/compute PSO sites should prefer the field-stream API below so
   * libm12core, not PE-side D3D12, owns ordered key accumulation.
   */
  M12CorePipelineKeyFields fields = {};
  fields.abi_version = input->abi_version;
  fields.kind = input->kind;
  fields.base_hash = input->base_hash;
  fields.device_id = input->device_id;
  fields.flags = input->flags;
  return m12core_make_pipeline_cache_key_from_fields(&fields, out_key);
}

extern "C" int m12core_make_pipeline_cache_key_from_fields(
    const M12CorePipelineKeyFields *input, M12CorePipelineCacheKey *out_key) {
  if (!input || !out_key || input->abi_version != M12CORE_ABI_VERSION)
    return 1;
  if (input->field_count && !input->fields)
    return 1;

  /* Phase 4 normalized PSO key seam.  PE-side D3D12 still knows how to map its
   * descriptors into stable scalar fields, but libm12core now owns the ordered
   * accumulation recipe and final device-scoped namespace.  This is the safe
   * midpoint before root-signature/binding ownership moves in Phase 5.
   */
  uint64_t key = input->base_hash;
  for (uint32_t i = 0; i < input->field_count; i++)
    pipelineHashCombine(key, input->fields[i]);
  pipelineHashCombine(key, input->device_id);
  pipelineHashCombine(key, input->kind);
  pipelineHashCombine(key, input->flags);

  out_key->abi_version = M12CORE_ABI_VERSION;
  out_key->kind = input->kind;
  out_key->key = key;
  return 0;
}

extern "C" int
m12core_summarize_root_signature(const M12CoreRootSignatureDesc *desc,
                                 M12CoreRootSignatureSummary *out_summary) {
  if (!desc || !out_summary || desc->abi_version != M12CORE_ABI_VERSION)
    return 1;
  if (desc->field_count && !desc->fields)
    return 1;

  /* Phase 5 root-signature seam.  D3D12 still parses the Windows root-signature
   * blob and performs descriptor binding lookups, but libm12core now owns the
   * stable structural key and summary counts that later binding-plan migration
   * will use.  The field stream is intentionally scalar/POD-only so PE and
   * native sides do not share C++ containers or D3D12 structs.
   */
  uint64_t key = 0x4d313252534947ull;
  pipelineHashCombine(key, desc->flags);
  pipelineHashCombine(key, desc->parameter_count);
  pipelineHashCombine(key, desc->static_sampler_count);

  uint32_t descriptor_tables = 0;
  uint32_t descriptor_ranges = 0;
  uint32_t root_descriptors = 0;
  uint32_t root_constants = 0;
  for (uint32_t i = 0; i < desc->field_count; i++) {
    const uint64_t field = desc->fields[i];
    pipelineHashCombine(key, field);
    const uint32_t tag = (uint32_t)(field >> 56);
    if (tag == 0x51u)
      descriptor_tables++;
    else if (tag == 0x52u)
      descriptor_ranges++;
    else if (tag == 0x53u)
      root_descriptors++;
    else if (tag == 0x54u)
      root_constants++;
  }

  out_summary->abi_version = M12CORE_ABI_VERSION;
  out_summary->status = M12CORE_ROOT_SIGNATURE_STATUS_OK;
  out_summary->parameter_count = desc->parameter_count;
  out_summary->descriptor_table_count = descriptor_tables;
  out_summary->descriptor_range_count = descriptor_ranges;
  out_summary->root_descriptor_count = root_descriptors;
  out_summary->root_constant_count = root_constants;
  out_summary->static_sampler_count = desc->static_sampler_count;
  out_summary->root_signature_key = key;
  return 0;
}

extern "C" int
m12core_build_root_binding_plan(const M12CoreRootBindingPlanDesc *desc,
                                M12CoreRootBindingPlanSummary *out_summary) {
  if (!desc || !out_summary || desc->abi_version != M12CORE_ABI_VERSION)
    return 1;
  if ((desc->parameter_count && !desc->parameters) ||
      (desc->range_count && !desc->ranges) ||
      (desc->static_sampler_count && !desc->static_samplers))
    return 1;

  /* Phase 5 binding-plan seam.  The PE side still executes all live binding,
   * but root-signature creation now sends a POD description to libm12core so
   * native code can construct the descriptor/register-space view once instead
   * of learning it from per-draw hot paths.  The summary/key are diagnostics
   * for now and a fallback-safe migration point for future argument buffers.
   */
  uint64_t key = desc->root_signature_key ? desc->root_signature_key
                                          : 0x4d313242504c414eull;
  pipelineHashCombine(key, desc->flags);
  pipelineHashCombine(key, desc->parameter_count);
  pipelineHashCombine(key, desc->range_count);
  pipelineHashCombine(key, desc->static_sampler_count);

  uint32_t descriptor_tables = 0;
  uint32_t root_descriptors = 0;
  uint32_t root_constants = 0;
  uint32_t resource_ranges = 0;
  uint32_t sampler_ranges = 0;
  uint32_t unbounded_ranges = 0;
  uint32_t visibility_specific = 0;
  uint32_t max_table_span = 0;
  uint32_t argument_resource_slots = 0;
  uint32_t argument_sampler_slots = 0;
  uint32_t argument_root_descriptor_slots = 0;
  uint32_t argument_root_constant_dwords = 0;
  uint32_t argument_visibility_mask = 0;
  std::vector<uint32_t> register_spaces;

  auto noteVisibility = [&](uint32_t visibility) {
    if (visibility < 32)
      argument_visibility_mask |= 1u << visibility;
  };

  for (uint32_t i = 0; i < desc->parameter_count; i++) {
    const auto &param = desc->parameters[i];
    pipelineHashCombine(key, param.type);
    pipelineHashCombine(key, param.shader_visibility);
    pipelineHashCombine(key, param.register_space);
    pipelineHashCombine(key, param.register_index);
    pipelineHashCombine(key, param.num_descriptors);
    pipelineHashCombine(key, param.num_32bit_values);
    pipelineHashCombine(key, param.descriptor_flags);
    pipelineHashCombine(key, param.range_start);
    pipelineHashCombine(key, param.range_count);

    if (param.shader_visibility != 0)
      visibility_specific++;
    noteVisibility(param.shader_visibility);
    if (std::find(register_spaces.begin(), register_spaces.end(),
                  param.register_space) == register_spaces.end())
      register_spaces.push_back(param.register_space);

    if (param.type == 0)
      descriptor_tables++;
    else if (param.type == 1) {
      root_constants++;
      argument_root_constant_dwords += param.num_32bit_values;
    } else if (param.type >= 2 && param.type <= 4) {
      root_descriptors++;
      argument_root_descriptor_slots++;
    }
  }

  for (uint32_t i = 0; i < desc->range_count; i++) {
    const auto &range = desc->ranges[i];
    pipelineHashCombine(key, range.range_type);
    pipelineHashCombine(key, range.num_descriptors);
    pipelineHashCombine(key, range.base_register);
    pipelineHashCombine(key, range.register_space);
    pipelineHashCombine(key, range.offset_in_table);
    pipelineHashCombine(key, range.flags);

    if (std::find(register_spaces.begin(), register_spaces.end(),
                  range.register_space) == register_spaces.end())
      register_spaces.push_back(range.register_space);
    if (range.range_type == 3) {
      sampler_ranges++;
      if (range.num_descriptors != UINT32_MAX)
        argument_sampler_slots += range.num_descriptors;
    } else {
      resource_ranges++;
      if (range.num_descriptors != UINT32_MAX)
        argument_resource_slots += range.num_descriptors;
    }
    if (range.num_descriptors == UINT32_MAX)
      unbounded_ranges++;
    else
      max_table_span = std::max(max_table_span,
                                range.offset_in_table + range.num_descriptors);
  }

  for (uint32_t i = 0; i < desc->static_sampler_count; i++) {
    const auto &sampler = desc->static_samplers[i];
    pipelineHashCombine(key, sampler.shader_register);
    pipelineHashCombine(key, sampler.register_space);
    pipelineHashCombine(key, sampler.shader_visibility);
    if (sampler.shader_visibility != 0)
      visibility_specific++;
    noteVisibility(sampler.shader_visibility);
    argument_sampler_slots++;
    if (std::find(register_spaces.begin(), register_spaces.end(),
                  sampler.register_space) == register_spaces.end())
      register_spaces.push_back(sampler.register_space);
  }

  out_summary->abi_version = M12CORE_ABI_VERSION;
  out_summary->status = M12CORE_ROOT_SIGNATURE_STATUS_OK;
  out_summary->parameter_count = desc->parameter_count;
  out_summary->descriptor_table_count = descriptor_tables;
  out_summary->descriptor_range_count = desc->range_count;
  out_summary->root_descriptor_count = root_descriptors;
  out_summary->root_constant_count = root_constants;
  out_summary->static_sampler_count = desc->static_sampler_count;
  out_summary->resource_range_count = resource_ranges;
  out_summary->sampler_range_count = sampler_ranges;
  out_summary->unbounded_range_count = unbounded_ranges;
  out_summary->visibility_specific_count = visibility_specific;
  out_summary->register_space_count =
      static_cast<uint32_t>(register_spaces.size());
  out_summary->max_descriptor_table_span = max_table_span;
  out_summary->argument_resource_slot_count = argument_resource_slots;
  out_summary->argument_sampler_slot_count = argument_sampler_slots;
  out_summary->argument_root_descriptor_slot_count =
      argument_root_descriptor_slots;
  out_summary->argument_root_constant_dword_count =
      argument_root_constant_dwords;
  out_summary->argument_visibility_mask = argument_visibility_mask;
  out_summary->argument_layout_reserved = 0;
  out_summary->binding_plan_key = key;
  return 0;
}

extern "C" int
m12core_lookup_root_binding(const M12CoreRootBindingLookupDesc *desc,
                            M12CoreRootBindingLookupResult *out_result) {
  if (!desc || !out_result || desc->abi_version != M12CORE_ABI_VERSION)
    return 1;
  if ((desc->parameter_count && !desc->parameters) ||
      (desc->range_count && !desc->ranges) ||
      (desc->static_sampler_count && !desc->static_samplers))
    return 1;

  out_result->abi_version = M12CORE_ABI_VERSION;
  out_result->status = M12CORE_ROOT_SIGNATURE_STATUS_OK;
  out_result->found = 0;
  out_result->root_parameter_index = UINT32_MAX;
  out_result->range_index = UINT32_MAX;
  out_result->descriptor_offset = 0;
  out_result->visibility_fallback = 0;

  if (desc->lookup_kind == M12CORE_ROOT_BINDING_LOOKUP_DESCRIPTOR_RANGE) {
    for (uint32_t visibility_pass = 0; visibility_pass < 2; visibility_pass++) {
      for (uint32_t p = 0; p < desc->parameter_count; p++) {
        const auto &param = desc->parameters[p];
        if (param.type != 0)
          continue;
        if (visibility_pass == 0 &&
            param.shader_visibility != desc->shader_visibility)
          continue;
        if (visibility_pass == 1 && param.shader_visibility != 0)
          continue;
        const uint32_t range_end = param.range_start + param.range_count;
        if (range_end < param.range_start || range_end > desc->range_count)
          continue;
        for (uint32_t r = param.range_start; r < range_end; r++) {
          const auto &range = desc->ranges[r];
          if (range.range_type != desc->range_type ||
              range.register_space != desc->register_space ||
              desc->shader_register < range.base_register)
            continue;
          const uint32_t relative = desc->shader_register - range.base_register;
          if (range.num_descriptors != UINT32_MAX &&
              relative >= range.num_descriptors)
            continue;
          out_result->found = 1;
          out_result->root_parameter_index = p;
          out_result->range_index = r;
          out_result->descriptor_offset = range.offset_in_table + relative;
          out_result->visibility_fallback = visibility_pass == 1 ? 1u : 0u;
          return 0;
        }
      }
    }
    return 0;
  }

  if (desc->lookup_kind == M12CORE_ROOT_BINDING_LOOKUP_STATIC_SAMPLER) {
    for (uint32_t visibility_pass = 0; visibility_pass < 2; visibility_pass++) {
      for (uint32_t i = 0; i < desc->static_sampler_count; i++) {
        const auto &sampler = desc->static_samplers[i];
        if (sampler.shader_register != desc->shader_register ||
            sampler.register_space != desc->register_space)
          continue;
        if (visibility_pass == 0 &&
            sampler.shader_visibility != desc->shader_visibility)
          continue;
        if (visibility_pass == 1 && sampler.shader_visibility != 0)
          continue;
        out_result->found = 1;
        out_result->range_index = i;
        out_result->visibility_fallback = visibility_pass == 1 ? 1u : 0u;
        return 0;
      }
    }
    return 0;
  }

  if (desc->lookup_kind == M12CORE_ROOT_BINDING_LOOKUP_ROOT_DESCRIPTOR) {
    /* Phase 5 root-descriptor lookup seam.  The lookup descriptor reuses
     * range_type to carry the D3D12 root parameter type (CBV/SRV/UAV) for this
     * lookup kind, preserving the fixed PE/unix payload while expanding native
     * coverage beyond descriptor tables and static samplers.
     */
    for (uint32_t visibility_pass = 0; visibility_pass < 2; visibility_pass++) {
      for (uint32_t p = 0; p < desc->parameter_count; p++) {
        const auto &param = desc->parameters[p];
        if (param.type != desc->range_type || param.type < 2 || param.type > 4)
          continue;
        if (param.register_index != desc->shader_register ||
            param.register_space != desc->register_space)
          continue;
        if (visibility_pass == 0 &&
            param.shader_visibility != desc->shader_visibility)
          continue;
        if (visibility_pass == 1 && param.shader_visibility != 0)
          continue;
        out_result->found = 1;
        out_result->root_parameter_index = p;
        out_result->visibility_fallback = visibility_pass == 1 ? 1u : 0u;
        return 0;
      }
    }
    return 0;
  }

  if (desc->lookup_kind == M12CORE_ROOT_BINDING_LOOKUP_ROOT_CONSTANTS) {
    /* Phase 5 root-constants lookup seam.  Root constants are type==1 in the
     * public root binding parameter stream; for this lookup kind the fixed
     * result payload reuses descriptor_offset to return Num32BitValues so no
     * new PE/unix thunk structure is needed before live binding migration.
     */
    for (uint32_t visibility_pass = 0; visibility_pass < 2; visibility_pass++) {
      for (uint32_t p = 0; p < desc->parameter_count; p++) {
        const auto &param = desc->parameters[p];
        if (param.type != 1)
          continue;
        if (param.register_index != desc->shader_register ||
            param.register_space != desc->register_space)
          continue;
        if (visibility_pass == 0 &&
            param.shader_visibility != desc->shader_visibility)
          continue;
        if (visibility_pass == 1 && param.shader_visibility != 0)
          continue;
        out_result->found = 1;
        out_result->root_parameter_index = p;
        out_result->descriptor_offset = param.num_32bit_values;
        out_result->visibility_fallback = visibility_pass == 1 ? 1u : 0u;
        return 0;
      }
    }
    return 0;
  }

  out_result->status = M12CORE_ROOT_SIGNATURE_STATUS_INVALID;
  return 1;
}

extern "C" int
m12core_summarize_prewarm_pack(const M12CorePrewarmPackDesc *desc,
                               M12CorePrewarmPackSummary *out_summary) {
  if (!desc || !out_summary || desc->abi_version != M12CORE_ABI_VERSION)
    return 1;
  std::memset(out_summary, 0, sizeof(*out_summary));
  if ((desc->pipeline_count && !desc->pipelines) ||
      (desc->stage_count && !desc->stages))
    return 1;

  /* Phase 6 oracle/prewarm ingestion seam.  libm12core consumes only compact,
   * profile-gated metadata: pipeline/root/shader keys, stage linkage, and
   * expected resource-layout summaries.  Raw D3DMetal cache payloads,
   * extracted metallibs, and DXBC blobs deliberately stay outside this ABI.
   * Later slices can use this validated POD model to schedule actual profile
   * prewarm work without proving binary compatibility in the same change.
   */
  uint64_t key =
      desc->source_pack_key ? desc->source_pack_key : 0x4d3132505245574dull;
  pipelineHashCombine(key, desc->flags);
  pipelineHashCombine(key, desc->appid);
  pipelineHashCombine(key, desc->profile_key);
  pipelineHashCombine(key, desc->pipeline_count);
  pipelineHashCombine(key, desc->stage_count);

  uint32_t render_pipelines = 0;
  uint32_t compute_pipelines = 0;
  uint32_t ordered_pipelines = 0;
  uint32_t max_stage_links = 0;
  uint32_t expected_resource_slots = 0;
  uint32_t expected_sampler_slots = 0;
  uint32_t expected_root_descriptor_slots = 0;
  uint32_t expected_root_constant_dwords = 0;
  std::vector<uint64_t> unique_roots;
  std::vector<uint64_t> unique_shaders;

  for (uint32_t i = 0; i < desc->pipeline_count; i++) {
    const auto &pipeline = desc->pipelines[i];
    pipelineHashCombine(key, pipeline.pipeline_key);
    pipelineHashCombine(key, pipeline.root_signature_key);
    pipelineHashCombine(key, pipeline.root_structural_hash);
    pipelineHashCombine(key, pipeline.stage_mask);
    pipelineHashCombine(key, pipeline.prewarm_order);
    pipelineHashCombine(key, pipeline.stage_start);
    pipelineHashCombine(key, pipeline.stage_count);
    pipelineHashCombine(key, pipeline.argument_resource_slot_count);
    pipelineHashCombine(key, pipeline.argument_sampler_slot_count);
    pipelineHashCombine(key, pipeline.argument_root_descriptor_slot_count);
    pipelineHashCombine(key, pipeline.argument_root_constant_dword_count);
    pipelineHashCombine(key, pipeline.expected_layout_flags);

    if (pipeline.stage_start > desc->stage_count ||
        pipeline.stage_count > desc->stage_count - pipeline.stage_start) {
      out_summary->abi_version = M12CORE_ABI_VERSION;
      out_summary->status = M12CORE_PREWARM_PACK_STATUS_INVALID;
      return 0;
    }

    if (pipeline.stage_mask & M12CORE_PREWARM_STAGE_MASK_COMPUTE)
      compute_pipelines++;
    else
      render_pipelines++;
    if (pipeline.prewarm_order != UINT32_MAX)
      ordered_pipelines++;
    max_stage_links = std::max(max_stage_links, pipeline.stage_count);
    expected_resource_slots += pipeline.argument_resource_slot_count;
    expected_sampler_slots += pipeline.argument_sampler_slot_count;
    expected_root_descriptor_slots +=
        pipeline.argument_root_descriptor_slot_count;
    expected_root_constant_dwords +=
        pipeline.argument_root_constant_dword_count;
    if (pipeline.root_signature_key &&
        std::find(unique_roots.begin(), unique_roots.end(),
                  pipeline.root_signature_key) == unique_roots.end())
      unique_roots.push_back(pipeline.root_signature_key);
  }

  for (uint32_t i = 0; i < desc->stage_count; i++) {
    const auto &stage = desc->stages[i];
    pipelineHashCombine(key, stage.stage);
    pipelineHashCombine(key, stage.shader_key);
    pipelineHashCombine(key, stage.shader_bytecode_hash);
    pipelineHashCombine(key, stage.root_structural_hash);
    if (stage.shader_key &&
        std::find(unique_shaders.begin(), unique_shaders.end(),
                  stage.shader_key) == unique_shaders.end())
      unique_shaders.push_back(stage.shader_key);
  }

  out_summary->abi_version = M12CORE_ABI_VERSION;
  out_summary->status = M12CORE_PREWARM_PACK_STATUS_OK;
  out_summary->pipeline_count = desc->pipeline_count;
  out_summary->stage_link_count = desc->stage_count;
  out_summary->render_pipeline_count = render_pipelines;
  out_summary->compute_pipeline_count = compute_pipelines;
  out_summary->unique_root_count = static_cast<uint32_t>(unique_roots.size());
  out_summary->unique_shader_count =
      static_cast<uint32_t>(unique_shaders.size());
  out_summary->ordered_pipeline_count = ordered_pipelines;
  out_summary->max_stage_links_per_pipeline = max_stage_links;
  out_summary->expected_resource_slots = expected_resource_slots;
  out_summary->expected_sampler_slots = expected_sampler_slots;
  out_summary->expected_root_descriptor_slots = expected_root_descriptor_slots;
  out_summary->expected_root_constant_dwords = expected_root_constant_dwords;
  out_summary->prewarm_pack_key = key;
  return 0;
}

extern "C" int m12core_build_draw_plan(const M12CoreDrawPlanDesc *desc,
                                       M12CoreDrawPlanSummary *out_summary) {
  if (!desc || !out_summary || desc->abi_version != M12CORE_ABI_VERSION)
    return 1;
  std::memset(out_summary, 0, sizeof(*out_summary));

  /* Phase 7 command-planning seam.  The native core receives only compact
   * scalar state needed to classify draw work: PSO/root/binding keys, argument
   * layout pressure, attachment count, descriptor heap count, and barrier
   * count.  It does not receive command-list objects, descriptor heap pointers,
   * Metal encoders, or resource handles, and it never executes commands here.
   */
  uint64_t key = 0x4d31324452415750ull; // "M12DRAWP" marker.
  pipelineHashCombine(key, desc->flags);
  pipelineHashCombine(key, desc->pso_key);
  pipelineHashCombine(key, desc->root_signature_key);
  pipelineHashCombine(key, desc->binding_plan_key);
  pipelineHashCombine(key, desc->root_parameter_count);
  pipelineHashCombine(key, desc->descriptor_table_count);
  pipelineHashCombine(key, desc->root_descriptor_count);
  pipelineHashCombine(key, desc->root_constant_count);
  pipelineHashCombine(key, desc->argument_resource_slot_count);
  pipelineHashCombine(key, desc->argument_sampler_slot_count);
  pipelineHashCombine(key, desc->argument_root_descriptor_slot_count);
  pipelineHashCombine(key, desc->argument_root_constant_dword_count);
  pipelineHashCombine(key, desc->render_target_count);
  pipelineHashCombine(key, desc->resource_barrier_count);
  pipelineHashCombine(key, desc->descriptor_heap_count);

  uint32_t validation_flags = 0;
  if (!(desc->flags & M12CORE_DRAW_PLAN_HAS_GRAPHICS_PSO) || !desc->pso_key)
    validation_flags |= 1u << 0;
  if (!(desc->flags & M12CORE_DRAW_PLAN_HAS_ROOT_SIGNATURE) ||
      !desc->root_signature_key || !desc->binding_plan_key)
    validation_flags |= 1u << 1;
  if (!(desc->flags & M12CORE_DRAW_PLAN_HAS_RENDER_TARGET))
    validation_flags |= 1u << 2;
  if (desc->descriptor_table_count > desc->root_parameter_count)
    validation_flags |= 1u << 3;

  uint32_t resource_usage =
      desc->argument_resource_slot_count +
      desc->argument_root_descriptor_slot_count + desc->render_target_count +
      ((desc->flags & M12CORE_DRAW_PLAN_HAS_DEPTH_STENCIL) ? 1u : 0u);
  uint32_t binding_validations = desc->descriptor_table_count +
                                 desc->root_descriptor_count +
                                 desc->root_constant_count;
  uint32_t redundant_candidates =
      desc->descriptor_heap_count > 1 ? desc->descriptor_heap_count - 1 : 0;
  uint32_t attachments =
      desc->render_target_count +
      ((desc->flags & M12CORE_DRAW_PLAN_HAS_DEPTH_STENCIL) ? 1u : 0u);
  uint32_t descriptor_pressure =
      desc->argument_resource_slot_count + desc->argument_sampler_slot_count +
      desc->argument_root_descriptor_slot_count +
      desc->argument_root_constant_dword_count + desc->descriptor_table_count;

  out_summary->abi_version = M12CORE_ABI_VERSION;
  out_summary->status = M12CORE_DRAW_PLAN_STATUS_OK;
  out_summary->flags = desc->flags;
  out_summary->validation_flags = validation_flags;
  out_summary->resource_usage_count = resource_usage;
  out_summary->binding_validation_count = binding_validations;
  out_summary->redundant_binding_candidate_count = redundant_candidates;
  out_summary->render_pass_attachment_count = attachments;
  out_summary->descriptor_pressure_score = descriptor_pressure;
  out_summary->draw_plan_key = key;
  return 0;
}

extern "C" int
m12core_build_present_plan(const M12CorePresentPlanDesc *desc,
                           M12CorePresentPlanSummary *out_summary) {
  if (!desc || !out_summary || desc->abi_version != M12CORE_ABI_VERSION)
    return 1;
  std::memset(out_summary, 0, sizeof(*out_summary));

  /* Phase 9 present-planning seam.  This keeps actual drawable acquisition,
   * blit/render encoder work, synchronization, and command-buffer commits in
   * the existing PE/DXMT path while libm12core owns compact present-path
   * classification/keying.  Only scalar identifiers and counts cross the ABI.
   */
  uint64_t key = 0x4d31325052455350ull; // "M12PRESP" marker.
  pipelineHashCombine(key, desc->flags);
  pipelineHashCombine(key, desc->width);
  pipelineHashCombine(key, desc->height);
  pipelineHashCombine(key, desc->format);
  pipelineHashCombine(key, desc->buffer_index);
  pipelineHashCombine(key, desc->buffer_count);
  pipelineHashCombine(key, desc->sync_interval);
  pipelineHashCombine(key, desc->present_flags);
  pipelineHashCombine(key, desc->work_classification);
  pipelineHashCombine(key, desc->command_buffer_status);
  pipelineHashCombine(key, desc->present_count);
  pipelineHashCombine(key, desc->source_texture_key);
  pipelineHashCombine(key, desc->drawable_texture_key);
  pipelineHashCombine(key, desc->queue_serial);
  pipelineHashCombine(key, desc->command_count);
  pipelineHashCombine(key, desc->draw_count);
  pipelineHashCombine(key, desc->dispatch_count);
  pipelineHashCombine(key, desc->clear_count);
  pipelineHashCombine(key, desc->wait_seq);

  uint32_t validation_flags = 0;
  if (!(desc->flags & M12CORE_PRESENT_PLAN_HAS_BACKBUFFER))
    validation_flags |= 1u << 0;
  if (!(desc->flags & M12CORE_PRESENT_PLAN_HAS_SOURCE_TEXTURE) ||
      !desc->source_texture_key)
    validation_flags |= 1u << 1;
  if (!desc->width || !desc->height)
    validation_flags |= 1u << 2;
  if ((desc->flags & M12CORE_PRESENT_PLAN_USES_RAW_BLIT) &&
      !(desc->flags & M12CORE_PRESENT_PLAN_HAS_DRAWABLE))
    validation_flags |= 1u << 3;

  uint32_t path = 0;
  if (desc->flags & M12CORE_PRESENT_PLAN_USES_PRESENTER)
    path = 1;
  else if (desc->flags & M12CORE_PRESENT_PLAN_USES_RAW_BLIT)
    path = 2;

  uint64_t scheduled = desc->command_count + desc->draw_count +
                       desc->dispatch_count + desc->clear_count;
  uint32_t hazard_score = 0;
  if (desc->flags & M12CORE_PRESENT_PLAN_WAITED_FOR_RENDER)
    hazard_score++;
  if (desc->draw_count || desc->dispatch_count)
    hazard_score++;
  if (desc->clear_count && !desc->draw_count)
    hazard_score++;
  if (desc->flags & M12CORE_PRESENT_PLAN_LIVE_PRESENT)
    hazard_score += 2;

  out_summary->abi_version = M12CORE_ABI_VERSION;
  out_summary->status = M12CORE_PRESENT_PLAN_STATUS_OK;
  out_summary->flags = desc->flags;
  out_summary->validation_flags = validation_flags;
  out_summary->present_path = path;
  out_summary->work_classification = desc->work_classification;
  out_summary->hazard_score = hazard_score;
  out_summary->present_plan_key = key;
  out_summary->scheduled_work_count = scheduled;
  return 0;
}

extern "C" int
m12core_plan_present_execute(const M12CorePresentExecuteDesc *desc,
                             M12CorePresentExecuteSummary *out_summary) {
  if (!desc || !out_summary || desc->abi_version != M12CORE_ABI_VERSION)
    return 1;
  std::memset(out_summary, 0, sizeof(*out_summary));

  /* Slice 4 present-execution seam.  libm12core owns only scalar
   * support/fallback classification; Metal command-buffer, texture, drawable,
   * encoder, present, and commit handles remain outside this ABI.
   */
  uint64_t key = 0x4d31325058454355ull; // "M12PXECU" marker.
  pipelineHashCombine(key, desc->flags);
  pipelineHashCombine(key, desc->width);
  pipelineHashCombine(key, desc->height);
  pipelineHashCombine(key, desc->format);
  pipelineHashCombine(key, desc->buffer_index);
  pipelineHashCombine(key, desc->buffer_count);
  pipelineHashCombine(key, desc->sync_interval);
  pipelineHashCombine(key, desc->present_flags);
  pipelineHashCombine(key, desc->work_classification);
  pipelineHashCombine(key, desc->command_buffer_status);
  pipelineHashCombine(key, desc->present_count);
  pipelineHashCombine(key, desc->source_texture_key);
  pipelineHashCombine(key, desc->drawable_texture_key);
  pipelineHashCombine(key, desc->queue_serial);
  pipelineHashCombine(key, desc->command_count);
  pipelineHashCombine(key, desc->draw_count);
  pipelineHashCombine(key, desc->dispatch_count);
  pipelineHashCombine(key, desc->clear_count);
  pipelineHashCombine(key, desc->wait_seq);

  uint32_t validation = 0;
  uint32_t fallback = M12CORE_PRESENT_EXECUTE_FALLBACK_NONE;
  if (!(desc->flags & M12CORE_PRESENT_EXECUTE_GATE_ENABLED))
    fallback = M12CORE_PRESENT_EXECUTE_FALLBACK_GATE_DISABLED;
  else if (!(desc->flags & M12CORE_PRESENT_EXECUTE_HAS_SOURCE_TEXTURE))
    fallback = M12CORE_PRESENT_EXECUTE_FALLBACK_MISSING_SOURCE;
  else if (!(desc->flags & M12CORE_PRESENT_EXECUTE_HAS_DRAWABLE))
    fallback = M12CORE_PRESENT_EXECUTE_FALLBACK_MISSING_DRAWABLE;
  else if (!(desc->flags & M12CORE_PRESENT_EXECUTE_USES_RAW_BLIT) ||
           (desc->flags & M12CORE_PRESENT_EXECUTE_USES_PRESENTER))
    fallback = M12CORE_PRESENT_EXECUTE_FALLBACK_NON_RAW_PATH;
  else if ((desc->flags & M12CORE_PRESENT_EXECUTE_LIVE_PRESENT) ||
           (desc->flags & M12CORE_PRESENT_EXECUTE_READBACK_REQUESTED))
    fallback = M12CORE_PRESENT_EXECUTE_FALLBACK_DIAGNOSTIC_ACTIVE;
  else if (!desc->width || !desc->height)
    fallback = M12CORE_PRESENT_EXECUTE_FALLBACK_ZERO_EXTENT;
  else if (!(desc->flags & M12CORE_PRESENT_EXECUTE_FORMAT_SUPPORTED))
    fallback = M12CORE_PRESENT_EXECUTE_FALLBACK_UNSUPPORTED_FORMAT;

  if (!(desc->flags & M12CORE_PRESENT_EXECUTE_HAS_SOURCE_TEXTURE))
    validation |= 1u << 0;
  if (!(desc->flags & M12CORE_PRESENT_EXECUTE_HAS_DRAWABLE))
    validation |= 1u << 1;
  if (!(desc->flags & M12CORE_PRESENT_EXECUTE_USES_RAW_BLIT))
    validation |= 1u << 2;
  if (desc->flags & M12CORE_PRESENT_EXECUTE_USES_PRESENTER)
    validation |= 1u << 3;
  if (desc->flags & M12CORE_PRESENT_EXECUTE_LIVE_PRESENT)
    validation |= 1u << 4;
  if (desc->flags & M12CORE_PRESENT_EXECUTE_READBACK_REQUESTED)
    validation |= 1u << 5;
  if (!(desc->flags & M12CORE_PRESENT_EXECUTE_GATE_ENABLED))
    validation |= 1u << 6;
  if (!desc->width || !desc->height)
    validation |= 1u << 7;
  if (!(desc->flags & M12CORE_PRESENT_EXECUTE_FORMAT_SUPPORTED))
    validation |= 1u << 8;

  const bool supported = fallback == M12CORE_PRESENT_EXECUTE_FALLBACK_NONE;
  uint32_t hazard_score = 0;
  if (desc->sync_interval)
    hazard_score += 1;
  if (desc->present_flags)
    hazard_score += 1;
  if (desc->command_buffer_status != 2)
    hazard_score += 1;
  if (desc->wait_seq)
    hazard_score += 1;

  out_summary->abi_version = M12CORE_ABI_VERSION;
  out_summary->status = M12CORE_PRESENT_EXECUTE_STATUS_OK;
  out_summary->flags = 0;
  if (supported)
    out_summary->flags |= M12CORE_PRESENT_EXECUTE_SUMMARY_SUPPORTED;
  if (desc->flags & M12CORE_PRESENT_EXECUTE_GATE_ENABLED)
    out_summary->flags |= M12CORE_PRESENT_EXECUTE_SUMMARY_GATE_ENABLED;
  out_summary->fallback_reason = fallback;
  out_summary->validation_flags = validation;
  out_summary->planned_operation_count = supported ? 3u : 0u;
  out_summary->hazard_score = hazard_score;
  out_summary->present_execute_key = key;
  out_summary->scheduled_work_count = supported ? desc->command_count + 3u : 0u;
  return 0;
}

extern "C" int
m12core_build_replay_plan(const M12CoreReplayPlanDesc *desc,
                          M12CoreReplayPlanSummary *out_summary) {
  if (!desc || !out_summary || desc->abi_version != M12CORE_ABI_VERSION)
    return 1;
  std::memset(out_summary, 0, sizeof(*out_summary));

  /* Phase 9 replay-planning seam.  Native code now owns deterministic replay
   * stream classification/keying, while command decoding, encoder lifetime,
   * command-buffer commits, synchronization, and hazard enforcement remain in
   * the existing PE/DXMT replay path.  Only scalar counters and status values
   * cross the ABI.
   */
  uint64_t key = 0x4d31325245504c59ull; // "M12REPLY" marker.
  pipelineHashCombine(key, desc->flags);
  pipelineHashCombine(key, desc->queue_type);
  pipelineHashCombine(key, desc->command_list_index);
  pipelineHashCombine(key, desc->command_buffer_status);
  pipelineHashCombine(key, desc->command_list_id);
  pipelineHashCombine(key, desc->queue_serial);
  pipelineHashCombine(key, desc->command_count);
  pipelineHashCombine(key, desc->draw_count);
  pipelineHashCombine(key, desc->indexed_draw_count);
  pipelineHashCombine(key, desc->indirect_count);
  pipelineHashCombine(key, desc->dispatch_count);
  pipelineHashCombine(key, desc->clear_rtv_count);
  pipelineHashCombine(key, desc->clear_dsv_count);
  pipelineHashCombine(key, desc->clear_uav_count);
  pipelineHashCombine(key, (uint64_t)desc->replay_ms);
  pipelineHashCombine(key, (uint64_t)desc->wait_ms);

  const uint64_t draw_work =
      desc->draw_count + desc->indexed_draw_count + desc->indirect_count;
  const uint64_t clear_work =
      desc->clear_rtv_count + desc->clear_dsv_count + desc->clear_uav_count;
  const uint64_t scheduled =
      desc->command_count + draw_work + desc->dispatch_count + clear_work;

  uint32_t validation_flags = 0;
  if (!(desc->flags & M12CORE_REPLAY_PLAN_HAS_COMMAND_STREAM) ||
      !desc->command_count)
    validation_flags |= 1u << 0;
  if ((desc->flags & M12CORE_REPLAY_PLAN_HAS_SWAPCHAIN_WORK) &&
      !desc->queue_serial)
    validation_flags |= 1u << 1;
  if ((desc->flags & M12CORE_REPLAY_PLAN_COMMAND_BUFFER_COMPLETED) &&
      !(desc->flags & M12CORE_REPLAY_PLAN_SYNC_EXECUTE))
    validation_flags |= 1u << 2;

  uint32_t classification = 0;
  if (draw_work)
    classification = 1;
  else if (desc->dispatch_count)
    classification = 2;
  else if (clear_work)
    classification = 3;
  else if (desc->command_count)
    classification = 4;

  uint32_t hazard_score = 0;
  if (desc->flags & M12CORE_REPLAY_PLAN_HAS_SWAPCHAIN_TARGET)
    hazard_score += 2;
  if (desc->flags & M12CORE_REPLAY_PLAN_HAS_SWAPCHAIN_WORK)
    hazard_score += 1;
  if (desc->flags & M12CORE_REPLAY_PLAN_SYNC_EXECUTE)
    hazard_score += 1;
  if (draw_work && desc->dispatch_count)
    hazard_score += 1;
  if (desc->wait_ms > 0)
    hazard_score += 1;

  uint32_t execution_path = 0;
  if (desc->flags & M12CORE_REPLAY_PLAN_HAS_GRAPHICS_WORK)
    execution_path = 1;
  else if (desc->flags & M12CORE_REPLAY_PLAN_HAS_COMPUTE_WORK)
    execution_path = 2;
  else if (desc->flags & M12CORE_REPLAY_PLAN_HAS_CLEAR_WORK)
    execution_path = 3;

  out_summary->abi_version = M12CORE_ABI_VERSION;
  out_summary->status = M12CORE_REPLAY_PLAN_STATUS_OK;
  out_summary->flags = desc->flags;
  out_summary->validation_flags = validation_flags;
  out_summary->work_classification = classification;
  out_summary->execution_path = execution_path;
  out_summary->hazard_score = hazard_score;
  out_summary->replay_plan_key = key;
  out_summary->scheduled_work_count = scheduled;
  return 0;
}

extern "C" int
m12core_plan_replay_execute(const M12CoreReplayExecuteDesc *desc,
                            M12CoreReplayExecuteSummary *out_summary) {
  if (!desc || !out_summary || desc->abi_version != M12CORE_ABI_VERSION)
    return 1;
  std::memset(out_summary, 0, sizeof(*out_summary));

  /* Slice 5 replay-execute ownership seam.  libm12core owns the scalar
   * command support table and eligibility/fallback decision.  PE/DXMT still
   * owns command decoding and execution until POD command packets plus
   * native-owned resource/pipeline/root handles exist.
   */
  uint64_t key = 0x4d31325245584543ull; // "M12REXEC" marker.
  pipelineHashCombine(key, desc->flags);
  pipelineHashCombine(key, desc->queue_type);
  pipelineHashCombine(key, desc->command_list_index);
  pipelineHashCombine(key, desc->command_count);
  pipelineHashCombine(key, desc->draw_count);
  pipelineHashCombine(key, desc->indexed_draw_count);
  pipelineHashCombine(key, desc->dispatch_count);
  pipelineHashCombine(key, desc->clear_count);
  pipelineHashCombine(key, desc->indirect_count);
  pipelineHashCombine(key, desc->barrier_count);
  pipelineHashCombine(key, desc->root_binding_count);
  pipelineHashCombine(key, desc->descriptor_heap_count);
  pipelineHashCombine(key, desc->render_target_count);
  pipelineHashCombine(key, desc->command_buffer_status);
  pipelineHashCombine(key, desc->command_list_id);
  pipelineHashCombine(key, desc->queue_serial);

  uint32_t supported_mask = 0;
  if (desc->clear_count)
    supported_mask |= 1u << 0;
  if (desc->draw_count)
    supported_mask |= 1u << 1;
  if (desc->indexed_draw_count)
    supported_mask |= 1u << 2;
  if (desc->dispatch_count)
    supported_mask |= 1u << 3;
  if (desc->barrier_count)
    supported_mask |= 1u << 4;
  if (desc->root_binding_count)
    supported_mask |= 1u << 5;
  if (desc->descriptor_heap_count)
    supported_mask |= 1u << 6;

  uint32_t unsupported_mask = 0;
  if (desc->indirect_count)
    unsupported_mask |= 1u << 0;
  if (desc->flags & M12CORE_REPLAY_EXECUTE_HAS_CORRUPTION)
    unsupported_mask |= 1u << 1;

  uint32_t fallback = M12CORE_REPLAY_EXECUTE_FALLBACK_NONE;
  if (!(desc->flags & M12CORE_REPLAY_EXECUTE_GATE_ENABLED))
    fallback = M12CORE_REPLAY_EXECUTE_FALLBACK_GATE_DISABLED;
  else if (!desc->command_count)
    fallback = M12CORE_REPLAY_EXECUTE_FALLBACK_EMPTY_STREAM;
  else if (desc->indirect_count)
    fallback = M12CORE_REPLAY_EXECUTE_FALLBACK_UNSUPPORTED_INDIRECT;
  else if (desc->flags & M12CORE_REPLAY_EXECUTE_HAS_CORRUPTION)
    fallback = M12CORE_REPLAY_EXECUTE_FALLBACK_CORRUPT_STREAM;
  else if (!supported_mask)
    fallback = M12CORE_REPLAY_EXECUTE_FALLBACK_UNSUPPORTED_SHAPE;

  const bool eligible = fallback == M12CORE_REPLAY_EXECUTE_FALLBACK_NONE;
  out_summary->abi_version = M12CORE_ABI_VERSION;
  out_summary->status = M12CORE_REPLAY_EXECUTE_STATUS_OK;
  if (eligible)
    out_summary->flags |= M12CORE_REPLAY_EXECUTE_SUMMARY_ELIGIBLE;
  if (desc->flags & M12CORE_REPLAY_EXECUTE_GATE_ENABLED)
    out_summary->flags |= M12CORE_REPLAY_EXECUTE_SUMMARY_GATE_ENABLED;
  out_summary->fallback_reason = fallback;
  out_summary->validation_flags = unsupported_mask;
  out_summary->supported_command_mask = supported_mask;
  out_summary->unsupported_command_mask = unsupported_mask;
  out_summary->planned_packet_count = eligible ? desc->command_count : 0;
  out_summary->replay_classification =
      desc->draw_count || desc->indexed_draw_count ? 1u
      : desc->dispatch_count                       ? 2u
      : desc->clear_count                          ? 3u
                                                   : 0u;
  out_summary->hazard_score =
      desc->barrier_count + desc->render_target_count +
      ((desc->flags & M12CORE_REPLAY_EXECUTE_HAS_SWAPCHAIN_TARGET) ? 2u : 0u);
  out_summary->replay_execute_key = key;
  out_summary->scheduled_work_count = eligible ? desc->command_count : 0;
  return 0;
}

extern "C" int
m12core_validate_command_stream(const M12CoreCommandStreamDesc *desc,
                                M12CoreCommandStreamSummary *out_summary) {
  if (!desc || !out_summary || desc->abi_version != M12CORE_ABI_VERSION)
    return 1;
  std::memset(out_summary, 0, sizeof(*out_summary));

  /* Slice 2 command-stream descriptor seam.  The PE replay path still owns
   * command decoding, Metal encoder lifetime, resource barriers, commits, and
   * synchronization.  libm12core owns the scalar execution descriptor
   * classification and drift checks so later slices can make execution
   * decisions from stable C/POD state instead of PE C++ objects.
   */
  const uint32_t draw_work =
      desc->draw_count + desc->indexed_draw_count + desc->indirect_count;
  const uint32_t clear_work =
      desc->clear_rtv_count + desc->clear_dsv_count + desc->clear_uav_count;
  const uint32_t graphics_bindings =
      desc->set_graphics_root_sig_count +
      desc->set_graphics_root_binding_count + desc->ia_binding_count +
      desc->viewport_scissor_count + desc->om_set_render_targets_count +
      desc->set_pso_count;
  const uint32_t compute_bindings =
      desc->set_compute_root_sig_count + desc->set_compute_root_binding_count;

  uint32_t expected_flags = 0;
  if (desc->command_count)
    expected_flags |= M12CORE_COMMAND_STREAM_HAS_COMMANDS;
  if (draw_work)
    expected_flags |= M12CORE_COMMAND_STREAM_HAS_GRAPHICS_WORK;
  if (desc->dispatch_count)
    expected_flags |= M12CORE_COMMAND_STREAM_HAS_COMPUTE_WORK;
  if (clear_work)
    expected_flags |= M12CORE_COMMAND_STREAM_HAS_CLEAR_WORK;
  if (graphics_bindings)
    expected_flags |= M12CORE_COMMAND_STREAM_HAS_GRAPHICS_SETUP;
  if (compute_bindings)
    expected_flags |= M12CORE_COMMAND_STREAM_HAS_COMPUTE_SETUP;
  if (desc->final_render_target_count)
    expected_flags |= M12CORE_COMMAND_STREAM_HAS_RENDER_TARGETS;
  if (desc->final_has_dsv)
    expected_flags |= M12CORE_COMMAND_STREAM_HAS_DSV;
  if (desc->final_descriptor_heap_count)
    expected_flags |= M12CORE_COMMAND_STREAM_HAS_DESCRIPTOR_HEAPS;
  if (desc->swapchain_touched_count)
    expected_flags |= M12CORE_COMMAND_STREAM_HAS_SWAPCHAIN_TOUCH;
  if (desc->flags & M12CORE_COMMAND_STREAM_HAS_SWAPCHAIN_TARGET)
    expected_flags |= M12CORE_COMMAND_STREAM_HAS_SWAPCHAIN_TARGET;
  if (desc->flags & M12CORE_COMMAND_STREAM_CORRUPT)
    expected_flags |= M12CORE_COMMAND_STREAM_CORRUPT;

  uint64_t key = 0x4d3132435354524dull; // "M12CSTRM" marker.
  pipelineHashCombine(key, desc->flags);
  pipelineHashCombine(key, expected_flags);
  pipelineHashCombine(key, desc->queue_type);
  pipelineHashCombine(key, desc->command_list_index);
  pipelineHashCombine(key, desc->command_buffer_status);
  pipelineHashCombine(key, desc->command_count);
  pipelineHashCombine(key, draw_work);
  pipelineHashCombine(key, desc->dispatch_count);
  pipelineHashCombine(key, clear_work);
  pipelineHashCombine(key, graphics_bindings);
  pipelineHashCombine(key, compute_bindings);
  pipelineHashCombine(key, desc->final_render_target_count);
  pipelineHashCombine(key, desc->final_has_dsv);
  pipelineHashCombine(key, desc->final_descriptor_heap_count);
  pipelineHashCombine(key, desc->swapchain_touched_count);
  pipelineHashCombine(key, desc->command_list_id);
  pipelineHashCombine(key, desc->queue_serial);

  uint32_t classification = 0;
  if (draw_work)
    classification = 1;
  else if (desc->dispatch_count)
    classification = 2;
  else if (clear_work)
    classification = 3;
  else if (graphics_bindings || compute_bindings)
    classification = 4;

  uint32_t execution_path = 0;
  if (desc->flags & M12CORE_COMMAND_STREAM_CORRUPT)
    execution_path = 5;
  else if (draw_work || graphics_bindings)
    execution_path = 1;
  else if (desc->dispatch_count || compute_bindings)
    execution_path = 2;
  else if (clear_work)
    execution_path = 3;
  else if (desc->command_count)
    execution_path = 4;

  const uint32_t attachment_count =
      desc->final_render_target_count + (desc->final_has_dsv ? 1u : 0u);
  const uint32_t descriptor_pressure = desc->final_descriptor_heap_count +
                                       desc->set_graphics_root_binding_count +
                                       desc->set_compute_root_binding_count;
  const uint32_t binding_changes = graphics_bindings + compute_bindings;
  const uint64_t scheduled = (uint64_t)desc->command_count + draw_work +
                             desc->dispatch_count + clear_work;

  out_summary->abi_version = M12CORE_ABI_VERSION;
  out_summary->status = M12CORE_COMMAND_STREAM_STATUS_OK;
  out_summary->input_flags = desc->flags;
  out_summary->expected_flags = expected_flags;
  out_summary->drift_flags = desc->flags ^ expected_flags;
  out_summary->work_classification = classification;
  out_summary->execution_path = execution_path;
  out_summary->descriptor_pressure_score = descriptor_pressure;
  out_summary->attachment_count = attachment_count;
  out_summary->binding_change_count = binding_changes;
  out_summary->command_stream_key = key;
  out_summary->scheduled_work_count = scheduled;
  return 0;
}

extern "C" int m12core_validate_command_packet_stream(
    const M12CoreCommandPacketStreamDesc *desc,
    M12CoreCommandPacketStreamSummary *out_summary) {
  if (!desc || !out_summary || desc->abi_version != M12CORE_ABI_VERSION)
    return 1;
  std::memset(out_summary, 0, sizeof(*out_summary));

  out_summary->abi_version = M12CORE_ABI_VERSION;
  out_summary->status = M12CORE_COMMAND_PACKET_STREAM_STATUS_OK;

  if (desc->packet_count && !desc->packets) {
    out_summary->status = M12CORE_COMMAND_PACKET_STREAM_STATUS_INVALID;
    out_summary->summary_flags |=
        M12CORE_COMMAND_PACKET_STREAM_SUMMARY_HAS_INVALID;
    out_summary->invalid_packet_count = desc->packet_count;
    return 0;
  }

  uint64_t key = 0x4d31325041434b54ull; // "M12PACKT" marker.
  pipelineHashCombine(key, desc->packet_count);
  pipelineHashCombine(key, desc->queue_type);
  pipelineHashCombine(key, desc->command_list_index);
  pipelineHashCombine(key, desc->command_list_id);
  pipelineHashCombine(key, desc->queue_serial);

  for (uint32_t i = 0; i < desc->packet_count; i++) {
    const M12CoreCommandPacket &packet = desc->packets[i];
    const uint32_t kind = packet.header.kind;
    bool invalid = packet.header.abi_version != M12CORE_ABI_VERSION ||
                   kind == M12CORE_COMMAND_PACKET_KIND_UNKNOWN ||
                   kind > M12CORE_COMMAND_PACKET_KIND_PRESENT;
    bool unsupported =
        (packet.header.flags & M12CORE_COMMAND_PACKET_FLAG_UNSUPPORTED) != 0;

    pipelineHashCombine(key, packet.header.kind);
    pipelineHashCombine(key, packet.header.flags);
    pipelineHashCombine(key, packet.header.payload_qwords);
    pipelineHashCombine(key, packet.header.sequence);
    pipelineHashCombine(key, packet.object_id0);
    pipelineHashCombine(key, packet.object_id1);
    pipelineHashCombine(key, packet.object_id2);
    pipelineHashCombine(key, packet.object_id3);
    pipelineHashCombine(key, packet.value0);
    pipelineHashCombine(key, packet.value1);
    pipelineHashCombine(key, packet.value2);
    pipelineHashCombine(key, packet.value3);
    out_summary->packet_sequence_xor ^= packet.header.sequence;

    if (invalid) {
      out_summary->invalid_packet_count++;
      out_summary->summary_flags |=
          M12CORE_COMMAND_PACKET_STREAM_SUMMARY_HAS_INVALID;
      continue;
    }
    if (unsupported) {
      out_summary->unsupported_packet_count++;
      out_summary->summary_flags |=
          M12CORE_COMMAND_PACKET_STREAM_SUMMARY_HAS_UNSUPPORTED;
    }

    bool counted_graphics = false;
    bool counted_compute = false;
    bool counted_copy = false;
    bool counted_present = false;

    switch (kind) {
    case M12CORE_COMMAND_PACKET_KIND_SET_PIPELINE:
    case M12CORE_COMMAND_PACKET_KIND_SET_ROOT_SIGNATURE:
    case M12CORE_COMMAND_PACKET_KIND_SET_DESCRIPTOR_HEAP:
    case M12CORE_COMMAND_PACKET_KIND_SET_ROOT_BINDING:
      out_summary->binding_packet_count++;
      break;
    case M12CORE_COMMAND_PACKET_KIND_SET_RENDER_TARGETS:
      out_summary->graphics_packet_count++;
      out_summary->summary_flags |=
          M12CORE_COMMAND_PACKET_STREAM_SUMMARY_HAS_GRAPHICS;
      out_summary->binding_packet_count++;
      counted_graphics = true;
      break;
    case M12CORE_COMMAND_PACKET_KIND_RESOURCE_BARRIER:
      out_summary->barrier_packet_count++;
      out_summary->summary_flags |=
          M12CORE_COMMAND_PACKET_STREAM_SUMMARY_HAS_BARRIERS;
      break;
    case M12CORE_COMMAND_PACKET_KIND_CLEAR_RTV:
    case M12CORE_COMMAND_PACKET_KIND_CLEAR_DSV:
    case M12CORE_COMMAND_PACKET_KIND_CLEAR_UAV:
      out_summary->clear_packet_count++;
      out_summary->graphics_packet_count++;
      out_summary->summary_flags |=
          M12CORE_COMMAND_PACKET_STREAM_SUMMARY_HAS_GRAPHICS;
      counted_graphics = true;
      break;
    case M12CORE_COMMAND_PACKET_KIND_DRAW:
    case M12CORE_COMMAND_PACKET_KIND_DRAW_INDEXED:
      out_summary->draw_packet_count++;
      out_summary->graphics_packet_count++;
      out_summary->summary_flags |=
          M12CORE_COMMAND_PACKET_STREAM_SUMMARY_HAS_GRAPHICS;
      counted_graphics = true;
      break;
    case M12CORE_COMMAND_PACKET_KIND_DISPATCH:
      out_summary->dispatch_packet_count++;
      out_summary->compute_packet_count++;
      out_summary->summary_flags |=
          M12CORE_COMMAND_PACKET_STREAM_SUMMARY_HAS_COMPUTE;
      counted_compute = true;
      break;
    case M12CORE_COMMAND_PACKET_KIND_COPY:
      out_summary->copy_packet_count++;
      out_summary->summary_flags |=
          M12CORE_COMMAND_PACKET_STREAM_SUMMARY_HAS_COPY;
      counted_copy = true;
      break;
    case M12CORE_COMMAND_PACKET_KIND_PRESENT:
      out_summary->present_packet_count++;
      out_summary->summary_flags |=
          M12CORE_COMMAND_PACKET_STREAM_SUMMARY_HAS_PRESENT;
      counted_present = true;
      break;
    default:
      break;
    }

    if ((packet.header.flags & M12CORE_COMMAND_PACKET_FLAG_GRAPHICS) &&
        !counted_graphics) {
      out_summary->graphics_packet_count++;
      out_summary->summary_flags |=
          M12CORE_COMMAND_PACKET_STREAM_SUMMARY_HAS_GRAPHICS;
    }
    if ((packet.header.flags & M12CORE_COMMAND_PACKET_FLAG_COMPUTE) &&
        !counted_compute) {
      out_summary->compute_packet_count++;
      out_summary->summary_flags |=
          M12CORE_COMMAND_PACKET_STREAM_SUMMARY_HAS_COMPUTE;
    }
    if ((packet.header.flags & M12CORE_COMMAND_PACKET_FLAG_COPY) &&
        !counted_copy) {
      out_summary->copy_packet_count++;
      out_summary->summary_flags |=
          M12CORE_COMMAND_PACKET_STREAM_SUMMARY_HAS_COPY;
    }
    if ((packet.header.flags & M12CORE_COMMAND_PACKET_FLAG_PRESENT) &&
        !counted_present) {
      out_summary->present_packet_count++;
      out_summary->summary_flags |=
          M12CORE_COMMAND_PACKET_STREAM_SUMMARY_HAS_PRESENT;
    }
  }

  if (out_summary->invalid_packet_count)
    out_summary->status = M12CORE_COMMAND_PACKET_STREAM_STATUS_INVALID;
  else if (out_summary->unsupported_packet_count)
    out_summary->status = M12CORE_COMMAND_PACKET_STREAM_STATUS_UNSUPPORTED;
  out_summary->stream_key = key;
  return 0;
}

extern "C" int
m12core_make_cache_compatibility_key(const M12CoreCacheCompatibilityDesc *desc,
                                     M12CoreCacheCompatibilityKey *out_key) {
  if (!desc || !out_key || desc->abi_version != M12CORE_ABI_VERSION)
    return 1;
  std::memset(out_key, 0, sizeof(*out_key));

  constexpr uint32_t required =
      M12CORE_CACHE_COMPAT_HAS_APPID | M12CORE_CACHE_COMPAT_HAS_DEVICE |
      M12CORE_CACHE_COMPAT_HAS_RUNTIME | M12CORE_CACHE_COMPAT_HAS_TRANSLATOR |
      M12CORE_CACHE_COMPAT_HAS_SCHEMA;
  uint32_t missing = required & ~desc->flags;
  if (!desc->schema_version)
    missing |= M12CORE_CACHE_COMPAT_HAS_SCHEMA;
  if (!desc->appid)
    missing |= M12CORE_CACHE_COMPAT_HAS_APPID;
  if (!desc->device_key)
    missing |= M12CORE_CACHE_COMPAT_HAS_DEVICE;
  if (!desc->core_build_key)
    missing |= M12CORE_CACHE_COMPAT_HAS_RUNTIME;
  if (!desc->translator_key)
    missing |= M12CORE_CACHE_COMPAT_HAS_TRANSLATOR;

  uint64_t key = 0x4d31324341434845ull; // "M12CACHE" marker.
  pipelineHashCombine(key, desc->artifact_kind);
  pipelineHashCombine(key, desc->schema_version);
  pipelineHashCombine(key, desc->appid);
  pipelineHashCombine(key, desc->profile_key);
  pipelineHashCombine(key, desc->device_key);
  pipelineHashCombine(key, desc->os_metal_key);
  pipelineHashCombine(key, desc->core_build_key);
  pipelineHashCombine(key, desc->core_feature_flags);
  pipelineHashCombine(key, desc->translator_key);
  pipelineHashCombine(key, desc->root_binding_key);
  pipelineHashCombine(key, desc->pipeline_key);
  pipelineHashCombine(key, desc->artifact_key);

  uint64_t invalidation = 0x4d3132494e56414cull; // "M12INVAL" marker.
  pipelineHashCombine(invalidation, desc->schema_version);
  pipelineHashCombine(invalidation, desc->device_key);
  pipelineHashCombine(invalidation, desc->os_metal_key);
  pipelineHashCombine(invalidation, desc->core_build_key);
  pipelineHashCombine(invalidation, desc->core_feature_flags);
  pipelineHashCombine(invalidation, desc->translator_key);
  pipelineHashCombine(invalidation, desc->root_binding_key);

  out_key->abi_version = M12CORE_ABI_VERSION;
  out_key->status = missing ? M12CORE_CACHE_COMPAT_STATUS_MISSING_DIMENSION
                            : M12CORE_CACHE_COMPAT_STATUS_OK;
  if (desc->artifact_kind == M12CORE_CACHE_ARTIFACT_UNKNOWN ||
      desc->artifact_kind > M12CORE_CACHE_ARTIFACT_PREWARM_PACK)
    out_key->status = M12CORE_CACHE_COMPAT_STATUS_INVALID;
  out_key->missing_flags = missing;
  out_key->artifact_kind = desc->artifact_kind;
  out_key->compatibility_key = key;
  out_key->invalidation_key = invalidation;
  return 0;
}

extern "C" int
m12core_register_handle(const M12CoreHandleRegistryDesc *desc,
                        M12CoreHandleRegistryResult *out_result) {
  if (!desc || !out_result || desc->abi_version != M12CORE_ABI_VERSION)
    return 1;
  std::memset(out_result, 0, sizeof(*out_result));

  out_result->abi_version = M12CORE_ABI_VERSION;
  out_result->kind = desc->kind;
  out_result->generation = desc->generation;
  if (!validHandleKind(desc->kind)) {
    out_result->status = M12CORE_HANDLE_REGISTRY_STATUS_UNSUPPORTED_KIND;
    return 0;
  }
  if (!desc->source_key || !desc->generation) {
    out_result->status = M12CORE_HANDLE_REGISTRY_STATUS_INVALID;
    return 0;
  }

  const uint64_t handle_key = makeHandleKey(*desc);
  out_result->status = M12CORE_HANDLE_REGISTRY_STATUS_OK;
  out_result->handle_key = handle_key;
  out_result->registry_id =
      makeRegistryId(desc->kind, desc->generation, handle_key);
  return 0;
}

extern "C" int
m12core_validate_handle(const M12CoreHandleValidationDesc *desc,
                        M12CoreHandleValidationResult *out_result) {
  if (!desc || !out_result || desc->abi_version != M12CORE_ABI_VERSION)
    return 1;
  std::memset(out_result, 0, sizeof(*out_result));

  out_result->abi_version = M12CORE_ABI_VERSION;
  out_result->registry_id = desc->registry_id;
  out_result->actual_kind = registryIdKind(desc->registry_id);
  out_result->actual_generation = registryIdGeneration(desc->registry_id);
  if (!registryIdHasMarker(desc->registry_id) || !out_result->actual_kind ||
      !out_result->actual_generation) {
    out_result->status = M12CORE_HANDLE_REGISTRY_STATUS_INVALID;
    return 0;
  }
  if (desc->expected_kind && desc->expected_kind != out_result->actual_kind) {
    out_result->status = M12CORE_HANDLE_REGISTRY_STATUS_UNSUPPORTED_KIND;
    return 0;
  }
  if (desc->expected_generation &&
      desc->expected_generation != out_result->actual_generation) {
    out_result->status = M12CORE_HANDLE_REGISTRY_STATUS_STALE;
    return 0;
  }

  out_result->status = M12CORE_HANDLE_REGISTRY_STATUS_OK;
  return 0;
}

extern "C" int
m12core_classify_packet_support(const M12CorePacketSupportDesc *desc,
                                M12CorePacketSupportSummary *out_summary) {
  if (!desc || !out_summary || desc->abi_version != M12CORE_ABI_VERSION)
    return 1;
  std::memset(out_summary, 0, sizeof(*out_summary));

  out_summary->abi_version = M12CORE_ABI_VERSION;
  out_summary->status = M12CORE_PACKET_SUPPORT_STATUS_SAFE;

  if (desc->packet_count && !desc->packets) {
    out_summary->status = M12CORE_PACKET_SUPPORT_STATUS_INVALID;
    out_summary->unsupported_reason_flags |=
        M12CORE_PACKET_UNSUPPORTED_INVALID_PACKET;
    out_summary->invalid_packet_count = desc->packet_count;
    return 0;
  }

  uint64_t shape_key = 0x4d31325348415045ull; // "M12SHAPE" marker.
  pipelineHashCombine(shape_key, desc->packet_count);
  pipelineHashCombine(shape_key, desc->queue_type);
  pipelineHashCombine(shape_key, desc->stream_key);
  pipelineHashCombine(shape_key, desc->packet_sequence_xor);

  for (uint32_t i = 0; i < desc->packet_count; i++) {
    const M12CoreCommandPacket &packet = desc->packets[i];
    const uint32_t kind = packet.header.kind;
    const uint32_t flags = packet.header.flags;
    pipelineHashCombine(shape_key, kind);
    pipelineHashCombine(shape_key, flags);

    bool invalid = packet.header.abi_version != M12CORE_ABI_VERSION ||
                   kind == M12CORE_COMMAND_PACKET_KIND_UNKNOWN ||
                   kind > M12CORE_COMMAND_PACKET_KIND_PRESENT;
    bool counted_unsupported_packet = false;
    auto markUnsupportedPacket = [&]() {
      if (!counted_unsupported_packet) {
        out_summary->unsupported_shape_count++;
        counted_unsupported_packet = true;
      }
    };

    if (invalid) {
      out_summary->invalid_packet_count++;
      markUnsupportedPacket();
      out_summary->unsupported_reason_flags |=
          M12CORE_PACKET_UNSUPPORTED_INVALID_PACKET;
      continue;
    }

    const bool explicit_unsupported =
        (flags & M12CORE_COMMAND_PACKET_FLAG_UNSUPPORTED) != 0;
    if (explicit_unsupported) {
      markUnsupportedPacket();
      if (kind == M12CORE_COMMAND_PACKET_KIND_DRAW) {
        out_summary->unsupported_reason_flags |=
            M12CORE_PACKET_UNSUPPORTED_INDIRECT;
      } else {
        out_summary->unsupported_reason_flags |=
            M12CORE_PACKET_UNSUPPORTED_UNKNOWN_KIND;
      }
    } else if (kind == M12CORE_COMMAND_PACKET_KIND_COPY) {
      markUnsupportedPacket();
      out_summary->unsupported_reason_flags |= M12CORE_PACKET_UNSUPPORTED_COPY;
    }

    const bool has_native_id = registryIdHasMarker(packet.object_id0) ||
                               registryIdHasMarker(packet.object_id1) ||
                               registryIdHasMarker(packet.object_id2) ||
                               registryIdHasMarker(packet.object_id3);
    if (packetKindRequiresNativeId(kind, flags) && !has_native_id) {
      out_summary->missing_native_id_count++;
      markUnsupportedPacket();
      out_summary->unsupported_reason_flags |=
          M12CORE_PACKET_UNSUPPORTED_MISSING_NATIVE_ID;
    }
    const uint64_t ids[4] = {packet.object_id0, packet.object_id1,
                             packet.object_id2, packet.object_id3};
    bool packet_has_stale_handle = false;
    for (uint32_t id_index = 0; id_index < 4; id_index++) {
      if (ids[id_index] && !registryIdHasMarker(ids[id_index])) {
        out_summary->stale_handle_count++;
        packet_has_stale_handle = true;
        out_summary->unsupported_reason_flags |=
            M12CORE_PACKET_UNSUPPORTED_STALE_HANDLE;
      }
    }
    if (packet_has_stale_handle)
      markUnsupportedPacket();
  }

  out_summary->shape_key = shape_key;
  if (out_summary->invalid_packet_count)
    out_summary->status = M12CORE_PACKET_SUPPORT_STATUS_INVALID;
  else if (out_summary->unsupported_reason_flags)
    out_summary->status = M12CORE_PACKET_SUPPORT_STATUS_UNSUPPORTED;
  out_summary->safe_for_probe_replay =
      out_summary->status == M12CORE_PACKET_SUPPORT_STATUS_SAFE ? 1u : 0u;
  if (out_summary->status == M12CORE_PACKET_SUPPORT_STATUS_UNSUPPORTED) {
    uint64_t negative = 0x4d31324e45474348ull; // "M12NEGCH" marker.
    pipelineHashCombine(negative, out_summary->unsupported_reason_flags);
    pipelineHashCombine(negative, out_summary->unsupported_shape_count);
    pipelineHashCombine(negative, shape_key);
    out_summary->negative_cache_key = negative;
  }
  return 0;
}

extern "C" int
m12core_plan_render_pass(const M12CoreRenderPassPlanDesc *desc,
                         M12CoreRenderPassPlanSummary *out_summary) {
  if (!desc || !out_summary || desc->abi_version != M12CORE_ABI_VERSION)
    return 1;
  std::memset(out_summary, 0, sizeof(*out_summary));

  /* Slice 3 render-pass/hazard planning seam.  The native core owns scalar
   * render-pass classification and hazard scoring here.  PE/DXMT still owns
   * Metal encoder creation, load/store actions, command emission, barriers,
   * command-buffer commits, and synchronization.
   */
  uint32_t expected_flags = 0;
  if (desc->render_target_count)
    expected_flags |= M12CORE_RENDER_PASS_PLAN_HAS_RENDER_TARGETS;
  if (desc->dsv_format)
    expected_flags |= M12CORE_RENDER_PASS_PLAN_HAS_DSV;
  if (desc->flags & M12CORE_RENDER_PASS_PLAN_HAS_SWAPCHAIN_TARGET)
    expected_flags |= M12CORE_RENDER_PASS_PLAN_HAS_SWAPCHAIN_TARGET;
  if (desc->swapchain_touched_count)
    expected_flags |= M12CORE_RENDER_PASS_PLAN_HAS_SWAPCHAIN_TOUCH;
  if (desc->draw_count)
    expected_flags |= M12CORE_RENDER_PASS_PLAN_HAS_DRAW_WORK;
  if (desc->clear_count)
    expected_flags |= M12CORE_RENDER_PASS_PLAN_HAS_CLEAR_WORK;
  if (desc->dispatch_count)
    expected_flags |= M12CORE_RENDER_PASS_PLAN_HAS_COMPUTE_WORK;
  if (desc->resource_barrier_count)
    expected_flags |= M12CORE_RENDER_PASS_PLAN_HAS_BARRIERS;
  if (desc->descriptor_heap_count)
    expected_flags |= M12CORE_RENDER_PASS_PLAN_HAS_DESCRIPTOR_HEAPS;

  uint64_t key = 0x4d31325250415353ull; // "M12RPASS" marker.
  pipelineHashCombine(key, desc->flags);
  pipelineHashCombine(key, expected_flags);
  pipelineHashCombine(key, desc->queue_type);
  pipelineHashCombine(key, desc->command_list_index);
  pipelineHashCombine(key, desc->render_target_count);
  pipelineHashCombine(key, desc->dsv_format);
  pipelineHashCombine(key, desc->rtv0_format);
  pipelineHashCombine(key, desc->rtv_format_xor);
  pipelineHashCombine(key, desc->draw_count);
  pipelineHashCombine(key, desc->clear_count);
  pipelineHashCombine(key, desc->dispatch_count);
  pipelineHashCombine(key, desc->resource_barrier_count);
  pipelineHashCombine(key, desc->descriptor_heap_count);
  pipelineHashCombine(key, desc->swapchain_touched_count);
  pipelineHashCombine(key, desc->command_buffer_status);
  pipelineHashCombine(key, desc->command_list_id);
  pipelineHashCombine(key, desc->queue_serial);

  uint32_t classification = 0;
  if (desc->render_target_count && desc->draw_count)
    classification = 1;
  else if (desc->render_target_count && desc->clear_count)
    classification = 2;
  else if (desc->dispatch_count)
    classification = 3;
  else if (desc->resource_barrier_count)
    classification = 4;

  uint32_t hazard_score = 0;
  hazard_score += desc->resource_barrier_count;
  if (desc->flags & M12CORE_RENDER_PASS_PLAN_HAS_SWAPCHAIN_TARGET)
    hazard_score += 2;
  if (desc->swapchain_touched_count)
    hazard_score += 1;
  if (desc->render_target_count > 1)
    hazard_score += desc->render_target_count - 1;
  if (desc->dsv_format && desc->draw_count)
    hazard_score += 1;

  out_summary->abi_version = M12CORE_ABI_VERSION;
  out_summary->status = M12CORE_RENDER_PASS_PLAN_STATUS_OK;
  out_summary->input_flags = desc->flags;
  out_summary->expected_flags = expected_flags;
  out_summary->drift_flags = desc->flags ^ expected_flags;
  out_summary->render_pass_classification = classification;
  out_summary->hazard_score = hazard_score;
  out_summary->attachment_count =
      desc->render_target_count + (desc->dsv_format ? 1u : 0u);
  out_summary->resource_transition_count = desc->resource_barrier_count;
  out_summary->descriptor_pressure_score = desc->descriptor_heap_count;
  out_summary->render_pass_plan_key = key;
  return 0;
}
