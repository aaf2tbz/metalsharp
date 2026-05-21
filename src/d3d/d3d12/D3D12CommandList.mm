/// @file D3D12CommandList.mm
/// @brief D3D12 graphics command list implementation translating to Metal encoders.
///
/// Command List Architecture
/// =========================
///
/// The D3D12GraphicsCommandList translates D3D12 rendering commands into Metal
/// render/blit/compute command encoders on a single MTLCommandBuffer.
///
/// Encoder Lifecycle:
///   Open() → creates MTLCommandBuffer from the device's command queue
///   ┌─ BeginRenderPass (OMSetRenderTargets) → MTLRenderCommandEncoder
///   │   SetPipelineState → setRenderPipelineState
///   │   SetGraphicsRootSignature/Table → encode arguments into MTLBuffer
///   │   IASetVertexBuffers/IndexBuffer → setVertexBuffer/setBytes
///   │   DrawInstanced/DrawIndexedInstanced → drawPrimitives/drawIndexedPrimitives
///   │   ResourceBarrier → no-op or endEncoding+beginEncoding for layout transitions
///   └─ EndRenderPass → endEncoding
///   Close() → the command buffer is ready for ExecuteCommandLists
///
/// Argument Buffer Encoding:
///   Each root signature parameter maps to a Metal argument buffer slot.
///   D3D12 root constants → setBytes (push constants, max 4KB)
///   D3D12 root descriptors (CBV/SRV/UAV) → setBuffer/setTexture on bind point
///   D3D12 descriptor tables → encode entire descriptor range into argument buffer
///
/// Resource State Tracking:
///   D3D12 requires explicit ResourceBarrier calls. Metal does not — resource
///   state is implicit. We track states for correctness but mostly no-op the
///   barriers. Split barriers are ignored.
///
/// Unsupported (stubs):
///   - Ray tracing (BuildRayTracingAccelerationStructure, DispatchRays)
///   - Mesh shaders (DispatchMesh)
///   - Sample feedback (WriteBufferImmediate)
///   - Bundle command lists (execute inline only)

#include <array>
#include <atomic>
#include <cstring>
#include <Foundation/Foundation.h>
#include <Metal/Metal.h>
#include <metalsharp/ArgumentBufferBinding.h>
#include <metalsharp/D3D12Device.h>
#include <metalsharp/Logger.h>
#include <metalsharp/PipelineState.h>
#include <thread>

namespace metalsharp {

static bool looksLikeMSL(const uint8_t* data, size_t size) {
    if (!data || size == 0)
        return false;
    std::string source(reinterpret_cast<const char*>(data), size);
    return source.find("#include <metal_stdlib>") != std::string::npos ||
           source.find("using namespace metal") != std::string::npos || source.find("kernel ") != std::string::npos;
}

static const char* d3d12ShaderSourceKind(const uint8_t* data, size_t size) {
    if (!data || size == 0)
        return "empty";
    if (looksLikeMSL(data, size))
        return "msl";

    auto& bridge = IRConverterBridge::instance();
    if (bridge.isDXIL(data, size))
        return "dxil";

    std::vector<uint8_t> dxil;
    if (bridge.extractDXILFromDXBC(data, size, dxil))
        return "dxbc_dxil";

    return "dxbc_or_unknown";
}

static uint32_t d3d12FormatSize(UINT format) {
    switch (format) {
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
        return 16;
    case DXGI_FORMAT_R32G32B32_FLOAT:
        return 12;
    case DXGI_FORMAT_R32G32_FLOAT:
        return 8;
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT:
        return 4;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return 8;
    case DXGI_FORMAT_R16G16_FLOAT:
        return 4;
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_R16_UINT:
        return 2;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        return 4;
    case DXGI_FORMAT_R8G8_UNORM:
        return 2;
    case DXGI_FORMAT_R8_UNORM:
        return 1;
    default:
        return 0;
    }
}

static uint32_t d3d12VertexStrideForSlot(const D3D12_INPUT_ELEMENT_DESC* elements, UINT numElements, UINT inputSlot) {
    if (!elements)
        return 0;

    std::array<uint64_t, 32> nextOffsets = {};
    std::array<uint64_t, 32> strides = {};

    for (UINT i = 0; i < numElements; ++i) {
        const D3D12_INPUT_ELEMENT_DESC& elem = elements[i];
        if (elem.InputSlot >= nextOffsets.size())
            continue;

        uint32_t elemSize = d3d12FormatSize(elem.Format);
        if (elemSize == 0)
            continue;

        uint64_t elemOffset = elem.AlignedByteOffset == D3D12_APPEND_ALIGNED_ELEMENT ? nextOffsets[elem.InputSlot]
                                                                                     : elem.AlignedByteOffset;
        uint64_t elemEnd = elemOffset + elemSize;
        nextOffsets[elem.InputSlot] = elemEnd;
        if (elemEnd > strides[elem.InputSlot])
            strides[elem.InputSlot] = elemEnd;
    }

    if (inputSlot >= strides.size())
        return 0;
    if (strides[inputSlot] > UINT32_MAX)
        return UINT32_MAX;
    return static_cast<uint32_t>(strides[inputSlot]);
}

HRESULT D3D12DeviceImpl::CreateCommandList(UINT, UINT, ID3D12CommandAllocator* pAllocator, ID3D12PipelineState*,
                                           REFIID riid, void** ppCommandList) {
    if (!ppCommandList)
        return E_POINTER;

    struct CmdListImpl final : public ID3D12GraphicsCommandList {
        std::atomic<ULONG> refCount{1};
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

        D3D12RootSignatureImpl* m_computeRootSignature = nullptr;
        std::vector<uint8_t> m_computeArgumentBuffer;
        D3D12_DISPATCH_RAYS_DESC m_dispatchRays = {};
        bool m_hasDispatchRays = false;
        std::vector<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC> m_buildASDescs;
        bool m_hasDispatchMesh = false;
        UINT m_meshGroupCount[3] = {};

        struct ClearCmd {
            void* texture;
            bool isDepth;
            FLOAT color[4];
            FLOAT depth;
            UINT8 stencil;
            UINT clearFlags;
        };
        std::vector<ClearCmd> m_clearCmds;

        struct ComputeDispatch {
            UINT x, y, z;
        };
        std::vector<ComputeDispatch> m_computeDispatches;

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

        void retainResource(ID3D12Resource* res) {
            if (!res)
                return;
            for (auto* existing : m_referencedResources) {
                if (existing == res)
                    return;
            }
            res->AddRef();
            m_referencedResources.push_back(res);
        }

        ~CmdListImpl() {
            for (auto* res : m_referencedResources) {
                if (res)
                    res->Release();
            }
        }

        HRESULT QueryInterface(REFIID riid, void** ppv) override {
            if (!ppv)
                return E_POINTER;
            if (riid == IID_ID3D12GraphicsCommandList || riid == IID_IUnknown) {
                AddRef();
                *ppv = this;
                return S_OK;
            }
            return E_NOINTERFACE;
        }
        ULONG AddRef() override { return ++refCount; }
        ULONG Release() override {
            ULONG c = --refCount;
            if (c == 0)
                delete this;
            return c;
        }
        METALSHARP_D3D12_PRIVATE_DATA_METHODS()

        HRESULT Close() override {
            m_closed = true;
            m_recording = false;
            return S_OK;
        }
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
            for (auto* res : m_referencedResources) {
                if (res)
                    res->Release();
            }
            m_referencedResources.clear();
            return S_OK;
        }

        HRESULT IASetPrimitiveTopology(UINT topo) override {
            m_primitiveTopology = topo;
            return S_OK;
        }

        HRESULT IASetVertexBuffers(UINT start, UINT num, const D3D12_VERTEX_BUFFER_VIEW* pViews) override {
            if (!pViews)
                return S_OK;
            for (UINT i = 0; i < num && (start + i) < 32; ++i) {
                UINT64 offset = 0;
                D3D12ResourceImpl* res = device.findResourceForGPUAddress(pViews[i].BufferLocation, &offset);
                m_vertexBuffers[start + i].metalBuffer = res ? res->__metalBufferPtr() : nullptr;
                m_vertexBuffers[start + i].offset = res ? offset : 0;
                m_vertexBuffers[start + i].size = pViews[i].SizeInBytes;
                m_vertexBuffers[start + i].stride = pViews[i].StrideInBytes;
                retainResource(res);
            }
            return S_OK;
        }

        HRESULT IASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW* pView) override {
            if (!pView)
                return S_OK;
            UINT64 offset = 0;
            D3D12ResourceImpl* res = device.findResourceForGPUAddress(pView->BufferLocation, &offset);
            m_indexBuffer.metalBuffer = res ? res->__metalBufferPtr() : nullptr;
            m_indexBuffer.offset = res ? offset : 0;
            m_indexBuffer.size = pView->SizeInBytes;
            m_indexBuffer.format = pView->Format;
            retainResource(res);
            return S_OK;
        }

