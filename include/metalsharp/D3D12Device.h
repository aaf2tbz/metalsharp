/// @file D3D12Device.h
/// @brief Complete D3D12 API implementation backed by Metal.
///
/// D3D12 Object Hierarchy
/// ======================
///
///   D3D12Device (root)
///   ├── D3D12CommandQueue → MTLCommandQueue
///   │   └── D3D12GraphicsCommandList → MTLCommandBuffer + MTLRenderCommandEncoder
///   │       └── Argument buffer encoding (see ArgumentBufferBinding.h)
///   ├── D3D12DescriptorHeap → Flat array of descriptor structs
///   │   └── CBV/SRV/UAV/Sampler descriptors → Metal argument buffer slots
///   ├── D3D12Resource → MTLBuffer or MTLTexture
///   │   ├── Committed (allocated directly) vs Placed (from heap)
///   │   └── State tracking (COMMON→GENERIC_READ→COPY_DEST transitions)
///   ├── D3D12RootSignature → Metal argument table layout
///   │   └── Maps root parameters (constants, descriptors, tables) to Metal bindings
///   ├── D3D12PipelineState → MTLRenderPipelineState or MTLComputePipelineState
///   │   └── Created from compiled metallib shaders (via IRConverter/DXBC→MSL)
///   ├── D3D12Fence → dispatch_semaphore_t for GPU/CPU synchronization
///   └── D3D12StateObject → Ray tracing pipeline (stub, future work)
///
/// Key Design Decisions
/// ====================
///
///   - All D3D12 COM objects are implemented inline in this header for simplicity.
///     Each class carries its own refcount and delegates to Metal objects.
///   - Descriptor heaps are flat arrays; descriptors are indexed by offset.
///   - Resource barriers are tracked but Metal doesn't have explicit barriers —
///     we use MTLHeap resource aliasing and encoder-level tracking.
///   - Root signatures drive Metal argument buffer layout. Each root parameter
///     maps to a specific binding slot (see ArgumentBufferBinding.cpp).
///   - Command lists record into MTLCommandBuffer. ExecuteCommandLists submits
///     them to the MTLCommandQueue.
///
/// D3D12 → Metal Mapping
/// =====================
///
///   D3D12 Concept              → Metal Equivalent
///   ─────────────────────────────────────────────
///   ID3D12Device               → MTLDevice
///   ID3D12CommandQueue         → MTLCommandQueue
///   ID3D12GraphicsCommandList  → MTLCommandBuffer + encoders
///   ID3D12Resource (buffer)    → MTLBuffer
///   ID3D12Resource (texture)   → MTLTexture
///   ID3D12DescriptorHeap       → Argument buffer (MTLBuffer)
///   ID3D12RootSignature        → Argument table layout descriptor
///   ID3D12PipelineState        → MTLRenderPipelineState / MTLComputePipelineState
///   ID3D12Fence                → dispatch_semaphore_t + shared event
///   ResourceBarrier            → Implicit (Metal tracks resource state)
///   D3D12_TEXTURE_LAYOUT_UNKNOWN → MTLStorageModeShared/Private

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <d3d/D3D12.h>
#include <deque>
#include <functional>
#include <memory>
#include <metalsharp/D3D12ResourceStateTracker.h>
#include <metalsharp/FormatTranslation.h>
#include <metalsharp/MetalBackend.h>
#include <metalsharp/MetalCapabilities.h>
#include <metalsharp/PipelineState.h>
#include <metalsharp/ShaderTranslator.h>
#include <metalsharp/StubTelemetry.h>
#include <metalsharp/SyncContext.h>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace metalsharp {

static HRESULT E_NOT_IMPL = E_NOTIMPL;

class D3D12ResourceImpl;
using GPUAddressRegistry = std::unordered_map<UINT64, D3D12ResourceImpl*>;

struct GUIDHasher {
    size_t operator()(const GUID& guid) const {
        const auto* bytes = reinterpret_cast<const uint8_t*>(&guid);
        size_t value = 1469598103934665603ull;
        for (size_t i = 0; i < sizeof(GUID); ++i) {
            value ^= bytes[i];
            value *= 1099511628211ull;
        }
        return value;
    }
};

class D3D12PrivateDataStore {
  public:
    ~D3D12PrivateDataStore() { clearInterfaces(); }

    HRESULT GetPrivateData(const GUID& guid, UINT* pDataSize, void* pData) {
        if (!pDataSize)
            return E_POINTER;
        std::lock_guard<std::mutex> lock(m_mutex);

        auto iface = m_interfaces.find(guid);
        if (iface != m_interfaces.end()) {
            const UINT requiredSize = sizeof(IUnknown*);
            if (!pData || *pDataSize < requiredSize) {
                *pDataSize = requiredSize;
                return pData ? DXGI_ERROR_MORE_DATA : S_OK;
            }
            auto** out = static_cast<IUnknown**>(pData);
            *out = iface->second;
            if (*out)
                (*out)->AddRef();
            *pDataSize = requiredSize;
            return S_OK;
        }

        auto blob = m_blobs.find(guid);
        if (blob == m_blobs.end())
            return DXGI_ERROR_NOT_FOUND;

        const UINT requiredSize = static_cast<UINT>(blob->second.size());
        if (!pData || *pDataSize < requiredSize) {
            *pDataSize = requiredSize;
            return pData ? DXGI_ERROR_MORE_DATA : S_OK;
        }
        if (requiredSize > 0)
            memcpy(pData, blob->second.data(), requiredSize);
        *pDataSize = requiredSize;
        return S_OK;
    }

    HRESULT SetPrivateData(const GUID& guid, UINT dataSize, const void* pData) {
        if (dataSize > 0 && !pData)
            return E_INVALIDARG;
        std::lock_guard<std::mutex> lock(m_mutex);
        eraseInterface(guid);
        if (dataSize == 0) {
            m_blobs.erase(guid);
            return S_OK;
        }
        const auto* bytes = static_cast<const uint8_t*>(pData);
        m_blobs[guid] = std::vector<uint8_t>(bytes, bytes + dataSize);
        return S_OK;
    }

    HRESULT SetPrivateDataInterface(const GUID& guid, const IUnknown* pData) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_blobs.erase(guid);
        eraseInterface(guid);
        if (!pData)
            return S_OK;
        auto* iface = const_cast<IUnknown*>(pData);
        iface->AddRef();
        m_interfaces[guid] = iface;
        return S_OK;
    }

    HRESULT SetName(const char* name) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_name = name ? name : "";
        return S_OK;
    }

  private:
    void eraseInterface(const GUID& guid) {
        auto it = m_interfaces.find(guid);
        if (it == m_interfaces.end())
            return;
        if (it->second)
            it->second->Release();
        m_interfaces.erase(it);
    }

    void clearInterfaces() {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& entry : m_interfaces) {
            if (entry.second)
                entry.second->Release();
        }
        m_interfaces.clear();
    }

    std::mutex m_mutex;
    std::unordered_map<GUID, std::vector<uint8_t>, GUIDHasher> m_blobs;
    std::unordered_map<GUID, IUnknown*, GUIDHasher> m_interfaces;
    std::string m_name;
};

#define METALSHARP_D3D12_PRIVATE_DATA_METHODS()                                                                        \
    D3D12PrivateDataStore m_privateData;                                                                               \
    STDMETHOD(GetPrivateData)(const GUID& guid, UINT* pDataSize, void* pData) override {                               \
        return m_privateData.GetPrivateData(guid, pDataSize, pData);                                                   \
    }                                                                                                                  \
    STDMETHOD(SetPrivateData)(const GUID& guid, UINT dataSize, const void* pData) override {                           \
        return m_privateData.SetPrivateData(guid, dataSize, pData);                                                    \
    }                                                                                                                  \
    STDMETHOD(SetPrivateDataInterface)(const GUID& guid, const IUnknown* pData) override {                             \
        return m_privateData.SetPrivateDataInterface(guid, pData);                                                     \
    }                                                                                                                  \
    STDMETHOD(SetName)(const char* name) override {                                                                    \
        return m_privateData.SetName(name);                                                                            \
    }

