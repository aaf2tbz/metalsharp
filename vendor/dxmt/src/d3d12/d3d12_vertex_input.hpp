#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
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

enum class D3D12DrawSafetySkipReason : uint32_t {
  None = 0,
  MissingPipelineState,
  PipelineStateCompileFailed,
  PipelineStateNotRenderable,
  UnsupportedVertexTableMode,
  MissingVertexBuffer,
  UnresolvedVertexBuffer,
  ZeroStrideVertexBuffer,
  MissingIndexBuffer,
  UnresolvedIndexBuffer,
  InvalidIndexFormat,
  IndexRangeOutOfBounds,
  VertexRangeOutOfBounds,
};

inline const char *
D3D12DrawSafetySkipReasonName(D3D12DrawSafetySkipReason reason) {
  switch (reason) {
  case D3D12DrawSafetySkipReason::None:
    return "none";
  case D3D12DrawSafetySkipReason::MissingPipelineState:
    return "missing_pso";
  case D3D12DrawSafetySkipReason::PipelineStateCompileFailed:
    return "pso_compile_failed";
  case D3D12DrawSafetySkipReason::PipelineStateNotRenderable:
    return "pso_not_renderable";
  case D3D12DrawSafetySkipReason::UnsupportedVertexTableMode:
    return "unsupported_vertex_table_mode";
  case D3D12DrawSafetySkipReason::MissingVertexBuffer:
    return "missing_vertex_buffer";
  case D3D12DrawSafetySkipReason::UnresolvedVertexBuffer:
    return "unresolved_vertex_buffer";
  case D3D12DrawSafetySkipReason::ZeroStrideVertexBuffer:
    return "zero_stride_vertex_buffer";
  case D3D12DrawSafetySkipReason::MissingIndexBuffer:
    return "missing_index_buffer";
  case D3D12DrawSafetySkipReason::UnresolvedIndexBuffer:
    return "unresolved_index_buffer";
  case D3D12DrawSafetySkipReason::InvalidIndexFormat:
    return "invalid_index_format";
  case D3D12DrawSafetySkipReason::IndexRangeOutOfBounds:
    return "index_range_oob";
  case D3D12DrawSafetySkipReason::VertexRangeOutOfBounds:
    return "vertex_range_oob";
  }
  return "unknown";
}

struct D3D12DrawSafetyVertexBuffer {
  uint32_t input_slot = 0;
  uint64_t buffer_location = 0;
  uint32_t size_in_bytes = 0;
  uint32_t stride_in_bytes = 0;
  bool view_supplied = false;
  bool gpu_address_resolved = false;
  bool allow_zero_stride = false;
};

struct D3D12DrawSafetyIndexRange {
  bool indexed = false;
  bool index_buffer_supplied = false;
  bool index_buffer_resolved = false;
  uint64_t index_buffer_location = 0;
  uint64_t index_buffer_size = 0;
  uint64_t index_buffer_offset = 0;
  uint32_t index_size = 0;
  bool has_min_max_index = false;
  uint32_t min_index = 0;
  uint32_t max_index = 0;
};

struct D3D12DrawSafetyDesc {
  bool pso_present = false;
  bool pso_compiled = false;
  bool pso_is_compute = false;
  bool render_pso_ready = false;
  bool expect_compact_vertex_table = true;
  uint32_t element_count = 0;
  uint32_t instance_count = 0;
  uint32_t start_element = 0;
  int32_t base_vertex = 0;
  uint32_t start_instance = 0;
  D3D12DrawSafetyIndexRange index_range = {};
  std::vector<D3D12ResolvedIAInputElementMetadata> inputs;
  std::vector<D3D12DrawSafetyVertexBuffer> vertex_buffers;
};

struct D3D12DrawSafetyResult {
  D3D12DrawSafetySkipReason reason = D3D12DrawSafetySkipReason::None;
  uint32_t input_slot = std::numeric_limits<uint32_t>::max();
  uint32_t table_index = std::numeric_limits<uint32_t>::max();
  uint64_t gpu_address = 0;
  uint32_t size_in_bytes = 0;
  uint32_t stride_in_bytes = 0;
  uint64_t required_vertices = 0;
  uint64_t available_vertices = 0;
};

inline bool D3D12DrawSafetySkipped(const D3D12DrawSafetyResult &result) {
  return result.reason != D3D12DrawSafetySkipReason::None;
}

inline const D3D12DrawSafetyVertexBuffer *D3D12FindSafetyVertexBuffer(
    const std::vector<D3D12DrawSafetyVertexBuffer> &views, uint32_t slot) {
  auto view = std::find_if(views.begin(), views.end(),
                           [&](const D3D12DrawSafetyVertexBuffer &candidate) {
                             return candidate.input_slot == slot;
                           });
  return view == views.end() ? nullptr : &*view;
}

