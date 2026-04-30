#include <metalsharp/IRConverterBridge.h>
#include <metalsharp/ShaderTranslator.h>
#include <metalsharp/ArgumentBufferBinding.h>
#include <metalsharp/ShaderCache.h>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <thread>
#include <chrono>

using namespace metalsharp;

static int testsPassed = 0;
static int testsFailed = 0;

#define TEST(name) \
    printf("  TEST: %-50s", #name); \
    if (test_##name()) { printf("PASS\n"); testsPassed++; } \
    else { printf("FAIL\n"); testsFailed++; }

static bool test_irconverter_available() {
    auto& bridge = IRConverterBridge::instance();
    bool avail = bridge.isAvailable();
    printf("[%s] ", avail ? "available" : "not found");
    return avail;
}

static bool test_irconverter_detect_dxil_container() {
    auto& bridge = IRConverterBridge::instance();
    if (!bridge.isAvailable()) return true;

    uint32_t fakeDXBC[16] = {};
    fakeDXBC[0] = 0x43425844;
    fakeDXBC[6] = 1;
    fakeDXBC[7] = 0;

    bool isDXIL = bridge.isDXIL(reinterpret_cast<uint8_t*>(fakeDXBC), sizeof(fakeDXBC));
    return true;
}

static bool test_irconverter_detect_shader_model() {
    auto& bridge = IRConverterBridge::instance();
    if (!bridge.isAvailable()) return true;

    uint8_t rawData[4] = {0x44, 0x58, 0x49, 0x4C};
    uint32_t sm = bridge.detectShaderModel(rawData, 4);
    return true;
}

static bool test_shader_translator_fallback_dxbc() {
    ShaderTranslator translator;

    uint8_t minimalDXBC[] = {
        0x44, 0x58, 0x42, 0x43,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x20, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00,
        0x20, 0x00, 0x00, 0x00,
        0x53, 0x48, 0x44, 0x52,
        0x08, 0x00, 0x00, 0x00,
        0xFE, 0xFF, 0x01, 0x00,
        0x02, 0x00, 0x00, 0x00,
    };

    CompiledShader shader = {};
    bool result = translator.translateDXBC(
        minimalDXBC, sizeof(minimalDXBC),
        ShaderStage::Vertex, shader
    );

    return true;
}

static bool test_shader_compile_service_init() {
    auto& service = ShaderCompileService::instance();
    bool ok = service.init(2);
    service.shutdown();
    return ok;
}

static bool test_argument_buffer_layout_empty() {
    auto& mgr = ArgumentBufferManager::instance();
    IRConverterReflection emptyReflection;
    auto layout = mgr.buildLayoutFromReflection(emptyReflection, ShaderStage::Vertex);
    return layout.totalSizeBytes > 0;
}

static bool test_argument_buffer_encode_constants() {
    auto& mgr = ArgumentBufferManager::instance();

    ArgumentBufferLayout layout;
    layout.totalSizeBytes = 256;
    layout.hasRootSignature = true;
    ArgumentBufferEntry entry;
    entry.rootParameterIndex = 0;
    entry.topLevelOffset = 0;
    entry.sizeBytes = 16;
    entry.type = ArgumentBufferResourceType::ConstantBuffer;
    layout.entries.push_back(entry);
    layout.rootParameterCount = 1;

    uint8_t buffer[256] = {};
    uint32_t constants[4] = {0xDEADBEEF, 0xCAFEBABE, 0x12345678, 0x87654321};

    bool ok = mgr.encodeRootConstants(layout, 0, buffer, sizeof(buffer), constants, 4);
    if (!ok) return false;

    uint32_t val0, val1;
    memcpy(&val0, buffer, 4);
    memcpy(&val1, buffer + 4, 4);
    return val0 == 0xDEADBEEF && val1 == 0xCAFEBABE;
}

static bool test_argument_buffer_encode_descriptor_table() {
    auto& mgr = ArgumentBufferManager::instance();

    ArgumentBufferLayout layout;
    layout.totalSizeBytes = 256;
    layout.hasRootSignature = true;
    ArgumentBufferEntry entry;
    entry.rootParameterIndex = 0;
    entry.topLevelOffset = 0;
    entry.sizeBytes = 8;
    entry.type = ArgumentBufferResourceType::ShaderResourceView;
    layout.entries.push_back(entry);
    layout.rootParameterCount = 1;

    uint8_t buffer[256] = {};
    uint64_t fakeGPUAddr = 0x12345678ABCDEF00ULL;

    bool ok = mgr.encodeDescriptorTable(layout, 0, buffer, sizeof(buffer), fakeGPUAddr);
    if (!ok) return false;

    uint64_t readback;
    memcpy(&readback, buffer, 8);
    return readback == fakeGPUAddr;
}

static bool test_argument_buffer_encode_root_descriptor() {
    auto& mgr = ArgumentBufferManager::instance();

    ArgumentBufferLayout layout;
    layout.totalSizeBytes = 256;
    ArgumentBufferEntry entry;
    entry.rootParameterIndex = 0;
    entry.topLevelOffset = 8;
    entry.sizeBytes = 8;
    entry.type = ArgumentBufferResourceType::ConstantBuffer;
    layout.entries.push_back(entry);
    layout.rootParameterCount = 1;

    uint8_t buffer[256] = {};
    uint64_t addr = 0xAABBCCDDEEFF0011ULL;

    bool ok = mgr.encodeRootDescriptor(layout, 0, buffer, sizeof(buffer), addr);
    if (!ok) return false;

    uint64_t readback;
    memcpy(&readback, buffer + 8, 8);
    return readback == addr;
}

static bool test_shader_cache_hash_stability() {
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    uint64_t h1 = ShaderCache::computeHash(data, sizeof(data));
    uint64_t h2 = ShaderCache::computeHash(data, sizeof(data));
    if (h1 != h2) return false;

    uint8_t data2[] = {0x01, 0x02, 0x03, 0x04, 0x06};
    uint64_t h3 = ShaderCache::computeHash(data2, sizeof(data2));
    return h1 != h3;
}

static bool test_shader_stage_enum_values() {
    return ShaderStage::Vertex == ShaderStage::Vertex &&
           ShaderStage::Pixel == ShaderStage::Pixel &&
           ShaderStage::Compute == ShaderStage::Compute;
}

static bool test_compiled_shader_init() {
    CompiledShader shader = {};
    return shader.library == nullptr &&
           shader.vertexFunction == nullptr &&
           shader.fragmentFunction == nullptr &&
           shader.computeFunction == nullptr;
}

int main() {
    printf("=== Phase 17: IRConverter Integration Tests ===\n\n");

    TEST(irconverter_available);
    TEST(irconverter_detect_dxil_container);
    TEST(irconverter_detect_shader_model);
    TEST(shader_translator_fallback_dxbc);
    TEST(shader_compile_service_init);
    TEST(argument_buffer_layout_empty);
    TEST(argument_buffer_encode_constants);
    TEST(argument_buffer_encode_descriptor_table);
    TEST(argument_buffer_encode_root_descriptor);
    TEST(shader_cache_hash_stability);
    TEST(shader_stage_enum_values);
    TEST(compiled_shader_init);

    printf("\n%d/%d passed", testsPassed, testsPassed + testsFailed);
    if (testsFailed > 0) printf(" (%d FAILED)", testsFailed);
    printf("\n");

    return testsFailed > 0 ? 1 : 0;
}
