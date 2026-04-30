#include <d3d/D3D12.h>
#include <metalsharp/MetalBackend.h>
#include <metalsharp/FormatTranslation.h>
#include <metalsharp/PipelineState.h>
#include <metalsharp/ShaderTranslator.h>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace metalsharp {

static HRESULT E_NOT_IMPL = E_NOTIMPL;

class D3D12FenceImpl final : public ID3D12Fence {
public:
    ULONG refCount = 1;
    UINT64 m_value = 0;
    UINT64 m_completed = 0;

    HRESULT QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_ID3D12Fence || riid == IID_IUnknown) { AddRef(); *ppv = this; return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG AddRef() override { return ++refCount; }
    ULONG Release() override { ULONG c = --refCount; if (c == 0) delete this; return c; }
    STDMETHOD(GetPrivateData)(const GUID&, UINT*, void*) override { return E_NOTIMPL; }
    STDMETHOD(SetPrivateData)(const GUID&, UINT, const void*) override { return E_NOTIMPL; }
    STDMETHOD(SetPrivateDataInterface)(const GUID&, const IUnknown*) override { return E_NOTIMPL; }
    STDMETHOD(SetName)(const char*) override { return S_OK; }

    HRESULT GetCompletedValue(UINT64* pValue) override {
        if (!pValue) return E_POINTER;
        *pValue = m_completed;
        return S_OK;
    }
    HRESULT SetEventOnCompletion(UINT64 Value, HANDLE hEvent) override { return S_OK; }
    HRESULT Signal(UINT64 Value) override { m_value = Value; m_completed = Value; return S_OK; }
};

class D3D12CommandAllocatorImpl final : public ID3D12CommandAllocator {
public:
    ULONG refCount = 1;

    HRESULT QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_ID3D12CommandAllocator || riid == IID_IUnknown) { AddRef(); *ppv = this; return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG AddRef() override { return ++refCount; }
    ULONG Release() override { ULONG c = --refCount; if (c == 0) delete this; return c; }
    STDMETHOD(GetPrivateData)(const GUID&, UINT*, void*) override { return E_NOTIMPL; }
    STDMETHOD(SetPrivateData)(const GUID&, UINT, const void*) override { return E_NOTIMPL; }
    STDMETHOD(SetPrivateDataInterface)(const GUID&, const IUnknown*) override { return E_NOTIMPL; }
    STDMETHOD(SetName)(const char*) override { return S_OK; }
    HRESULT Reset() override { return S_OK; }
};

class D3D12CommandQueueImpl final : public ID3D12CommandQueue {
public:
    ULONG refCount = 1;
    MetalDevice& metalDevice;

    explicit D3D12CommandQueueImpl(MetalDevice& dev) : metalDevice(dev) {}

    HRESULT QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_ID3D12CommandQueue || riid == IID_IUnknown) { AddRef(); *ppv = this; return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG AddRef() override { return ++refCount; }
    ULONG Release() override { ULONG c = --refCount; if (c == 0) delete this; return c; }
    STDMETHOD(GetPrivateData)(const GUID&, UINT*, void*) override { return E_NOTIMPL; }
    STDMETHOD(SetPrivateData)(const GUID&, UINT, const void*) override { return E_NOTIMPL; }
    STDMETHOD(SetPrivateDataInterface)(const GUID&, const IUnknown*) override { return E_NOTIMPL; }
    STDMETHOD(SetName)(const char*) override { return S_OK; }

    HRESULT ExecuteCommandLists(UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists) override;
    HRESULT Signal(ID3D12Fence* pFence, UINT64 Value) override {
        if (!pFence) return E_INVALIDARG;
        return pFence->Signal(Value);
    }
    HRESULT Wait(ID3D12Fence* pFence, UINT64 Value) override { return S_OK; }
};

struct D3D12Descriptor {
    ID3D12Resource* resource = nullptr;
    UINT type = 0;
    UINT format = 0;
    void* metalTexture = nullptr;
    void* metalBuffer = nullptr;
    UINT64 bufferOffset = 0;
    UINT64 bufferSize = 0;
    D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
    void* metalSampler = nullptr;
    UINT shaderRegister = 0;
    UINT registerSpace = 0;
    UINT numDescriptors = 1;
};

class D3D12DescriptorHeapImpl final : public ID3D12DescriptorHeap {
public:
    ULONG refCount = 1;
    D3D12_DESCRIPTOR_HEAP_DESC desc;
    std::vector<D3D12Descriptor> descriptors;
    void* metalArgumentBuffer = nullptr;

    D3D12DescriptorHeapImpl(const D3D12_DESCRIPTOR_HEAP_DESC* d) : desc(*d), descriptors(d->NumDescriptors) {}

