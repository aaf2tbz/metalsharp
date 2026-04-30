#include "metalsharp_unix.h"
#include <wine/debug.h>

WINE_DEFAULT_DEBUG_CHANNEL(metalsharp);

static ms_unix_call_func unix_call = NULL;

static BOOL init_unix_lib(void) {
    if (unix_call) return TRUE;

    HMODULE mod = GetModuleHandleA("metalsharp_unix.dll");
    if (!mod) return FALSE;

    unix_call = (ms_unix_call_func)GetProcAddress(mod, "__wine_unix_call_funcs");
    if (!unix_call) return FALSE;

    unix_call(MS_FUNC_INIT, NULL);
    return TRUE;
}

struct ID3D11DeviceVtbl;
struct ID3D11DeviceContextVtbl;
struct IDXGISwapChainVtbl;
struct IDXGIFactoryVtbl;
struct IDXGIAdapterVtbl;

typedef struct ID3D11Device {
    const struct ID3D11DeviceVtbl* lpVtbl;
} ID3D11Device;

typedef struct ID3D11DeviceContext {
    const struct ID3D11DeviceContextVtbl* lpVtbl;
} ID3D11DeviceContext;

typedef struct IDXGISwapChain {
    const struct IDXGISwapChainVtbl* lpVtbl;
} IDXGISwapChain;

typedef struct IDXGIFactory {
    const struct IDXGIFactoryVtbl* lpVtbl;
} IDXGIFactory;

typedef struct IDXGIAdapter {
    const struct IDXGIAdapterVtbl* lpVtbl;
} IDXGIAdapter;

