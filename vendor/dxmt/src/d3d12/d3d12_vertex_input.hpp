#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

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

enum class D3D12VertexInputSlotClass : uint32_t {
  PerVertex = 0,
  PerInstance = 1,
};

inline const char *
D3D12VertexInputSlotClassName(D3D12VertexInputSlotClass slot_class) {
  switch (slot_class) {
  case D3D12VertexInputSlotClass::PerVertex:
    return "per_vertex";
  case D3D12VertexInputSlotClass::PerInstance:
    return "per_instance";
  }
  return "unknown";
}

inline uint32_t D3D12CountSetBits(uint32_t value) {
  uint32_t count = 0;
  while (value) {
    count += value & 1u;
    value >>= 1u;
  }
  return count;
}

inline uint32_t D3D12CompactVertexTableIndex(uint32_t slot_mask,
                                             uint32_t slot) {
  if (slot >= 32 || !(slot_mask & (1u << slot)))
    return std::numeric_limits<uint32_t>::max();

  uint32_t lower_slots = slot == 0 ? 0 : slot_mask & ((1u << slot) - 1u);
  return D3D12CountSetBits(lower_slots);
}

inline uint32_t D3D12ResolveAlignedInputOffset(uint32_t previous_offset,
                                               uint32_t element_size) {
  uint32_t alignment = element_size < 4 ? element_size : 4;
  if (alignment <= 1)
    return previous_offset;
  return (previous_offset + alignment - 1) & ~(alignment - 1);
}

inline bool D3D12SemanticNameEquals(const std::string &lhs,
                                    const std::string &rhs) {
  if (lhs.size() != rhs.size())
    return false;

  for (size_t i = 0; i < lhs.size(); i++) {
    auto l = static_cast<unsigned char>(lhs[i]);
    auto r = static_cast<unsigned char>(rhs[i]);
    if (std::tolower(l) != std::tolower(r))
      return false;
  }

  return true;
}

struct D3D12IAInputLayoutElementMetadata {
  std::string semantic_name;
  uint32_t semantic_index = 0;
  uint32_t input_slot = 0;
  uint32_t aligned_byte_offset = 0;
  uint32_t dxgi_format = 0;
  uint32_t metal_format = 0;
  uint32_t bytes_per_texel = 0;
  D3D12VertexInputSlotClass input_slot_class =
      D3D12VertexInputSlotClass::PerVertex;
  uint32_t instance_step_rate = 1;
  bool supported_format = true;
};

struct D3D12IAInputSignatureElementMetadata {
  std::string semantic_name;
  uint32_t semantic_index = 0;
  uint32_t shader_register = 0;
  bool system_value = false;
};

struct D3D12ResolvedIAInputElementMetadata {
  std::string semantic_name;
  uint32_t semantic_index = 0;
  uint32_t shader_register = 0;
  uint32_t input_slot = std::numeric_limits<uint32_t>::max();
  uint32_t table_index = std::numeric_limits<uint32_t>::max();
  D3D12VertexTableIndexingMode table_indexing_mode =
      D3D12VertexTableIndexingMode::CompactBySlotMask;
  uint32_t aligned_byte_offset = 0;
  uint32_t dxgi_format = 0;
  uint32_t metal_format = 0;
  D3D12VertexInputSlotClass input_slot_class =
      D3D12VertexInputSlotClass::PerVertex;
  uint32_t instance_step_rate = 1;
  bool system_value = false;
};

struct D3D12IAInputLayoutMetadata {
  uint32_t slot_mask = 0;
  std::vector<D3D12ResolvedIAInputElementMetadata> elements;
};

struct D3D12VertexBufferViewMetadata {
  uint32_t input_slot = 0;
  uint64_t buffer_location = 0;
  uint32_t size_in_bytes = 0;
  uint32_t stride_in_bytes = 0;
};

struct D3D12VertexTableRowMetadata {
  uint32_t table_index = std::numeric_limits<uint32_t>::max();
  uint32_t input_slot = std::numeric_limits<uint32_t>::max();
  uint64_t buffer_location = 0;
  uint32_t size_in_bytes = 0;
  uint32_t stride_in_bytes = 0;
};

