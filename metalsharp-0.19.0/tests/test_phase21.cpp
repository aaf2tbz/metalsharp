#include <metalsharp/ShaderCache.h>
#include <metalsharp/PipelineCache.h>
#include <metalsharp/FramePacer.h>
#include <metalsharp/GPUProfiler.h>
#include <metalsharp/MetalFXUpscaler.h>
#include <metalsharp/RenderThreadPool.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <thread>

using namespace metalsharp;

static int testsPassed = 0;
static int testsFailed = 0;

#define TEST(name) \
    printf("  TEST: %-55s", #name); \
    if (test_##name()) { printf("PASS\n"); testsPassed++; } \
    else { printf("FAIL\n"); testsFailed++; }

// 21.1 Shader Cache
static bool test_shader_cache_hash_consistency() {
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    uint64_t h1 = ShaderCache::computeHash(data, 5);
    uint64_t h2 = ShaderCache::computeHash(data, 5);
    return h1 == h2 && h1 != 0;
}

static bool test_shader_cache_hash_differs() {
    uint8_t data1[] = {0x01, 0x02, 0x03};
    uint8_t data2[] = {0x01, 0x02, 0x04};
    return ShaderCache::computeHash(data1, 3) != ShaderCache::computeHash(data2, 3);
}

static bool test_shader_cache_store_and_lookup() {
    auto& cache = ShaderCache::instance();
    cache.clear();
    cache.init("/tmp/metalsharp_test_cache");

    uint8_t bytecode[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint64_t hash = ShaderCache::computeHash(bytecode, 4);

    CachedShader shader;
    shader.hash = hash;
    shader.mslSource = "float4 main() { return 1; }";
    cache.store(hash, shader);

    CachedShader out;
    bool found = cache.lookup(hash, out);
    cache.shutdown();
    return found && out.mslSource == "float4 main() { return 1; }";
}

static bool test_shader_cache_miss() {
    auto& cache = ShaderCache::instance();
    cache.clear();
    cache.init("/tmp/metalsharp_test_cache");

    CachedShader out;
    bool found = cache.lookup(0xDEADBEEFDEADBEEFULL, out);
    cache.shutdown();
    return !found;
}

static bool test_shader_cache_metallib_store_lookup() {
    auto& cache = ShaderCache::instance();
    cache.clear();
    cache.init("/tmp/metalsharp_test_cache");

    uint8_t bytecode[] = {0xCA, 0xFE, 0xBA, 0xBE};
    uint64_t hash = ShaderCache::computeHash(bytecode, 4);

    uint8_t fakeMetallib[] = {0x4D, 0x54, 0x4C, 0x42};
    cache.storeMetallib(hash, fakeMetallib, 4, "main0");

    std::vector<uint8_t> outData;
    std::string outEntry;
    bool found = cache.lookupMetallib(hash, outData, outEntry);
    cache.shutdown();
    return found && outData.size() == 4 && outEntry == "main0";
}

static bool test_shader_cache_hit_miss_counts() {
    auto& cache = ShaderCache::instance();
    cache.clear();
    cache.init("/tmp/metalsharp_test_cache");

    uint8_t data[] = {0x01};
    uint64_t hash = ShaderCache::computeHash(data, 1);

    CachedShader shader;
    shader.hash = hash;
    shader.mslSource = "test";
    cache.store(hash, shader);

    CachedShader out;
    cache.lookup(hash, out);
    cache.lookup(0xDEAD, out);

    uint64_t hits = cache.hitCount();
    uint64_t misses = cache.missCount();
    cache.shutdown();
    return hits >= 1 && misses >= 1;
}

static bool test_shader_cache_entry_count() {
    auto& cache = ShaderCache::instance();
    cache.clear();
    cache.init("/tmp/metalsharp_test_cache");

    uint8_t d1[] = {0x01};
    uint8_t d2[] = {0x02};
    CachedShader s1, s2;
    s1.hash = ShaderCache::computeHash(d1, 1);
    s1.mslSource = "a";
    s2.hash = ShaderCache::computeHash(d2, 1);
    s2.mslSource = "b";

    cache.store(s1.hash, s1);
    cache.store(s2.hash, s2);

    uint64_t count = cache.entryCount();
    cache.shutdown();
    return count == 2;
}

// 21.2 Pipeline Cache
static bool test_pipeline_cache_descriptor_hash() {
    uint8_t desc[] = {0x01, 0x02, 0x03, 0x04};
    uint64_t h = PipelineCache::computeDescriptorHash(desc, 4);
    return h != 0;
}

static bool test_pipeline_cache_store_lookup() {
    auto& cache = PipelineCache::instance();
    cache.clear();
    cache.init("/tmp/metalsharp_test_cache");

    uint64_t hash = 0x1234567890ABCDEF;
    int dummyPipeline = 42;
    cache.store(hash, &dummyPipeline, "test_pipeline");

    void* result = cache.lookup(hash);
    cache.shutdown();
    return result == &dummyPipeline;
}

static bool test_pipeline_cache_miss() {
    auto& cache = PipelineCache::instance();
    cache.clear();
    cache.init("/tmp/metalsharp_test_cache");

    void* result = cache.lookup(0xDEAD);
    cache.shutdown();
    return result == nullptr;
}

static bool test_pipeline_cache_descriptor_roundtrip() {
    auto& cache = PipelineCache::instance();
    cache.clear();
    cache.init("/tmp/metalsharp_test_cache");

    uint8_t desc[] = {0xAA, 0xBB, 0xCC, 0xDD};
    uint64_t hash = PipelineCache::computeDescriptorHash(desc, 4);
    cache.storeDescriptor(hash, desc, 4, "my_pipeline");

    std::vector<uint8_t> outDesc;
    std::string outLabel;
    bool found = cache.getDescriptor(hash, outDesc, outLabel);
    cache.shutdown();
    return found && outDesc.size() == 4 && outLabel == "my_pipeline" && outDesc[0] == 0xAA;
}

// 21.3 Render Thread Pool
static bool test_render_thread_pool_init_shutdown() {
    auto& pool = RenderThreadPool::instance();
    pool.init(2);
    bool ok = pool.isInitialized() && pool.threadCount() == 2;
    pool.shutdown();
    return ok;
}

static bool test_render_thread_pool_submit() {
    auto& pool = RenderThreadPool::instance();
    pool.init(2);

    std::atomic<int> counter{0};
    for (int i = 0; i < 10; i++) {
        pool.submit([&counter]() { counter.fetch_add(1); });
    }
    pool.waitIdle();
    int val = counter.load();
    pool.shutdown();
    return val == 10;
}

static bool test_command_buffer_pool_lifecycle() {
    auto& pool = CommandBufferPool::instance();
    pool.init(nullptr);
    pool.shutdown();
    return true;
}

// 21.4 Frame Pacer
static bool test_frame_pacer_init() {
    auto& pacer = FramePacer::instance();
    pacer.init(60.0);
    bool ok = pacer.targetFPS() == 60.0 && pacer.isEnabled();
    pacer.shutdown();
    return ok;
}

static bool test_frame_pacer_present_modes() {
    auto& pacer = FramePacer::instance();
    pacer.init(60.0);

    pacer.setPresentMode(PresentMode::Immediate);
    bool imm = pacer.presentMode() == PresentMode::Immediate;

    pacer.setPresentMode(PresentMode::VSync);
    bool vsync = pacer.presentMode() == PresentMode::VSync;

    pacer.setPresentMode(PresentMode::HalfRateVSync);
    bool half = pacer.presentMode() == PresentMode::HalfRateVSync;

    pacer.shutdown();
    return imm && vsync && half;
}

static bool test_frame_pacer_frame_timing() {
    auto& pacer = FramePacer::instance();
    pacer.init(60.0);

    pacer.beginFrame();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    pacer.endFrame();

    FrameTiming timing = pacer.getTiming();
    pacer.shutdown();
    return timing.frameCount == 1 && timing.currentFPS >= 0;
}

static bool test_frame_pacer_triple_buffer() {
    auto& pacer = FramePacer::instance();
    pacer.init(60.0);
    pacer.setTripleBufferCount(3);
    bool ok = pacer.tripleBufferCount() == 3;
    pacer.shutdown();
    return ok;
}

// 21.5 MetalFX
static bool test_metal_fx_create_destroy() {
    auto* upscaler = MetalFXUpscaler::create(nullptr);
    bool ok = upscaler != nullptr;
    delete upscaler;
    return ok;
}

static bool test_metal_fx_init_process() {
    auto* upscaler = MetalFXUpscaler::create(nullptr);
    bool initOk = upscaler->init(960, 540, 1920, 1080);
    bool dims = upscaler->inputWidth() == 960 && upscaler->outputWidth() == 1920;
    upscaler->setSharpness(0.5f);
    bool sharp = upscaler->sharpness() == 0.5f;
    delete upscaler;
    return initOk && dims && sharp;
}

// 21.6 GPU Profiler
static bool test_gpu_profiler_init() {
    auto& profiler = GPUProfiler::instance();
    profiler.init();
    bool ok = profiler.isEnabled() == false;
    profiler.shutdown();
    return ok;
}

static bool test_gpu_profiler_frame_stats() {
    auto& profiler = GPUProfiler::instance();
    profiler.init();
    profiler.setEnabled(true);

    profiler.beginFrame();
    profiler.beginPass("render");
    profiler.recordDraw("render", 100, 1);
    profiler.endPass("render");
    profiler.endFrame();

    auto stats = profiler.getLastFrameStats();
    profiler.shutdown();
    return stats.drawCalls == 1 && stats.triangles == 33 && stats.frameIndex == 1;
}

static bool test_gpu_profiler_callback() {
    auto& profiler = GPUProfiler::instance();
    profiler.init();
    profiler.setEnabled(true);

    bool callbackFired = false;
    profiler.setStatsCallback([&callbackFired](const GPUProfiler::FrameStats& s) {
        callbackFired = true;
    });

    profiler.beginFrame();
    profiler.endFrame();

    profiler.shutdown();
    return callbackFired;
}

int main() {
    printf("=== Phase 21: Performance Pipeline ===\n\n");

    printf("--- 21.1 Shader Cache Overhaul ---\n");
    TEST(shader_cache_hash_consistency);
    TEST(shader_cache_hash_differs);
    TEST(shader_cache_store_and_lookup);
    TEST(shader_cache_miss);
    TEST(shader_cache_metallib_store_lookup);
    TEST(shader_cache_hit_miss_counts);
    TEST(shader_cache_entry_count);

    printf("\n--- 21.2 Pipeline State Cache ---\n");
    TEST(pipeline_cache_descriptor_hash);
    TEST(pipeline_cache_store_lookup);
    TEST(pipeline_cache_miss);
    TEST(pipeline_cache_descriptor_roundtrip);

    printf("\n--- 21.3 Multi-Threaded Rendering ---\n");
    TEST(render_thread_pool_init_shutdown);
    TEST(render_thread_pool_submit);
    TEST(command_buffer_pool_lifecycle);

    printf("\n--- 21.4 Frame Pacing & VSync ---\n");
    TEST(frame_pacer_init);
    TEST(frame_pacer_present_modes);
    TEST(frame_pacer_frame_timing);
    TEST(frame_pacer_triple_buffer);

    printf("\n--- 21.5 MetalFX Integration ---\n");
    TEST(metal_fx_create_destroy);
    TEST(metal_fx_init_process);

    printf("\n--- 21.6 GPU Profiling ---\n");
    TEST(gpu_profiler_init);
    TEST(gpu_profiler_frame_stats);
    TEST(gpu_profiler_callback);

    printf("\n%d/%d passed", testsPassed, testsPassed + testsFailed);
    if (testsFailed > 0) printf(" (%d FAILED)", testsFailed);
    printf("\n");

    return testsFailed > 0 ? 1 : 0;
}
