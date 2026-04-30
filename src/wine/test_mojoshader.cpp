#include <windows.h>
#include <d3d9.h>
#include <stdio.h>

static const DWORD vs_bytecode[] = {
    0xFFFE0300,
    0x0000004, 0xFFFE0002, 0x00000010, 0x00000001,
    0x00000000, 0x800F0000, 0x90E40000,
    0x00000000, 0x800F0001, 0x90E40001,
    0x0000FFFF
};

static const DWORD ps_bytecode[] = {
    0xFFFF0300,
    0x0000004, 0xFFFE0002, 0x00000010, 0x00000001,
    0x00000000, 0x800F0000, 0x80E40001,
    0x0000FFFF
};

int WINAPI WinMain(HINSTANCE h, HINSTANCE p, LPSTR cmd, int show) {
    fprintf(stderr, "=== D3D9 MojoShader Test ===\n");

    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) { fprintf(stderr, "  FAIL: Direct3DCreate9\n"); return 1; }

    HWND wnd = CreateWindowExA(0, "STATIC", "mojotest", 0, 0, 0, 400, 300, NULL, NULL, h, NULL);

    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferWidth = 400;
    pp.BackBufferHeight = 300;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;

    IDirect3DDevice9* dev = NULL;
    HRESULT hr = d3d->CreateDevice(0, D3DDEVTYPE_HAL, wnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev);
    if (FAILED(hr)) { fprintf(stderr, "  FAIL: CreateDevice (0x%08x)\n", hr); return 1; }
    fprintf(stderr, "  PASS: CreateDevice\n");

    IDirect3DVertexShader9* vs = NULL;
    hr = dev->CreateVertexShader(vs_bytecode, &vs);
    if (FAILED(hr)) { fprintf(stderr, "  FAIL: CreateVertexShader (0x%08x)\n", hr); return 1; }
    fprintf(stderr, "  PASS: CreateVertexShader (SM3.0 bytecode)\n");

    IDirect3DPixelShader9* ps = NULL;
    hr = dev->CreatePixelShader(ps_bytecode, &ps);
    if (FAILED(hr)) { fprintf(stderr, "  FAIL: CreatePixelShader (0x%08x)\n", hr); return 1; }
    fprintf(stderr, "  PASS: CreatePixelShader (SM3.0 bytecode)\n");

    hr = dev->SetVertexShader(vs);
    if (FAILED(hr)) { fprintf(stderr, "  FAIL: SetVertexShader\n"); return 1; }
    fprintf(stderr, "  PASS: SetVertexShader\n");

    hr = dev->SetPixelShader(ps);
    if (FAILED(hr)) { fprintf(stderr, "  FAIL: SetPixelShader\n"); return 1; }
    fprintf(stderr, "  PASS: SetPixelShader\n");

    fprintf(stderr, "\n=== ALL MOJOSHADER TESTS PASSED ===\n");

    vs->Release();
    ps->Release();
    dev->Release();
    d3d->Release();
    DestroyWindow(wnd);
    return 0;
}
