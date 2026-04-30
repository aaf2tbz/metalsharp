#include <d3d11.h>
#include <dxgi.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <windows.h>
#include <unordered_map>
#include <winternl.h>

#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

#include "metalsharp_unix.h"

typedef long NTSTATUS;
typedef unsigned long long unixlib_handle_t;
typedef NTSTATUS (WINAPI *unix_call_dispatcher_t)(unixlib_handle_t, unsigned int, void*);
typedef NTSTATUS (*unixlib_entry_t)(void*);

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0)
#endif

static unixlib_handle_t g_unixlib_handle = 0;
static unix_call_dispatcher_t g_unix_call_dispatcher = nullptr;
static HMODULE g_hModule = nullptr;

static NTSTATUS unix_call(unsigned int code, void* args) {
    if (!g_unix_call_dispatcher || !g_unixlib_handle)
        return (NTSTATUS)0xC000000E;
    return g_unix_call_dispatcher(g_unixlib_handle, code, args);
}

static bool init_unix_call() {
    if (g_unixlib_handle && g_unix_call_dispatcher) return true;

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) { fprintf(stderr, "[metalsharp] no ntdll\n"); return false; }

    typedef NTSTATUS (NTAPI *pNtQVM)(HANDLE, const void*, ULONG, void*, SIZE_T, SIZE_T*);
    pNtQVM pNtQueryVirtualMemory = (pNtQVM)GetProcAddress(ntdll, "NtQueryVirtualMemory");
    if (!pNtQueryVirtualMemory) { fprintf(stderr, "[metalsharp] no NtQueryVirtualMemory\n"); return false; }

    unixlib_handle_t handle = 0;
    NTSTATUS status = pNtQueryVirtualMemory(
        (HANDLE)(LONG_PTR)-1,
        (const void*)g_hModule,
        1000,
        &handle,
        sizeof(handle),
        nullptr
    );
    fprintf(stderr, "[metalsharp] NtQueryVirtualMemory(1000) status=%ld handle=%llu\n",
        status, (unsigned long long)handle);

    if (status < 0) return false;
    g_unixlib_handle = handle;

    void** ptr = (void**)GetProcAddress(ntdll, "__wine_unix_call_dispatcher");
    if (ptr && *ptr) {
        g_unix_call_dispatcher = (unix_call_dispatcher_t)*ptr;
        fprintf(stderr, "[metalsharp] dispatcher resolved at %p\n", (void*)g_unix_call_dispatcher);
    } else {
        fprintf(stderr, "[metalsharp] __wine_unix_call_dispatcher not found, trying direct\n");
        g_unix_call_dispatcher = (unix_call_dispatcher_t)GetProcAddress(ntdll, "__wine_unix_call_dispatcher");
    }

    if (!g_unix_call_dispatcher) return false;

    fprintf(stderr, "[metalsharp] unix call initialized: handle=%llu dispatch=%p\n",
        (unsigned long long)g_unixlib_handle, (void*)g_unix_call_dispatcher);
    return true;
}

static std::unordered_map<void*, uint64_t> g_handles;

static uint64_t obj_handle(void* obj) {
    if (!obj) return 0;
    auto it = g_handles.find(obj);
    return it != g_handles.end() ? it->second : 0;
}

static void set_handle(void* obj, uint64_t h) {
    if (obj) g_handles[obj] = h;
}

#define MS_QI \
    HRESULT WINAPI QueryInterface(REFIID, void** ppv) override { if(ppv)*ppv=nullptr; return E_NOINTERFACE; } \
    ULONG WINAPI AddRef() override { return InterlockedIncrement(&m_ref); } \
    ULONG WINAPI Release() override { ULONG r = InterlockedDecrement(&m_ref); return r ? r : 1; }

class MSDeviceContext;
static MSDeviceContext* g_ctx = nullptr;

class MSDeviceChildStub : public ID3D11DeviceChild {
    LONG m_ref = 1;
public:
    MS_QI
    void WINAPI GetDevice(ID3D11Device** pp) override { if(pp) *pp = nullptr; }
    HRESULT WINAPI GetPrivateData(REFGUID, UINT*, void*) override { return E_NOTIMPL; }
    HRESULT WINAPI SetPrivateData(REFGUID, UINT, const void*) override { return S_OK; }
    HRESULT WINAPI SetPrivateDataInterface(REFGUID, const IUnknown*) override { return S_OK; }
};

