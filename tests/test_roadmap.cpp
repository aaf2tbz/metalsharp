#include <cassert>
#include <cstdio>
#include <cstring>
#include <metalsharp/D3D12Device.h>
#include <metalsharp/D3D12ResourceStateTracker.h>
#include <metalsharp/PipelineCache.h>

using namespace metalsharp;

static int testsPassed = 0;
static int testsFailed = 0;

#define TEST(name)                                                                                                     \
    printf("  TEST: %-50s", #name);                                                                                    \
    if (test_##name()) {                                                                                               \
        printf("PASS\n");                                                                                              \
        testsPassed++;                                                                                                 \
    } else {                                                                                                           \
        printf("FAIL\n");                                                                                              \
        testsFailed++;                                                                                                 \
    }

class FakeResource final : public ID3D12Resource {
  public:
    ULONG refs = 1;
    UINT state = D3D12_RESOURCE_STATE_COMMON;

    HRESULT QueryInterface(REFIID, void** ppv) override {
        if (!ppv)
            return E_POINTER;
        *ppv = this;
        AddRef();
        return S_OK;
    }
    ULONG AddRef() override { return ++refs; }
    ULONG Release() override { return --refs; }
    HRESULT GetPrivateData(const GUID&, UINT*, void*) override { return E_NOTIMPL; }
    HRESULT SetPrivateData(const GUID&, UINT, const void*) override { return E_NOTIMPL; }
    HRESULT SetPrivateDataInterface(const GUID&, const IUnknown*) override { return E_NOTIMPL; }
    HRESULT SetName(const char*) override { return S_OK; }
    HRESULT Map(UINT, const D3D12_RANGE*, void**) override { return E_NOTIMPL; }
    HRESULT Unmap(UINT, const D3D12_RANGE*) override { return S_OK; }
    HRESULT GetDesc(D3D12_RESOURCE_DESC*) override { return E_NOTIMPL; }
    HRESULT GetGPUVirtualAddress(D3D12_GPU_VIRTUAL_ADDRESS*) override { return E_NOTIMPL; }
    UINT __getResourceState() const override { return state; }
    void __setResourceState(UINT next) override { state = next; }
};

static bool test_pipeline_cache_key_is_stable() {
    PipelineCacheKey key = {};
    key.vertexShaderHash = 0x1111;
    key.pixelShaderHash = 0x2222;
    key.rootSignatureHash = 0x3333;
    key.renderTargetFormats[0] = 80;
    key.depthStencilFormat = 252;
    key.sampleCount = 4;
    key.blendStateHash = 0x44;
    key.rasterStateHash = 0x55;
    key.depthStateHash = 0x66;
    key.featureFlags = 0x77;

    uint64_t first = PipelineCache::computeKeyHash(key);
    uint64_t second = PipelineCache::computeKeyHash(key);
    key.sampleCount = 1;
    uint64_t changed = PipelineCache::computeKeyHash(key);
    return first != 0 && first == second && first != changed;
}

static bool test_pipeline_miss_telemetry_records_reason() {
    auto& cache = PipelineCache::instance();
    cache.recordMiss(0x1234, "unit_test", "roadmap");
    PipelineMissTelemetry miss = cache.lastMiss();
    return miss.hash == 0x1234 && miss.reason == "unit_test" && miss.label == "roadmap";
}

static bool test_resource_state_tracker_transition() {
    FakeResource resource;
    D3D12ResourceStateTracker tracker;
    tracker.setInitialState(&resource, D3D12_RESOURCE_STATE_COPY_DEST);

    auto barrier =
        tracker.applyTransition(&resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    return !barrier.stateMismatch && resource.state == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE &&
           tracker.stateFor(&resource) == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE &&
           barrier.metalUsage == MetalResourceUsage::Read;
}

static bool test_resource_state_tracker_mismatch() {
    FakeResource resource;
    D3D12ResourceStateTracker tracker;
    tracker.setInitialState(&resource, D3D12_RESOURCE_STATE_COPY_DEST);

    auto barrier =
        tracker.applyTransition(&resource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    return barrier.stateMismatch && resource.state == D3D12_RESOURCE_STATE_COPY_SOURCE;
}

static bool test_descriptor_heap_dirty_ranges() {
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8, 0, 0};
    D3D12DescriptorHeapImpl heap(&heapDesc);
    heap.markDirty(2, 3);
    bool first = heap.hasDirtyRange() && heap.dirtyStart == 2 && heap.dirtyEnd == 5 && heap.generation == 1;
    heap.clearDirtyRange();
    return first && !heap.hasDirtyRange();
}

int main() {
    printf("MetalSharp roadmap foundation tests\n");
    TEST(pipeline_cache_key_is_stable);
    TEST(pipeline_miss_telemetry_records_reason);
    TEST(resource_state_tracker_transition);
    TEST(resource_state_tracker_mismatch);
    TEST(descriptor_heap_dirty_ranges);

    printf("\nPassed: %d  Failed: %d\n", testsPassed, testsFailed);
    return testsFailed == 0 ? 0 : 1;
}
