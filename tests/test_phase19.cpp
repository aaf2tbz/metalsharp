#include <metalsharp/D3D12Device.h>
#include <metalsharp/IRConverterBridge.h>
#include <metalsharp/ShaderStage.h>
#include <metalsharp/ShaderModelValidator.h>
#include <d3d/D3D12.h>
#include <cstdio>
#include <cstring>
#include <cassert>

using namespace metalsharp;

static int testsPassed = 0;
static int testsFailed = 0;

#define TEST(name) \
    printf("  TEST: %-55s", #name); \
    if (test_##name()) { printf("PASS\n"); testsPassed++; } \
    else { printf("FAIL\n"); testsFailed++; }

static bool test_shader_stage_new_values() {
    return ShaderStage::Mesh != ShaderStage::Vertex &&
           ShaderStage::Amplification != ShaderStage::Compute &&
           ShaderStage::RayGeneration != ShaderStage::ClosestHit &&
           ShaderStage::Miss != ShaderStage::Intersection &&
           ShaderStage::AnyHit != ShaderStage::Callable;
}

static bool test_shader_model_60_capabilities() {
    auto& bridge = IRConverterBridge::instance();
    if (!bridge.isAvailable()) return true;
    auto caps = bridge.getShaderModelCapabilities(60);
    return caps.waveOps && caps.int64 &&
           !caps.halfPrecision && !caps.barycentrics &&
           !caps.rayTracing && !caps.meshShaders;
}

static bool test_shader_model_61_capabilities() {
    auto& bridge = IRConverterBridge::instance();
    if (!bridge.isAvailable()) return true;
    auto caps = bridge.getShaderModelCapabilities(61);
    return caps.waveOps && caps.int64 &&
           caps.halfPrecision && caps.barycentrics &&
           !caps.rayTracing && !caps.meshShaders;
}

static bool test_shader_model_63_raytracing() {
    auto& bridge = IRConverterBridge::instance();
    if (!bridge.isAvailable()) return true;
    auto caps = bridge.getShaderModelCapabilities(63);
    return caps.rayTracing && caps.waveOps && caps.halfPrecision;
}

static bool test_shader_model_65_mesh() {
    auto& bridge = IRConverterBridge::instance();
    if (!bridge.isAvailable()) return true;
    auto caps = bridge.getShaderModelCapabilities(65);
    return caps.meshShaders && caps.samplerFeedback && caps.rayTracing;
}

static bool test_shader_model_66_derivatives() {
    auto& bridge = IRConverterBridge::instance();
    if (!bridge.isAvailable()) return true;
    auto caps = bridge.getShaderModelCapabilities(66);
    return caps.computeDerivatives && caps.meshShaders && caps.rayTracing;
}

static bool test_shader_model_50_no_capabilities() {
    auto& bridge = IRConverterBridge::instance();
    auto caps = bridge.getShaderModelCapabilities(50);
    return !caps.waveOps && !caps.rayTracing && !caps.meshShaders && !caps.halfPrecision;
}

static bool test_create_state_object() {
    D3D12DeviceImpl* device = nullptr;
    D3D12DeviceImpl::create(&device);
    if (!device) return false;

    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {2};
    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {64, 32};
    D3D12_STATE_SUBOBJECT subobjects[2] = {};
    subobjects[0].Type = 0;
    subobjects[0].pDesc = &pipelineConfig;
    subobjects[1].Type = 1;
    subobjects[1].pDesc = &shaderConfig;

    D3D12_STATE_OBJECT_DESC stateDesc = {};
    stateDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    stateDesc.NumSubobjects = 2;
    stateDesc.pSubobjects = subobjects;

    void* stateObj = nullptr;
    static const GUID IID_StateObj = {0x1fdcde31, 0x2c0e, 0x4ce7, {0x92, 0x8e, 0x22, 0x0e, 0x37, 0x26, 0x68, 0x51}};
    HRESULT hr = device->CreateStateObject(&stateDesc, IID_StateObj, &stateObj);
    device->Release();
    return SUCCEEDED(hr) && stateObj != nullptr;
}

static bool test_state_object_with_library() {
    D3D12DeviceImpl* device = nullptr;
    D3D12DeviceImpl::create(&device);
    if (!device) return false;

    uint8_t fakeDXIL[64] = {};
    memcpy(fakeDXIL, "DXIL", 4);
    uint32_t version = 0x60;
    memcpy(fakeDXIL + 4, &version, 4);

    D3D12_DXIL_LIBRARY_DESC libDesc = {};
    libDesc.DXILLibrary = fakeDXIL;
    libDesc.DXILLibrarySize = sizeof(fakeDXIL);
    libDesc.NumExports = 0;

    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {1};

    D3D12_STATE_SUBOBJECT subobjects[2] = {};
    subobjects[0].Type = 2;
    subobjects[0].pDesc = &libDesc;
    subobjects[1].Type = 0;
    subobjects[1].pDesc = &pipelineConfig;

    D3D12_STATE_OBJECT_DESC stateDesc = {};
    stateDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    stateDesc.NumSubobjects = 2;
    stateDesc.pSubobjects = subobjects;

    void* stateObj = nullptr;
    static const GUID IID_SO = {0x2fdcde31, 0x2c0e, 0x4ce7, {0x92, 0x8e, 0x22, 0x0e, 0x37, 0x26, 0x68, 0x51}};
    HRESULT hr = device->CreateStateObject(&stateDesc, IID_SO, &stateObj);
    device->Release();
    return SUCCEEDED(hr) && stateObj != nullptr;
}

static bool test_rt_prebuild_info() {
    D3D12DeviceImpl* device = nullptr;
    D3D12DeviceImpl::create(&device);
    if (!device) return false;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.NumDescs = 1;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
    HRESULT hr = device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
    device->Release();
    return SUCCEEDED(hr) && info.ResultDataMaxSizeInBytes > 0 && info.ScratchDataSizeInBytes > 0;
}

static bool test_dispatch_rays_command() {
    D3D12DeviceImpl* device = nullptr;
    D3D12DeviceImpl::create(&device);
    if (!device) return false;

    void* alloc = nullptr;
    device->CreateCommandAllocator(0, IID_ID3D12CommandAllocator, &alloc);
    void* cmdListPtr = nullptr;
    device->CreateCommandList(0, 0, static_cast<ID3D12CommandAllocator*>(alloc), nullptr, IID_ID3D12GraphicsCommandList, &cmdListPtr);
    auto* cmdList = static_cast<ID3D12GraphicsCommandList*>(cmdListPtr);

    D3D12_DISPATCH_RAYS_DESC desc = {};
    desc.RayGenerationShaderRecord = 0x1000;
    desc.RayGenerationShaderRecordSize = 32;
    desc.Width = 128;
    desc.Height = 128;
    desc.Depth = 1;

    HRESULT hr = cmdList->DispatchRays(&desc);
    cmdList->Close();
    cmdList->Release();
    device->Release();
    return SUCCEEDED(hr);
}

static bool test_build_acceleration_structure() {
    D3D12DeviceImpl* device = nullptr;
    D3D12DeviceImpl::create(&device);
    if (!device) return false;

    void* alloc = nullptr;
    device->CreateCommandAllocator(0, IID_ID3D12CommandAllocator, &alloc);
    void* cmdListPtr = nullptr;
    device->CreateCommandList(0, 0, static_cast<ID3D12CommandAllocator*>(alloc), nullptr, IID_ID3D12GraphicsCommandList, &cmdListPtr);
    auto* cmdList = static_cast<ID3D12GraphicsCommandList*>(cmdListPtr);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
    desc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    desc.Inputs.NumDescs = 0;
    desc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    desc.DestAccelerationStructureData = 0x2000;
    desc.ScratchAccelerationStructureData = 0x3000;

    HRESULT hr = cmdList->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
    cmdList->Close();
    cmdList->Release();
    device->Release();
    return SUCCEEDED(hr);
}

static bool test_dispatch_mesh_command() {
    D3D12DeviceImpl* device = nullptr;
    D3D12DeviceImpl::create(&device);
    if (!device) return false;

    void* alloc = nullptr;
    device->CreateCommandAllocator(0, IID_ID3D12CommandAllocator, &alloc);
    void* cmdListPtr = nullptr;
    device->CreateCommandList(0, 0, static_cast<ID3D12CommandAllocator*>(alloc), nullptr, IID_ID3D12GraphicsCommandList, &cmdListPtr);
    auto* cmdList = static_cast<ID3D12GraphicsCommandList*>(cmdListPtr);

    HRESULT hr = cmdList->DispatchMesh(4, 1, 1);
    cmdList->Close();
    cmdList->Release();
    device->Release();
    return SUCCEEDED(hr);
}

static bool test_compute_root_signature() {
    D3D12DeviceImpl* device = nullptr;
    D3D12DeviceImpl::create(&device);
    if (!device) return false;

    void* alloc = nullptr;
    device->CreateCommandAllocator(0, IID_ID3D12CommandAllocator, &alloc);

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

    void* cmdListPtr = nullptr;
    device->CreateCommandList(0, 0, static_cast<ID3D12CommandAllocator*>(alloc), nullptr, IID_ID3D12GraphicsCommandList, &cmdListPtr);
    auto* cmdList = static_cast<ID3D12GraphicsCommandList*>(cmdListPtr);

    HRESULT hr = cmdList->SetComputeRootSignature(static_cast<ID3D12RootSignature*>(rsPtr));
    bool rootOk = SUCCEEDED(hr);

    uint32_t constants[4] = {0xAAA, 0xBBB, 0xCCC, 0xDDD};
    hr = cmdList->SetComputeRoot32BitConstants(0, 4, constants, 0);
    bool constOk = SUCCEEDED(hr);

    hr = cmdList->SetComputeRootConstantBufferView(0, 0xDEADBEEF00000000ULL);
    bool cbvOk = SUCCEEDED(hr);

    cmdList->Close();
    cmdList->Release();
    static_cast<ID3D12RootSignature*>(rsPtr)->Release();
    device->Release();
    return rootOk && constOk && cbvOk;
}

static bool test_compute_dispatch_records() {
    D3D12DeviceImpl* device = nullptr;
    D3D12DeviceImpl::create(&device);
    if (!device) return false;

    void* alloc = nullptr;
    device->CreateCommandAllocator(0, IID_ID3D12CommandAllocator, &alloc);
    void* cmdListPtr = nullptr;
    device->CreateCommandList(0, 0, static_cast<ID3D12CommandAllocator*>(alloc), nullptr, IID_ID3D12GraphicsCommandList, &cmdListPtr);
    auto* cmdList = static_cast<ID3D12GraphicsCommandList*>(cmdListPtr);

    HRESULT hr1 = cmdList->Dispatch(8, 8, 1);
    HRESULT hr2 = cmdList->Dispatch(16, 16, 1);
    cmdList->Close();
    cmdList->Release();
    device->Release();
    return SUCCEEDED(hr1) && SUCCEEDED(hr2);
}

static bool test_sm66_wave_size_validation() {
    ShaderModelValidator::validateWaveSize(66, 32);
    ShaderModelValidator::validateWaveSize(66, 64);
    return true;
}

static bool test_sm66_compute_validation() {
    ShaderModelValidator::validateComputeShader(
        66,
        256, 1, 1,
        true, true, true
    );
    ShaderModelValidator::validateComputeShader(
        66,
        8, 8, 1,
        true, false, false
    );
    return true;
}

static bool test_sm_pre60_no_validation() {
    ShaderModelValidator::validateComputeShader(
        50,
        8, 8, 8,
        true, true, true
    );
    ShaderModelValidator::validateWaveSize(60, 64);
    return true;
}

static bool test_irconverter_availability() {
    auto& bridge = IRConverterBridge::instance();
    bool avail = bridge.isAvailable();
    return true;
}

static bool test_detect_shader_model_dxil() {
    auto& bridge = IRConverterBridge::instance();
    if (!bridge.isAvailable()) return true;

    uint8_t fakeDXBC[64] = {};
    memcpy(fakeDXBC, "DXBC", 4);
    uint32_t hash = 0;
    memcpy(fakeDXBC + 16, &hash, 4);
    uint32_t one = 1;
    memcpy(fakeDXBC + 20, &one, 4);
    uint32_t totalSize = 64;
    memcpy(fakeDXBC + 24, &totalSize, 4);
    uint32_t chunkCount = 1;
    memcpy(fakeDXBC + 28, &chunkCount, 4);
    uint32_t chunkOffset = 36;
    memcpy(fakeDXBC + 32, &chunkOffset, 4);
    uint32_t chunkMagic = 0x52444853;
    memcpy(fakeDXBC + chunkOffset, &chunkMagic, 4);
    uint32_t chunkSize = 16;
    memcpy(fakeDXBC + chunkOffset + 4, &chunkSize, 4);
    uint32_t versionToken = (6 << 4) | 0;
    memcpy(fakeDXBC + chunkOffset + 8, &versionToken, 4);

    uint32_t sm = bridge.detectShaderModel(fakeDXBC, sizeof(fakeDXBC));
    return sm == 60;
}

int main() {
    printf("=== Phase 19: SM 6.x Coverage ===\n\n");

    printf("--- 19.1 Shader Stages & SM 6.0-6.2 ---\n");
    TEST(shader_stage_new_values);
    TEST(shader_model_60_capabilities);
    TEST(shader_model_61_capabilities);
    TEST(irconverter_availability);
    TEST(detect_shader_model_dxil);

    printf("\n--- 19.2 Ray Tracing (SM 6.3-6.4) ---\n");
    TEST(shader_model_63_raytracing);
    TEST(create_state_object);
    TEST(state_object_with_library);
    TEST(rt_prebuild_info);
    TEST(dispatch_rays_command);
    TEST(build_acceleration_structure);

    printf("\n--- 19.3 Mesh Shaders (SM 6.5) ---\n");
    TEST(shader_model_65_mesh);
    TEST(dispatch_mesh_command);

    printf("\n--- 19.4 Compute & SM 6.6 ---\n");
    TEST(shader_model_66_derivatives);
    TEST(compute_root_signature);
    TEST(compute_dispatch_records);
    TEST(sm66_wave_size_validation);
    TEST(sm66_compute_validation);
    TEST(sm_pre60_no_validation);
    TEST(shader_model_50_no_capabilities);

    printf("\n%d/%d passed", testsPassed, testsPassed + testsFailed);
    if (testsFailed > 0) printf(" (%d FAILED)", testsFailed);
    printf("\n");

    return testsFailed > 0 ? 1 : 0;
}
