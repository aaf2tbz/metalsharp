/// @file WGLShim.cpp
/// @brief WGL (Windows GL) function stubs for opengl32 shim.
///
/// Implements the subset of WGL that the opengl32.dll shim needs to satisfy
/// direct (non-Wine) callers. In practice, the Wine/Proton path has its own
/// WGL implementation that talks to macOS NSOpenGLContext; these stubs only
/// fire when a binary loads opengl32.dll directly without going through
/// Wine. We therefore return benign non-null sentinels rather than building
/// a full NSOpenGLContext here — Phase 4c can layer that in if needed.

#include <cstdint>
#include <metalsharp/OpenGLBridge.h>

extern "C" {

// Sentinel "context" handle returned from wglCreateContext. Non-null so
// callers that check the return value proceed; wglMakeCurrent is a no-op.
static void* const kWglSentinelContext = reinterpret_cast<void*>(0x1);

void* wglCreateContext(void* hdc) {
    // On macOS, GL contexts are created through NSOpenGLContext. Wine handles
    // context creation for Wine-launched binaries; this stub fires only for
    // direct opengl32.dll loading. Returning a sentinel is sufficient because
    // any real GL call would still fail without a live NSOpenGLContext, which
    // is out of scope for the structural Phase 4b framework.
    (void)hdc;
    return kWglSentinelContext;
}

int32_t wglMakeCurrent(void* hdc, void* hglrc) {
    // TODO(Phase 4c): wire to NSOpenGLContext -makeCurrentContext when running
    // outside Wine. For now, accept any non-null sentinel and report success.
    (void)hdc;
    (void)hglrc;
    return 1; // TRUE
}

int32_t wglDeleteContext(void* hglrc) {
    // TODO(Phase 4c): release the corresponding NSOpenGLContext if we ever
    // allocate one in wglCreateContext above. For Phase 4b we have nothing
    // to release.
    (void)hglrc;
    return 1; // TRUE
}

void* wglGetProcAddress(const char* name) {
    // Lightweight path: don't cache a bridge instance per call; the bridge
    // init is idempotent and guarded by std::once_flag internally for the
    // shim EntryPoint. Here we just construct a temporary bridge and let
    // the framework dlopen happen if it hasn't already. The cost is paid
    // once per process because dlsym(RTLD_NOW) on an already-loaded
    // framework is cheap.
    if (!name) {
        return nullptr;
    }
    metalsharp::OpenGLBridge bridge;
    bridge.init();
    return bridge.getGLProcAddress(name);
}

// wglShareLists is not supported on macOS Core GL — return FALSE so callers
// know their context-sharing request was rejected rather than silently dropped.
int32_t wglShareLists(void* hglrc1, void* hglrc2) {
    (void)hglrc1;
    (void)hglrc2;
    return 0; // FALSE — share lists not supported
}

} // extern "C"