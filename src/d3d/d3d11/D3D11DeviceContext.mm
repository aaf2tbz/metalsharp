#include <metalsharp/D3D11DeviceContext.h>
#include <metalsharp/D3D11Device.h>
#include <metalsharp/PipelineState.h>
#include <Foundation/Foundation.h>
#include <Metal/Metal.h>
#include <cstring>

namespace metalsharp {

D3D11DeviceContext::D3D11DeviceContext(D3D11Device& device) : m_device(device) {}

HRESULT D3D11DeviceContext::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceContext)) {
        AddRef(); *ppvObject = this; return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG D3D11DeviceContext::AddRef() { return ++m_refCount; }
ULONG D3D11DeviceContext::Release() { ULONG c = --m_refCount; if (c == 0) delete this; return c; }

HRESULT D3D11DeviceContext::GetDevice(ID3D11Device** ppDevice) {
    if (!ppDevice) return E_POINTER;
    m_device.AddRef();
    *ppDevice = &m_device;
    return S_OK;
}

HRESULT D3D11DeviceContext::GetPrivateData(const GUID&, UINT*, void*) { return E_NOTIMPL; }
HRESULT D3D11DeviceContext::SetPrivateData(const GUID&, UINT, const void*) { return E_NOTIMPL; }
HRESULT D3D11DeviceContext::SetPrivateDataInterface(const GUID&, const IUnknown*) { return E_NOTIMPL; }

HRESULT D3D11DeviceContext::IASetInputLayout(ID3D11InputLayout* pInputLayout) {
    m_inputLayout = pInputLayout;
    m_cachedPipeline.reset();
    return S_OK;
}

HRESULT D3D11DeviceContext::IASetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppVertexBuffers, const UINT* pStrides, const UINT* pOffsets) {
    for (UINT i = 0; i < NumBuffers && (StartSlot + i) < MAX_VERTEX_BUFFERS; ++i) {
        m_vertexBuffers[StartSlot + i].buffer = ppVertexBuffers ? ppVertexBuffers[i] : nullptr;
        m_vertexBuffers[StartSlot + i].stride = pStrides ? pStrides[i] : 0;
        m_vertexBuffers[StartSlot + i].offset = pOffsets ? pOffsets[i] : 0;
    }
    m_cachedPipeline.reset();
    return S_OK;
}

HRESULT D3D11DeviceContext::IASetIndexBuffer(ID3D11Buffer* pIndexBuffer, DXGI_FORMAT Format, UINT Offset) {
    m_indexBuffer = pIndexBuffer;
    m_indexBufferFormat = Format;
    m_indexBufferOffset = Offset;
    return S_OK;
}

HRESULT D3D11DeviceContext::IASetPrimitiveTopology(UINT Topology) {
    m_primitiveTopology = Topology;
    return S_OK;
}

HRESULT D3D11DeviceContext::VSSetShader(ID3D11VertexShader* pVertexShader, ID3D11ClassInstance* const*, UINT) {
    m_vertexShader = pVertexShader;
    m_cachedPipeline.reset();
    return S_OK;
}

HRESULT D3D11DeviceContext::VSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) {
    for (UINT i = 0; i < NumBuffers && (StartSlot + i) < MAX_CONSTANT_BUFFERS; ++i) {
        m_vsConstantBuffers[StartSlot + i] = ppConstantBuffers ? ppConstantBuffers[i] : nullptr;
    }
    return S_OK;
}

HRESULT D3D11DeviceContext::VSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppSRVs) {
    for (UINT i = 0; i < NumViews && (StartSlot + i) < 128; ++i) {
        m_vsShaderResources[StartSlot + i] = ppSRVs ? ppSRVs[i] : nullptr;
    }
    return S_OK;
}

HRESULT D3D11DeviceContext::VSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) {
    for (UINT i = 0; i < NumSamplers && (StartSlot + i) < 16; ++i) {
        m_vsSamplers[StartSlot + i] = ppSamplers ? ppSamplers[i] : nullptr;
    }
    return S_OK;
}