class MSResourceStub : public ID3D11Resource {
    LONG m_ref = 1;
public:
    MS_QI
    void WINAPI GetDevice(ID3D11Device** pp) override { if(pp) *pp = nullptr; }
    HRESULT WINAPI GetPrivateData(REFGUID, UINT*, void*) override { return E_NOTIMPL; }
    HRESULT WINAPI SetPrivateData(REFGUID, UINT, const void*) override { return S_OK; }
    HRESULT WINAPI SetPrivateDataInterface(REFGUID, const IUnknown*) override { return S_OK; }
    void WINAPI GetType(D3D11_RESOURCE_DIMENSION* r) override { if(r) *r = D3D11_RESOURCE_DIMENSION_UNKNOWN; }
    void WINAPI SetEvictionPriority(UINT) override {}
    UINT WINAPI GetEvictionPriority() override { return 0; }
};

class MSBuffer : public ID3D11Buffer {
    LONG m_ref = 1;
public:
    MS_QI
    void WINAPI GetDevice(ID3D11Device** pp) override { if(pp) *pp = nullptr; }
    HRESULT WINAPI GetPrivateData(REFGUID, UINT*, void*) override { return E_NOTIMPL; }
    HRESULT WINAPI SetPrivateData(REFGUID, UINT, const void*) override { return S_OK; }
    HRESULT WINAPI SetPrivateDataInterface(REFGUID, const IUnknown*) override { return S_OK; }
    void WINAPI GetType(D3D11_RESOURCE_DIMENSION* r) override { if(r) *r = D3D11_RESOURCE_DIMENSION_BUFFER; }
    void WINAPI SetEvictionPriority(UINT) override {}
    UINT WINAPI GetEvictionPriority() override { return 0; }
    void WINAPI GetDesc(D3D11_BUFFER_DESC* pDesc) override { if(pDesc) memset(pDesc, 0, sizeof(*pDesc)); }
};

class MSTexture2D : public ID3D11Texture2D {
    LONG m_ref = 1;
public:
    MS_QI
    void WINAPI GetDevice(ID3D11Device** pp) override { if(pp) *pp = nullptr; }
    HRESULT WINAPI GetPrivateData(REFGUID, UINT*, void*) override { return E_NOTIMPL; }
    HRESULT WINAPI SetPrivateData(REFGUID, UINT, const void*) override { return S_OK; }
    HRESULT WINAPI SetPrivateDataInterface(REFGUID, const IUnknown*) override { return S_OK; }
    void WINAPI GetType(D3D11_RESOURCE_DIMENSION* r) override { if(r) *r = D3D11_RESOURCE_DIMENSION_TEXTURE2D; }
    void WINAPI SetEvictionPriority(UINT) override {}
    UINT WINAPI GetEvictionPriority() override { return 0; }
    void WINAPI GetDesc(D3D11_TEXTURE2D_DESC* pDesc) override { if(pDesc) memset(pDesc, 0, sizeof(*pDesc)); }
};

class MSView : public ID3D11View {
    LONG m_ref = 1;
public:
    MS_QI
    void WINAPI GetDevice(ID3D11Device** pp) override { if(pp) *pp = nullptr; }
    HRESULT WINAPI GetPrivateData(REFGUID, UINT*, void*) override { return E_NOTIMPL; }
    HRESULT WINAPI SetPrivateData(REFGUID, UINT, const void*) override { return S_OK; }
    HRESULT WINAPI SetPrivateDataInterface(REFGUID, const IUnknown*) override { return S_OK; }
    void WINAPI GetResource(ID3D11Resource** pp) override { if(pp) *pp = nullptr; }
};

class MSDeviceContext : public ID3D11DeviceContext {
    LONG m_ref = 1;
public:
    MS_QI
    void WINAPI GetDevice(ID3D11Device** pp) override { if(pp) *pp = nullptr; }
    D3D11_DEVICE_CONTEXT_TYPE WINAPI GetType() override { return D3D11_DEVICE_CONTEXT_IMMEDIATE; }
    UINT WINAPI GetContextFlags() override { return 0; }
    HRESULT WINAPI GetPrivateData(REFGUID, UINT*, void*) override { return E_NOTIMPL; }
    HRESULT WINAPI SetPrivateData(REFGUID, UINT, const void*) override { return S_OK; }
    HRESULT WINAPI SetPrivateDataInterface(REFGUID, const IUnknown*) override { return S_OK; }

