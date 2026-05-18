/// @file EntryPoint.cpp
/// @brief D3D12 DLL entry points for device creation and root signature serialization.
///
/// Implements D3D12CreateDevice, D3D12SerializeRootSignature,
/// D3D12SerializeVersionedRootSignature, D3D12GetDebugInterface, and
/// D3D12EnableExperimentalFeatures.

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <metalsharp/D3D12Device.h>
#include <new>
#include <vector>

namespace {

class D3DBlobImpl final : public ID3DBlob {
  public:
    explicit D3DBlobImpl(std::vector<uint8_t>&& data) : m_data(std::move(data)) {}

    HRESULT QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject)
            return E_POINTER;
        if (riid == IID_IUnknown) {
            AddRef();
            *ppvObject = this;
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }
    ULONG AddRef() override { return ++m_refCount; }
    ULONG Release() override {
        ULONG count = --m_refCount;
        if (count == 0)
            delete this;
        return count;
    }
    void* GetBufferPointer() override { return m_data.data(); }
    SIZE_T GetBufferSize() override { return m_data.size(); }

  private:
    std::atomic<ULONG> m_refCount{1};
    std::vector<uint8_t> m_data;
};

} // namespace

extern "C" {

HRESULT D3D12CreateDevice(void* pAdapter, UINT MinimumFeatureLevel, const GUID& riid, void** ppDevice) {
    if (!ppDevice)
        return E_POINTER;
    metalsharp::D3D12DeviceImpl* device = nullptr;
    HRESULT hr = metalsharp::D3D12DeviceImpl::create(&device);
    if (SUCCEEDED(hr)) {
        *ppDevice = device;
    }
    return hr;
}

static void writeRootSignatureHeader(uint8_t* out, uint32_t numParameters, uint32_t numStaticSamplers, uint32_t flags) {
    uint32_t version = 0x00000001;
    memcpy(out, &version, 4);
    memcpy(out + 4, &numParameters, 4);
    memcpy(out + 8, &numStaticSamplers, 4);
    memcpy(out + 12, &flags, 4);
}

static uint32_t rootParameterRecordSize(UINT type) {
    return type == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS ? 20 : 16;
}

static std::vector<uint8_t> wrapRootSignatureInDXBC(std::vector<uint8_t>&& rootSignature) {
    constexpr uint32_t dxbcMagic = 0x43425844;
    constexpr uint32_t rts0Magic = 0x30535452;
    constexpr uint32_t headerSize = 32;
    constexpr uint32_t chunkCount = 1;
    constexpr uint32_t chunkTableSize = chunkCount * 4;
    constexpr uint32_t chunkOffset = headerSize + chunkTableSize;
    uint32_t chunkSize = static_cast<uint32_t>(rootSignature.size());
    uint32_t totalSize = chunkOffset + 8 + chunkSize;

    std::vector<uint8_t> container(totalSize);
    memcpy(container.data(), &dxbcMagic, 4);
    memcpy(container.data() + 24, &totalSize, 4);
    memcpy(container.data() + 28, &chunkCount, 4);
    memcpy(container.data() + 32, &chunkOffset, 4);
    memcpy(container.data() + chunkOffset, &rts0Magic, 4);
    memcpy(container.data() + chunkOffset + 4, &chunkSize, 4);
    memcpy(container.data() + chunkOffset + 8, rootSignature.data(), rootSignature.size());
    return container;
}

HRESULT D3D12SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC* pRootSignatureDesc, UINT Version, void** ppBlob,
                                    void** ppErrorBlob) {
    if (!pRootSignatureDesc || !ppBlob)
        return E_INVALIDARG;
    *ppBlob = nullptr;
    if (ppErrorBlob)
        *ppErrorBlob = nullptr;
    if (Version > 0x1)
        return E_INVALIDARG;
    if (pRootSignatureDesc->NumParameters > 0 && !pRootSignatureDesc->pParameters)
        return E_INVALIDARG;
    if (pRootSignatureDesc->NumStaticSamplers > 0 && !pRootSignatureDesc->pStaticSamplers)
        return E_INVALIDARG;

    uint32_t numParams = pRootSignatureDesc->NumParameters;
    uint32_t numSamplers = pRootSignatureDesc->NumStaticSamplers;
    uint32_t descriptorRangeCount = 0;
    uint32_t parameterBytes = 0;
    for (uint32_t i = 0; i < numParams; i++) {
        const D3D12_ROOT_PARAMETER& param = pRootSignatureDesc->pParameters[i];
        parameterBytes += rootParameterRecordSize(param.ParameterType);
        if (param.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
            if (param.DescriptorTable.NumDescriptorRanges > 0 && !param.DescriptorTable.pDescriptorRanges)
                return E_INVALIDARG;
            descriptorRangeCount += param.DescriptorTable.NumDescriptorRanges;
        }
    }

    uint32_t rootBlobSize = 16 + parameterBytes + descriptorRangeCount * 20 + numSamplers * 32;

    std::vector<uint8_t> rootBlob(rootBlobSize);

    writeRootSignatureHeader(rootBlob.data(), numParams, numSamplers, pRootSignatureDesc->Flags);

    uint32_t offset = 16;
    for (uint32_t i = 0; i < numParams; i++) {
        const D3D12_ROOT_PARAMETER& param = pRootSignatureDesc->pParameters[i];
        uint32_t paramData[5] = {};
        paramData[0] = param.ParameterType;
        paramData[1] = param.ShaderVisibility;
        if (param.ParameterType == 0) {
            paramData[2] = param.DescriptorTable.NumDescriptorRanges;
        } else if (param.ParameterType == 1) {
            paramData[2] = param.Constants.ShaderRegister;
            paramData[3] = param.Constants.RegisterSpace;
            paramData[4] = param.Constants.Num32BitValues;
        } else {
            paramData[2] = param.Descriptor.ShaderRegister;
            paramData[3] = param.Descriptor.RegisterSpace;
        }
        uint32_t paramSize = rootParameterRecordSize(param.ParameterType);
        memcpy(rootBlob.data() + offset, paramData, paramSize);
        offset += paramSize;

        if (param.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
            for (uint32_t rangeIndex = 0; rangeIndex < param.DescriptorTable.NumDescriptorRanges; rangeIndex++) {
                const D3D12_DESCRIPTOR_RANGE& range = param.DescriptorTable.pDescriptorRanges[rangeIndex];
                uint32_t rangeData[5] = {range.RangeType, range.NumDescriptors, range.BaseShaderRegister,
                                         range.RegisterSpace, range.OffsetInDescriptorsFromTableStart};
                memcpy(rootBlob.data() + offset, rangeData, 20);
                offset += 20;
            }
        }
    }

    for (uint32_t i = 0; i < numSamplers; i++) {
        const D3D12_STATIC_SAMPLER_DESC& sampler = pRootSignatureDesc->pStaticSamplers[i];
        uint32_t samplerData[8] = {};
        samplerData[0] = sampler.Filter;
        samplerData[1] = sampler.AddressU | (sampler.AddressV << 4) | (sampler.AddressW << 8);
        float mipBias = sampler.MipLODBias;
        memcpy(&samplerData[2], &mipBias, 4);
        samplerData[3] = sampler.MaxAnisotropy | (sampler.ComparisonFunc << 8);
        float minLod = sampler.MinLOD;
        memcpy(&samplerData[4], &minLod, 4);
        float maxLod = sampler.MaxLOD;
        memcpy(&samplerData[5], &maxLod, 4);
        samplerData[6] = sampler.ShaderRegister | (sampler.RegisterSpace << 16);
        samplerData[7] = sampler.ShaderVisibility;
        memcpy(rootBlob.data() + offset, samplerData, 32);
        offset += 32;
    }

    std::vector<uint8_t> blob = wrapRootSignatureInDXBC(std::move(rootBlob));
    auto* blobObject = new (std::nothrow) D3DBlobImpl(std::move(blob));
    if (!blobObject)
        return E_OUTOFMEMORY;
    *ppBlob = blobObject;
    return S_OK;
}

HRESULT D3D12SerializeVersionedRootSignature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* pVersionedRootSignatureDesc,
                                             void** ppBlob, void** ppErrorBlob) {
    if (!pVersionedRootSignatureDesc || !ppBlob)
        return E_INVALIDARG;
    if (pVersionedRootSignatureDesc->Version != D3D_ROOT_SIGNATURE_VERSION_1_0)
        return E_INVALIDARG;
    return D3D12SerializeRootSignature(&pVersionedRootSignatureDesc->Desc_1_0, D3D_ROOT_SIGNATURE_VERSION_1_0, ppBlob,
                                       ppErrorBlob);
}

HRESULT D3D12GetDebugInterface(const GUID& riid, void** ppvDebug) {
    if (!ppvDebug)
        return E_POINTER;
    *ppvDebug = nullptr;
    return E_NOINTERFACE;
}

HRESULT D3D12EnableExperimentalFeatures(UINT NumFeatures, const GUID* pFeatureGUIDs, void* pConfigurationStructs,
                                        UINT* pConfigurationStructSizes) {
    return S_OK;
}
}
