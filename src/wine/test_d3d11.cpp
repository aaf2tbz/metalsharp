#include <windows.h>
#include <d3d11.h>
#include <stdio.h>

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdline, int show) {
    printf("MetalSharp D3D11 Test\n");

    ID3D11Device* device = NULL;
    ID3D11DeviceContext* context = NULL;
    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr;

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0,
                           D3D11_SDK_VERSION, &device, &featureLevel, &context);

    printf("D3D11CreateDevice returned: 0x%08X\n", (unsigned)hr);
    printf("Device: %p\n", device);
    printf("Context: %p\n", context);
    printf("Feature Level: 0x%X\n", featureLevel);

    if (SUCCEEDED(hr) && device) {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = 64;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        ID3D11Buffer* buffer = NULL;
        hr = device->CreateBuffer(&desc, NULL, &buffer);
        printf("CreateBuffer returned: 0x%08X, buffer=%p\n", (unsigned)hr, buffer);

        device->Release();
        if (context) context->Release();
    }

    printf("Test complete!\n");
    return 0;
}
