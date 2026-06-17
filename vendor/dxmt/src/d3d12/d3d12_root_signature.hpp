#pragma once

#include "com/com_pointer.hpp"
#include "d3d12.h"
#include "Metal.hpp"
#include <atomic>
#include <cstddef>
#include <vector>

namespace dxmt {

class MTLD3D12Device;

struct RootDescriptorRange {
  D3D12_DESCRIPTOR_RANGE_TYPE range_type;
  uint32_t num_descriptors;
  uint32_t base_register;
  uint32_t register_space;
  uint32_t offset_in_table;
  uint32_t flags;
};

struct RootParameter {
  D3D12_ROOT_PARAMETER_TYPE type;
  uint32_t shader_visibility;
  uint32_t register_space;
  uint32_t register_index;
  uint32_t num_descriptors;
  uint32_t num_32bit_values;
  uint32_t descriptor_flags;
  D3D12_DESCRIPTOR_RANGE_TYPE range_type;
  uint32_t descriptor_table_entries;
  std::vector<RootDescriptorRange> ranges;
};

struct RootStaticSampler {
  uint32_t shader_register;
  uint32_t register_space;
  uint32_t shader_visibility;
  uint32_t filter;
  uint32_t address_u;
  uint32_t address_v;
  uint32_t address_w;
  uint32_t max_anisotropy;
  uint32_t comparison_func;
  uint32_t border_color;
  uint64_t min_lod_bits;
  uint64_t max_lod_bits;
  uint64_t sampler_gpu_id;
  uint64_t sampler_cube_gpu_id;
  uint64_t lod_bias_bits;
  WMT::Reference<WMT::SamplerState> sampler;
  WMT::Reference<WMT::SamplerState> sampler_cube;
};

class MTLD3D12RootSignature : public ID3D12RootSignature {
public:
  MTLD3D12RootSignature(MTLD3D12Device *device, const void *blob,
                        SIZE_T blob_size);
  ~MTLD3D12RootSignature();

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **ppvObject) override;
  ULONG STDMETHODCALLTYPE AddRef() override;
  ULONG STDMETHODCALLTYPE Release() override;

  HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT *data_size,
                                          void *data) override;
  HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT data_size,
                                          const void *data) override;
  HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(
      REFGUID guid, const IUnknown *data) override;
  HRESULT STDMETHODCALLTYPE SetName(LPCWSTR name) override;

  HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **device) override;

  const std::vector<RootParameter> &GetParameters() const {
    return m_parameters;
  }
  bool FindDescriptorTableRange(D3D12_DESCRIPTOR_RANGE_TYPE range_type,
                                uint32_t shader_register,
                                uint32_t *root_parameter_index,
                                uint32_t *descriptor_offset) const;
  bool FindDescriptorTableRange(D3D12_DESCRIPTOR_RANGE_TYPE range_type,
                                uint32_t shader_register,
                                uint32_t register_space,
                                uint32_t *root_parameter_index,
                                uint32_t *descriptor_offset) const;
  bool FindDescriptorTableRangeForVisibility(
      D3D12_DESCRIPTOR_RANGE_TYPE range_type, uint32_t shader_register,
      D3D12_SHADER_VISIBILITY shader_visibility,
      uint32_t *root_parameter_index, uint32_t *descriptor_offset) const;
  bool FindDescriptorTableRangeForVisibility(
      D3D12_DESCRIPTOR_RANGE_TYPE range_type, uint32_t shader_register,
      uint32_t register_space, D3D12_SHADER_VISIBILITY shader_visibility,
      uint32_t *root_parameter_index, uint32_t *descriptor_offset) const;
  const RootStaticSampler *FindStaticSampler(
      uint32_t shader_register, uint32_t register_space,
      D3D12_SHADER_VISIBILITY shader_visibility) const;
  uint32_t GetNumParameters() const { return m_parameters.size(); }
  uint32_t GetNumStaticSamplers() const { return m_num_static_samplers; }
  const std::vector<RootStaticSampler> &GetStaticSamplers() const { return m_static_samplers; }
  D3D12_ROOT_SIGNATURE_FLAGS GetFlags() const { return m_flags; }
  size_t GetBlobHash() const { return m_blob_hash; }
  bool HasM12CoreSummary() const { return m_core_summary_valid; }
  uint64_t GetM12CoreRootSignatureKey() const { return m_core_root_signature_key; }
  const M12CoreRootSignatureSummary &GetM12CoreSummary() const { return m_core_summary; }
  bool HasM12CoreBindingPlan() const { return m_core_binding_plan_valid; }
  const M12CoreRootBindingPlanSummary &GetM12CoreBindingPlanSummary() const { return m_core_binding_plan_summary; }

private:
  void Parse(const void *blob, SIZE_T blob_size);
  void SummarizeWithM12Core();
  void BuildM12CoreBindingPlanArrays();
  M12CoreRootBindingPlanDesc MakeM12CoreBindingPlanDesc() const;
  bool LookupM12CoreDescriptorRangeForVisibility(
      D3D12_DESCRIPTOR_RANGE_TYPE range_type, uint32_t shader_register,
      uint32_t register_space, D3D12_SHADER_VISIBILITY shader_visibility,
      M12CoreRootBindingLookupResult *out_result) const;
  bool LookupM12CoreStaticSampler(uint32_t shader_register,
                                  uint32_t register_space,
                                  D3D12_SHADER_VISIBILITY shader_visibility,
                                  M12CoreRootBindingLookupResult *out_result) const;
  void ValidateM12CoreBindingPlanLookups(uint32_t *out_lookup_checks,
                                         uint32_t *out_lookup_mismatches) const;

  MTLD3D12Device *m_device;
  std::vector<RootParameter> m_parameters;
  std::vector<RootStaticSampler> m_static_samplers;
  uint32_t m_num_static_samplers = 0;
  D3D12_ROOT_SIGNATURE_FLAGS m_flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  size_t m_blob_hash = 0;
  bool m_core_summary_valid = false;
  uint64_t m_core_root_signature_key = 0;
  M12CoreRootSignatureSummary m_core_summary = {};
  bool m_core_binding_plan_valid = false;
  M12CoreRootBindingPlanSummary m_core_binding_plan_summary = {};
  std::vector<M12CoreRootBindingParameter> m_core_binding_parameters;
  std::vector<M12CoreRootBindingRange> m_core_binding_ranges;
  std::vector<M12CoreStaticSamplerBinding> m_core_static_samplers;
  std::atomic<uint32_t> m_refCount = {1ul};
};

} // namespace dxmt