        HRESULT SetPipelineState(ID3D12PipelineState* pPSO) override {
            m_pso = pPSO;
            return S_OK;
        }

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

        HRESULT SetGraphicsRootDescriptorTable(UINT rootParamIndex,
                                               D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor) override {
            if (!m_rootSignature || rootParamIndex >= m_rootSignature->parameterLayouts.size())
                return S_OK;
            auto& layout = m_rootSignature->parameterLayouts[rootParamIndex];

            if (layout.offset + sizeof(uint64_t) <= m_argumentBuffer.size()) {
                uint64_t ptr = baseDescriptor.ptr;
                memcpy(m_argumentBuffer.data() + layout.offset, &ptr, sizeof(uint64_t));
            }
            return S_OK;
        }

        HRESULT SetGraphicsRootConstantBufferView(UINT rootParamIndex,
                                                  D3D12_GPU_VIRTUAL_ADDRESS bufferLocation) override {
            if (!m_rootSignature || rootParamIndex >= m_rootSignature->parameterLayouts.size())
                return S_OK;
            auto& layout = m_rootSignature->parameterLayouts[rootParamIndex];

            if (layout.offset + sizeof(uint64_t) <= m_argumentBuffer.size()) {
                memcpy(m_argumentBuffer.data() + layout.offset, &bufferLocation, sizeof(uint64_t));
            }
            return S_OK;
        }

        HRESULT SetGraphicsRootShaderResourceView(UINT rootParamIndex,
                                                  D3D12_GPU_VIRTUAL_ADDRESS bufferLocation) override {
            if (!m_rootSignature || rootParamIndex >= m_rootSignature->parameterLayouts.size())
                return S_OK;
            auto& layout = m_rootSignature->parameterLayouts[rootParamIndex];

            if (layout.offset + sizeof(uint64_t) <= m_argumentBuffer.size()) {
                memcpy(m_argumentBuffer.data() + layout.offset, &bufferLocation, sizeof(uint64_t));
            }
            return S_OK;
        }

        HRESULT SetGraphicsRootUnorderedAccessView(UINT rootParamIndex,
                                                   D3D12_GPU_VIRTUAL_ADDRESS bufferLocation) override {
            if (!m_rootSignature || rootParamIndex >= m_rootSignature->parameterLayouts.size())
                return S_OK;
            auto& layout = m_rootSignature->parameterLayouts[rootParamIndex];

            if (layout.offset + sizeof(uint64_t) <= m_argumentBuffer.size()) {
                memcpy(m_argumentBuffer.data() + layout.offset, &bufferLocation, sizeof(uint64_t));
            }
            return S_OK;
        }

        HRESULT SetGraphicsRoot32BitConstants(UINT rootParamIndex, UINT num32BitValues, const void* pData,
                                              UINT destOffset) override {
            if (!pData || num32BitValues == 0)
                return S_OK;
            if (!m_rootSignature || rootParamIndex >= m_rootSignature->parameterLayouts.size())
                return S_OK;
            auto& layout = m_rootSignature->parameterLayouts[rootParamIndex];

            size_t writeOffset = layout.offset + destOffset * sizeof(uint32_t);
            size_t writeSize = num32BitValues * sizeof(uint32_t);
            if (writeOffset + writeSize <= m_argumentBuffer.size()) {
                memcpy(m_argumentBuffer.data() + writeOffset, pData, writeSize);
            }
            return S_OK;
        }

