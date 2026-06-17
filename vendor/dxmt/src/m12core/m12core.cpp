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

constexpr uint32_t kBuildIdLow = 0x4d313243u;  // "M12C" marker.
constexpr uint32_t kBuildIdHigh = 0x0000000au; // Phase-5 root binding plan foundation.

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
   * still exposes stable M12CORE_SHADER_STAGE_* values; this mapping is only for
   * preserving the current hash namespace during the migration.
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
  const char *root = (cache_root && cache_root[0]) ? cache_root : "/tmp/dxmt_shader_cache";
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
  out_version->feature_flags = M12CORE_FEATURE_INERT_LOADER | M12CORE_FEATURE_COUNTERS |
                               M12CORE_FEATURE_SHADER_INTROSPECTION |
                               M12CORE_FEATURE_SHADER_FUNCTIONS |
                               M12CORE_FEATURE_DXIL_TO_MSL |
                               M12CORE_FEATURE_SM50_REFLECTION |
                               M12CORE_FEATURE_PIPELINE_CACHE |
                               M12CORE_FEATURE_PIPELINE_CREATION |
                               M12CORE_FEATURE_ROOT_SIGNATURE_KEYS |
                               M12CORE_FEATURE_ROOT_BINDING_PLAN;
  out_version->build_id_low = kBuildIdLow;
  out_version->build_id_high = kBuildIdHigh;
  return 0;
}

