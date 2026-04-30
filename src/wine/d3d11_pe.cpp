#include <d3d11.h>
#include <dxgi.h>
#include <string.h>

#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

template<typename Base>
class StubImpl : public Base {
    LONG refCount = 1;
public:
    HRESULT WINAPI QueryInterface(REFIID riid, void** ppv) override { if(ppv)*ppv=NULL; return E_NOINTERFACE; }
    ULONG WINAPI AddRef() override { return InterlockedIncrement(&refCount); }
    ULONG WINAPI Release() override { ULONG r = InterlockedDecrement(&refCount); return r ? r : 1; }
};

#define STUB_METHOD(ret, name, ...) ret WINAPI name(__VA_ARGS__) override { return ret{}; }
#define STUB_VOID(name, ...) void WINAPI name(__VA_ARGS__) override {}

class StubDeviceContext : public ID3D11DeviceContext {
    LONG refCount = 1;
public:
    HRESULT WINAPI QueryInterface(REFIID riid, void** ppv) override { if(ppv)*ppv=NULL; return E_NOINTERFACE; }
    ULONG WINAPI AddRef() override { return InterlockedIncrement(&refCount); }
    ULONG WINAPI Release() override { ULONG r = InterlockedDecrement(&refCount); return r ? r : 1; }

    void WINAPI GetDevice(ID3D11Device** ppDevice) override;
    D3D11_DEVICE_CONTEXT_TYPE WINAPI GetType() override { return D3D11_DEVICE_CONTEXT_IMMEDIATE; }
    UINT WINAPI GetContextFlags() override { return 0; }
    HRESULT WINAPI GetPrivateData(REFGUID, UINT*, void*) override { return E_NOTIMPL; }
    HRESULT WINAPI SetPrivateData(REFGUID, UINT, const void*) override { return S_OK; }
    HRESULT WINAPI SetPrivateDataInterface(REFGUID, const IUnknown*) override { return S_OK; }