HRESULT D3D11DeviceContext::PSSetShader(ID3D11PixelShader* pPixelShader, ID3D11ClassInstance* const*, UINT) {
    m_pixelShader = pPixelShader;
    m_cachedPipeline.reset();
    return S_OK;
}

HRESULT D3D11DeviceContext::PSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) {
    for (UINT i = 0; i < NumBuffers && (StartSlot + i) < MAX_CONSTANT_BUFFERS; ++i) {
        m_psConstantBuffers[StartSlot + i] = ppConstantBuffers ? ppConstantBuffers[i] : nullptr;
    }
    return S_OK;
}

HRESULT D3D11DeviceContext::PSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppSRVs) {
    for (UINT i = 0; i < NumViews && (StartSlot + i) < 128; ++i) {
        m_psShaderResources[StartSlot + i] = ppSRVs ? ppSRVs[i] : nullptr;
    }
    return S_OK;
}

HRESULT D3D11DeviceContext::PSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) {
    for (UINT i = 0; i < NumSamplers && (StartSlot + i) < 16; ++i) {
        m_psSamplers[StartSlot + i] = ppSamplers ? ppSamplers[i] : nullptr;
    }
    return S_OK;
}

HRESULT D3D11DeviceContext::GSSetShader(ID3D11GeometryShader*, ID3D11ClassInstance* const*, UINT) { return E_NOTIMPL; }
HRESULT D3D11DeviceContext::CSSetShader(ID3D11ComputeShader* pComputeShader, ID3D11ClassInstance* const*, UINT) { m_computeShader = pComputeShader; return S_OK; }

HRESULT D3D11DeviceContext::CSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppBuffers) {
    if (!ppBuffers) return S_OK;
    for (UINT i = 0; i < NumBuffers && StartSlot + i < MAX_CONSTANT_BUFFERS; ++i)
        m_vsConstantBuffers[StartSlot + i] = ppBuffers[i];
    return S_OK;
}

HRESULT D3D11DeviceContext::CSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppViews) {
    if (!ppViews) return S_OK;
    for (UINT i = 0; i < NumViews && StartSlot + i < 128; ++i)
        m_psShaderResources[StartSlot + i] = ppViews[i];
    return S_OK;
}

HRESULT D3D11DeviceContext::CSSetUnorderedAccessViews(UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView* const* ppUAVs, const UINT*) {
    if (!ppUAVs) return S_OK;
    for (UINT i = 0; i < NumUAVs && StartSlot + i < 8; ++i)
        m_csUAVs[StartSlot + i] = ppUAVs[i];
    return S_OK;
}

HRESULT D3D11DeviceContext::CSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) {
    if (!ppSamplers) return S_OK;
    for (UINT i = 0; i < NumSamplers && StartSlot + i < 16; ++i)
        m_psSamplers[StartSlot + i] = ppSamplers[i];
    return S_OK;
}

HRESULT D3D11DeviceContext::Dispatch(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ) { return E_NOTIMPL; }

HRESULT D3D11DeviceContext::OMSetRenderTargets(UINT NumViews, ID3D11RenderTargetView* const* ppRenderTargetViews, ID3D11DepthStencilView* pDepthStencilView) {
    m_renderTargets.fill(nullptr);
    for (UINT i = 0; i < NumViews && i < MAX_RENDER_TARGETS; ++i) {
        m_renderTargets[i] = ppRenderTargetViews ? ppRenderTargetViews[i] : nullptr;
    }
    m_depthStencilView = pDepthStencilView;
    m_cachedPipeline.reset();
    return S_OK;
}

HRESULT D3D11DeviceContext::OMSetRenderTargetsAndUnorderedAccessViews(UINT NumRTVs, ID3D11RenderTargetView* const* ppRTVs, ID3D11DepthStencilView* pDSV, UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*) {
    return OMSetRenderTargets(NumRTVs, ppRTVs, pDSV);
}

