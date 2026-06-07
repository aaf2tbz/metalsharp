#include <windows.h>
#include <stdio.h>

typedef long HRESULT;
typedef unsigned int UINT;
HRESULT WINAPI D3D12CreateDevice(void* adapter, UINT min_feature_level, void* riid, void** device);

int main() {
    void* device = NULL;
    HRESULT hr = D3D12CreateDevice(NULL, 0xB000, NULL, &device);
    printf("D3D12CreateDevice returned 0x%08X device=%p\n", (unsigned)hr, device);
    return 0;
}
