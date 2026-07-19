/// @file test_opengl_bridge.cpp
/// @brief Tests for the OpenGL bridge framework (Phase 4b).
///
/// Validates that the OpenGL bridge can be initialized, that the GL state
/// tracker has sensible defaults, that function-pointer lookup is well-
/// behaved for both known and unknown names, and that the GL version
/// reported by the bridge matches the macOS native framework (GL 2.1).

#include <cstdio>
#include <cstring>
#include <metalsharp/OpenGLBridge.h>

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

int main() {
    printf("=== OpenGLBridge Tests ===\n\n");

    {
        printf("--- GLVersion enum ---\n");
        CHECK(static_cast<uint32_t>(metalsharp::GLVersion::None) == 0, "GLVersion::None is 0");
        CHECK(static_cast<uint32_t>(metalsharp::GLVersion::GL21) == 21, "GLVersion::GL21 is 21");
        CHECK(static_cast<uint32_t>(metalsharp::GLVersion::GL30) == 30, "GLVersion::GL30 is 30");
        CHECK(static_cast<uint32_t>(metalsharp::GLVersion::GL33) == 33, "GLVersion::GL33 is 33");
        CHECK(static_cast<uint32_t>(metalsharp::GLVersion::GL40) == 40, "GLVersion::GL40 is 40");
        CHECK(static_cast<uint32_t>(metalsharp::GLVersion::GL46) == 46, "GLVersion::GL46 is 46");
    }

    {
        printf("\n--- GLState defaults ---\n");
        metalsharp::GLState state{};
        CHECK(state.version == metalsharp::GLVersion::None, "Default GLState.version is None");
        CHECK(state.currentProgram == 0, "Default currentProgram is 0");
        CHECK(!state.shaderCompilePending, "Default shaderCompilePending is false");
        CHECK(!state.blendEnabled, "Default blendEnabled is false");
        CHECK(state.blendSrcRGB == 0, "Default blendSrcRGB is 0");
        CHECK(state.blendDstRGB == 0, "Default blendDstRGB is 0");
        CHECK(!state.depthTestEnabled, "Default depthTestEnabled is false");
        CHECK(state.depthFunc == 0, "Default depthFunc is 0");
        CHECK(state.boundTexture2D == 0, "Default boundTexture2D is 0");
        CHECK(state.boundFramebuffer == 0, "Default boundFramebuffer is 0");
        CHECK(state.boundArrayBuffer == 0, "Default boundArrayBuffer is 0");
        CHECK(state.boundElementArrayBuffer == 0, "Default boundElementArrayBuffer is 0");
        CHECK(state.viewportX == 0, "Default viewportX is 0");
        CHECK(state.viewportY == 0, "Default viewportY is 0");
        CHECK(state.viewportWidth == 0, "Default viewportWidth is 0");
        CHECK(state.viewportHeight == 0, "Default viewportHeight is 0");
        for (uint32_t i = 0; i < metalsharp::kMaxVertexAttribs; ++i) {
            if (state.vertexAttribEnabled[i]) {
                printf("  [FAIL] vertexAttribEnabled[%u] should default to false\n", i);
                failed++;
            }
        }
        passed++; // count the loop check as one assertion
        printf("  [OK] All kMaxVertexAttribs vertexAttribEnabled default to false\n");
    }

    {
        printf("\n--- GLState write/read ---\n");
        metalsharp::GLState state{};
        state.version = metalsharp::GLVersion::GL21;
        state.currentProgram = 42;
        state.shaderCompilePending = true;
        state.blendEnabled = true;
        state.blendSrcRGB = 1;
        state.blendDstRGB = 2;
        state.depthTestEnabled = true;
        state.depthFunc = 0x0201; // GL_LESS
        state.boundTexture2D = 7;
        state.boundFramebuffer = 9;
        state.viewportX = 10;
        state.viewportY = 20;
        state.viewportWidth = 800;
        state.viewportHeight = 600;
        state.vertexAttribEnabled[3] = true;
        state.vertexAttribEnabled[7] = true;

        CHECK(state.version == metalsharp::GLVersion::GL21, "Assigned version persists");
        CHECK(state.currentProgram == 42, "Assigned currentProgram persists");
        CHECK(state.shaderCompilePending, "Assigned shaderCompilePending persists");
        CHECK(state.blendEnabled, "Assigned blendEnabled persists");
        CHECK(state.blendSrcRGB == 1, "Assigned blendSrcRGB persists");
        CHECK(state.blendDstRGB == 2, "Assigned blendDstRGB persists");
        CHECK(state.depthTestEnabled, "Assigned depthTestEnabled persists");
        CHECK(state.depthFunc == 0x0201, "Assigned depthFunc persists");
        CHECK(state.boundTexture2D == 7, "Assigned boundTexture2D persists");
        CHECK(state.boundFramebuffer == 9, "Assigned boundFramebuffer persists");
        CHECK(state.viewportWidth == 800, "Assigned viewportWidth persists");
        CHECK(state.viewportHeight == 600, "Assigned viewportHeight persists");
        CHECK(state.vertexAttribEnabled[3], "vertexAttribEnabled[3] persists");
        CHECK(state.vertexAttribEnabled[7], "vertexAttribEnabled[7] persists");
        CHECK(!state.vertexAttribEnabled[0], "vertexAttribEnabled[0] still false");
        CHECK(!state.vertexAttribEnabled[15], "vertexAttribEnabled[15] still false");
    }

    {
        printf("\n--- OpenGLBridge lifecycle ---\n");
        metalsharp::OpenGLBridge bridge;
        // State should be default before init.
        CHECK(bridge.state().version == metalsharp::GLVersion::None, "Pre-init state().version is None");
        CHECK(!bridge.hasNativeGL(), "Pre-init hasNativeGL() is false");

        bool ok = bridge.init();
        CHECK(ok, "init() succeeds on macOS (OpenGL framework is available)");
        CHECK(bridge.hasNativeGL(), "hasNativeGL() is true after init()");
        CHECK(bridge.state().version == metalsharp::GLVersion::GL21,
              "state().version is GL21 after init() (macOS legacy profile)");

        // Idempotency: second init() should be safe.
        bool ok2 = bridge.init();
        CHECK(ok2, "Second init() call is also successful (idempotent)");
        CHECK(bridge.hasNativeGL(), "hasNativeGL() still true after re-init");
    }

    {
        printf("\n--- getGLProcAddress ---\n");
        metalsharp::OpenGLBridge bridge;
        bridge.init();

        // Known symbol: glClear is part of GL 1.0 and must be exported by
        // the macOS OpenGL framework.
        void* clearFn = bridge.getGLProcAddress("glClear");
        CHECK(clearFn != nullptr, "getGLProcAddress(\"glClear\") returns non-null");

        void* clearColorFn = bridge.getGLProcAddress("glClearColor");
        CHECK(clearColorFn != nullptr, "getGLProcAddress(\"glClearColor\") returns non-null");

        // Unknown symbol: should return nullptr, not crash.
        void* bogusFn = bridge.getGLProcAddress("this_symbol_does_not_exist_xyzzy");
        CHECK(bogusFn == nullptr, "Unknown symbol returns nullptr");

        // NULL name should return nullptr (defensive).
        void* nullFn = bridge.getGLProcAddress(nullptr);
        CHECK(nullFn == nullptr, "nullptr name returns nullptr");
    }

    {
        printf("\n--- Buffer object passthroughs ---\n");
        metalsharp::OpenGLBridge bridge;
        bool ok = bridge.init();
        CHECK(ok, "init() succeeds for buffer-object tests");

        // The GL 1.5 buffer-object entry points should all resolve through
        // the macOS OpenGL framework, which exports the full legacy profile.
        const char* bufferSymbols[] = {
            "glGenBuffers", "glBindBuffer",  "glBufferData", "glBufferSubData",
            "glMapBuffer",  "glUnmapBuffer", "glIsBuffer",   "glDeleteBuffers",
        };
        for (const char* name : bufferSymbols) {
            char msg[96];
            std::snprintf(msg, sizeof(msg), "getGLProcAddress(\"%s\") returns non-null", name);
            void* fn = bridge.getGLProcAddress(name);
            CHECK(fn != nullptr, msg);
        }

        // After a successful init, the buffer-binding fields on the state
        // tracker must be zero by default — the framework reports nothing
        // is bound until the application calls glBindBuffer.
        CHECK(bridge.state().boundArrayBuffer == 0, "Default state().boundArrayBuffer is 0 after init");
        CHECK(bridge.state().boundElementArrayBuffer == 0, "Default state().boundElementArrayBuffer is 0 after init");
    }

    {
        printf("\n--- kMaxTextureUnits / kMaxVertexAttribs ---\n");
        CHECK(metalsharp::kMaxTextureUnits >= 16, "kMaxTextureUnits is at least 16");
        CHECK(metalsharp::kMaxVertexAttribs >= 16, "kMaxVertexAttribs is at least 16");
        // Standard GL 2.1 minimums.
        CHECK(metalsharp::kMaxTextureUnits == 32, "kMaxTextureUnits is 32");
        CHECK(metalsharp::kMaxVertexAttribs == 16, "kMaxVertexAttribs is 16");
    }

    {
        printf("\n--- Vertex attribute passthroughs ---\n");
        metalsharp::OpenGLBridge bridge;
        bool ok = bridge.init();
        CHECK(ok, "init() succeeds for vertex-attribute tests");

        // Symbol resolution: every GL 2.0 vertex-attribute entry point must
        // resolve through the macOS OpenGL framework, which exports the full
        // legacy profile (including GL 2.0 generic vertex attributes).
        const char* vertexAttribSymbols[] = {
            "glEnableVertexAttribArray", "glDisableVertexAttribArray", "glVertexAttribPointer", "glVertexAttrib1f",
            "glVertexAttrib2f",          "glVertexAttrib3f",           "glVertexAttrib4f",      "glGetVertexAttribiv",
            "glGetVertexAttribPointerv", "glVertexAttribDivisor",
        };
        for (const char* name : vertexAttribSymbols) {
            char msg[96];
            std::snprintf(msg, sizeof(msg), "getGLProcAddress(\"%s\") returns non-null", name);
            void* fn = bridge.getGLProcAddress(name);
            CHECK(fn != nullptr, msg);
        }

        // State tracking: after init, no vertex attribute is enabled by default.
        bool allDisabled = true;
        for (uint32_t i = 0; i < metalsharp::kMaxVertexAttribs; ++i) {
            if (bridge.state().vertexAttribEnabled[i]) {
                allDisabled = false;
                printf("  [FAIL] vertexAttribEnabled[%u] should default to false after init\n", i);
                failed++;
                break;
            }
        }
        if (allDisabled) {
            printf("  [OK] All kMaxVertexAttribs vertexAttribEnabled default to false after init\n");
            passed++;
        }

        // State tracking: init must not affect the buffer-binding state tracker;
        // those fields stay zero until the application calls glBindBuffer.
        CHECK(bridge.state().boundArrayBuffer == 0, "state().boundArrayBuffer is still 0 after init");
        CHECK(bridge.state().boundElementArrayBuffer == 0, "state().boundElementArrayBuffer is still 0 after init");
    }

    {
        printf("\n--- Shader program passthroughs ---\n");
        metalsharp::OpenGLBridge bridge;
        bool ok = bridge.init();
        CHECK(ok, "init() succeeds for shader-program tests");

        // Symbol resolution: every GL 2.0 shader-program entry point must
        // resolve through the macOS OpenGL framework, which exports the full
        // legacy profile (including GL 2.0 program objects).
        const char* programSymbols[] = {
            "glCreateProgram", "glDeleteProgram", "glLinkProgram", "glUseProgram", "glValidateProgram", "glIsProgram",
        };
        for (const char* name : programSymbols) {
            char msg[96];
            std::snprintf(msg, sizeof(msg), "getGLProcAddress(\"%s\") returns non-null", name);
            void* fn = bridge.getGLProcAddress(name);
            CHECK(fn != nullptr, msg);
        }

        // State tracking: after init, no program is bound and no shader
        // compile is pending — the framework reports a clean default.
        CHECK(bridge.state().currentProgram == 0, "Default state().currentProgram is 0 after init");
        CHECK(!bridge.state().shaderCompilePending, "Default state().shaderCompilePending is false after init");
    }

    {
        printf("\n--- Shader object passthroughs ---\n");
        metalsharp::OpenGLBridge bridge;
        bool ok = bridge.init();
        CHECK(ok, "init() succeeds for shader-object tests");

        // Symbol resolution: every GL 2.0 shader-object entry point must
        // resolve through the macOS OpenGL framework, which exports the full
        // legacy profile (including GL 2.0 shader objects and info-log queries).
        const char* shaderSymbols[] = {
            "glCreateShader", "glDeleteShader",     "glShaderSource", "glCompileShader",
            "glGetShaderiv",  "glGetShaderInfoLog", "glGetProgramiv", "glGetProgramInfoLog",
            "glAttachShader", "glDetachShader",     "glIsShader",
        };
        for (const char* name : shaderSymbols) {
            char msg[96];
            std::snprintf(msg, sizeof(msg), "getGLProcAddress(\"%s\") returns non-null", name);
            void* fn = bridge.getGLProcAddress(name);
            CHECK(fn != nullptr, msg);
        }

        // State tracking: init must leave shaderCompilePending false; the
        // bridge starts in a quiescent state with no compile in flight.
        CHECK(!bridge.state().shaderCompilePending, "Default state().shaderCompilePending is false after init");
    }

    {
        printf("\n--- Uniform passthroughs ---\n");
        metalsharp::OpenGLBridge bridge;
        bool ok = bridge.init();
        CHECK(ok, "init() succeeds for uniform passthroughs");

        // Symbol resolution: every GL 2.0 uniform entry point must resolve
        // through the macOS OpenGL framework, which exports the full legacy
        // profile (including GL 2.0 uniform variables and uniforms queries).
        const char* uniformSymbols[] = {
            "glGetUniformLocation", "glUniform1f",        "glUniform2f",    "glUniform3f",    "glUniform4f",
            "glUniform1i",          "glUniform2i",        "glUniform3i",    "glUniform4i",    "glUniformMatrix2fv",
            "glUniformMatrix3fv",   "glUniformMatrix4fv", "glGetUniformfv", "glGetUniformiv", "glGetActiveUniform",
        };
        for (const char* name : uniformSymbols) {
            char msg[96];
            std::snprintf(msg, sizeof(msg), "getGLProcAddress(\"%s\") returns non-null", name);
            void* fn = bridge.getGLProcAddress(name);
            CHECK(fn != nullptr, msg);
        }

        // State tracking: init must not have left a program bound. Even
        // though the uniform entry points read/write through the active
        // program implicitly, the shim must leave currentProgram at 0
        // unless the application explicitly calls glUseProgram.
        CHECK(bridge.state().currentProgram == 0, "Default state().currentProgram is 0 after init");
    }

    printf("\n=== Summary: %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}