        HRESULT OMSetRenderTargets(UINT numRTVs, const D3D12_CPU_DESCRIPTOR_HANDLE* pRTVs, BOOL,
                                   const D3D12_CPU_DESCRIPTOR_HANDLE* pDSV) override {
            m_numRenderTargets = numRTVs;
            if (pRTVs) {
                for (UINT i = 0; i < numRTVs && i < 8; ++i) {
                    D3D12DescriptorHeapImpl* heap = device.findHeapForHandle(pRTVs[i]);
                    if (heap) {
                        auto* desc = heap->getDescriptor(pRTVs[i]);
                        if (desc)
                            m_renderTargets[i] = desc->metalTexture;
                    } else {
                        m_renderTargets[i] = reinterpret_cast<void*>(pRTVs[i].ptr);
                    }
                }
            }
            if (pDSV) {
                D3D12DescriptorHeapImpl* heap = device.findHeapForHandle(*pDSV);
                if (heap) {
                    auto* desc = heap->getDescriptor(*pDSV);
                    m_depthTarget = desc ? desc->metalTexture : nullptr;
                } else {
                    m_depthTarget = reinterpret_cast<void*>(pDSV->ptr);
                }
            }
            return S_OK;
        }
        HRESULT OMSetStencilRef(UINT ref) override { return S_OK; }
        HRESULT OMSetBlendFactor(const FLOAT factor[4]) override { return S_OK; }

        HRESULT RSSetViewports(UINT num, const D3D12_VIEWPORT* pVPs) override {
            if (num > 0 && pVPs)
                m_viewport = pVPs[0];
            return S_OK;
        }
        HRESULT RSSetScissorRects(UINT num, const D3D12_RECT* pRects) override {
            if (num > 0 && pRects)
                m_scissorRect = pRects[0];
            return S_OK;
        }