    void WINAPI IASetInputLayout(ID3D11InputLayout*) override {}
    void WINAPI IASetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppVB, const UINT* pStrides, const UINT* pOffsets) override {
        struct ms_set_vertex_buffers_params p = {};
        p.start_slot = StartSlot;
        p.num_buffers = NumBuffers;
        for (UINT i = 0; i < NumBuffers && i < 16; i++) {
            p.buffer_handles[i] = obj_handle(ppVB[i]);
            p.strides[i] = pStrides ? pStrides[i] : 0;
            p.offsets[i] = pOffsets ? pOffsets[i] : 0;
        }
        unix_call(MS_FUNC_IA_SET_VERTEX_BUFFERS, &p);
    }
    void WINAPI IASetIndexBuffer(ID3D11Buffer* pIB, DXGI_FORMAT Format, UINT Offset) override {
        struct ms_set_index_buffer_params p = {};
        p.buffer_handle = obj_handle(pIB);
        p.format = (uint32_t)Format;
        p.offset = Offset;
        unix_call(MS_FUNC_IA_SET_INDEX_BUFFER, &p);
    }
    void WINAPI IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY Topology) override {
        uint32_t t = (uint32_t)Topology;
        unix_call(MS_FUNC_IA_SET_PRIMITIVE_TOPOLOGY, &t);
    }
    void WINAPI DrawIndexed(UINT IndexCount, UINT StartIndex, INT BaseVertex) override {
        struct ms_draw_indexed_params p = {IndexCount, StartIndex, BaseVertex};
        unix_call(MS_FUNC_DRAW_INDEXED, &p);
    }
    void WINAPI Draw(UINT VertexCount, UINT StartVertex) override {
        struct ms_draw_params p = {VertexCount, StartVertex};
        unix_call(MS_FUNC_DRAW, &p);
    }
    void WINAPI DrawInstanced(UINT,UINT,UINT,UINT) override {}
    void WINAPI DrawIndexedInstanced(UINT,UINT,UINT,INT,UINT) override {}
    void WINAPI DrawAuto() override {}
    void WINAPI DrawIndexedInstancedIndirect(ID3D11Buffer*,UINT) override {}
    void WINAPI DrawInstancedIndirect(ID3D11Buffer*,UINT) override {}
    void WINAPI VSSetShader(ID3D11VertexShader* pShader, ID3D11ClassInstance* const*, UINT) override {
        uint64_t h = obj_handle(pShader);
        unix_call(MS_FUNC_VS_SET_SHADER, &h);
    }
    void WINAPI VSSetConstantBuffers(UINT Slot, UINT Num, ID3D11Buffer* const* ppB) override {
        struct ms_set_constant_buffers_params p = {};
        p.start_slot = Slot;
        p.num_buffers = Num;
        for (UINT i = 0; i < Num && i < 14; i++)
            p.buffer_handles[i] = obj_handle(ppB[i]);
        unix_call(MS_FUNC_VS_SET_CONSTANT_BUFFERS, &p);
    }
    void WINAPI VSSetShaderResources(UINT,UINT,ID3D11ShaderResourceView*const*) override {}
    void WINAPI VSSetSamplers(UINT,UINT,ID3D11SamplerState*const*) override {}
    void WINAPI PSSetShader(ID3D11PixelShader* pShader, ID3D11ClassInstance* const*, UINT) override {
        uint64_t h = obj_handle(pShader);
        unix_call(MS_FUNC_PS_SET_SHADER, &h);
    }
    void WINAPI PSSetConstantBuffers(UINT Slot, UINT Num, ID3D11Buffer* const* ppB) override {
        struct ms_set_constant_buffers_params p = {};
        p.start_slot = Slot;
        p.num_buffers = Num;
        for (UINT i = 0; i < Num && i < 14; i++)
            p.buffer_handles[i] = obj_handle(ppB[i]);
        unix_call(MS_FUNC_PS_SET_CONSTANT_BUFFERS, &p);
    }
    void WINAPI PSSetShaderResources(UINT,UINT,ID3D11ShaderResourceView*const*) override {}
    void WINAPI PSSetSamplers(UINT,UINT,ID3D11SamplerState*const*) override {}
    void WINAPI GSSetShader(ID3D11GeometryShader*,ID3D11ClassInstance*const*,UINT) override {}
    void WINAPI GSSetConstantBuffers(UINT,UINT,ID3D11Buffer*const*) override {}
    void WINAPI GSSetShaderResources(UINT,UINT,ID3D11ShaderResourceView*const*) override {}
    void WINAPI GSSetSamplers(UINT,UINT,ID3D11SamplerState*const*) override {}
    void WINAPI CSSetShader(ID3D11ComputeShader*,ID3D11ClassInstance*const*,UINT) override {}
    void WINAPI CSSetConstantBuffers(UINT,UINT,ID3D11Buffer*const*) override {}
    void WINAPI CSSetShaderResources(UINT,UINT,ID3D11ShaderResourceView*const*) override {}
    void WINAPI CSSetSamplers(UINT,UINT,ID3D11SamplerState*const*) override {}
    void WINAPI CSSetUnorderedAccessViews(UINT,UINT,ID3D11UnorderedAccessView*const*,const UINT*) override {}
    void WINAPI Dispatch(UINT,UINT,UINT) override {}
    void WINAPI DispatchIndirect(ID3D11Buffer*,UINT) override {}
    void WINAPI HSSetShader(ID3D11HullShader*,ID3D11ClassInstance*const*,UINT) override {}
    void WINAPI HSSetShaderResources(UINT,UINT,ID3D11ShaderResourceView*const*) override {}
    void WINAPI HSSetSamplers(UINT,UINT,ID3D11SamplerState*const*) override {}
    void WINAPI HSSetConstantBuffers(UINT,UINT,ID3D11Buffer*const*) override {}
    void WINAPI DSSetShader(ID3D11DomainShader*,ID3D11ClassInstance*const*,UINT) override {}
    void WINAPI DSSetShaderResources(UINT,UINT,ID3D11ShaderResourceView*const*) override {}
    void WINAPI DSSetSamplers(UINT,UINT,ID3D11SamplerState*const*) override {}
    void WINAPI DSSetConstantBuffers(UINT,UINT,ID3D11Buffer*const*) override {}
    void WINAPI OMSetRenderTargets(UINT NumViews, ID3D11RenderTargetView* const* ppRTV, ID3D11DepthStencilView* pDSV) override {
        struct ms_set_render_targets_params p = {};
        p.num_views = NumViews;
        for (UINT i = 0; i < NumViews && i < 8; i++)
            p.rtv_handles[i] = obj_handle(ppRTV[i]);
        p.dsv_handle = obj_handle(pDSV);
        unix_call(MS_FUNC_OM_SET_RENDER_TARGETS, &p);
    }
    void WINAPI OMSetRenderTargetsAndUnorderedAccessViews(UINT,ID3D11RenderTargetView*const*,ID3D11DepthStencilView*,UINT,UINT,ID3D11UnorderedAccessView*const*,const UINT*) override {}
    void WINAPI OMSetBlendState(ID3D11BlendState*,const FLOAT[4],UINT) override {}
    void WINAPI OMSetDepthStencilState(ID3D11DepthStencilState*,UINT) override {}
    void WINAPI RSSetState(ID3D11RasterizerState*) override {}
    void WINAPI RSSetViewports(UINT, const D3D11_VIEWPORT* pVP) override {
        struct ms_viewport vp = {};
        if (pVP) {
            vp.top_left_x = pVP->TopLeftX;
            vp.top_left_y = pVP->TopLeftY;
            vp.width = pVP->Width;
            vp.height = pVP->Height;
            vp.min_depth = pVP->MinDepth;
            vp.max_depth = pVP->MaxDepth;
        }
        unix_call(MS_FUNC_SET_VIEWPORTS, &vp);
    }
    void WINAPI RSSetScissorRects(UINT,const D3D11_RECT*) override {}
    void WINAPI SOSetTargets(UINT,ID3D11Buffer*const*,const UINT*) override {}
    void WINAPI ClearRenderTargetView(ID3D11RenderTargetView* pRTV, const FLOAT c[4]) override {
        struct ms_clear_params p = {};
        p.view_handle = obj_handle(pRTV);
        if (c) { p.r=c[0]; p.g=c[1]; p.b=c[2]; p.a=c[3]; }
        unix_call(MS_FUNC_CLEAR_RENDER_TARGET_VIEW, &p);
    }
    void WINAPI ClearDepthStencilView(ID3D11DepthStencilView*,UINT,FLOAT,UINT8) override {}
    void WINAPI ClearUnorderedAccessViewUint(ID3D11UnorderedAccessView*,const UINT[4]) override {}
    void WINAPI ClearUnorderedAccessViewFloat(ID3D11UnorderedAccessView*,const FLOAT[4]) override {}
    void WINAPI ClearState() override {}
    void WINAPI UpdateSubresource(ID3D11Resource* pRes, UINT Sub, const D3D11_BOX*, const void* pData, UINT RowPitch, UINT DepthPitch) override {
        struct ms_update_subresource_params p = {};
        p.resource_handle = obj_handle(pRes);
        p.subresource = Sub;
        p.data = pData;
        p.row_pitch = RowPitch;
        p.depth_pitch = DepthPitch;
        unix_call(MS_FUNC_UPDATE_SUBRESOURCE, &p);
    }
    void WINAPI CopyResource(ID3D11Resource*,ID3D11Resource*) override {}
    void WINAPI CopySubresourceRegion(ID3D11Resource*,UINT,UINT,UINT,UINT,ID3D11Resource*,UINT,const D3D11_BOX*) override {}
    void WINAPI ResolveSubresource(ID3D11Resource*,UINT,ID3D11Resource*,UINT,DXGI_FORMAT) override {}
    void WINAPI GenerateMips(ID3D11ShaderResourceView*) override {}
    void WINAPI CopyStructureCount(ID3D11Buffer*,UINT,ID3D11UnorderedAccessView*) override {}
    void WINAPI SetResourceMinLOD(ID3D11Resource*,FLOAT) override {}
    FLOAT WINAPI GetResourceMinLOD(ID3D11Resource*) override { return 0.0f; }
    HRESULT WINAPI Map(ID3D11Resource*,UINT,D3D11_MAP,UINT,D3D11_MAPPED_SUBRESOURCE*) override { return E_NOTIMPL; }
    void WINAPI Unmap(ID3D11Resource*,UINT) override {}
    void WINAPI Flush() override { unix_call(MS_FUNC_FLUSH, nullptr); }
    void WINAPI Begin(ID3D11Asynchronous*) override {}
    void WINAPI End(ID3D11Asynchronous*) override {}
    HRESULT WINAPI GetData(ID3D11Asynchronous*,void*,UINT,UINT) override { return S_FALSE; }
    void WINAPI SetPredication(ID3D11Predicate*,BOOL) override {}
    void WINAPI GetPredication(ID3D11Predicate**,BOOL*) override {}
    void WINAPI ExecuteCommandList(ID3D11CommandList*,BOOL) override {}
    HRESULT WINAPI FinishCommandList(BOOL,ID3D11CommandList**) override { return S_OK; }
    void WINAPI VSGetShader(ID3D11VertexShader**,ID3D11ClassInstance**,UINT*) override {}
    void WINAPI VSGetConstantBuffers(UINT,UINT,ID3D11Buffer**) override {}
    void WINAPI VSGetShaderResources(UINT,UINT,ID3D11ShaderResourceView**) override {}
    void WINAPI VSGetSamplers(UINT,UINT,ID3D11SamplerState**) override {}
    void WINAPI PSGetShader(ID3D11PixelShader**,ID3D11ClassInstance**,UINT*) override {}
    void WINAPI PSGetConstantBuffers(UINT,UINT,ID3D11Buffer**) override {}
    void WINAPI PSGetShaderResources(UINT,UINT,ID3D11ShaderResourceView**) override {}
    void WINAPI PSGetSamplers(UINT,UINT,ID3D11SamplerState**) override {}
    void WINAPI GSGetShader(ID3D11GeometryShader**,ID3D11ClassInstance**,UINT*) override {}
    void WINAPI GSGetConstantBuffers(UINT,UINT,ID3D11Buffer**) override {}
    void WINAPI GSGetShaderResources(UINT,UINT,ID3D11ShaderResourceView**) override {}
    void WINAPI GSGetSamplers(UINT,UINT,ID3D11SamplerState**) override {}
    void WINAPI HSGetShader(ID3D11HullShader**,ID3D11ClassInstance**,UINT*) override {}
    void WINAPI HSGetConstantBuffers(UINT,UINT,ID3D11Buffer**) override {}
    void WINAPI HSGetShaderResources(UINT,UINT,ID3D11ShaderResourceView**) override {}
    void WINAPI HSGetSamplers(UINT,UINT,ID3D11SamplerState**) override {}
    void WINAPI DSGetShader(ID3D11DomainShader**,ID3D11ClassInstance**,UINT*) override {}
    void WINAPI DSGetConstantBuffers(UINT,UINT,ID3D11Buffer**) override {}
    void WINAPI DSGetShaderResources(UINT,UINT,ID3D11ShaderResourceView**) override {}
    void WINAPI DSGetSamplers(UINT,UINT,ID3D11SamplerState**) override {}
    void WINAPI CSGetShader(ID3D11ComputeShader**,ID3D11ClassInstance**,UINT*) override {}
    void WINAPI CSGetConstantBuffers(UINT,UINT,ID3D11Buffer**) override {}
    void WINAPI CSGetShaderResources(UINT,UINT,ID3D11ShaderResourceView**) override {}
    void WINAPI CSGetSamplers(UINT,UINT,ID3D11SamplerState**) override {}
    void WINAPI CSGetUnorderedAccessViews(UINT,UINT,ID3D11UnorderedAccessView**) override {}
    void WINAPI IAGetInputLayout(ID3D11InputLayout**) override {}
    void WINAPI IAGetVertexBuffers(UINT,UINT,ID3D11Buffer**,UINT*,UINT*) override {}
    void WINAPI IAGetIndexBuffer(ID3D11Buffer**,DXGI_FORMAT*,UINT*) override {}
    void WINAPI IAGetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY*) override {}
    void WINAPI OMGetRenderTargets(UINT,ID3D11RenderTargetView**,ID3D11DepthStencilView**) override {}
    void WINAPI OMGetRenderTargetsAndUnorderedAccessViews(UINT,ID3D11RenderTargetView**,ID3D11DepthStencilView**,UINT,UINT,ID3D11UnorderedAccessView**) override {}
    void WINAPI OMGetBlendState(ID3D11BlendState**,FLOAT[4],UINT*) override {}
    void WINAPI OMGetDepthStencilState(ID3D11DepthStencilState**,UINT*) override {}
    void WINAPI RSGetState(ID3D11RasterizerState**) override {}
    void WINAPI RSGetViewports(UINT*,D3D11_VIEWPORT*) override {}
    void WINAPI RSGetScissorRects(UINT*,D3D11_RECT*) override {}
    void WINAPI SOGetTargets(UINT,ID3D11Buffer**) override {}
};