inline D3D12DrawSafetyResult
D3D12ValidateDrawSafety(const D3D12DrawSafetyDesc &desc) {
  D3D12DrawSafetyResult result;

  if (!desc.element_count || !desc.instance_count)
    return result;

  if (!desc.pso_present) {
    result.reason = D3D12DrawSafetySkipReason::MissingPipelineState;
    return result;
  }
  if (!desc.pso_compiled) {
    result.reason = D3D12DrawSafetySkipReason::PipelineStateCompileFailed;
    return result;
  }
  if (desc.pso_is_compute || !desc.render_pso_ready) {
    result.reason = D3D12DrawSafetySkipReason::PipelineStateNotRenderable;
    return result;
  }

  const auto &index = desc.index_range;
  if (index.indexed) {
    if (!index.index_buffer_supplied) {
      result.reason = D3D12DrawSafetySkipReason::MissingIndexBuffer;
      result.gpu_address = index.index_buffer_location;
      return result;
    }
    if (!index.index_buffer_resolved) {
      result.reason = D3D12DrawSafetySkipReason::UnresolvedIndexBuffer;
      result.gpu_address = index.index_buffer_location;
      return result;
    }
    if (index.index_size != 2 && index.index_size != 4) {
      result.reason = D3D12DrawSafetySkipReason::InvalidIndexFormat;
      result.gpu_address = index.index_buffer_location;
      return result;
    }
    const uint64_t index_bytes =
        uint64_t(desc.element_count) * uint64_t(index.index_size);
    if (index.index_buffer_offset > index.index_buffer_size ||
        index_bytes > index.index_buffer_size - index.index_buffer_offset) {
      result.reason = D3D12DrawSafetySkipReason::IndexRangeOutOfBounds;
      result.gpu_address = index.index_buffer_location;
      result.size_in_bytes = static_cast<uint32_t>(std::min<uint64_t>(
          index.index_buffer_size, std::numeric_limits<uint32_t>::max()));
      result.required_vertices = index.index_buffer_offset + index_bytes;
      result.available_vertices = index.index_buffer_size;
      return result;
    }
  }

  uint64_t max_vertex_id =
      uint64_t(desc.start_element) + uint64_t(desc.element_count) - 1ull;
  if (index.indexed && index.has_min_max_index) {
    const int64_t min_index =
        int64_t(desc.base_vertex) + int64_t(index.min_index);
    const int64_t max_index =
        int64_t(desc.base_vertex) + int64_t(index.max_index);
    if (min_index < 0 || max_index < 0) {
      result.reason = D3D12DrawSafetySkipReason::VertexRangeOutOfBounds;
      result.required_vertices = max_index < 0 ? 0 : uint64_t(max_index) + 1ull;
      return result;
    }
    max_vertex_id = uint64_t(max_index);
  }

  for (const auto &input : desc.inputs) {
    if (input.system_value)
      continue;

    result.input_slot = input.input_slot;
    result.table_index = input.table_index;

    if (input.input_slot >= 32 ||
        input.table_index == std::numeric_limits<uint32_t>::max()) {
      result.reason = D3D12DrawSafetySkipReason::MissingVertexBuffer;
      return result;
    }

    if (desc.expect_compact_vertex_table &&
        input.table_indexing_mode !=
            D3D12VertexTableIndexingMode::CompactBySlotMask) {
      result.reason = D3D12DrawSafetySkipReason::UnsupportedVertexTableMode;
      return result;
    }

    const auto *view =
        D3D12FindSafetyVertexBuffer(desc.vertex_buffers, input.input_slot);
    if (!view || !view->view_supplied || !view->buffer_location) {
      result.reason = D3D12DrawSafetySkipReason::MissingVertexBuffer;
      return result;
    }

    result.gpu_address = view->buffer_location;
    result.size_in_bytes = view->size_in_bytes;
    result.stride_in_bytes = view->stride_in_bytes;

    if (!view->gpu_address_resolved) {
      result.reason = D3D12DrawSafetySkipReason::UnresolvedVertexBuffer;
      return result;
    }
    if (!view->stride_in_bytes && !view->allow_zero_stride) {
      result.reason = D3D12DrawSafetySkipReason::ZeroStrideVertexBuffer;
      return result;
    }

    uint64_t required = max_vertex_id + 1ull;
    if (input.input_slot_class == D3D12VertexInputSlotClass::PerInstance) {
      if (!input.instance_step_rate) {
        required = uint64_t(desc.start_instance) + 1ull;
      } else {
        required =
            uint64_t(desc.start_instance) +
            (uint64_t(desc.instance_count) + input.instance_step_rate - 1ull) /
                input.instance_step_rate;
      }
    }

    const uint64_t available =
        view->stride_in_bytes ? view->size_in_bytes / view->stride_in_bytes : 0;
    if (required > available) {
      result.reason = D3D12DrawSafetySkipReason::VertexRangeOutOfBounds;
      result.required_vertices = required;
      result.available_vertices = available;
      return result;
    }
  }

  return result;
}

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

    auto view =
        std::find_if(views.begin(), views.end(),
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
