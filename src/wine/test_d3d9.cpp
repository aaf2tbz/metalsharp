#include <windows.h>
#include <d3d9.h>
#include <stdio.h>

int WINAPI WinMain(HINSTANCE h, HINSTANCE p, LPSTR cmd, int show) {
    fprintf(stderr, "=== D3D9 Bridge Test ===\n");

    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) { fprintf(stderr, "  FAIL: Direct3DCreate9\n"); return 1; }
    fprintf(stderr, "  PASS: Direct3DCreate9\n");

    D3DADAPTER_IDENTIFIER9 id = {};
    HRESULT hr = d3d->GetAdapterIdentifier(0, 0, &id);
    if (FAILED(hr)) { fprintf(stderr, "  FAIL: GetAdapterIdentifier (0x%08x)\n", hr); return 1; }
    fprintf(stderr, "  PASS: GetAdapterIdentifier: %s\n", id.Description);

    D3DCAPS9 caps = {};
    hr = d3d->GetDeviceCaps(0, D3DDEVTYPE_HAL, &caps);
    if (FAILED(hr)) { fprintf(stderr, "  FAIL: GetDeviceCaps (0x%08x)\n", hr); return 1; }
    fprintf(stderr, "  PASS: GetDeviceCaps: VS=%u PS=%u\n", caps.VertexShaderVersion, caps.PixelShaderVersion);

    D3DDISPLAYMODE dm = {};
    hr = d3d->GetAdapterDisplayMode(0, &dm);
    if (FAILED(hr)) { fprintf(stderr, "  FAIL: GetAdapterDisplayMode (0x%08x)\n", hr); return 1; }
    fprintf(stderr, "  PASS: GetAdapterDisplayMode: %ux%u @ %uHz fmt=%u\n", dm.Width, dm.Height, dm.RefreshRate, dm.Format);

    HWND wnd = CreateWindowExA(0, "STATIC", "d3d9test", 0, 0, 0, 800, 600, NULL, NULL, h, NULL);
    if (!wnd) { fprintf(stderr, "  FAIL: CreateWindow\n"); return 1; }

    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferWidth = 800;
    pp.BackBufferHeight = 600;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;

    IDirect3DDevice9* dev = NULL;
    hr = d3d->CreateDevice(0, D3DDEVTYPE_HAL, wnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev);
    if (FAILED(hr)) { fprintf(stderr, "  FAIL: CreateDevice (0x%08x)\n", hr); return 1; }
    fprintf(stderr, "  PASS: CreateDevice\n");

    struct Vertex { float x, y, z; DWORD color; };
    Vertex tri[] = {
        { 0.0f,  0.5f, 0.0f, 0x00ff0000 },
        { 0.5f, -0.5f, 0.0f, 0x0000ff00 },
        {-0.5f, -0.5f, 0.0f, 0x000000ff },
    };

    IDirect3DVertexBuffer9* vb = NULL;
    hr = dev->CreateVertexBuffer(sizeof(tri), 0, D3DFVF_XYZ | D3DFVF_DIFFUSE, D3DPOOL_DEFAULT, &vb, NULL);
    if (FAILED(hr)) { fprintf(stderr, "  FAIL: CreateVertexBuffer (0x%08x)\n", hr); return 1; }
    fprintf(stderr, "  PASS: CreateVertexBuffer\n");

    void* data = NULL;
    hr = vb->Lock(0, 0, &data, 0);
    if (FAILED(hr)) { fprintf(stderr, "  FAIL: Lock (0x%08x)\n", hr); return 1; }
    memcpy(data, tri, sizeof(tri));
    vb->Unlock();
    fprintf(stderr, "  PASS: Lock/Unlock\n");

    hr = dev->SetStreamSource(0, vb, 0, sizeof(Vertex));
    if (FAILED(hr)) { fprintf(stderr, "  FAIL: SetStreamSource (0x%08x)\n", hr); return 1; }
    fprintf(stderr, "  PASS: SetStreamSource\n");

    hr = dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    if (FAILED(hr)) { fprintf(stderr, "  FAIL: SetFVF (0x%08x)\n", hr); return 1; }
    fprintf(stderr, "  PASS: SetFVF\n");

    hr = dev->BeginScene();
    if (FAILED(hr)) { fprintf(stderr, "  FAIL: BeginScene (0x%08x)\n", hr); return 1; }
    fprintf(stderr, "  PASS: BeginScene\n");

    hr = dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1);
    if (FAILED(hr)) { fprintf(stderr, "  FAIL: DrawPrimitive (0x%08x)\n", hr); return 1; }
    fprintf(stderr, "  PASS: DrawPrimitive\n");

    hr = dev->EndScene();
    if (FAILED(hr)) { fprintf(stderr, "  FAIL: EndScene (0x%08x)\n", hr); return 1; }
    fprintf(stderr, "  PASS: EndScene\n");

    hr = dev->Present(NULL, NULL, NULL, NULL);
    if (FAILED(hr)) { fprintf(stderr, "  FAIL: Present (0x%08x)\n", hr); return 1; }
    fprintf(stderr, "  PASS: Present\n");

    fprintf(stderr, "\n=== ALL D3D9 TESTS PASSED ===\n");

    vb->Release();
    dev->Release();
    d3d->Release();
    DestroyWindow(wnd);
    return 0;
}
