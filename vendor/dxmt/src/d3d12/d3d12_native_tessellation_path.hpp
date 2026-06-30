#pragma once

#include "d3d12.h"
#include <cstdint>
#include <string>

namespace dxmt {

struct D3D12NativeTessellationPSOMetadata {
  bool has_hull_shader = false;
  bool has_domain_shader = false;
  bool complete_hs_ds_pair = false;
  bool patch_topology_type = false;
  uint64_t hull_shader_hash = 0;
  uint64_t domain_shader_hash = 0;
  uint64_t vertex_shader_hash = 0;
  uint64_t pixel_shader_hash = 0;
  size_t hull_shader_bytes = 0;
  size_t domain_shader_bytes = 0;
  size_t vertex_shader_bytes = 0;
  size_t pixel_shader_bytes = 0;
  D3D12_PRIMITIVE_TOPOLOGY_TYPE primitive_topology_type =
      D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
  uint32_t input_layout_elements = 0;
  uint32_t render_target_count = 0;
  DXGI_FORMAT depth_stencil_format = DXGI_FORMAT_UNKNOWN;
};

bool D3D12NativeTessellationRequired(
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC &desc);

D3D12NativeTessellationPSOMetadata InspectD3D12NativeTessellationPSO(
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC &desc);

const char *
D3D12PrimitiveTopologyTypeName(D3D12_PRIMITIVE_TOPOLOGY_TYPE topology_type);

std::string DescribeD3D12NativeTessellationPSO(
    const D3D12NativeTessellationPSOMetadata &metadata);

} // namespace dxmt
