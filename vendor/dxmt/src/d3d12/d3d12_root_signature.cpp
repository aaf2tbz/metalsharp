#include "d3d12_root_signature.hpp"
#include "d3d12_device.hpp"
#include "log/log.hpp"
#include "util_string.hpp"
#include <algorithm>
#include <cstring>
#include <iomanip>

namespace dxmt {

#pragma pack(push, 1)
struct RSHeader {
  uint32_t num_parameters;
  uint32_t num_static_samplers;
  uint32_t flags;
};

struct RSParameter {
  uint8_t type;
  uint8_t visibility;
  union {
    struct {
      uint32_t register_space;
      uint32_t register_index;
      uint32_t num_32bit_values;
    } constants;
    struct {
      uint32_t register_space;
      uint32_t register_index;
    } descriptor;
    struct {
      uint32_t num_ranges;
    } table;
  };
};

struct RSDescriptorRange {
  uint8_t range_type;
  uint32_t num_descriptors;
  uint32_t base_register;
  uint32_t register_space;
  uint32_t offset_in_table;
};

struct RSStaticSampler {
  uint32_t filter;
  uint32_t address_u;
  uint32_t address_v;
  uint32_t address_w;
  float mip_lod_bias;
  uint32_t max_anisotropy;
  uint32_t comparison_func;
  uint32_t border_color;
  float min_lod;
  float max_lod;
  uint32_t register_space;
  uint32_t register_index;
  uint32_t shader_register_space;
  uint8_t shader_visibility;
};

struct DXContainerHeader {
  uint8_t magic[4];
  uint8_t digest[16];
  uint16_t major;
  uint16_t minor;
  uint32_t file_size;
  uint32_t part_count;
};

struct DXContainerPartHeader {
  uint8_t name[4];
  uint32_t size;
};

struct DXRootSignatureHeader {
  uint32_t version;
  uint32_t num_parameters;
  uint32_t parameters_offset;
  uint32_t num_static_samplers;
  uint32_t static_sampler_offset;
  uint32_t flags;
};

struct DXRootParameterHeader {
  uint32_t parameter_type;
  uint32_t shader_visibility;
  uint32_t parameter_offset;
};

struct DXRootConstants {
  uint32_t shader_register;
  uint32_t register_space;
  uint32_t num_32bit_values;
};

struct DXRootDescriptor10 {
  uint32_t shader_register;
  uint32_t register_space;
};

struct DXDescriptorTable {
  uint32_t num_ranges;
  uint32_t ranges_offset;
};

struct DXDescriptorRange10 {
  uint32_t range_type;
  uint32_t num_descriptors;
  uint32_t base_shader_register;
  uint32_t register_space;
  uint32_t offset_in_table;
};

struct DXDescriptorRange11 {
  uint32_t range_type;
  uint32_t num_descriptors;
  uint32_t base_shader_register;
  uint32_t register_space;
  uint32_t flags;
  uint32_t offset_in_table;
};

struct DXStaticSampler {
  uint32_t filter;
  uint32_t address_u;
  uint32_t address_v;
  uint32_t address_w;
  float mip_lod_bias;
  uint32_t max_anisotropy;
  uint32_t comparison_func;
  uint32_t border_color;
  float min_lod;
  float max_lod;
  uint32_t shader_register;
  uint32_t register_space;
  uint32_t shader_visibility;
};
#pragma pack(pop)

#define RSTRACE(fmt, ...) do { FILE *_tf = dxmt::openDiagnosticLog("dxmt-d3d12-trace.log"); if (_tf) { fprintf(_tf, fmt "\n", ##__VA_ARGS__); fclose(_tf); } } while(0)

static bool range_contains(size_t size, uint32_t offset, size_t bytes) {
  return offset <= size && bytes <= size - offset;
}

static WMTSamplerAddressMode
map_sampler_address_mode(D3D12_TEXTURE_ADDRESS_MODE mode) {
  switch (mode) {
  case D3D12_TEXTURE_ADDRESS_MODE_WRAP:
    return WMTSamplerAddressModeRepeat;
  case D3D12_TEXTURE_ADDRESS_MODE_MIRROR:
    return WMTSamplerAddressModeMirrorRepeat;
  case D3D12_TEXTURE_ADDRESS_MODE_CLAMP:
    return WMTSamplerAddressModeClampToEdge;
  case D3D12_TEXTURE_ADDRESS_MODE_BORDER:
    return WMTSamplerAddressModeClampToBorderColor;
  case D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE:
    return WMTSamplerAddressModeMirrorClampToEdge;
  default:
    return WMTSamplerAddressModeClampToEdge;
  }
}

static WMTSamplerBorderColor
map_sampler_border_color(D3D12_STATIC_BORDER_COLOR color) {
  switch (color) {
  case D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK:
    return WMTSamplerBorderColorOpaqueBlack;
  case D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE:
    return WMTSamplerBorderColorOpaqueWhite;
  default:
    return WMTSamplerBorderColorTransparentBlack;
  }
}

static WMTSamplerInfo sampler_info_from_static_desc(
    const D3D12_STATIC_SAMPLER_DESC &desc) {
  WMTSamplerInfo info = {};
  switch (desc.Filter) {
  case D3D12_FILTER_MIN_MAG_MIP_POINT:
    info.min_filter = WMTSamplerMinMagFilterNearest;
    info.mag_filter = WMTSamplerMinMagFilterNearest;
    info.mip_filter = WMTSamplerMipFilterNearest;
    break;
  case D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR:
    info.min_filter = WMTSamplerMinMagFilterNearest;
    info.mag_filter = WMTSamplerMinMagFilterNearest;
    info.mip_filter = WMTSamplerMipFilterLinear;
    break;
  case D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT:
    info.min_filter = WMTSamplerMinMagFilterNearest;
    info.mag_filter = WMTSamplerMinMagFilterLinear;
    info.mip_filter = WMTSamplerMipFilterNearest;
    break;
  case D3D12_FILTER_MIN_POINT_MAG_MIP_LINEAR:
    info.min_filter = WMTSamplerMinMagFilterNearest;
    info.mag_filter = WMTSamplerMinMagFilterLinear;
    info.mip_filter = WMTSamplerMipFilterLinear;
    break;
  case D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT:
    info.min_filter = WMTSamplerMinMagFilterLinear;
    info.mag_filter = WMTSamplerMinMagFilterNearest;
    info.mip_filter = WMTSamplerMipFilterNearest;
    break;
  case D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR:
    info.min_filter = WMTSamplerMinMagFilterLinear;
    info.mag_filter = WMTSamplerMinMagFilterNearest;
    info.mip_filter = WMTSamplerMipFilterLinear;
    break;
  case D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT:
    info.min_filter = WMTSamplerMinMagFilterLinear;
    info.mag_filter = WMTSamplerMinMagFilterLinear;
    info.mip_filter = WMTSamplerMipFilterNearest;
    break;
  case D3D12_FILTER_MIN_MAG_MIP_LINEAR:
  case D3D12_FILTER_ANISOTROPIC:
  case D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR:
  case D3D12_FILTER_COMPARISON_ANISOTROPIC:
    info.min_filter = WMTSamplerMinMagFilterLinear;
    info.mag_filter = WMTSamplerMinMagFilterLinear;
    info.mip_filter = WMTSamplerMipFilterLinear;
    break;
  default:
    info.min_filter = WMTSamplerMinMagFilterLinear;
    info.mag_filter = WMTSamplerMinMagFilterLinear;
    info.mip_filter = WMTSamplerMipFilterLinear;
    break;
  }

  if (desc.Filter == D3D12_FILTER_ANISOTROPIC ||
      desc.Filter == D3D12_FILTER_COMPARISON_ANISOTROPIC)
    info.max_anisotroy = desc.MaxAnisotropy;

  info.s_address_mode = map_sampler_address_mode(desc.AddressU);
  info.t_address_mode = map_sampler_address_mode(desc.AddressV);
  info.r_address_mode = map_sampler_address_mode(desc.AddressW);
  info.border_color = map_sampler_border_color(desc.BorderColor);
  info.lod_min_clamp = desc.MinLOD;
  info.lod_max_clamp = desc.MaxLOD;
  info.normalized_coords = true;
  info.support_argument_buffers = true;
  if (desc.Filter >= D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT &&
      desc.Filter <= D3D12_FILTER_COMPARISON_ANISOTROPIC &&
      desc.ComparisonFunc >= D3D12_COMPARISON_FUNC_LESS &&
      desc.ComparisonFunc <= D3D12_COMPARISON_FUNC_ALWAYS) {
    info.compare_function =
        static_cast<WMTCompareFunction>(desc.ComparisonFunc - 1);
  }
  return info;
}

static uint64_t sampler_lod_bias_bits(float lod_bias) {
  uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(lod_bias));
  memcpy(&bits, &lod_bias, sizeof(bits));
  return bits;
}

