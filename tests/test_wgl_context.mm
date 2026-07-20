/// @file test_wgl_context.mm
/// @brief Tests for WGLContextManager: pixel format decoding, context
/// creation/make-current/deletion, swap-interval plumbing, and the
/// Wine sentinel shortcut. All tests run against real AppKit
/// NSOpenGLContext so they confirm the bridge can drive a Metal-capable
/// OpenGL context.

#import <AppKit/AppKit.h>
#include <cstdio>
#include <metalsharp/WGLContextManager.h>

static int passed = 0;
static int failed = 0;

#define CHECK(cond, msg)                                                                                               \
    do {                                                                                                               \
        if (cond) {                                                                                                    \
            printf("  [OK] %s\n", msg);                                                                                \
            passed++;                                                                                                  \
        } else {                                                                                                       \
            printf("  [FAIL] %s\n", msg);                                                                              \
            failed++;                                                                                                  \
        }                                                                                                              \
    } while (0)

#define CHECK_EQ(a, b, msg)                                                                                            \
    do {                                                                                                               \
        auto _a = (a);                                                                                                 \
        auto _b = (b);                                                                                                 \
        if (_a == _b) {                                                                                                \
            printf("  [OK] %s\n", msg);                                                                                \
            passed++;                                                                                                  \
        } else {                                                                                                       \
            printf("  [FAIL] %s (%lld != %lld)\n", msg, static_cast<long long>(_a), static_cast<long long>(_b));       \
            failed++;                                                                                                  \
        }                                                                                                              \
    } while (0)

int main() {
    printf("=== WGL Context Tests ===\n\n");

    auto& mgr = metalsharp::WGLContextManager::instance();

    // Test pixel format decoding with default attribs
    {
        metalsharp::WGLPixelFormat fmt = mgr.choosePixelFormat(nullptr);
        CHECK(fmt.colorBits >= 24, "choosePixelFormat colorBits default >= 24");
        CHECK(fmt.depthBits == 24, "choosePixelFormat depthBits default == 24");
        CHECK(fmt.doubleBuffer, "choosePixelFormat doubleBuffer default == true");
    }

    // Test pixel format decoding with explicit attribs
    {
        const int attribs[] = {
            0x2010, 16, // WGL_COLOR_BITS_ARB
            0x2022, 16, // WGL_DEPTH_BITS_ARB
            0x2023, 0,  // WGL_STENCIL_BITS_ARB
            0x2011, 0,  // WGL_DOUBLE_BUFFER_ARB
            0,
        };
        metalsharp::WGLPixelFormat fmt = mgr.choosePixelFormat(attribs);
        CHECK_EQ(fmt.colorBits, 16u, "choosePixelFormat colorBits override == 16");
        CHECK_EQ(fmt.depthBits, 16u, "choosePixelFormat depthBits override == 16");
        CHECK_EQ(fmt.stencilBits, 0u, "choosePixelFormat stencilBits override == 0");
        CHECK(!fmt.doubleBuffer, "choosePixelFormat doubleBuffer override == false");
    }

    // Test context creation
    void* ctx = mgr.createContext(nullptr, nullptr);
    CHECK(ctx != nullptr, "createContext returned non-null");

    NSOpenGLContext* nsCtx = (__bridge NSOpenGLContext*)ctx;
    CHECK(nsCtx != nil, "createContext produces valid NSOpenGLContext");

    // Test makeCurrent
    bool ok = mgr.makeCurrent(nullptr, ctx);
    CHECK(ok, "makeCurrent succeeded");

    void* cur = mgr.getCurrentContext();
    CHECK(cur == ctx, "getCurrentContext matches");

    // Test makeCurrent(nullptr) clears
    mgr.makeCurrent(nullptr, nullptr);
    cur = mgr.getCurrentContext();
    CHECK(cur == nullptr, "makeCurrent(null) clears context");

    // Test createContextAttribs
    void* ctx2 = mgr.createContextAttribs(nullptr, nullptr, nullptr);
    CHECK(ctx2 != nullptr, "createContextAttribs returned non-null");

    // Test setSwapInterval — fails if no current context
    bool swapOk = mgr.setSwapInterval(1);
    CHECK(!swapOk, "setSwapInterval fails when no current context");

    // Make ctx2 current and try swap interval
    mgr.makeCurrent(nullptr, ctx2);
    swapOk = mgr.setSwapInterval(1);
    CHECK(swapOk, "setSwapInterval succeeds with current context");

    // Test clearing current state
    mgr.makeCurrent(nullptr, nullptr);

    // Test describePixelFormat stub
    uint32_t values = 0;
    bool descOk = mgr.describePixelFormat(metalsharp::WGLPixelFormat{}, 1, &values);
    CHECK(descOk, "describePixelFormat(1) returns true");

    descOk = mgr.describePixelFormat(metalsharp::WGLPixelFormat{}, 2, nullptr);
    CHECK(!descOk, "describePixelFormat(2) returns false");

    // Test delete
    bool delOk = mgr.deleteContext(ctx);
    CHECK(delOk, "deleteContext succeeded");
    delOk = mgr.deleteContext(ctx2);
    CHECK(delOk, "deleteContext(ctx2) succeeded");

    // Double-delete should return false
    delOk = mgr.deleteContext(ctx);
    CHECK(!delOk, "double delete returns false");

    // Test Wine sentinel short-circuit
    mgr.setRunningInWine(true);
    void* wineCtx = mgr.createContext(nullptr, nullptr);
    CHECK(wineCtx == reinterpret_cast<void*>(0x1), "createContext in Wine returns sentinel 0x1");
    CHECK(mgr.makeCurrent(nullptr, wineCtx), "makeCurrent in Wine succeeds");
    CHECK(mgr.deleteContext(wineCtx), "deleteContext in Wine succeeds");
    CHECK(mgr.setSwapInterval(1), "setSwapInterval in Wine succeeds");
    mgr.setRunningInWine(false);

    // Recreate after toggling off Wine mode
    void* ctx3 = mgr.createContext(nullptr, nullptr);
    CHECK(ctx3 != nullptr, "createContext after Wine toggle succeeds");
    mgr.deleteContext(ctx3);

    printf("\n=== Summary: %d passed, %d failed ===\n", passed, failed);
    return failed ? 1 : 0;
}