        HRESULT ClearRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE handle, const FLOAT color[4], UINT,
                                      const D3D12_RECT*) override {
            ClearCmd cmd = {};
            D3D12DescriptorHeapImpl* heap = device.findHeapForHandle(handle);
            if (heap) {
                auto* desc = heap->getDescriptor(handle);
                if (desc)
                    cmd.texture = desc->metalTexture;
            }
            if (!cmd.texture)
                cmd.texture = reinterpret_cast<void*>(handle.ptr);
            cmd.isDepth = false;
            if (color)
                memcpy(cmd.color, color, sizeof(cmd.color));
            m_clearCmds.push_back(cmd);
            return S_OK;
        }

        HRESULT ClearDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE handle, UINT flags, FLOAT depth, UINT8 stencil, UINT,
                                      const D3D12_RECT*) override {
            ClearCmd cmd = {};
            D3D12DescriptorHeapImpl* heap = device.findHeapForHandle(handle);
            if (heap) {
                auto* desc = heap->getDescriptor(handle);
                if (desc)
                    cmd.texture = desc->metalTexture;
            }
            if (!cmd.texture)
                cmd.texture = reinterpret_cast<void*>(handle.ptr);
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

        HRESULT DrawIndexedInstanced(UINT idxCount, UINT instCount, UINT startIdx, INT baseVert,
                                     UINT startInst) override {
            m_drawCmds.push_back({true, idxCount, instCount, startIdx, baseVert, startInst});
            return S_OK;
        }

        HRESULT Dispatch(UINT x, UINT y, UINT z) override {
            m_computeDispatches.push_back({x, y, z});
            return S_OK;
        }

        HRESULT CopyResource(ID3D12Resource* dst, ID3D12Resource* src) override {
            if (!dst || !src)
                return E_INVALIDARG;
            dst->AddRef();
            src->AddRef();
            m_copyCmds.push_back({dst, src, false, 0, 0, 0});
            return S_OK;
        }

        HRESULT CopyBufferRegion(ID3D12Resource* dst, UINT64 dstOff, ID3D12Resource* src, UINT64 srcOff,
                                 UINT64 numBytes) override {
            if (!dst || !src)
                return E_INVALIDARG;
            dst->AddRef();
            src->AddRef();
            m_copyCmds.push_back({dst, src, true, dstOff, srcOff, numBytes});
            return S_OK;
        }

        HRESULT CopyTextureRegion(const void* pDst, UINT dstX, UINT dstY, UINT dstZ, ID3D12Resource* pSrc, UINT srcSub,
                                  const void* pSrcBox) override {
            if (!pSrc)
                return E_INVALIDARG;
            pSrc->AddRef();
            m_copyCmds.push_back({nullptr, pSrc, false, (UINT64)dstX | ((UINT64)dstY << 16), srcSub, (UINT64)dstZ});
            return S_OK;
        }

        HRESULT ResourceBarrier(UINT numBarriers, const D3D12_RESOURCE_BARRIER* pBarriers) override {
            auto tracked = device.resourceStateTracker().applyBarriers(numBarriers, pBarriers);
            for (UINT i = 0; i < numBarriers; ++i) {
                m_barriers.push_back(pBarriers[i]);
                if (i < tracked.size() && tracked[i].stateMismatch) {
                    MS_WARN("D3D12 ResourceBarrier state mismatch before=%s after=%s",
                            D3D12ResourceStateTracker::describeState(tracked[i].stateBefore).c_str(),
                            D3D12ResourceStateTracker::describeState(tracked[i].stateAfter).c_str());
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
            if (!pRes || !ppData)
                return E_POINTER;
            return pRes->Map(sub, nullptr, ppData);
        }
        HRESULT Unmap(ID3D12Resource* pRes, UINT sub, const D3D12_RANGE*) override {
            if (!pRes)
                return E_POINTER;
            return pRes->Unmap(sub, nullptr);
        }
        HRESULT ExecuteIndirect(ID3D12CommandSignature*, UINT, ID3D12Resource*, UINT64, ID3D12Resource*,
                                UINT64) override {
            StubTelemetry::record("d3d12.dll", "ExecuteIndirect", StubBehavior::UnsupportedHardFailure, "E_NOTIMPL",
                                  "full command signature lowering is not implemented; repeated direct draws use ICBs");
            return E_NOTIMPL;
        }

        HRESULT DispatchRays(const D3D12_DISPATCH_RAYS_DESC* pDesc) override {
            if (!pDesc)
                return E_INVALIDARG;
            m_dispatchRays = *pDesc;
            m_hasDispatchRays = true;
            return S_OK;
        }

        HRESULT BuildRaytracingAccelerationStructure(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC* pDesc,
                                                     UINT, const void*) override {
            if (!pDesc)
                return E_INVALIDARG;
            m_buildASDescs.push_back(*pDesc);
            return S_OK;
        }

        HRESULT CopyRaytracingAccelerationStructure(UINT64, UINT64, UINT) override {
            StubTelemetry::record("d3d12.dll", "CopyRaytracingAccelerationStructure", StubBehavior::CompatibilityShim,
                                  "S_OK", "raytracing acceleration structure copies are recorded but not materialized");
            return S_OK;
        }

        HRESULT EmitRaytracingAccelerationStructurePostbuildInfo(const void*, UINT, const UINT64*) override {
            StubTelemetry::record("d3d12.dll", "EmitRaytracingAccelerationStructurePostbuildInfo",
                                  StubBehavior::CompatibilityShim, "S_OK",
                                  "postbuild metadata is accepted but not emitted yet");
            return S_OK;
        }

        HRESULT DispatchMesh(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ) override {
            m_hasDispatchMesh = true;
            m_meshGroupCount[0] = ThreadGroupCountX;
            m_meshGroupCount[1] = ThreadGroupCountY;
            m_meshGroupCount[2] = ThreadGroupCountZ;
            if (!device.metalCapabilities().supportsMeshShaders) {
                StubTelemetry::record("d3d12.dll", "DispatchMesh", StubBehavior::CompatibilityShim, "S_OK",
                                      "Metal mesh shader support is unavailable on this device");
            }
            return S_OK;
        }

        HRESULT SetComputeRootSignature(ID3D12RootSignature* pRootSignature) override {
            m_computeRootSignature = static_cast<D3D12RootSignatureImpl*>(pRootSignature);
            if (m_computeRootSignature) {
                m_computeArgumentBuffer.resize(m_computeRootSignature->argumentBufferSize, 0);
            }
            return S_OK;
        }

        HRESULT SetComputeRoot32BitConstants(UINT RootParameterIndex, UINT Num32BitValues, const void* pData,
                                             UINT DestOffset) override {
            if (!m_computeRootSignature || !pData)
                return E_INVALIDARG;
            if (RootParameterIndex >= m_computeRootSignature->parameterLayouts.size())
                return E_INVALIDARG;
            auto& layout = m_computeRootSignature->parameterLayouts[RootParameterIndex];
            size_t offset = layout.offset + DestOffset * sizeof(uint32_t);
            if (offset + Num32BitValues * sizeof(uint32_t) <= m_computeArgumentBuffer.size()) {
                memcpy(m_computeArgumentBuffer.data() + offset, pData, Num32BitValues * sizeof(uint32_t));
            }
            return S_OK;
        }

        HRESULT SetComputeRootDescriptorTable(UINT RootParameterIndex,
                                              D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) override {
            if (!m_computeRootSignature)
                return E_INVALIDARG;
            if (RootParameterIndex >= m_computeRootSignature->parameterLayouts.size())
                return E_INVALIDARG;
            auto& layout = m_computeRootSignature->parameterLayouts[RootParameterIndex];
            if (layout.offset + sizeof(uint64_t) <= m_computeArgumentBuffer.size()) {
                memcpy(m_computeArgumentBuffer.data() + layout.offset, &BaseDescriptor.ptr, sizeof(uint64_t));
            }
            return S_OK;
        }

        HRESULT SetComputeRootConstantBufferView(UINT RootParameterIndex,
                                                 D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override {
            if (!m_computeRootSignature)
                return E_INVALIDARG;
            if (RootParameterIndex >= m_computeRootSignature->parameterLayouts.size())
                return E_INVALIDARG;
            auto& layout = m_computeRootSignature->parameterLayouts[RootParameterIndex];
            if (layout.offset + sizeof(uint64_t) <= m_computeArgumentBuffer.size()) {
                memcpy(m_computeArgumentBuffer.data() + layout.offset, &BufferLocation, sizeof(uint64_t));
            }
            return S_OK;
        }

        HRESULT SetComputeRootShaderResourceView(UINT RootParameterIndex,
                                                 D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override {
            return SetComputeRootConstantBufferView(RootParameterIndex, BufferLocation);
        }

        HRESULT SetComputeRootUnorderedAccessView(UINT RootParameterIndex,
                                                  D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override {
            return SetComputeRootConstantBufferView(RootParameterIndex, BufferLocation);
        }

        HRESULT CopyTiles(ID3D12Resource* pTiledResource, const D3D12_TILED_RESOURCE_COORDINATE* pCoord,
                          const D3D12_TILE_REGION_SIZE* pSize, ID3D12Resource* pBuffer, UINT64 BufferStartOffset,
                          UINT Flags) override {
            if (!pTiledResource || !pCoord || !pSize || !pBuffer)
                return E_INVALIDARG;

            void* srcTex = pTiledResource->__metalTexturePtr();
            void* dstBuf = pBuffer->__metalBufferPtr();
            if (!srcTex || !dstBuf)
                return E_FAIL;

            const UINT TILE_BYTES = 65536;
            UINT numTiles = pSize->NumTiles;

            if (pSize->bUseBox) {
                numTiles = pSize->Width * pSize->Height * pSize->Depth;
                if (numTiles == 0)
                    numTiles = pSize->NumTiles;
            }

            id<MTLTexture> tex = (__bridge id<MTLTexture>)srcTex;
            id<MTLBuffer> buf = (__bridge id<MTLBuffer>)dstBuf;

            for (UINT t = 0; t < numTiles; t++) {
                UINT tileX = pCoord->X + (t % (pSize->Width > 0 ? pSize->Width : 1));
                UINT tileY =
                    pCoord->Y + ((t / (pSize->Width > 0 ? pSize->Width : 1)) % (pSize->Height > 0 ? pSize->Height : 1));
                UINT tileZ =
                    pCoord->Z + (t / ((pSize->Width > 0 ? pSize->Width : 1) * (pSize->Height > 0 ? pSize->Height : 1)));

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

        void __execute(void* dev) override { execute(*static_cast<MetalDevice*>(dev)); }

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
                        memcpy((char*)[dst contents] + cmd.dstOffset, (char*)[src contents] + cmd.srcOffset,
                               (size_t)cmd.numBytes);
                    }
                }
            }

            for (auto& clear : m_clearCmds) {
                if (!clear.texture)
                    continue;
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
                    passDesc.colorAttachments[0].clearColor =
                        MTLClearColorMake(clear.color[0], clear.color[1], clear.color[2], clear.color[3]);
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
                        id<MTLDevice> mtlDev = (__bridge id<MTLDevice>)metalDev.nativeDevice();
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

                    for (UINT i = 0; i < 32; ++i) {
                        if (!m_vertexBuffers[i].metalBuffer)
                            continue;
                        [enc setVertexBuffer:(__bridge id<MTLBuffer>)m_vertexBuffers[i].metalBuffer
                                      offset:m_vertexBuffers[i].offset
                                     atIndex:i];
                    }

                    for (auto* res : m_referencedResources) {
                        if (!res)
                            continue;
                        void* buf = res->__metalBufferPtr();
                        void* tex = res->__metalTexturePtr();
                        if (buf)
                            [enc useResource:(__bridge id<MTLBuffer>)buf
                                       usage:MTLResourceUsageRead
                                      stages:MTLRenderStageVertex | MTLRenderStageFragment];
                        if (tex)
                            [enc useResource:(__bridge id<MTLTexture>)tex
                                       usage:MTLResourceUsageRead
                                      stages:MTLRenderStageVertex | MTLRenderStageFragment];
                    }

                    MTLPrimitiveType primType = MTLPrimitiveTypeTriangle;
                    switch (m_primitiveTopology) {
                    case D3D12_PRIMITIVE_TOPOLOGY_POINTLIST:
                        primType = MTLPrimitiveTypePoint;
                        break;
                    case D3D12_PRIMITIVE_TOPOLOGY_LINELIST:
                        primType = MTLPrimitiveTypeLine;
                        break;
                    case D3D12_PRIMITIVE_TOPOLOGY_LINESTRIP:
                        primType = MTLPrimitiveTypeLineStrip;
                        break;
                    case D3D12_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
                        primType = MTLPrimitiveTypeTriangleStrip;
                        break;
                    default:
                        break;
                    }

                    bool usedIndirectCommandBuffer = false;
                    if (device.metalCapabilities().supportsIndirectCommandBuffers && m_drawCmds.size() > 1) {
                        MTLIndirectCommandBufferDescriptor* icbDesc = [[MTLIndirectCommandBufferDescriptor alloc] init];
                        icbDesc.commandTypes = MTLIndirectCommandTypeDraw | MTLIndirectCommandTypeDrawIndexed;
                        icbDesc.inheritPipelineState = YES;
                        icbDesc.inheritBuffers = YES;

                        id<MTLDevice> mtlDev = (__bridge id<MTLDevice>)metalDev.nativeDevice();
                        id<MTLIndirectCommandBuffer> icb =
                            [mtlDev newIndirectCommandBufferWithDescriptor:icbDesc
                                                           maxCommandCount:m_drawCmds.size()
                                                                   options:0];
                        if (icb) {
                            for (NSUInteger commandIndex = 0; commandIndex < m_drawCmds.size(); ++commandIndex) {
                                const DrawCmd& draw = m_drawCmds[commandIndex];
                                id<MTLIndirectRenderCommand> cmd = [icb indirectRenderCommandAtIndex:commandIndex];
                                if (draw.indexed) {
                                    if (!m_indexBuffer.metalBuffer) {
                                        [cmd reset];
                                        continue;
                                    }
                                    MTLIndexType indexType = m_indexBuffer.format == DXGI_FORMAT_R16_UINT
                                                                 ? MTLIndexTypeUInt16
                                                                 : MTLIndexTypeUInt32;
                                    NSUInteger indexSize = indexType == MTLIndexTypeUInt16 ? 2 : 4;
                                    NSUInteger indexOffset =
                                        (NSUInteger)m_indexBuffer.offset + draw.startIndex * indexSize;
                                    [cmd drawIndexedPrimitives:primType
                                                    indexCount:draw.vertexCount
                                                     indexType:indexType
                                                   indexBuffer:(__bridge id<MTLBuffer>)m_indexBuffer.metalBuffer
                                             indexBufferOffset:indexOffset
                                                 instanceCount:draw.instanceCount > 1 ? draw.instanceCount : 1
                                                    baseVertex:draw.baseVertex
                                                  baseInstance:draw.startInstance];
                                } else {
                                    [cmd drawPrimitives:primType
                                            vertexStart:draw.startIndex
                                            vertexCount:draw.vertexCount
                                          instanceCount:draw.instanceCount > 1 ? draw.instanceCount : 1
                                           baseInstance:draw.startInstance];
                                }
                            }
                            [enc executeCommandsInBuffer:icb withRange:NSMakeRange(0, m_drawCmds.size())];
                            usedIndirectCommandBuffer = true;
                        }
                    }

                    if (!usedIndirectCommandBuffer) {
                        for (auto& draw : m_drawCmds) {
                            if (draw.indexed) {
                                if (!m_indexBuffer.metalBuffer)
                                    continue;
                                MTLIndexType indexType = m_indexBuffer.format == DXGI_FORMAT_R16_UINT
                                                             ? MTLIndexTypeUInt16
                                                             : MTLIndexTypeUInt32;
                                NSUInteger indexSize = indexType == MTLIndexTypeUInt16 ? 2 : 4;
                                NSUInteger indexOffset = (NSUInteger)m_indexBuffer.offset + draw.startIndex * indexSize;
                                [enc drawIndexedPrimitives:primType
                                                indexCount:draw.vertexCount
                                                 indexType:indexType
                                               indexBuffer:(__bridge id<MTLBuffer>)m_indexBuffer.metalBuffer
                                         indexBufferOffset:indexOffset
                                             instanceCount:draw.instanceCount > 1 ? draw.instanceCount : 1
                                                baseVertex:draw.baseVertex
                                              baseInstance:draw.startInstance];
                            } else {
                                [enc drawPrimitives:primType
                                        vertexStart:draw.startIndex
                                        vertexCount:draw.vertexCount
                                      instanceCount:draw.instanceCount > 1 ? draw.instanceCount : 1
                                       baseInstance:draw.startInstance];
                            }
                        }
                    }
                    [enc endEncoding];
                }
            }

            if (m_hasDispatchRays) {
                void* rtPipeline = nullptr;
                if (m_pso)
                    rtPipeline = m_pso->__metalRenderPipelineState();

                id<MTLComputePipelineState> computePipeline = nil;
                if (rtPipeline) {
                    computePipeline = (__bridge id<MTLComputePipelineState>)rtPipeline;
                }

                if (computePipeline) {
                    id<MTLComputeCommandEncoder> enc = [cmdBuffer computeCommandEncoder];
                    [enc setComputePipelineState:computePipeline];

                    if (!m_computeArgumentBuffer.empty() && m_computeRootSignature) {
                        id<MTLDevice> mtlDev = (__bridge id<MTLDevice>)metalDev.nativeDevice();
                        if (mtlDev) {
                            id<MTLBuffer> argBuf = [mtlDev newBufferWithBytes:m_computeArgumentBuffer.data()
                                                                       length:m_computeRootSignature->argumentBufferSize
                                                                      options:MTLResourceStorageModeShared];
                            if (argBuf) {
                                [enc setBuffer:argBuf offset:0 atIndex:16];
                            }
                        }
                    }

                    MTLSize threadgroups = MTLSizeMake((m_dispatchRays.Width + 7) / 8, (m_dispatchRays.Height + 7) / 8,
                                                       m_dispatchRays.Depth > 0 ? m_dispatchRays.Depth : 1);
                    MTLSize threadsPerGroup = MTLSizeMake(8, 8, 1);
                    [enc dispatchThreadgroups:threadgroups threadsPerThreadgroup:threadsPerGroup];
                    [enc endEncoding];
                }
            }

            if (!m_computeDispatches.empty()) {
                id<MTLComputePipelineState> computePipeline = nil;
                void* computePtr = nullptr;
                void* fallbackRenderPtr = nullptr;
                if (m_pso) {
                    computePtr = m_pso->__metalComputePipelineState();
                    if (computePtr)
                        computePipeline = (__bridge id<MTLComputePipelineState>)computePtr;
                    if (!computePtr) {
                        fallbackRenderPtr = m_pso->__metalRenderPipelineState();
                    }
                }

                if (computePipeline) {
                    const ComputeDispatch& firstDispatch = m_computeDispatches.front();
                    MS_INFO("d3d12_compute_dispatch_execute count=%zu first=%u,%u,%u argument_buffer_bytes=%zu",
                            m_computeDispatches.size(), firstDispatch.x, firstDispatch.y, firstDispatch.z,
                            m_computeRootSignature ? (size_t)m_computeRootSignature->argumentBufferSize : 0);

                    id<MTLComputeCommandEncoder> enc = [cmdBuffer computeCommandEncoder];
                    [enc setComputePipelineState:computePipeline];

                    if (!m_computeArgumentBuffer.empty() && m_computeRootSignature) {
                        id<MTLDevice> mtlDev = (__bridge id<MTLDevice>)metalDev.nativeDevice();
                        if (mtlDev) {
                            id<MTLBuffer> argBuf = [mtlDev newBufferWithBytes:m_computeArgumentBuffer.data()
                                                                       length:m_computeRootSignature->argumentBufferSize
                                                                      options:MTLResourceStorageModeShared];
                            if (argBuf)
                                [enc setBuffer:argBuf offset:0 atIndex:16];
                        }
                    }

                    for (auto& dispatch : m_computeDispatches) {
                        MTLSize threadgroups = MTLSizeMake(dispatch.x, dispatch.y, dispatch.z);
                        MTLSize threadsPerGroup = MTLSizeMake(1, 1, 1);
                        [enc dispatchThreadgroups:threadgroups threadsPerThreadgroup:threadsPerGroup];
                    }
                    [enc endEncoding];
                } else {
                    const ComputeDispatch& firstDispatch = m_computeDispatches.front();
                    MS_WARN("d3d12_compute_dispatch_no_pipeline count=%zu first=%u,%u,%u has_pso=%s compute_ptr=%s "
                            "render_fallback_ptr=%s",
                            m_computeDispatches.size(), firstDispatch.x, firstDispatch.y, firstDispatch.z,
                            m_pso ? "true" : "false", computePtr ? "true" : "false",
                            fallbackRenderPtr ? "true" : "false");
                }
            }

            if (m_hasDispatchMesh) {
                void* pipelinePtr = m_pso ? m_pso->__metalRenderPipelineState() : nullptr;
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

                    MTLSize threadgroups = MTLSizeMake(m_meshGroupCount[0], m_meshGroupCount[1], m_meshGroupCount[2]);
                    MTLSize threadsPerGroup = MTLSizeMake(1, 1, 1);

                    if (@available(macOS 13.0, *)) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundeclared-selector"
                        if ([enc respondsToSelector:@selector(drawMeshThreadgroups:threadsPerMeshThreadgroup:)]) {
                            [enc performSelector:@selector(drawMeshThreadgroups:threadsPerMeshThreadgroup:)
                                      withObject:nil];
                        }
#pragma clang diagnostic pop
                    }

                    [enc endEncoding];
                }
            }

            for (auto& asDesc : m_buildASDescs) {
            }

            [cmdBuffer commit];
        }
    };

    *ppCommandList = new CmdListImpl(*this);
    return S_OK;
}

