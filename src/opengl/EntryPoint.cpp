/// @file EntryPoint.cpp
/// @brief opengl32.dll shim exports for MetalSharp GL→Metal bridge.
///
/// Exports all commonly-used OpenGL 1.0-2.1 entry points. The shim is thin:
/// each function delegates through OpenGLBridge::getGLProcAddress() to the
/// macOS native OpenGL framework. Phase 4b focuses on correctness over
/// completeness — games that stick to GL 1.x/2.x immediate mode and fixed
/// pipeline will run unmodified.
///
/// Extension functions (GL 3.x/4.x, e.g. glCreateShader, glGenBuffers,
/// glBindBuffer) are resolved at runtime by Wine/wglGetProcAddress from
/// the framework. Full GL 3.x/4.x shader translation via SPIRV-Cross →
/// MSL is scaffolded for Phase 4c; the OpenGLBridge::GLState tracker
/// already captures program/blend/depth/attrib/viewport so future
/// instrumentation can be added without breaking this shim.

#include <metalsharp/OpenGLBridge.h>
#include <mutex>

namespace {

metalsharp::OpenGLBridge g_glBridge;
std::once_flag g_glInitFlag;

void ensureGLInit() {
    std::call_once(g_glInitFlag, [] { g_glBridge.init(); });
}

// Variadic dispatch: forwards to a native GL function resolved by name and
// returns a default-constructed value if the framework is not loaded.
// This is the dispatch core used by every GL_PASSTHROUGH macro.
template <typename Ret, typename... Args> Ret glDispatch(const char* name, Args... args) {
    ensureGLInit();
    auto fn = reinterpret_cast<Ret (*)(Args...)>(g_glBridge.getGLProcAddress(name));
    if (fn) {
        return fn(static_cast<Args>(args)...);
    }
    return Ret();
}

// Per-arity macros. We need separate macros for each arity because variadic
// macros can't "spread" parenthesised argument lists inside another function
// call (e.g. `f((a, b))` is the comma operator, not two arguments). The 0-arg
// variant takes no signature/args and is the simplest case.

#define GL_PASSTHROUGH0(ret, name)                                                                                     \
    extern "C" ret name() {                                                                                            \
        return glDispatch<ret>(#name);                                                                                 \
    }

#define GL_PASSTHROUGH1(ret, name, T1, a1)                                                                             \
    extern "C" ret name(T1 a1) {                                                                                       \
        return glDispatch<ret, T1>(#name, (a1));                                                                       \
    }

#define GL_PASSTHROUGH2(ret, name, T1, a1, T2, a2)                                                                     \
    extern "C" ret name(T1 a1, T2 a2) {                                                                                \
        return glDispatch<ret, T1, T2>(#name, (a1), (a2));                                                             \
    }

#define GL_PASSTHROUGH3(ret, name, T1, a1, T2, a2, T3, a3)                                                             \
    extern "C" ret name(T1 a1, T2 a2, T3 a3) {                                                                         \
        return glDispatch<ret, T1, T2, T3>(#name, (a1), (a2), (a3));                                                   \
    }

#define GL_PASSTHROUGH4(ret, name, T1, a1, T2, a2, T3, a3, T4, a4)                                                     \
    extern "C" ret name(T1 a1, T2 a2, T3 a3, T4 a4) {                                                                  \
        return glDispatch<ret, T1, T2, T3, T4>(#name, (a1), (a2), (a3), (a4));                                         \
    }

#define GL_PASSTHROUGH5(ret, name, T1, a1, T2, a2, T3, a3, T4, a4, T5, a5)                                             \
    extern "C" ret name(T1 a1, T2 a2, T3 a3, T4 a4, T5 a5) {                                                           \
        return glDispatch<ret, T1, T2, T3, T4, T5>(#name, (a1), (a2), (a3), (a4), (a5));                               \
    }

#define GL_PASSTHROUGH6(ret, name, T1, a1, T2, a2, T3, a3, T4, a4, T5, a5, T6, a6)                                     \
    extern "C" ret name(T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6) {                                                    \
        return glDispatch<ret, T1, T2, T3, T4, T5, T6>(#name, (a1), (a2), (a3), (a4), (a5), (a6));                     \
    }

#define GL_PASSTHROUGH7(ret, name, T1, a1, T2, a2, T3, a3, T4, a4, T5, a5, T6, a6, T7, a7)                             \
    extern "C" ret name(T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7) {                                             \
        return glDispatch<ret, T1, T2, T3, T4, T5, T6, T7>(#name, (a1), (a2), (a3), (a4), (a5), (a6), (a7));           \
    }

#define GL_PASSTHROUGH9(ret, name, T1, a1, T2, a2, T3, a3, T4, a4, T5, a5, T6, a6, T7, a7, T8, a8, T9, a9)             \
    extern "C" ret name(T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9) {                               \
        return glDispatch<ret, T1, T2, T3, T4, T5, T6, T7, T8, T9>(#name, (a1), (a2), (a3), (a4), (a5), (a6), (a7),    \
                                                                   (a8), (a9));                                        \
    }

} // namespace

// ---------------------------------------------------------------------------
// Vertex / immediate-mode pipeline (GL 1.0/1.1)
// ---------------------------------------------------------------------------
GL_PASSTHROUGH1(void, glBegin, uint32_t, mode)
GL_PASSTHROUGH0(void, glEnd)

// ---------------------------------------------------------------------------
// Buffers / state
// ---------------------------------------------------------------------------
GL_PASSTHROUGH1(void, glClear, uint32_t, mask)
GL_PASSTHROUGH4(void, glClearColor, float, r, float, g, float, b, float, a)
GL_PASSTHROUGH4(void, glViewport, int32_t, x, int32_t, y, int32_t, w, int32_t, h)
GL_PASSTHROUGH1(void, glEnable, uint32_t, cap)
GL_PASSTHROUGH1(void, glDisable, uint32_t, cap)
GL_PASSTHROUGH2(void, glBlendFunc, uint32_t, sfactor, uint32_t, dfactor)
GL_PASSTHROUGH1(void, glDepthFunc, uint32_t, func)
GL_PASSTHROUGH2(void, glBindTexture, uint32_t, target, uint32_t, texture)

// ---------------------------------------------------------------------------
// Buffer objects (GL 1.5)
// ---------------------------------------------------------------------------
GL_PASSTHROUGH2(void, glGenBuffers, int32_t, n, uint32_t*, buffers)
GL_PASSTHROUGH2(void, glDeleteBuffers, int32_t, n, const uint32_t*, buffers)
GL_PASSTHROUGH4(void, glBufferData, uint32_t, target, int64_t, size, const void*, data, uint32_t, usage)
GL_PASSTHROUGH4(void, glBufferSubData, uint32_t, target, int64_t, offset, int64_t, size, const void*, data)
GL_PASSTHROUGH2(void*, glMapBuffer, uint32_t, target, uint32_t, access)
GL_PASSTHROUGH1(unsigned char, glUnmapBuffer, uint32_t, target)
GL_PASSTHROUGH1(unsigned char, glIsBuffer, uint32_t, buffer)

// glBindBuffer is hand-written because it must mirror the binding into
// GLState so subsequent draw calls / VAO setup can observe which buffer
// is currently bound. The native call is still issued so the framework
// context state stays in sync.
extern "C" void glBindBuffer(uint32_t target, uint32_t buffer) {
    ensureGLInit();
    auto fn = reinterpret_cast<void (*)(uint32_t, uint32_t)>(g_glBridge.getGLProcAddress("glBindBuffer"));
    if (fn) {
        fn(target, buffer);
    }
    // Mirror into the state tracker for GL_ARRAY_BUFFER / GL_ELEMENT_ARRAY_BUFFER.
    // Other targets (e.g. GL_PIXEL_PACK_BUFFER, GL_UNIFORM_BUFFER) are ignored
    // here; the bridge only tracks the two targets consumed by draw submission.
    constexpr uint32_t kGL_ARRAY_BUFFER = 0x8892;         // GL_ARRAY_BUFFER
    constexpr uint32_t kGL_ELEMENT_ARRAY_BUFFER = 0x8893; // GL_ELEMENT_ARRAY_BUFFER
    if (target == kGL_ARRAY_BUFFER) {
        g_glBridge.state().boundArrayBuffer = buffer;
    } else if (target == kGL_ELEMENT_ARRAY_BUFFER) {
        g_glBridge.state().boundElementArrayBuffer = buffer;
    }
}

// ---------------------------------------------------------------------------
// Draw submission
// ---------------------------------------------------------------------------
GL_PASSTHROUGH3(void, glDrawArrays, uint32_t, mode, int32_t, first, int32_t, count)
GL_PASSTHROUGH4(void, glDrawElements, uint32_t, mode, int32_t, count, uint32_t, type, const void*, indices)

// ---------------------------------------------------------------------------
// Client-side vertex arrays (legacy immediate-mode interop)
// ---------------------------------------------------------------------------
GL_PASSTHROUGH4(void, glVertexPointer, int32_t, size, uint32_t, type, int32_t, stride, const void*, ptr)
GL_PASSTHROUGH4(void, glTexCoordPointer, int32_t, size, uint32_t, type, int32_t, stride, const void*, ptr)
GL_PASSTHROUGH4(void, glColorPointer, int32_t, size, uint32_t, type, int32_t, stride, const void*, ptr)
GL_PASSTHROUGH3(void, glNormalPointer, uint32_t, type, int32_t, stride, const void*, ptr)

// ---------------------------------------------------------------------------
// Vertex attributes (GL 2.0)
// ---------------------------------------------------------------------------

// glEnableVertexAttribArray / glDisableVertexAttribArray are hand-written
// because they must mirror the per-index enable state into GLState so
// subsequent draw calls can observe which attribute streams are active.
// The native call is still issued so the framework context state stays
// in sync with the shim's view.
extern "C" void glEnableVertexAttribArray(uint32_t index) {
    ensureGLInit();
    auto fn = reinterpret_cast<void (*)(uint32_t)>(g_glBridge.getGLProcAddress("glEnableVertexAttribArray"));
    if (fn) {
        fn(index);
    }
    if (index < metalsharp::kMaxVertexAttribs) {
        g_glBridge.state().vertexAttribEnabled[index] = true;
    }
}

extern "C" void glDisableVertexAttribArray(uint32_t index) {
    ensureGLInit();
    auto fn = reinterpret_cast<void (*)(uint32_t)>(g_glBridge.getGLProcAddress("glDisableVertexAttribArray"));
    if (fn) {
        fn(index);
    }
    if (index < metalsharp::kMaxVertexAttribs) {
        g_glBridge.state().vertexAttribEnabled[index] = false;
    }
}

GL_PASSTHROUGH6(void, glVertexAttribPointer, uint32_t, index, int32_t, size, uint32_t, type, unsigned char, normalized,
                int32_t, stride, const void*, pointer)
GL_PASSTHROUGH2(void, glVertexAttrib1f, uint32_t, index, float, v0)
GL_PASSTHROUGH3(void, glVertexAttrib2f, uint32_t, index, float, v0, float, v1)
GL_PASSTHROUGH4(void, glVertexAttrib3f, uint32_t, index, float, v0, float, v1, float, v2)
GL_PASSTHROUGH5(void, glVertexAttrib4f, uint32_t, index, float, v0, float, v1, float, v2, float, v3)
GL_PASSTHROUGH3(void, glGetVertexAttribiv, uint32_t, index, uint32_t, pname, int32_t*, params)
GL_PASSTHROUGH3(void, glGetVertexAttribPointerv, uint32_t, index, uint32_t, pname, void**, pointer)
GL_PASSTHROUGH2(void, glVertexAttribDivisor, uint32_t, index, uint32_t, divisor)

// ---------------------------------------------------------------------------
// Matrix stack (fixed pipeline, GL 1.0)
// ---------------------------------------------------------------------------
GL_PASSTHROUGH1(void, glMatrixMode, uint32_t, mode)
GL_PASSTHROUGH0(void, glLoadIdentity)
GL_PASSTHROUGH6(void, glOrtho, double, l, double, r, double, b, double, t, double, n, double, f)
GL_PASSTHROUGH6(void, glFrustum, double, l, double, r, double, b, double, t, double, n, double, f)
GL_PASSTHROUGH0(void, glPushMatrix)
GL_PASSTHROUGH0(void, glPopMatrix)
GL_PASSTHROUGH3(void, glTranslatef, float, x, float, y, float, z)
GL_PASSTHROUGH4(void, glRotatef, float, angle, float, x, float, y, float, z)
GL_PASSTHROUGH3(void, glScalef, float, x, float, y, float, z)

// ---------------------------------------------------------------------------
// Color (legacy)
// ---------------------------------------------------------------------------
GL_PASSTHROUGH3(void, glColor3f, float, r, float, g, float, b)
GL_PASSTHROUGH4(void, glColor4f, float, r, float, g, float, b, float, a)

// ---------------------------------------------------------------------------
// Texture upload / readback
// ---------------------------------------------------------------------------
GL_PASSTHROUGH9(void, glTexImage2D, uint32_t, target, int32_t, level, int32_t, internalFormat, int32_t, w, int32_t, h,
                int32_t, border, uint32_t, format, uint32_t, type, const void*, data)
GL_PASSTHROUGH7(void, glReadPixels, int32_t, x, int32_t, y, int32_t, w, int32_t, h, uint32_t, format, uint32_t, type,
                void*, data)

// ---------------------------------------------------------------------------
// Info queries (return non-default values — declared by hand instead of
// via GL_PASSTHROUGH).
// ---------------------------------------------------------------------------
extern "C" const uint8_t* glGetString(uint32_t name) {
    ensureGLInit();
    auto fn = reinterpret_cast<const uint8_t* (*)(uint32_t)>(g_glBridge.getGLProcAddress("glGetString"));
    if (fn) {
        return fn(name);
    }
    // Empty string sentinel; safe to return from glGetString.
    return reinterpret_cast<const uint8_t*>("");
}

extern "C" void glGetIntegerv(uint32_t pname, int32_t* params) {
    ensureGLInit();
    if (!params) {
        return;
    }
    auto fn = reinterpret_cast<void (*)(uint32_t, int32_t*)>(g_glBridge.getGLProcAddress("glGetIntegerv"));
    if (fn) {
        fn(pname, params);
    } else {
        // Best-effort default. Return zero so callers don't read garbage.
        *params = 0;
    }
}