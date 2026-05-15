/// @file d3d9_pe.cpp
/// @brief Wine D3D9 PE module with COM interface forwarding.
///
/// Standalone Wine PE-side d3d9.dll that implements IDirect3D9 and IDirect3DDevice9 COM interfaces. Forwards create/execute calls through Wine's unixlib mechanism to a platform-specific rendering backend for legacy D3D9 games.
#include <d3d9.h>
#include <stdio.h>
#include <string.h>

static inline LONG ref_add(LONG* r) { return __sync_add_and_fetch(r, 1); }
static inline LONG ref_sub(LONG* r) { return __sync_sub_and_fetch(r, 1); }
#include <stdint.h>
#include <windows.h>
#include <winternl.h>

#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

#include "d3d9_unix.h"

typedef long NTSTATUS;
typedef unsigned long long unixlib_handle_t;
typedef NTSTATUS (WINAPI *unix_call_dispatcher_t)(unixlib_handle_t, unsigned int, void*);

extern "C" {
extern NTSTATUS (WINAPI *__wine_unix_call_dispatcher)(unixlib_handle_t, unsigned int, void*);
extern unixlib_handle_t __wine_unixlib_handle;
extern NTSTATUS WINAPI __wine_init_unix_call(void);
}

static NTSTATUS unix_call(unsigned int code, void* args) {
    return __wine_unix_call_dispatcher(__wine_unixlib_handle, code, args);
}

struct handle_entry { void* obj; uint64_t handle; };
static handle_entry g_handles[512];
static int g_handle_count = 0;

static uint64_t obj_handle(void* obj) {
    if (!obj) return 0;
    for (int i = 0; i < g_handle_count; i++)
        if (g_handles[i].obj == obj) return g_handles[i].handle;
    return 0;
}

static void set_handle(void* obj, uint64_t h) {
    if (!obj) return;
    for (int i = 0; i < g_handle_count; i++) {
        if (g_handles[i].obj == obj) { g_handles[i].handle = h; return; }
    }
    if (g_handle_count < 512) {
        g_handles[g_handle_count].obj = obj;
        g_handles[g_handle_count].handle = h;
        g_handle_count++;
    }
}

class MSD3DSurface : public IDirect3DSurface9 {
    LONG m_ref = 1;
public:
    HRESULT WINAPI QueryInterface(REFIID, void** ppv) override { if(ppv)*ppv=nullptr; return E_NOINTERFACE; }
    ULONG WINAPI AddRef() override { return ref_add(&m_ref); }
    ULONG WINAPI Release() override { ULONG r = ref_sub(&m_ref); return r ? r : 0; }
    HRESULT WINAPI GetDevice(IDirect3DDevice9** pp) override { if(pp)*pp=nullptr; return S_OK; }
    HRESULT WINAPI SetPrivateData(REFGUID, const void*, DWORD, DWORD) override { return S_OK; }
    HRESULT WINAPI GetPrivateData(REFGUID, void*, DWORD*) override { return E_NOTIMPL; }
    HRESULT WINAPI FreePrivateData(REFGUID) override { return S_OK; }
    DWORD WINAPI SetPriority(DWORD p) override { return p; }
    DWORD WINAPI GetPriority() override { return 0; }
    void WINAPI PreLoad() override {}
    D3DRESOURCETYPE WINAPI GetType() override { return D3DRTYPE_SURFACE; }
    HRESULT WINAPI GetContainer(REFIID, void**) override { return E_NOTIMPL; }
    HRESULT WINAPI GetDesc(D3DSURFACE_DESC* pDesc) override { if(pDesc) memset(pDesc,0,sizeof(*pDesc)); return S_OK; }
    HRESULT WINAPI LockRect(D3DLOCKED_RECT*, const RECT*, DWORD) override { return E_NOTIMPL; }
    HRESULT WINAPI UnlockRect() override { return S_OK; }
    HRESULT WINAPI GetDC(HDC*) override { return E_NOTIMPL; }
    HRESULT WINAPI ReleaseDC(HDC) override { return S_OK; }
};

class MSD3DVertexBuffer : public IDirect3DVertexBuffer9 {
public:
    LONG m_ref = 1;
    void* m_lock_ptr = nullptr;
    DWORD m_lock_size = 0;
    HRESULT WINAPI QueryInterface(REFIID, void** ppv) override { if(ppv)*ppv=nullptr; return E_NOINTERFACE; }
    ULONG WINAPI AddRef() override { return ref_add(&m_ref); }
    ULONG WINAPI Release() override { ULONG r = ref_sub(&m_ref); return r ? r : 0; }
    HRESULT WINAPI GetDevice(IDirect3DDevice9** pp) override { if(pp)*pp=nullptr; return S_OK; }
    HRESULT WINAPI SetPrivateData(REFGUID, const void*, DWORD, DWORD) override { return S_OK; }
    HRESULT WINAPI GetPrivateData(REFGUID, void*, DWORD*) override { return E_NOTIMPL; }
    HRESULT WINAPI FreePrivateData(REFGUID) override { return S_OK; }
    DWORD WINAPI SetPriority(DWORD p) override { return p; }
    DWORD WINAPI GetPriority() override { return 0; }
    void WINAPI PreLoad() override {}
    D3DRESOURCETYPE WINAPI GetType() override { return D3DRTYPE_VERTEXBUFFER; }
    HRESULT WINAPI Lock(UINT ofs, UINT sz, void** ppbData, DWORD flags) override {
        if(!ppbData) return E_POINTER;
        fprintf(stderr, "[d3d9] VB Lock: m_lock_ptr=%p m_lock_size=%u\n", m_lock_ptr, m_lock_size);
        *ppbData = m_lock_ptr;
        return S_OK;
    }
    HRESULT WINAPI Unlock() override {
        if(!m_lock_ptr || !m_lock_size) return S_OK;
        struct d3d9_upload_params p = {};
        p.resource_handle = obj_handle(this);
        p.data = (uint64_t)(uintptr_t)m_lock_ptr;
        p.size = m_lock_size;
        fprintf(stderr, "[d3d9] VB Unlock: uploading %u bytes from %p handle=%llu\n", m_lock_size, m_lock_ptr, (unsigned long long)p.resource_handle);
        unix_call(D3D9_FUNC_UPLOAD_BUFFER, &p);
        return S_OK;
    }
    HRESULT WINAPI GetDesc(D3DVERTEXBUFFER_DESC*) override { return E_NOTIMPL; }
};