inline D3D12IAInputLayoutMetadata D3D12BuildIAInputLayoutMetadata(
    const std::vector<D3D12IAInputLayoutElementMetadata> &layout,
    const std::vector<D3D12IAInputSignatureElementMetadata> &signature,
    uint32_t vertex_slot_cap, uint32_t append_aligned_element) {
  D3D12IAInputLayoutMetadata result;
  if (!vertex_slot_cap)
    return result;

  vertex_slot_cap = std::min<uint32_t>(vertex_slot_cap, 32);
  std::vector<uint32_t> append_offsets(vertex_slot_cap);

  for (const auto &desc : layout) {
    if (desc.input_slot >= vertex_slot_cap || desc.semantic_name.empty() ||
        !desc.supported_format || !desc.metal_format || !desc.bytes_per_texel)
      continue;

    auto sig = std::find_if(
        signature.begin(), signature.end(),
        [&](const D3D12IAInputSignatureElementMetadata &input_sig) {
          return !input_sig.system_value &&
                 desc.semantic_index == input_sig.semantic_index &&
                 D3D12SemanticNameEquals(desc.semantic_name,
                                         input_sig.semantic_name);
        });
    if (sig == signature.end())
      continue;

    uint32_t aligned_offset =
        desc.aligned_byte_offset == append_aligned_element
            ? D3D12ResolveAlignedInputOffset(append_offsets[desc.input_slot],
                                             desc.bytes_per_texel)
            : desc.aligned_byte_offset;
    append_offsets[desc.input_slot] = aligned_offset + desc.bytes_per_texel;

    D3D12ResolvedIAInputElementMetadata info;
    info.semantic_name = desc.semantic_name;
    info.semantic_index = desc.semantic_index;
    info.shader_register = sig->shader_register;
    info.input_slot = desc.input_slot;
    info.table_indexing_mode = D3D12VertexTableIndexingMode::CompactBySlotMask;
    info.aligned_byte_offset = aligned_offset;
    info.dxgi_format = desc.dxgi_format;
    info.metal_format = desc.metal_format;
    info.input_slot_class = desc.input_slot_class;
    info.instance_step_rate =
        desc.input_slot_class == D3D12VertexInputSlotClass::PerInstance
            ? desc.instance_step_rate
            : 1;
    result.slot_mask |= 1u << desc.input_slot;
    result.elements.push_back(std::move(info));
  }

  for (const auto &input_sig : signature) {
    if (!input_sig.system_value)
      continue;

    D3D12ResolvedIAInputElementMetadata info;
    info.semantic_name = input_sig.semantic_name;
    info.semantic_index = input_sig.semantic_index;
    info.shader_register = input_sig.shader_register;
    info.system_value = true;
    result.elements.push_back(std::move(info));
  }

  for (auto &info : result.elements) {
    if (info.system_value)
      continue;
    info.table_index =
        D3D12CompactVertexTableIndex(result.slot_mask, info.input_slot);
  }

  return result;
}

inline std::vector<D3D12VertexTableRowMetadata> D3D12BuildVertexTableRows(
    const std::vector<D3D12ResolvedIAInputElementMetadata> &inputs,
    const std::vector<D3D12VertexBufferViewMetadata> &views,
    uint32_t table_cap) {
  std::vector<D3D12VertexTableRowMetadata> rows;
  if (!table_cap)
    return rows;

  table_cap = std::min<uint32_t>(table_cap, 32);
  std::vector<bool> emitted(table_cap);

  for (const auto &input : inputs) {
    if (input.system_value ||
        input.table_indexing_mode !=
            D3D12VertexTableIndexingMode::CompactBySlotMask ||
        input.input_slot >= 32 || input.table_index >= table_cap ||
        emitted[input.table_index])
      continue;

    auto view = std::find_if(
        views.begin(), views.end(),
        [&](const D3D12VertexBufferViewMetadata &candidate) {
          return candidate.input_slot == input.input_slot;
        });
    if (view == views.end())
      continue;

    D3D12VertexTableRowMetadata row;
    row.table_index = input.table_index;
    row.input_slot = input.input_slot;
    row.buffer_location = view->buffer_location;
    row.size_in_bytes = view->size_in_bytes;
    row.stride_in_bytes = view->stride_in_bytes;
    rows.push_back(row);
    emitted[input.table_index] = true;
  }

  return rows;
}

} // namespace dxmt
