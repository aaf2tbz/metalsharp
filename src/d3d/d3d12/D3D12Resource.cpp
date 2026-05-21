/// @file D3D12Resource.cpp
/// @brief D3D12 resource stub — passthrough implementation placeholder.
///
/// Reserved for ID3D12Resource. Maps D3D12 GPU resources (buffers, textures)
/// to Metal MTLBuffer and MTLTexture objects.

#include <metalsharp/D3D12Device.h>
#include <metalsharp/Platform.h>

namespace metalsharp {

HRESULT createD3D12SwapChainBackBuffer(void* nativeTexture, uint32_t width, uint32_t height, uint32_t format,
                                       void** ppResource) {
    if (!nativeTexture || !ppResource)
        return E_POINTER;

    *ppResource = nullptr;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    auto* backBuffer = new D3D12ResourceImpl(desc);
    backBuffer->metalTexture.reset(MetalTexture::wrapNative2D(nativeTexture, width, height, format, 1, 1));
    backBuffer->__setResourceState(D3D12_RESOURCE_STATE_PRESENT);
    if (!backBuffer->metalTexture) {
        delete backBuffer;
        return E_FAIL;
    }

    *ppResource = backBuffer;
    return S_OK;
}

} // namespace metalsharp
