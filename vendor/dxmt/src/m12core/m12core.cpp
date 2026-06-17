#include "m12core.h"

#include <atomic>
#include <cstddef>

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
constexpr uint32_t kBuildIdHigh = 0x00000002u; // Phase-2 counters build.

std::atomic<uint64_t> g_counters[M12CORE_COUNTER_COUNT] = {};

bool validCounter(uint32_t counter_id) {
  return counter_id < M12CORE_COUNTER_COUNT;
}

} // namespace

extern "C" int m12core_get_version(M12CoreVersion *out_version) {
  if (!out_version)
    return 1;

  out_version->abi_version = M12CORE_ABI_VERSION;
  out_version->feature_flags = M12CORE_FEATURE_INERT_LOADER | M12CORE_FEATURE_COUNTERS;
  out_version->build_id_low = kBuildIdLow;
  out_version->build_id_high = kBuildIdHigh;
  return 0;
}

extern "C" const char *m12core_build_string(void) {
  return "libm12core phase2 counters abi=1";
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
