#include "m12core.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

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
constexpr uint32_t kBuildIdHigh = 0x00000003u; // Phase-3 shader introspection foundation.

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
                               M12CORE_FEATURE_SHADER_INTROSPECTION;
  out_version->build_id_low = kBuildIdLow;
  out_version->build_id_high = kBuildIdHigh;
  return 0;
}

extern "C" const char *m12core_build_string(void) {
  return "libm12core phase3 shader-introspection abi=1";
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