HRESULT D3D11DeviceContext::OMSetBlendState(ID3D11BlendState* pBlendState, const FLOAT BlendFactor[4], UINT SampleMask) {
    m_blendState = pBlendState;
    if (BlendFactor) memcpy(m_blendFactor, BlendFactor, sizeof(m_blendFactor));
    m_sampleMask = SampleMask;
    m_cachedPipeline.reset();
    return S_OK;
}

HRESULT D3D11DeviceContext::OMSetDepthStencilState(ID3D11DepthStencilState* pDSState, UINT StencilRef) {
    m_depthStencilState = pDSState;
    m_stencilRef = StencilRef;
    return S_OK;
}

HRESULT D3D11DeviceContext::RSSetState(ID3D11RasterizerState* pRasterizerState) {
    m_rasterizerState = pRasterizerState;
    return S_OK;
}

HRESULT D3D11DeviceContext::RSSetViewports(UINT NumViewports, const D3D11_VIEWPORT* pViewports) {
    if (NumViewports > 0 && pViewports) m_viewport = pViewports[0];
    return S_OK;
}

HRESULT D3D11DeviceContext::RSSetScissorRects(UINT, const RECT*) { return S_OK; }

HRESULT D3D11DeviceContext::ClearRenderTargetView(ID3D11RenderTargetView* pRTV, const FLOAT ColorRGBA[4]) {
    if (!pRTV || !m_renderTargets[0]) return S_OK;
    auto& metalDev = m_device.metalDevice();
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)metalDev.nativeCommandQueue();
    id<MTLCommandBuffer> cmdBuffer = [queue commandBuffer];

    void* texPtr = pRTV->__metalTexturePtr();
    if (!texPtr) return S_OK;

    MTLRenderPassDescriptor* passDesc = [[MTLRenderPassDescriptor alloc] init];
    passDesc.colorAttachments[0].texture = (__bridge id<MTLTexture>)texPtr;
    passDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
    passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
    passDesc.colorAttachments[0].clearColor = MTLClearColorMake(
        ColorRGBA[0], ColorRGBA[1], ColorRGBA[2], ColorRGBA[3]);

    id<MTLRenderCommandEncoder> encoder = [cmdBuffer renderCommandEncoderWithDescriptor:passDesc];
    [encoder endEncoding];
    [cmdBuffer commit];
    return S_OK;
}

HRESULT D3D11DeviceContext::ClearDepthStencilView(ID3D11DepthStencilView* pDSV, UINT ClearFlags, FLOAT Depth, UINT8 Stencil) {
    if (!pDSV) return S_OK;
    void* texPtr = pDSV->__metalTexturePtr();
    if (!texPtr) return S_OK;

    auto& metalDev = m_device.metalDevice();
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)metalDev.nativeCommandQueue();
    id<MTLCommandBuffer> cmdBuffer = [queue commandBuffer];

    MTLRenderPassDescriptor* passDesc = [[MTLRenderPassDescriptor alloc] init];
    passDesc.depthAttachment.texture = (__bridge id<MTLTexture>)texPtr;
    passDesc.depthAttachment.loadAction = MTLLoadActionClear;
    passDesc.depthAttachment.storeAction = MTLStoreActionStore;
    passDesc.depthAttachment.clearDepth = Depth;

    if (ClearFlags & D3D11_CLEAR_STENCIL) {
        passDesc.stencilAttachment.texture = (__bridge id<MTLTexture>)texPtr;
        passDesc.stencilAttachment.loadAction = MTLLoadActionClear;
        passDesc.stencilAttachment.storeAction = MTLStoreActionStore;
        passDesc.stencilAttachment.clearStencil = Stencil;
    }

    id<MTLRenderCommandEncoder> encoder = [cmdBuffer renderCommandEncoderWithDescriptor:passDesc];
    [encoder endEncoding];
    [cmdBuffer commit];
    return S_OK;
}