class MSD3DIndexBuffer : public IDirect3DIndexBuffer9 {
public:
    LONG m_ref = 1;
    void* m_lock_ptr = nullptr;
    DWORD m_lock_size = 0;

    HRESULT WINAPI QueryInterface(REFIID, void** ppv) override { if(ppv)*ppv=nullptr; return E_NOINTERFACE; }
    ULONG WINAPI AddRef() override { return ref_add(&m_ref); }
    ULONG WINAPI Release() override { ULONG r = ref_sub(&m_ref); return r ? r : 0; }
    HRESULT WINAPI GetDevice(IDirect3DDevice9** pp) override { if(pp)*pp=nullptr; return S_OK; }
    HRESULT WINAPI SetPrivateData(REFGUID, const void*, DWORD, DWORD) override { return S_OK; }
    HRESULT WINAPI GetPrivateData(REFGUID, void*, DWORD*) override { return S_OK; }
    HRESULT WINAPI FreePrivateData(REFGUID) override { return S_OK; }
    DWORD WINAPI SetPriority(DWORD p) override { return p; }
    DWORD WINAPI GetPriority() override { return 0; }
    void WINAPI PreLoad() override {}
    D3DRESOURCETYPE WINAPI GetType() override { return D3DRTYPE_INDEXBUFFER; }
    HRESULT WINAPI Lock(UINT ofs, UINT sz, void** ppbData, DWORD flags) override {
        if(!ppbData) return E_POINTER;
        *ppbData = m_lock_ptr;
        return S_OK;
    }
    HRESULT WINAPI Unlock() override {
        if(!m_lock_ptr || !m_lock_size) return S_OK;
        struct d3d9_upload_params p = {};
        p.resource_handle = obj_handle(this);
        p.data = (uint64_t)(uintptr_t)m_lock_ptr;
        p.size = m_lock_size;
        unix_call(D3D9_FUNC_UPLOAD_BUFFER, &p);
        return S_OK;
    }
    HRESULT WINAPI GetDesc(D3DINDEXBUFFER_DESC* pDesc) override { if(pDesc) memset(pDesc,0,sizeof(*pDesc)); return S_OK; }
};

class MSD3DTexture : public IDirect3DTexture9 {
    LONG m_ref = 1;
public:
    HRESULT WINAPI QueryInterface(REFIID, void** ppv) override { if(ppv)*ppv=nullptr; return E_NOINTERFACE; }
    ULONG WINAPI AddRef() override { return ref_add(&m_ref); }
    ULONG WINAPI Release() override { ULONG r = ref_sub(&m_ref); return r ? r : 0; }
    HRESULT WINAPI GetDevice(IDirect3DDevice9** pp) override { if(pp)*pp=nullptr; return S_OK; }
    HRESULT WINAPI SetPrivateData(REFGUID, const void*, DWORD, DWORD) override { return S_OK; }
    HRESULT WINAPI GetPrivateData(REFGUID, void*, DWORD*) override { return E_NOTIMPL; }
    HRESULT WINAPI FreePrivateData(REFGUID) override { return S_OK; }
    DWORD WINAPI SetPriority(DWORD p) override { return p; }
    DWORD WINAPI GetPriority() override { return 0; }
    void WINAPI PreLoad() override {}
    D3DRESOURCETYPE WINAPI GetType() override { return D3DRTYPE_TEXTURE; }
    DWORD WINAPI SetLOD(DWORD) override { return 0; }
    DWORD WINAPI GetLOD() override { return 0; }
    DWORD WINAPI GetLevelCount() override { return 1; }
    HRESULT WINAPI SetAutoGenFilterType(D3DTEXTUREFILTERTYPE) override { return S_OK; }
    D3DTEXTUREFILTERTYPE WINAPI GetAutoGenFilterType() override { return D3DTEXF_LINEAR; }
    void WINAPI GenerateMipSubLevels() override {}
    HRESULT WINAPI GetLevelDesc(UINT, D3DSURFACE_DESC* pDesc) override { if(pDesc) memset(pDesc,0,sizeof(*pDesc)); return S_OK; }
    HRESULT WINAPI GetSurfaceLevel(UINT, IDirect3DSurface9** pp) override { if(pp) *pp = new MSD3DSurface(); return S_OK; }
    HRESULT WINAPI LockRect(UINT, D3DLOCKED_RECT*, const RECT*, DWORD) override { return E_NOTIMPL; }
    HRESULT WINAPI UnlockRect(UINT) override { return S_OK; }
    HRESULT WINAPI AddDirtyRect(const RECT*) override { return S_OK; }
};

class MSD3DVertexDeclaration : public IDirect3DVertexDeclaration9 {
    LONG m_ref = 1;
public:
    HRESULT WINAPI QueryInterface(REFIID, void** ppv) override { if(ppv)*ppv=nullptr; return E_NOINTERFACE; }
    ULONG WINAPI AddRef() override { return ref_add(&m_ref); }
    ULONG WINAPI Release() override { ULONG r = ref_sub(&m_ref); return r ? r : 0; }
    HRESULT WINAPI GetDevice(IDirect3DDevice9** pp) override { if(pp)*pp=nullptr; return S_OK; }
    HRESULT WINAPI GetDeclaration(D3DVERTEXELEMENT9*, UINT*) override { return E_NOTIMPL; }
};

class MSD3DVertexShader : public IDirect3DVertexShader9 {
    LONG m_ref = 1;
public:
    HRESULT WINAPI QueryInterface(REFIID, void** ppv) override { if(ppv)*ppv=nullptr; return E_NOINTERFACE; }
    ULONG WINAPI AddRef() override { return ref_add(&m_ref); }
    ULONG WINAPI Release() override { ULONG r = ref_sub(&m_ref); return r ? r : 0; }
    HRESULT WINAPI GetDevice(IDirect3DDevice9** pp) override { if(pp)*pp=nullptr; return S_OK; }
    HRESULT WINAPI GetFunction(void*, UINT*) override { return S_OK; }
};

