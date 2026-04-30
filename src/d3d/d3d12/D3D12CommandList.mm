#include <metalsharp/D3D12Device.h>
#include <metalsharp/PipelineState.h>
#include <metalsharp/ArgumentBufferBinding.h>
#include <metalsharp/Logger.h>
#include <Foundation/Foundation.h>
#include <Metal/Metal.h>
#include <cstring>

namespace metalsharp {

HRESULT D3D12DeviceImpl::CreateCommandList(UINT, UINT, ID3D12CommandAllocator* pAllocator, ID3D12PipelineState*, REFIID riid, void** ppCommandList) {
    if (!ppCommandList) return E_POINTER;

    struct CmdListImpl final : public ID3D12GraphicsCommandList {
        ULONG refCount = 1;
        D3D12DeviceImpl& device;
        bool m_closed = false;
        bool m_recording = true;

        struct VertexBufferBinding {
            void* metalBuffer = nullptr;
            UINT size = 0;
            UINT stride = 0;
            UINT64 offset = 0;
        };
        VertexBufferBinding m_vertexBuffers[32] = {};
        struct IndexBufferBinding {
            void* metalBuffer = nullptr;
            UINT size = 0;
            UINT format = 0;
            UINT64 offset = 0;
        } m_indexBuffer = {};
        UINT m_primitiveTopology = D3D12_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        void* m_pipelineState = nullptr;
        ID3D12PipelineState* m_pso = nullptr;

        void* m_renderTargets[8] = {};
        void* m_depthTarget = nullptr;
        UINT m_numRenderTargets = 0;
        UINT m_rtvFormats[MAX_RENDER_TARGETS] = {};
        UINT m_dsvFormat = 0;

        D3D12RootSignatureImpl* m_rootSignature = nullptr;
        std::vector<uint8_t> m_argumentBuffer;
        size_t MAX_ARGUMENT_BUFFER_SIZE = 4096;
        std::vector<ID3D12Resource*> m_referencedResources;
        ID3D12DescriptorHeap* m_descriptorHeaps[2] = {};
        UINT m_numDescriptorHeaps = 0;

        struct ClearCmd {
            void* texture;
            bool isDepth;
            FLOAT color[4];
            FLOAT depth;
            UINT8 stencil;
            UINT clearFlags;
        };
        std::vector<ClearCmd> m_clearCmds;

        struct DrawCmd {
            bool indexed;
            UINT vertexCount;
            UINT instanceCount;
            UINT startIndex;
            INT baseVertex;
            UINT startInstance;
        };
        std::vector<DrawCmd> m_drawCmds;

        struct CopyCmd {
            ID3D12Resource* dst;
            ID3D12Resource* src;
            bool isBuffer;
            UINT64 dstOffset;
            UINT64 srcOffset;
            UINT64 numBytes;
        };
        std::vector<CopyCmd> m_copyCmds;

        std::vector<D3D12_RESOURCE_BARRIER> m_barriers;

        D3D12_VIEWPORT m_viewport = {};
        D3D12_RECT m_scissorRect = {};

        explicit CmdListImpl(D3D12DeviceImpl& dev) : device(dev) {
            m_argumentBuffer.resize(MAX_ARGUMENT_BUFFER_SIZE, 0);
        }

        ~CmdListImpl() {
            for (auto* res : m_referencedResources) {
                if (res) res->Release();
            }
        }

        HRESULT QueryInterface(REFIID riid, void** ppv) override {
            if (!ppv) return E_POINTER;
            if (riid == IID_ID3D12GraphicsCommandList || riid == IID_IUnknown) { AddRef(); *ppv = this; return S_OK; }
            return E_NOINTERFACE;
        }
        ULONG AddRef() override { return ++refCount; }
        ULONG Release() override { ULONG c = --refCount; if (c == 0) delete this; return c; }
        STDMETHOD(GetPrivateData)(const GUID&, UINT*, void*) override { return E_NOTIMPL; }
        STDMETHOD(SetPrivateData)(const GUID&, UINT, const void*) override { return E_NOTIMPL; }
        STDMETHOD(SetPrivateDataInterface)(const GUID&, const IUnknown*) override { return E_NOTIMPL; }
        STDMETHOD(SetName)(const char*) override { return S_OK; }

        HRESULT Close() override { m_closed = true; m_recording = false; return S_OK; }
        HRESULT Reset(ID3D12CommandAllocator* pAllocator, ID3D12PipelineState* pInitialState) override {
            m_closed = false;
            m_recording = true;
            m_clearCmds.clear();
            m_drawCmds.clear();
            m_copyCmds.clear();
            m_barriers.clear();
            m_numRenderTargets = 0;
            memset(m_renderTargets, 0, sizeof(m_renderTargets));
            m_depthTarget = nullptr;
            m_pso = pInitialState;
            m_rootSignature = nullptr;
            std::fill(m_argumentBuffer.begin(), m_argumentBuffer.end(), 0);
            m_numDescriptorHeaps = 0;
            for (auto* res : m_referencedResources) { if (res) res->Release(); }
            m_referencedResources.clear();
            return S_OK;
        }

        HRESULT IASetPrimitiveTopology(UINT topo) override { m_primitiveTopology = topo; return S_OK; }

        HRESULT IASetVertexBuffers(UINT start, UINT num, const D3D12_VERTEX_BUFFER_VIEW* pViews) override {
            if (!pViews) return S_OK;
            for (UINT i = 0; i < num && (start + i) < 32; ++i) {
                m_vertexBuffers[start + i].offset = pViews[i].BufferLocation;
                m_vertexBuffers[start + i].size = pViews[i].SizeInBytes;
                m_vertexBuffers[start + i].stride = pViews[i].StrideInBytes;
            }
            return S_OK;
        }

        HRESULT IASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW* pView) override {
            if (!pView) return S_OK;
            m_indexBuffer.offset = pView->BufferLocation;
            m_indexBuffer.size = pView->SizeInBytes;
            m_indexBuffer.format = pView->Format;
            return S_OK;
        }

        HRESULT SetPipelineState(ID3D12PipelineState* pPSO) override { m_pso = pPSO; return S_OK; }

        HRESULT SetGraphicsRootSignature(ID3D12RootSignature* pRS) override {
            m_rootSignature = static_cast<D3D12RootSignatureImpl*>(pRS);
            if (m_rootSignature) {
                size_t needed = m_rootSignature->argumentBufferSize;
                if (needed > m_argumentBuffer.size())
                    m_argumentBuffer.resize(needed, 0);
                std::fill(m_argumentBuffer.begin(), m_argumentBuffer.begin() + needed, 0);
            }
            return S_OK;
        }

        HRESULT SetGraphicsRootDescriptorTable(UINT rootParamIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor) override {
            if (!m_rootSignature || rootParamIndex >= m_rootSignature->parameterLayouts.size()) return S_OK;
            auto& layout = m_rootSignature->parameterLayouts[rootParamIndex];

            if (layout.offset + sizeof(uint64_t) <= m_argumentBuffer.size()) {
                uint64_t ptr = baseDescriptor.ptr;
                memcpy(m_argumentBuffer.data() + layout.offset, &ptr, sizeof(uint64_t));
            }
            return S_OK;
        }

        HRESULT SetGraphicsRootConstantBufferView(UINT rootParamIndex, D3D12_GPU_VIRTUAL_ADDRESS bufferLocation) override {
            if (!m_rootSignature || rootParamIndex >= m_rootSignature->parameterLayouts.size()) return S_OK;
            auto& layout = m_rootSignature->parameterLayouts[rootParamIndex];

            if (layout.offset + sizeof(uint64_t) <= m_argumentBuffer.size()) {
                memcpy(m_argumentBuffer.data() + layout.offset, &bufferLocation, sizeof(uint64_t));
            }
            return S_OK;
        }

        HRESULT SetGraphicsRootShaderResourceView(UINT rootParamIndex, D3D12_GPU_VIRTUAL_ADDRESS bufferLocation) override {
            if (!m_rootSignature || rootParamIndex >= m_rootSignature->parameterLayouts.size()) return S_OK;
            auto& layout = m_rootSignature->parameterLayouts[rootParamIndex];

            if (layout.offset + sizeof(uint64_t) <= m_argumentBuffer.size()) {
                memcpy(m_argumentBuffer.data() + layout.offset, &bufferLocation, sizeof(uint64_t));
            }
            return S_OK;
        }

        HRESULT SetGraphicsRootUnorderedAccessView(UINT rootParamIndex, D3D12_GPU_VIRTUAL_ADDRESS bufferLocation) override {
            if (!m_rootSignature || rootParamIndex >= m_rootSignature->parameterLayouts.size()) return S_OK;
            auto& layout = m_rootSignature->parameterLayouts[rootParamIndex];

            if (layout.offset + sizeof(uint64_t) <= m_argumentBuffer.size()) {
                memcpy(m_argumentBuffer.data() + layout.offset, &bufferLocation, sizeof(uint64_t));
            }
            return S_OK;
        }

        HRESULT SetGraphicsRoot32BitConstants(UINT rootParamIndex, UINT num32BitValues, const void* pData, UINT destOffset) override {
            if (!pData || num32BitValues == 0) return S_OK;
            if (!m_rootSignature || rootParamIndex >= m_rootSignature->parameterLayouts.size()) return S_OK;
            auto& layout = m_rootSignature->parameterLayouts[rootParamIndex];

            size_t writeOffset = layout.offset + destOffset * sizeof(uint32_t);
            size_t writeSize = num32BitValues * sizeof(uint32_t);
            if (writeOffset + writeSize <= m_argumentBuffer.size()) {
                memcpy(m_argumentBuffer.data() + writeOffset, pData, writeSize);
            }
            return S_OK;
        }

        HRESULT OMSetRenderTargets(UINT numRTVs, const D3D12_CPU_DESCRIPTOR_HANDLE* pRTVs, BOOL, const D3D12_CPU_DESCRIPTOR_HANDLE* pDSV) override {
            m_numRenderTargets = numRTVs;
            if (pRTVs) {
                for (UINT i = 0; i < numRTVs && i < 8; ++i) {
                    D3D12DescriptorHeapImpl* heap = device.findHeapForHandle(pRTVs[i]);
                    if (heap) {
                        auto* desc = heap->getDescriptor(pRTVs[i]);
                        if (desc) m_renderTargets[i] = desc->metalTexture;
                    } else {
                        m_renderTargets[i] = reinterpret_cast<void*>(pRTVs[i].ptr);
                    }
                }
            }
            if (pDSV) m_depthTarget = reinterpret_cast<void*>(pDSV->ptr);
            return S_OK;
        }
        HRESULT OMSetStencilRef(UINT ref) override { return S_OK; }
        HRESULT OMSetBlendFactor(const FLOAT factor[4]) override { return S_OK; }

        HRESULT RSSetViewports(UINT num, const D3D12_VIEWPORT* pVPs) override {
            if (num > 0 && pVPs) m_viewport = pVPs[0];
            return S_OK;
        }
        HRESULT RSSetScissorRects(UINT num, const D3D12_RECT* pRects) override {
            if (num > 0 && pRects) m_scissorRect = pRects[0];
            return S_OK;
        }

        HRESULT ClearRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE handle, const FLOAT color[4], UINT, const D3D12_RECT*) override {
            ClearCmd cmd = {};
            D3D12DescriptorHeapImpl* heap = device.findHeapForHandle(handle);
            if (heap) {
                auto* desc = heap->getDescriptor(handle);
                if (desc) cmd.texture = desc->metalTexture;
            }
            if (!cmd.texture) cmd.texture = reinterpret_cast<void*>(handle.ptr);
            cmd.isDepth = false;
            if (color) memcpy(cmd.color, color, sizeof(cmd.color));
            m_clearCmds.push_back(cmd);
            return S_OK;
        }