static ULONG STDMETHODCALLTYPE device_AddRef(ID3D11Device* This) { return 1; }
static ULONG STDMETHODCALLTYPE device_Release(ID3D11Device* This) { return 0; }
static HRESULT STDMETHODCALLTYPE device_QueryInterface(ID3D11Device* This, REFIID riid, void** ppv) {
    *ppv = NULL;
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE device_CheckFeatureSupport(ID3D11Device* This, int Feature, void* pFeatureSupportData, UINT FeatureSupportDataSize) {
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE device_CreateTexture2D(ID3D11Device* This, const void* pDesc, const void* pInitialData, void** ppTexture2D) {
    if (!ppTexture2D) return E_POINTER;
    *ppTexture2D = (void*)0x1;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_CreateBuffer(ID3D11Device* This, const void* pDesc, const void* pInitialData, void** ppBuffer) {
    if (!ppTexture2D) return E_POINTER;
    if (!init_unix_lib()) return E_FAIL;
    struct ms_create_buffer_params params = {0};
    params.initial_data = pInitialData;
    unix_call(MS_FUNC_CREATE_BUFFER, &params);
    *ppBuffer = (void*)0x2;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_CreateVertexShader(ID3D11Device* This, const void* pShaderBytecode, SIZE_T BytecodeLength, void* pClassLinkage, void** ppVertexShader) {
    if (!ppVertexShader) return E_POINTER;
    *ppVertexShader = (void*)0x3;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_CreatePixelShader(ID3D11Device* This, const void* pShaderBytecode, SIZE_T BytecodeLength, void* pClassLinkage, void** ppPixelShader) {
    if (!ppPixelShader) return E_POINTER;
    *ppPixelShader = (void*)0x4;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_CreateInputLayout(ID3D11Device* This, const void* pInputElementDescs, UINT NumElements, const void* pShaderBytecodeWithInputSignature, SIZE_T BytecodeLength, void** ppInputLayout) {
    if (!ppInputLayout) return E_POINTER;
    *ppInputLayout = (void*)0x5;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_CreateRenderTargetView(ID3D11Device* This, void* pResource, const void* pDesc, void** ppRTView) {
    if (!ppRTView) return E_POINTER;
    *ppRTView = (void*)0x6;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_CreateDepthStencilView(ID3D11Device* This, void* pResource, const void* pDesc, void** ppDepthStencilView) {
    if (!ppDepthStencilView) return E_POINTER;
    *ppDepthStencilView = (void*)0x7;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_CreateSamplerState(ID3D11Device* This, const void* pSamplerDesc, void** ppSamplerState) {
    if (!ppSamplerState) return E_POINTER;
    *ppSamplerState = (void*)0x8;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_CreateBlendState(ID3D11Device* This, const void* pBlendStateDesc, void** ppBlendState) {
    if (!ppBlendState) return E_POINTER;
    *ppBlendState = (void*)0x9;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_CreateRasterizerState(ID3D11Device* This, const void* pRasterizerDesc, void** ppRasterizerState) {
    if (!ppRasterizerState) return E_POINTER;
    *ppRasterizerState = (void*)0xA;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_CreateDepthStencilState(ID3D11Device* This, const void* pDepthStencilDesc, void** ppDepthStencilState) {
    if (!ppDepthStencilState) return E_POINTER;
    *ppDepthStencilState = (void*)0xB;
    return S_OK;
}

static void STDMETHODCALLTYPE device_GetImmediateContext(ID3D11Device* This, ID3D11DeviceContext** ppImmediateContext);

static HRESULT STDMETHODCALLTYPE device_CreateShaderResourceView(ID3D11Device* This, void* pResource, const void* pDesc, void** ppSRView) {
    if (!ppSRView) return E_POINTER;
    *ppSRView = (void*)0xC;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_CreateComputeShader(ID3D11Device* This, const void* pShaderBytecode, SIZE_T BytecodeLength, void* pClassLinkage, void** ppComputeShader) {
    if (!ppComputeShader) return E_POINTER;
    *ppComputeShader = (void*)0xD;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_CreateUnorderedAccessView(ID3D11Device* This, void* pResource, const void* pDesc, void** ppUAView) {
    if (!ppUAView) return E_POINTER;
    *ppUAView = (void*)0xE;
    return S_OK;
}

static void STDMETHODCALLTYPE device_GetFeatureLevel(ID3D11Device* This, void* pFeatureLevel) {
    static const int level = 0xb000;
    if (pFeatureLevel) memcpy(pFeatureLevel, &level, sizeof(int));
}

static const struct ID3D11DeviceVtbl device_vtbl = {
    device_QueryInterface,
    device_AddRef,
    device_Release,
    device_GetImmediateContext,
    device_CheckFeatureSupport,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    device_CreateBuffer,
    NULL,
    NULL,
    device_CreateTexture2D,
    NULL,
    NULL,
    device_CreateShaderResourceView,
    device_CreateUnorderedAccessView,
    device_CreateRenderTargetView,
    device_CreateDepthStencilView,
    NULL,
    device_CreateInputLayout,
    device_CreateVertexShader,
    device_CreateGeometryShader,
    NULL,
    NULL,
    device_CreatePixelShader,
    NULL,
    NULL,
    NULL,
    device_CreateComputeShader,
    NULL,
    NULL,
    NULL,
    NULL,
    device_CreateSamplerState,
    NULL,
    device_CreateBlendState,
    device_CreateDepthStencilState,
    device_CreateRasterizerState,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    device_GetFeatureLevel,
};

static ID3D11Device g_device = { &device_vtbl };
static ID3D11DeviceContext g_context;

static void STDMETHODCALLTYPE ctx_QueryInterface(ID3D11DeviceContext* This, REFIID riid, void** ppv) { *ppv = NULL; }
static ULONG STDMETHODCALLTYPE ctx_AddRef(ID3D11DeviceContext* This) { return 1; }
static ULONG STDMETHODCALLTYPE ctx_Release(ID3D11DeviceContext* This) { return 0; }

static void STDMETHODCALLTYPE ctx_IASetVertexBuffers(ID3D11DeviceContext* This, UINT StartSlot, UINT NumBuffers, void* const* ppVertexBuffers, const UINT* pStrides, const UINT* pOffsets) {}
static void STDMETHODCALLTYPE ctx_IASetIndexBuffer(ID3D11DeviceContext* This, void* pBuffer, int Format, UINT Offset) {}
static void STDMETHODCALLTYPE ctx_IASetPrimitiveTopology(ID3D11DeviceContext* This, int Topology) {}
static void STDMETHODCALLTYPE ctx_DrawIndexed(ID3D11DeviceContext* This, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation) {}
static void STDMETHODCALLTYPE ctx_Draw(ID3D11DeviceContext* This, UINT VertexCount, UINT StartVertexLocation) {}
static void STDMETHODCALLTYPE ctx_VSSetShader(ID3D11DeviceContext* This, void* pVertexShader, void* const* ppClassInstances, UINT NumClassInstances) {}
static void STDMETHODCALLTYPE ctx_PSSetShader(ID3D11DeviceContext* This, void* pPixelShader, void* const* ppClassInstances, UINT NumClassInstances) {}
static void STDMETHODCALLTYPE ctx_VSSetConstantBuffers(ID3D11DeviceContext* This, UINT StartSlot, UINT NumBuffers, void* const* ppConstantBuffers) {}
static void STDMETHODCALLTYPE ctx_PSSetConstantBuffers(ID3D11DeviceContext* This, UINT StartSlot, UINT NumBuffers, void* const* ppConstantBuffers) {}
static void STDMETHODCALLTYPE ctx_VSSetShaderResources(ID3D11DeviceContext* This, UINT StartSlot, UINT NumViews, void* const* ppShaderResourceViews) {}
static void STDMETHODCALLTYPE ctx_PSSetShaderResources(ID3D11DeviceContext* This, UINT StartSlot, UINT NumViews, void* const* ppShaderResourceViews) {}
static void STDMETHODCALLTYPE ctx_VSSetSamplers(ID3D11DeviceContext* This, UINT StartSlot, UINT NumSamplers, void* const* ppSamplers) {}
static void STDMETHODCALLTYPE ctx_PSSetSamplers(ID3D11DeviceContext* This, UINT StartSlot, UINT NumSamplers, void* const* ppSamplers) {}
static void STDMETHODCALLTYPE ctx_OMSetRenderTargets(ID3D11DeviceContext* This, UINT NumViews, void* const* ppRenderTargetViews, void* pDepthStencilView) {}
static void STDMETHODCALLTYPE ctx_OMSetBlendState(ID3D11DeviceContext* This, void* pBlendState, const float BlendFactor[4], UINT SampleMask) {}
static void STDMETHODCALLTYPE ctx_OMSetDepthStencilState(ID3D11DeviceContext* This, void* pDepthStencilState, UINT StencilRef) {}
static void STDMETHODCALLTYPE ctx_RSSetState(ID3D11DeviceContext* This, void* pRasterizerState) {}
static void STDMETHODCALLTYPE ctx_RSSetViewports(ID3D11DeviceContext* This, UINT NumViewports, const void* pViewports) {}
static void STDMETHODCALLTYPE ctx_RSSetScissorRects(ID3D11DeviceContext* This, UINT NumRects, const void* pRects) {}
static void STDMETHODCALLTYPE ctx_ClearRenderTargetView(ID3D11DeviceContext* This, void* pRenderTargetView, const float ColorRGBA[4]) {}
static void STDMETHODCALLTYPE ctx_ClearDepthStencilView(ID3D11DeviceContext* This, void* pDepthStencilView, UINT ClearFlags, float Depth, UINT8 Stencil) {}
static void STDMETHODCALLTYPE ctx_UpdateSubresource(ID3D11DeviceContext* This, void* pDstResource, UINT DstSubresource, const void* pDstBox, const void* pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch) {}
static void STDMETHODCALLTYPE ctx_CopyResource(ID3D11DeviceContext* This, void* pDstResource, void* pSrcResource) {}
static void STDMETHODCALLTYPE ctx_Map(ID3D11DeviceContext* This, void* pResource, UINT Subresource, int MapType, UINT MapFlags, void* pMappedResource) {
    memset(pMappedResource, 0, sizeof(struct ms_mapped_subresource));
}
static void STDMETHODCALLTYPE ctx_Unmap(ID3D11DeviceContext* This, void* pResource, UINT Subresource) {}
static void STDMETHODCALLTYPE ctx_Flush(ID3D11DeviceContext* This) {}

static void STDMETHODCALLTYPE device_GetImmediateContext(ID3D11Device* This, ID3D11DeviceContext** ppImmediateContext) {
    if (ppImmediateContext) *ppImmediateContext = &g_context;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)inst; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        init_unix_lib();
    }
    return TRUE;
}

__declspec(dllexport)
HRESULT WINAPI D3D11CreateDeviceAndSwapChain(
    void* pAdapter, int DriverType, HMODULE Software, UINT Flags,
    const void* pFeatureLevels, UINT FeatureLevels, UINT SDKVersion,
    const void* pSwapChainDesc, void** ppSwapChain,
    ID3D11Device** ppDevice, void* pFeatureLevel,
    ID3D11DeviceContext** ppImmediateContext)
{
    TRACE("D3D11CreateDeviceAndSwapChain adapter=%p\n", pAdapter);

    if (!init_unix_lib()) {
        ERR("Failed to initialize unix library\n");
        return E_FAIL;
    }

    struct ms_create_device_params dp = {0};
    dp.driver_type = DriverType;
    dp.flags = Flags;
    dp.sdk_version = SDKVersion;
    unix_call(MS_FUNC_CREATE_DEVICE, &dp);

    if (ppSwapChain && pSwapChainDesc) {
        struct ms_create_swap_chain_params sc = {0};
        const UINT* desc = (const UINT*)pSwapChainDesc;
        sc.hwnd = *(void**)((char*)pSwapChainDesc + 0);
        sc.width = desc[2];
        sc.height = desc[3];
        sc.windowed = desc[8];
        unix_call(MS_FUNC_CREATE_SWAP_CHAIN, &sc);
        *ppSwapChain = (void*)0x10;
    }

    if (ppDevice) *ppDevice = &g_device;
    if (ppImmediateContext) *ppImmediateContext = &g_context;

    return S_OK;
}

__declspec(dllexport)
HRESULT WINAPI D3D11CreateDevice(
    void* pAdapter, int DriverType, HMODULE Software, UINT Flags,
    const void* pFeatureLevels, UINT FeatureLevels, UINT SDKVersion,
    ID3D11Device** ppDevice, void* pFeatureLevel,
    ID3D11DeviceContext** ppImmediateContext)
{
    return D3D11CreateDeviceAndSwapChain(
        pAdapter, DriverType, Software, Flags,
        pFeatureLevels, FeatureLevels, SDKVersion,
        NULL, NULL, ppDevice, pFeatureLevel, ppImmediateContext);
}
