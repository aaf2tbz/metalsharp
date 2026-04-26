#include <metalsharp/D3D12Device.h>

extern "C" {

HRESULT D3D12CreateDevice(void* pAdapter, UINT MinimumFeatureLevel, const GUID& riid, void** ppDevice) {
    if (!ppDevice) return E_POINTER;
    metalsharp::D3D12DeviceImpl* device = nullptr;
    HRESULT hr = metalsharp::D3D12DeviceImpl::create(&device);
    if (SUCCEEDED(hr)) {
        *ppDevice = device;
    }
    return hr;
}

}