    void WINAPI IASetInputLayout(ID3D11InputLayout*) override {}
    void WINAPI IASetVertexBuffers(UINT,UINT,ID3D11Buffer*const*,const UINT*,const UINT*) override {}
    void WINAPI IASetIndexBuffer(ID3D11Buffer*,DXGI_FORMAT,UINT) override {}
    void WINAPI IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY) override {}
    void WINAPI DrawIndexed(UINT,UINT,INT) override {}
    void WINAPI Draw(UINT,UINT) override {}
    void WINAPI DrawInstanced(UINT,UINT,UINT,UINT) override {}
    void WINAPI DrawIndexedInstanced(UINT,UINT,UINT,INT,UINT) override {}
    void WINAPI DrawAuto() override {}
    void WINAPI DrawIndexedInstancedIndirect(ID3D11Buffer*,UINT) override {}
    void WINAPI DrawInstancedIndirect(ID3D11Buffer*,UINT) override {}
    void WINAPI VSSetShader(ID3D11VertexShader*,ID3D11ClassInstance*const*,UINT) override {}
    void WINAPI VSSetConstantBuffers(UINT,UINT,ID3D11Buffer*const*) override {}
    void WINAPI VSSetShaderResources(UINT,UINT,ID3D11ShaderResourceView*const*) override {}
    void WINAPI VSSetSamplers(UINT,UINT,ID3D11SamplerState*const*) override {}
    void WINAPI PSSetShader(ID3D11PixelShader*,ID3D11ClassInstance*const*,UINT) override {}
    void WINAPI PSSetConstantBuffers(UINT,UINT,ID3D11Buffer*const*) override {}
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
    void WINAPI OMSetRenderTargets(UINT,ID3D11RenderTargetView*const*,ID3D11DepthStencilView*) override {}
    void WINAPI OMSetRenderTargetsAndUnorderedAccessViews(UINT,ID3D11RenderTargetView*const*,ID3D11DepthStencilView*,UINT,UINT,ID3D11UnorderedAccessView*const*,const UINT*) override {}
    void WINAPI OMSetBlendState(ID3D11BlendState*,const FLOAT[4],UINT) override {}
    void WINAPI OMSetDepthStencilState(ID3D11DepthStencilState*,UINT) override {}
    void WINAPI RSSetState(ID3D11RasterizerState*) override {}
    void WINAPI RSSetViewports(UINT,const D3D11_VIEWPORT*) override {}
    void WINAPI RSSetScissorRects(UINT,const D3D11_RECT*) override {}
    void WINAPI SOSetTargets(UINT,ID3D11Buffer*const*,const UINT*) override {}
    void WINAPI ClearRenderTargetView(ID3D11RenderTargetView*,const FLOAT[4]) override {}
    void WINAPI ClearDepthStencilView(ID3D11DepthStencilView*,UINT,FLOAT,UINT8) override {}
    void WINAPI ClearUnorderedAccessViewUint(ID3D11UnorderedAccessView*,const UINT[4]) override {}
    void WINAPI ClearUnorderedAccessViewFloat(ID3D11UnorderedAccessView*,const FLOAT[4]) override {}
    void WINAPI ClearState() override {}
    void WINAPI UpdateSubresource(ID3D11Resource*,UINT,const D3D11_BOX*,const void*,UINT,UINT) override {}
    void WINAPI CopyResource(ID3D11Resource*,ID3D11Resource*) override {}
    void WINAPI CopySubresourceRegion(ID3D11Resource*,UINT,UINT,UINT,UINT,ID3D11Resource*,UINT,const D3D11_BOX*) override {}
    void WINAPI ResolveSubresource(ID3D11Resource*,UINT,ID3D11Resource*,UINT,DXGI_FORMAT) override {}
    void WINAPI GenerateMips(ID3D11ShaderResourceView*) override {}
    void WINAPI CopyStructureCount(ID3D11Buffer*,UINT,ID3D11UnorderedAccessView*) override {}
    void WINAPI SetResourceMinLOD(ID3D11Resource*,FLOAT) override {}
    FLOAT WINAPI GetResourceMinLOD(ID3D11Resource*) override { return 0.0f; }
    HRESULT WINAPI Map(ID3D11Resource*,UINT,D3D11_MAP,UINT,D3D11_MAPPED_SUBRESOURCE*) override {
        return E_NOTIMPL;
    }
    void WINAPI Unmap(ID3D11Resource*,UINT) override {}
    void WINAPI Flush() override {}
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

static class StubDeviceContext g_ctx_impl;

