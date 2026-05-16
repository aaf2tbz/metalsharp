#include <cstdio>
#include <d3d/D3D12.h>

extern "C" HRESULT D3D12CreateDevice(void* pAdapter, UINT MinimumFeatureLevel, const GUID& riid, void** ppDevice);

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                                                               \
    do {                                                                                                               \
        if (cond) {                                                                                                    \
            std::printf("  [OK] %s\n", msg);                                                                           \
            ++g_pass;                                                                                                  \
        } else {                                                                                                       \
            std::printf("  [FAIL] %s\n", msg);                                                                         \
            ++g_fail;                                                                                                  \
        }                                                                                                              \
    } while (0)

int main() {
    std::printf("=== D3D12 C Entrypoint Tests ===\n\n");

    HRESULT hr = D3D12CreateDevice(nullptr, 0, IID_ID3D12Device, nullptr);
    CHECK(hr == E_POINTER, "D3D12CreateDevice rejects null output pointer");

    ID3D12Device* device = nullptr;
    hr = D3D12CreateDevice(nullptr, 0, IID_ID3D12Device, reinterpret_cast<void**>(&device));
    CHECK(SUCCEEDED(hr) && device != nullptr, "D3D12CreateDevice returns ID3D12Device");

    if (device) {
        void* queried = nullptr;
        hr = device->QueryInterface(IID_ID3D12Device, &queried);
        CHECK(SUCCEEDED(hr) && queried == device, "ID3D12Device QueryInterface round trip");
        if (queried)
            static_cast<ID3D12Device*>(queried)->Release();

        ID3D12CommandQueue* queue = nullptr;
        hr = device->CreateCommandQueue(nullptr, IID_ID3D12CommandQueue, reinterpret_cast<void**>(&queue));
        CHECK(SUCCEEDED(hr) && queue != nullptr, "C entrypoint device creates command queue");
        if (queue)
            queue->Release();

        ID3D12CommandAllocator* allocator = nullptr;
        hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_ID3D12CommandAllocator,
                                            reinterpret_cast<void**>(&allocator));
        CHECK(SUCCEEDED(hr) && allocator != nullptr, "C entrypoint device creates command allocator");
        if (allocator)
            allocator->Release();

        device->Release();
    }

    std::printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