HRESULT D3D12DeviceImpl::CreateGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc, REFIID riid,
                                                     void** ppPSO) {
    if (!pDesc || !ppPSO)
        return E_INVALIDARG;

    auto* pso = new D3D12PipelineStateImpl();

    if (pDesc->VS && pDesc->VSsize > 0) {
        PipelineStateDesc pipeDesc;
        pipeDesc.vertexStride = d3d12VertexStrideForSlot(pDesc->InputLayout, pDesc->NumInputElements, 0);

        CompiledShader compiled;
        const uint8_t* shaderData = static_cast<const uint8_t*>(pDesc->VS);
        size_t shaderSize = pDesc->VSsize;

        D3D12RootSignatureImpl* rootSig = static_cast<D3D12RootSignatureImpl*>(pDesc->pRootSignature);
        bool ok = false;
        if (looksLikeMSL(shaderData, shaderSize)) {
            std::string source(reinterpret_cast<const char*>(shaderData), shaderSize);
            ok = m_shaderTranslator->compileMSL(source.c_str(), "vertexShader", "fragmentShader", compiled);
        } else if (rootSig && !rootSig->rawBytecode.empty()) {
            ok = m_shaderTranslator->translateDXBCWithRootSignature(shaderData, shaderSize, ShaderStage::Vertex,
                                                                    rootSig->rawBytecode.data(),
                                                                    rootSig->rawBytecode.size(), compiled);
        }

        if (!ok) {
            ok = m_shaderTranslator->translateDXBC(shaderData, shaderSize, ShaderStage::Vertex, compiled);
        }

        if (ok) {
            pipeDesc.vertexFunction = compiled.vertexFunction;
        }

        if (!compiled.fragmentFunction && pDesc->PS && pDesc->PSsize > 0) {
            CompiledShader psCompiled;
            const uint8_t* psData = static_cast<const uint8_t*>(pDesc->PS);
            bool psOk = false;
            if (looksLikeMSL(psData, pDesc->PSsize)) {
                std::string source(reinterpret_cast<const char*>(psData), pDesc->PSsize);
                psOk = m_shaderTranslator->compileMSL(source.c_str(), "vertexShader", "fragmentShader", psCompiled);
            } else {
                psOk = m_shaderTranslator->translateDXBC(psData, pDesc->PSsize, ShaderStage::Pixel, psCompiled);
            }
            if (psOk) {
                pipeDesc.fragmentFunction = psCompiled.fragmentFunction;
            }
        }

        pipeDesc.numColorAttachments = pDesc->NumRenderTargets;
        for (UINT i = 0; i < pDesc->NumRenderTargets && i < 8; ++i) {
            pipeDesc.colorPixelFormats[i] = dxgiFormatToMetal((DXGITranslation)pDesc->RTVFormats[i]);
        }
        if (pDesc->DSVFormat != 0 && dxgiFormatIsDepth((DXGITranslation)pDesc->DSVFormat)) {
            pipeDesc.depthPixelFormat = dxgiDepthFormatToMetal((DXGITranslation)pDesc->DSVFormat);
        }

        PipelineState* pipeline = PipelineState::create(*m_metalDevice, pipeDesc);
        if (pipeline) {
            pso->m_renderPipeline = pipeline->nativeRenderPipelineState();
        }
    }

    *ppPSO = pso;
    return S_OK;
}