static uint64_t m12_root_field(uint8_t tag, uint64_t payload) {
  return (uint64_t(tag) << 56) | (payload & 0x00ffffffffffffffull);
}

RootStaticSampler make_static_sampler(WMT::Device device,
                                      const D3D12_STATIC_SAMPLER_DESC &desc) {
  RootStaticSampler sampler = {};
  sampler.shader_register = desc.ShaderRegister;
  sampler.register_space = desc.RegisterSpace;
  sampler.shader_visibility = desc.ShaderVisibility;
  sampler.filter = desc.Filter;
  sampler.address_u = desc.AddressU;
  sampler.address_v = desc.AddressV;
  sampler.address_w = desc.AddressW;
  sampler.max_anisotropy = desc.MaxAnisotropy;
  sampler.comparison_func = desc.ComparisonFunc;
  sampler.border_color = desc.BorderColor;
  sampler.lod_bias_bits = sampler_lod_bias_bits(desc.MipLODBias);
  sampler.min_lod_bits = sampler_lod_bias_bits(desc.MinLOD);
  sampler.max_lod_bits = sampler_lod_bias_bits(desc.MaxLOD);

  WMTSamplerInfo info = sampler_info_from_static_desc(desc);
  sampler.sampler = device.newSamplerState(info);
  sampler.sampler_gpu_id = info.gpu_resource_id;

  WMTSamplerInfo cube_info = info;
  if (cube_info.min_filter == WMTSamplerMinMagFilterLinear &&
      cube_info.mag_filter == WMTSamplerMinMagFilterLinear) {
    cube_info.s_address_mode = WMTSamplerAddressModeClampToBorderColor;
    cube_info.t_address_mode = WMTSamplerAddressModeClampToBorderColor;
    cube_info.r_address_mode = WMTSamplerAddressModeClampToBorderColor;
  } else {
    cube_info.s_address_mode = WMTSamplerAddressModeClampToEdge;
    cube_info.t_address_mode = WMTSamplerAddressModeClampToEdge;
    cube_info.r_address_mode = WMTSamplerAddressModeClampToEdge;
  }
  sampler.sampler_cube = device.newSamplerState(cube_info);
  sampler.sampler_cube_gpu_id = cube_info.gpu_resource_id;
  return sampler;
}

MTLD3D12RootSignature::MTLD3D12RootSignature(MTLD3D12Device *device,
                                             const void *blob, SIZE_T blob_size)
    : m_device(device) {
  m_device->AddRef();
  if (blob && blob_size) {
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(blob);
    for (SIZE_T i = 0; i < blob_size; i++)
      m_blob_hash = m_blob_hash * 131 + bytes[i];
  }
  Parse(blob, blob_size);
  SummarizeWithM12Core();
  Logger::info(str::format("D3D12RootSignature: ", m_parameters.size(),
                            " params, ", m_num_static_samplers,
                            " static samplers, flags=", m_flags,
                            " hash=0x", std::hex, m_blob_hash, std::dec));
}

MTLD3D12RootSignature::~MTLD3D12RootSignature() { m_device->Release(); }

void MTLD3D12RootSignature::BuildM12CoreBindingPlanArrays() {
  /* Phase 5 binding-plan persistence seam: retain a flattened POD copy of the
   * parsed root signature on the PE object so future binding paths can reuse
   * the same native-facing description without rebuilding it per draw.  The
   * authoritative live binding model remains m_parameters/m_static_samplers.
   */
  m_core_binding_parameters.clear();
  m_core_binding_ranges.clear();
  m_core_static_samplers.clear();
  m_core_binding_parameters.reserve(m_parameters.size());
  m_core_static_samplers.reserve(m_static_samplers.size());
  size_t total_ranges = 0;
  for (const auto &param : m_parameters)
    total_ranges += param.ranges.size();
  m_core_binding_ranges.reserve(total_ranges);

  for (const auto &param : m_parameters) {
    M12CoreRootBindingParameter core_param = {};
    core_param.type = static_cast<uint32_t>(param.type);
    core_param.shader_visibility = param.shader_visibility;
    core_param.register_space = param.register_space;
    core_param.register_index = param.register_index;
    core_param.num_descriptors = param.num_descriptors;
    core_param.num_32bit_values = param.num_32bit_values;
    core_param.descriptor_flags = param.descriptor_flags;
    core_param.range_start = static_cast<uint32_t>(m_core_binding_ranges.size());
    core_param.range_count = static_cast<uint32_t>(param.ranges.size());
    for (const auto &range : param.ranges) {
      M12CoreRootBindingRange core_range = {};
      core_range.range_type = static_cast<uint32_t>(range.range_type);
      core_range.num_descriptors = range.num_descriptors;
      core_range.base_register = range.base_register;
      core_range.register_space = range.register_space;
      core_range.offset_in_table = range.offset_in_table;
      core_range.flags = range.flags;
      m_core_binding_ranges.push_back(core_range);
    }
    m_core_binding_parameters.push_back(core_param);
  }

  for (const auto &sampler : m_static_samplers) {
    M12CoreStaticSamplerBinding core_sampler = {};
    core_sampler.shader_register = sampler.shader_register;
    core_sampler.register_space = sampler.register_space;
    core_sampler.shader_visibility = sampler.shader_visibility;
    m_core_static_samplers.push_back(core_sampler);
  }
}

M12CoreRootBindingPlanDesc MTLD3D12RootSignature::MakeM12CoreBindingPlanDesc() const {
  M12CoreRootBindingPlanDesc plan_desc = {};
  plan_desc.abi_version = M12CORE_ABI_VERSION;
  plan_desc.flags = static_cast<uint32_t>(m_flags);
  plan_desc.root_signature_key = m_core_root_signature_key;
  plan_desc.parameters = m_core_binding_parameters.empty() ? nullptr : m_core_binding_parameters.data();
  plan_desc.parameter_count = static_cast<uint32_t>(m_core_binding_parameters.size());
  plan_desc.ranges = m_core_binding_ranges.empty() ? nullptr : m_core_binding_ranges.data();
  plan_desc.range_count = static_cast<uint32_t>(m_core_binding_ranges.size());
  plan_desc.static_samplers = m_core_static_samplers.empty() ? nullptr : m_core_static_samplers.data();
  plan_desc.static_sampler_count = static_cast<uint32_t>(m_core_static_samplers.size());
  return plan_desc;
}

bool MTLD3D12RootSignature::LookupM12CoreDescriptorRangeForVisibility(
    D3D12_DESCRIPTOR_RANGE_TYPE range_type, uint32_t shader_register,
    uint32_t register_space, D3D12_SHADER_VISIBILITY shader_visibility,
    M12CoreRootBindingLookupResult *out_result) const {
  if (!m_core_binding_plan_valid || !out_result)
    return false;

  /* Phase 5 lookup seam: this native lookup mirrors the PE helper but is not
   * used for live binding yet.  It provides the exact future call boundary while
   * preserving the PE helper as fallback/authority during migration.
   */
  const auto plan_desc = MakeM12CoreBindingPlanDesc();
  M12CoreRootBindingLookupDesc lookup = {};
  lookup.abi_version = M12CORE_ABI_VERSION;
  lookup.lookup_kind = M12CORE_ROOT_BINDING_LOOKUP_DESCRIPTOR_RANGE;
  lookup.range_type = static_cast<uint32_t>(range_type);
  lookup.shader_register = shader_register;
  lookup.register_space = register_space;
  lookup.shader_visibility = static_cast<uint32_t>(shader_visibility);
  lookup.parameters = plan_desc.parameters;
  lookup.parameter_count = plan_desc.parameter_count;
  lookup.ranges = plan_desc.ranges;
  lookup.range_count = plan_desc.range_count;
  lookup.static_samplers = plan_desc.static_samplers;
  lookup.static_sampler_count = plan_desc.static_sampler_count;
  return WMTM12CoreLookupRootBinding(&lookup, out_result) &&
         out_result->abi_version == M12CORE_ABI_VERSION &&
         out_result->status == M12CORE_ROOT_SIGNATURE_STATUS_OK;
}

