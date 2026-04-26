#pragma once

#include <d3d/D3D11.h>
#include <functional>
#include <vector>
#include <memory>

namespace metalsharp {

class D3D11Device;

class CommandList final : public ID3D11CommandList {
public:
    using Cmd = std::function<void(ID3D11DeviceContext*)>;

    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObject) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;
    STDMETHOD(GetDevice)(ID3D11Device** ppDevice) override;
    STDMETHOD(GetPrivateData)(const GUID&, UINT*, void*) override;
    STDMETHOD(SetPrivateData)(const GUID&, UINT, const void*) override;
    STDMETHOD(SetPrivateDataInterface)(const GUID&, const IUnknown*) override;
    STDMETHOD_(UINT, GetContextFlags)() override;

    void record(Cmd cmd) { m_commands.push_back(std::move(cmd)); }
    void replay(ID3D11DeviceContext* target) {
        for (auto& cmd : m_commands) cmd(target);
    }
    void clear() { m_commands.clear(); }

private:
    ULONG m_refCount = 1;
    std::vector<Cmd> m_commands;
};

class DeferredContext final : public ID3D11DeviceContext {
public:
    explicit DeferredContext(D3D11Device& device);

    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObject) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;
    STDMETHOD(GetDevice)(ID3D11Device** ppDevice) override;
    STDMETHOD(GetPrivateData)(const GUID&, UINT*, void*) override;
    STDMETHOD(SetPrivateData)(const GUID&, UINT, const void*) override;
    STDMETHOD(SetPrivateDataInterface)(const GUID&, const IUnknown*) override;

    STDMETHOD(IASetInputLayout)(ID3D11InputLayout*) override;
    STDMETHOD(IASetVertexBuffers)(UINT, UINT, ID3D11Buffer* const*, const UINT*, const UINT*) override;
    STDMETHOD(IASetIndexBuffer)(ID3D11Buffer*, DXGI_FORMAT, UINT) override;
    STDMETHOD(IASetPrimitiveTopology)(UINT) override;

    STDMETHOD(VSSetShader)(ID3D11VertexShader*, ID3D11ClassInstance* const*, UINT) override;
    STDMETHOD(VSSetConstantBuffers)(UINT, UINT, ID3D11Buffer* const*) override;
    STDMETHOD(VSSetShaderResources)(UINT, UINT, ID3D11ShaderResourceView* const*) override;
    STDMETHOD(VSSetSamplers)(UINT, UINT, ID3D11SamplerState* const*) override;

    STDMETHOD(PSSetShader)(ID3D11PixelShader*, ID3D11ClassInstance* const*, UINT) override;
    STDMETHOD(PSSetConstantBuffers)(UINT, UINT, ID3D11Buffer* const*) override;
    STDMETHOD(PSSetShaderResources)(UINT, UINT, ID3D11ShaderResourceView* const*) override;
    STDMETHOD(PSSetSamplers)(UINT, UINT, ID3D11SamplerState* const*) override;

    STDMETHOD(GSSetShader)(ID3D11GeometryShader*, ID3D11ClassInstance* const*, UINT) override;

    STDMETHOD(CSSetShader)(ID3D11ComputeShader*, ID3D11ClassInstance* const*, UINT) override;
    STDMETHOD(CSSetConstantBuffers)(UINT, UINT, ID3D11Buffer* const*) override;
    STDMETHOD(CSSetShaderResources)(UINT, UINT, ID3D11ShaderResourceView* const*) override;
    STDMETHOD(CSSetUnorderedAccessViews)(UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*) override;
    STDMETHOD(CSSetSamplers)(UINT, UINT, ID3D11SamplerState* const*) override;
    STDMETHOD(Dispatch)(UINT, UINT, UINT) override;

    STDMETHOD(OMSetRenderTargets)(UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*) override;
    STDMETHOD(OMSetRenderTargetsAndUnorderedAccessViews)(UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*, UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*) override;
    STDMETHOD(OMSetBlendState)(ID3D11BlendState*, const FLOAT[4], UINT) override;
    STDMETHOD(OMSetDepthStencilState)(ID3D11DepthStencilState*, UINT) override;

    STDMETHOD(RSSetState)(ID3D11RasterizerState*) override;
    STDMETHOD(RSSetViewports)(UINT, const D3D11_VIEWPORT*) override;
    STDMETHOD(RSSetScissorRects)(UINT, const RECT*) override;

    STDMETHOD(ClearRenderTargetView)(ID3D11RenderTargetView*, const FLOAT[4]) override;
    STDMETHOD(ClearDepthStencilView)(ID3D11DepthStencilView*, UINT, FLOAT, UINT8) override;

    STDMETHOD(DrawIndexed)(UINT, UINT, INT) override;
    STDMETHOD(Draw)(UINT, UINT) override;
    STDMETHOD(DrawIndexedInstanced)(UINT, UINT, UINT, INT, UINT) override;
    STDMETHOD(DrawInstanced)(UINT, UINT, UINT, UINT) override;

    STDMETHOD(Map)(ID3D11Resource*, UINT, UINT, UINT, void*) override;
    STDMETHOD(Unmap)(ID3D11Resource*, UINT) override;

    STDMETHOD(GenerateMips)(ID3D11ShaderResourceView*) override;
    STDMETHOD(CopyResource)(ID3D11Resource*, ID3D11Resource*) override;
    STDMETHOD(UpdateSubresource)(ID3D11Resource*, UINT, const void*, const void*, UINT, UINT) override;
    STDMETHOD(CopySubresourceRegion)(ID3D11Resource*, UINT, UINT, UINT, UINT, ID3D11Resource*, UINT, const void*) override;
    STDMETHOD(ResolveSubresource)(ID3D11Resource*, UINT, ID3D11Resource*, UINT, DXGI_FORMAT) override;
    STDMETHOD(Begin)(ID3D11Query*) override;
    STDMETHOD(End)(ID3D11Query*) override;
    STDMETHOD(GetData)(ID3D11Query*, void*, UINT, UINT) override;
    STDMETHOD(SetPredication)(ID3D11Predicate*, INT) override;

    STDMETHOD(FinishCommandList)(INT, ID3D11CommandList**) override;
    STDMETHOD(ExecuteCommandList)(ID3D11CommandList*, INT) override;

private:
    template<typename F>
    void record(F&& fn) {
        if (m_commandList) m_commandList->record(std::forward<F>(fn));
    }

    ULONG m_refCount = 1;
    D3D11Device& m_device;
    CommandList* m_commandList = new CommandList();
};

}