static bool d3d12FormatHasMetalBacking(UINT format) {
    if (format == ::DXGI_FORMAT_UNKNOWN)
        return false;
    DXGITranslation dxgiFormat = static_cast<DXGITranslation>(format);
    return dxgiFormatToMetal(dxgiFormat) != 0 || dxgiDepthFormatToMetal(dxgiFormat) != 0;
}

static bool d3d12FormatIsRenderTargetCompatible(UINT format) {
    switch (format) {
    case ::DXGI_FORMAT_R32G32B32A32_FLOAT:
    case ::DXGI_FORMAT_R32G32B32A32_UINT:
    case ::DXGI_FORMAT_R32G32B32A32_SINT:
    case ::DXGI_FORMAT_R16G16B16A16_FLOAT:
    case ::DXGI_FORMAT_R16G16B16A16_UNORM:
    case ::DXGI_FORMAT_R16G16B16A16_UINT:
    case ::DXGI_FORMAT_R16G16B16A16_SNORM:
    case ::DXGI_FORMAT_R16G16B16A16_SINT:
    case ::DXGI_FORMAT_R32G32_FLOAT:
    case ::DXGI_FORMAT_R32G32_UINT:
    case ::DXGI_FORMAT_R32G32_SINT:
    case ::DXGI_FORMAT_R10G10B10A2_UNORM:
    case ::DXGI_FORMAT_R10G10B10A2_UINT:
    case ::DXGI_FORMAT_R11G11B10_FLOAT:
    case ::DXGI_FORMAT_R8G8B8A8_UNORM:
    case ::DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case ::DXGI_FORMAT_R8G8B8A8_UINT:
    case ::DXGI_FORMAT_R8G8B8A8_SNORM:
    case ::DXGI_FORMAT_R8G8B8A8_SINT:
    case ::DXGI_FORMAT_R16G16_FLOAT:
    case ::DXGI_FORMAT_R16G16_UNORM:
    case ::DXGI_FORMAT_R16G16_UINT:
    case ::DXGI_FORMAT_R16G16_SNORM:
    case ::DXGI_FORMAT_R16G16_SINT:
    case ::DXGI_FORMAT_R32_FLOAT:
    case ::DXGI_FORMAT_R32_UINT:
    case ::DXGI_FORMAT_R32_SINT:
    case ::DXGI_FORMAT_R16_FLOAT:
    case ::DXGI_FORMAT_R16_UNORM:
    case ::DXGI_FORMAT_R16_UINT:
    case ::DXGI_FORMAT_R16_SNORM:
    case ::DXGI_FORMAT_R16_SINT:
    case ::DXGI_FORMAT_R8_UNORM:
    case ::DXGI_FORMAT_R8_UINT:
    case ::DXGI_FORMAT_R8_SNORM:
    case ::DXGI_FORMAT_R8_SINT:
    case ::DXGI_FORMAT_A8_UNORM:
    case ::DXGI_FORMAT_B8G8R8A8_UNORM:
    case ::DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case ::DXGI_FORMAT_B8G8R8X8_UNORM:
    case ::DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case ::DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
        return true;
    default:
        return false;
    }
}

static bool d3d12FormatSupportsTypedUAV(UINT format) {
    switch (format) {
    case ::DXGI_FORMAT_R32_FLOAT:
    case ::DXGI_FORMAT_R32_UINT:
    case ::DXGI_FORMAT_R32_SINT:
    case ::DXGI_FORMAT_R32G32_FLOAT:
    case ::DXGI_FORMAT_R32G32_UINT:
    case ::DXGI_FORMAT_R32G32_SINT:
    case ::DXGI_FORMAT_R32G32B32A32_FLOAT:
    case ::DXGI_FORMAT_R32G32B32A32_UINT:
    case ::DXGI_FORMAT_R32G32B32A32_SINT:
    case ::DXGI_FORMAT_R16G16B16A16_FLOAT:
    case ::DXGI_FORMAT_R16G16B16A16_UINT:
    case ::DXGI_FORMAT_R16G16B16A16_SINT:
    case ::DXGI_FORMAT_R8G8B8A8_UNORM:
    case ::DXGI_FORMAT_R8G8B8A8_UINT:
    case ::DXGI_FORMAT_R8G8B8A8_SINT:
    case ::DXGI_FORMAT_R16_FLOAT:
    case ::DXGI_FORMAT_R16_UINT:
    case ::DXGI_FORMAT_R16_SINT:
    case ::DXGI_FORMAT_R8_UNORM:
    case ::DXGI_FORMAT_R8_UINT:
    case ::DXGI_FORMAT_R8_SINT:
        return true;
    default:
        return false;
    }
}

static UINT64 d3d12AlignUp(UINT64 value, UINT64 alignment) {
    if (alignment == 0)
        return value;
    return (value + alignment - 1) & ~(alignment - 1);
}

static UINT d3d12FormatBitsPerBlock(UINT format) {
    switch (format) {
    case ::DXGI_FORMAT_R32G32B32A32_TYPELESS:
    case ::DXGI_FORMAT_R32G32B32A32_FLOAT:
    case ::DXGI_FORMAT_R32G32B32A32_UINT:
    case ::DXGI_FORMAT_R32G32B32A32_SINT:
        return 128;
    case ::DXGI_FORMAT_R32G32B32_TYPELESS:
    case ::DXGI_FORMAT_R32G32B32_FLOAT:
    case ::DXGI_FORMAT_R32G32B32_UINT:
    case ::DXGI_FORMAT_R32G32B32_SINT:
        return 96;
    case ::DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case ::DXGI_FORMAT_R16G16B16A16_FLOAT:
    case ::DXGI_FORMAT_R16G16B16A16_UNORM:
    case ::DXGI_FORMAT_R16G16B16A16_UINT:
    case ::DXGI_FORMAT_R16G16B16A16_SNORM:
    case ::DXGI_FORMAT_R16G16B16A16_SINT:
    case ::DXGI_FORMAT_R32G32_TYPELESS:
    case ::DXGI_FORMAT_R32G32_FLOAT:
    case ::DXGI_FORMAT_R32G32_UINT:
    case ::DXGI_FORMAT_R32G32_SINT:
        return 64;
    case ::DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case ::DXGI_FORMAT_R10G10B10A2_UNORM:
    case ::DXGI_FORMAT_R10G10B10A2_UINT:
    case ::DXGI_FORMAT_R11G11B10_FLOAT:
    case ::DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case ::DXGI_FORMAT_R8G8B8A8_UNORM:
    case ::DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case ::DXGI_FORMAT_R8G8B8A8_UINT:
    case ::DXGI_FORMAT_R8G8B8A8_SNORM:
    case ::DXGI_FORMAT_R8G8B8A8_SINT:
    case ::DXGI_FORMAT_R16G16_TYPELESS:
    case ::DXGI_FORMAT_R16G16_FLOAT:
    case ::DXGI_FORMAT_R16G16_UNORM:
    case ::DXGI_FORMAT_R16G16_UINT:
    case ::DXGI_FORMAT_R16G16_SNORM:
    case ::DXGI_FORMAT_R16G16_SINT:
    case ::DXGI_FORMAT_R32_TYPELESS:
    case ::DXGI_FORMAT_D32_FLOAT:
    case ::DXGI_FORMAT_R32_FLOAT:
    case ::DXGI_FORMAT_R32_UINT:
    case ::DXGI_FORMAT_R32_SINT:
    case ::DXGI_FORMAT_B8G8R8A8_UNORM:
    case ::DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case ::DXGI_FORMAT_B8G8R8X8_UNORM:
    case ::DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case ::DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
        return 32;
    case ::DXGI_FORMAT_R8G8_TYPELESS:
    case ::DXGI_FORMAT_R8G8_UNORM:
    case ::DXGI_FORMAT_R8G8_UINT:
    case ::DXGI_FORMAT_R8G8_SNORM:
    case ::DXGI_FORMAT_R8G8_SINT:
    case ::DXGI_FORMAT_R16_TYPELESS:
    case ::DXGI_FORMAT_R16_FLOAT:
    case ::DXGI_FORMAT_D16_UNORM:
    case ::DXGI_FORMAT_R16_UNORM:
    case ::DXGI_FORMAT_R16_UINT:
    case ::DXGI_FORMAT_R16_SNORM:
    case ::DXGI_FORMAT_R16_SINT:
        return 16;
    case ::DXGI_FORMAT_R8_TYPELESS:
    case ::DXGI_FORMAT_R8_UNORM:
    case ::DXGI_FORMAT_R8_UINT:
    case ::DXGI_FORMAT_R8_SNORM:
    case ::DXGI_FORMAT_R8_SINT:
    case ::DXGI_FORMAT_A8_UNORM:
        return 8;
    case ::DXGI_FORMAT_BC1_UNORM:
    case ::DXGI_FORMAT_BC1_UNORM_SRGB:
    case ::DXGI_FORMAT_BC4_UNORM:
    case ::DXGI_FORMAT_BC4_SNORM:
        return 64;
    case ::DXGI_FORMAT_BC2_UNORM:
    case ::DXGI_FORMAT_BC2_UNORM_SRGB:
    case ::DXGI_FORMAT_BC3_UNORM:
    case ::DXGI_FORMAT_BC3_UNORM_SRGB:
    case ::DXGI_FORMAT_BC5_UNORM:
    case ::DXGI_FORMAT_BC5_SNORM:
    case ::DXGI_FORMAT_BC6H_UF16:
    case ::DXGI_FORMAT_BC6H_SF16:
    case ::DXGI_FORMAT_BC7_UNORM:
    case ::DXGI_FORMAT_BC7_UNORM_SRGB:
        return 128;
    default:
        return 32;
    }
}