bool MTLD3D12RootSignature::LookupM12CoreStaticSampler(
    uint32_t shader_register, uint32_t register_space,
    D3D12_SHADER_VISIBILITY shader_visibility,
    M12CoreRootBindingLookupResult *out_result) const {
  if (!m_core_binding_plan_valid || !out_result)
    return false;

  const auto plan_desc = MakeM12CoreBindingPlanDesc();
  M12CoreRootBindingLookupDesc lookup = {};
  lookup.abi_version = M12CORE_ABI_VERSION;
  lookup.lookup_kind = M12CORE_ROOT_BINDING_LOOKUP_STATIC_SAMPLER;
  lookup.shader_register = shader_register;
  lookup.register_space = register_space;
  lookup.shader_visibility = static_cast<uint32_t>(shader_visibility);
  lookup.parameters = plan_desc.parameters;
  lookup.parameter_count = plan_desc.parameter_count;
  lookup.ranges = plan_desc.ranges;
  lookup.range_count = plan_desc.range_count;
  lookup.static_samplers = plan_desc.static_samplers;
  lookup.static_sampler_count = plan_desc.static_sampler_count;
  return WMTM12CoreLookupRootBinding(&lookup, out_result) &&
         out_result->abi_version == M12CORE_ABI_VERSION &&
         out_result->status == M12CORE_ROOT_SIGNATURE_STATUS_OK;
}

bool MTLD3D12RootSignature::LookupM12CoreRootDescriptor(
    D3D12_ROOT_PARAMETER_TYPE type, uint32_t shader_register,
    uint32_t register_space, D3D12_SHADER_VISIBILITY shader_visibility,
    M12CoreRootBindingLookupResult *out_result) const {
  if (!m_core_binding_plan_valid || !out_result)
    return false;

  const auto plan_desc = MakeM12CoreBindingPlanDesc();
  M12CoreRootBindingLookupDesc lookup = {};
  lookup.abi_version = M12CORE_ABI_VERSION;
  lookup.lookup_kind = M12CORE_ROOT_BINDING_LOOKUP_ROOT_DESCRIPTOR;
  /* For root-descriptor lookups, range_type carries the root parameter type so
   * the fixed Phase 5 thunk payload can cover CBV/SRV/UAV without a new struct.
   */
  lookup.range_type = static_cast<uint32_t>(type);
  lookup.shader_register = shader_register;
  lookup.register_space = register_space;
  lookup.shader_visibility = static_cast<uint32_t>(shader_visibility);
  lookup.parameters = plan_desc.parameters;
  lookup.parameter_count = plan_desc.parameter_count;
  lookup.ranges = plan_desc.ranges;
  lookup.range_count = plan_desc.range_count;
  lookup.static_samplers = plan_desc.static_samplers;
  lookup.static_sampler_count = plan_desc.static_sampler_count;
  return WMTM12CoreLookupRootBinding(&lookup, out_result) &&
         out_result->abi_version == M12CORE_ABI_VERSION &&
         out_result->status == M12CORE_ROOT_SIGNATURE_STATUS_OK;
}

bool MTLD3D12RootSignature::LookupM12CoreRootConstants(
    uint32_t shader_register, uint32_t register_space,
    D3D12_SHADER_VISIBILITY shader_visibility,
    M12CoreRootBindingLookupResult *out_result) const {
  if (!m_core_binding_plan_valid || !out_result)
    return false;

  const auto plan_desc = MakeM12CoreBindingPlanDesc();
  M12CoreRootBindingLookupDesc lookup = {};
  lookup.abi_version = M12CORE_ABI_VERSION;
  lookup.lookup_kind = M12CORE_ROOT_BINDING_LOOKUP_ROOT_CONSTANTS;
  lookup.shader_register = shader_register;
  lookup.register_space = register_space;
  lookup.shader_visibility = static_cast<uint32_t>(shader_visibility);
  lookup.parameters = plan_desc.parameters;
  lookup.parameter_count = plan_desc.parameter_count;
  lookup.ranges = plan_desc.ranges;
  lookup.range_count = plan_desc.range_count;
  lookup.static_samplers = plan_desc.static_samplers;
  lookup.static_sampler_count = plan_desc.static_sampler_count;
  return WMTM12CoreLookupRootBinding(&lookup, out_result) &&
         out_result->abi_version == M12CORE_ABI_VERSION &&
         out_result->status == M12CORE_ROOT_SIGNATURE_STATUS_OK;
}