HRESULT D3D12DeviceImpl::CreateComputePipelineState(const void* pDesc, REFIID riid, void** ppPSO) {
    if (!ppPSO)
        return E_POINTER;
    *ppPSO = nullptr;

    auto* pso = new D3D12PipelineStateImpl();

    if (!pDesc) {
        *ppPSO = pso;
        return S_OK;
    }

    auto* desc = static_cast<const D3D12_COMPUTE_PIPELINE_STATE_DESC*>(pDesc);
    const uint8_t* shaderData = static_cast<const uint8_t*>(desc->CS.pShaderBytecode);
    size_t shaderSize = desc->CS.BytecodeLength;
    if (!shaderData || shaderSize == 0) {
        MS_WARN("d3d12_compute_pso_empty_shader bytecode_size=%zu", shaderSize);
        *ppPSO = pso;
        return S_OK;
    }

    CompiledShader compiled;
    D3D12RootSignatureImpl* rootSig = static_cast<D3D12RootSignatureImpl*>(desc->pRootSignature);
    const char* sourceKind = d3d12ShaderSourceKind(shaderData, shaderSize);
    bool ok = false;
    if (rootSig && !rootSig->rawBytecode.empty()) {
        ok = m_shaderTranslator->translateDXBCWithRootSignature(shaderData, shaderSize, ShaderStage::Compute,
                                                                rootSig->rawBytecode.data(),
                                                                rootSig->rawBytecode.size(), compiled);
    }
    if (!ok) {
        ok = m_shaderTranslator->translateDXBC(shaderData, shaderSize, ShaderStage::Compute, compiled);
    }
    if (!ok && !looksLikeMSL(shaderData, shaderSize)) {
        MS_WARN("d3d12_compute_pso_translate_failed source=%s bytecode_size=%zu root_signature_bytes=%zu", sourceKind,
                shaderSize, rootSig ? rootSig->rawBytecode.size() : 0);
    }

    id<MTLFunction> function = nil;
    if (ok && compiled.computeFunction) {
        function = (__bridge id<MTLFunction>)compiled.computeFunction;
        MS_INFO("d3d12_compute_pso_shader source=%s bytecode_size=%zu entry=%s tg=%u,%u,%u resources=%zu", sourceKind,
                shaderSize, compiled.entryPointName.empty() ? "unknown" : compiled.entryPointName.c_str(),
                compiled.reflection.threadgroupSize[0], compiled.reflection.threadgroupSize[1],
                compiled.reflection.threadgroupSize[2], compiled.reflection.resources.size());
    } else if (looksLikeMSL(shaderData, shaderSize)) {
        id<MTLDevice> mtlDevice = (__bridge id<MTLDevice>)m_metalDevice->nativeDevice();
        NSString* source = [[NSString alloc] initWithBytes:shaderData length:shaderSize encoding:NSUTF8StringEncoding];
        NSError* libraryError = nil;
        id<MTLLibrary> library = [mtlDevice newLibraryWithSource:source options:nil error:&libraryError];
        if (library) {
            function = [library newFunctionWithName:@"computeShader"];
            if (!function)
                function = [library newFunctionWithName:@"main0"];
            if (!function && library.functionNames.count > 0)
                function = [library newFunctionWithName:library.functionNames.firstObject];
            MS_INFO("d3d12_compute_pso_shader source=msl bytecode_size=%zu functions=%lu selected=%s", shaderSize,
                    (unsigned long)library.functionNames.count, function ? [[function name] UTF8String] : "none");
        } else if (libraryError) {
            const char* errorMessage = [[libraryError localizedDescription] UTF8String];
            MS_WARN("d3d12_compute_pso_msl_compile_failed bytecode_size=%zu error=%s", shaderSize,
                    errorMessage ? errorMessage : "unknown");
        }
    }

    if (!function) {
        MS_WARN("d3d12_compute_pso_no_function source=%s bytecode_size=%zu", sourceKind, shaderSize);
        pso->Release();
        return E_FAIL;
    }

    id<MTLDevice> mtlDevice = (__bridge id<MTLDevice>)m_metalDevice->nativeDevice();
    NSError* error = nil;
    id<MTLComputePipelineState> pipeline = [mtlDevice newComputePipelineStateWithFunction:function error:&error];
    if (!pipeline) {
        const char* errorMessage = error ? [[error localizedDescription] UTF8String] : "unknown";
        MS_WARN("d3d12_compute_pso_pipeline_failed source=%s bytecode_size=%zu function=%s error=%s", sourceKind,
                shaderSize, [[function name] UTF8String] ? [[function name] UTF8String] : "unknown",
                errorMessage ? errorMessage : "unknown");
        pso->Release();
        return E_FAIL;
    }

    MS_INFO("d3d12_compute_pso_ready source=%s bytecode_size=%zu function=%s", sourceKind, shaderSize,
            [[function name] UTF8String] ? [[function name] UTF8String] : "unknown");
    pso->m_computePipeline = (__bridge_retained void*)pipeline;
    pso->m_ownedComputePipeline = pso->m_computePipeline;
    *ppPSO = pso;
    return S_OK;
}