static bool d3d12FormatIsBlockCompressed(UINT format) {
    return (format >= ::DXGI_FORMAT_BC1_UNORM && format <= ::DXGI_FORMAT_BC5_SNORM) ||
           format == ::DXGI_FORMAT_BC6H_UF16 || format == ::DXGI_FORMAT_BC6H_SF16 ||
           format == ::DXGI_FORMAT_BC7_UNORM || format == ::DXGI_FORMAT_BC7_UNORM_SRGB;
}

static UINT64 d3d12EstimateSubresourceSize(UINT64 width, UINT height, UINT depth, UINT format) {
    UINT blockWidth = d3d12FormatIsBlockCompressed(format) ? 4 : 1;
    UINT blockHeight = d3d12FormatIsBlockCompressed(format) ? 4 : 1;
    UINT64 blocksWide = std::max<UINT64>(1, (width + blockWidth - 1) / blockWidth);
    UINT64 blocksHigh = std::max<UINT64>(1, (static_cast<UINT64>(height) + blockHeight - 1) / blockHeight);
    UINT64 bitsPerBlock = d3d12FormatBitsPerBlock(format);
    return d3d12AlignUp(blocksWide * blocksHigh * std::max<UINT>(1, depth) * bitsPerBlock, 8) / 8;
}

static UINT64 d3d12EstimateResourceSize(const D3D12_RESOURCE_DESC& desc) {
    if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        return d3d12AlignUp(desc.Width, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

    UINT mipLevels = desc.MipLevels > 0 ? desc.MipLevels : 1;
    UINT arrayOrDepth = std::max<UINT>(1, desc.DepthOrArraySize);
    UINT64 totalSize = 0;
    for (UINT mip = 0; mip < mipLevels; ++mip) {
        UINT64 width = std::max<UINT64>(1, desc.Width >> mip);
        UINT height = std::max<UINT>(1, desc.Height >> mip);
        UINT depth = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? std::max<UINT>(1, arrayOrDepth >> mip)
                                                                          : arrayOrDepth;
        totalSize += d3d12EstimateSubresourceSize(width, height, depth, desc.Format);
    }
    UINT sampleCount = desc.SampleDesc.Count > 0 ? desc.SampleDesc.Count : 1;
    totalSize *= sampleCount;
    UINT64 alignment = desc.Alignment != 0 ? desc.Alignment
                                           : (sampleCount > 1 ? D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT
                                                              : D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
    return d3d12AlignUp(totalSize, alignment);
}

class D3D12FenceImpl final : public ID3D12Fence {
  public:
    std::atomic<ULONG> refCount{1};
    UINT64 m_value = 0;
    UINT64 m_completed = 0;
    std::mutex m_mutex;
    std::condition_variable m_completionCond;
    std::vector<std::pair<UINT64, HANDLE>> m_completionEvents;

    HRESULT QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv)
            return E_POINTER;
        if (riid == IID_ID3D12Fence || riid == IID_IUnknown) {
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

    HRESULT GetCompletedValue(UINT64* pValue) override {
        if (!pValue)
            return E_POINTER;
        std::lock_guard<std::mutex> lock(m_mutex);
        *pValue = m_completed;
        return S_OK;
    }
    HRESULT SetEventOnCompletion(UINT64 Value, HANDLE hEvent) override {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_completed >= Value) {
            lock.unlock();
            if (hEvent)
                win32::SyncContext::instance().setEvent(hEvent);
            return S_OK;
        }

        if (!hEvent) {
            m_completionCond.wait(lock, [&] { return m_completed >= Value; });
            return S_OK;
        }

        m_completionEvents.push_back({Value, hEvent});
        return S_OK;
    }
    HRESULT Signal(UINT64 Value) override {
        std::vector<HANDLE> eventsToSignal;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_value = Value;
            m_completed = Value;
            auto it = m_completionEvents.begin();
            while (it != m_completionEvents.end()) {
                if (m_completed >= it->first) {
                    eventsToSignal.push_back(it->second);
                    it = m_completionEvents.erase(it);
                } else {
                    ++it;
                }
            }
        }
        m_completionCond.notify_all();
        for (HANDLE eventHandle : eventsToSignal) {
            win32::SyncContext::instance().setEvent(eventHandle);
        }
        return S_OK;
    }

    HRESULT WaitForValue(UINT64 Value) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_completionCond.wait(lock, [&] { return m_completed >= Value; });
        return S_OK;
    }
};

class D3D12CommandAllocatorImpl final : public ID3D12CommandAllocator {
  public:
    std::atomic<ULONG> refCount{1};

    HRESULT QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv)
            return E_POINTER;
        if (riid == IID_ID3D12CommandAllocator || riid == IID_IUnknown) {
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
    HRESULT Reset() override { return S_OK; }
};

class D3D12CommandQueueImpl final : public ID3D12CommandQueue {
  public:
    std::atomic<ULONG> refCount{1};
    MetalDevice& metalDevice;
    struct QueueWorkItem {
        std::function<void()> operation;
        ID3D12Fence* signalFence = nullptr;
        UINT64 signalValue = 0;
    };
    struct QueueState {
        std::mutex mutex;
        std::condition_variable cond;
        std::deque<QueueWorkItem> work;
        std::thread worker;
        bool workerStarted = false;
        bool stopping = false;
        bool busy = false;
    };
    std::shared_ptr<QueueState> m_queueState;

    explicit D3D12CommandQueueImpl(MetalDevice& dev) : metalDevice(dev), m_queueState(std::make_shared<QueueState>()) {}
    ~D3D12CommandQueueImpl();

    HRESULT QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv)
            return E_POINTER;
        if (riid == IID_ID3D12CommandQueue || riid == IID_IUnknown) {
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

    HRESULT ExecuteCommandLists(UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists) override;
    HRESULT Signal(ID3D12Fence* pFence, UINT64 Value) override;
    HRESULT Wait(ID3D12Fence* pFence, UINT64 Value) override;

    bool hasQueuedWork() {
        std::lock_guard<std::mutex> lock(m_queueState->mutex);
        return m_queueState->busy || !m_queueState->work.empty();
    }

    HRESULT enqueueQueueWork(std::function<void()> operation, ID3D12Fence* signalFence = nullptr,
                             UINT64 signalValue = 0);
    static void runQueueWorker(std::shared_ptr<QueueState> state);
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
    uint64_t generation = 0;
};