extern "C" const char *m12core_build_string(void) {
  return "libm12core phase5 root-binding-plan abi=1";
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

extern "C" int m12core_hash_shader_bytecode(const void *bytecode,
                                             uint64_t bytecode_size,
                                             uint32_t stage,
                                             M12CoreShaderBytecodeInfo *out_info) {
  if (!out_info)
    return 1;

  out_info->abi_version = M12CORE_ABI_VERSION;
  out_info->stage = stage;
  out_info->bytecode_hash = hashShaderBytecode(bytecode, bytecode_size, stage);
  out_info->bytecode_size = bytecode_size;
  out_info->contains_dxil = shaderContainsDxil(bytecode, bytecode_size) ? 1u : 0u;
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
  return path && path[0] && stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

void pipelineHashCombine(uint64_t &hash, uint64_t value) {
  hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
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
    dst.instance_step_rate = src.instance_step_rate ? src.instance_step_rate : 1;
    dst.table_indexing_mode = src.table_indexing_mode == 1
      ? dxmt::dxil::MSLVertexTableIndexingMode::RawSlot
      : dxmt::dxil::MSLVertexTableIndexingMode::CompactBySlotMask;
    dst.system_value = src.system_value != 0;
    options.vertex_inputs.push_back(dst);
  }
}

extern "C" int m12core_format_shader_cache_paths(const char *cache_root,
                                                 uint64_t shader_hash,
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
  std::snprintf(suffix, sizeof(suffix), "%016llx", (unsigned long long)shader_hash);
  formatCachePath(out_paths->cache_path, sizeof(out_paths->cache_path), root, suffix);

  std::snprintf(suffix, sizeof(suffix), "%016llx.dxbc", (unsigned long long)shader_hash);
  formatCachePath(out_paths->dxbc_path, sizeof(out_paths->dxbc_path), root, suffix);
  std::snprintf(suffix, sizeof(suffix), "%016llx.metallib", (unsigned long long)shader_hash);
  formatCachePath(out_paths->metallib_path, sizeof(out_paths->metallib_path), root, suffix);
  std::snprintf(suffix, sizeof(suffix), "%016llx.json", (unsigned long long)shader_hash);
  formatCachePath(out_paths->reflection_path, sizeof(out_paths->reflection_path), root, suffix);
  std::snprintf(suffix, sizeof(suffix), "%016llx.module.txt", (unsigned long long)shader_hash);
  formatCachePath(out_paths->module_summary_path, sizeof(out_paths->module_summary_path), root, suffix);
  std::snprintf(suffix, sizeof(suffix), "%016llx.dxil_report.txt", (unsigned long long)shader_hash);
  formatCachePath(out_paths->dxil_report_path, sizeof(out_paths->dxil_report_path), root, suffix);
  std::snprintf(suffix, sizeof(suffix), "%016llx.metallib.err.txt", (unsigned long long)shader_hash);
  formatCachePath(out_paths->metallib_error_path, sizeof(out_paths->metallib_error_path), root, suffix);
  return 0;
}

void parseReflectionText(const char *text, M12CoreShaderReflectionSummary *out_summary) {
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

extern "C" int m12core_probe_shader_cache(const char *cache_root,
                                          uint64_t shader_hash,
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
  if (m12core_format_shader_cache_paths(cache_root, shader_hash, &out_lookup->paths) != 0)
    return 1;
  out_lookup->metallib_exists = regularFileExists(out_lookup->paths.metallib_path) ? 1u : 0u;
  out_lookup->metallib_available =
      (!force_source_compile && out_lookup->metallib_exists) ? 1u : 0u;
  return 0;
}

extern "C" int m12core_parse_shader_reflection(const char *reflection_text,
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

  if (!desc || desc->abi_version != M12CORE_ABI_VERSION || !desc->dxil_container ||
      desc->dxil_container_size == 0) {
    out_result->status = M12CORE_DXIL_TO_MSL_STATUS_INVALID;
    return 0;
  }

  /* Phase 3 DXIL->MSL ownership seam.  libm12core now parses the DXIL
   * container, LLVM bitcode, and vertex-input lowering metadata to generate MSL
   * source.  The PE side still writes diagnostics/cache files and asks the
   * shader-function ABI to create Metal objects so each ownership transfer stays
   * independently reviewable.
   */
  auto container = dxmt::dxil::DXILContainer::parse(desc->dxil_container,
                                                   (size_t)desc->dxil_container_size);
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
  auto typed_msl = dxmt::dxil::MSLLowering::lower(*module, shader_info, lowering_options);
  auto msl_result = typed_msl
      ? std::optional<dxmt::dxil::MSLShader>(std::in_place, toRuntimeMSLShader(std::move(*typed_msl)))
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

extern "C" int m12core_reflect_sm50_shader(const void *bytecode,
                                           uint64_t bytecode_size,
                                           uint32_t options,
                                           M12CoreSM50ShaderReflection *out_reflection,
                                           M12CoreSM50ShaderArgument *out_constant_buffers,
                                           uint32_t constant_buffer_capacity,
                                           M12CoreSM50ShaderArgument *out_arguments,
                                           uint32_t argument_capacity,
                                           M12CoreSM50ReflectionResult *out_result) {
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
       (!out_constant_buffers || constant_buffer_capacity < reflection.NumConstantBuffers)) ||
      (reflection.NumArguments &&
       (!out_arguments || argument_capacity < reflection.NumArguments))) {
    SM50Destroy(shader);
    out_result->status = M12CORE_SM50_REFLECTION_STATUS_OUTPUT_TOO_SMALL;
    return 0;
  }

  std::vector<MTL_SM50_SHADER_ARGUMENT> cbs(reflection.NumConstantBuffers);
  std::vector<MTL_SM50_SHADER_ARGUMENT> args(reflection.NumArguments);
  if (reflection.NumConstantBuffers || reflection.NumArguments)
    SM50GetArgumentsInfo(shader,
                         cbs.empty() ? nullptr : cbs.data(),
                         args.empty() ? nullptr : args.data());
  for (uint32_t i = 0; i < reflection.NumConstantBuffers; i++)
    copySm50Argument(cbs[i], out_constant_buffers[i]);
  for (uint32_t i = 0; i < reflection.NumArguments; i++)
    copySm50Argument(args[i], out_arguments[i]);

  SM50Destroy(shader);
  out_result->status = M12CORE_SM50_REFLECTION_STATUS_OK;
  return 0;
}

extern "C" int m12core_make_pipeline_cache_key(const M12CorePipelineCacheKeyInput *input,
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
    const M12CorePipelineKeyFields *input,
    M12CorePipelineCacheKey *out_key) {
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

extern "C" int m12core_summarize_root_signature(
    const M12CoreRootSignatureDesc *desc,
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

extern "C" int m12core_build_root_binding_plan(
    const M12CoreRootBindingPlanDesc *desc,
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
  std::vector<uint32_t> register_spaces;

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
    if (std::find(register_spaces.begin(), register_spaces.end(),
                  param.register_space) == register_spaces.end())
      register_spaces.push_back(param.register_space);

    if (param.type == 0)
      descriptor_tables++;
    else if (param.type == 1)
      root_constants++;
    else if (param.type >= 2 && param.type <= 4)
      root_descriptors++;
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
    if (range.range_type == 3)
      sampler_ranges++;
    else
      resource_ranges++;
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
  out_summary->register_space_count = static_cast<uint32_t>(register_spaces.size());
  out_summary->max_descriptor_table_span = max_table_span;
  out_summary->binding_plan_key = key;
  return 0;
}

extern "C" int m12core_lookup_root_binding(
    const M12CoreRootBindingLookupDesc *desc,
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
        if (visibility_pass == 0 && param.shader_visibility != desc->shader_visibility)
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
        if (visibility_pass == 0 && sampler.shader_visibility != desc->shader_visibility)
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

  out_result->status = M12CORE_ROOT_SIGNATURE_STATUS_INVALID;
  return 1;
}
