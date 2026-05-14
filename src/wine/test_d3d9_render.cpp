#include <d3d9.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

static IDirect3D9* g_d3d = NULL;
static IDirect3DDevice9* g_dev = NULL;
static HWND g_wnd = NULL;
static const int W = 800, H = 600;

static int init_d3d() {
    g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!g_d3d) {
        fprintf(stderr, "  FAIL: Direct3DCreate9\n");
        return 0;
    }
    fprintf(stderr, "  PASS: Direct3DCreate9\n");

    g_wnd = CreateWindowExA(0, "STATIC", "d3d9_render_test", 0, 0, 0, W, H, NULL, NULL, GetModuleHandle(NULL), NULL);
    if (!g_wnd) {
        fprintf(stderr, "  FAIL: CreateWindow\n");
        return 0;
    }

    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferWidth = W;
    pp.BackBufferHeight = H;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24X8;

    HRESULT hr = g_d3d->CreateDevice(0, D3DDEVTYPE_HAL, g_wnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &g_dev);
    if (FAILED(hr)) {
        fprintf(stderr, "  FAIL: CreateDevice (0x%08x)\n", hr);
        return 0;
    }
    fprintf(stderr, "  PASS: CreateDevice\n");
    return 1;
}

static void shutdown_d3d() {
    if (g_dev)
        g_dev->Release();
    if (g_d3d)
        g_d3d->Release();
    if (g_wnd)
        DestroyWindow(g_wnd);
}

static int present_and_wait(const char* name) {
    HRESULT hr = g_dev->Present(NULL, NULL, NULL, NULL);
    if (FAILED(hr)) {
        fprintf(stderr, "  FAIL: %s Present (0x%08x)\n", name, hr);
        return 0;
    }
    Sleep(2000);
    fprintf(stderr, "  PASS: %s\n", name);
    return 1;
}

struct VertexPC {
    float x, y, z;
    DWORD color;
};

struct VertexPCT {
    float x, y, z;
    DWORD color;
    float u, v;
};

static IDirect3DVertexBuffer9* make_vb(const void* data, UINT size) {
    IDirect3DVertexBuffer9* vb = NULL;
    HRESULT hr = g_dev->CreateVertexBuffer(size, 0, 0, D3DPOOL_DEFAULT, &vb, NULL);
    if (FAILED(hr)) {
        fprintf(stderr, "    FAIL: CreateVertexBuffer (0x%08x)\n", hr);
        return NULL;
    }
    void* ptr = NULL;
    vb->Lock(0, 0, &ptr, 0);
    memcpy(ptr, data, size);
    vb->Unlock();
    return vb;
}

static int probe1_triangle_no_bg() {
    fprintf(stderr, "\n--- Probe 1: Colored triangle, no background ---\n");

    VertexPC tri[] = {
        {0.0f, 0.5f, 0.5f, 0x00ff0000},
        {0.5f, -0.5f, 0.5f, 0x0000ff00},
        {-0.5f, -0.5f, 0.5f, 0x000000ff},
    };

    g_dev->Clear(0, NULL, D3DCLEAR_TARGET, 0xff000000, 1.0f, 0);
    g_dev->BeginScene();

    IDirect3DVertexBuffer9* vb = make_vb(tri, sizeof(tri));
    if (!vb)
        return 0;

    g_dev->SetStreamSource(0, vb, 0, sizeof(VertexPC));
    g_dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    g_dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1);

    g_dev->EndScene();
    vb->Release();
    return present_and_wait("Probe 1: colored triangle on black");
}

static int probe2_triangle_with_bg() {
    fprintf(stderr, "\n--- Probe 2: Colored triangle, blue background ---\n");

    VertexPC tri[] = {
        {0.0f, 0.5f, 0.5f, 0x00ffff00},
        {0.5f, -0.5f, 0.5f, 0x00ff00ff},
        {-0.5f, -0.5f, 0.5f, 0x0000ffff},
    };

    g_dev->Clear(0, NULL, D3DCLEAR_TARGET, 0xff404080, 1.0f, 0);
    g_dev->BeginScene();

    IDirect3DVertexBuffer9* vb = make_vb(tri, sizeof(tri));
    if (!vb)
        return 0;

    g_dev->SetStreamSource(0, vb, 0, sizeof(VertexPC));
    g_dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    g_dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1);

    g_dev->EndScene();
    vb->Release();
    return present_and_wait("Probe 2: colored triangle on blue bg");
}