void MTLD3D12RootSignature::ValidateM12CoreBindingPlanLookups(
    uint32_t *out_lookup_checks, uint32_t *out_lookup_mismatches) const {
  uint32_t lookup_checks = 0;
  uint32_t lookup_mismatches = 0;

  for (uint32_t p = 0; p < m_parameters.size(); p++) {
    const auto &param = m_parameters[p];
    for (const auto &range : param.ranges) {
      M12CoreRootBindingLookupResult native_lookup = {};
      uint32_t pe_root = UINT32_MAX;
      uint32_t pe_offset = UINT32_MAX;
      const bool pe_found = FindDescriptorTableRangeForVisibilityPELocal(
          range.range_type, range.base_register, range.register_space,
          static_cast<D3D12_SHADER_VISIBILITY>(param.shader_visibility),
          &pe_root, &pe_offset);
      const bool native_ok = LookupM12CoreDescriptorRangeForVisibility(
          range.range_type, range.base_register, range.register_space,
          static_cast<D3D12_SHADER_VISIBILITY>(param.shader_visibility),
          &native_lookup);
      uint32_t plan_root = UINT32_MAX;
      uint32_t plan_offset = UINT32_MAX;
      const bool plan_found = FindDescriptorTableRangeForVisibility(
          range.range_type, range.base_register, range.register_space,
          static_cast<D3D12_SHADER_VISIBILITY>(param.shader_visibility),
          &plan_root, &plan_offset);
      lookup_checks++;
      if (!native_ok || (native_lookup.found != (pe_found ? 1u : 0u)) ||
          (pe_found && (native_lookup.root_parameter_index != pe_root ||
                        native_lookup.descriptor_offset != pe_offset))) {
        lookup_mismatches++;
        Logger::warn(str::format("M12_ROOT_BINDING_LOOKUP_MISMATCH key=0x",
                                 std::hex, m_core_root_signature_key, std::dec,
                                 " type=range range_type=", (uint32_t)range.range_type,
                                 " reg=", range.base_register,
                                 " space=", range.register_space,
                                 " native_ok=", native_ok ? 1 : 0,
                                 " native_found=", native_lookup.found,
                                 " native_root=", native_lookup.root_parameter_index,
                                 " native_offset=", native_lookup.descriptor_offset,
                                 " pe_found=", pe_found ? 1 : 0,
                                 " pe_root=", pe_root,
                                 " pe_offset=", pe_offset));
      }
      if (plan_found != pe_found ||
          (pe_found && (plan_root != pe_root || plan_offset != pe_offset))) {
        lookup_mismatches++;
        Logger::warn(str::format("M12_ROOT_BINDING_LOOKUP_MISMATCH key=0x",
                                 std::hex, m_core_root_signature_key, std::dec,
                                 " type=plan_range range_type=", (uint32_t)range.range_type,
                                 " reg=", range.base_register,
                                 " space=", range.register_space,
                                 " plan_found=", plan_found ? 1 : 0,
                                 " plan_root=", plan_root,
                                 " plan_offset=", plan_offset,
                                 " pe_found=", pe_found ? 1 : 0,
                                 " pe_root=", pe_root,
                                 " pe_offset=", pe_offset));
      }
    }
  }

  for (uint32_t p = 0; p < m_parameters.size(); p++) {
    const auto &param = m_parameters[p];
    if (param.type != D3D12_ROOT_PARAMETER_TYPE_CBV &&
        param.type != D3D12_ROOT_PARAMETER_TYPE_SRV &&
        param.type != D3D12_ROOT_PARAMETER_TYPE_UAV)
      continue;

    M12CoreRootBindingLookupResult native_lookup = {};
    uint32_t pe_root = UINT32_MAX;
    bool pe_found = false;
    for (uint32_t visibility_pass = 0; visibility_pass < 2 && !pe_found; visibility_pass++) {
      for (uint32_t candidate = 0; candidate < m_parameters.size(); candidate++) {
        const auto &pe_param = m_parameters[candidate];
        if (pe_param.type != param.type ||
            pe_param.register_index != param.register_index ||
            pe_param.register_space != param.register_space)
          continue;
        if (visibility_pass == 0 && pe_param.shader_visibility != param.shader_visibility)
          continue;
        if (visibility_pass == 1 && pe_param.shader_visibility != D3D12_SHADER_VISIBILITY_ALL)
          continue;
        pe_root = candidate;
        pe_found = true;
        break;
      }
    }

    const bool native_ok = LookupM12CoreRootDescriptor(
        param.type, param.register_index, param.register_space,
        static_cast<D3D12_SHADER_VISIBILITY>(param.shader_visibility),
        &native_lookup);
    uint32_t plan_root = UINT32_MAX;
    const bool plan_found = FindRootDescriptorParameter(
        param.type, param.register_index, param.register_space,
        static_cast<D3D12_SHADER_VISIBILITY>(param.shader_visibility),
        &plan_root);
    lookup_checks++;
    if (!native_ok || (native_lookup.found != (pe_found ? 1u : 0u)) ||
        (pe_found && native_lookup.root_parameter_index != pe_root)) {
      lookup_mismatches++;
      Logger::warn(str::format("M12_ROOT_BINDING_LOOKUP_MISMATCH key=0x",
                               std::hex, m_core_root_signature_key, std::dec,
                               " type=root_descriptor root_type=", (uint32_t)param.type,
                               " reg=", param.register_index,
                               " space=", param.register_space,
                               " native_ok=", native_ok ? 1 : 0,
                               " native_found=", native_lookup.found,
                               " native_root=", native_lookup.root_parameter_index,
                               " pe_found=", pe_found ? 1 : 0,
                               " pe_root=", pe_root));
    }
    if (plan_found != pe_found || (pe_found && plan_root != pe_root)) {
      lookup_mismatches++;
      Logger::warn(str::format("M12_ROOT_BINDING_LOOKUP_MISMATCH key=0x",
                               std::hex, m_core_root_signature_key, std::dec,
                               " type=plan_root_descriptor root_type=", (uint32_t)param.type,
                               " reg=", param.register_index,
                               " space=", param.register_space,
                               " plan_found=", plan_found ? 1 : 0,
                               " plan_root=", plan_root,
                               " pe_found=", pe_found ? 1 : 0,
                               " pe_root=", pe_root));
    }
  }

  for (uint32_t p = 0; p < m_parameters.size(); p++) {
    const auto &param = m_parameters[p];
    if (param.type != D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS)
      continue;

    M12CoreRootBindingLookupResult native_lookup = {};
    uint32_t pe_root = UINT32_MAX;
    uint32_t pe_dwords = 0;
    bool pe_found = false;
    for (uint32_t visibility_pass = 0; visibility_pass < 2 && !pe_found; visibility_pass++) {
      for (uint32_t candidate = 0; candidate < m_parameters.size(); candidate++) {
        const auto &pe_param = m_parameters[candidate];
        if (pe_param.type != D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS ||
            pe_param.register_index != param.register_index ||
            pe_param.register_space != param.register_space)
          continue;
        if (visibility_pass == 0 && pe_param.shader_visibility != param.shader_visibility)
          continue;
        if (visibility_pass == 1 && pe_param.shader_visibility != D3D12_SHADER_VISIBILITY_ALL)
          continue;
        pe_root = candidate;
        pe_dwords = pe_param.num_32bit_values;
        pe_found = true;
        break;
      }
    }

    const bool native_ok = LookupM12CoreRootConstants(
        param.register_index, param.register_space,
        static_cast<D3D12_SHADER_VISIBILITY>(param.shader_visibility),
        &native_lookup);
    uint32_t plan_root = UINT32_MAX;
    uint32_t plan_dwords = 0;
    const bool plan_found = FindRootConstantsParameter(
        param.register_index, param.register_space,
        static_cast<D3D12_SHADER_VISIBILITY>(param.shader_visibility),
        &plan_root, &plan_dwords);
    lookup_checks++;
    if (!native_ok || (native_lookup.found != (pe_found ? 1u : 0u)) ||
        (pe_found && (native_lookup.root_parameter_index != pe_root ||
                      native_lookup.descriptor_offset != pe_dwords))) {
      lookup_mismatches++;
      Logger::warn(str::format("M12_ROOT_BINDING_LOOKUP_MISMATCH key=0x",
                               std::hex, m_core_root_signature_key, std::dec,
                               " type=root_constants reg=", param.register_index,
                               " space=", param.register_space,
                               " native_ok=", native_ok ? 1 : 0,
                               " native_found=", native_lookup.found,
                               " native_root=", native_lookup.root_parameter_index,
                               " native_dwords=", native_lookup.descriptor_offset,
                               " pe_found=", pe_found ? 1 : 0,
                               " pe_root=", pe_root,
                               " pe_dwords=", pe_dwords));
    }
    if (plan_found != pe_found ||
        (pe_found && (plan_root != pe_root || plan_dwords != pe_dwords))) {
      lookup_mismatches++;
      Logger::warn(str::format("M12_ROOT_BINDING_LOOKUP_MISMATCH key=0x",
                               std::hex, m_core_root_signature_key, std::dec,
                               " type=plan_root_constants reg=", param.register_index,
                               " space=", param.register_space,
                               " plan_found=", plan_found ? 1 : 0,
                               " plan_root=", plan_root,
                               " plan_dwords=", plan_dwords,
                               " pe_found=", pe_found ? 1 : 0,
                               " pe_root=", pe_root,
                               " pe_dwords=", pe_dwords));
    }
  }

  for (const auto &sampler : m_static_samplers) {
    M12CoreRootBindingLookupResult native_lookup = {};
    const auto *pe_sampler = FindStaticSamplerPELocal(
        sampler.shader_register, sampler.register_space,
        static_cast<D3D12_SHADER_VISIBILITY>(sampler.shader_visibility));
    const bool pe_found = pe_sampler != nullptr;
    const auto *plan_sampler = FindStaticSampler(
        sampler.shader_register, sampler.register_space,
        static_cast<D3D12_SHADER_VISIBILITY>(sampler.shader_visibility));
    const bool plan_found = plan_sampler != nullptr;
    const bool native_ok = LookupM12CoreStaticSampler(
        sampler.shader_register, sampler.register_space,
        static_cast<D3D12_SHADER_VISIBILITY>(sampler.shader_visibility),
        &native_lookup);
    lookup_checks++;
    if (!native_ok || (native_lookup.found != (pe_found ? 1u : 0u))) {
      lookup_mismatches++;
      Logger::warn(str::format("M12_ROOT_BINDING_LOOKUP_MISMATCH key=0x",
                               std::hex, m_core_root_signature_key, std::dec,
                               " type=static_sampler reg=", sampler.shader_register,
                               " space=", sampler.register_space,
                               " native_ok=", native_ok ? 1 : 0,
                               " native_found=", native_lookup.found,
                               " pe_found=", pe_found ? 1 : 0));
    }
    if (plan_found != pe_found || (pe_found && plan_sampler != pe_sampler)) {
      lookup_mismatches++;
      Logger::warn(str::format("M12_ROOT_BINDING_LOOKUP_MISMATCH key=0x",
                               std::hex, m_core_root_signature_key, std::dec,
                               " type=plan_static_sampler reg=", sampler.shader_register,
                               " space=", sampler.register_space,
                               " plan_found=", plan_found ? 1 : 0,
                               " pe_found=", pe_found ? 1 : 0));
    }
  }

  if (out_lookup_checks)
    *out_lookup_checks = lookup_checks;
  if (out_lookup_mismatches)
    *out_lookup_mismatches = lookup_mismatches;
}