void D3D11DeviceContext::ensurePipeline() {
    if (m_cachedPipeline) return;

    PipelineStateDesc desc;
    desc.vertexStride = m_vertexBuffers[0].stride;

    if (m_vertexShader) desc.vertexFunction = m_vertexShader->__metalVertexFunction();
    if (m_pixelShader) desc.fragmentFunction = m_pixelShader->__metalFragmentFunction();

    desc.numColorAttachments = 0;
    for (UINT i = 0; i < MAX_RENDER_TARGETS; ++i) {
        if (!m_renderTargets[i]) continue;
        void* texPtr = m_renderTargets[i]->__metalTexturePtr();
        if (!texPtr) continue;
        id<MTLTexture> tex = (__bridge id<MTLTexture>)texPtr;
        desc.colorPixelFormats[i] = (uint32_t)tex.pixelFormat;
        desc.numColorAttachments = i + 1;

        if (m_blendState) {
            desc.blendEnabled[i] = m_blendState->__getBlendEnable(i) != 0;
            desc.srcBlend[i] = m_blendState->__getSrcBlend(i);
            desc.destBlend[i] = m_blendState->__getDestBlend(i);
            desc.blendOp[i] = m_blendState->__getBlendOp(i);
            desc.srcBlendAlpha[i] = m_blendState->__getSrcBlendAlpha(i);
            desc.destBlendAlpha[i] = m_blendState->__getDestBlendAlpha(i);
            desc.blendOpAlpha[i] = m_blendState->__getBlendOpAlpha(i);
            desc.renderTargetWriteMask[i] = m_blendState->__getRenderTargetWriteMask(i);
        }
    }

    if (desc.numColorAttachments == 0) {
        desc.colorPixelFormats[0] = MTLPixelFormatBGRA8Unorm;
        desc.numColorAttachments = 1;
    }

    if (m_depthStencilState) {
        desc.depthEnabled = m_depthStencilState->__getDepthEnable() != 0;
        desc.depthWriteEnabled = m_depthStencilState->__getDepthWriteMask() == D3D11_DEPTH_WRITE_MASK_ALL;
    }

    m_cachedPipeline.reset(PipelineState::create(m_device.metalDevice(), desc));
}