    HRESULT QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_ID3D12DescriptorHeap || riid == IID_IUnknown) { AddRef(); *ppv = this; return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG AddRef() override { return ++refCount; }
    ULONG Release() override { ULONG c = --refCount; if (c == 0) delete this; return c; }
    STDMETHOD(GetPrivateData)(const GUID&, UINT*, void*) override { return E_NOTIMPL; }
    STDMETHOD(SetPrivateData)(const GUID&, UINT, const void*) override { return E_NOTIMPL; }
    STDMETHOD(SetPrivateDataInterface)(const GUID&, const IUnknown*) override { return E_NOTIMPL; }
    STDMETHOD(SetName)(const char*) override { return S_OK; }

    D3D12_CPU_DESCRIPTOR_HANDLE __getCPUDescriptorHandleForHeapStart() override { return {1}; }
    D3D12_GPU_DESCRIPTOR_HANDLE __getGPUDescriptorHandleForHeapStart() override { return {reinterpret_cast<UINT64>(this) + 1}; }
    UINT __getDescriptorCount() const override { return desc.NumDescriptors; }
    UINT __getHeapType() const override { return desc.Type; }

    D3D12Descriptor* getDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle) {
        if (handle.ptr == 0 || handle.ptr > desc.NumDescriptors) return nullptr;
        return &descriptors[handle.ptr - 1];
    }

    D3D12Descriptor* getDescriptorByIndex(UINT index) {
        if (index >= desc.NumDescriptors) return nullptr;
        return &descriptors[index];
    }

    UINT handleToIndex(D3D12_CPU_DESCRIPTOR_HANDLE handle) const {
        if (handle.ptr == 0 || handle.ptr > desc.NumDescriptors) return UINT_MAX;
        return static_cast<UINT>(handle.ptr - 1);
    }

    UINT gpuHandleToIndex(D3D12_GPU_DESCRIPTOR_HANDLE handle) const {
        UINT64 base = reinterpret_cast<UINT64>(this) + 1;
        if (handle.ptr < base) return UINT_MAX;
        UINT64 offset = handle.ptr - base;
        if (offset >= desc.NumDescriptors) return UINT_MAX;
        return static_cast<UINT>(offset);
    }

    void copyDescriptors(UINT numDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE dstStart, D3D12_CPU_DESCRIPTOR_HANDLE srcStart) {
        UINT dstIdx = handleToIndex(dstStart);
        UINT srcIdx = handleToIndex(srcStart);
        for (UINT i = 0; i < numDescriptors; ++i) {
            if (dstIdx + i < desc.NumDescriptors && srcIdx + i < desc.NumDescriptors) {
                descriptors[dstIdx + i] = descriptors[srcIdx + i];
            }
        }
    }
};

class D3D12RootSignatureImpl final : public ID3D12RootSignature {
public:
    ULONG refCount = 1;
    std::vector<D3D12_ROOT_PARAMETER> parameters;
    std::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplers;
    UINT flags = 0;
    std::vector<uint8_t> rawBytecode;
    uint32_t numParameters = 0;
    uint64_t argumentBufferSize = 0;

    struct RootParameterLayout {
        uint32_t type;
        uint32_t offset;
        uint32_t size;
        uint32_t shaderVisibility;
    };
    std::vector<RootParameterLayout> parameterLayouts;

    HRESULT QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_ID3D12RootSignature || riid == IID_IUnknown) { AddRef(); *ppv = this; return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG AddRef() override { return ++refCount; }
    ULONG Release() override { ULONG c = --refCount; if (c == 0) delete this; return c; }
    STDMETHOD(GetPrivateData)(const GUID&, UINT*, void*) override { return E_NOTIMPL; }
    STDMETHOD(SetPrivateData)(const GUID&, UINT, const void*) override { return E_NOTIMPL; }
    STDMETHOD(SetPrivateDataInterface)(const GUID&, const IUnknown*) override { return E_NOTIMPL; }
    STDMETHOD(SetName)(const char*) override { return S_OK; }

    void computeLayout() {
        parameterLayouts.clear();
        uint32_t offset = 0;
        for (uint32_t i = 0; i < numParameters; ++i) {
            RootParameterLayout layout;
            layout.type = (i < parameters.size()) ? parameters[i].ParameterType : 0;
            layout.offset = offset;
            layout.shaderVisibility = (i < parameters.size()) ? parameters[i].ShaderVisibility : 0;

            switch (layout.type) {
                case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS: {
                    uint32_t numConst = (i < parameters.size()) ? parameters[i].Constants.Num32BitValues : 0;
                    layout.size = numConst * sizeof(uint32_t);
                    if (layout.size < 4) layout.size = 4;
                    break;
                }
                case D3D12_ROOT_PARAMETER_TYPE_CBV:
                case D3D12_ROOT_PARAMETER_TYPE_SRV:
                case D3D12_ROOT_PARAMETER_TYPE_UAV:
                    layout.size = sizeof(uint64_t);
                    break;
                case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
                    layout.size = sizeof(uint64_t);
                    break;
                default:
                    layout.size = sizeof(uint64_t);
                    break;
            }

            parameterLayouts.push_back(layout);
            offset += layout.size;
            offset = (offset + 15u) & ~15u;
        }
        argumentBufferSize = offset > 0 ? offset : 256;
    }
};

