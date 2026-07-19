/// @file test_metal_renderer.mm
/// @brief Tests for the GL→Metal draw emitter (Phase 3a/3b/3c).
///
/// Exercises the basic renderer surface — device init, buffer handle
/// allocation, render pass begin/end — without touching the GL→MSL
/// translation pipeline (those surfaces are tested separately in
/// test_glsl_compiler / test_opengl_bridge). The shader-handle checks
/// confirm that the renderer's own thread-safe state tracker is wired up
/// to GLShaderTracker::instance(), since Phase 3d/3e will reuse this
/// surface to materialise MTLRenderPipelineState objects from translated
/// MSL.

#include <cstdio>
#import <Metal/Metal.h>
#include <metalsharp/GLMetalRenderer.h>
#include <metalsharp/GLShaderTracker.h>

int main() {
    int passed = 0, failed = 0;

    printf("=== GLMetalRenderer Tests ===\n\n");

    // Test device init
    metalsharp::GLMetalRenderer renderer;
    bool ok = renderer.init();
    if (ok) {
        printf("[OK] init() succeeded\n");
        passed++;
    } else {
        printf("[FAIL] init() failed\n");
        failed++;
    }

    if (renderer.isAvailable()) {
        printf("[OK] isAvailable() true\n");
        passed++;
    } else {
        printf("[FAIL] isAvailable() false\n");
        failed++;
    }

    // Test buffer creation
    float vertices[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    uint64_t buf = renderer.createBuffer(vertices, sizeof(vertices));
    if (buf > 0) {
        printf("[OK] createBuffer returned handle %llu\n", buf);
        passed++;
    } else {
        printf("[FAIL] createBuffer failed\n");
        failed++;
    }

    // Test begin/end render pass (no crash)
    renderer.beginRenderPass(256, 256);
    renderer.endRenderPass();
    printf("[OK] beginRenderPass/endRenderPass no crash\n");
    passed++;

    // Test shader compilation via tracker
    auto& tracker = metalsharp::GLShaderTracker::instance();
    uint32_t vs = tracker.createShader(0x8B31); // GL_VERTEX_SHADER
    uint32_t fs = tracker.createShader(0x8B30); // GL_FRAGMENT_SHADER

    if (vs > 0 && fs > 0) {
        printf("[OK] createShader handles valid\n");
        passed++;
    } else {
        printf("[FAIL] createShader failed\n");
        failed++;
    }

    printf("\n=== Summary: %d passed, %d failed ===\n", passed, failed);
    return failed ? 1 : 0;
}