        HRESULT ClearDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE handle, UINT flags, FLOAT depth, UINT8 stencil, UINT, const D3D12_RECT*) override {
            ClearCmd cmd = {};
            D3D12DescriptorHeapImpl* heap = device.findHeapForHandle(handle);
            if (heap) {
                auto* desc = heap->getDescriptor(handle);
                if (desc) cmd.texture = desc->metalTexture;
            }
            if (!cmd.texture) cmd.texture = reinterpret_cast<void*>(handle.ptr);
            cmd.isDepth = true;
            cmd.depth = depth;
            cmd.stencil = stencil;
            cmd.clearFlags = flags;
            m_clearCmds.push_back(cmd);
            return S_OK;
        }

        HRESULT DrawInstanced(UINT vertCount, UINT instCount, UINT startVert, UINT startInst) override {
            m_drawCmds.push_back({false, vertCount, instCount, startVert, 0, startInst});
            return S_OK;
        }

        HRESULT DrawIndexedInstanced(UINT idxCount, UINT instCount, UINT startIdx, INT baseVert, UINT startInst) override {
            m_drawCmds.push_back({true, idxCount, instCount, startIdx, baseVert, startInst});
            return S_OK;
        }

        HRESULT Dispatch(UINT, UINT, UINT) override { return S_OK; }

        HRESULT CopyResource(ID3D12Resource* dst, ID3D12Resource* src) override {
            if (!dst || !src) return E_INVALIDARG;
            dst->AddRef();
            src->AddRef();
            m_copyCmds.push_back({dst, src, false, 0, 0, 0});
            return S_OK;
        }

        HRESULT CopyBufferRegion(ID3D12Resource* dst, UINT64 dstOff, ID3D12Resource* src, UINT64 srcOff, UINT64 numBytes) override {
            if (!dst || !src) return E_INVALIDARG;
            dst->AddRef();
            src->AddRef();
            m_copyCmds.push_back({dst, src, true, dstOff, srcOff, numBytes});
            return S_OK;
        }

        HRESULT CopyTextureRegion(const void* pDst, UINT dstX, UINT dstY, UINT dstZ, ID3D12Resource* pSrc, UINT srcSub, const void* pSrcBox) override {
            if (!pSrc) return E_INVALIDARG;
            pSrc->AddRef();
            m_copyCmds.push_back({nullptr, pSrc, false, (UINT64)dstX | ((UINT64)dstY << 16), srcSub, (UINT64)dstZ});
            return S_OK;
        }

        HRESULT ResourceBarrier(UINT numBarriers, const D3D12_RESOURCE_BARRIER* pBarriers) override {
            for (UINT i = 0; i < numBarriers; ++i) {
                m_barriers.push_back(pBarriers[i]);
                if (pBarriers[i].pResource) {
                    pBarriers[i].pResource->__setResourceState(pBarriers[i].StateAfter);
                }
            }
            return S_OK;
        }

        HRESULT SetDescriptorHeaps(UINT numHeaps, ID3D12DescriptorHeap* const* ppHeaps) override {
            m_numDescriptorHeaps = std::min(numHeaps, (UINT)2);
            for (UINT i = 0; i < m_numDescriptorHeaps; ++i) {
                m_descriptorHeaps[i] = ppHeaps ? ppHeaps[i] : nullptr;
            }
            return S_OK;
        }
        HRESULT IASetInputLayout(UINT, const D3D12_INPUT_ELEMENT_DESC*) override { return S_OK; }
        HRESULT Map(ID3D12Resource* pRes, UINT sub, const D3D12_RANGE*, void** ppData) override {
            if (!pRes || !ppData) return E_POINTER;
            return pRes->Map(sub, nullptr, ppData);
        }
        HRESULT Unmap(ID3D12Resource* pRes, UINT sub, const D3D12_RANGE*) override {
            if (!pRes) return E_POINTER;
            return pRes->Unmap(sub, nullptr);
        }
        HRESULT ExecuteIndirect(ID3D12CommandSignature*, UINT, ID3D12Resource*, UINT64, ID3D12Resource*, UINT64) override { return E_NOTIMPL; }

        HRESULT CopyTiles(ID3D12Resource* pTiledResource, const D3D12_TILED_RESOURCE_COORDINATE* pCoord, const D3D12_TILE_REGION_SIZE* pSize, ID3D12Resource* pBuffer, UINT64 BufferStartOffset, UINT Flags) override {
            if (!pTiledResource || !pCoord || !pSize || !pBuffer) return E_INVALIDARG;

            void* srcTex = pTiledResource->__metalTexturePtr();
            void* dstBuf = pBuffer->__metalBufferPtr();
            if (!srcTex || !dstBuf) return E_FAIL;

            const UINT TILE_BYTES = 65536;
            UINT numTiles = pSize->NumTiles;

            if (pSize->bUseBox) {
                numTiles = pSize->Width * pSize->Height * pSize->Depth;
                if (numTiles == 0) numTiles = pSize->NumTiles;
            }

            id<MTLTexture> tex = (__bridge id<MTLTexture>)srcTex;
            id<MTLBuffer> buf = (__bridge id<MTLBuffer>)dstBuf;

            for (UINT t = 0; t < numTiles; t++) {
                UINT tileX = pCoord->X + (t % (pSize->Width > 0 ? pSize->Width : 1));
                UINT tileY = pCoord->Y + ((t / (pSize->Width > 0 ? pSize->Width : 1)) % (pSize->Height > 0 ? pSize->Height : 1));
                UINT tileZ = pCoord->Z + (t / ((pSize->Width > 0 ? pSize->Width : 1) * (pSize->Height > 0 ? pSize->Height : 1)));

                MTLRegion region = MTLRegionMake2D(tileX * 128, tileY * 128, 128, 128);
                NSUInteger bytesPerRow = 128 * 4;

                if ([tex respondsToSelector:@selector(supportsNonPrivateTextureWithFormat:)]) {
                    char zeros[512] = {};
                    [tex replaceRegion:region
                             mipmapLevel:pCoord->Subresource
                                   slice:tileZ
                               withBytes:zeros
                             bytesPerRow:bytesPerRow
                           bytesPerImage:0];
                }
            }

            return S_OK;
        }

        void __execute(void* dev) override {
            execute(*static_cast<MetalDevice*>(dev));
        }

        void execute(MetalDevice& metalDev) {
            id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)metalDev.nativeCommandQueue();
            id<MTLCommandBuffer> cmdBuffer = [queue commandBuffer];

            for (auto& cmd : m_copyCmds) {
                if (cmd.isBuffer && cmd.dst && cmd.src) {
                    void* dstBuf = cmd.dst->__metalBufferPtr();
                    void* srcBuf = cmd.src->__metalBufferPtr();
                    if (dstBuf && srcBuf) {
                        id<MTLBuffer> dst = (__bridge id<MTLBuffer>)dstBuf;
                        id<MTLBuffer> src = (__bridge id<MTLBuffer>)srcBuf;
                        memcpy((char*)[dst contents] + cmd.dstOffset,
                               (char*)[src contents] + cmd.srcOffset,
                               (size_t)cmd.numBytes);
                    }
                }
            }

            for (auto& clear : m_clearCmds) {
                if (!clear.texture) continue;
                MTLRenderPassDescriptor* passDesc = [[MTLRenderPassDescriptor alloc] init];
                if (clear.isDepth) {
                    passDesc.depthAttachment.texture = (__bridge id<MTLTexture>)clear.texture;
                    passDesc.depthAttachment.loadAction = MTLLoadActionClear;
                    passDesc.depthAttachment.storeAction = MTLStoreActionStore;
                    passDesc.depthAttachment.clearDepth = clear.depth;
                } else {
                    passDesc.colorAttachments[0].texture = (__bridge id<MTLTexture>)clear.texture;
                    passDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
                    passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
                    passDesc.colorAttachments[0].clearColor = MTLClearColorMake(
                        clear.color[0], clear.color[1], clear.color[2], clear.color[3]);
                }
                id<MTLRenderCommandEncoder> enc = [cmdBuffer renderCommandEncoderWithDescriptor:passDesc];
                [enc endEncoding];
            }

            if (!m_drawCmds.empty() && m_pso) {
                void* pipelinePtr = m_pso->__metalRenderPipelineState();
                if (pipelinePtr) {
                    id<MTLRenderPipelineState> pipeline = (__bridge id<MTLRenderPipelineState>)pipelinePtr;

                    MTLRenderPassDescriptor* passDesc = [[MTLRenderPassDescriptor alloc] init];
                    for (UINT i = 0; i < m_numRenderTargets && i < 8; ++i) {
                        if (m_renderTargets[i]) {
                            passDesc.colorAttachments[i].texture = (__bridge id<MTLTexture>)m_renderTargets[i];
                            passDesc.colorAttachments[i].loadAction = MTLLoadActionLoad;
                            passDesc.colorAttachments[i].storeAction = MTLStoreActionStore;
                        }
                    }
                    if (m_depthTarget) {
                        passDesc.depthAttachment.texture = (__bridge id<MTLTexture>)m_depthTarget;
                        passDesc.depthAttachment.loadAction = MTLLoadActionLoad;
                        passDesc.depthAttachment.storeAction = MTLStoreActionStore;
                    }

                    id<MTLRenderCommandEncoder> enc = [cmdBuffer renderCommandEncoderWithDescriptor:passDesc];
                    [enc setRenderPipelineState:pipeline];

                    if (m_viewport.Width > 0) {
                        MTLViewport vp;
                        vp.originX = m_viewport.TopLeftX;
                        vp.originY = m_viewport.TopLeftY;
                        vp.width = m_viewport.Width;
                        vp.height = m_viewport.Height;
                        vp.znear = m_viewport.MinDepth;
                        vp.zfar = m_viewport.MaxDepth;
                        [enc setViewport:vp];
                    }

                    if (m_scissorRect.right > m_scissorRect.left) {
                        MTLScissorRect scissor;
                        scissor.x = m_scissorRect.left;
                        scissor.y = m_scissorRect.top;
                        scissor.width = m_scissorRect.right - m_scissorRect.left;
                        scissor.height = m_scissorRect.bottom - m_scissorRect.top;
                        [enc setScissorRect:scissor];
                    }

                    if (m_rootSignature && !m_argumentBuffer.empty()) {
                        id<MTLDevice> mtlDev = MTLCreateSystemDefaultDevice();
                        if (mtlDev) {
                            id<MTLBuffer> argBuf = [mtlDev newBufferWithBytes:m_argumentBuffer.data()
                                                                       length:m_rootSignature->argumentBufferSize
                                                                      options:MTLResourceStorageModeShared];
                            if (argBuf) {
                                [enc setVertexBuffer:argBuf offset:0 atIndex:16];
                                [enc setFragmentBuffer:argBuf offset:0 atIndex:16];
                            }
                        }
                    }

                    for (auto* res : m_referencedResources) {
                        if (!res) continue;
                        void* buf = res->__metalBufferPtr();
                        void* tex = res->__metalTexturePtr();
                        if (buf) [enc useResource:(__bridge id<MTLBuffer>)buf usage:MTLResourceUsageRead stages:MTLRenderStageVertex | MTLRenderStageFragment];
                        if (tex) [enc useResource:(__bridge id<MTLTexture>)tex usage:MTLResourceUsageRead stages:MTLRenderStageVertex | MTLRenderStageFragment];
                    }

                    MTLPrimitiveType primType = MTLPrimitiveTypeTriangle;
                    switch (m_primitiveTopology) {
                        case D3D12_PRIMITIVE_TOPOLOGY_POINTLIST: primType = MTLPrimitiveTypePoint; break;
                        case D3D12_PRIMITIVE_TOPOLOGY_LINELIST: primType = MTLPrimitiveTypeLine; break;
                        case D3D12_PRIMITIVE_TOPOLOGY_LINESTRIP: primType = MTLPrimitiveTypeLineStrip; break;
                        case D3D12_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP: primType = MTLPrimitiveTypeTriangleStrip; break;
                        default: break;
                    }

                    for (auto& draw : m_drawCmds) {
                        if (draw.indexed) {
                            [enc drawIndexedPrimitives:primType
                                            indexCount:draw.vertexCount
                                             indexType:MTLIndexTypeUInt32
                                           indexBuffer:(__bridge id<MTLBuffer>)m_indexBuffer.metalBuffer
                                     indexBufferOffset:0];
                        } else {
                            [enc drawPrimitives:primType
                                     vertexStart:draw.startIndex
                                     vertexCount:draw.vertexCount
                                   instanceCount:draw.instanceCount > 1 ? draw.instanceCount : 1
                                    baseInstance:0];
                        }
                    }
                    [enc endEncoding];
                }
            }

            [cmdBuffer commit];
        }
    };

    *ppCommandList = new CmdListImpl(*this);
    return S_OK;
}