void D3D11DeviceContext::commitDraw(UINT vertexCount, UINT instanceCount, UINT startIndexLocation, INT baseVertexLocation, bool indexed) {
    if (!m_cachedPipeline) return;
    if (!m_vertexBuffers[0].buffer && !indexed) return;

    auto& metalDev = m_device.metalDevice();
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)metalDev.nativeCommandQueue();
    id<MTLCommandBuffer> cmdBuffer = [queue commandBuffer];

    MTLRenderPassDescriptor* passDesc = [[MTLRenderPassDescriptor alloc] init];

    for (UINT i = 0; i < MAX_RENDER_TARGETS; ++i) {
        if (!m_renderTargets[i]) continue;
        void* texPtr = m_renderTargets[i]->__metalTexturePtr();
        if (!texPtr) continue;
        passDesc.colorAttachments[i].texture = (__bridge id<MTLTexture>)texPtr;
        passDesc.colorAttachments[i].loadAction = MTLLoadActionLoad;
        passDesc.colorAttachments[i].storeAction = MTLStoreActionStore;
    }

    if (m_depthStencilView) {
        void* texPtr = m_depthStencilView->__metalTexturePtr();
        if (texPtr) {
            passDesc.depthAttachment.texture = (__bridge id<MTLTexture>)texPtr;
            passDesc.depthAttachment.loadAction = MTLLoadActionLoad;
            passDesc.depthAttachment.storeAction = MTLStoreActionStore;
        }
    }

    id<MTLRenderPipelineState> pipeline = (__bridge id<MTLRenderPipelineState>)m_cachedPipeline->nativeRenderPipelineState();

    id<MTLRenderCommandEncoder> encoder = [cmdBuffer renderCommandEncoderWithDescriptor:passDesc];
    [encoder setRenderPipelineState:pipeline];

    if (m_vertexBuffers[0].buffer) {
        void* bufPtr = m_vertexBuffers[0].buffer->__metalBufferPtr();
        if (bufPtr) {
            id<MTLBuffer> mtlBuf = (__bridge id<MTLBuffer>)bufPtr;
            [encoder setVertexBuffer:mtlBuf offset:m_vertexBuffers[0].offset atIndex:0];
        }
    }

    for (UINT i = 0; i < MAX_CONSTANT_BUFFERS; ++i) {
        if (m_vsConstantBuffers[i]) {
            void* bufPtr = m_vsConstantBuffers[i]->__metalBufferPtr();
            if (bufPtr) {
                id<MTLBuffer> mtlBuf = (__bridge id<MTLBuffer>)bufPtr;
                [encoder setVertexBuffer:mtlBuf offset:0 atIndex:i + 1];
            }
        }
        if (m_psConstantBuffers[i]) {
            void* bufPtr = m_psConstantBuffers[i]->__metalBufferPtr();
            if (bufPtr) {
                id<MTLBuffer> mtlBuf = (__bridge id<MTLBuffer>)bufPtr;
                [encoder setFragmentBuffer:mtlBuf offset:0 atIndex:i];
            }
        }
    }

    for (UINT i = 0; i < 128; ++i) {
        if (m_psShaderResources[i]) {
            void* texPtr = m_psShaderResources[i]->__metalTexturePtr();
            if (texPtr) {
                [encoder setFragmentTexture:(__bridge id<MTLTexture>)texPtr atIndex:i];
            }
        }
    }

    for (UINT i = 0; i < 16; ++i) {
        if (m_psSamplers[i]) {
            void* sampPtr = m_psSamplers[i]->__metalSamplerState();
            if (sampPtr) {
                [encoder setFragmentSamplerState:(__bridge id<MTLSamplerState>)sampPtr atIndex:i];
            }
        }
    }

    if (m_viewport.Width > 0 && m_viewport.Height > 0) {
        MTLViewport vp;
        vp.originX = m_viewport.TopLeftX;
        vp.originY = m_viewport.TopLeftY;
        vp.width = m_viewport.Width;
        vp.height = m_viewport.Height;
        vp.znear = m_viewport.MinDepth;
        vp.zfar = m_viewport.MaxDepth;
        [encoder setViewport:vp];
    }

    if (m_rasterizerState) {
        MTLCullMode cullMode = MTLCullModeNone;
        switch (m_rasterizerState->__getCullMode()) {
            case D3D11_CULL_FRONT: cullMode = MTLCullModeFront; break;
            case D3D11_CULL_BACK: cullMode = MTLCullModeBack; break;
            default: break;
        }
        [encoder setCullMode:cullMode];
        if (m_rasterizerState->__getFrontCCW()) {
            [encoder setFrontFacingWinding:MTLWindingCounterClockwise];
        } else {
            [encoder setFrontFacingWinding:MTLWindingClockwise];
        }
    }

    MTLPrimitiveType primType = MTLPrimitiveTypeTriangle;
    switch (m_primitiveTopology) {
        case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST:     primType = MTLPrimitiveTypePoint; break;
        case D3D11_PRIMITIVE_TOPOLOGY_LINELIST:      primType = MTLPrimitiveTypeLine; break;
        case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP:     primType = MTLPrimitiveTypeLineStrip; break;
        case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST:  primType = MTLPrimitiveTypeTriangle; break;
        case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP: primType = MTLPrimitiveTypeTriangleStrip; break;
        default: break;
    }

    if (indexed && m_indexBuffer) {
        void* idxBufPtr = m_indexBuffer->__metalBufferPtr();
        if (idxBufPtr) {
            MTLIndexType indexType = MTLIndexTypeUInt16;
            size_t indexSize = 2;
            if (m_indexBufferFormat == DXGI_FORMAT_R32_UINT) {
                indexType = MTLIndexTypeUInt32;
                indexSize = 4;
            }
            id<MTLBuffer> mtlIdxBuf = (__bridge id<MTLBuffer>)idxBufPtr;
            [encoder drawIndexedPrimitives:primType
                                indexCount:vertexCount
                                 indexType:indexType
                               indexBuffer:mtlIdxBuf
                         indexBufferOffset:m_indexBufferOffset + startIndexLocation * indexSize];
        }
    } else {
        [encoder drawPrimitives:primType vertexStart:startIndexLocation vertexCount:vertexCount instanceCount:instanceCount > 1 ? instanceCount : 1 baseInstance:0];
    }

    [encoder endEncoding];
    [cmdBuffer commit];
}