static MSDeviceContext g_ctx_impl;

class MSDevice : public ID3D11Device {
    LONG m_ref = 1;
public:
    MS_QI
    void WINAPI GetImmediateContext(ID3D11DeviceContext** pp) override { if(pp) *pp = g_ctx; }
    HRESULT WINAPI CheckFeatureSupport(D3D11_FEATURE,void*,UINT) override { return E_NOTIMPL; }
    HRESULT WINAPI CheckMultisampleQualityLevels(DXGI_FORMAT,UINT,UINT*) override { return E_NOTIMPL; }
    void WINAPI CheckCounterInfo(D3D11_COUNTER_INFO*) override {}
    HRESULT WINAPI CheckCounter(const D3D11_COUNTER_DESC*,D3D11_COUNTER_TYPE*,UINT*,LPSTR,UINT*,LPSTR,UINT*,LPSTR,UINT*) override { return E_NOTIMPL; }
    HRESULT WINAPI CheckFormatSupport(DXGI_FORMAT,UINT* pS) override { if(pS)*pS=0x3FFF; return S_OK; }
    HRESULT WINAPI GetPrivateData(REFGUID,UINT*,void*) override { return E_NOTIMPL; }
    HRESULT WINAPI SetPrivateData(REFGUID,UINT,const void*) override { return S_OK; }
    HRESULT WINAPI SetPrivateDataInterface(REFGUID,const IUnknown*) override { return S_OK; }
    D3D_FEATURE_LEVEL WINAPI GetFeatureLevel() override { return D3D_FEATURE_LEVEL_11_0; }
    UINT WINAPI GetCreationFlags() override { return 0; }
    HRESULT WINAPI GetDeviceRemovedReason() override { return S_OK; }
    UINT WINAPI GetExceptionMode() override { return 0; }
    HRESULT WINAPI SetExceptionMode(UINT) override { return S_OK; }

