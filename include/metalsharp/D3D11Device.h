#pragma once

#include <d3d/D3D11.h>
#include <metalsharp/MetalBackend.h>
#include <metalsharp/ShaderTranslator.h>
#include <mutex>
#include <unordered_map>
#include <memory>

namespace metalsharp {

class D3D11Device : public ID3D11Device {
public:
    static HRESULT create(D3D11Device** ppDevice);

    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObject) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    STDMETHOD(CreateBuffer)(const D3D11_BUFFER_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Buffer** ppBuffer) override;
    STDMETHOD(CreateTexture2D)(const D3D11_TEXTURE2D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D) override;
    STDMETHOD(CreateShaderResourceView)(ID3D11Resource* pResource, const void* pDesc, ID3D11ShaderResourceView** ppSRView) override;
    STDMETHOD(CreateRenderTargetView)(ID3D11Resource* pResource, const void* pDesc, ID3D11RenderTargetView** ppRTView) override;
    STDMETHOD(CreateDepthStencilView)(ID3D11Resource* pResource, const void* pDesc, ID3D11DepthStencilView** ppDepthStencilView) override;
    STDMETHOD(CreateVertexShader)(const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage*, ID3D11VertexShader** ppVertexShader) override;
    STDMETHOD(CreatePixelShader)(const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage*, ID3D11PixelShader** ppPixelShader) override;
    STDMETHOD(CreateGeometryShader)(const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage*, ID3D11GeometryShader** ppGeometryShader) override;
    STDMETHOD(CreateComputeShader)(const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage*, ID3D11ComputeShader** ppComputeShader) override;
    STDMETHOD(CreateInputLayout)(const void* pInputElementDescs, UINT NumElements, const void* pShaderBytecodeWithInputSignature, SIZE_T BytecodeLength, ID3D11InputLayout** ppInputLayout) override;
    STDMETHOD(CreateSamplerState)(const void* pSamplerDesc, ID3D11SamplerState** ppSamplerState) override;
    STDMETHOD(CreateRasterizerState)(const void* pRasterizerDesc, ID3D11RasterizerState** ppRasterizerState) override;
    STDMETHOD(CreateDepthStencilState)(const void* pDepthStencilDesc, ID3D11DepthStencilState** ppDepthStencilState) override;
    STDMETHOD(CreateBlendState)(const void* pBlendStateDesc, ID3D11BlendState** ppBlendState) override;
    STDMETHOD_(void, GetImmediateContext)(ID3D11DeviceContext** ppImmediateContext) override;
    STDMETHOD(GetDeviceFeatureLevel)(UINT* pFeatureLevel) override;

    MetalDevice& metalDevice() { return *m_metalDevice; }

private:
    D3D11Device();
    ~D3D11Device();

    ULONG m_refCount = 1;
    std::unique_ptr<MetalDevice> m_metalDevice;
    std::unique_ptr<class D3D11DeviceContext> m_immediateContext;
    std::unique_ptr<ShaderTranslator> m_shaderTranslator;
};

}