class D3D12PipelineStateImpl final : public ID3D12PipelineState {
public:
    ULONG refCount = 1;
    void* m_renderPipeline = nullptr;
    void* m_computePipeline = nullptr;

    HRESULT QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_ID3D12PipelineState || riid == IID_IUnknown) { AddRef(); *ppv = this; return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG AddRef() override { return ++refCount; }
    ULONG Release() override { ULONG c = --refCount; if (c == 0) delete this; return c; }
    STDMETHOD(GetPrivateData)(const GUID&, UINT*, void*) override { return E_NOTIMPL; }
    STDMETHOD(SetPrivateData)(const GUID&, UINT, const void*) override { return E_NOTIMPL; }
    STDMETHOD(SetPrivateDataInterface)(const GUID&, const IUnknown*) override { return E_NOTIMPL; }
    STDMETHOD(SetName)(const char*) override { return S_OK; }
    void* __metalRenderPipelineState() const override { return m_renderPipeline; }
    void* __metalComputePipelineState() const override { return m_computePipeline; }
};

class D3D12CommandSignatureImpl final : public ID3D12CommandSignature {
public:
    ULONG refCount = 1;
    HRESULT QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_ID3D12CommandSignature || riid == IID_IUnknown) { AddRef(); *ppv = this; return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG AddRef() override { return ++refCount; }
    ULONG Release() override { ULONG c = --refCount; if (c == 0) delete this; return c; }
    STDMETHOD(GetPrivateData)(const GUID&, UINT*, void*) override { return E_NOTIMPL; }
    STDMETHOD(SetPrivateData)(const GUID&, UINT, const void*) override { return E_NOTIMPL; }
    STDMETHOD(SetPrivateDataInterface)(const GUID&, const IUnknown*) override { return E_NOTIMPL; }
    STDMETHOD(SetName)(const char*) override { return S_OK; }
};

class D3D12StateObjectImpl final : public ID3D12StateObject {
public:
    ULONG refCount = 1;
    void* m_rtPipeline = nullptr;
    std::vector<uint8_t> m_shaderIdentifierData;

    HRESULT QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown) { AddRef(); *ppv = this; return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG AddRef() override { return ++refCount; }
    ULONG Release() override { ULONG c = --refCount; if (c == 0) delete this; return c; }
    STDMETHOD(GetPrivateData)(const GUID&, UINT*, void*) override { return E_NOTIMPL; }
    STDMETHOD(SetPrivateData)(const GUID&, UINT, const void*) override { return E_NOTIMPL; }
    STDMETHOD(SetPrivateDataInterface)(const GUID&, const IUnknown*) override { return E_NOTIMPL; }
    STDMETHOD(SetName)(const char*) override { return S_OK; }
    void* __metalRTPipeline() const override { return m_rtPipeline; }
};

class D3D12ResourceImpl final : public ID3D12Resource {
public:
    ULONG refCount = 1;
    D3D12_RESOURCE_DESC desc;
    std::unique_ptr<MetalBuffer> metalBuffer;
    std::unique_ptr<MetalTexture> metalTexture;
    UINT m_resourceState;
    UINT64 m_gpuAddress = 0;

    D3D12ResourceImpl(const D3D12_RESOURCE_DESC& d) : desc(d), m_resourceState(D3D12_RESOURCE_STATE_COMMON) {}

    HRESULT QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_ID3D12Resource || riid == IID_IUnknown) { AddRef(); *ppv = this; return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG AddRef() override { return ++refCount; }
    ULONG Release() override { ULONG c = --refCount; if (c == 0) delete this; return c; }
    STDMETHOD(GetPrivateData)(const GUID&, UINT*, void*) override { return E_NOTIMPL; }
    STDMETHOD(SetPrivateData)(const GUID&, UINT, const void*) override { return E_NOTIMPL; }
    STDMETHOD(SetPrivateDataInterface)(const GUID&, const IUnknown*) override { return E_NOTIMPL; }
    STDMETHOD(SetName)(const char*) override { return S_OK; }

    HRESULT Map(UINT, const D3D12_RANGE*, void** ppData) override {
        if (!ppData) return E_POINTER;
        if (metalBuffer) { *ppData = metalBuffer->contents(); return S_OK; }
        return E_FAIL;
    }
    HRESULT Unmap(UINT, const D3D12_RANGE*) override { return S_OK; }
    HRESULT GetDesc(D3D12_RESOURCE_DESC* pDesc) override {
        if (!pDesc) return E_POINTER;
        *pDesc = desc;
        return S_OK;
    }
    HRESULT GetGPUVirtualAddress(D3D12_GPU_VIRTUAL_ADDRESS* pAddress) override {
        if (!pAddress) return E_POINTER;
        *pAddress = m_gpuAddress;
        return S_OK;
    }
    void* __metalBufferPtr() const override { return metalBuffer ? metalBuffer->nativeBuffer() : nullptr; }
    void* __metalTexturePtr() const override { return metalTexture ? metalTexture->nativeTexture() : nullptr; }
    UINT __getResourceState() const override { return m_resourceState; }
    void __setResourceState(UINT state) override { m_resourceState = state; }
};

