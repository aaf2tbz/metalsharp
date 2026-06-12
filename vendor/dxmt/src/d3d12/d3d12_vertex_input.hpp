#pragma once

#include <cstdint>
#include <limits>

namespace dxmt {

inline uint32_t D3D12CompactVertexTableIndex(uint32_t slot_mask,
                                             uint32_t slot) {
  if (slot >= 32 || !(slot_mask & (1u << slot)))
    return std::numeric_limits<uint32_t>::max();

  uint32_t lower_slots = slot == 0 ? 0 : slot_mask & ((1u << slot) - 1u);
  return __builtin_popcount(lower_slots);
}

} // namespace dxmt
