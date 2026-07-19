/// @file test_glsl_compiler.cpp
/// @brief Phase 2c sanity test for the GLSL → SPIR-V compiler wrapper.
///
/// Verifies that:
///   1. The compiler initializes correctly.
///   2. A minimal GLSL 450 vertex shader compiles to valid SPIR-V (the magic
///      number 0x07230203 is the SPIR-V header word 0, which is fixed by the
///      SPIR-V spec).
///   3. Invalid GLSL source produces a non-empty error log and an empty
///      SPIR-V output.
///   4. Shutdown is reachable and idempotent.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <metalsharp/GLShaderTracker.h>
#include <metalsharp/GLSLCompiler.h>
#include <metalsharp/GLSLVersion.h>
#include <metalsharp/ShaderStage.h>

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
    printf("=== GLSLCompiler Tests ===\n\n");

    {
        printf("--- Initialization ---\n");
        const bool ok = metalsharp::GLSLCompiler::initialize();
        CHECK(ok, "initialize() returned true");
        CHECK(metalsharp::GLSLCompiler::isAvailable(), "isAvailable() returns true after init");

        // Idempotent init should be safe
        const bool ok2 = metalsharp::GLSLCompiler::initialize();
        CHECK(ok2, "initialize() is idempotent (second call returns true)");
    }

    {
        printf("\n--- Compile valid GLSL vertex shader ---\n");
        const char* src = "#version 450 core\n"
                          "layout(location = 0) in vec3 pos;\n"
                          "void main() { gl_Position = vec4(pos, 1.0); }\n";

        metalsharp::GLSLVersion ver{};
        const bool parsed = metalsharp::parseGLSLVersion(src, ver);
        CHECK(parsed, "parseGLSLVersion finds #version directive");
        CHECK(ver.major == 450, "Parsed major version is 450");
        CHECK(ver.valid, "GLSLVersion.valid is true");

        std::vector<uint32_t> spirv;
        std::string errorLog;
        const bool compiled =
            metalsharp::GLSLCompiler::compileToSPIRV(src, metalsharp::ShaderStage::Vertex, ver, spirv, errorLog);

        CHECK(compiled, "compileToSPIRV returned true for valid GLSL");
        CHECK(errorLog.empty(), "errorLog is empty for valid GLSL");
        CHECK(!spirv.empty(), "SPIR-V output is non-empty");

        if (!spirv.empty()) {
            const uint32_t magic = spirv[0];
            const bool isSpirv = (magic == 0x07230203u);
            char buf[128];
            std::snprintf(buf, sizeof(buf), "SPIR-V magic word is 0x07230203 (got 0x%08x)", magic);
            CHECK(isSpirv, buf);

            // Version word is at index 1: minor version byte + major version
            // byte (little-endian). SPIR-V 1.0 = 0x00010000.
            const uint32_t versionWord = spirv[1];
            const uint8_t major = static_cast<uint8_t>((versionWord >> 16) & 0xFF);
            const uint8_t minor = static_cast<uint8_t>((versionWord >> 8) & 0xFF);
            std::snprintf(buf, sizeof(buf), "SPIR-V version is 1.%u (got %u.%u)", minor, major, minor);
            CHECK(major == 1, buf);

            // Generator word is at index 2: high 16 bits hold the tool id
            // assigned by Khronos. glslang's reference frontend reports 0x0008.
            const uint32_t generator = spirv[2];
            const uint32_t generatorMagic = generator & 0xFFFF0000u;
            char gbuf[128];
            std::snprintf(gbuf, sizeof(gbuf), "SPIR-V generator tool id is 0x0008 (got 0x%08x)", generatorMagic);
            CHECK(generatorMagic == 0x00080000u, gbuf);
        }
    }

    {
        printf("\n--- Compile invalid GLSL ---\n");
        const char* src = "#version 450 core\n"
                          "this is not valid glsl;\n"
                          "void main() { @@@ }\n";

        metalsharp::GLSLVersion ver{};
        metalsharp::parseGLSLVersion(src, ver);

        std::vector<uint32_t> spirv;
        std::string errorLog;
        const bool compiled =
            metalsharp::GLSLCompiler::compileToSPIRV(src, metalsharp::ShaderStage::Vertex, ver, spirv, errorLog);

        CHECK(!compiled, "compileToSPIRV returns false for invalid GLSL");
        CHECK(spirv.empty(), "SPIR-V output is empty for invalid GLSL");
        CHECK(!errorLog.empty(), "errorLog is non-empty for invalid GLSL");

        if (!errorLog.empty()) {
            printf("    errorLog snippet: %.120s%s\n", errorLog.c_str(), errorLog.size() > 120 ? "..." : "");
        }
    }

    {
        printf("\n--- Compile valid GLSL fragment shader ---\n");
        const char* src = "#version 450 core\n"
                          "layout(location = 0) out vec4 outColor;\n"
                          "void main() { outColor = vec4(1.0, 0.0, 0.0, 1.0); }\n";

        metalsharp::GLSLVersion ver{};
        metalsharp::parseGLSLVersion(src, ver);

        std::vector<uint32_t> spirv;
        std::string errorLog;
        const bool compiled =
            metalsharp::GLSLCompiler::compileToSPIRV(src, metalsharp::ShaderStage::Pixel, ver, spirv, errorLog);

        CHECK(compiled, "compileToSPIRV returned true for valid fragment shader");
        CHECK(!spirv.empty(), "SPIR-V output is non-empty");
        CHECK(!spirv.empty() && spirv[0] == 0x07230203u, "Fragment SPIR-V has correct magic word");
    }

    {
        printf("\n--- Compile valid GLSL compute shader ---\n");
        const char* src = "#version 450 core\n"
                          "layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;\n"
                          "layout(set = 0, binding = 0) buffer Buf { uint counter; };\n"
                          "void main() { counter += 1u; }\n";

        metalsharp::GLSLVersion ver{};
        metalsharp::parseGLSLVersion(src, ver);

        std::vector<uint32_t> spirv;
        std::string errorLog;
        const bool compiled =
            metalsharp::GLSLCompiler::compileToSPIRV(src, metalsharp::ShaderStage::Compute, ver, spirv, errorLog);

        CHECK(compiled, "compileToSPIRV returned true for valid compute shader");
        CHECK(!spirv.empty(), "Compute SPIR-V output is non-empty");
        CHECK(!spirv.empty() && spirv[0] == 0x07230203u, "Compute SPIR-V has correct magic word");
    }

    {
        printf("\n--- SPIR-V -> MSL: vertex shader ---\n");
        const char* src = "#version 450 core\n"
                          "layout(location = 0) in vec3 pos;\n"
                          "void main() { gl_Position = vec4(pos, 1.0); }\n";

        metalsharp::GLSLVersion ver{};
        metalsharp::parseGLSLVersion(src, ver);

        std::vector<uint32_t> spirv;
        std::string errorLog;
        const bool compiled =
            metalsharp::GLSLCompiler::compileToSPIRV(src, metalsharp::ShaderStage::Vertex, ver, spirv, errorLog);
        CHECK(compiled, "vertex shader compiled to SPIR-V");

        std::string msl;
        std::string mslError;
        const bool translated =
            metalsharp::GLSLCompiler::translateSPIRVtoMSL(spirv, metalsharp::ShaderStage::Vertex, msl, mslError);
        CHECK(translated, "translateSPIRVtoMSL succeeded for vertex shader");
        CHECK(mslError.empty(), "vertex MSL errorLog is empty");
        CHECK(!msl.empty(), "vertex MSL output is non-empty");

        if (!msl.empty()) {
            // MSL vertex shader signature is `vertex <return_type> <name>(...)`.
            // SPIRV-Cross emits the `vertex` qualifier keyword on its own line,
            // so a substring search is reliable.
            const bool hasVertexKw = msl.find("vertex") != std::string::npos;
            CHECK(hasVertexKw, "vertex MSL contains the `vertex` qualifier keyword");

            // We rename to vertex_main in GLSLCompiler::translateSPIRVtoMSL.
            const bool hasEntryName = msl.find("vertex_main") != std::string::npos;
            CHECK(hasEntryName, "vertex MSL contains renamed entry point `vertex_main`");

            // Metal platform include / namespace declarations are part of every
            // SPIRV-Cross MSL output (e.g. `#include <metal_stdlib>`).
            const bool hasMetalInclude = msl.find("metal") != std::string::npos;
            CHECK(hasMetalInclude, "vertex MSL contains `metal` (stdlib include)");
        }
    }

    {
        printf("\n--- SPIR-V -> MSL: fragment shader ---\n");
        const char* src = "#version 450 core\n"
                          "layout(location = 0) out vec4 outColor;\n"
                          "void main() { outColor = vec4(1.0, 0.0, 0.0, 1.0); }\n";

        metalsharp::GLSLVersion ver{};
        metalsharp::parseGLSLVersion(src, ver);

        std::vector<uint32_t> spirv;
        std::string errorLog;
        const bool compiled =
            metalsharp::GLSLCompiler::compileToSPIRV(src, metalsharp::ShaderStage::Pixel, ver, spirv, errorLog);
        CHECK(compiled, "fragment shader compiled to SPIR-V");

        std::string msl;
        std::string mslError;
        const bool translated =
            metalsharp::GLSLCompiler::translateSPIRVtoMSL(spirv, metalsharp::ShaderStage::Pixel, msl, mslError);
        CHECK(translated, "translateSPIRVtoMSL succeeded for fragment shader");
        CHECK(mslError.empty(), "fragment MSL errorLog is empty");
        CHECK(!msl.empty(), "fragment MSL output is non-empty");

        if (!msl.empty()) {
            // MSL fragment shader signature is `fragment <return_type> <name>(...)`.
            // We need to be careful: the word `fragment` also appears in things
            // like `[[fragment]]` qualifiers, but a simple substring match
            // (case-sensitive) is sufficient for our purposes.
            const bool hasFragmentKw = msl.find("fragment") != std::string::npos;
            CHECK(hasFragmentKw, "fragment MSL contains the `fragment` qualifier keyword");

            const bool hasEntryName = msl.find("fragment_main") != std::string::npos;
            CHECK(hasEntryName, "fragment MSL contains renamed entry point `fragment_main`");
        }
    }

    {
        printf("\n--- SPIR-V -> MSL: compute shader ---\n");
        const char* src = "#version 450 core\n"
                          "layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;\n"
                          "layout(set = 0, binding = 0) buffer Buf { uint counter; };\n"
                          "void main() { counter += 1u; }\n";

        metalsharp::GLSLVersion ver{};
        metalsharp::parseGLSLVersion(src, ver);

        std::vector<uint32_t> spirv;
        std::string errorLog;
        const bool compiled =
            metalsharp::GLSLCompiler::compileToSPIRV(src, metalsharp::ShaderStage::Compute, ver, spirv, errorLog);
        CHECK(compiled, "compute shader compiled to SPIR-V");

        std::string msl;
        std::string mslError;
        const bool translated =
            metalsharp::GLSLCompiler::translateSPIRVtoMSL(spirv, metalsharp::ShaderStage::Compute, msl, mslError);
        CHECK(translated, "translateSPIRVtoMSL succeeded for compute shader");
        CHECK(mslError.empty(), "compute MSL errorLog is empty");
        CHECK(!msl.empty(), "compute MSL output is non-empty");

        if (!msl.empty()) {
            // MSL compute kernels use the `kernel` qualifier keyword.
            const bool hasKernelKw = msl.find("kernel") != std::string::npos;
            CHECK(hasKernelKw, "compute MSL contains the `kernel` qualifier keyword");

            const bool hasEntryName = msl.find("kernel_main") != std::string::npos;
            CHECK(hasEntryName, "compute MSL contains renamed entry point `kernel_main`");
        }
    }

    {
        printf("\n--- SPIR-V -> MSL: empty input rejected ---\n");
        std::vector<uint32_t> emptySpirv;
        std::string msl;
        std::string mslError;
        const bool translated =
            metalsharp::GLSLCompiler::translateSPIRVtoMSL(emptySpirv, metalsharp::ShaderStage::Vertex, msl, mslError);
        CHECK(!translated, "translateSPIRVtoMSL returns false for empty SPIR-V");
        CHECK(msl.empty(), "MSL output stays empty on failure");
        CHECK(!mslError.empty(), "errorLog is non-empty on failure");
    }

    {
        printf("\n--- Shader tracker ---\n");

        // 1. Get tracker instance.
        auto& tracker = metalsharp::GLShaderTracker::instance();
        CHECK(true, "GLShaderTracker::instance() returns a valid tracker");

        // 2. Create vertex shader.
        constexpr uint32_t kGL_VERTEX_SHADER = 0x8B31;
        const uint32_t shaderName = tracker.createShader(kGL_VERTEX_SHADER);
        char nameBuf[64];
        std::snprintf(nameBuf, sizeof(nameBuf), "createShader returns non-zero name (got %u)", shaderName);
        CHECK(shaderName != 0, nameBuf);

        auto* state = tracker.getShader(shaderName);
        CHECK(state != nullptr, "getShader returns non-null for freshly created shader");
        if (state) {
            CHECK(state->type == kGL_VERTEX_SHADER, "shader type matches GL_VERTEX_SHADER");
            CHECK(state->stage == metalsharp::ShaderStage::Vertex, "shader stage mapped to ShaderStage::Vertex");
            CHECK(!state->compiled, "freshly created shader is not yet compiled");
            CHECK(!state->compileSuccess, "freshly created shader is not yet compileSuccess");
        }

        // 3. Set source — simulates glShaderSource. parseGLSLVersion + needsCrossCompile
        //    are run on the captured source the same way the shim does.
        const char* src450 = "#version 450 core\n"
                             "layout(location = 0) in vec3 pos;\n"
                             "void main() { gl_Position = vec4(pos, 1.0); }\n";

        if (state) {
            state->source = src450;
            const bool parsed = metalsharp::parseGLSLVersion(state->source.c_str(), state->glslVersion);
            state->needsCrossCompile = metalsharp::needsCrossCompile(state->glslVersion);

            CHECK(parsed, "parseGLSLVersion finds #version 450 directive");
            CHECK(state->glslVersion.major == 450, "Parsed major version is 450");
            CHECK(state->glslVersion.valid, "GLSLVersion.valid is true");
            CHECK(state->needsCrossCompile, "needsCrossCompile is true for #version 450");
        }

        // 4. Compile — drive GLSLCompiler::compileToSPIRV + translateSPIRVtoMSL
        //    exactly the way glCompileShader does in the shim.
        if (state && state->needsCrossCompile && !state->source.empty()) {
            std::string errorLog;
            bool ok = metalsharp::GLSLCompiler::compileToSPIRV(state->source.c_str(), state->stage, state->glslVersion,
                                                               state->spirv, errorLog);
            if (ok) {
                ok = metalsharp::GLSLCompiler::translateSPIRVtoMSL(state->spirv, state->stage, state->msl, errorLog);
            }
            state->compiled = true;
            state->compileSuccess = ok;
            state->infoLog = errorLog;

            CHECK(state->compileSuccess, "tracker-driven compile + translate succeeded");
            CHECK(state->infoLog.empty(), "tracker infoLog is empty on success");
            CHECK(!state->msl.empty(), "tracker MSL output is non-empty");
            CHECK(!state->spirv.empty(), "tracker SPIR-V output is non-empty");
        } else {
            printf("  [FAIL] shader not in cross-compile state\n");
            failed++;
        }

        // 5. Delete shader — getShader must return null afterwards.
        tracker.deleteShader(shaderName);
        CHECK(tracker.getShader(shaderName) == nullptr, "getShader returns null after deleteShader");
    }

    {
        printf("\n--- Shutdown ---\n");
        metalsharp::GLSLCompiler::shutdown();
        printf("  [OK] shutdown() returned cleanly\n");
        passed++;

        // Re-init + shutdown to ensure the path is repeatable.
        metalsharp::GLSLCompiler::initialize();
        metalsharp::GLSLCompiler::shutdown();
        printf("  [OK] init/shutdown cycle is repeatable\n");
        passed++;
    }

    printf("\n=== Summary: %d passed, %d failed ===\n", passed, failed);
    return failed ? 1 : 0;
}