class StubDevice : public ID3D11Device {
    LONG refCount = 1;
public:
    HRESULT WINAPI QueryInterface(REFIID riid, void** ppv) override { if(ppv)*ppv=NULL; return E_NOINTERFACE; }
    ULONG WINAPI AddRef() override { return InterlockedIncrement(&refCount); }
    ULONG WINAPI Release() override { ULONG r = InterlockedDecrement(&refCount); return r ? r : 1; }
    void WINAPI GetImmediateContext(ID3D11DeviceContext** ppImmediateContext) override {
        if (ppImmediateContext) *ppImmediateContext = &g_ctx_impl;
    }
    HRESULT WINAPI CheckFeatureSupport(D3D11_FEATURE,void*,UINT) override { return E_NOTIMPL; }
    HRESULT WINAPI CheckMultisampleQualityLevels(DXGI_FORMAT,UINT,UINT*) override { return E_NOTIMPL; }
    void WINAPI CheckCounterInfo(D3D11_COUNTER_INFO*) override {}
    HRESULT WINAPI CheckCounter(const D3D11_COUNTER_DESC*,D3D11_COUNTER_TYPE*,UINT*,LPSTR,UINT*,LPSTR,UINT*,LPSTR,UINT*) override { return E_NOTIMPL; }
    HRESULT WINAPI CheckFormatSupport(DXGI_FORMAT,UINT* pSupport) override { if(pSupport)*pSupport=0x3FFF; return S_OK; }
    HRESULT WINAPI GetPrivateData(REFGUID,UINT*,void*) override { return E_NOTIMPL; }
    HRESULT WINAPI SetPrivateData(REFGUID,UINT,const void*) override { return S_OK; }
    HRESULT WINAPI SetPrivateDataInterface(REFGUID,const IUnknown*) override { return S_OK; }
    D3D_FEATURE_LEVEL WINAPI GetFeatureLevel() override { return D3D_FEATURE_LEVEL_11_0; }
    UINT WINAPI GetCreationFlags() override { return 0; }
    HRESULT WINAPI GetDeviceRemovedReason() override { return S_OK; }
    UINT WINAPI GetExceptionMode() override { return 0; }
    HRESULT WINAPI SetExceptionMode(UINT) override { return S_OK; }
    HRESULT WINAPI CreateBuffer(const D3D11_BUFFER_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Buffer**) override { return S_OK; }
    HRESULT WINAPI CreateTexture1D(const D3D11_TEXTURE1D_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Texture1D**) override { return S_OK; }
    HRESULT WINAPI CreateTexture2D(const D3D11_TEXTURE2D_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Texture2D**) override { return S_OK; }
    HRESULT WINAPI CreateTexture3D(const D3D11_TEXTURE3D_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Texture3D**) override { return S_OK; }
    HRESULT WINAPI CreateShaderResourceView(ID3D11Resource*,const D3D11_SHADER_RESOURCE_VIEW_DESC*,ID3D11ShaderResourceView**) override { return S_OK; }
    HRESULT WINAPI CreateUnorderedAccessView(ID3D11Resource*,const D3D11_UNORDERED_ACCESS_VIEW_DESC*,ID3D11UnorderedAccessView**) override { return S_OK; }
    HRESULT WINAPI CreateRenderTargetView(ID3D11Resource*,const D3D11_RENDER_TARGET_VIEW_DESC*,ID3D11RenderTargetView**) override { return S_OK; }
    HRESULT WINAPI CreateDepthStencilView(ID3D11Resource*,const D3D11_DEPTH_STENCIL_VIEW_DESC*,ID3D11DepthStencilView**) override { return S_OK; }
    HRESULT WINAPI CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC*,UINT,const void*,SIZE_T,ID3D11InputLayout**) override { return S_OK; }
    HRESULT WINAPI CreateVertexShader(const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11VertexShader**) override { return S_OK; }
    HRESULT WINAPI CreateGeometryShader(const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11GeometryShader**) override { return S_OK; }
    HRESULT WINAPI CreateGeometryShaderWithStreamOutput(const void*,SIZE_T,const D3D11_SO_DECLARATION_ENTRY*,UINT,const UINT*,UINT,UINT,ID3D11ClassLinkage*,ID3D11GeometryShader**) override { return S_OK; }
    HRESULT WINAPI CreatePixelShader(const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11PixelShader**) override { return S_OK; }
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
    HRESULT WINAPI CreateDeferredContext(UINT,ID3D11DeviceContext** ppCtx) override { if(ppCtx)*ppCtx=&g_ctx_impl; return S_OK; }
    HRESULT WINAPI OpenSharedResource(HANDLE,REFIID,void**) override { return E_NOTIMPL; }
};

static StubDevice g_dev_impl;

void WINAPI StubDeviceContext::GetDevice(ID3D11Device** ppDevice) {
    if (ppDevice) *ppDevice = &g_dev_impl;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)inst; (void)reserved;
    return TRUE;
}

extern "C" {

__declspec(dllexport)
HRESULT WINAPI D3D11CreateDeviceAndSwapChain(
    IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT,
    const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**,
    ID3D11Device** ppDevice, D3D_FEATURE_LEVEL* pFeatureLevel,
    ID3D11DeviceContext** ppImmediateContext)
{
    if (ppDevice) *ppDevice = &g_dev_impl;
    if (ppImmediateContext) *ppImmediateContext = &g_ctx_impl;
    if (pFeatureLevel) *pFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    return S_OK;
}

__declspec(dllexport)
HRESULT WINAPI D3D11CreateDevice(
    IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels, UINT SDKVersion,
    ID3D11Device** ppDevice, D3D_FEATURE_LEVEL* pFeatureLevel,
    ID3D11DeviceContext** ppImmediateContext)
{
    return D3D11CreateDeviceAndSwapChain(pAdapter, DriverType, Software, Flags,
        pFeatureLevels, FeatureLevels, SDKVersion, NULL, NULL,
        ppDevice, pFeatureLevel, ppImmediateContext);
}

}