    HRESULT WINAPI CreateBuffer(const D3D11_BUFFER_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInit, ID3D11Buffer** ppOut) override {
        if (!ppOut) return S_OK;
        auto* obj = new MSBuffer();
        struct ms_create_buffer_params p = {};
        if (pDesc) {
            p.byte_width = pDesc->ByteWidth;
            p.usage = (int)pDesc->Usage;
            p.bind_flags = (int)pDesc->BindFlags;
            p.cpu_access_flags = (int)pDesc->CPUAccessFlags;
            p.misc_flags = (int)pDesc->MiscFlags;
            p.structure_byte_stride = pDesc->StructureByteStride;
        }
        if (pInit) {
            p.initial_data = pInit->pSysMem;
            p.initial_data_size = pDesc ? pDesc->ByteWidth : 0;
        }
        NTSTATUS ret = unix_call(MS_FUNC_CREATE_BUFFER, &p);
        if (ret >= 0) set_handle(obj, p.out_handle);
        *ppOut = obj;
        return S_OK;
    }
    HRESULT WINAPI CreateTexture1D(const D3D11_TEXTURE1D_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Texture1D**) override { return S_OK; }
    HRESULT WINAPI CreateTexture2D(const D3D11_TEXTURE2D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture2D** ppOut) override {
        if (!ppOut) return S_OK;
        auto* obj = new MSTexture2D();
        struct ms_create_texture_2d_params p = {};
        if (pDesc) {
            p.width = pDesc->Width;
            p.height = pDesc->Height;
            p.mip_levels = pDesc->MipLevels;
            p.array_size = pDesc->ArraySize;
            p.format = (uint32_t)pDesc->Format;
            p.sample_count = pDesc->SampleDesc.Count;
            p.sample_quality = pDesc->SampleDesc.Quality;
            p.usage = (uint32_t)pDesc->Usage;
            p.bind_flags = (uint32_t)pDesc->BindFlags;
            p.cpu_access_flags = (uint32_t)pDesc->CPUAccessFlags;
            p.misc_flags = (uint32_t)pDesc->MiscFlags;
        }
        NTSTATUS ret = unix_call(MS_FUNC_CREATE_TEXTURE_2D, &p);
        if (ret >= 0) set_handle(obj, p.out_handle);
        *ppOut = obj;
        return S_OK;
    }
    HRESULT WINAPI CreateTexture3D(const D3D11_TEXTURE3D_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Texture3D**) override { return S_OK; }
    HRESULT WINAPI CreateShaderResourceView(ID3D11Resource*,const D3D11_SHADER_RESOURCE_VIEW_DESC*,ID3D11ShaderResourceView**) override { return S_OK; }
    HRESULT WINAPI CreateUnorderedAccessView(ID3D11Resource*,const D3D11_UNORDERED_ACCESS_VIEW_DESC*,ID3D11UnorderedAccessView**) override { return S_OK; }
    HRESULT WINAPI CreateRenderTargetView(ID3D11Resource* pRes, const D3D11_RENDER_TARGET_VIEW_DESC* pDesc, ID3D11RenderTargetView** ppOut) override {
        if (!ppOut) return S_OK;
        auto* obj = new MSDeviceChildStub();
        struct ms_create_rt_view_params p = {};
        p.texture_handle = obj_handle(pRes);
        p.format = pDesc ? (uint32_t)pDesc->Format : 0;
        p.mip_slice = 0;
        NTSTATUS ret = unix_call(MS_FUNC_CREATE_RENDER_TARGET_VIEW, &p);
        if (ret >= 0) set_handle(obj, p.out_handle);
        *ppOut = (ID3D11RenderTargetView*)obj;
        return S_OK;
    }
    HRESULT WINAPI CreateDepthStencilView(ID3D11Resource*,const D3D11_DEPTH_STENCIL_VIEW_DESC*,ID3D11DepthStencilView**) override { return S_OK; }
    HRESULT WINAPI CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC*,UINT,const void*,SIZE_T,ID3D11InputLayout** ppOut) override {
        if (ppOut) *ppOut = (ID3D11InputLayout*)new MSDeviceChildStub();
        return S_OK;
    }
    HRESULT WINAPI CreateVertexShader(const void* pBC, SIZE_T Len, ID3D11ClassLinkage*, ID3D11VertexShader** ppOut) override {
        if (!ppOut) return S_OK;
        auto* obj = new MSDeviceChildStub();
        struct ms_create_shader_params p = {};
        p.bytecode = pBC;
        p.bytecode_size = (uint32_t)Len;
        NTSTATUS ret = unix_call(MS_FUNC_CREATE_VERTEX_SHADER, &p);
        if (ret >= 0) set_handle(obj, p.out_handle);
        *ppOut = (ID3D11VertexShader*)obj;
        return S_OK;
    }
    HRESULT WINAPI CreateGeometryShader(const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11GeometryShader**) override { return S_OK; }
    HRESULT WINAPI CreateGeometryShaderWithStreamOutput(const void*,SIZE_T,const D3D11_SO_DECLARATION_ENTRY*,UINT,const UINT*,UINT,UINT,ID3D11ClassLinkage*,ID3D11GeometryShader**) override { return S_OK; }
    HRESULT WINAPI CreatePixelShader(const void* pBC, SIZE_T Len, ID3D11ClassLinkage*, ID3D11PixelShader** ppOut) override {
        if (!ppOut) return S_OK;
        auto* obj = new MSDeviceChildStub();
        struct ms_create_shader_params p = {};
        p.bytecode = pBC;
        p.bytecode_size = (uint32_t)Len;
        NTSTATUS ret = unix_call(MS_FUNC_CREATE_PIXEL_SHADER, &p);
        if (ret >= 0) set_handle(obj, p.out_handle);
        *ppOut = (ID3D11PixelShader*)obj;
        return S_OK;
    }
    HRESULT WINAPI CreateHullShader(const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11HullShader**) override { return S_OK; }
    HRESULT WINAPI CreateDomainShader(const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11DomainShader**) override { return S_OK; }
    HRESULT WINAPI CreateComputeShader(const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11ComputeShader**) override { return S_OK; }
    HRESULT WINAPI CreateClassLinkage(ID3D11ClassLinkage**) override { return S_OK; }
    HRESULT WINAPI CreateBlendState(const D3D11_BLEND_DESC*,ID3D11BlendState**) override { return S_OK; }
    HRESULT WINAPI CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC*,ID3D11DepthStencilState**) override { return S_OK; }
    HRESULT WINAPI CreateRasterizerState(const D3D11_RASTERIZER_DESC*,ID3D11RasterizerState**) override { return S_OK; }
    HRESULT WINAPI CreateSamplerState(const D3D11_SAMPLER_DESC*,ID3D11SamplerState**) override { return S_OK; }
    HRESULT WINAPI CreateQuery(const D3D11_QUERY_DESC*,ID3D11Query**) override { return S_OK; }
    HRESULT WINAPI CreatePredicate(const D3D11_QUERY_DESC*,ID3D11Predicate**) override { return S_OK; }
    HRESULT WINAPI CreateCounter(const D3D11_COUNTER_DESC*,ID3D11Counter**) override { return E_NOTIMPL; }
    HRESULT WINAPI CreateDeferredContext(UINT,ID3D11DeviceContext**) override { return E_NOTIMPL; }
    HRESULT WINAPI OpenSharedResource(HANDLE,REFIID,void**) override { return E_NOTIMPL; }
};

