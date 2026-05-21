/// @file D3D12CommandQueue.cpp
/// @brief D3D12 command queue stub — passthrough implementation placeholder.
///
/// Reserved for ID3D12CommandQueue. Maps D3D12 queue submissions to Metal
/// MTLCommandQueue scheduling.

#include <metalsharp/D3D12Device.h>

namespace metalsharp {

MetalDevice* metalDeviceForD3D12CommandQueue(IUnknown* queue) {
    if (!queue)
        return nullptr;

    ID3D12CommandQueue* d3d12Queue = nullptr;
    HRESULT hr = queue->QueryInterface(IID_ID3D12CommandQueue, (void**)&d3d12Queue);
    if (FAILED(hr) || !d3d12Queue)
        return nullptr;

    auto* queueImpl = static_cast<D3D12CommandQueueImpl*>(d3d12Queue);
    MetalDevice* metalDevice = &queueImpl->metalDevice;
    d3d12Queue->Release();
    return metalDevice;
}

} // namespace metalsharp
