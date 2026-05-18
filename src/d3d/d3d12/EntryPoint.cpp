/// @file EntryPoint.cpp
/// @brief D3D12 DLL entry points for device creation and root signature serialization.
///
/// Implements D3D12CreateDevice, D3D12SerializeRootSignature,
/// D3D12SerializeVersionedRootSignature, D3D12GetDebugInterface, and
/// D3D12EnableExperimentalFeatures.

#include <metalsharp/D3D12Device.h>
#include <cstdlib>
#include <cstring>

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
    out[0] = 0x44;
    out[1] = 0x58;
    out[2] = 0x42;
    out[3] = 0x43;
    uint32_t version = 0x00000001;
    memcpy(out + 4, &version, 4);
    uint32_t totalSize = 32 + numParameters * 20 + numStaticSamplers * 32;
    memcpy(out + 8, &totalSize, 4);
    memcpy(out + 12, &numParameters, 4);
    memcpy(out + 16, &numStaticSamplers, 4);
    memcpy(out + 20, &flags, 4);
}

HRESULT D3D12SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC* pRootSignatureDesc, UINT Version,
                                     void** ppBlob, void** ppErrorBlob) {
    if (!pRootSignatureDesc || !ppBlob)
        return E_INVALIDARG;
    if (Version > 0x1)
        return E_INVALIDARG;

    uint32_t numParams = pRootSignatureDesc->NumParameters;
    uint32_t numSamplers = pRootSignatureDesc->NumStaticSamplers;
    uint32_t blobSize = 32 + numParams * 20 + numSamplers * 32;

    uint8_t* blob = (uint8_t*)malloc(blobSize);
    if (!blob)
        return E_OUTOFMEMORY;
    memset(blob, 0, blobSize);

    writeRootSignatureHeader(blob, numParams, numSamplers, pRootSignatureDesc->Flags);

    uint32_t offset = 32;
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
        memcpy(blob + offset, paramData, 20);
        offset += 20;
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
        memcpy(blob + offset, samplerData, 32);
        offset += 32;
    }

    *ppBlob = blob;
    if (ppErrorBlob)
        *ppErrorBlob = nullptr;
    return S_OK;
}

HRESULT D3D12SerializeVersionedRootSignature(const void* pVersionedRootSignatureDesc, UINT Version,
                                              void** ppBlob, void** ppErrorBlob) {
    if (!pVersionedRootSignatureDesc || !ppBlob)
        return E_INVALIDARG;

    const uint8_t* desc = (const uint8_t*)pVersionedRootSignatureDesc;
    uint32_t descVersion;
    memcpy(&descVersion, desc, 4);

    if (descVersion == 0x1) {
        const D3D12_ROOT_SIGNATURE_DESC* pDesc = (const D3D12_ROOT_SIGNATURE_DESC*)pVersionedRootSignatureDesc;
        return D3D12SerializeRootSignature(pDesc, Version, ppBlob, ppErrorBlob);
    }

    if (descVersion == 0x2) {
        uint32_t numParameters;
        memcpy(&numParameters, desc + 4, 4);
        uint32_t numStaticSamplers;
        memcpy(&numStaticSamplers, desc + 8, 4);
        uint32_t flags;
        memcpy(&flags, desc + 12, 4);

        D3D12_ROOT_SIGNATURE_DESC fallbackDesc = {};
        fallbackDesc.NumParameters = numParameters;
        fallbackDesc.pParameters = nullptr;
        fallbackDesc.NumStaticSamplers = numStaticSamplers;
        fallbackDesc.pStaticSamplers = nullptr;
        fallbackDesc.Flags = flags;
        return D3D12SerializeRootSignature(&fallbackDesc, Version, ppBlob, ppErrorBlob);
    }

    return E_INVALIDARG;
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
