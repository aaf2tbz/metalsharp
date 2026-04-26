#include <d3d/D3D11.h>
#include <cstdio>
#include <cstring>

extern "C" HRESULT D3D11CreateDevice(
    void*, UINT, HMODULE, UINT, const void*, UINT, UINT,
    ID3D11Device**, void*, ID3D11DeviceContext**);

int main() {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;

    HRESULT hr = D3D11CreateDevice(
        nullptr, 0, nullptr, 0,
        nullptr, 0, 7,
        &device, nullptr, &context);

    if (FAILED(hr)) {
        fprintf(stderr, "FAIL: D3D11CreateDevice returned 0x%08x\n", hr);
        return 1;
    }

    if (!device) {
        fprintf(stderr, "FAIL: device is null\n");
        return 1;
    }

    if (!context) {
        fprintf(stderr, "FAIL: context is null\n");
        device->Release();
        return 1;
    }

    D3D11_BUFFER_DESC bufDesc{};
    bufDesc.ByteWidth = 256;
    bufDesc.BindFlags = 0x1;

    ID3D11Buffer* buffer = nullptr;
    hr = device->CreateBuffer(&bufDesc, nullptr, &buffer);
    if (FAILED(hr)) {
        fprintf(stderr, "FAIL: CreateBuffer returned 0x%08x\n", hr);
    } else {
        buffer->Release();
    }

    device->Release();
    context->Release();

    printf("PASS: D3D11 device creation\n");
    return 0;
}