class MSD3DPixelShader : public IDirect3DPixelShader9 {
    LONG m_ref = 1;
public:
    HRESULT WINAPI QueryInterface(REFIID, void** ppv) override { if(ppv)*ppv=nullptr; return E_NOINTERFACE; }
    ULONG WINAPI AddRef() override { return ref_add(&m_ref); }
    ULONG WINAPI Release() override { ULONG r = ref_sub(&m_ref); return r ? r : 0; }
    HRESULT WINAPI GetDevice(IDirect3DDevice9** pp) override { if(pp)*pp=nullptr; return S_OK; }
    HRESULT WINAPI GetFunction(void*, UINT*) override { return S_OK; }
};

class MSD3DStateBlock : public IDirect3DStateBlock9 {
    LONG m_ref = 1;
public:
    HRESULT WINAPI QueryInterface(REFIID, void** ppv) override { if(ppv)*ppv=nullptr; return E_NOINTERFACE; }
    ULONG WINAPI AddRef() override { return ref_add(&m_ref); }
    ULONG WINAPI Release() override { ULONG r = ref_sub(&m_ref); return r ? r : 0; }
    HRESULT WINAPI GetDevice(IDirect3DDevice9** pp) override { if(pp)*pp=nullptr; return S_OK; }
    HRESULT WINAPI Capture() override { return S_OK; }
    HRESULT WINAPI Apply() override { return S_OK; }
};

static void fill_caps(D3DCAPS9* pCaps) {
    memset(pCaps, 0, sizeof(D3DCAPS9));
    pCaps->DeviceType = D3DDEVTYPE_HAL;
    pCaps->Caps = D3DCAPS_READ_SCANLINE;
    pCaps->Caps2 = D3DCAPS2_DYNAMICTEXTURES | D3DCAPS2_FULLSCREENGAMMA;
    pCaps->Caps3 = D3DCAPS3_ALPHA_FULLSCREEN_FLIP_OR_DISCARD;
    pCaps->PresentationIntervals = D3DPRESENT_INTERVAL_IMMEDIATE | D3DPRESENT_INTERVAL_ONE;
    pCaps->DevCaps = D3DDEVCAPS_HWTRANSFORMANDLIGHT | D3DDEVCAPS_PUREDEVICE |
                     D3DDEVCAPS_DRAWPRIMITIVES2 | D3DDEVCAPS_DRAWPRIMITIVES2EX;
    pCaps->PrimitiveMiscCaps = D3DPMISCCAPS_MASKZ | D3DPMISCCAPS_CULLNONE |
                               D3DPMISCCAPS_CULLCW | D3DPMISCCAPS_CULLCCW |
                               D3DPMISCCAPS_BLENDOP;
    pCaps->RasterCaps = D3DPRASTERCAPS_DITHER | D3DPRASTERCAPS_ZTEST | D3DPRASTERCAPS_SCISSORTEST;
    pCaps->ZCmpCaps = 0xFF;
    pCaps->SrcBlendCaps = 0x1FF;
    pCaps->DestBlendCaps = 0x1FF;
    pCaps->AlphaCmpCaps = 0xFF;
    pCaps->ShadeCaps = D3DPSHADECAPS_COLORGOURAUDRGB;
    pCaps->TextureCaps = D3DPTEXTURECAPS_PERSPECTIVE | D3DPTEXTURECAPS_ALPHA |
                         D3DPTEXTURECAPS_POW2 | D3DPTEXTURECAPS_CUBEMAP | D3DPTEXTURECAPS_MIPMAP;
    pCaps->TextureFilterCaps = 0x3F;
    pCaps->CubeTextureFilterCaps = 0x3F;
    pCaps->VolumeTextureFilterCaps = 0x3F;
    pCaps->TextureAddressCaps = 0x3F;
    pCaps->VolumeTextureAddressCaps = 0x3F;
    pCaps->LineCaps = 0x1F;
    pCaps->MaxTextureWidth = 16384;
    pCaps->MaxTextureHeight = 16384;
    pCaps->MaxVolumeExtent = 2048;
    pCaps->MaxTextureRepeat = 8192;
    pCaps->MaxTextureAspectRatio = 16384;
    pCaps->MaxAnisotropy = 16;
    pCaps->MaxVertexW = 1e9f;
    pCaps->GuardBandLeft = -32768.0f;
    pCaps->GuardBandTop = -32768.0f;
    pCaps->GuardBandRight = 32767.0f;
    pCaps->GuardBandBottom = 32767.0f;
    pCaps->StencilCaps = 0xFF;
    pCaps->FVFCaps = D3DFVFCAPS_DONOTSTRIPELEMENTS | 8;
    pCaps->TextureOpCaps = 0xFFFFFFFF;
    pCaps->MaxTextureBlendStages = 8;
    pCaps->MaxSimultaneousTextures = 8;
    pCaps->VertexProcessingCaps = D3DVTXPCAPS_TEXGEN | D3DVTXPCAPS_LOCALVIEWER;
    pCaps->MaxActiveLights = 8;
    pCaps->MaxUserClipPlanes = 6;
    pCaps->MaxVertexBlendMatrices = 4;
    pCaps->MaxPointSize = 256.0f;
    pCaps->MaxPrimitiveCount = 0x555555;
    pCaps->MaxVertexIndex = 0xFFFFFF;
    pCaps->MaxStreams = 16;
    pCaps->MaxStreamStride = 508;
    pCaps->VertexShaderVersion = D3DVS_VERSION(3, 0);
    pCaps->PixelShaderVersion = D3DPS_VERSION(3, 0);
    pCaps->MaxVertexShaderConst = 256;
    pCaps->MaxVertexShader30InstructionSlots = 32768;
    pCaps->MaxPixelShader30InstructionSlots = 32768;
}