class D3D12DescriptorHeapImpl final : public ID3D12DescriptorHeap {
  public:
    std::atomic<ULONG> refCount{1};
    D3D12_DESCRIPTOR_HEAP_DESC desc;
    std::vector<D3D12Descriptor> descriptors;
    void* metalArgumentBuffer = nullptr;
    UINT dirtyStart = UINT_MAX;
    UINT dirtyEnd = 0;
    uint64_t generation = 0;
    UINT64 cpuHandleBase = 0;
    UINT64 gpuHandleBase = 0;

    D3D12DescriptorHeapImpl(const D3D12_DESCRIPTOR_HEAP_DESC* d) : desc(*d), descriptors(d->NumDescriptors) {
        cpuHandleBase = reinterpret_cast<UINT64>(this) + 0x1000;
        gpuHandleBase = reinterpret_cast<UINT64>(this) + 0x2000;
    }

    HRESULT QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv)
            return E_POINTER;
        if (riid == IID_ID3D12DescriptorHeap || riid == IID_IUnknown) {
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

    D3D12_CPU_DESCRIPTOR_HANDLE __getCPUDescriptorHandleForHeapStart() override { return {cpuHandleBase}; }
    D3D12_GPU_DESCRIPTOR_HANDLE __getGPUDescriptorHandleForHeapStart() override { return {gpuHandleBase}; }
    UINT __getDescriptorCount() const override { return desc.NumDescriptors; }
    UINT __getHeapType() const override { return desc.Type; }

    D3D12Descriptor* getDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle) {
        if (handle.ptr < cpuHandleBase)
            return nullptr;
        UINT64 offset = handle.ptr - cpuHandleBase;
        if (offset >= desc.NumDescriptors)
            return nullptr;
        return &descriptors[offset];
    }

    D3D12Descriptor* getDescriptorByIndex(UINT index) {
        if (index >= desc.NumDescriptors)
            return nullptr;
        return &descriptors[index];
    }

    void markDirty(UINT index, UINT count = 1) {
        if (index >= desc.NumDescriptors || count == 0)
            return;
        UINT end = std::min(desc.NumDescriptors, index + count);
        dirtyStart = std::min(dirtyStart, index);
        dirtyEnd = std::max(dirtyEnd, end);
        generation++;
        for (UINT i = index; i < end; ++i)
            descriptors[i].generation = generation;
    }

    bool hasDirtyRange() const { return dirtyStart != UINT_MAX && dirtyStart < dirtyEnd; }
    void clearDirtyRange() {
        dirtyStart = UINT_MAX;
        dirtyEnd = 0;
    }

    UINT handleToIndex(D3D12_CPU_DESCRIPTOR_HANDLE handle) const {
        if (handle.ptr < cpuHandleBase)
            return UINT_MAX;
        UINT64 offset = handle.ptr - cpuHandleBase;
        if (offset >= desc.NumDescriptors)
            return UINT_MAX;
        return static_cast<UINT>(offset);
    }

    UINT gpuHandleToIndex(D3D12_GPU_DESCRIPTOR_HANDLE handle) const {
        if (handle.ptr < gpuHandleBase)
            return UINT_MAX;
        UINT64 offset = handle.ptr - gpuHandleBase;
        if (offset >= desc.NumDescriptors)
            return UINT_MAX;
        return static_cast<UINT>(offset);
    }

    void copyDescriptors(UINT numDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE dstStart,
                         D3D12_CPU_DESCRIPTOR_HANDLE srcStart) {
        UINT dstIdx = handleToIndex(dstStart);
        UINT srcIdx = handleToIndex(srcStart);
        if (dstIdx == UINT_MAX || srcIdx == UINT_MAX)
            return;
        UINT copyCount = std::min(numDescriptors, desc.NumDescriptors - dstIdx);
        copyCount = std::min(copyCount, desc.NumDescriptors - srcIdx);
        if (copyCount == 0)
            return;

        std::vector<D3D12Descriptor> snapshot;
        snapshot.reserve(copyCount);
        for (UINT i = 0; i < copyCount; ++i)
            snapshot.push_back(descriptors[srcIdx + i]);

        for (UINT i = 0; i < copyCount; ++i)
            descriptors[dstIdx + i] = snapshot[i];
        markDirty(dstIdx, copyCount);
    }
};

class D3D12RootSignatureImpl final : public ID3D12RootSignature {
  public:
    std::atomic<ULONG> refCount{1};
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
        if (!ppv)
            return E_POINTER;
        if (riid == IID_ID3D12RootSignature || riid == IID_IUnknown) {
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
                if (layout.size < 4)
                    layout.size = 4;
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
    std::atomic<ULONG> refCount{1};
    void* m_renderPipeline = nullptr;
    void* m_computePipeline = nullptr;
    void* m_ownedRenderPipeline = nullptr;
    void* m_ownedComputePipeline = nullptr;

    HRESULT QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv)
            return E_POINTER;
        if (riid == IID_ID3D12PipelineState || riid == IID_IUnknown) {
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
    void* __metalRenderPipelineState() const override { return m_renderPipeline; }
    void* __metalComputePipelineState() const override { return m_computePipeline; }
};

class D3D12CommandSignatureImpl final : public ID3D12CommandSignature {
  public:
    std::atomic<ULONG> refCount{1};
    HRESULT QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv)
            return E_POINTER;
        if (riid == IID_ID3D12CommandSignature || riid == IID_IUnknown) {
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
};

class D3D12StateObjectImpl final : public ID3D12StateObject {
  public:
    std::atomic<ULONG> refCount{1};
    void* m_rtPipeline = nullptr;
    std::vector<uint8_t> m_shaderIdentifierData;

    HRESULT QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv)
            return E_POINTER;
        if (riid == IID_IUnknown) {
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
    void* __metalRTPipeline() const override { return m_rtPipeline; }
};

class D3D12ResourceImpl final : public ID3D12Resource {
  public:
    std::atomic<ULONG> refCount{1};
    D3D12_RESOURCE_DESC desc;
    std::unique_ptr<MetalBuffer> metalBuffer;
    std::unique_ptr<MetalTexture> metalTexture;
    UINT m_resourceState;
    UINT64 m_gpuAddress = 0;
    std::weak_ptr<GPUAddressRegistry> m_gpuAddressRegistry;

    D3D12ResourceImpl(const D3D12_RESOURCE_DESC& d) : desc(d), m_resourceState(D3D12_RESOURCE_STATE_COMMON) {}
    ~D3D12ResourceImpl() {
        if (m_gpuAddress != 0) {
            auto registry = m_gpuAddressRegistry.lock();
            if (registry) {
                auto it = registry->find(m_gpuAddress);
                if (it != registry->end() && it->second == this)
                    registry->erase(it);
            }
        }
    }

    HRESULT QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv)
            return E_POINTER;
        if (riid == IID_ID3D12Resource || riid == IID_IUnknown) {
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

    HRESULT Map(UINT, const D3D12_RANGE*, void** ppData) override {
        if (!ppData)
            return E_POINTER;
        if (metalBuffer) {
            *ppData = metalBuffer->contents();
            return S_OK;
        }
        return E_FAIL;
    }
    HRESULT Unmap(UINT, const D3D12_RANGE*) override { return S_OK; }
    HRESULT GetDesc(D3D12_RESOURCE_DESC* pDesc) override {
        if (!pDesc)
            return E_POINTER;
        *pDesc = desc;
        return S_OK;
    }
    HRESULT GetGPUVirtualAddress(D3D12_GPU_VIRTUAL_ADDRESS* pAddress) override {
        if (!pAddress)
            return E_POINTER;
        *pAddress = m_gpuAddress;
        return S_OK;
    }
    void* __metalBufferPtr() const override { return metalBuffer ? metalBuffer->nativeBuffer() : nullptr; }
    void* __metalTexturePtr() const override { return metalTexture ? metalTexture->nativeTexture() : nullptr; }
    UINT __getResourceState() const override { return m_resourceState; }
    void __setResourceState(UINT state) override { m_resourceState = state; }
    size_t bufferSize() const { return metalBuffer ? metalBuffer->size() : 0; }
};

class D3D12DeviceImpl final : public ID3D12Device {
  public:
    static HRESULT create(D3D12DeviceImpl** ppDevice) {
        if (!ppDevice)
            return E_POINTER;
        auto* device = new D3D12DeviceImpl();
        device->m_metalDevice = std::unique_ptr<MetalDevice>(MetalDevice::create());
        if (!device->m_metalDevice) {
            delete device;
            return E_FAIL;
        }
        device->m_metalCapabilities = MetalCapabilityDetector::detect(device->m_metalDevice->nativeDevice());
        MetalCapabilityDetector::log(device->m_metalCapabilities, "d3d12_device_create");
        device->m_shaderTranslator = std::make_unique<ShaderTranslator>();
        *ppDevice = device;
        return S_OK;
    }

