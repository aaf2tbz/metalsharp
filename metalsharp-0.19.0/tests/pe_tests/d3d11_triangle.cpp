#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
#include <cstdlib>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "user32.lib")

static ID3D11Device* g_device = nullptr;
static ID3D11DeviceContext* g_context = nullptr;
static IDXGISwapChain* g_swapChain = nullptr;
static ID3D11RenderTargetView* g_rtView = nullptr;
static ID3D11VertexShader* g_vs = nullptr;
static ID3D11PixelShader* g_ps = nullptr;
static ID3D11InputLayout* g_layout = nullptr;
static ID3D11Buffer* g_vb = nullptr;

static const char vs_src[] =
    "float4 VS(float3 pos : POSITION) : SV_POSITION {"
    "  return float4(pos, 1.0);"
    "}";

static const char ps_src[] =
    "float4 PS() : SV_TARGET {"
    "  return float4(0.2, 0.6, 1.0, 1.0);"
    "}";

static float vertices[] = {
     0.0f,  0.5f, 0.0f,
     0.5f, -0.5f, 0.0f,
    -0.5f, -0.5f, 0.0f,
};

static LRESULT WINAPI WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(wnd, msg, wp, lp);
}

static void createRenderTarget() {
    ID3D11Texture2D* backBuf = nullptr;
    g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuf);
    if (backBuf) {
        g_device->CreateRenderTargetView(backBuf, nullptr, &g_rtView);
        backBuf->Release();
    }
    g_context->OMSetRenderTargets(1, &g_rtView, nullptr);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdLine, int show) {
    OutputDebugStringA("MetalSharp PE test: starting\n");

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"MetalSharpTest";
    RegisterClassW(&wc);

    HWND wnd = CreateWindowExW(0, L"MetalSharpTest", L"MetalSharp D3D11 Triangle",
        WS_OVERLAPPEDWINDOW, 100, 100, 800, 600,
        nullptr, nullptr, inst, nullptr);
    ShowWindow(wnd, show);
    UpdateWindow(wnd);

    RECT clientRect;
    GetClientRect(wnd, &clientRect);
    int w = clientRect.right - clientRect.left;
    int h = clientRect.bottom - clientRect.top;

    DXGI_SWAP_CHAIN_DESC scDesc = {};
    scDesc.BufferCount = 2;
    scDesc.BufferDesc.Width = w;
    scDesc.BufferDesc.Height = h;
    scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.BufferDesc.RefreshRate.Numerator = 60;
    scDesc.BufferDesc.RefreshRate.Denominator = 1;
    scDesc.SampleDesc.Count = 1;
    scDesc.BufferUsage = 0x20;
    scDesc.OutputWindow = wnd;
    scDesc.Windowed = TRUE;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE,
        nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
        &scDesc, &g_swapChain, &g_device, &featureLevel, &g_context);

    if (FAILED(hr)) {
        char buf[256];
        wsprintfA(buf, "D3D11CreateDeviceAndSwapChain failed: 0x%08X\n", (unsigned)hr);
        OutputDebugStringA(buf);
        return 1;
    }

    createRenderTarget();

    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = sizeof(vertices);
    vbDesc.Usage = D3D11_USAGE_DEFAULT;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vbData = { vertices, 0, 0 };
    g_device->CreateBuffer(&vbDesc, &vbData, &g_vb);

    UINT stride = 12;
    UINT offset = 0;
    g_context->IASetVertexBuffers(0, 1, &g_vb, &stride, &offset);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
    g_context->RSSetViewports(1, &vp);

    float clearColor[4] = { 0.05f, 0.05f, 0.1f, 1.0f };

    OutputDebugStringA("MetalSharp PE test: entering render loop\n");

    int frameCount = 0;
    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            continue;
        }

        g_context->ClearRenderTargetView(g_rtView, clearColor);
        g_context->Draw(3, 0);
        g_swapChain->Present(1, 0);
        frameCount++;

        if (frameCount >= 10) {
            OutputDebugStringA("MetalSharp PE test: rendered 10 frames, exiting\n");
            PostQuitMessage(0);
        }
    }

    OutputDebugStringA("MetalSharp PE test: shutting down\n");

    if (g_rtView) g_rtView->Release();
    if (g_vb) g_vb->Release();
    if (g_layout) g_layout->Release();
    if (g_vs) g_vs->Release();
    if (g_ps) g_ps->Release();
    if (g_swapChain) g_swapChain->Release();
    if (g_context) g_context->Release();
    if (g_device) g_device->Release();

    return 0;
}
