#pragma once

#include <cstdint>
#include <limits>

namespace dxmt {

enum class D3D12VertexTableIndexingMode : uint32_t {
  CompactBySlotMask = 0,
  RawSlot = 1,
};

inline const char *
D3D12VertexTableIndexingModeName(D3D12VertexTableIndexingMode mode) {
  switch (mode) {
  case D3D12VertexTableIndexingMode::CompactBySlotMask:
    return "compact_by_slot_mask";
  case D3D12VertexTableIndexingMode::RawSlot:
    return "raw_slot";
  }
  return "unknown";
}

inline uint32_t D3D12CompactVertexTableIndex(uint32_t slot_mask,
                                             uint32_t slot) {
  if (slot >= 32 || !(slot_mask & (1u << slot)))
    return std::numeric_limits<uint32_t>::max();

  uint32_t lower_slots = slot == 0 ? 0 : slot_mask & ((1u << slot) - 1u);
  return __builtin_popcount(lower_slots);
}

inline uint32_t D3D12ResolveAlignedInputOffset(uint32_t previous_offset,
                                               uint32_t element_size) {
  uint32_t alignment = element_size < 4 ? element_size : 4;
  if (alignment <= 1)
    return previous_offset;
  return (previous_offset + alignment - 1) & ~(alignment - 1);
}

} // namespace dxmt