D3D12CommandQueueImpl::~D3D12CommandQueueImpl() {
    auto state = m_queueState;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->stopping = true;
    }
    state->cond.notify_all();
    if (state->worker.joinable()) {
        state->worker.join();
    }
}

void D3D12CommandQueueImpl::runQueueWorker(std::shared_ptr<QueueState> state) {
    for (;;) {
        QueueWorkItem item;
        {
            std::unique_lock<std::mutex> lock(state->mutex);
            state->cond.wait(lock, [&] { return state->stopping || !state->work.empty(); });
            if (state->stopping && state->work.empty())
                return;
            item = std::move(state->work.front());
            state->work.pop_front();
            state->busy = true;
        }

        if (item.operation)
            item.operation();

        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->busy = false;
        }
        state->cond.notify_all();
    }
}

HRESULT D3D12CommandQueueImpl::enqueueQueueWork(std::function<void()> operation, ID3D12Fence* signalFence,
                                                UINT64 signalValue) {
    auto state = m_queueState;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->work.push_back(QueueWorkItem{std::move(operation), signalFence, signalValue});
        if (!state->workerStarted) {
            state->workerStarted = true;
            state->worker = std::thread([state] { runQueueWorker(state); });
        }
    }
    state->cond.notify_one();
    return S_OK;
}