static int probe3_depth_test() {
    fprintf(stderr, "\n--- Probe 3: Two triangles at different depths ---\n");

    VertexPC tris[] = {
        {-0.8f, 0.8f, 0.2f, 0x00ff0000}, {0.0f, -0.8f, 0.2f, 0x00ff0000}, {-0.8f, -0.8f, 0.2f, 0x00ff0000},

        {-0.2f, 0.8f, 0.8f, 0x0000ff00}, {0.8f, -0.8f, 0.8f, 0x0000ff00}, {0.8f, 0.8f, 0.8f, 0x0000ff00},
    };

    g_dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff1a1a2e, 1.0f, 0);
    g_dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    g_dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    g_dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESS);
    g_dev->BeginScene();

    IDirect3DVertexBuffer9* vb = make_vb(tris, sizeof(tris));
    if (!vb)
        return 0;

    g_dev->SetStreamSource(0, vb, 0, sizeof(VertexPC));
    g_dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    g_dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2);

    g_dev->EndScene();
    g_dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    vb->Release();
    return present_and_wait("Probe 3: depth-tested triangles (green in front of red)");
}

static int probe4_rgb_square() {
    fprintf(stderr, "\n--- Probe 4: Two RGB triangles forming a square ---\n");

    VertexPC sq[] = {
        {-0.5f, 0.5f, 0.5f, 0x00ff0000},  {0.5f, 0.5f, 0.5f, 0x0000ff00}, {-0.5f, -0.5f, 0.5f, 0x000000ff},

        {-0.5f, -0.5f, 0.5f, 0x000000ff}, {0.5f, 0.5f, 0.5f, 0x0000ff00}, {0.5f, -0.5f, 0.5f, 0x00ffffff},
    };

    g_dev->Clear(0, NULL, D3DCLEAR_TARGET, 0xff2d2d2d, 1.0f, 0);
    g_dev->BeginScene();

    IDirect3DVertexBuffer9* vb = make_vb(sq, sizeof(sq));
    if (!vb)
        return 0;

    g_dev->SetStreamSource(0, vb, 0, sizeof(VertexPC));
    g_dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    g_dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2);

    g_dev->EndScene();
    vb->Release();
    return present_and_wait("Probe 4: RGB square");
}

static int probe5_textured_bg() {
    fprintf(stderr, "\n--- Probe 5: Triangle over textured background ---\n");

    IDirect3DTexture9* tex = NULL;
    HRESULT hr = g_dev->CreateTexture(2, 2, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, NULL);
    if (FAILED(hr)) {
        fprintf(stderr, "    FAIL: CreateTexture (0x%08x)\n", hr);
        return 0;
    }

    D3DLOCKED_RECT lr;
    tex->LockRect(0, &lr, NULL, 0);
    DWORD* px = (DWORD*)lr.pBits;
    px[0] = 0xff808080;
    px[1] = 0xff606060;
    px[2] = 0xff606060;
    px[3] = 0xff808080;
    tex->UnlockRect(0);

    VertexPCT bg[] = {
        {-1.0f, 1.0f, 0.9f, 0xffffffff, 0.0f, 0.0f},  {1.0f, 1.0f, 0.9f, 0xffffffff, 1.0f, 0.0f},
        {-1.0f, -1.0f, 0.9f, 0xffffffff, 0.0f, 1.0f},

        {-1.0f, -1.0f, 0.9f, 0xffffffff, 0.0f, 1.0f}, {1.0f, 1.0f, 0.9f, 0xffffffff, 1.0f, 0.0f},
        {1.0f, -1.0f, 0.9f, 0xffffffff, 1.0f, 1.0f},
    };

    VertexPC tri[] = {
        {0.0f, 0.5f, 0.5f, 0x00ff4444},
        {0.5f, -0.5f, 0.5f, 0x0044ff44},
        {-0.5f, -0.5f, 0.5f, 0x004444ff},
    };

    g_dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff000000, 1.0f, 0);
    g_dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    g_dev->BeginScene();

    IDirect3DVertexBuffer9* vbBg = make_vb(bg, sizeof(bg));
    g_dev->SetStreamSource(0, vbBg, 0, sizeof(VertexPCT));
    g_dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    g_dev->SetTexture(0, tex);
    g_dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2);
    g_dev->SetTexture(0, NULL);
    vbBg->Release();

    IDirect3DVertexBuffer9* vbTri = make_vb(tri, sizeof(tri));
    g_dev->SetStreamSource(0, vbTri, 0, sizeof(VertexPC));
    g_dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    g_dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1);
    vbTri->Release();

    g_dev->EndScene();
    g_dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    tex->Release();
    return present_and_wait("Probe 5: triangle over checkerboard texture");
}

static int probe6_alpha_blend() {
    fprintf(stderr, "\n--- Probe 6: Alpha-blended triangle over background ---\n");

    VertexPC bg[] = {
        {-0.8f, 0.8f, 0.5f, 0x00ffffff},  {0.8f, 0.8f, 0.5f, 0x00ffffff}, {-0.8f, -0.8f, 0.5f, 0x00333333},

        {-0.8f, -0.8f, 0.5f, 0x00333333}, {0.8f, 0.8f, 0.5f, 0x00ffffff}, {0.8f, -0.8f, 0.5f, 0x00333333},
    };

    VertexPC tri[] = {
        {0.0f, 0.5f, 0.2f, 0x80ff0000},
        {0.5f, -0.5f, 0.2f, 0x8000ff00},
        {-0.5f, -0.5f, 0.2f, 0x800000ff},
    };

    g_dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff000000, 1.0f, 0);
    g_dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    g_dev->BeginScene();

    IDirect3DVertexBuffer9* vbBg = make_vb(bg, sizeof(bg));
    g_dev->SetStreamSource(0, vbBg, 0, sizeof(VertexPC));
    g_dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    g_dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2);
    vbBg->Release();

    g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

    IDirect3DVertexBuffer9* vbTri = make_vb(tri, sizeof(tri));
    g_dev->SetStreamSource(0, vbTri, 0, sizeof(VertexPC));
    g_dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    g_dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1);
    vbTri->Release();

    g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_dev->EndScene();
    g_dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    return present_and_wait("Probe 6: alpha-blended triangle");
}

