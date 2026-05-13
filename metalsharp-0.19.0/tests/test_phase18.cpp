#include <metalsharp/D3D12Device.h>
#include <cstdio>
#include <cstring>
#include <cassert>

using namespace metalsharp;

static int testsPassed = 0;
static int testsFailed = 0;

#define TEST(name) \
    printf("  TEST: %-50s", #name); \
    if (test_##name()) { printf("PASS\n"); testsPassed++; } \
    else { printf("FAIL\n"); testsFailed++; }

static bool test_d3d12_device_create() {
    D3D12DeviceImpl* device = nullptr;
    HRESULT hr = D3D12DeviceImpl::create(&device);
    if (FAILED(hr) || !device) return false;
    device->Release();
    return true;
}

static bool test_d3d12_create_command_queue() {
    D3D12DeviceImpl* device = nullptr;
    D3D12DeviceImpl::create(&device);
    if (!device) return false;

    void* queue = nullptr;
    HRESULT hr = device->CreateCommandQueue(nullptr, IID_ID3D12CommandQueue, &queue);
    device->Release();
    return SUCCEEDED(hr) && queue != nullptr;
}

static bool test_d3d12_create_command_allocator() {
    D3D12DeviceImpl* device = nullptr;
    D3D12DeviceImpl::create(&device);
    if (!device) return false;

    void* alloc = nullptr;
    HRESULT hr = device->CreateCommandAllocator(0, IID_ID3D12CommandAllocator, &alloc);
    device->Release();
    return SUCCEEDED(hr) && alloc != nullptr;
}

static bool test_d3d12_create_root_signature_empty() {
    D3D12DeviceImpl* device = nullptr;
    D3D12DeviceImpl::create(&device);
    if (!device) return false;

    uint8_t blob[16] = {};
    uint32_t version = 1, numParams = 0, numStatic = 0, flags = 0;
    memcpy(blob, &version, 4);
    memcpy(blob + 4, &numParams, 4);
    memcpy(blob + 8, &numStatic, 4);
    memcpy(blob + 12, &flags, 4);

    void* rs = nullptr;
    HRESULT hr = device->CreateRootSignature(0, blob, sizeof(blob), IID_ID3D12RootSignature, &rs);
    device->Release();
    return SUCCEEDED(hr) && rs != nullptr;
}

static bool test_d3d12_root_signature_parameters() {
    D3D12DeviceImpl* device = nullptr;
    D3D12DeviceImpl::create(&device);
    if (!device) return false;

    struct {
        uint32_t version;
        uint32_t numParams;
        uint32_t numStatic;
        uint32_t flags;
        struct {
            uint32_t type;
            uint32_t visibility;
            uint32_t shaderRegister;
            uint32_t registerSpace;
            uint32_t num32BitValues;
        } param;
    } blob = {};
    blob.version = 1;
    blob.numParams = 1;
    blob.numStatic = 0;
    blob.flags = 0;
    blob.param.type = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    blob.param.visibility = 0;
    blob.param.shaderRegister = 0;
    blob.param.registerSpace = 0;
    blob.param.num32BitValues = 4;

    void* rsPtr = nullptr;
    HRESULT hr = device->CreateRootSignature(0, &blob, sizeof(blob), IID_ID3D12RootSignature, &rsPtr);
    if (FAILED(hr)) { device->Release(); return false; }

    auto* rs = static_cast<D3D12RootSignatureImpl*>(rsPtr);
    bool ok = rs->numParameters == 1 &&
              rs->parameters.size() == 1 &&
              rs->parameters[0].ParameterType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS &&
              rs->parameters[0].Constants.Num32BitValues == 4 &&
              rs->argumentBufferSize > 0 &&
              rs->parameterLayouts.size() == 1;

    rs->Release();
    device->Release();
    return ok;
}

static bool test_d3d12_root_signature_layout_offsets() {
    D3D12DeviceImpl* device = nullptr;
    D3D12DeviceImpl::create(&device);
    if (!device) return false;

    struct {
        uint32_t version;
        uint32_t numParams;
        uint32_t numStatic;
        uint32_t flags;
        struct {
            uint32_t type;
            uint32_t visibility;
            uint32_t shaderRegister;
            uint32_t registerSpace;
        } params[2];
    } blob = {};
    blob.version = 1;
    blob.numParams = 2;
    blob.numStatic = 0;
    blob.flags = 0;
    blob.params[0].type = D3D12_ROOT_PARAMETER_TYPE_CBV;
    blob.params[0].visibility = 0;
    blob.params[0].shaderRegister = 0;
    blob.params[0].registerSpace = 0;
    blob.params[1].type = D3D12_ROOT_PARAMETER_TYPE_SRV;
    blob.params[1].visibility = 0;
    blob.params[1].shaderRegister = 1;
    blob.params[1].registerSpace = 0;

    void* rsPtr = nullptr;
    device->CreateRootSignature(0, &blob, sizeof(blob), IID_ID3D12RootSignature, &rsPtr);
    auto* rs = static_cast<D3D12RootSignatureImpl*>(rsPtr);

    bool ok = rs->parameterLayouts.size() == 2 &&
              rs->parameterLayouts[0].offset == 0 &&
              rs->parameterLayouts[1].offset >= sizeof(uint64_t) &&
              rs->parameterLayouts[1].offset % 16 == 0;

    rs->Release();
    device->Release();
    return ok;
}

static bool test_d3d12_descriptor_heap_create() {
    D3D12DeviceImpl* device = nullptr;
    D3D12DeviceImpl::create(&device);
    if (!device) return false;

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 64;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    void* heap = nullptr;
    HRESULT hr = device->CreateDescriptorHeap(&desc, IID_ID3D12DescriptorHeap, &heap);
    device->Release();
    return SUCCEEDED(hr) && heap != nullptr;
}

static bool test_d3d12_descriptor_heap_copy() {
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 16, 0, 0};
    D3D12DescriptorHeapImpl heap(&heapDesc);

    auto* desc = heap.getDescriptorByIndex(0);
    if (!desc) return false;
    desc->gpuAddress = 0xDEADBEEF;
    desc->bufferSize = 256;
    desc->type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

    D3D12_CPU_DESCRIPTOR_HANDLE dst = {2};
    D3D12_CPU_DESCRIPTOR_HANDLE src = {1};
    heap.copyDescriptors(1, dst, src);

    auto* copied = heap.getDescriptorByIndex(1);
    return copied && copied->gpuAddress == 0xDEADBEEF && copied->bufferSize == 256;
}

static bool test_d3d12_create_command_list() {
    D3D12DeviceImpl* device = nullptr;
    D3D12DeviceImpl::create(&device);
    if (!device) return false;

    void* alloc = nullptr;
    device->CreateCommandAllocator(0, IID_ID3D12CommandAllocator, &alloc);

    void* cmdList = nullptr;
    HRESULT hr = device->CreateCommandList(0, 0, static_cast<ID3D12CommandAllocator*>(alloc), nullptr, IID_ID3D12GraphicsCommandList, &cmdList);
    device->Release();
    return SUCCEEDED(hr) && cmdList != nullptr;
}

static bool test_d3d12_root_binding_constants() {
    D3D12DeviceImpl* device = nullptr;
    D3D12DeviceImpl::create(&device);
    if (!device) return false;

    struct {
        uint32_t version = 1;
        uint32_t numParams = 1;
        uint32_t numStatic = 0;
        uint32_t flags = 0;
        struct {
            uint32_t type = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            uint32_t visibility = 0;
            uint32_t shaderRegister = 0;
            uint32_t registerSpace = 0;
            uint32_t num32BitValues = 4;
        } param;
    } blob;

    void* rsPtr = nullptr;
    device->CreateRootSignature(0, &blob, sizeof(blob), IID_ID3D12RootSignature, &rsPtr);
    auto* rs = static_cast<D3D12RootSignatureImpl*>(rsPtr);

    void* alloc = nullptr;
    device->CreateCommandAllocator(0, IID_ID3D12CommandAllocator, &alloc);
    void* cmdListPtr = nullptr;
    device->CreateCommandList(0, 0, static_cast<ID3D12CommandAllocator*>(alloc), nullptr, IID_ID3D12GraphicsCommandList, &cmdListPtr);
    auto* cmdList = static_cast<ID3D12GraphicsCommandList*>(cmdListPtr);

    cmdList->SetGraphicsRootSignature(static_cast<ID3D12RootSignature*>(rs));

    uint32_t constants[4] = {0x11111111, 0x22222222, 0x33333333, 0x44444444};
    cmdList->SetGraphicsRoot32BitConstants(0, 4, constants, 0);

    cmdList->Close();
    rs->Release();
    cmdList->Release();
    device->Release();
    return true;
}

static bool test_d3d12_root_binding_cbv() {
    D3D12DeviceImpl* device = nullptr;
    D3D12DeviceImpl::create(&device);
    if (!device) return false;

    struct {
        uint32_t version = 1;
        uint32_t numParams = 1;
        uint32_t numStatic = 0;
        uint32_t flags = 0;
        struct {
            uint32_t type = D3D12_ROOT_PARAMETER_TYPE_CBV;
            uint32_t visibility = 0;
            uint32_t shaderRegister = 0;
            uint32_t registerSpace = 0;
        } param;
    } blob;

    void* rsPtr = nullptr;
    device->CreateRootSignature(0, &blob, sizeof(blob), IID_ID3D12RootSignature, &rsPtr);
    auto* rs = static_cast<D3D12RootSignatureImpl*>(rsPtr);

    void* alloc = nullptr;
    device->CreateCommandAllocator(0, IID_ID3D12CommandAllocator, &alloc);
    void* cmdListPtr = nullptr;
    device->CreateCommandList(0, 0, static_cast<ID3D12CommandAllocator*>(alloc), nullptr, IID_ID3D12GraphicsCommandList, &cmdListPtr);
    auto* cmdList = static_cast<ID3D12GraphicsCommandList*>(cmdListPtr);

    cmdList->SetGraphicsRootSignature(static_cast<ID3D12RootSignature*>(rs));
    cmdList->SetGraphicsRootConstantBufferView(0, 0x12345678ABCDEF00ULL);
    cmdList->Close();

    rs->Release();
    cmdList->Release();
    device->Release();
    return true;
}

static bool test_d3d12_root_binding_descriptor_table() {
    D3D12DeviceImpl* device = nullptr;
    D3D12DeviceImpl::create(&device);
    if (!device) return false;

    struct {
        uint32_t version = 1;
        uint32_t numParams = 1;
        uint32_t numStatic = 0;
        uint32_t flags = 0;
        struct {
            uint32_t type = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            uint32_t visibility = 0;
            uint32_t numRanges = 0;
            uint32_t pad;
        } param;
    } blob;

    void* rsPtr = nullptr;
    device->CreateRootSignature(0, &blob, sizeof(blob), IID_ID3D12RootSignature, &rsPtr);
    auto* rs = static_cast<D3D12RootSignatureImpl*>(rsPtr);

    void* alloc = nullptr;
    device->CreateCommandAllocator(0, IID_ID3D12CommandAllocator, &alloc);
    void* cmdListPtr = nullptr;
    device->CreateCommandList(0, 0, static_cast<ID3D12CommandAllocator*>(alloc), nullptr, IID_ID3D12GraphicsCommandList, &cmdListPtr);
    auto* cmdList = static_cast<ID3D12GraphicsCommandList*>(cmdListPtr);

    cmdList->SetGraphicsRootSignature(static_cast<ID3D12RootSignature*>(rs));
    cmdList->SetGraphicsRootDescriptorTable(0, {0xDEADBEEFCAFEBABE});
    cmdList->Close();

    rs->Release();
    cmdList->Release();
    device->Release();
    return true;
}

static bool test_d3d12_committed_resource_buffer() {
    D3D12DeviceImpl* device = nullptr;
    D3D12DeviceImpl::create(&device);
    if (!device) return false;

    D3D12_HEAP_PROPERTIES heapProps = {D3D12_HEAP_TYPE_UPLOAD, 0, 0, 0, 0};
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 1024;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = 0;
    desc.SampleDesc = {1, 0};
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = 0;

    void* resPtr = nullptr;
    HRESULT hr = device->CreateCommittedResource(&heapProps, 0, &desc, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, nullptr, IID_ID3D12Resource, &resPtr);
    device->Release();
    return SUCCEEDED(hr) && resPtr != nullptr;
}

static bool test_d3d12_buffer_map_unmap() {
    D3D12DeviceImpl* device = nullptr;
    D3D12DeviceImpl::create(&device);
    if (!device) return false;

    D3D12_HEAP_PROPERTIES heapProps = {D3D12_HEAP_TYPE_UPLOAD, 0, 0, 0, 0};
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 256;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc = {1, 0};

    void* resPtr = nullptr;
    device->CreateCommittedResource(&heapProps, 0, &desc, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, nullptr, IID_ID3D12Resource, &resPtr);
    auto* res = static_cast<ID3D12Resource*>(resPtr);

    void* data = nullptr;
    HRESULT hr = res->Map(0, nullptr, &data);
    bool mapped = SUCCEEDED(hr) && data != nullptr;
    if (mapped) {
        memset(data, 0xAB, 256);
        res->Unmap(0, nullptr);
    }
    res->Release();
    device->Release();
    return mapped;
}

static bool test_d3d12_fence_signal() {
    D3D12DeviceImpl* device = nullptr;
    D3D12DeviceImpl::create(&device);
    if (!device) return false;

    void* fencePtr = nullptr;
    device->CreateFence(0, 0, IID_ID3D12Fence, &fencePtr);
    auto* fence = static_cast<D3D12FenceImpl*>(fencePtr);

    fence->Signal(42);
    UINT64 completed = 0;
    fence->GetCompletedValue(&completed);

    fence->Release();
    device->Release();
    return completed == 42;
}

int main() {
    printf("=== Phase 18: D3D12 Root Signatures & Descriptor Heaps ===\n\n");

    TEST(d3d12_device_create);
    TEST(d3d12_create_command_queue);
    TEST(d3d12_create_command_allocator);
    TEST(d3d12_create_root_signature_empty);
    TEST(d3d12_root_signature_parameters);
    TEST(d3d12_root_signature_layout_offsets);
    TEST(d3d12_descriptor_heap_create);
    TEST(d3d12_descriptor_heap_copy);
    TEST(d3d12_create_command_list);
    TEST(d3d12_root_binding_constants);
    TEST(d3d12_root_binding_cbv);
    TEST(d3d12_root_binding_descriptor_table);
    TEST(d3d12_committed_resource_buffer);
    TEST(d3d12_buffer_map_unmap);
    TEST(d3d12_fence_signal);

    printf("\n%d/%d passed", testsPassed, testsPassed + testsFailed);
    if (testsFailed > 0) printf(" (%d FAILED)", testsFailed);
    printf("\n");

    return testsFailed > 0 ? 1 : 0;
}