class MSD3DDevice9 : public IDirect3DDevice9 {
    LONG m_ref = 1;
    IDirect3D9* m_parent;
public:
    MSD3DDevice9(IDirect3D9* parent) : m_parent(parent) {}
    HRESULT WINAPI QueryInterface(REFIID, void** ppv) override { if(ppv)*ppv=nullptr; return E_NOINTERFACE; }
    ULONG WINAPI AddRef() override { return ref_add(&m_ref); }
    ULONG WINAPI Release() override { ULONG r = ref_sub(&m_ref); return r ? r : 0; }
    HRESULT WINAPI TestCooperativeLevel() override { return D3D_OK; }
    UINT WINAPI GetAvailableTextureMem() override { return 512*1024*1024; }
    HRESULT WINAPI EvictManagedResources() override { return S_OK; }
    HRESULT WINAPI GetDirect3D(IDirect3D9** pp) override { if(pp)*pp=m_parent; return S_OK; }
    HRESULT WINAPI GetDeviceCaps(D3DCAPS9* p) override { if(!p) return E_POINTER; fill_caps(p); return S_OK; }
    HRESULT WINAPI GetDisplayMode(UINT, D3DDISPLAYMODE* p) override {
        if(p){p->Width=1920;p->Height=1080;p->RefreshRate=60;p->Format=D3DFMT_X8R8G8B8;} return S_OK;
    }
    HRESULT WINAPI GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* p) override { if(p)memset(p,0,sizeof(*p)); return S_OK; }
    HRESULT WINAPI SetCursorProperties(UINT,UINT,IDirect3DSurface9*) override { return S_OK; }
    void WINAPI SetCursorPosition(int,int,DWORD) override {}
    BOOL WINAPI ShowCursor(BOOL) override { return FALSE; }
    HRESULT WINAPI CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS*,IDirect3DSwapChain9**) override { return E_NOTIMPL; }
    HRESULT WINAPI GetSwapChain(UINT,IDirect3DSwapChain9**) override { return E_NOTIMPL; }
    UINT WINAPI GetNumberOfSwapChains() override { return 1; }
    HRESULT WINAPI Reset(D3DPRESENT_PARAMETERS* pParams) override {
        if(!pParams) return E_POINTER;
        struct d3d9_reset_params p = {};
        p.width=pParams->BackBufferWidth; p.height=pParams->BackBufferHeight;
        p.back_buffer_format=(uint32_t)pParams->BackBufferFormat; p.windowed=pParams->Windowed;
        p.back_buffer_count=pParams->BackBufferCount; p.enable_auto_depth_stencil=pParams->EnableAutoDepthStencil;
        p.auto_depth_stencil_format=(uint32_t)pParams->AutoDepthStencilFormat;
        unix_call(D3D9_FUNC_RESET, &p); return S_OK;
    }
    HRESULT WINAPI Present(const RECT*, const RECT*, HWND, const RGNDATA*) override { unix_call(D3D9_FUNC_PRESENT, nullptr); return S_OK; }
    HRESULT WINAPI GetBackBuffer(UINT,UINT,D3DBACKBUFFER_TYPE,IDirect3DSurface9** pp) override { if(pp)*pp=new MSD3DSurface(); return S_OK; }
    HRESULT WINAPI GetRasterStatus(UINT,D3DRASTER_STATUS*) override { return E_NOTIMPL; }
    HRESULT WINAPI SetDialogBoxMode(BOOL) override { return S_OK; }
    void WINAPI SetGammaRamp(UINT,DWORD,const D3DGAMMARAMP*) override {}
    void WINAPI GetGammaRamp(UINT,D3DGAMMARAMP*) override {}
    HRESULT WINAPI CreateTexture(UINT W,UINT H,UINT L,DWORD U,D3DFORMAT F,D3DPOOL P,IDirect3DTexture9** pp,HANDLE*) override {
        if(!pp) return S_OK;
        auto* t = new MSD3DTexture();
        struct d3d9_create_texture_params p = {W,H,L,(uint32_t)U,(uint32_t)F,(int)P,0};
        NTSTATUS r = unix_call(D3D9_FUNC_CREATE_TEXTURE, &p);
        if(r>=0) set_handle(t, p.out_handle);
        *pp = t; return S_OK;
    }
    HRESULT WINAPI CreateVolumeTexture(UINT,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DVolumeTexture9**,HANDLE*) override { return E_NOTIMPL; }
    HRESULT WINAPI CreateCubeTexture(UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DCubeTexture9**,HANDLE*) override { return E_NOTIMPL; }
    HRESULT WINAPI CreateVertexBuffer(UINT L,DWORD U,DWORD F,D3DPOOL P,IDirect3DVertexBuffer9** pp,HANDLE*) override {
        if(!pp) return S_OK;
        auto* b = new MSD3DVertexBuffer();
        b->m_lock_size = L;
        b->m_lock_ptr = new char[L];
        fprintf(stderr, "[d3d9] CreateVertexBuffer: %u bytes, lock_ptr=%p\n", L, b->m_lock_ptr);
        struct d3d9_create_buffer_params p = {L,(uint32_t)U,F,(int)P,0};
        NTSTATUS r = unix_call(D3D9_FUNC_CREATE_VERTEX_BUFFER, &p);
        if(r>=0) set_handle(b, p.out_handle);
        *pp = b; return S_OK;
    }
    HRESULT WINAPI CreateIndexBuffer(UINT L,DWORD U,D3DFORMAT,D3DPOOL P,IDirect3DIndexBuffer9** pp,HANDLE*) override {
        if(!pp) return S_OK;
        auto* b = new MSD3DIndexBuffer();
        b->m_lock_size = L;
        b->m_lock_ptr = new char[L];
        struct d3d9_create_buffer_params p = {L,(uint32_t)U,0,(int)P,0};
        NTSTATUS r = unix_call(D3D9_FUNC_CREATE_INDEX_BUFFER, &p);
        if(r>=0) set_handle(b, p.out_handle);
        *pp = b; return S_OK;
    }
    HRESULT WINAPI CreateRenderTarget(UINT W,UINT H,D3DFORMAT F,D3DMULTISAMPLE_TYPE MS,DWORD MQ,BOOL,IDirect3DSurface9** pp,HANDLE*) override {
        if(!pp) return S_OK;
        auto* s = new MSD3DSurface();
        struct d3d9_create_surface_params p = {W,H,(uint32_t)F,(uint32_t)MS,MQ,0,0};
        NTSTATUS r = unix_call(D3D9_FUNC_CREATE_RENDER_TARGET, &p);
        if(r>=0) set_handle(s, p.out_handle);
        *pp = s; return S_OK;
    }
    HRESULT WINAPI CreateDepthStencilSurface(UINT W,UINT H,D3DFORMAT F,D3DMULTISAMPLE_TYPE MS,DWORD MQ,BOOL,IDirect3DSurface9** pp,HANDLE*) override {
        if(!pp) return S_OK;
        auto* s = new MSD3DSurface();
        struct d3d9_create_surface_params p = {W,H,(uint32_t)F,(uint32_t)MS,MQ,0,0};
        NTSTATUS r = unix_call(D3D9_FUNC_CREATE_DEPTH_STENCIL, &p);
        if(r>=0) set_handle(s, p.out_handle);
        *pp = s; return S_OK;
    }
    HRESULT WINAPI UpdateSurface(IDirect3DSurface9*,const RECT*,IDirect3DSurface9*,const POINT*) override { return S_OK; }
    HRESULT WINAPI UpdateTexture(IDirect3DBaseTexture9*,IDirect3DBaseTexture9*) override { return S_OK; }
    HRESULT WINAPI GetRenderTargetData(IDirect3DSurface9*,IDirect3DSurface9*) override { return S_OK; }
    HRESULT WINAPI GetFrontBufferData(UINT,IDirect3DSurface9*) override { return S_OK; }
    HRESULT WINAPI StretchRect(IDirect3DSurface9*,const RECT*,IDirect3DSurface9*,const RECT*,D3DTEXTUREFILTERTYPE) override { return S_OK; }
    HRESULT WINAPI ColorFill(IDirect3DSurface9*,const RECT*,D3DCOLOR) override { return S_OK; }
    HRESULT WINAPI CreateOffscreenPlainSurface(UINT,UINT,D3DFORMAT,D3DPOOL,IDirect3DSurface9** pp,HANDLE*) override { if(pp)*pp=new MSD3DSurface(); return S_OK; }
    HRESULT WINAPI SetRenderTarget(DWORD idx,IDirect3DSurface9* s) override {
        uint64_t h[2] = {(uint64_t)idx, obj_handle(s)}; unix_call(D3D9_FUNC_SET_RENDER_TARGET, h); return S_OK;
    }
    HRESULT WINAPI GetRenderTarget(DWORD,IDirect3DSurface9** pp) override { if(pp)*pp=nullptr; return D3DERR_NOTFOUND; }
    HRESULT WINAPI SetDepthStencilSurface(IDirect3DSurface9* s) override { uint64_t h=obj_handle(s); unix_call(D3D9_FUNC_SET_DEPTH_STENCIL,&h); return S_OK; }
    HRESULT WINAPI GetDepthStencilSurface(IDirect3DSurface9** pp) override { if(pp)*pp=nullptr; return D3DERR_NOTFOUND; }
    HRESULT WINAPI BeginScene() override { unix_call(D3D9_FUNC_BEGIN_SCENE, nullptr); return S_OK; }
    HRESULT WINAPI EndScene() override { unix_call(D3D9_FUNC_END_SCENE, nullptr); return S_OK; }
    HRESULT WINAPI Clear(DWORD,const D3DRECT*,DWORD Flags,D3DCOLOR Color,float Z,DWORD Stencil) override {
        struct d3d9_clear_params p = {(uint32_t)Flags,(uint32_t)Color,Z,(uint32_t)Stencil};
        unix_call(D3D9_FUNC_CLEAR, &p); return S_OK;
    }
    HRESULT WINAPI SetTransform(D3DTRANSFORMSTATETYPE,const D3DMATRIX*) override { return S_OK; }
    HRESULT WINAPI GetTransform(D3DTRANSFORMSTATETYPE,D3DMATRIX*) override { return E_NOTIMPL; }
    HRESULT WINAPI MultiplyTransform(D3DTRANSFORMSTATETYPE,const D3DMATRIX*) override { return S_OK; }
    HRESULT WINAPI SetViewport(const D3DVIEWPORT9* vp) override {
        if(!vp) return E_POINTER;
        struct d3d9_viewport v = {vp->X,vp->Y,vp->Width,vp->Height,vp->MinZ,vp->MaxZ};
        unix_call(D3D9_FUNC_SET_VIEWPORT, &v); return S_OK;
    }
    HRESULT WINAPI GetViewport(D3DVIEWPORT9* vp) override { if(vp)memset(vp,0,sizeof(*vp)); return S_OK; }
    HRESULT WINAPI SetMaterial(const D3DMATERIAL9*) override { return S_OK; }
    HRESULT WINAPI GetMaterial(D3DMATERIAL9*) override { return E_NOTIMPL; }
    HRESULT WINAPI SetLight(DWORD,const D3DLIGHT9*) override { return S_OK; }
    HRESULT WINAPI GetLight(DWORD,D3DLIGHT9*) override { return E_NOTIMPL; }
    HRESULT WINAPI LightEnable(DWORD,BOOL) override { return S_OK; }
    HRESULT WINAPI GetLightEnable(DWORD,BOOL*) override { return E_NOTIMPL; }
    HRESULT WINAPI SetClipPlane(DWORD,const float*) override { return S_OK; }
    HRESULT WINAPI GetClipPlane(DWORD,float*) override { return E_NOTIMPL; }
    HRESULT WINAPI SetRenderState(D3DRENDERSTATETYPE s,DWORD v) override {
        struct d3d9_set_render_state_params p = {(uint32_t)s,(uint32_t)v}; unix_call(D3D9_FUNC_SET_RENDER_STATE,&p); return S_OK;
    }
    HRESULT WINAPI GetRenderState(D3DRENDERSTATETYPE,DWORD* p) override { if(p)*p=0; return S_OK; }
    HRESULT WINAPI CreateStateBlock(D3DSTATEBLOCKTYPE,IDirect3DStateBlock9** pp) override { if(pp)*pp=new MSD3DStateBlock(); return S_OK; }
    HRESULT WINAPI BeginStateBlock() override { return S_OK; }
    HRESULT WINAPI EndStateBlock(IDirect3DStateBlock9** pp) override { if(pp)*pp=new MSD3DStateBlock(); return S_OK; }
    HRESULT WINAPI SetClipStatus(const D3DCLIPSTATUS9*) override { return S_OK; }
    HRESULT WINAPI GetClipStatus(D3DCLIPSTATUS9*) override { return E_NOTIMPL; }
    HRESULT WINAPI GetTexture(DWORD,IDirect3DBaseTexture9** pp) override { if(pp)*pp=nullptr; return S_OK; }
    HRESULT WINAPI SetTexture(DWORD Stage,IDirect3DBaseTexture9* t) override {
        struct d3d9_set_texture_params p = {(uint32_t)Stage,obj_handle(t)}; unix_call(D3D9_FUNC_SET_TEXTURE,&p); return S_OK;
    }
    HRESULT WINAPI GetTextureStageState(DWORD,D3DTEXTURESTAGESTATETYPE,DWORD*) override { return E_NOTIMPL; }
    HRESULT WINAPI SetTextureStageState(DWORD,D3DTEXTURESTAGESTATETYPE,DWORD) override { return S_OK; }
    HRESULT WINAPI GetSamplerState(DWORD,D3DSAMPLERSTATETYPE,DWORD*) override { return E_NOTIMPL; }
    HRESULT WINAPI SetSamplerState(DWORD s,D3DSAMPLERSTATETYPE t,DWORD v) override {
        struct d3d9_set_sampler_state_params p = {(uint32_t)s,(uint32_t)t,(uint32_t)v}; unix_call(D3D9_FUNC_SET_SAMPLER_STATE,&p); return S_OK;
    }
    HRESULT WINAPI ValidateDevice(DWORD*) override { return S_OK; }
    HRESULT WINAPI SetPaletteEntries(UINT,const PALETTEENTRY*) override { return S_OK; }
    HRESULT WINAPI GetPaletteEntries(UINT,PALETTEENTRY*) override { return E_NOTIMPL; }
    HRESULT WINAPI SetCurrentTexturePalette(UINT) override { return S_OK; }
    HRESULT WINAPI GetCurrentTexturePalette(UINT*) override { return E_NOTIMPL; }
    HRESULT WINAPI SetScissorRect(const RECT* r) override {
        if(!r) return E_POINTER;
        struct d3d9_scissor_rect s = {r->left,r->top,r->right,r->bottom};
        unix_call(D3D9_FUNC_SET_SCISSOR_RECT, &s); return S_OK;
    }
    HRESULT WINAPI GetScissorRect(RECT* r) override { if(r)memset(r,0,sizeof(*r)); return S_OK; }
    HRESULT WINAPI SetSoftwareVertexProcessing(BOOL) override { return S_OK; }
    BOOL WINAPI GetSoftwareVertexProcessing() override { return FALSE; }
    HRESULT WINAPI SetNPatchMode(float) override { return S_OK; }
    float WINAPI GetNPatchMode() override { return 0.0f; }
    HRESULT WINAPI DrawPrimitive(D3DPRIMITIVETYPE pt,UINT sv,UINT pc) override {
        struct d3d9_draw_params p = {(uint32_t)pt,sv,pc}; unix_call(D3D9_FUNC_DRAW_PRIMITIVE,&p); return S_OK;
    }
    HRESULT WINAPI DrawIndexedPrimitive(D3DPRIMITIVETYPE pt,INT bvi,UINT mvi,UINT nv,UINT si,UINT pc) override {
        struct d3d9_draw_indexed_params p = {(uint32_t)pt,bvi,mvi,nv,si,pc};
        unix_call(D3D9_FUNC_DRAW_INDEXED_PRIMITIVE,&p); return S_OK;
    }
    HRESULT WINAPI DrawPrimitiveUP(D3DPRIMITIVETYPE,UINT,const void*,UINT) override { return E_NOTIMPL; }
    HRESULT WINAPI DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE,UINT,UINT,UINT,const void*,D3DFORMAT,const void*,UINT) override { return E_NOTIMPL; }
    HRESULT WINAPI ProcessVertices(UINT,UINT,UINT,IDirect3DVertexBuffer9*,IDirect3DVertexDeclaration9*,DWORD) override { return E_NOTIMPL; }
    HRESULT WINAPI CreateVertexDeclaration(const D3DVERTEXELEMENT9* els,IDirect3DVertexDeclaration9** pp) override {
        if(!pp) return S_OK;
        auto* d = new MSD3DVertexDeclaration();
        struct d3d9_create_vertex_decl_params p = {}; p.num_elements = 0;
        struct d3d9_vertex_element elems[64];
        if(els) {
            for(UINT i=0;i<64;i++) {
                if(els[i].Stream==0xFF && els[i].Type==D3DDECLTYPE_UNUSED) break;
                elems[i].stream=els[i].Stream; elems[i].offset=els[i].Offset;
                elems[i].type=els[i].Type; elems[i].method=els[i].Method;
                elems[i].usage=els[i].Usage; elems[i].usage_index=els[i].UsageIndex;
                p.num_elements++;
            }
            p.elements = (uint64_t)(uintptr_t)elems;
        }
        NTSTATUS r = unix_call(D3D9_FUNC_CREATE_VERTEX_DECLARATION, &p);
        if(r>=0) set_handle(d, p.out_handle);
        *pp = d; return S_OK;
    }
    HRESULT WINAPI SetVertexDeclaration(IDirect3DVertexDeclaration9* d) override { uint64_t h=obj_handle(d); unix_call(D3D9_FUNC_SET_VERTEX_DECLARATION,&h); return S_OK; }
    HRESULT WINAPI GetVertexDeclaration(IDirect3DVertexDeclaration9** pp) override { if(pp)*pp=nullptr; return D3DERR_NOTFOUND; }
    HRESULT WINAPI SetFVF(DWORD fvf) override {
        D3DVERTEXELEMENT9 elems[16] = {};
        UINT n = 0;
        WORD offset = 0;
        if (fvf & D3DFVF_XYZ) {
            elems[n] = {0, offset, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0};
            offset += 12; n++;
        } else if (fvf & D3DFVF_XYZRHW) {
            elems[n] = {0, offset, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITIONT, 0};
            offset += 16; n++;
        } else if (fvf & D3DFVF_XYZB1) {
            elems[n] = {0, offset, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0};
            offset += 16; n++;
        }
        if (fvf & D3DFVF_NORMAL) {
            elems[n] = {0, offset, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0};
            offset += 12; n++;
        }
        if (fvf & D3DFVF_PSIZE) {
            elems[n] = {0, offset, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_PSIZE, 0};
            offset += 4; n++;
        }
        if (fvf & D3DFVF_DIFFUSE) {
            elems[n] = {0, offset, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0};
            offset += 4; n++;
        }
        if (fvf & D3DFVF_SPECULAR) {
            elems[n] = {0, offset, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 1};
            offset += 4; n++;
        }
        UINT numTexCoords = (fvf >> 8) & 0xF;
        D3DDECLTYPE texTypes[] = {D3DDECLTYPE_FLOAT1, D3DDECLTYPE_FLOAT2, D3DDECLTYPE_FLOAT3, D3DDECLTYPE_FLOAT4};
        WORD texSizes[] = {4, 8, 12, 16};
        for (UINT i = 0; i < numTexCoords && n < 15; i++) {
            UINT dim = (fvf >> (16 + i * 2)) & 0x3;
            elems[n] = {0, offset, texTypes[dim], D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, (BYTE)i};
            offset += texSizes[dim]; n++;
        }
        elems[n] = D3DDECL_END();
        IDirect3DVertexDeclaration9* decl = nullptr;
        HRESULT hr = CreateVertexDeclaration(elems, &decl);
        if (FAILED(hr)) return hr;
        hr = SetVertexDeclaration(decl);
        if (decl) decl->Release();
        return hr;
    }
    HRESULT WINAPI GetFVF(DWORD*) override { return E_NOTIMPL; }
    HRESULT WINAPI CreateVertexShader(const DWORD* fn,IDirect3DVertexShader9** pp) override {
        if(!pp) return S_OK;
        auto* s = new MSD3DVertexShader();
        struct d3d9_create_shader_params p = {};
        if(fn) {
            const uint8_t* ptr=(const uint8_t*)fn; uint32_t sz=0;
            if(ptr[0]=='D'&&ptr[1]=='X'&&ptr[2]=='B'&&ptr[3]=='C') { sz=*(const uint32_t*)(ptr+16); }
            else { while(*(const uint32_t*)(ptr+sz)!=0x0000FFFF && sz<65536) sz+=4; sz+=4; }
            p.bytecode=(uint64_t)(uintptr_t)fn; p.bytecode_size=sz;
        }
        NTSTATUS r = unix_call(D3D9_FUNC_CREATE_VERTEX_SHADER, &p);
        if(r>=0) set_handle(s, p.out_handle);
        *pp = s; return S_OK;
    }
    HRESULT WINAPI SetVertexShader(IDirect3DVertexShader9* s) override { uint64_t h=obj_handle(s); unix_call(D3D9_FUNC_SET_VERTEX_SHADER,&h); return S_OK; }
    HRESULT WINAPI GetVertexShader(IDirect3DVertexShader9** pp) override { if(pp)*pp=nullptr; return D3DERR_NOTFOUND; }
    HRESULT WINAPI SetVertexShaderConstantF(UINT sr,const float* d,UINT c) override {
        struct d3d9_set_constants_f_params p = {}; p.start_register=sr; p.count=c*4;
        if(p.count>256) p.count=256; if(d) memcpy(p.values,d,p.count*sizeof(float));
        unix_call(D3D9_FUNC_SET_VS_CONSTANTS_F,&p); return S_OK;
    }
    HRESULT WINAPI GetVertexShaderConstantF(UINT,float*,UINT) override { return E_NOTIMPL; }
    HRESULT WINAPI SetVertexShaderConstantI(UINT,const int*,UINT) override { return S_OK; }
    HRESULT WINAPI GetVertexShaderConstantI(UINT,int*,UINT) override { return E_NOTIMPL; }
    HRESULT WINAPI SetVertexShaderConstantB(UINT,const BOOL*,UINT) override { return S_OK; }
    HRESULT WINAPI GetVertexShaderConstantB(UINT,BOOL*,UINT) override { return E_NOTIMPL; }
    HRESULT WINAPI SetStreamSource(UINT sn,IDirect3DVertexBuffer9* b,UINT off,UINT stride) override {
        struct d3d9_set_stream_source_params p = {sn,obj_handle(b),off,stride};
        unix_call(D3D9_FUNC_SET_STREAM_SOURCE,&p); return S_OK;
    }
    HRESULT WINAPI GetStreamSource(UINT,IDirect3DVertexBuffer9**,UINT*,UINT*) override { return E_NOTIMPL; }
    HRESULT WINAPI SetStreamSourceFreq(UINT,UINT) override { return S_OK; }
    HRESULT WINAPI GetStreamSourceFreq(UINT,UINT*) override { return E_NOTIMPL; }
    HRESULT WINAPI SetIndices(IDirect3DIndexBuffer9* b) override { uint64_t h=obj_handle(b); unix_call(D3D9_FUNC_SET_INDICES,&h); return S_OK; }
    HRESULT WINAPI GetIndices(IDirect3DIndexBuffer9** pp) override { if(pp)*pp=nullptr; return D3DERR_NOTFOUND; }
    HRESULT WINAPI CreatePixelShader(const DWORD* fn,IDirect3DPixelShader9** pp) override {
        if(!pp) return S_OK;
        auto* s = new MSD3DPixelShader();
        struct d3d9_create_shader_params p = {};
        if(fn) {
            const uint8_t* ptr=(const uint8_t*)fn; uint32_t sz=0;
            if(ptr[0]=='D'&&ptr[1]=='X'&&ptr[2]=='B'&&ptr[3]=='C') { sz=*(const uint32_t*)(ptr+16); }
            else { while(*(const uint32_t*)(ptr+sz)!=0x0000FFFF && sz<65536) sz+=4; sz+=4; }
            p.bytecode=(uint64_t)(uintptr_t)fn; p.bytecode_size=sz;
        }
        NTSTATUS r = unix_call(D3D9_FUNC_CREATE_PIXEL_SHADER, &p);
        if(r>=0) set_handle(s, p.out_handle);
        *pp = s; return S_OK;
    }
    HRESULT WINAPI SetPixelShader(IDirect3DPixelShader9* s) override { uint64_t h=obj_handle(s); unix_call(D3D9_FUNC_SET_PIXEL_SHADER,&h); return S_OK; }
    HRESULT WINAPI GetPixelShader(IDirect3DPixelShader9** pp) override { if(pp)*pp=nullptr; return D3DERR_NOTFOUND; }
    HRESULT WINAPI SetPixelShaderConstantF(UINT sr,const float* d,UINT c) override {
        struct d3d9_set_constants_f_params p = {}; p.start_register=sr; p.count=c*4;
        if(p.count>256) p.count=256; if(d) memcpy(p.values,d,p.count*sizeof(float));
        unix_call(D3D9_FUNC_SET_PS_CONSTANTS_F,&p); return S_OK;
    }
    HRESULT WINAPI GetPixelShaderConstantF(UINT,float*,UINT) override { return E_NOTIMPL; }
    HRESULT WINAPI SetPixelShaderConstantI(UINT,const int*,UINT) override { return S_OK; }
    HRESULT WINAPI GetPixelShaderConstantI(UINT,int*,UINT) override { return E_NOTIMPL; }
    HRESULT WINAPI SetPixelShaderConstantB(UINT,const BOOL*,UINT) override { return S_OK; }
    HRESULT WINAPI GetPixelShaderConstantB(UINT,BOOL*,UINT) override { return E_NOTIMPL; }
    HRESULT WINAPI DrawRectPatch(UINT,const float*,const D3DRECTPATCH_INFO*) override { return E_NOTIMPL; }
    HRESULT WINAPI DrawTriPatch(UINT,const float*,const D3DTRIPATCH_INFO*) override { return E_NOTIMPL; }
    HRESULT WINAPI DeletePatch(UINT) override { return S_OK; }
    HRESULT WINAPI CreateQuery(D3DQUERYTYPE,IDirect3DQuery9**) override { return E_NOTIMPL; }
};