void MTLD3D12RootSignature::SummarizeWithM12Core() {
  /* Phase 5 migration seam: D3D12 still owns Windows blob parsing and all live
   * descriptor binding behavior, while libm12core owns the stable structural
   * key/summary that future binding-plan migration will consume.  If the
   * native core is disabled or lacks this ABI, the existing blob-hash path and
   * PE-local lookup helpers remain the fallback.
   */
  std::vector<uint64_t> fields;
  fields.reserve(m_parameters.size() * 4 + m_static_samplers.size());
  for (uint32_t i = 0; i < m_parameters.size(); i++) {
    const auto &param = m_parameters[i];
    uint8_t tag = 0x50;
    if (param.type == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
      tag = 0x51;
    else if (param.type == D3D12_ROOT_PARAMETER_TYPE_CBV ||
             param.type == D3D12_ROOT_PARAMETER_TYPE_SRV ||
             param.type == D3D12_ROOT_PARAMETER_TYPE_UAV)
      tag = 0x53;
    else if (param.type == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS)
      tag = 0x54;

    fields.push_back(m12_root_field(tag, uint64_t(i) |
                                         (uint64_t(param.type) << 8) |
                                         (uint64_t(param.shader_visibility) << 16)));
    fields.push_back(m12_root_field(0x56, param.register_index));
    fields.push_back(m12_root_field(0x56, param.register_space));
    fields.push_back(m12_root_field(0x56, param.num_descriptors));
    fields.push_back(m12_root_field(0x56, param.num_32bit_values));
    fields.push_back(m12_root_field(0x56, param.descriptor_flags));
    fields.push_back(m12_root_field(0x56, param.descriptor_table_entries));
    for (uint32_t r = 0; r < param.ranges.size(); r++) {
      const auto &range = param.ranges[r];
      fields.push_back(m12_root_field(0x52, uint64_t(i) |
                                           (uint64_t(r) << 8) |
                                           (uint64_t(range.range_type) << 24)));
      fields.push_back(m12_root_field(0x56, range.base_register));
      fields.push_back(m12_root_field(0x56, range.register_space));
      fields.push_back(m12_root_field(0x56, range.num_descriptors));
      fields.push_back(m12_root_field(0x56, range.offset_in_table));
      fields.push_back(m12_root_field(0x56, range.flags));
    }
  }
  for (uint32_t i = 0; i < m_static_samplers.size(); i++) {
    const auto &sampler = m_static_samplers[i];
    fields.push_back(m12_root_field(0x55, uint64_t(i) |
                                         (uint64_t(sampler.shader_visibility) << 8)));
    fields.push_back(m12_root_field(0x56, sampler.shader_register));
    fields.push_back(m12_root_field(0x56, sampler.register_space));
    fields.push_back(m12_root_field(0x56, sampler.filter));
    fields.push_back(m12_root_field(0x56, sampler.address_u));
    fields.push_back(m12_root_field(0x56, sampler.address_v));
    fields.push_back(m12_root_field(0x56, sampler.address_w));
    fields.push_back(m12_root_field(0x56, sampler.max_anisotropy));
    fields.push_back(m12_root_field(0x56, sampler.comparison_func));
    fields.push_back(m12_root_field(0x56, sampler.border_color));
    fields.push_back(m12_root_field(0x56, sampler.lod_bias_bits));
    fields.push_back(m12_root_field(0x56, sampler.min_lod_bits));
    fields.push_back(m12_root_field(0x56, sampler.max_lod_bits));
  }

  M12CoreRootSignatureDesc desc = {};
  desc.abi_version = M12CORE_ABI_VERSION;
  desc.parameter_count = static_cast<uint32_t>(m_parameters.size());
  desc.static_sampler_count = static_cast<uint32_t>(m_static_samplers.size());
  desc.flags = static_cast<uint32_t>(m_flags);
  desc.blob_hash = static_cast<uint64_t>(m_blob_hash);
  desc.fields = fields.empty() ? nullptr : fields.data();
  desc.field_count = static_cast<uint32_t>(fields.size());

  M12CoreRootSignatureSummary summary = {};
  if (!WMTM12CoreSummarizeRootSignature(&desc, &summary) ||
      summary.abi_version != M12CORE_ABI_VERSION ||
      summary.status != M12CORE_ROOT_SIGNATURE_STATUS_OK)
    return;

  m_core_summary = summary;
  m_core_root_signature_key = summary.root_signature_key;
  m_core_summary_valid = true;
  Logger::info(str::format("M12_ROOT_SIGNATURE_CORE key=0x", std::hex,
                           m_core_root_signature_key, std::dec,
                           " params=", summary.parameter_count,
                           " tables=", summary.descriptor_table_count,
                           " ranges=", summary.descriptor_range_count,
                           " root_desc=", summary.root_descriptor_count,
                           " root_constants=", summary.root_constant_count,
                           " static_samplers=", summary.static_sampler_count));

  BuildM12CoreBindingPlanArrays();
  const auto plan_desc = MakeM12CoreBindingPlanDesc();
  M12CoreRootBindingPlanSummary plan_summary = {};
  if (!WMTM12CoreBuildRootBindingPlan(&plan_desc, &plan_summary) ||
      plan_summary.abi_version != M12CORE_ABI_VERSION ||
      plan_summary.status != M12CORE_ROOT_SIGNATURE_STATUS_OK)
    return;

  m_core_binding_plan_summary = plan_summary;
  m_core_binding_plan_valid = true;

  uint32_t lookup_checks = 0;
  uint32_t lookup_mismatches = 0;
  ValidateM12CoreBindingPlanLookups(&lookup_checks, &lookup_mismatches);

  Logger::info(str::format("M12_ROOT_BINDING_PLAN key=0x", std::hex,
                           plan_summary.binding_plan_key, std::dec,
                           " root_key=0x", std::hex, m_core_root_signature_key,
                           std::dec,
                           " params=", plan_summary.parameter_count,
                           " tables=", plan_summary.descriptor_table_count,
                           " ranges=", plan_summary.descriptor_range_count,
                           " resources=", plan_summary.resource_range_count,
                           " samplers=", plan_summary.sampler_range_count,
                           " root_desc=", plan_summary.root_descriptor_count,
                           " root_constants=", plan_summary.root_constant_count,
                           " static_samplers=", plan_summary.static_sampler_count,
                           " spaces=", plan_summary.register_space_count,
                           " max_span=", plan_summary.max_descriptor_table_span,
                           " arg_resources=", plan_summary.argument_resource_slot_count,
                           " arg_samplers=", plan_summary.argument_sampler_slot_count,
                           " arg_root_desc=", plan_summary.argument_root_descriptor_slot_count,
                           " arg_const_dwords=", plan_summary.argument_root_constant_dword_count,
                           " arg_visibility=0x", std::hex, plan_summary.argument_visibility_mask,
                           std::dec,
                           " lookup_checks=", lookup_checks,
                           " lookup_mismatches=", lookup_mismatches));
}

void MTLD3D12RootSignature::Parse(const void *blob, SIZE_T blob_size) {
  if (!blob || blob_size < sizeof(RSHeader))
    return;

  auto parse_rts0 = [&](const uint8_t *data, size_t size) -> bool {
    if (size < sizeof(DXRootSignatureHeader))
      return false;

    auto header = reinterpret_cast<const DXRootSignatureHeader *>(data);
    if ((header->version != D3D_ROOT_SIGNATURE_VERSION_1_0 &&
         header->version != D3D_ROOT_SIGNATURE_VERSION_1_1) ||
        (header->num_parameters > 0 &&
         header->parameters_offset < sizeof(DXRootSignatureHeader)) ||
        !range_contains(size, header->parameters_offset,
                        header->num_parameters *
                            sizeof(DXRootParameterHeader)) ||
        header->num_parameters > 64 ||
        header->num_static_samplers > 64)
      return false;

    if (header->num_static_samplers > 0 &&
        (header->static_sampler_offset < sizeof(DXRootSignatureHeader) ||
         !range_contains(size, header->static_sampler_offset,
                         header->num_static_samplers * 52u)))
      return false;

    m_parameters.clear();
    m_static_samplers.clear();
    m_num_static_samplers = header->num_static_samplers;
    m_flags = static_cast<D3D12_ROOT_SIGNATURE_FLAGS>(header->flags);

    auto param_headers = reinterpret_cast<const DXRootParameterHeader *>(
        data + header->parameters_offset);

    for (uint32_t i = 0; i < header->num_parameters; i++) {
      const auto &src = param_headers[i];
      if (!range_contains(size, src.parameter_offset, sizeof(uint32_t)))
        return false;

      RootParameter rp = {};
      rp.type = static_cast<D3D12_ROOT_PARAMETER_TYPE>(src.parameter_type);
      rp.shader_visibility = src.shader_visibility;

      switch (rp.type) {
      case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS: {
        if (!range_contains(size, src.parameter_offset,
                            sizeof(DXRootConstants)))
          return false;
        auto constants = reinterpret_cast<const DXRootConstants *>(
            data + src.parameter_offset);
        rp.register_space = constants->register_space;
        rp.register_index = constants->shader_register;
        rp.num_32bit_values = constants->num_32bit_values;
        rp.num_descriptors = constants->num_32bit_values;
        break;
      }
      case D3D12_ROOT_PARAMETER_TYPE_CBV:
      case D3D12_ROOT_PARAMETER_TYPE_SRV:
      case D3D12_ROOT_PARAMETER_TYPE_UAV: {
        size_t descriptor_size = header->version == D3D_ROOT_SIGNATURE_VERSION_1_0
                                     ? sizeof(DXRootDescriptor10)
                                     : sizeof(DXRootDescriptor10) + sizeof(uint32_t);
        if (!range_contains(size, src.parameter_offset, descriptor_size))
          return false;
        auto descriptor = reinterpret_cast<const DXRootDescriptor10 *>(
            data + src.parameter_offset);
        rp.register_space = descriptor->register_space;
        rp.register_index = descriptor->shader_register;
        if (header->version == D3D_ROOT_SIGNATURE_VERSION_1_1)
          memcpy(&rp.descriptor_flags,
                 data + src.parameter_offset + sizeof(DXRootDescriptor10),
                 sizeof(rp.descriptor_flags));
        break;
      }
      case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: {
        if (!range_contains(size, src.parameter_offset,
                            sizeof(DXDescriptorTable)))
          return false;
        auto table = reinterpret_cast<const DXDescriptorTable *>(
            data + src.parameter_offset);
        if (table->num_ranges > 256)
          return false;

        size_t range_size = header->version == D3D_ROOT_SIGNATURE_VERSION_1_0
                                ? sizeof(DXDescriptorRange10)
                                : sizeof(DXDescriptorRange11);
        const uint8_t *ranges_base = data + src.parameter_offset + sizeof(*table);
        uint32_t ranges_offset = table->ranges_offset;
        if (range_contains(size, ranges_offset, table->num_ranges * range_size))
          ranges_base = data + ranges_offset;
        else if (!range_contains(size,
                                 (uint32_t)(src.parameter_offset + sizeof(*table)),
                                 table->num_ranges * range_size))
          return false;

        rp.descriptor_table_entries = table->num_ranges;
        uint32_t append_offset = 0;
        for (uint32_t r = 0; r < table->num_ranges; r++) {
          RootDescriptorRange range = {};
          if (header->version == D3D_ROOT_SIGNATURE_VERSION_1_0) {
            auto src_range = reinterpret_cast<const DXDescriptorRange10 *>(
                ranges_base + r * range_size);
            range.range_type =
                static_cast<D3D12_DESCRIPTOR_RANGE_TYPE>(src_range->range_type);
            range.num_descriptors = src_range->num_descriptors;
            range.base_register = src_range->base_shader_register;
            range.register_space = src_range->register_space;
            range.offset_in_table = src_range->offset_in_table;
          } else {
            auto src_range = reinterpret_cast<const DXDescriptorRange11 *>(
                ranges_base + r * range_size);
            range.range_type =
                static_cast<D3D12_DESCRIPTOR_RANGE_TYPE>(src_range->range_type);
            range.num_descriptors = src_range->num_descriptors;
            range.base_register = src_range->base_shader_register;
            range.register_space = src_range->register_space;
            range.offset_in_table = src_range->offset_in_table;
            range.flags = src_range->flags;
          }

          range.offset_in_table =
              range.offset_in_table == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
                  ? append_offset
                  : range.offset_in_table;
          rp.ranges.push_back(range);

          if (r == 0) {
            rp.range_type = range.range_type;
            rp.num_descriptors = range.num_descriptors;
            rp.register_space = range.register_space;
            rp.register_index = range.base_register;
          }

          if (range.num_descriptors != UINT32_MAX)
            append_offset = range.offset_in_table + range.num_descriptors;
          else
            append_offset = range.offset_in_table;
        }
        break;
      }
      default:
        RSTRACE("RootSignature RTS0 unknown parameter type=%u", src.parameter_type);
        return false;
      }

      m_parameters.push_back(rp);
    }

    auto static_samplers = reinterpret_cast<const DXStaticSampler *>(
        data + header->static_sampler_offset);
    for (uint32_t i = 0; i < header->num_static_samplers; i++) {
      D3D12_STATIC_SAMPLER_DESC desc = {};
      desc.Filter = static_cast<D3D12_FILTER>(static_samplers[i].filter);
      desc.AddressU =
          static_cast<D3D12_TEXTURE_ADDRESS_MODE>(static_samplers[i].address_u);
      desc.AddressV =
          static_cast<D3D12_TEXTURE_ADDRESS_MODE>(static_samplers[i].address_v);
      desc.AddressW =
          static_cast<D3D12_TEXTURE_ADDRESS_MODE>(static_samplers[i].address_w);
      desc.MipLODBias = static_samplers[i].mip_lod_bias;
      desc.MaxAnisotropy = static_samplers[i].max_anisotropy;
      desc.ComparisonFunc =
          static_cast<D3D12_COMPARISON_FUNC>(static_samplers[i].comparison_func);
      desc.BorderColor = static_cast<D3D12_STATIC_BORDER_COLOR>(
          static_samplers[i].border_color);
      desc.MinLOD = static_samplers[i].min_lod;
      desc.MaxLOD = static_samplers[i].max_lod;
      desc.ShaderRegister = static_samplers[i].shader_register;
      desc.RegisterSpace = static_samplers[i].register_space;
      desc.ShaderVisibility =
          static_cast<D3D12_SHADER_VISIBILITY>(static_samplers[i].shader_visibility);
      m_static_samplers.push_back(
          make_static_sampler(m_device->GetMTLDevice(), desc));
    }

    RSTRACE("RootSignature parsed RTS0 version=%u params=%u samplers=%u flags=0x%x",
            header->version, header->num_parameters,
            header->num_static_samplers, header->flags);
    return true;
  };

  auto bytes = static_cast<const uint8_t *>(blob);
  if (blob_size >= sizeof(DXContainerHeader) &&
      memcmp(bytes, "DXBC", 4) == 0) {
    auto container = reinterpret_cast<const DXContainerHeader *>(bytes);
    if (container->part_count <= 256 &&
        container->file_size <= blob_size &&
        range_contains(blob_size, sizeof(DXContainerHeader),
                       container->part_count * sizeof(uint32_t))) {
      auto part_offsets = reinterpret_cast<const uint32_t *>(
          bytes + sizeof(DXContainerHeader));
      for (uint32_t i = 0; i < container->part_count; i++) {
        uint32_t offset = part_offsets[i];
        if (!range_contains(blob_size, offset, sizeof(DXContainerPartHeader)))
          continue;
        auto part = reinterpret_cast<const DXContainerPartHeader *>(
            bytes + offset);
        if (memcmp(part->name, "RTS0", 4) != 0)
          continue;
        if (!range_contains(blob_size, offset + sizeof(*part), part->size))
          continue;
        if (parse_rts0(bytes + offset + sizeof(*part), part->size))
          return;
      }
    }
  }

  if (parse_rts0(bytes, blob_size))
    return;

  auto header = static_cast<const RSHeader *>(blob);
  size_t min_size = sizeof(RSHeader) +
                    header->num_parameters * sizeof(RSParameter);
  if (header->num_parameters > 64 || header->num_static_samplers > 64 ||
      min_size > blob_size) {
    RSTRACE("RootSignature parse failed: unknown blob size=%zu magic=0x%08x",
            blob_size, blob_size >= 4 ? *(const uint32_t *)blob : 0);
    return;
  }

  m_parameters.clear();
  m_static_samplers.clear();
  m_num_static_samplers = header->num_static_samplers;
  m_flags = static_cast<D3D12_ROOT_SIGNATURE_FLAGS>(header->flags);

  auto params = reinterpret_cast<const uint8_t *>(blob) + sizeof(RSHeader);
  auto end = reinterpret_cast<const uint8_t *>(blob) + blob_size;
  for (uint32_t i = 0; i < header->num_parameters; i++) {
    if (params + sizeof(RSParameter) > end)
      return;
    auto p = reinterpret_cast<const RSParameter *>(params);
    RootParameter rp = {};
    rp.type = static_cast<D3D12_ROOT_PARAMETER_TYPE>(p->type);
    rp.shader_visibility = p->visibility;

    if (p->type == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS) {
      rp.register_space = p->constants.register_space;
      rp.register_index = p->constants.register_index;
      rp.num_32bit_values = p->constants.num_32bit_values;
      rp.num_descriptors = p->constants.num_32bit_values;
    } else if (p->type == D3D12_ROOT_PARAMETER_TYPE_CBV ||
               p->type == D3D12_ROOT_PARAMETER_TYPE_SRV ||
               p->type == D3D12_ROOT_PARAMETER_TYPE_UAV) {
      rp.register_space = p->descriptor.register_space;
      rp.register_index = p->descriptor.register_index;
    } else if (p->type == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
      if (params + sizeof(RSParameter) +
              p->table.num_ranges * sizeof(RSDescriptorRange) >
          end)
        return;
      auto ranges = reinterpret_cast<const RSDescriptorRange *>(
          params + sizeof(RSParameter));
      rp.descriptor_table_entries = p->table.num_ranges;
      if (p->table.num_ranges > 0) {
        rp.range_type =
            static_cast<D3D12_DESCRIPTOR_RANGE_TYPE>(ranges[0].range_type);
        rp.num_descriptors = ranges[0].num_descriptors;
        rp.register_space = ranges[0].register_space;
        rp.register_index = ranges[0].base_register;
      }
      uint32_t append_offset = 0;
      for (uint32_t r = 0; r < p->table.num_ranges; r++) {
        RootDescriptorRange range = {};
        range.range_type =
            static_cast<D3D12_DESCRIPTOR_RANGE_TYPE>(ranges[r].range_type);
        range.num_descriptors = ranges[r].num_descriptors;
        range.base_register = ranges[r].base_register;
        range.register_space = ranges[r].register_space;
        range.offset_in_table =
            ranges[r].offset_in_table == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
                ? append_offset
                : ranges[r].offset_in_table;
        rp.ranges.push_back(range);

        if (range.num_descriptors != UINT32_MAX)
          append_offset = range.offset_in_table + range.num_descriptors;
        else
          append_offset = range.offset_in_table;
      }
      params += p->table.num_ranges * sizeof(RSDescriptorRange);
    }
    m_parameters.push_back(rp);
    params += sizeof(RSParameter);
  }

  for (uint32_t i = 0; i < header->num_static_samplers; i++) {
    if (params + sizeof(RSStaticSampler) > end)
      return;
    auto sampler = reinterpret_cast<const RSStaticSampler *>(params);
    D3D12_STATIC_SAMPLER_DESC desc = {};
    desc.Filter = static_cast<D3D12_FILTER>(sampler->filter);
    desc.AddressU =
        static_cast<D3D12_TEXTURE_ADDRESS_MODE>(sampler->address_u);
    desc.AddressV =
        static_cast<D3D12_TEXTURE_ADDRESS_MODE>(sampler->address_v);
    desc.AddressW =
        static_cast<D3D12_TEXTURE_ADDRESS_MODE>(sampler->address_w);
    desc.MipLODBias = sampler->mip_lod_bias;
    desc.MaxAnisotropy = sampler->max_anisotropy;
    desc.ComparisonFunc =
        static_cast<D3D12_COMPARISON_FUNC>(sampler->comparison_func);
    desc.BorderColor =
        static_cast<D3D12_STATIC_BORDER_COLOR>(sampler->border_color);
    desc.MinLOD = sampler->min_lod;
    desc.MaxLOD = sampler->max_lod;
    desc.ShaderRegister = sampler->register_index;
    desc.RegisterSpace = sampler->register_space;
    desc.ShaderVisibility =
        static_cast<D3D12_SHADER_VISIBILITY>(sampler->shader_visibility);
    m_static_samplers.push_back(
        make_static_sampler(m_device->GetMTLDevice(), desc));
    params += sizeof(RSStaticSampler);
  }
}

bool MTLD3D12RootSignature::FindDescriptorTableRange(
    D3D12_DESCRIPTOR_RANGE_TYPE range_type, uint32_t shader_register,
    uint32_t *root_parameter_index, uint32_t *descriptor_offset) const {
  return FindDescriptorTableRange(range_type, shader_register, 0,
                                  root_parameter_index, descriptor_offset);
}

bool MTLD3D12RootSignature::FindDescriptorTableRange(
    D3D12_DESCRIPTOR_RANGE_TYPE range_type, uint32_t shader_register,
    uint32_t register_space, uint32_t *root_parameter_index,
    uint32_t *descriptor_offset) const {
  return FindDescriptorTableRangeForVisibility(
      range_type, shader_register, register_space, D3D12_SHADER_VISIBILITY_ALL,
      root_parameter_index, descriptor_offset);
}

bool MTLD3D12RootSignature::FindDescriptorTableRangeForVisibility(
    D3D12_DESCRIPTOR_RANGE_TYPE range_type, uint32_t shader_register,
    D3D12_SHADER_VISIBILITY shader_visibility, uint32_t *root_parameter_index,
    uint32_t *descriptor_offset) const {
  return FindDescriptorTableRangeForVisibility(
      range_type, shader_register, 0, shader_visibility, root_parameter_index,
      descriptor_offset);
}

bool MTLD3D12RootSignature::FindDescriptorTableRangeForVisibility(
    D3D12_DESCRIPTOR_RANGE_TYPE range_type, uint32_t shader_register,
    uint32_t register_space, D3D12_SHADER_VISIBILITY shader_visibility,
    uint32_t *root_parameter_index, uint32_t *descriptor_offset) const {
  if (m_core_binding_plan_valid) {
    /* Phase 5 consumption seam: live PE binding now consults the persisted
     * M12Core binding plan first, while keeping the legacy PE scan below as a
     * fallback if the native plan was unavailable at root-signature creation.
     * This does not add per-draw unixcalls; the plan is local POD state.
     */
    for (uint32_t visibility_pass = 0; visibility_pass < 2; visibility_pass++) {
      for (uint32_t p = 0; p < m_core_binding_parameters.size(); p++) {
        const auto &param = m_core_binding_parameters[p];
        if (param.type != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
          continue;
        if (visibility_pass == 0 && param.shader_visibility != shader_visibility)
          continue;
        if (visibility_pass == 1 &&
            param.shader_visibility != D3D12_SHADER_VISIBILITY_ALL)
          continue;
        const uint32_t range_end = param.range_start + param.range_count;
        if (range_end < param.range_start || range_end > m_core_binding_ranges.size())
          continue;
        for (uint32_t r = param.range_start; r < range_end; r++) {
          const auto &range = m_core_binding_ranges[r];
          if (range.range_type != range_type ||
              range.register_space != register_space ||
              shader_register < range.base_register)
            continue;
          const uint32_t relative = shader_register - range.base_register;
          if (range.num_descriptors != UINT32_MAX &&
              relative >= range.num_descriptors)
            continue;
          if (root_parameter_index)
            *root_parameter_index = p;
          if (descriptor_offset)
            *descriptor_offset = range.offset_in_table + relative;
          return true;
        }
      }
    }
  }

  return FindDescriptorTableRangeForVisibilityPELocal(
      range_type, shader_register, register_space, shader_visibility,
      root_parameter_index, descriptor_offset);
}

bool MTLD3D12RootSignature::FindDescriptorTableRangeForVisibilityPELocal(
    D3D12_DESCRIPTOR_RANGE_TYPE range_type, uint32_t shader_register,
    uint32_t register_space, D3D12_SHADER_VISIBILITY shader_visibility,
    uint32_t *root_parameter_index, uint32_t *descriptor_offset) const {
  for (uint32_t visibility_pass = 0; visibility_pass < 2; visibility_pass++) {
    for (uint32_t p = 0; p < m_parameters.size(); p++) {
      const auto &param = m_parameters[p];
      if (param.type != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
        continue;
      if (visibility_pass == 0 &&
          param.shader_visibility != shader_visibility)
        continue;
      if (visibility_pass == 1 &&
          param.shader_visibility != D3D12_SHADER_VISIBILITY_ALL)
        continue;

      for (const auto &range : param.ranges) {
        if (range.range_type != range_type)
          continue;
        if (range.register_space != register_space)
          continue;
        if (shader_register < range.base_register)
          continue;
        uint32_t relative = shader_register - range.base_register;
        if (range.num_descriptors != UINT32_MAX &&
            relative >= range.num_descriptors)
          continue;

        if (root_parameter_index)
          *root_parameter_index = p;
        if (descriptor_offset)
          *descriptor_offset = range.offset_in_table + relative;
        return true;
      }
    }
  }

  return false;
}

const RootStaticSampler *MTLD3D12RootSignature::FindStaticSampler(
    uint32_t shader_register, uint32_t register_space,
    D3D12_SHADER_VISIBILITY shader_visibility) const {
  if (m_core_binding_plan_valid) {
    for (uint32_t pass = 0; pass < 2; pass++) {
      for (uint32_t i = 0; i < m_core_static_samplers.size(); i++) {
        const auto &sampler = m_core_static_samplers[i];
        if (sampler.shader_register != shader_register ||
            sampler.register_space != register_space)
          continue;
        if (pass == 0 && sampler.shader_visibility != shader_visibility)
          continue;
        if (pass == 1 &&
            sampler.shader_visibility != D3D12_SHADER_VISIBILITY_ALL)
          continue;
        if (i < m_static_samplers.size())
          return &m_static_samplers[i];
        return nullptr;
      }
    }
  }
  return FindStaticSamplerPELocal(shader_register, register_space, shader_visibility);
}

const RootStaticSampler *MTLD3D12RootSignature::FindStaticSamplerPELocal(
    uint32_t shader_register, uint32_t register_space,
    D3D12_SHADER_VISIBILITY shader_visibility) const {
  for (uint32_t pass = 0; pass < 2; pass++) {
    for (const auto &sampler : m_static_samplers) {
      if (sampler.shader_register != shader_register ||
          sampler.register_space != register_space)
        continue;
      if (pass == 0 && sampler.shader_visibility != shader_visibility)
        continue;
      if (pass == 1 &&
          sampler.shader_visibility != D3D12_SHADER_VISIBILITY_ALL)
        continue;
      return &sampler;
    }
  }
  return nullptr;
}

bool MTLD3D12RootSignature::FindRootDescriptorParameter(
    D3D12_ROOT_PARAMETER_TYPE type, uint32_t shader_register,
    uint32_t register_space, D3D12_SHADER_VISIBILITY shader_visibility,
    uint32_t *root_parameter_index, uint32_t max_root_parameters) const {
  if (!root_parameter_index)
    return false;

  /* Phase 5 PE-local lookup seam: command binding can ask the root-signature
   * object for root descriptor/root constant mappings instead of open-coding
   * parameter scans.  Prefer the persisted M12Core plan when available, but
   * keep the legacy scan as fallback and preserve the caller's root-slot bound.
   */
  if (m_core_binding_plan_valid) {
    const uint32_t limit = std::min<uint32_t>(
        static_cast<uint32_t>(m_core_binding_parameters.size()),
        max_root_parameters);
    for (uint32_t pass = 0; pass < 2; pass++) {
      for (uint32_t p = 0; p < limit; p++) {
        const auto &param = m_core_binding_parameters[p];
        if (param.type != type || param.register_index != shader_register ||
            param.register_space != register_space)
          continue;
        if (pass == 0 && param.shader_visibility != shader_visibility)
          continue;
        if (pass == 1 && param.shader_visibility != D3D12_SHADER_VISIBILITY_ALL)
          continue;
        *root_parameter_index = p;
        return true;
      }
    }
  }

  const uint32_t limit = std::min<uint32_t>(static_cast<uint32_t>(m_parameters.size()),
                                            max_root_parameters);
  for (uint32_t pass = 0; pass < 2; pass++) {
    for (uint32_t p = 0; p < limit; p++) {
      const auto &param = m_parameters[p];
      if (param.type != type || param.register_index != shader_register ||
          param.register_space != register_space)
        continue;
      if (pass == 0 && param.shader_visibility != shader_visibility)
        continue;
      if (pass == 1 && param.shader_visibility != D3D12_SHADER_VISIBILITY_ALL)
        continue;
      *root_parameter_index = p;
      return true;
    }
  }
  return false;
}

bool MTLD3D12RootSignature::FindRootConstantsParameter(
    uint32_t shader_register, uint32_t register_space,
    D3D12_SHADER_VISIBILITY shader_visibility,
    uint32_t *root_parameter_index, uint32_t *num_32bit_values,
    uint32_t max_root_parameters) const {
  uint32_t root = UINT32_MAX;
  if (!FindRootDescriptorParameter(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
                                   shader_register, register_space,
                                   shader_visibility, &root,
                                   max_root_parameters))
    return false;
  if (root_parameter_index)
    *root_parameter_index = root;
  if (num_32bit_values) {
    if (m_core_binding_plan_valid && root < m_core_binding_parameters.size())
      *num_32bit_values = m_core_binding_parameters[root].num_32bit_values;
    else
      *num_32bit_values = m_parameters[root].num_32bit_values;
  }
  return true;
}

HRESULT STDMETHODCALLTYPE
MTLD3D12RootSignature::QueryInterface(REFIID riid, void **ppvObject) {
  if (!ppvObject)
    return E_POINTER;
  *ppvObject = nullptr;

  if (riid == IID_IUnknown || riid == IID_ID3D12Object ||
      riid == IID_ID3D12DeviceChild || riid == IID_ID3D12RootSignature) {
    *ppvObject = ref(this);
    return S_OK;
  }
  return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE
MTLD3D12RootSignature::AddRef() { return ++m_refCount; }

ULONG STDMETHODCALLTYPE MTLD3D12RootSignature::Release() {
  uint32_t rc = --m_refCount;
  if (!rc)
    delete this;
  return rc;
}

HRESULT STDMETHODCALLTYPE
MTLD3D12RootSignature::GetPrivateData(REFGUID guid, UINT *data_size,
                                      void *data) {
  return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE
MTLD3D12RootSignature::SetPrivateData(REFGUID guid, UINT data_size,
                                      const void *data) {
  return S_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D12RootSignature::SetPrivateDataInterface(REFGUID guid,
                                               const IUnknown *data) {
  return S_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D12RootSignature::SetName(LPCWSTR name) {
  return S_OK;
}

HRESULT STDMETHODCALLTYPE
MTLD3D12RootSignature::GetDevice(REFIID riid, void **device) {
  return m_device->QueryInterface(riid, device);
}

} // namespace dxmt