HRESULT D3D11DeviceContext::DrawIndexed(UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation) {
    ensurePipeline();
    commitDraw(IndexCount, 1, StartIndexLocation, BaseVertexLocation, true);
    return S_OK;
}

HRESULT D3D11DeviceContext::Draw(UINT VertexCount, UINT StartVertexLocation) {
    ensurePipeline();
    commitDraw(VertexCount, 1, StartVertexLocation, 0, false);
    return S_OK;
}

HRESULT D3D11DeviceContext::DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT) {
    ensurePipeline();
    commitDraw(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, true);
    return S_OK;
}

HRESULT D3D11DeviceContext::DrawInstanced(UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT) {
    ensurePipeline();
    commitDraw(VertexCountPerInstance, InstanceCount, StartVertexLocation, 0, false);
    return S_OK;
}

HRESULT D3D11DeviceContext::Map(ID3D11Resource* pResource, UINT, UINT MapType, UINT, void* pMappedResource) {
    if (!pResource || !pMappedResource) return E_INVALIDARG;

    auto* mapped = static_cast<D3D11_MAPPED_SUBRESOURCE*>(pMappedResource);
    memset(mapped, 0, sizeof(D3D11_MAPPED_SUBRESOURCE));

    void* bufPtr = pResource->__metalBufferPtr();
    if (bufPtr) {
        id<MTLBuffer> mtlBuf = (__bridge id<MTLBuffer>)bufPtr;
        mapped->pData = [mtlBuf contents];
        mapped->RowPitch = 0;
        mapped->DepthPitch = 0;
        return S_OK;
    }

    return E_FAIL;
}

HRESULT D3D11DeviceContext::Unmap(ID3D11Resource*, UINT) {
    return S_OK;
}

HRESULT D3D11DeviceContext::GenerateMips(ID3D11ShaderResourceView* pShaderResourceView) {
    if (!pShaderResourceView) return S_OK;
    void* texPtr = pShaderResourceView->__metalTexturePtr();
    if (!texPtr) return S_OK;

    id<MTLTexture> tex = (__bridge id<MTLTexture>)texPtr;
    if (tex.mipmapLevelCount <= 1) return S_OK;

    auto& metalDev = m_device.metalDevice();
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)metalDev.nativeCommandQueue();
    id<MTLCommandBuffer> cmdBuffer = [queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cmdBuffer blitCommandEncoder];

    for (NSUInteger level = 1; level < tex.mipmapLevelCount; ++level) {
        NSUInteger srcW = tex.width >> (level - 1);
        NSUInteger srcH = tex.height >> (level - 1);
        NSUInteger dstW = tex.width >> level;
        NSUInteger dstH = tex.height >> level;
        if (srcW == 0) srcW = 1;
        if (srcH == 0) srcH = 1;
        if (dstW == 0) dstW = 1;
        if (dstH == 0) dstH = 1;

        [blit generateMipmapsForTexture:tex];
        break;
    }

    [blit endEncoding];
    [cmdBuffer commit];
    return S_OK;
}

