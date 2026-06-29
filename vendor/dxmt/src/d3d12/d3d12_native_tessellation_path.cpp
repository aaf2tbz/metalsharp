#include "d3d12_native_tessellation_path.hpp"

#include "d3d12_trace.hpp"
#include "util_string.hpp"

namespace dxmt {

namespace {

uint64_t ShaderHash(const D3D12_SHADER_BYTECODE &bytecode) {
  if (!bytecode.pShaderBytecode || !bytecode.BytecodeLength)
    return 0;
  return DXMTD3D12Hash64(bytecode.pShaderBytecode, bytecode.BytecodeLength);
}

size_t ShaderSize(const D3D12_SHADER_BYTECODE &bytecode) {
  return bytecode.pShaderBytecode ? bytecode.BytecodeLength : 0;
}

} // namespace

bool D3D12NativeTessellationRequired(
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC &desc) {
  return desc.HS.BytecodeLength > 0 || desc.DS.BytecodeLength > 0;
}

D3D12NativeTessellationPSOMetadata InspectD3D12NativeTessellationPSO(
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC &desc) {
  D3D12NativeTessellationPSOMetadata metadata = {};
  metadata.has_hull_shader = desc.HS.pShaderBytecode && desc.HS.BytecodeLength;
  metadata.has_domain_shader =
      desc.DS.pShaderBytecode && desc.DS.BytecodeLength;
  metadata.complete_hs_ds_pair =
      metadata.has_hull_shader && metadata.has_domain_shader;
  metadata.patch_topology_type =
      desc.PrimitiveTopologyType == D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
  metadata.hull_shader_hash = ShaderHash(desc.HS);
  metadata.domain_shader_hash = ShaderHash(desc.DS);
  metadata.vertex_shader_hash = ShaderHash(desc.VS);
  metadata.pixel_shader_hash = ShaderHash(desc.PS);
  metadata.hull_shader_bytes = ShaderSize(desc.HS);
  metadata.domain_shader_bytes = ShaderSize(desc.DS);
  metadata.vertex_shader_bytes = ShaderSize(desc.VS);
  metadata.pixel_shader_bytes = ShaderSize(desc.PS);
  metadata.primitive_topology_type = desc.PrimitiveTopologyType;
  metadata.input_layout_elements = desc.InputLayout.NumElements;
  metadata.render_target_count = desc.NumRenderTargets;
  metadata.depth_stencil_format = desc.DSVFormat;
  return metadata;
}

const char *
D3D12PrimitiveTopologyTypeName(D3D12_PRIMITIVE_TOPOLOGY_TYPE topology_type) {
  switch (topology_type) {
  case D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED:
    return "undefined";
  case D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT:
    return "point";
  case D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE:
    return "line";
  case D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE:
    return "triangle";
  case D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH:
    return "patch";
  default:
    return "unknown";
  }
}

std::string DescribeD3D12NativeTessellationPSO(
    const D3D12NativeTessellationPSOMetadata &metadata) {
  return str::format(
      "implementation=d3d12_native_tessellation_path ",
      "status=native_tessellation_required ", "d3d11_reuse=forbidden ",
      "hs_present=", metadata.has_hull_shader,
      " ds_present=", metadata.has_domain_shader,
      " hs_ds_pair=", metadata.complete_hs_ds_pair,
      " hs_bytes=", metadata.hull_shader_bytes,
      " ds_bytes=", metadata.domain_shader_bytes, " hs_hash=0x", std::hex,
      metadata.hull_shader_hash, " ds_hash=0x", metadata.domain_shader_hash,
      " vs_hash=0x", metadata.vertex_shader_hash, " ps_hash=0x",
      metadata.pixel_shader_hash, std::dec, " topology_type=",
      D3D12PrimitiveTopologyTypeName(metadata.primitive_topology_type), "(",
      (uint32_t)metadata.primitive_topology_type,
      ") patch_topology_type=", metadata.patch_topology_type,
      " input_layout_elements=", metadata.input_layout_elements,
      " render_targets=", metadata.render_target_count,
      " dsv_format=", (uint32_t)metadata.depth_stencil_format);
}

} // namespace dxmt
