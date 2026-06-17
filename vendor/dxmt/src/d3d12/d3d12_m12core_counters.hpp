#pragma once

#include "../m12core/m12core.h"
#include "../winemetal/winemetal.h"

#include <atomic>
#include <cstdint>

namespace dxmt::m12core {

/*
 * PE-side batching shim for libm12core counters.
 *
 * D3D12 hot paths cannot call libm12core.dylib directly: they run in the PE
 * half of the Wine split and must cross through winemetal's unixcall ABI.  A
 * unixcall per PSO/shader event would be too expensive for AC6, so each PE
 * translation unit keeps relaxed atomic deltas and only flushes a compact fixed
 * snapshot periodically.  This keeps the existing PSO_PRESSURE log lines as the
 * semantic oracle while letting the native core become the long-term owner of
 * aggregate diagnostics.
 *
 * Future refactors should replace local atomics/log parsing with explicit
 * libm12core snapshots once the D3D12 device/pipeline ownership boundary moves
 * native-side.  Until then, this header is the narrow bridge to audit.
 */

constexpr uint64_t kFlushIntervalEvents = 64;

inline std::atomic<uint64_t> *PendingCounters() {
  static std::atomic<uint64_t> counters[M12CORE_COUNTER_COUNT] = {};
  return counters;
}

inline std::atomic<uint64_t> &FlushTick() {
  static std::atomic<uint64_t> tick{0};
  return tick;
}

inline void FlushCounters() {
  uint64_t deltas[M12CORE_COUNTER_COUNT] = {};
  bool any = false;
  auto *pending = PendingCounters();
  for (uint32_t i = 0; i < M12CORE_COUNTER_COUNT; i++) {
    deltas[i] = pending[i].exchange(0, std::memory_order_relaxed);
    any = any || deltas[i] != 0;
  }
  if (!any)
    return;

  if (!WMTM12CoreRecordCounters(deltas, M12CORE_COUNTER_COUNT)) {
    /* Preserve deltas if the native core is disabled or the bridge is absent.
     * This is best-effort diagnostics: rendering must continue and the existing
     * PSO_PRESSURE logs remain authoritative when libm12core is not enabled.
     */
    for (uint32_t i = 0; i < M12CORE_COUNTER_COUNT; i++) {
      if (deltas[i])
        pending[i].fetch_add(deltas[i], std::memory_order_relaxed);
    }
  }
}

inline void RecordCounter(uint32_t counter_id, uint64_t delta = 1) {
  if (counter_id >= M12CORE_COUNTER_COUNT || delta == 0)
    return;

  PendingCounters()[counter_id].fetch_add(delta, std::memory_order_relaxed);
  uint64_t tick = FlushTick().fetch_add(1, std::memory_order_relaxed) + 1;
  if ((tick % kFlushIntervalEvents) == 0)
    FlushCounters();
}

} // namespace dxmt::m12core