HRESULT D3D11DeviceContext::CopyResource(ID3D11Resource* pDst, ID3D11Resource* pSrc) {
    if (!pDst || !pSrc) return E_INVALIDARG;

    void* dstBuf = pDst->__metalBufferPtr();
    void* srcBuf = pSrc->__metalBufferPtr();
    if (dstBuf && srcBuf) {
        id<MTLBuffer> dst = (__bridge id<MTLBuffer>)dstBuf;
        id<MTLBuffer> src = (__bridge id<MTLBuffer>)srcBuf;
        memcpy([dst contents], [src contents], [src length] < [dst length] ? [src length] : [dst length]);
        return S_OK;
    }

    void* dstTex = pDst->__metalTexturePtr();
    void* srcTex = pSrc->__metalTexturePtr();
    if (dstTex && srcTex) {
        auto& metalDev = m_device.metalDevice();
        id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)metalDev.nativeCommandQueue();
        id<MTLCommandBuffer> cmdBuffer = [queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [cmdBuffer blitCommandEncoder];

        id<MTLTexture> dst = (__bridge id<MTLTexture>)dstTex;
        id<MTLTexture> src = (__bridge id<MTLTexture>)srcTex;

        for (NSUInteger level = 0; level < src.mipmapLevelCount && level < dst.mipmapLevelCount; ++level) {
            [blit copyFromTexture:src
                      sourceSlice:0
                      sourceLevel:level
                     sourceOrigin:MTLOriginMake(0, 0, 0)
                       sourceSize:MTLSizeMake(src.width >> level, src.height >> level, 1)
                        toTexture:dst
                 destinationSlice:0
                 destinationLevel:level
                destinationOrigin:MTLOriginMake(0, 0, 0)];
        }

        [blit endEncoding];
        [cmdBuffer commit];
        return S_OK;
    }

    return E_NOTIMPL;
}

HRESULT D3D11DeviceContext::UpdateSubresource(ID3D11Resource* pDst, UINT DstSubresource, const void* pDstBox, const void* pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch) {
    if (!pDst || !pSrcData) return E_INVALIDARG;

    void* dstBuf = pDst->__metalBufferPtr();
    if (dstBuf) {
        id<MTLBuffer> dst = (__bridge id<MTLBuffer>)dstBuf;
        memcpy([dst contents], pSrcData, [dst length]);
        return S_OK;
    }

    void* dstTex = pDst->__metalTexturePtr();
    if (dstTex) {
        id<MTLTexture> tex = (__bridge id<MTLTexture>)dstTex;
        MTLRegion region;
        if (pDstBox) {
            const UINT* box = static_cast<const UINT*>(pDstBox);
            region = MTLRegionMake2D(box[0], box[1], box[3] - box[0], box[4] - box[1]);
        } else {
            region = MTLRegionMake2D(0, 0, tex.width, tex.height);
        }
        NSUInteger bytesPerImage = SrcDepthPitch > 0 ? SrcDepthPitch : SrcRowPitch * tex.height;
        [tex replaceRegion:region
                   mipmapLevel:DstSubresource
                     slice:0
                 withBytes:pSrcData
               bytesPerRow:SrcRowPitch
             bytesPerImage:bytesPerImage];
        return S_OK;
    }

    return E_NOTIMPL;
}

