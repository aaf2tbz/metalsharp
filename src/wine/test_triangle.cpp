#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static const char msl_source[] =
    "using namespace metal;\n"
    "struct VertexOut { float4 position [[position]]; float4 color; };\n"
    "vertex VertexOut vs_main(uint vid [[vertex_id]], device float3* pos [[buffer(0)]]) {\n"
    "    float3 p = pos[vid];\n"
    "    VertexOut out;\n"
    "    out.position = float4(p.xy, 0.0, 1.0);\n"
    "    out.color = float4(0.9, 0.4, 0.1, 1.0);\n"
    "    return out;\n"
    "}\n"
    "fragment float4 ps_main(VertexOut in [[stage_in]]) {\n"
    "    return in.color;\n"
    "}\n";

static int g_pass = 0;
static int g_fail = 0;

static void check(const char* name, BOOL cond) {
    if (cond) { printf("  PASS: %s\n", name); g_pass++; }
    else      { printf("  FAIL: %s\n", name); g_fail++; }
}

static void build_shader_bytecode(const char* func_name, void* out, int* out_len) {
    uint8_t* p = (uint8_t*)out;
    memcpy(p, "MSL", 4); p += 4;
    int name_len = (int)strlen(func_name);
    memcpy(p, func_name, name_len + 1); p += name_len + 1;
    memcpy(p, msl_source, sizeof(msl_source)); p += sizeof(msl_source);
    *out_len = (int)(p - (uint8_t*)out);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdline, int show) {
    printf("=== MetalSharp Triangle Rendering Test ===\n\n");

    ID3D11Device* device = NULL;
    ID3D11DeviceContext* ctx = NULL;
    IDXGISwapChain* swapchain = NULL;
    D3D_FEATURE_LEVEL fl;

    DXGI_SWAP_CHAIN_DESC scDesc = {};
    scDesc.BufferDesc.Width = 800;
    scDesc.BufferDesc.Height = 600;
    scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.BufferDesc.RefreshRate.Numerator = 60;
    scDesc.BufferDesc.RefreshRate.Denominator = 1;
    scDesc.SampleDesc.Count = 1;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.BufferCount = 2;
    scDesc.OutputWindow = NULL;
    scDesc.Windowed = TRUE;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
        NULL, 0, D3D11_SDK_VERSION, &scDesc, &swapchain, &device, &fl, &ctx);
    printf("[Device] hr=0x%08X dev=%p ctx=%p sc=%p fl=0x%X\n", (unsigned)hr, device, ctx, swapchain, fl);
    check("D3D11CreateDeviceAndSwapChain", SUCCEEDED(hr) && device && ctx && swapchain);

    if (FAILED(hr)) {
        printf("Cannot create device. Aborting.\n");
        return 1;
    }

    ID3D11Texture2D* backBuffer = NULL;
    hr = swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    printf("[BackBuffer] hr=0x%08X tex=%p\n", (unsigned)hr, backBuffer);
    check("GetBuffer", SUCCEEDED(hr) && backBuffer);

    ID3D11RenderTargetView* rtv = NULL;
    hr = device->CreateRenderTargetView(backBuffer, NULL, &rtv);
    printf("[RTV] hr=0x%08X rtv=%p\n", (unsigned)hr, rtv);
    check("CreateRenderTargetView", SUCCEEDED(hr) && rtv);

    float vertices[] = {
         0.0f,  0.5f, 0.0f,
         0.5f, -0.5f, 0.0f,
        -0.5f, -0.5f, 0.0f,
    };

    ID3D11Buffer* vertexBuffer = NULL;
    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = sizeof(vertices);
    vbDesc.Usage = D3D11_USAGE_DEFAULT;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vbData = {};
    vbData.pSysMem = vertices;
    hr = device->CreateBuffer(&vbDesc, &vbData, &vertexBuffer);
    printf("[VB] hr=0x%08X buf=%p\n", (unsigned)hr, vertexBuffer);
    check("CreateBuffer (vertex)", SUCCEEDED(hr) && vertexBuffer);

    char vs_bc[4096]; int vs_len;
    build_shader_bytecode("vs_main", vs_bc, &vs_len);
    ID3D11VertexShader* vs = NULL;
    hr = device->CreateVertexShader(vs_bc, vs_len, NULL, &vs);
    printf("[VS] hr=0x%08X vs=%p\n", (unsigned)hr, vs);
    check("CreateVertexShader (MSL)", SUCCEEDED(hr) && vs);

    char ps_bc[4096]; int ps_len;
    build_shader_bytecode("ps_main", ps_bc, &ps_len);
    ID3D11PixelShader* ps = NULL;
    hr = device->CreatePixelShader(ps_bc, ps_len, NULL, &ps);
    printf("[PS] hr=0x%08X ps=%p\n", (unsigned)hr, ps);
    check("CreatePixelShader (MSL)", SUCCEEDED(hr) && ps);

    ID3D11InputLayout* inputLayout = NULL;
    D3D11_INPUT_ELEMENT_DESC layoutDesc = { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 };
    hr = device->CreateInputLayout(&layoutDesc, 1, vs_bc, vs_len, &inputLayout);
    printf("[Layout] hr=0x%08X layout=%p\n", (unsigned)hr, inputLayout);
    check("CreateInputLayout", SUCCEEDED(hr));

    printf("\n--- Render Loop (60 frames) ---\n");
    for (int frame = 0; frame < 60; frame++) {
        float clearColor[4] = { 0.1f, 0.1f, 0.15f, 1.0f };
        ctx->ClearRenderTargetView(rtv, clearColor);

        ctx->OMSetRenderTargets(1, &rtv, NULL);

        UINT stride = 12;
        UINT offset = 0;
        ctx->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->IASetInputLayout(inputLayout);

        ctx->VSSetShader(vs, NULL, 0);
        ctx->PSSetShader(ps, NULL, 0);

        D3D11_VIEWPORT vp = { 0, 0, 800, 600, 0, 1 };
        ctx->RSSetViewports(1, &vp);

        ctx->Draw(3, 0);

        hr = swapchain->Present(1, 0);
        if (FAILED(hr) && frame == 0) {
            printf("  FAIL: Present hr=0x%08X\n", (unsigned)hr);
            g_fail++;
        }
        if (frame == 0) {
            check("First frame presented", SUCCEEDED(hr));
        }

        Sleep(16);
    }

    printf("\n--- Cleanup ---\n");
    if (inputLayout) inputLayout->Release();
    if (ps) ps->Release();
    if (vs) vs->Release();
    if (vertexBuffer) vertexBuffer->Release();
    if (rtv) rtv->Release();
    if (backBuffer) backBuffer->Release();
    if (swapchain) swapchain->Release();
    if (ctx) ctx->Release();
    if (device) device->Release();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
