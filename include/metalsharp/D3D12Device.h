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
};

class D3D12DescriptorHeapImpl final : public ID3D12DescriptorHeap {
public:
    ULONG refCount = 1;
    D3D12_DESCRIPTOR_HEAP_DESC desc;
    std::vector<D3D12Descriptor> descriptors;

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
    D3D12_GPU_DESCRIPTOR_HANDLE __getGPUDescriptorHandleForHeapStart() override { return {1}; }
    UINT __getDescriptorCount() const override { return desc.NumDescriptors; }
    UINT __getHeapType() const override { return desc.Type; }

    D3D12Descriptor* getDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle) {
        if (handle.ptr == 0 || handle.ptr > desc.NumDescriptors) return nullptr;
        return &descriptors[handle.ptr - 1];
    }
};

class D3D12RootSignatureImpl final : public ID3D12RootSignature {
public:
    ULONG refCount = 1;

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
        *ppHeap = new D3D12DescriptorHeapImpl(pDesc);
        return S_OK;
    }

    HRESULT CreateRootSignature(UINT, const void*, SIZE_T, REFIID riid, void** ppRS) override {
        if (!ppRS) return E_POINTER;
        *ppRS = new D3D12RootSignatureImpl();
        return S_OK;
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

    HRESULT CreateConstantBufferView(const void* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE handle) override { return E_NOTIMPL; }

    HRESULT CreateSampler(const void* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE handle) override { return E_NOTIMPL; }

    UINT GetDescriptorHandleIncrementSize(UINT) override { return 1; }

    HRESULT ReserveTiles(ID3D12Resource*, UINT, const D3D12_TILED_RESOURCE_COORDINATE*, const D3D12_TILE_REGION_SIZE*, BOOL) override { return S_OK; }
    HRESULT GetResourceTiling(ID3D12Resource*, UINT*, const void*, const void*, UINT*, UINT, void*) override { return E_NOTIMPL; }

private:
    HRESULT createView(ID3D12Resource* pResource, UINT type, UINT format, D3D12_CPU_DESCRIPTOR_HANDLE handle) {
        if (handle.ptr == 0) return E_INVALIDARG;
        return S_OK;
    }
};

}