static int probe7_full_scene() {
    fprintf(stderr, "\n--- Probe 7: Full scene — textured quad + depth + alpha ---\n");

    IDirect3DTexture9* tex = NULL;
    g_dev->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, NULL);
    D3DLOCKED_RECT lr;
    tex->LockRect(0, &lr, NULL, 0);
    DWORD* px = (DWORD*)lr.pBits;
    for (int i = 0; i < 16; i++)
        px[i] = (i % 2 == i / 4 % 2) ? 0xff446688 : 0xff223344;
    tex->UnlockRect(0);

    VertexPCT bg[] = {
        {-1.0f, 1.0f, 0.9f, 0xffffffff, 0.0f, 0.0f},  {1.0f, 1.0f, 0.9f, 0xffffffff, 4.0f, 0.0f},
        {-1.0f, -1.0f, 0.9f, 0xffffffff, 0.0f, 4.0f},

        {-1.0f, -1.0f, 0.9f, 0xffffffff, 0.0f, 4.0f}, {1.0f, 1.0f, 0.9f, 0xffffffff, 4.0f, 0.0f},
        {1.0f, -1.0f, 0.9f, 0xffffffff, 4.0f, 4.0f},
    };

    VertexPC depth_tris[] = {
        {-0.7f, 0.7f, 0.3f, 0x00ff4444}, {0.0f, -0.7f, 0.3f, 0x00ff4444}, {-0.7f, -0.7f, 0.3f, 0x00ff4444},

        {-0.1f, 0.7f, 0.6f, 0x0044ff44}, {0.6f, -0.7f, 0.6f, 0x0044ff44}, {0.6f, 0.7f, 0.6f, 0x0044ff44},
    };

    VertexPC alpha_tri[] = {
        {0.0f, 0.6f, 0.1f, 0xa0ffffff},
        {0.6f, -0.4f, 0.1f, 0xa0ff8800},
        {-0.6f, -0.4f, 0.1f, 0xa08800ff},
    };

    g_dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff0a0a0a, 1.0f, 0);
    g_dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    g_dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    g_dev->BeginScene();

    IDirect3DVertexBuffer9* vbBg = make_vb(bg, sizeof(bg));
    g_dev->SetStreamSource(0, vbBg, 0, sizeof(VertexPCT));
    g_dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    g_dev->SetTexture(0, tex);
    g_dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2);
    g_dev->SetTexture(0, NULL);
    vbBg->Release();

    IDirect3DVertexBuffer9* vbDepth = make_vb(depth_tris, sizeof(depth_tris));
    g_dev->SetStreamSource(0, vbDepth, 0, sizeof(VertexPC));
    g_dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    g_dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2);
    vbDepth->Release();

    g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

    IDirect3DVertexBuffer9* vbAlpha = make_vb(alpha_tri, sizeof(alpha_tri));
    g_dev->SetStreamSource(0, vbAlpha, 0, sizeof(VertexPC));
    g_dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    g_dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1);
    vbAlpha->Release();

    g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_dev->EndScene();
    g_dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    tex->Release();
    return present_and_wait("Probe 7: full scene");
}

int WINAPI WinMain(HINSTANCE h, HINSTANCE p, LPSTR cmd, int show) {
    fprintf(stderr, "\n========================================\n");
    fprintf(stderr, "  D3D9→Metal 7-Probe Rendering Test\n");
    fprintf(stderr, "========================================\n\n");

    if (!init_d3d()) {
        fprintf(stderr, "\nFAILED: could not initialize D3D9\n");
        shutdown_d3d();
        return 1;
    }

    int passed = 0, total = 7;

    if (probe1_triangle_no_bg())
        passed++;
    if (probe2_triangle_with_bg())
        passed++;
    if (probe3_depth_test())
        passed++;
    if (probe4_rgb_square())
        passed++;
    if (probe5_textured_bg())
        passed++;
    if (probe6_alpha_blend())
        passed++;
    if (probe7_full_scene())
        passed++;

    fprintf(stderr, "\n========================================\n");
    fprintf(stderr, "  Results: %d / %d probes passed\n", passed, total);
    fprintf(stderr, "========================================\n");

    shutdown_d3d();
    return passed == total ? 0 : 1;
}