static MSD3DDevice9* g_device = nullptr;

class MSD3D9 : public IDirect3D9 {
    LONG m_ref = 1;
public:
    HRESULT WINAPI QueryInterface(REFIID, void** ppv) override { if(ppv)*ppv=nullptr; return E_NOINTERFACE; }
    ULONG WINAPI AddRef() override { return ref_add(&m_ref); }
    ULONG WINAPI Release() override { ULONG r = ref_sub(&m_ref); return r ? r : 0; }
    HRESULT WINAPI RegisterSoftwareDevice(void*) override { return S_OK; }
    UINT WINAPI GetAdapterCount() override { return 1; }
    HRESULT WINAPI GetAdapterIdentifier(UINT,DWORD,D3DADAPTER_IDENTIFIER9* p) override {
        if(!p) return E_POINTER; memset(p,0,sizeof(*p));
        strncpy(p->Driver,"metalsharp_d3d9",sizeof(p->Driver)-1);
        strncpy(p->Description,"MetalSharp D3D9",sizeof(p->Description)-1);
        p->VendorId=0x106B; p->DeviceId=1; p->WHQLLevel=1; return S_OK;
    }
    UINT WINAPI GetAdapterModeCount(UINT,D3DFORMAT) override { return 1; }
    HRESULT WINAPI EnumAdapterModes(UINT,D3DFORMAT,UINT,D3DDISPLAYMODE* p) override {
        if(p){p->Width=1920;p->Height=1080;p->RefreshRate=60;p->Format=D3DFMT_X8R8G8B8;} return S_OK;
    }
    HRESULT WINAPI GetAdapterDisplayMode(UINT,D3DDISPLAYMODE* p) override {
        if(p){p->Width=1920;p->Height=1080;p->RefreshRate=60;p->Format=D3DFMT_X8R8G8B8;} return S_OK;
    }
    HRESULT WINAPI CheckDeviceType(UINT,D3DDEVTYPE,D3DFORMAT,D3DFORMAT,BOOL) override { return D3D_OK; }
    HRESULT WINAPI CheckDeviceFormat(UINT,D3DDEVTYPE,D3DFORMAT,DWORD,D3DRESOURCETYPE,D3DFORMAT) override { return D3D_OK; }
    HRESULT WINAPI CheckDeviceMultiSampleType(UINT,D3DDEVTYPE,D3DFORMAT,BOOL,D3DMULTISAMPLE_TYPE,DWORD*) override { return D3D_OK; }
    HRESULT WINAPI CheckDepthStencilMatch(UINT,D3DDEVTYPE,D3DFORMAT,D3DFORMAT,D3DFORMAT) override { return D3D_OK; }
    HRESULT WINAPI CheckDeviceFormatConversion(UINT,D3DDEVTYPE,D3DFORMAT,D3DFORMAT) override { return D3D_OK; }
    HRESULT WINAPI GetDeviceCaps(UINT,D3DDEVTYPE,D3DCAPS9* p) override { if(!p) return E_POINTER; fill_caps(p); return S_OK; }
    HMONITOR WINAPI GetAdapterMonitor(UINT) override { return (HMONITOR)1; }
    HRESULT WINAPI CreateDevice(UINT,D3DDEVTYPE,HWND hw,DWORD,D3DPRESENT_PARAMETERS* pp,IDirect3DDevice9** ppd) override {
        fprintf(stderr, "[d3d9] CreateDevice called\n");
        if(!ppd) return E_POINTER;
        if(!__wine_unixlib_handle) return E_FAIL;
        fprintf(stderr, "[d3d9] calling D3D9_FUNC_INIT...\n");
        NTSTATUS r = unix_call(D3D9_FUNC_INIT, nullptr);
        fprintf(stderr, "[d3d9] D3D9_FUNC_INIT returned %ld\n", (long)r);
        if(r<0) return E_FAIL;
        struct d3d9_create_device_params p = {};
        if(pp) {
            p.width=pp->BackBufferWidth; p.height=pp->BackBufferHeight;
            p.back_buffer_format=(uint32_t)pp->BackBufferFormat; p.windowed=pp->Windowed;
            p.back_buffer_count=pp->BackBufferCount; p.enable_auto_depth_stencil=pp->EnableAutoDepthStencil;
            p.auto_depth_stencil_format=(uint32_t)pp->AutoDepthStencilFormat;
            p.presentation_interval=(uint32_t)pp->PresentationInterval;
            p.multisample_type=(uint32_t)pp->MultiSampleType; p.multisample_quality=pp->MultiSampleQuality;
        } else { p.width=1280; p.height=720; p.windowed=1; p.back_buffer_count=1; p.back_buffer_format=22; }
        p.hwnd=(uint64_t)(uintptr_t)hw;
        fprintf(stderr, "[d3d9] calling D3D9_FUNC_CREATE_DEVICE...\n");
        r = unix_call(D3D9_FUNC_CREATE_DEVICE, &p);
        if(r<0) return E_FAIL;
        auto* dev = new MSD3DDevice9(this);
        g_device = dev;
        *ppd = dev; return S_OK;
    }
};

static MSD3D9 g_d3d9_impl;

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if(reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        NTSTATUS s = __wine_init_unix_call();
        if (s < 0) {
            fprintf(stderr, "[d3d9] __wine_init_unix_call failed: 0x%lx\n", (long)s);
            return FALSE;
        }
        fprintf(stderr, "[d3d9] init OK handle=%llu\n", (unsigned long long)__wine_unixlib_handle);
    }
    return TRUE;
}

extern "C" {

__declspec(dllexport)
IDirect3D9* WINAPI Direct3DCreate9(UINT SDKVersion) {
    fprintf(stderr, "[d3d9] Direct3DCreate9(%u)\n", SDKVersion);
    return &g_d3d9_impl;
}

}