    MetalDevice& metalDevice() { return *m_metalDevice; }
    const MetalCapabilities& metalCapabilities() const { return m_metalCapabilities; }
    D3D12ResourceStateTracker& resourceStateTracker() { return m_resourceStateTracker; }

    std::atomic<ULONG> m_refCount{1};
    std::unique_ptr<MetalDevice> m_metalDevice;
    std::unique_ptr<ShaderTranslator> m_shaderTranslator;
    MetalCapabilities m_metalCapabilities;
    D3D12ResourceStateTracker m_resourceStateTracker;
    size_t gpuAddressResourceCountForTesting() const {
        return m_gpuAddressResources ? m_gpuAddressResources->size() : 0;
    }

    HRESULT QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv)
            return E_POINTER;
        if (riid == IID_ID3D12Device || riid == IID_IUnknown) {
            AddRef();
            *ppv = this;
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG AddRef() override { return ++m_refCount; }
    ULONG Release() override {
        ULONG c = --m_refCount;
        if (c == 0)
            delete this;
        return c;
    }
    METALSHARP_D3D12_PRIVATE_DATA_METHODS()

    HRESULT CreateCommandQueue(const void* pDesc, REFIID riid, void** ppCommandQueue) override {
        if (!ppCommandQueue)
            return E_POINTER;
        *ppCommandQueue = new D3D12CommandQueueImpl(*m_metalDevice);
        return S_OK;
    }

    HRESULT CreateCommandAllocator(UINT type, REFIID riid, void** ppAllocator) override {
        if (!ppAllocator)
            return E_POINTER;
        *ppAllocator = new D3D12CommandAllocatorImpl();
        return S_OK;
    }

    HRESULT CreateCommandList(UINT nodeMask, UINT type, ID3D12CommandAllocator* pAllocator,
                              ID3D12PipelineState* pInitialState, REFIID riid, void** ppCommandList) override;

    HRESULT CheckFeatureSupport(UINT feature, void* pFeatureSupportData, UINT featureSupportDataSize) override {
        if (!pFeatureSupportData)
            return E_INVALIDARG;

        switch (feature) {
        case D3D12_FEATURE_FEATURE_LEVELS: {
            if (featureSupportDataSize < sizeof(D3D12_FEATURE_DATA_FEATURE_LEVELS))
                return E_INVALIDARG;
            auto* data = static_cast<D3D12_FEATURE_DATA_FEATURE_LEVELS*>(pFeatureSupportData);
            UINT maxRequested = D3D_FEATURE_LEVEL_11_0;
            for (UINT i = 0; i < data->NumFeatureLevels && data->pFeatureLevelsRequested; ++i) {
                maxRequested = std::max(maxRequested, data->pFeatureLevelsRequested[i]);
            }
            data->MaxSupportedFeatureLevel = std::min(maxRequested, D3D_FEATURE_LEVEL_12_0);
            return S_OK;
        }
        case D3D12_FEATURE_D3D12_OPTIONS: {
            if (featureSupportDataSize < sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS))
                return E_INVALIDARG;
            auto* data = static_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS*>(pFeatureSupportData);
            std::memset(data, 0, sizeof(*data));
            data->OutputMergerLogicOp = TRUE;
            data->TiledResourcesTier = D3D12_TILED_RESOURCES_TIER_2;
            data->ResourceBindingTier = m_metalCapabilities.supportsArgumentBuffers ? D3D12_RESOURCE_BINDING_TIER_3
                                                                                    : D3D12_RESOURCE_BINDING_TIER_2;
            data->TypedUAVLoadAdditionalFormats = TRUE;
            data->MaxGPUVirtualAddressBitsPerResource = 40;
            data->StandardSwizzle64KBSupported = TRUE;
            data->ResourceHeapTier = D3D12_RESOURCE_HEAP_TIER_2;
            return S_OK;
        }
        case D3D12_FEATURE_ARCHITECTURE: {
            if (featureSupportDataSize < sizeof(D3D12_FEATURE_DATA_ARCHITECTURE))
                return E_INVALIDARG;
            auto* data = static_cast<D3D12_FEATURE_DATA_ARCHITECTURE*>(pFeatureSupportData);
            data->TileBasedRenderer = TRUE;
            data->UMA = TRUE;
            data->CacheCoherentUMA = TRUE;
            return S_OK;
        }
        case D3D12_FEATURE_ARCHITECTURE1: {
            if (featureSupportDataSize < sizeof(D3D12_FEATURE_DATA_ARCHITECTURE1))
                return E_INVALIDARG;
            auto* data = static_cast<D3D12_FEATURE_DATA_ARCHITECTURE1*>(pFeatureSupportData);
            data->TileBasedRenderer = TRUE;
            data->UMA = TRUE;
            data->CacheCoherentUMA = TRUE;
            data->IsolatedMMU = FALSE;
            return S_OK;
        }
        case D3D12_FEATURE_FORMAT_SUPPORT: {
            if (featureSupportDataSize < sizeof(D3D12_FEATURE_DATA_FORMAT_SUPPORT))
                return E_INVALIDARG;
            auto* data = static_cast<D3D12_FEATURE_DATA_FORMAT_SUPPORT*>(pFeatureSupportData);
            data->Support1 = 0;
            data->Support2 = 0;
            DXGITranslation dxgiFormat = static_cast<DXGITranslation>(data->Format);
            if (!d3d12FormatHasMetalBacking(data->Format))
                return S_OK;

            data->Support1 = D3D12_FORMAT_SUPPORT1_TEXTURE1D | D3D12_FORMAT_SUPPORT1_TEXTURE2D |
                             D3D12_FORMAT_SUPPORT1_TEXTURECUBE | D3D12_FORMAT_SUPPORT1_SHADER_LOAD |
                             D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE;
            if (!dxgiFormatIsDepth(dxgiFormat) && !dxgiFormatIsCompressed(dxgiFormat))
                data->Support1 |= D3D12_FORMAT_SUPPORT1_BUFFER | D3D12_FORMAT_SUPPORT1_IA_VERTEX_BUFFER;
            if (!dxgiFormatIsDepth(dxgiFormat))
                data->Support1 |= D3D12_FORMAT_SUPPORT1_SHADER_GATHER;
            if (data->Format == ::DXGI_FORMAT_R16_UINT || data->Format == ::DXGI_FORMAT_R32_UINT)
                data->Support1 |= D3D12_FORMAT_SUPPORT1_IA_INDEX_BUFFER;
            if (d3d12FormatIsRenderTargetCompatible(data->Format))
                data->Support1 |= D3D12_FORMAT_SUPPORT1_RENDER_TARGET;
            if (dxgiFormatIsDepth(dxgiFormat))
                data->Support1 |= D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL;
            if (d3d12FormatSupportsTypedUAV(data->Format))
                data->Support2 = D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD | D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
            return S_OK;
        }
        case D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS: {
            if (featureSupportDataSize < sizeof(D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS))
                return E_INVALIDARG;
            auto* data = static_cast<D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS*>(pFeatureSupportData);
            data->NumQualityLevels =
                (data->SampleCount == 1 || data->SampleCount == 2 || data->SampleCount == 4) ? 1 : 0;
            return S_OK;
        }
        case D3D12_FEATURE_FORMAT_INFO: {
            if (featureSupportDataSize < sizeof(D3D12_FEATURE_DATA_FORMAT_INFO))
                return E_INVALIDARG;
            auto* data = static_cast<D3D12_FEATURE_DATA_FORMAT_INFO*>(pFeatureSupportData);
            data->PlaneCount = 1;
            return S_OK;
        }
        case D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT: {
            if (featureSupportDataSize < sizeof(D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT))
                return E_INVALIDARG;
            auto* data = static_cast<D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT*>(pFeatureSupportData);
            data->MaxGPUVirtualAddressBitsPerResource = 40;
            data->MaxGPUVirtualAddressBitsPerProcess = 40;
            return S_OK;
        }
        case D3D12_FEATURE_SHADER_MODEL: {
            if (featureSupportDataSize < sizeof(D3D12_FEATURE_DATA_SHADER_MODEL))
                return E_INVALIDARG;
            auto* data = static_cast<D3D12_FEATURE_DATA_SHADER_MODEL*>(pFeatureSupportData);
            if (data->HighestShaderModel == 0 || data->HighestShaderModel > D3D_SHADER_MODEL_6_0)
                data->HighestShaderModel = D3D_SHADER_MODEL_6_0;
            return S_OK;
        }
        case D3D12_FEATURE_ROOT_SIGNATURE: {
            if (featureSupportDataSize < sizeof(D3D12_FEATURE_DATA_ROOT_SIGNATURE))
                return E_INVALIDARG;
            auto* data = static_cast<D3D12_FEATURE_DATA_ROOT_SIGNATURE*>(pFeatureSupportData);
            if (data->HighestVersion == 0 || data->HighestVersion > D3D_ROOT_SIGNATURE_VERSION_1_1)
                data->HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
            return S_OK;
        }
        case D3D12_FEATURE_D3D12_OPTIONS1: {
            if (featureSupportDataSize < sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS1))
                return E_INVALIDARG;
            auto* data = static_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS1*>(pFeatureSupportData);
            std::memset(data, 0, sizeof(*data));
            data->ExpandedComputeResourceStates = TRUE;
            return S_OK;
        }
        case D3D12_FEATURE_D3D12_OPTIONS5: {
            if (featureSupportDataSize < sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS5))
                return E_INVALIDARG;
            auto* data = static_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS5*>(pFeatureSupportData);
            std::memset(data, 0, sizeof(*data));
            data->RaytracingTier = m_metalCapabilities.supportsRayTracing ? D3D12_RAYTRACING_TIER_1_1
                                                                          : D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
            return S_OK;
        }
        case D3D12_FEATURE_D3D12_OPTIONS7: {
            if (featureSupportDataSize < sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS7))
                return E_INVALIDARG;
            auto* data = static_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS7*>(pFeatureSupportData);
            data->MeshShaderTier = m_metalCapabilities.supportsMeshShaders ? D3D12_MESH_SHADER_TIER_1
                                                                           : D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
            data->SamplerFeedbackTier = 0;
            return S_OK;
        }
        default:
            return E_INVALIDARG;
        }
    }

    HRESULT CreateCommittedResource(const D3D12_HEAP_PROPERTIES* pHeap, UINT HeapFlags,
                                    const D3D12_RESOURCE_DESC* pDesc, UINT InitialState, const void* pClearVal,
                                    REFIID riid, void** ppvResource) override {
        if (!pDesc || !ppvResource)
            return E_INVALIDARG;

        auto* res = new D3D12ResourceImpl(*pDesc);
        res->m_resourceState = InitialState;
        m_resourceStateTracker.setInitialState(res, InitialState);

        if (pDesc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
            res->metalBuffer =
                std::unique_ptr<MetalBuffer>(MetalBuffer::create(*m_metalDevice, (size_t)pDesc->Width, nullptr));
            if (res->metalBuffer) {
                res->m_gpuAddress = reinterpret_cast<UINT64>(res->metalBuffer->nativeBuffer());
                res->m_gpuAddressRegistry = m_gpuAddressResources;
                (*m_gpuAddressResources)[res->m_gpuAddress] = res;
            }
        } else {
            DXGITranslation dxgiFormat = (DXGITranslation)pDesc->Format;
            bool isDepthStencil =
                (pDesc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) && dxgiFormatIsDepth(dxgiFormat);
            uint32_t fmt = isDepthStencil ? dxgiDepthFormatToMetal(dxgiFormat) : dxgiFormatToMetal(dxgiFormat);
            if (fmt == 0) {
                delete res;
                return E_INVALIDARG;
            }
            uint32_t usage = 0;
            if (pDesc->Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
                usage |= 0x1;
            if (pDesc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
                usage |= 0x1;
            if (pDesc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
                usage |= 0x4;
            if (!(pDesc->Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))
                usage |= 0x2 | 0x8;
            uint32_t mips = pDesc->MipLevels > 0 ? pDesc->MipLevels : 1;
            uint32_t samples = pDesc->SampleDesc.Count > 0 ? pDesc->SampleDesc.Count : 1;

            if (pDesc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
                res->metalTexture = std::unique_ptr<MetalTexture>(MetalTexture::create2D(
                    *m_metalDevice, (uint32_t)pDesc->Width, pDesc->Height, fmt, usage, mips, samples));
            } else if (pDesc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D) {
                res->metalTexture = std::unique_ptr<MetalTexture>(
                    MetalTexture::create1D(*m_metalDevice, (uint32_t)pDesc->Width, fmt, usage, mips));
            } else if (pDesc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
                res->metalTexture = std::unique_ptr<MetalTexture>(MetalTexture::create3D(
                    *m_metalDevice, (uint32_t)pDesc->Width, pDesc->Height, pDesc->DepthOrArraySize, fmt, usage, mips));
            }
        }

        *ppvResource = res;
        return S_OK;
    }

    D3D12_RESOURCE_ALLOCATION_INFO GetResourceAllocationInfo(UINT, UINT numResourceDescs,
                                                             const D3D12_RESOURCE_DESC* pResourceDescs) override {
        D3D12_RESOURCE_ALLOCATION_INFO info = {};
        if (numResourceDescs == 0 || !pResourceDescs)
            return info;

        UINT64 maxAlignment = 0;
        UINT64 offset = 0;
        for (UINT i = 0; i < numResourceDescs; ++i) {
            const D3D12_RESOURCE_DESC& desc = pResourceDescs[i];
            UINT sampleCount = desc.SampleDesc.Count > 0 ? desc.SampleDesc.Count : 1;
            UINT64 alignment = desc.Alignment != 0 ? desc.Alignment
                                                   : (sampleCount > 1 ? D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT
                                                                      : D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
            UINT64 resourceSize = d3d12EstimateResourceSize(desc);
            offset = d3d12AlignUp(offset, alignment);
            offset += resourceSize;
            maxAlignment = std::max(maxAlignment, alignment);
        }

        info.Alignment = maxAlignment;
        info.SizeInBytes = d3d12AlignUp(offset, maxAlignment);
        return info;
    }

    HRESULT CreateDescriptorHeap(const D3D12_DESCRIPTOR_HEAP_DESC* pDesc, REFIID riid, void** ppHeap) override {
        if (!pDesc || !ppHeap)
            return E_INVALIDARG;
        auto* heap = new D3D12DescriptorHeapImpl(pDesc);
        m_trackedHeaps.push_back(heap);
        *ppHeap = heap;
        return S_OK;
    }

    HRESULT CreateRootSignature(UINT, const void* pBlob, SIZE_T blobSize, REFIID riid, void** ppRS) override {
        if (!ppRS)
            return E_POINTER;
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
                    if (32 + i * 4 + 4 > size)
                        break;
                    memcpy(&offset, data + 32 + i * 4, 4);
                    if (offset + 8 > size)
                        continue;
                    uint32_t chunkMagic;
                    memcpy(&chunkMagic, data + offset, 4);
                    uint32_t chunkSize;
                    memcpy(&chunkSize, data + offset + 4, 4);
                    if (offset + 8 + chunkSize > size)
                        continue;

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
        if (size < 16)
            return;
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

    HRESULT CreateGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc, REFIID riid,
                                        void** ppPSO) override;

    HRESULT CreateComputePipelineState(const void* pDesc, REFIID riid, void** ppPSO) override;

    HRESULT CreateFence(UINT64 InitialValue, UINT Flags, REFIID riid, void** ppFence) override {
        if (!ppFence)
            return E_POINTER;
        auto* fence = new D3D12FenceImpl();
        fence->m_value = InitialValue;
        fence->m_completed = InitialValue;
        *ppFence = fence;
        return S_OK;
    }

    HRESULT CreateCommandSignature(const D3D12_COMMAND_SIGNATURE_DESC*, ID3D12RootSignature*, REFIID,
                                   void** ppSig) override {
        if (!ppSig)
            return E_POINTER;
        *ppSig = new D3D12CommandSignatureImpl();
        return S_OK;
    }

    HRESULT CreateRenderTargetView(ID3D12Resource* pResource, const void* pDesc,
                                   D3D12_CPU_DESCRIPTOR_HANDLE handle) override {
        return createView(pResource, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, DXGI_FORMAT_UNKNOWN, handle);
    }

    HRESULT CreateDepthStencilView(ID3D12Resource* pResource, const void* pDesc,
                                   D3D12_CPU_DESCRIPTOR_HANDLE handle) override {
        return createView(pResource, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, DXGI_FORMAT_UNKNOWN, handle);
    }

    HRESULT CreateShaderResourceView(ID3D12Resource* pResource, const void* pDesc,
                                     D3D12_CPU_DESCRIPTOR_HANDLE handle) override {
        return createView(pResource, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, DXGI_FORMAT_UNKNOWN, handle);
    }

    HRESULT CreateUnorderedAccessView(ID3D12Resource* pResource, ID3D12Resource*, const void* pDesc,
                                      D3D12_CPU_DESCRIPTOR_HANDLE handle) override {
        return createView(pResource, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, DXGI_FORMAT_UNKNOWN, handle);
    }

    HRESULT CreateConstantBufferView(const void* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE handle) override {
        if (!pDesc || handle.ptr == 0)
            return E_INVALIDARG;
        D3D12DescriptorHeapImpl* heap = nullptr;
        for (auto* h : m_trackedHeaps) {
            if (h->handleToIndex(handle) != UINT_MAX) {
                heap = h;
                break;
            }
        }
        if (!heap)
            return S_OK;
        auto* desc = heap->getDescriptor(handle);
        if (!desc)
            return S_OK;
        UINT index = heap->handleToIndex(handle);

        struct CBVDesc {
            D3D12_GPU_VIRTUAL_ADDRESS BufferLocation;
            UINT SizeInBytes;
        };
        auto* cbv = static_cast<const CBVDesc*>(pDesc);
        desc->gpuAddress = cbv->BufferLocation;
        desc->bufferSize = cbv->SizeInBytes;
        desc->type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap->markDirty(index);
        return S_OK;
    }

    HRESULT CreateSampler(const void* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE handle) override {
        if (!pDesc || handle.ptr == 0)
            return E_INVALIDARG;
        D3D12DescriptorHeapImpl* heap = nullptr;
        for (auto* h : m_trackedHeaps) {
            if (h->handleToIndex(handle) != UINT_MAX) {
                heap = h;
                break;
            }
        }
        if (!heap)
            return S_OK;
        auto* desc = heap->getDescriptor(handle);
        if (!desc)
            return S_OK;
        UINT index = heap->handleToIndex(handle);

        desc->type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        auto* samplerDesc = static_cast<const D3D12_STATIC_SAMPLER_DESC*>(pDesc);
        if (samplerDesc) {
            desc->shaderRegister = samplerDesc->ShaderRegister;
            desc->registerSpace = samplerDesc->RegisterSpace;
        }
        heap->markDirty(index);
        return S_OK;
    }

    UINT GetDescriptorHandleIncrementSize(UINT) override { return 1; }

    void CopyDescriptors(UINT numDestDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE* pDestDescriptorRangeStarts,
                         const UINT* pDestDescriptorRangeSizes, UINT numSrcDescriptorRanges,
                         const D3D12_CPU_DESCRIPTOR_HANDLE* pSrcDescriptorRangeStarts,
                         const UINT* pSrcDescriptorRangeSizes, UINT) override {
        if (!pDestDescriptorRangeStarts || !pSrcDescriptorRangeStarts || numDestDescriptorRanges == 0 ||
            numSrcDescriptorRanges == 0)
            return;

        UINT dstRange = 0;
        UINT srcRange = 0;
        UINT dstOffset = 0;
        UINT srcOffset = 0;
        UINT totalDescriptors = 0;
        if (pDestDescriptorRangeSizes) {
            for (UINT i = 0; i < numDestDescriptorRanges; ++i)
                totalDescriptors += pDestDescriptorRangeSizes[i];
        } else if (pSrcDescriptorRangeSizes) {
            for (UINT i = 0; i < numSrcDescriptorRanges; ++i)
                totalDescriptors += pSrcDescriptorRangeSizes[i];
        } else {
            totalDescriptors = 1;
        }

        while (dstRange < numDestDescriptorRanges && srcRange < numSrcDescriptorRanges) {
            UINT dstRangeSize = pDestDescriptorRangeSizes ? pDestDescriptorRangeSizes[dstRange] : totalDescriptors;
            UINT srcRangeSize = pSrcDescriptorRangeSizes ? pSrcDescriptorRangeSizes[srcRange] : totalDescriptors;
            if (dstRangeSize == 0) {
                ++dstRange;
                dstOffset = 0;
                continue;
            }
            if (srcRangeSize == 0) {
                ++srcRange;
                srcOffset = 0;
                continue;
            }

            UINT copyCount = std::min(dstRangeSize - dstOffset, srcRangeSize - srcOffset);

            D3D12_CPU_DESCRIPTOR_HANDLE dst = {pDestDescriptorRangeStarts[dstRange].ptr + dstOffset};
            D3D12_CPU_DESCRIPTOR_HANDLE src = {pSrcDescriptorRangeStarts[srcRange].ptr + srcOffset};
            CopyDescriptorsSimple(copyCount, dst, src, 0);

            dstOffset += copyCount;
            srcOffset += copyCount;
            if (dstOffset >= dstRangeSize) {
                ++dstRange;
                dstOffset = 0;
            }
            if (srcOffset >= srcRangeSize) {
                ++srcRange;
                srcOffset = 0;
            }
        }
    }

    void CopyDescriptorsSimple(UINT numDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptorRangeStart,
                               D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptorRangeStart, UINT) override {
        if (numDescriptors == 0)
            return;
        D3D12DescriptorHeapImpl* dstHeap = findHeapForHandle(destDescriptorRangeStart);
        D3D12DescriptorHeapImpl* srcHeap = findHeapForHandle(srcDescriptorRangeStart);
        if (!dstHeap || !srcHeap)
            return;

        UINT dstIdx = dstHeap->handleToIndex(destDescriptorRangeStart);
        UINT srcIdx = srcHeap->handleToIndex(srcDescriptorRangeStart);
        if (dstIdx == UINT_MAX || srcIdx == UINT_MAX)
            return;

        UINT copyCount = std::min(numDescriptors, dstHeap->desc.NumDescriptors - dstIdx);
        copyCount = std::min(copyCount, srcHeap->desc.NumDescriptors - srcIdx);
        if (copyCount == 0)
            return;

        std::vector<D3D12Descriptor> snapshot;
        snapshot.reserve(copyCount);
        for (UINT i = 0; i < copyCount; ++i)
            snapshot.push_back(*srcHeap->getDescriptorByIndex(srcIdx + i));

        for (UINT i = 0; i < copyCount; ++i)
            *dstHeap->getDescriptorByIndex(dstIdx + i) = snapshot[i];
        dstHeap->markDirty(dstIdx, copyCount);
    }

    HRESULT ReserveTiles(ID3D12Resource* pTiledResource, UINT NumTileRegions,
                         const D3D12_TILED_RESOURCE_COORDINATE* pCoords, const D3D12_TILE_REGION_SIZE* pSizes,
                         BOOL bSingleTile) override {
        if (!pTiledResource)
            return E_INVALIDARG;
        StubTelemetry::record("d3d12.dll", "ReserveTiles", StubBehavior::CompatibilityShim, "S_OK",
                              "tile reservation is accepted; physical sparse tile commit is not implemented yet");
        return S_OK;
    }

    HRESULT GetResourceTiling(ID3D12Resource* pTiledResource, UINT* pNumTilesForResource,
                              D3D12_PACKED_MIP_INFO* pPackedMipDesc, D3D12_TILE_SHAPE* pStandardTileShape,
                              UINT* pNumSubresourceTilings, UINT FirstSubresourceTilingToGet,
                              D3D12_SUBRESOURCE_TILING* pSubresourceTilings) override {
        if (!pTiledResource)
            return E_INVALIDARG;

        D3D12_RESOURCE_DESC desc;
        if (FAILED(pTiledResource->GetDesc(&desc)))
            return E_FAIL;

        const UINT TILE_W = 128;
        const UINT TILE_H = 128;
        const UINT TILE_D = 1;

        UINT totalMips = desc.MipLevels > 0 ? desc.MipLevels : 1;
        UINT packedMips = 0;
        if (totalMips > 1) {
            UINT w = (UINT)desc.Width, h = desc.Height, d = desc.DepthOrArraySize;
            for (UINT i = 0; i < totalMips; i++) {
                if (w < TILE_W && h < TILE_H) {
                    packedMips = totalMips - i;
                    break;
                }
                w = w > 1 ? w >> 1 : 1;
                h = h > 1 ? h >> 1 : 1;
                d = d > 1 ? d >> 1 : 1;
            }
        }
        UINT standardMips = totalMips - packedMips;

        UINT totalTiles = 0;
        for (UINT i = 0; i < standardMips; i++) {
            UINT w = (UINT)desc.Width >> i;
            if (w == 0)
                w = 1;
            UINT h = desc.Height >> i;
            if (h == 0)
                h = 1;
            UINT d = desc.DepthOrArraySize >> i;
            if (d == 0)
                d = 1;
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
                    UINT w = (UINT)desc.Width >> i;
                    if (w == 0)
                        w = 1;
                    UINT h = desc.Height >> i;
                    if (h == 0)
                        h = 1;
                    UINT d = desc.DepthOrArraySize >> i;
                    if (d == 0)
                        d = 1;
                    pSubresourceTilings[i - FirstSubresourceTilingToGet].WidthInTiles = (w + TILE_W - 1) / TILE_W;
                    pSubresourceTilings[i - FirstSubresourceTilingToGet].HeightInTiles = (h + TILE_H - 1) / TILE_H;
                    pSubresourceTilings[i - FirstSubresourceTilingToGet].DepthInTiles = d;
                    pSubresourceTilings[i - FirstSubresourceTilingToGet].StartTileIndexInOverallResource =
                        i == 0 ? 0 : 0;
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

    UINT64 GetTiledResourceSize(UINT64 Width, UINT Height, UINT DepthOrArraySize, UINT Format,
                                UINT MipLevels) override {
        const UINT TILE_W = 128;
        const UINT TILE_H = 128;
        const UINT64 TILE_BYTES = D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;

        UINT totalMips = MipLevels > 0 ? MipLevels : 1;
        UINT totalTiles = 0;
        for (UINT i = 0; i < totalMips; i++) {
            UINT w = (UINT)(Width >> i);
            if (w == 0)
                w = 1;
            UINT h = (UINT)(Height >> i);
            if (h == 0)
                h = 1;
            UINT d = (UINT)(DepthOrArraySize >> i);
            if (d == 0)
                d = 1;
            UINT tw = (w + TILE_W - 1) / TILE_W;
            UINT th = (h + TILE_H - 1) / TILE_H;
            totalTiles += tw * th * d;
        }
        return totalTiles * TILE_BYTES;
    }

    HRESULT CreateSamplerFeedback(ID3D12Resource* pTargetResource, UINT FeedbackType, REFIID riid,
                                  void** ppFeedbackResource) override {
        if (!pTargetResource || !ppFeedbackResource)
            return E_INVALIDARG;
        D3D12_RESOURCE_DESC targetDesc;
        if (FAILED(pTargetResource->GetDesc(&targetDesc)))
            return E_FAIL;

        auto* feedbackRes = new D3D12ResourceImpl(targetDesc);
        feedbackRes->m_resourceState = D3D12_RESOURCE_STATE_COMMON;
        *ppFeedbackResource = feedbackRes;
        return S_OK;
    }

    HRESULT WriteSamplerFeedback(ID3D12Resource* pTargetResource, ID3D12Resource* pFeedbackResource,
                                 UINT FeedbackType) override {
        if (!pTargetResource || !pFeedbackResource)
            return E_INVALIDARG;
        StubTelemetry::record("d3d12.dll", "WriteSamplerFeedback", StubBehavior::CompatibilityShim, "S_OK",
                              "sampler feedback writes are accepted but not materialized yet");
        return S_OK;
    }

    HRESULT ResolveSamplerFeedback(ID3D12Resource* pFeedbackResource, ID3D12Resource* pDestResource) override {
        if (!pFeedbackResource || !pDestResource)
            return E_INVALIDARG;
        StubTelemetry::record("d3d12.dll", "ResolveSamplerFeedback", StubBehavior::CompatibilityShim, "S_OK",
                              "sampler feedback resolves are accepted but not materialized yet");
        return S_OK;
    }

    HRESULT CreateStateObject(const void* pDesc, REFIID riid, void** ppStateObject) override {
        if (!pDesc || !ppStateObject)
            return E_INVALIDARG;
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
                if (config)
                    maxRecursion = config->MaxTraceRecursionDepth;
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
                    libraries.emplace_back(static_cast<const uint8_t*>(lib->DXILLibrary), lib->DXILLibrarySize);
                }
                break;
            }
            case 5: {
                auto* config1 = static_cast<const D3D12_RAYTRACING_PIPELINE_CONFIG1*>(sub.pDesc);
                if (config1)
                    maxRecursion = config1->MaxTraceRecursionDepth;
                break;
            }
            }
        }

        auto& bridge = IRConverterBridge::instance();
        if (bridge.isAvailable() && !libraries.empty()) {
            IRConverterReflection reflection;
            std::vector<uint8_t> metallib;
            for (auto& [data, size] : libraries) {
                bridge.compileRayTracingShader(data, size, ShaderStage::RayGeneration, nullptr, maxRecursion,
                                               maxAttributeSize > 0 ? maxAttributeSize : 32, metallib, reflection);
            }
            stateObj->m_shaderIdentifierData.resize(64, 0);
        }

        *ppStateObject = stateObj;
        return S_OK;
    }

    HRESULT GetRaytracingAccelerationStructurePrebuildInfo(const void* pDesc, void* pInfo) override {
        if (!pDesc || !pInfo)
            return E_INVALIDARG;
        auto* info = static_cast<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO*>(pInfo);
        info->ResultDataMaxSizeInBytes = 64 * 1024 * 1024;
        info->ScratchDataSizeInBytes = 32 * 1024 * 1024;
        info->UpdateScratchDataSizeInBytes = 32 * 1024 * 1024;
        return S_OK;
    }

    HRESULT DecodeRaytracingAccelerationStructure(ID3D12Resource*, void*) override { return E_NOTIMPL; }

  private:
    HRESULT createView(ID3D12Resource* pResource, UINT type, UINT format, D3D12_CPU_DESCRIPTOR_HANDLE handle) {
        if (handle.ptr == 0)
            return E_INVALIDARG;

        D3D12DescriptorHeapImpl* heap = nullptr;
        for (auto* h : m_trackedHeaps) {
            if (h->handleToIndex(handle) != UINT_MAX) {
                heap = h;
                break;
            }
        }
        if (!heap)
            return S_OK;

        auto* desc = heap->getDescriptor(handle);
        if (!desc)
            return S_OK;
        UINT index = heap->handleToIndex(handle);

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
        heap->markDirty(index);
        return S_OK;
    }

    std::vector<D3D12DescriptorHeapImpl*> m_trackedHeaps;
    std::shared_ptr<GPUAddressRegistry> m_gpuAddressResources = std::make_shared<GPUAddressRegistry>();

    D3D12DescriptorHeapImpl* findHeapForHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle) const;
    D3D12DescriptorHeapImpl* findHeapForGPUHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle) const;
    D3D12ResourceImpl* findResourceForGPUAddress(D3D12_GPU_VIRTUAL_ADDRESS address, UINT64* offset = nullptr) const;
};

} // namespace metalsharp