HRESULT D3D11DeviceContext::CopySubresourceRegion(ID3D11Resource* pDst, UINT DstSubresource, UINT DstX, UINT DstY, UINT DstZ, ID3D11Resource* pSrc, UINT SrcSubresource, const void* pSrcBox) {
    if (!pDst || !pSrc) return E_INVALIDARG;

    void* dstTex = pDst->__metalTexturePtr();
    void* srcTex = pSrc->__metalTexturePtr();
    if (dstTex && srcTex) {
        auto& metalDev = m_device.metalDevice();
        id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)metalDev.nativeCommandQueue();
        id<MTLCommandBuffer> cmdBuffer = [queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [cmdBuffer blitCommandEncoder];

        id<MTLTexture> dst = (__bridge id<MTLTexture>)dstTex;
        id<MTLTexture> src = (__bridge id<MTLTexture>)srcTex;

        MTLSize srcSize;
        MTLOrigin srcOrigin;
        if (pSrcBox) {
            const UINT* box = static_cast<const UINT*>(pSrcBox);
            srcOrigin = MTLOriginMake(box[0], box[1], box[2]);
            srcSize = MTLSizeMake(box[3] - box[0], box[4] - box[1], box[5] - box[2]);
        } else {
            srcOrigin = MTLOriginMake(0, 0, 0);
            srcSize = MTLSizeMake(src.width, src.height, 1);
        }

        [blit copyFromTexture:src
                  sourceSlice:0
                  sourceLevel:SrcSubresource
                 sourceOrigin:srcOrigin
                   sourceSize:srcSize
                    toTexture:dst
             destinationSlice:0
             destinationLevel:DstSubresource
            destinationOrigin:MTLOriginMake(DstX, DstY, DstZ)];

        [blit endEncoding];
        [cmdBuffer commit];
        return S_OK;
    }

    void* dstBuf = pDst->__metalBufferPtr();
    void* srcBuf = pSrc->__metalBufferPtr();
    if (dstBuf && srcBuf) {
        id<MTLBuffer> dst = (__bridge id<MTLBuffer>)dstBuf;
        id<MTLBuffer> src = (__bridge id<MTLBuffer>)srcBuf;
        memcpy([dst contents], [src contents], [src length] < [dst length] ? [src length] : [dst length]);
        return S_OK;
    }

    return E_NOTIMPL;
}

HRESULT D3D11DeviceContext::ResolveSubresource(ID3D11Resource* pDst, UINT DstSubresource, ID3D11Resource* pSrc, UINT SrcSubresource, DXGI_FORMAT Format) {
    if (!pDst || !pSrc) return E_INVALIDARG;

    void* dstTex = pDst->__metalTexturePtr();
    void* srcTex = pSrc->__metalTexturePtr();
    if (dstTex && srcTex) {
        auto& metalDev = m_device.metalDevice();
        id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)metalDev.nativeCommandQueue();
        id<MTLCommandBuffer> cmdBuffer = [queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [cmdBuffer blitCommandEncoder];

        id<MTLTexture> dst = (__bridge id<MTLTexture>)dstTex;
        id<MTLTexture> src = (__bridge id<MTLTexture>)srcTex;

        [blit generateMipmapsForTexture:dst];

        [blit copyFromTexture:src
                  sourceSlice:0
                  sourceLevel:SrcSubresource
                 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:MTLSizeMake(src.width, src.height, 1)
                    toTexture:dst
             destinationSlice:0
             destinationLevel:DstSubresource
            destinationOrigin:MTLOriginMake(0, 0, 0)];

        [blit endEncoding];
        [cmdBuffer commit];
        return S_OK;
    }

    return E_NOTIMPL;
}

HRESULT D3D11DeviceContext::Begin(ID3D11Query* pQuery) {
    if (!pQuery) return S_OK;
    return S_OK;
}

HRESULT D3D11DeviceContext::End(ID3D11Query* pQuery) {
    if (!pQuery) return S_OK;
    return S_OK;
}

HRESULT D3D11DeviceContext::GetData(ID3D11Query* pQuery, void* pData, UINT DataSize, UINT GetDataFlags) {
    if (!pQuery) return S_OK;

    UINT queryType = pQuery->__getQueryType();
    if (pData && DataSize >= sizeof(UINT64)) {
        UINT64* result = static_cast<UINT64*>(pData);
        if (queryType == D3D11_QUERY_OCCLUSION) {
            *result = 1;
        } else if (queryType == D3D11_QUERY_TIMESTAMP) {
            *result = 0;
        } else if (queryType == D3D11_QUERY_EVENT) {
            *result = 1;
        }
    }

    return S_OK;
}

HRESULT D3D11DeviceContext::SetPredication(ID3D11Predicate* pPredicate, INT PredicateValue) {
    return S_OK;
}

}