static MSDevice g_dev_impl;

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = (HMODULE)inst;
        g_ctx = &g_ctx_impl;
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}

extern "C" {

__declspec(dllexport)
HRESULT WINAPI D3D11CreateDeviceAndSwapChain(
    IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT,
    const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**,
    ID3D11Device** ppDevice, D3D_FEATURE_LEVEL* pFL,
    ID3D11DeviceContext** ppCtx)
{
    fprintf(stderr, "[metalsharp] D3D11CreateDevice called\n");
    if (!init_unix_call()) {
        fprintf(stderr, "[metalsharp] FAILED to init unix call bridge\n");
        return E_FAIL;
    }
    NTSTATUS r = unix_call(MS_FUNC_INIT, nullptr);
    fprintf(stderr, "[metalsharp] INIT returned %ld\n", r);
    if (r < 0) return E_FAIL;
    unix_call(MS_FUNC_CREATE_DEVICE, nullptr);
    if (ppDevice) *ppDevice = &g_dev_impl;
    if (ppCtx) *ppCtx = g_ctx;
    if (pFL) *pFL = D3D_FEATURE_LEVEL_11_0;
    return S_OK;
}

__declspec(dllexport)
HRESULT WINAPI D3D11CreateDevice(
    IDXGIAdapter* a, D3D_DRIVER_TYPE dt, HMODULE sw, UINT f,
    const D3D_FEATURE_LEVEL* fl, UINT nfl, UINT sdk,
    ID3D11Device** ppDev, D3D_FEATURE_LEVEL* pFL2,
    ID3D11DeviceContext** ppCtx)
{
    return D3D11CreateDeviceAndSwapChain(a, dt, sw, f, fl, nfl, sdk, NULL, NULL, ppDev, pFL2, ppCtx);
}

}