HRESULT D3D12CommandQueueImpl::Signal(ID3D12Fence* pFence, UINT64 Value) {
    if (!pFence)
        return E_INVALIDARG;

    if (!hasQueuedWork())
        return pFence->Signal(Value);

    pFence->AddRef();
    return enqueueQueueWork(
        [pFence, Value] {
            pFence->Signal(Value);
            pFence->Release();
        },
        pFence, Value);
}

HRESULT D3D12CommandQueueImpl::Wait(ID3D12Fence* pFence, UINT64 Value) {
    if (!pFence)
        return E_INVALIDARG;

    pFence->AddRef();
    auto state = m_queueState;
    return enqueueQueueWork([state, pFence, Value] {
        for (;;) {
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->stopping)
                    break;
            }

            UINT64 completed = 0;
            if (SUCCEEDED(pFence->GetCompletedValue(&completed)) && completed >= Value)
                break;

            QueueWorkItem satisfyingSignal;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                for (auto it = state->work.begin(); it != state->work.end(); ++it) {
                    if (it->signalFence == pFence && it->signalValue >= Value) {
                        satisfyingSignal = std::move(*it);
                        state->work.erase(it);
                        break;
                    }
                }
            }
            if (satisfyingSignal.operation) {
                satisfyingSignal.operation();
                continue;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        pFence->Release();
    });
}

HRESULT D3D12CommandQueueImpl::ExecuteCommandLists(UINT numLists, ID3D12CommandList* const* ppLists) {
    if (hasQueuedWork()) {
        std::vector<ID3D12CommandList*> lists;
        lists.reserve(numLists);
        for (UINT i = 0; i < numLists; ++i) {
            if (!ppLists[i])
                continue;
            ppLists[i]->AddRef();
            lists.push_back(ppLists[i]);
        }

        MetalDevice* queuedMetalDevice = &metalDevice;
        return enqueueQueueWork([queuedMetalDevice, lists = std::move(lists)]() mutable {
            for (auto* list : lists) {
                list->__execute(queuedMetalDevice);
                list->Release();
            }
        });
    }

    for (UINT i = 0; i < numLists; ++i) {
        if (!ppLists[i])
            continue;
        ppLists[i]->__execute(&metalDevice);
    }
    return S_OK;
}

D3D12DescriptorHeapImpl* D3D12DeviceImpl::findHeapForHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle) const {
    for (auto* h : m_trackedHeaps) {
        if (h->handleToIndex(handle) != UINT_MAX)
            return h;
    }
    return nullptr;
}

D3D12DescriptorHeapImpl* D3D12DeviceImpl::findHeapForGPUHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle) const {
    for (auto* h : m_trackedHeaps) {
        if (h->gpuHandleToIndex(handle) != UINT_MAX)
            return h;
    }
    return nullptr;
}

D3D12ResourceImpl* D3D12DeviceImpl::findResourceForGPUAddress(D3D12_GPU_VIRTUAL_ADDRESS address, UINT64* offset) const {
    if (!m_gpuAddressResources) {
        if (offset)
            *offset = 0;
        return nullptr;
    }
    for (const auto& [base, resource] : *m_gpuAddressResources) {
        if (!resource || address < base)
            continue;
        UINT64 localOffset = address - base;
        if (localOffset < resource->bufferSize()) {
            if (offset)
                *offset = localOffset;
            return resource;
        }
    }
    if (offset)
        *offset = 0;
    return nullptr;
}

} // namespace metalsharp
