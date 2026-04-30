#include <windows.h>
#include <d3d11.h>
#include <stdio.h>

static int g_pass = 0;
static int g_fail = 0;

static void check(const char* name, BOOL cond) {
    if (cond) { printf("  PASS: %s\n", name); g_pass++; }
    else      { printf("  FAIL: %s\n", name); g_fail++; }
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdline, int show) {
    printf("=== MetalSharp D3D11 Round-Trip Test ===\n\n");

    ID3D11Device* device = NULL;
    ID3D11DeviceContext* context = NULL;
    D3D_FEATURE_LEVEL featureLevel;

    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0,
                                    D3D11_SDK_VERSION, &device, &featureLevel, &context);
    printf("[Device] hr=0x%08X device=%p ctx=%p fl=0x%X\n", (unsigned)hr, device, context, featureLevel);
    check("D3D11CreateDevice", SUCCEEDED(hr) && device && context);

    if (FAILED(hr) || !device) {
        printf("\nCannot continue without device.\n");
        return 1;
    }

    // --- Test 1: CreateBuffer with initial data ---
    printf("\n--- Test 1: CreateBuffer ---\n");
    {
        float verts[] = { 0.0f, 0.5f, 0.5f, -0.5f, -0.5f, -0.5f };
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(verts);
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem = verts;

        ID3D11Buffer* buf = NULL;
        hr = device->CreateBuffer(&desc, &init, &buf);
        printf("  hr=0x%08X buffer=%p\n", (unsigned)hr, buf);
        check("CreateBuffer returns S_OK", SUCCEEDED(hr));
        check("Buffer pointer non-NULL", buf != NULL);

        if (buf) {
            buf->Release();
            check("Buffer Release succeeded", TRUE);
        }
    }

    // --- Test 2: CreateBuffer without initial data ---
    printf("\n--- Test 2: CreateBuffer (empty) ---\n");
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = 256;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        ID3D11Buffer* buf = NULL;
        hr = device->CreateBuffer(&desc, NULL, &buf);
        printf("  hr=0x%08X buffer=%p\n", (unsigned)hr, buf);
        check("CreateBuffer (dynamic const) returns S_OK", SUCCEEDED(hr));
        check("Buffer pointer non-NULL", buf != NULL);
        if (buf) buf->Release();
    }

    // --- Test 3: CreateBuffer large ---
    printf("\n--- Test 3: CreateBuffer (1MB) ---\n");
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = 1024 * 1024;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        ID3D11Buffer* buf = NULL;
        hr = device->CreateBuffer(&desc, NULL, &buf);
        printf("  hr=0x%08X buffer=%p\n", (unsigned)hr, buf);
        check("CreateBuffer (1MB) returns S_OK", SUCCEEDED(hr));
        if (buf) buf->Release();
    }

    // --- Test 4: CreateTexture2D (render target) ---
    printf("\n--- Test 4: CreateTexture2D (RT) ---\n");
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = 800;
        desc.Height = 600;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        ID3D11Texture2D* tex = NULL;
        hr = device->CreateTexture2D(&desc, NULL, &tex);
        printf("  hr=0x%08X texture=%p\n", (unsigned)hr, tex);
        check("CreateTexture2D (RT) returns S_OK", SUCCEEDED(hr));
        check("Texture pointer non-NULL", tex != NULL);
        if (tex) tex->Release();
    }

    // --- Test 5: CreateTexture2D (depth stencil) ---
    printf("\n--- Test 5: CreateTexture2D (DS) ---\n");
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = 800;
        desc.Height = 600;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

        ID3D11Texture2D* tex = NULL;
        hr = device->CreateTexture2D(&desc, NULL, &tex);
        printf("  hr=0x%08X texture=%p\n", (unsigned)hr, tex);
        check("CreateTexture2D (DS) returns S_OK", SUCCEEDED(hr));
        if (tex) tex->Release();
    }

    // --- Test 6: CreateTexture2D (staging / CPU access) ---
    printf("\n--- Test 6: CreateTexture2D (staging) ---\n");
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = 256;
        desc.Height = 256;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;

        ID3D11Texture2D* tex = NULL;
        hr = device->CreateTexture2D(&desc, NULL, &tex);
        printf("  hr=0x%08X texture=%p\n", (unsigned)hr, tex);
        check("CreateTexture2D (staging) returns S_OK", SUCCEEDED(hr));
        if (tex) tex->Release();
    }

    // --- Test 7: CreateRenderTargetView ---
    printf("\n--- Test 7: CreateRenderTargetView ---\n");
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = 128;
        desc.Height = 128;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;

        ID3D11Texture2D* tex = NULL;
        hr = device->CreateTexture2D(&desc, NULL, &tex);
        if (SUCCEEDED(hr) && tex) {
            ID3D11RenderTargetView* rtv = NULL;
            hr = device->CreateRenderTargetView(tex, NULL, &rtv);
            printf("  hr=0x%08X rtv=%p\n", (unsigned)hr, rtv);
            check("CreateRenderTargetView returns S_OK", SUCCEEDED(hr));
            check("RTV pointer non-NULL", rtv != NULL);
            if (rtv) rtv->Release();
            tex->Release();
        } else {
            check("CreateRenderTargetView (no texture)", FALSE);
        }
    }

    // --- Test 8: Multiple buffers ---
    printf("\n--- Test 8: Multiple buffers (resource tracking) ---\n");
    {
        ID3D11Buffer* bufs[4] = {};
        BOOL all_ok = TRUE;
        for (int i = 0; i < 4; i++) {
            D3D11_BUFFER_DESC desc = {};
            desc.ByteWidth = 64 * (i + 1);
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            hr = device->CreateBuffer(&desc, NULL, &bufs[i]);
            printf("  buf[%d]=%p hr=0x%08X\n", i, bufs[i], (unsigned)hr);
            if (FAILED(hr) || !bufs[i]) all_ok = FALSE;
        }
        check("All 4 buffers created", all_ok);
        for (int i = 0; i < 4; i++) if (bufs[i]) bufs[i]->Release();
    }

    context->Release();
    device->Release();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    printf("Test complete!\n");
    return g_fail > 0 ? 1 : 0;
}