HRESULT D3D12DeviceImpl::CreateGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc, REFIID riid, void** ppPSO) {
    if (!pDesc || !ppPSO) return E_INVALIDARG;

    auto* pso = new D3D12PipelineStateImpl();

    if (pDesc->VS && pDesc->VSsize > 0) {
        PipelineStateDesc pipeDesc;
        pipeDesc.vertexStride = 0;

        CompiledShader compiled;
        const uint8_t* shaderData = static_cast<const uint8_t*>(pDesc->VS);
        size_t shaderSize = pDesc->VSsize;

        D3D12RootSignatureImpl* rootSig = static_cast<D3D12RootSignatureImpl*>(pDesc->pRootSignature);
        bool ok = false;
        if (rootSig && !rootSig->rawBytecode.empty()) {
            ok = m_shaderTranslator->translateDXBCWithRootSignature(
                shaderData, shaderSize,
                ShaderStage::Vertex,
                rootSig->rawBytecode.data(), rootSig->rawBytecode.size(),
                compiled
            );
        }

        if (!ok) {
            ok = m_shaderTranslator->translateDXBC(shaderData, shaderSize, ShaderStage::Vertex, compiled);
        }

        if (ok) {
            pipeDesc.vertexFunction = compiled.vertexFunction;
        }

        if (pDesc->PS && pDesc->PSsize > 0) {
            CompiledShader psCompiled;
            if (m_shaderTranslator->translateDXBC(static_cast<const uint8_t*>(pDesc->PS), pDesc->PSsize, ShaderStage::Pixel, psCompiled)) {
                pipeDesc.fragmentFunction = psCompiled.fragmentFunction;
            }
        }

        pipeDesc.numColorAttachments = pDesc->NumRenderTargets;
        for (UINT i = 0; i < pDesc->NumRenderTargets && i < 8; ++i) {
            pipeDesc.colorPixelFormats[i] = dxgiFormatToMetal((DXGITranslation)pDesc->RTVFormats[i]);
        }

        PipelineState* pipeline = PipelineState::create(*m_metalDevice, pipeDesc);
        if (pipeline) {
            pso->m_renderPipeline = pipeline->nativeRenderPipelineState();
        }
    }

    *ppPSO = pso;
    return S_OK;
}

HRESULT D3D12CommandQueueImpl::ExecuteCommandLists(UINT numLists, ID3D12CommandList* const* ppLists) {
    for (UINT i = 0; i < numLists; ++i) {
        if (!ppLists[i]) continue;
        ppLists[i]->__execute(&metalDevice);
    }
    return S_OK;
}

D3D12DescriptorHeapImpl* D3D12DeviceImpl::findHeapForHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle) const {
    for (auto* h : m_trackedHeaps) {
        if (h->handleToIndex(handle) != UINT_MAX) return h;
    }
    return nullptr;
}

}
