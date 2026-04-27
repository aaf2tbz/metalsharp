#include <metalsharp/ShaderCache.h>
#include <metalsharp/PipelineCache.h>
#include <metalsharp/BufferPool.h>
#include <metalsharp/FramePacer.h>
#include <metalsharp/GPUProfiler.h>
#include <metalsharp/CommandBatcher.h>
#include <metalsharp/MetalFXUpscaler.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

static int passed = 0;
static int failed = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("  [OK] %s\n", msg); passed++; } \
    else { printf("  [FAIL] %s\n", msg); failed++; } \
} while(0)

int main() {
    printf("=== Phase 5 Performance Tests ===\n\n");

    {
        printf("--- ShaderCache ---\n");
        auto& cache = metalsharp::ShaderCache::instance();
        std::string tmpDir = std::string(getenv("TMPDIR") ?: "/tmp") + "/metalsharp_perf_test";
        cache.init(tmpDir);

        const char* testData = "test shader data";
        uint64_t hash = metalsharp::ShaderCache::computeHash(
            reinterpret_cast<const uint8_t*>(testData), strlen(testData));
        CHECK(hash != 0, "Compute hash returns non-zero");

        uint64_t sameHash = metalsharp::ShaderCache::computeHash(
            reinterpret_cast<const uint8_t*>(testData), strlen(testData));
        CHECK(hash == sameHash, "Same input produces same hash");

        const char* otherData = "different data";
        uint64_t otherHash = metalsharp::ShaderCache::computeHash(
            reinterpret_cast<const uint8_t*>(otherData), strlen(otherData));
        CHECK(hash != otherHash, "Different input produces different hash");

        metalsharp::CachedShader lookup;
        bool found = cache.lookup(hash, lookup);
        CHECK(!found, "Lookup empty cache returns false");
        CHECK(cache.missCount() == 1, "Miss count incremented");
        CHECK(cache.hitCount() == 0, "Hit count is 0");

        metalsharp::CachedShader entry;
        entry.hash = hash;
        entry.mslSource = "#include <metal_stdlib>\nvertex float4 vertexShader() { return float4(0); }";
        entry.library = nullptr;
        entry.vertexFunction = nullptr;
        entry.fragmentFunction = nullptr;
        entry.computeFunction = nullptr;
        cache.store(hash, entry);
        CHECK(cache.entryCount() == 1, "Entry count is 1 after store");

        found = cache.lookup(hash, lookup);
        CHECK(found, "Lookup after store returns true");
        CHECK(lookup.hash == hash, "Retrieved hash matches");
        CHECK(lookup.mslSource == entry.mslSource, "Retrieved MSL source matches");
        CHECK(cache.hitCount() == 1, "Hit count incremented");

        CHECK(cache.saveToDisk(), "Save to disk succeeds");

        cache.clear();
        CHECK(cache.entryCount() == 0, "Clear empties cache");

        cache.shutdown();
    }

    {
        printf("\n--- PipelineCache ---\n");
        auto& cache = metalsharp::PipelineCache::instance();
        cache.init("/tmp/metalsharp_pipeline_test");

        void* result = cache.lookup(0x12345678);
        CHECK(result == nullptr, "Lookup empty cache returns nullptr");
        CHECK(cache.missCount() == 1, "Miss count incremented");

        uint64_t hash = metalsharp::PipelineCache::computeDescriptorHash("test", 4);
        CHECK(hash != 0, "Compute descriptor hash returns non-zero");

        cache.store(hash, reinterpret_cast<void*>(0xDEADBEEF), "test_pipeline");
        CHECK(cache.entryCount() == 1, "Entry count is 1");

        result = cache.lookup(hash);
        CHECK(result == reinterpret_cast<void*>(0xDEADBEEF), "Lookup returns stored value");
        CHECK(cache.hitCount() == 1, "Hit count incremented");

        cache.clear();
        CHECK(cache.entryCount() == 0, "Clear empties cache");

        cache.shutdown();
    }

    {
        printf("\n--- BufferPool ---\n");
        auto& pool = metalsharp::BufferPool::instance();

        CHECK(pool.activeBuffers() == 0, "No active buffers before init");
        CHECK(pool.pooledBuffers() == 0, "No pooled buffers before init");

        pool.shutdown();
    }

    {
        printf("\n--- FramePacer ---\n");
        auto& pacer = metalsharp::FramePacer::instance();
        pacer.init(60.0);

        CHECK(pacer.targetFPS() == 60.0, "Target FPS is 60");
        CHECK(pacer.isEnabled(), "Enabled by default");

        pacer.beginFrame();
        pacer.endFrame();

        auto timing = pacer.getTiming();
        CHECK(timing.targetFrameTime > 0, "Target frame time positive");
        CHECK(timing.frameCount == 1, "Frame count is 1");

        pacer.setTargetFPS(120.0);
        CHECK(pacer.targetFPS() == 120.0, "Target FPS updated to 120");

        pacer.setEnabled(false);
        CHECK(!pacer.isEnabled(), "Can be disabled");
        pacer.setEnabled(true);

        pacer.shutdown();
    }

    {
        printf("\n--- GPUProfiler ---\n");
        auto& profiler = metalsharp::GPUProfiler::instance();
        profiler.init();

        CHECK(!profiler.isEnabled(), "Disabled by default");
        profiler.setEnabled(true);
        CHECK(profiler.isEnabled(), "Can be enabled");

        profiler.beginFrame();
        profiler.beginPass("render");
        profiler.recordDraw("render", 36, 1);
        profiler.endPass("render");
        profiler.endFrame();

        auto stats = profiler.getLastFrameStats();
        CHECK(stats.frameTime >= 0, "Frame time non-negative");
        CHECK(stats.drawCalls == 1, "Draw call counted");
        CHECK(stats.triangles == 12, "Triangles counted (36/3)");
        CHECK(stats.frameIndex == 1, "Frame index is 1");

        profiler.beginFrame();
        profiler.beginPass("compute_pass");
        profiler.recordCompute("compute_pass", 8, 8, 1);
        profiler.endPass("compute_pass");
        profiler.endFrame();

        stats = profiler.getLastFrameStats();
        CHECK(stats.computeCalls == 1, "Compute call counted");

        auto avg = profiler.getAverageStats(2);
        CHECK(avg.frameTime > 0, "Average frame time positive");
        CHECK(avg.drawCalls == 0, "Average draw calls (0 from rounding)");
        CHECK(avg.frameIndex == 2, "Average frame index is 2");

        profiler.shutdown();
    }

    {
        printf("\n--- CommandBatcher ---\n");
        auto& batcher = metalsharp::CommandBatcher::instance();
        batcher.init();

        CHECK(batcher.batchSize() == 0, "Batch size is 0 initially");

        int executeCount = 0;
        batcher.setExecuteFunction([&executeCount](const metalsharp::BatchedCommand& cmd) {
            executeCount++;
        });

        batcher.beginFrame();

        metalsharp::BatchedCommand cmd;
        cmd.type = metalsharp::BatchedCommand::Draw;
        cmd.vertexCount = 3;
        cmd.instanceCount = 1;
        batcher.enqueue(cmd);

        CHECK(batcher.batchSize() == 1, "Batch size is 1 after enqueue");

        cmd.type = metalsharp::BatchedCommand::DrawIndexed;
        cmd.vertexCount = 36;
        batcher.enqueue(cmd);

        batcher.endFrame();
        CHECK(executeCount == 2, "Execute called 2 times after endFrame");
        CHECK(batcher.batchSize() == 0, "Batch cleared after endFrame");
        CHECK(batcher.totalFlushes() == 1, "Total flushes is 1");

        batcher.shutdown();
    }

    {
        printf("\n--- MetalFXUpscaler ---\n");
        auto* upscaler = metalsharp::MetalFXUpscaler::create(nullptr);
        CHECK(upscaler != nullptr, "Upscaler created");

        if (upscaler) {
            bool hasMetalFX = upscaler->isAvailable();
            printf("  [INFO] MetalFX available: %s\n", hasMetalFX ? "yes" : "no");

            if (hasMetalFX) {
                bool inited = upscaler->init(960, 540, 1920, 1080);
                CHECK(inited, "Upscaler init succeeds");
                CHECK(upscaler->inputWidth() == 960, "Input width correct");
                CHECK(upscaler->outputWidth() == 1920, "Output width correct");
            }

            upscaler->setSharpness(0.5f);
            CHECK(upscaler->sharpness() == 0.5f, "Sharpness set correctly");

            delete upscaler;
        }

        auto* interp = metalsharp::MetalFXInterpolator::create(nullptr);
        CHECK(interp != nullptr, "Interpolator created");
        delete interp;
    }

    {
        printf("\n--- Performance Integration ---\n");
        auto& pacer = metalsharp::FramePacer::instance();
        pacer.init(60.0);
        auto& profiler = metalsharp::GPUProfiler::instance();
        profiler.init();
        profiler.setEnabled(true);

        profiler.beginFrame();
        pacer.beginFrame();

        profiler.beginPass("main_render");
        profiler.recordDraw("main_render", 100, 1);
        profiler.endPass("main_render");

        pacer.endFrame();
        profiler.endFrame();

        auto stats = profiler.getLastFrameStats();
        auto timing = pacer.getTiming();
        CHECK(stats.frameIndex > 0, "Profiler has frame data");
        CHECK(timing.frameCount > 0, "Pacer has frame data");

        profiler.shutdown();
        pacer.shutdown();
    }

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