class D3D12DeviceImpl final : public ID3D12Device {
public:
    static HRESULT create(D3D12DeviceImpl** ppDevice) {
        if (!ppDevice) return E_POINTER;
        auto* device = new D3D12DeviceImpl();
        device->m_metalDevice = std::unique_ptr<MetalDevice>(MetalDevice::create());
        if (!device->m_metalDevice) { delete device; return E_FAIL; }
        device->m_shaderTranslator = std::make_unique<ShaderTranslator>();
        *ppDevice = device;
        return S_OK;
    }

    MetalDevice& metalDevice() { return *m_metalDevice; }

    ULONG m_refCount = 1;
    std::unique_ptr<MetalDevice> m_metalDevice;
    std::unique_ptr<ShaderTranslator> m_shaderTranslator;

    HRESULT QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_ID3D12Device || riid == IID_IUnknown) { AddRef(); *ppv = this; return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG AddRef() override { return ++m_refCount; }
    ULONG Release() override { ULONG c = --m_refCount; if (c == 0) delete this; return c; }
    STDMETHOD(GetPrivateData)(const GUID&, UINT*, void*) override { return E_NOTIMPL; }
    STDMETHOD(SetPrivateData)(const GUID&, UINT, const void*) override { return E_NOTIMPL; }
    STDMETHOD(SetPrivateDataInterface)(const GUID&, const IUnknown*) override { return E_NOTIMPL; }
    STDMETHOD(SetName)(const char*) override { return S_OK; }

    HRESULT CreateCommandQueue(const void* pDesc, REFIID riid, void** ppCommandQueue) override {
        if (!ppCommandQueue) return E_POINTER;
        *ppCommandQueue = new D3D12CommandQueueImpl(*m_metalDevice);
        return S_OK;
    }

    HRESULT CreateCommandAllocator(UINT type, REFIID riid, void** ppAllocator) override {
        if (!ppAllocator) return E_POINTER;
        *ppAllocator = new D3D12CommandAllocatorImpl();
        return S_OK;
    }

    HRESULT CreateCommandList(UINT nodeMask, UINT type, ID3D12CommandAllocator* pAllocator, ID3D12PipelineState* pInitialState, REFIID riid, void** ppCommandList) override;

    HRESULT CreateCommittedResource(const D3D12_HEAP_PROPERTIES* pHeap, UINT HeapFlags, const D3D12_RESOURCE_DESC* pDesc, UINT InitialState, const void* pClearVal, REFIID riid, void** ppvResource) override {
        if (!pDesc || !ppvResource) return E_INVALIDARG;

        auto* res = new D3D12ResourceImpl(*pDesc);
        res->m_resourceState = InitialState;

        if (pDesc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
            res->metalBuffer = std::unique_ptr<MetalBuffer>(MetalBuffer::create(*m_metalDevice, (size_t)pDesc->Width, nullptr));
            if (res->metalBuffer) {
                res->m_gpuAddress = reinterpret_cast<UINT64>(res->metalBuffer->nativeBuffer());
            }
        } else {
            uint32_t fmt = dxgiFormatToMetal((DXGITranslation)pDesc->Format);
            uint32_t usage = 0;
            if (pDesc->Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) usage |= 0x1;
            if (pDesc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) usage |= 0x1;
            if (pDesc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) usage |= 0x4;
            if (!(pDesc->Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) usage |= 0x2 | 0x8;
            uint32_t mips = pDesc->MipLevels > 0 ? pDesc->MipLevels : 1;
            uint32_t samples = pDesc->SampleDesc.Count > 0 ? pDesc->SampleDesc.Count : 1;

            if (pDesc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
                res->metalTexture = std::unique_ptr<MetalTexture>(
                    MetalTexture::create2D(*m_metalDevice, (uint32_t)pDesc->Width, pDesc->Height, fmt, usage, mips, samples));
            } else if (pDesc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D) {
                res->metalTexture = std::unique_ptr<MetalTexture>(
                    MetalTexture::create1D(*m_metalDevice, (uint32_t)pDesc->Width, fmt, usage, mips));
            } else if (pDesc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
                res->metalTexture = std::unique_ptr<MetalTexture>(
                    MetalTexture::create3D(*m_metalDevice, (uint32_t)pDesc->Width, pDesc->Height, pDesc->DepthOrArraySize, fmt, usage, mips));
            }
        }

        *ppvResource = res;
        return S_OK;
    }

    HRESULT CreateDescriptorHeap(const D3D12_DESCRIPTOR_HEAP_DESC* pDesc, REFIID riid, void** ppHeap) override {
        if (!pDesc || !ppHeap) return E_INVALIDARG;
        auto* heap = new D3D12DescriptorHeapImpl(pDesc);
        m_trackedHeaps.push_back(heap);
        *ppHeap = heap;
        return S_OK;
    }

    HRESULT CreateRootSignature(UINT, const void* pBlob, SIZE_T blobSize, REFIID riid, void** ppRS) override {
        if (!ppRS) return E_POINTER;
        auto* rs = new D3D12RootSignatureImpl();
        rs->rawBytecode.assign(static_cast<const uint8_t*>(pBlob), static_cast<const uint8_t*>(pBlob) + blobSize);

        const uint8_t* data = rs->rawBytecode.data();
        size_t size = rs->rawBytecode.size();

        if (size >= 4) {
            uint32_t magic;
            memcpy(&magic, data, 4);

            if (magic == 0x43425844 && size >= 32) {
                uint32_t chunkCount;
                memcpy(&chunkCount, data + 28, 4);
                for (uint32_t i = 0; i < chunkCount; ++i) {
                    uint32_t offset;
                    if (32 + i * 4 + 4 > size) break;
                    memcpy(&offset, data + 32 + i * 4, 4);
                    if (offset + 8 > size) continue;
                    uint32_t chunkMagic;
                    memcpy(&chunkMagic, data + offset, 4);
                    uint32_t chunkSize;
                    memcpy(&chunkSize, data + offset + 4, 4);
                    if (offset + 8 + chunkSize > size) continue;

                    if (chunkMagic == 0x30535452) {
                        parseRootSignatureBlob(data + offset + 8, chunkSize, rs);
                    }
                }
            } else {
                parseRootSignatureBlob(data, size, rs);
            }
        }

        rs->computeLayout();
        *ppRS = rs;
        return S_OK;
    }

    void parseRootSignatureBlob(const uint8_t* data, size_t size, D3D12RootSignatureImpl* rs) {
        if (size < 16) return;
        uint32_t version;
        memcpy(&version, data, 4);
        uint32_t numParams;
        memcpy(&numParams, data + 4, 4);
        uint32_t numStaticSamplers;
        memcpy(&numStaticSamplers, data + 8, 4);
        uint32_t flags;
        memcpy(&flags, data + 12, 4);

        rs->numParameters = numParams;
        rs->flags = flags;

        size_t paramOffset = 16;
        for (uint32_t i = 0; i < numParams && paramOffset + 16 <= size; ++i) {
            D3D12_ROOT_PARAMETER param = {};
            memcpy(&param.ParameterType, data + paramOffset, 4);
            memcpy(&param.ShaderVisibility, data + paramOffset + 4, 4);

            switch (param.ParameterType) {
                case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                    memcpy(&param.Constants.ShaderRegister, data + paramOffset + 8, 4);
                    memcpy(&param.Constants.RegisterSpace, data + paramOffset + 12, 4);
                    memcpy(&param.Constants.Num32BitValues, data + paramOffset + 16, 4);
                    paramOffset += 20;
                    break;
                case D3D12_ROOT_PARAMETER_TYPE_CBV:
                case D3D12_ROOT_PARAMETER_TYPE_SRV:
                case D3D12_ROOT_PARAMETER_TYPE_UAV:
                    memcpy(&param.Descriptor.ShaderRegister, data + paramOffset + 8, 4);
                    memcpy(&param.Descriptor.RegisterSpace, data + paramOffset + 12, 4);
                    paramOffset += 16;
                    break;
                case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: {
                    uint32_t numRanges;
                    memcpy(&numRanges, data + paramOffset + 8, 4);
                    paramOffset += 16;
                    D3D12_ROOT_DESCRIPTOR_TABLE table = {};
                    table.NumDescriptorRanges = numRanges;
                    param.DescriptorTable = table;
                    paramOffset += numRanges * 20;
                    break;
                }
                default:
                    paramOffset += 16;
                    break;
            }
            rs->parameters.push_back(param);
        }
    }

    HRESULT CreateGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc, REFIID riid, void** ppPSO) override;

    HRESULT CreateComputePipelineState(const void* pDesc, REFIID riid, void** ppPSO) override {
        if (!ppPSO) return E_POINTER;
        *ppPSO = new D3D12PipelineStateImpl();
        return S_OK;
    }

    HRESULT CreateFence(UINT64 InitialValue, UINT Flags, REFIID riid, void** ppFence) override {
        if (!ppFence) return E_POINTER;
        auto* fence = new D3D12FenceImpl();
        fence->m_value = InitialValue;
        fence->m_completed = InitialValue;
        *ppFence = fence;
        return S_OK;
    }

    HRESULT CreateCommandSignature(const D3D12_COMMAND_SIGNATURE_DESC*, ID3D12RootSignature*, REFIID, void** ppSig) override {
        if (!ppSig) return E_POINTER;
        *ppSig = new D3D12CommandSignatureImpl();
        return S_OK;
    }

    HRESULT CreateRenderTargetView(ID3D12Resource* pResource, const void* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE handle) override {
        return createView(pResource, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, DXGI_FORMAT_UNKNOWN, handle);
    }

    HRESULT CreateDepthStencilView(ID3D12Resource* pResource, const void* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE handle) override {
        return createView(pResource, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, DXGI_FORMAT_UNKNOWN, handle);
    }

    HRESULT CreateShaderResourceView(ID3D12Resource* pResource, const void* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE handle) override {
        return createView(pResource, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, DXGI_FORMAT_UNKNOWN, handle);
    }

    HRESULT CreateUnorderedAccessView(ID3D12Resource* pResource, ID3D12Resource*, const void* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE handle) override {
        return createView(pResource, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, DXGI_FORMAT_UNKNOWN, handle);
    }

    HRESULT CreateConstantBufferView(const void* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE handle) override {
        if (!pDesc || handle.ptr == 0) return E_INVALIDARG;
        D3D12DescriptorHeapImpl* heap = nullptr;
        for (auto* h : m_trackedHeaps) {
            if (h->handleToIndex(handle) != UINT_MAX) { heap = h; break; }
        }
        if (!heap) return S_OK;
        auto* desc = heap->getDescriptor(handle);
        if (!desc) return S_OK;

        struct CBVDesc { D3D12_GPU_VIRTUAL_ADDRESS BufferLocation; UINT SizeInBytes; };
        auto* cbv = static_cast<const CBVDesc*>(pDesc);
        desc->gpuAddress = cbv->BufferLocation;
        desc->bufferSize = cbv->SizeInBytes;
        desc->type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        return S_OK;
    }

    HRESULT CreateSampler(const void* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE handle) override {
        if (!pDesc || handle.ptr == 0) return E_INVALIDARG;
        D3D12DescriptorHeapImpl* heap = nullptr;
        for (auto* h : m_trackedHeaps) {
            if (h->handleToIndex(handle) != UINT_MAX) { heap = h; break; }
        }
        if (!heap) return S_OK;
        auto* desc = heap->getDescriptor(handle);
        if (!desc) return S_OK;

        desc->type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        auto* samplerDesc = static_cast<const D3D12_STATIC_SAMPLER_DESC*>(pDesc);
        if (samplerDesc) {
            desc->shaderRegister = samplerDesc->ShaderRegister;
            desc->registerSpace = samplerDesc->RegisterSpace;
        }
        return S_OK;
    }

    UINT GetDescriptorHandleIncrementSize(UINT) override { return 1; }

    HRESULT ReserveTiles(ID3D12Resource* pTiledResource, UINT NumTileRegions, const D3D12_TILED_RESOURCE_COORDINATE* pCoords, const D3D12_TILE_REGION_SIZE* pSizes, BOOL bSingleTile) override {
        if (!pTiledResource) return E_INVALIDARG;
        return S_OK;
    }

    HRESULT GetResourceTiling(ID3D12Resource* pTiledResource, UINT* pNumTilesForResource, D3D12_PACKED_MIP_INFO* pPackedMipDesc, D3D12_TILE_SHAPE* pStandardTileShape, UINT* pNumSubresourceTilings, UINT FirstSubresourceTilingToGet, D3D12_SUBRESOURCE_TILING* pSubresourceTilings) override {
        if (!pTiledResource) return E_INVALIDARG;

        D3D12_RESOURCE_DESC desc;
        if (FAILED(pTiledResource->GetDesc(&desc))) return E_FAIL;

        const UINT TILE_W = 128;
        const UINT TILE_H = 128;
        const UINT TILE_D = 1;

        UINT totalMips = desc.MipLevels > 0 ? desc.MipLevels : 1;
        UINT packedMips = 0;
        if (totalMips > 1) {
            UINT w = (UINT)desc.Width, h = desc.Height, d = desc.DepthOrArraySize;
            for (UINT i = 0; i < totalMips; i++) {
                if (w < TILE_W && h < TILE_H) { packedMips = totalMips - i; break; }
                w = w > 1 ? w >> 1 : 1;
                h = h > 1 ? h >> 1 : 1;
                d = d > 1 ? d >> 1 : 1;
            }
        }
        UINT standardMips = totalMips - packedMips;

        UINT totalTiles = 0;
        for (UINT i = 0; i < standardMips; i++) {
            UINT w = (UINT)desc.Width >> i; if (w == 0) w = 1;
            UINT h = desc.Height >> i; if (h == 0) h = 1;
            UINT d = desc.DepthOrArraySize >> i; if (d == 0) d = 1;
            UINT tw = (w + TILE_W - 1) / TILE_W;
            UINT th = (h + TILE_H - 1) / TILE_H;
            totalTiles += tw * th * d;
        }
        UINT packedTiles = packedMips > 0 ? 1 : 0;

        if (pPackedMipDesc) {
            pPackedMipDesc->NumStandardMips = standardMips;
            pPackedMipDesc->NumPackedMips = packedMips;
            pPackedMipDesc->NumTilesForPackedMips = packedTiles;
            pPackedMipDesc->StartTileIndexInOverallResource = standardMips > 0 ? totalTiles : 0;
        }
        if (pStandardTileShape) {
            pStandardTileShape->WidthInTexels = TILE_W;
            pStandardTileShape->HeightInTexels = TILE_H;
            pStandardTileShape->DepthInTexels = TILE_D;
        }
        if (pNumTilesForResource) {
            *pNumTilesForResource = totalTiles + packedTiles;
        }
        if (pNumSubresourceTilings && pSubresourceTilings) {
            UINT count = 0;
            for (UINT i = FirstSubresourceTilingToGet; i < totalMips && count < *pNumSubresourceTilings; i++, count++) {
                if (i < standardMips) {
                    UINT w = (UINT)desc.Width >> i; if (w == 0) w = 1;
                    UINT h = desc.Height >> i; if (h == 0) h = 1;
                    UINT d = desc.DepthOrArraySize >> i; if (d == 0) d = 1;
                    pSubresourceTilings[i - FirstSubresourceTilingToGet].WidthInTiles = (w + TILE_W - 1) / TILE_W;
                    pSubresourceTilings[i - FirstSubresourceTilingToGet].HeightInTiles = (h + TILE_H - 1) / TILE_H;
                    pSubresourceTilings[i - FirstSubresourceTilingToGet].DepthInTiles = d;
                    pSubresourceTilings[i - FirstSubresourceTilingToGet].StartTileIndexInOverallResource = i == 0 ? 0 : 0;
                    pSubresourceTilings[i - FirstSubresourceTilingToGet].NumTiles =
                        pSubresourceTilings[i - FirstSubresourceTilingToGet].WidthInTiles *
                        pSubresourceTilings[i - FirstSubresourceTilingToGet].HeightInTiles * d;
                } else {
                    pSubresourceTilings[i - FirstSubresourceTilingToGet].WidthInTiles = 0;
                    pSubresourceTilings[i - FirstSubresourceTilingToGet].HeightInTiles = 0;
                    pSubresourceTilings[i - FirstSubresourceTilingToGet].DepthInTiles = 0;
                    pSubresourceTilings[i - FirstSubresourceTilingToGet].StartTileIndexInOverallResource = totalTiles;
                    pSubresourceTilings[i - FirstSubresourceTilingToGet].NumTiles = packedTiles;
                }
            }
            *pNumSubresourceTilings = count;
        }

        return S_OK;
    }

    UINT64 GetTiledResourceSize(UINT64 Width, UINT Height, UINT DepthOrArraySize, UINT Format, UINT MipLevels) override {
        const UINT TILE_W = 128;
        const UINT TILE_H = 128;
        const UINT64 TILE_BYTES = D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;

        UINT totalMips = MipLevels > 0 ? MipLevels : 1;
        UINT totalTiles = 0;
        for (UINT i = 0; i < totalMips; i++) {
            UINT w = (UINT)(Width >> i); if (w == 0) w = 1;
            UINT h = (UINT)(Height >> i); if (h == 0) h = 1;
            UINT d = (UINT)(DepthOrArraySize >> i); if (d == 0) d = 1;
            UINT tw = (w + TILE_W - 1) / TILE_W;
            UINT th = (h + TILE_H - 1) / TILE_H;
            totalTiles += tw * th * d;
        }
        return totalTiles * TILE_BYTES;
    }

    HRESULT CreateSamplerFeedback(ID3D12Resource* pTargetResource, UINT FeedbackType, REFIID riid, void** ppFeedbackResource) override {
        if (!pTargetResource || !ppFeedbackResource) return E_INVALIDARG;
        D3D12_RESOURCE_DESC targetDesc;
        if (FAILED(pTargetResource->GetDesc(&targetDesc))) return E_FAIL;

        auto* feedbackRes = new D3D12ResourceImpl(targetDesc);
        feedbackRes->m_resourceState = D3D12_RESOURCE_STATE_COMMON;
        *ppFeedbackResource = feedbackRes;
        return S_OK;
    }

    HRESULT WriteSamplerFeedback(ID3D12Resource* pTargetResource, ID3D12Resource* pFeedbackResource, UINT FeedbackType) override {
        if (!pTargetResource || !pFeedbackResource) return E_INVALIDARG;
        return S_OK;
    }

    HRESULT ResolveSamplerFeedback(ID3D12Resource* pFeedbackResource, ID3D12Resource* pDestResource) override {
        if (!pFeedbackResource || !pDestResource) return E_INVALIDARG;
        return S_OK;
    }

    HRESULT CreateStateObject(const void* pDesc, REFIID riid, void** ppStateObject) override {
        if (!pDesc || !ppStateObject) return E_INVALIDARG;
        auto* desc = static_cast<const D3D12_STATE_OBJECT_DESC*>(pDesc);

        auto* stateObj = new D3D12StateObjectImpl();

        uint32_t maxRecursion = 0;
        uint32_t maxPayloadSize = 0;
        uint32_t maxAttributeSize = 0;
        std::vector<std::pair<const uint8_t*, size_t>> libraries;

        for (UINT i = 0; i < desc->NumSubobjects; ++i) {
            auto& sub = desc->pSubobjects[i];
            switch (sub.Type) {
                case 0: {
                    auto* config = static_cast<const D3D12_RAYTRACING_PIPELINE_CONFIG*>(sub.pDesc);
                    if (config) maxRecursion = config->MaxTraceRecursionDepth;
                    break;
                }
                case 1: {
                    auto* config = static_cast<const D3D12_RAYTRACING_SHADER_CONFIG*>(sub.pDesc);
                    if (config) {
                        maxPayloadSize = config->MaxPayloadSizeInBytes;
                        maxAttributeSize = config->MaxAttributeSizeInBytes;
                    }
                    break;
                }
                case 2: {
                    auto* lib = static_cast<const D3D12_DXIL_LIBRARY_DESC*>(sub.pDesc);
                    if (lib && lib->DXILLibrary && lib->DXILLibrarySize > 0) {
                        libraries.emplace_back(
                            static_cast<const uint8_t*>(lib->DXILLibrary),
                            lib->DXILLibrarySize
                        );
                    }
                    break;
                }
                case 5: {
                    auto* config1 = static_cast<const D3D12_RAYTRACING_PIPELINE_CONFIG1*>(sub.pDesc);
                    if (config1) maxRecursion = config1->MaxTraceRecursionDepth;
                    break;
                }
            }
        }

        auto& bridge = IRConverterBridge::instance();
        if (bridge.isAvailable() && !libraries.empty()) {
            IRConverterReflection reflection;
            std::vector<uint8_t> metallib;
            for (auto& [data, size] : libraries) {
                bridge.compileRayTracingShader(
                    data, size,
                    ShaderStage::RayGeneration,
                    nullptr,
                    maxRecursion,
                    maxAttributeSize > 0 ? maxAttributeSize : 32,
                    metallib,
                    reflection
                );
            }
            stateObj->m_shaderIdentifierData.resize(64, 0);
        }

        *ppStateObject = stateObj;
        return S_OK;
    }

    HRESULT GetRaytracingAccelerationStructurePrebuildInfo(const void* pDesc, void* pInfo) override {
        if (!pDesc || !pInfo) return E_INVALIDARG;
        auto* info = static_cast<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO*>(pInfo);
        info->ResultDataMaxSizeInBytes = 64 * 1024 * 1024;
        info->ScratchDataSizeInBytes = 32 * 1024 * 1024;
        info->UpdateScratchDataSizeInBytes = 32 * 1024 * 1024;
        return S_OK;
    }

    HRESULT DecodeRaytracingAccelerationStructure(ID3D12Resource*, void*) override {
        return E_NOTIMPL;
    }

private:
    HRESULT createView(ID3D12Resource* pResource, UINT type, UINT format, D3D12_CPU_DESCRIPTOR_HANDLE handle) {
        if (handle.ptr == 0) return E_INVALIDARG;

        D3D12DescriptorHeapImpl* heap = nullptr;
        for (auto* h : m_trackedHeaps) {
            if (h->handleToIndex(handle) != UINT_MAX) { heap = h; break; }
        }
        if (!heap) return S_OK;

        auto* desc = heap->getDescriptor(handle);
        if (!desc) return S_OK;

        desc->resource = pResource;
        desc->type = type;
        desc->format = format;

        if (pResource) {
            D3D12ResourceImpl* res = static_cast<D3D12ResourceImpl*>(pResource);
            desc->metalBuffer = res->__metalBufferPtr();
            desc->metalTexture = res->__metalTexturePtr();
            desc->gpuAddress = res->m_gpuAddress;

            D3D12_RESOURCE_DESC resDesc;
            if (SUCCEEDED(pResource->GetDesc(&resDesc))) {
                desc->bufferSize = resDesc.Width;
            }
        }
        return S_OK;
    }

    std::vector<D3D12DescriptorHeapImpl*> m_trackedHeaps;

    D3D12DescriptorHeapImpl* findHeapForHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle) const;
};

}
