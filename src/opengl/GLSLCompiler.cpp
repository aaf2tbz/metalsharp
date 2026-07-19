/// @file GLSLCompiler.cpp
/// @brief GLSL → SPIR-V compiler wrapper around the Khronos glslang reference
///        implementation.
///
/// The OpenGL 3.x/4.x bridge needs to compile guest GLSL shaders into SPIR-V
/// before handing them to SPIRV-Cross for cross-compilation to MSL. This
/// translation unit is the single entry point for that compilation step.
///
/// Process-level glslang state is initialized lazily on the first call to
/// compileToSPIRV() and torn down in shutdown(). It uses std::call_once so
/// that concurrent callers do not race the initialization, and is idempotent
/// so that callers do not need to pair initialize() with shutdown() for
/// normal use.
///
/// This is intentionally a thin wrapper. It does not try to replicate the
/// full glslang option surface; it covers the GLSL cases the OpenGL bridge
/// actually needs (Vulkan and OpenGL dialects, SPIR-V 1.0 target).

#include <metalsharp/GLSLCompiler.h>
#include <metalsharp/GLSLVersion.h>
#include <metalsharp/ShaderStage.h>

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

#include <mutex>

namespace metalsharp {

bool GLSLCompiler::s_initialized = false;

namespace {

std::once_flag g_init_once;

// glslang keeps its language/message enums in two places:
//   * EShLanguage / EShMessages / EShExecutable / etc. — global namespace
//     (typedef enums declared above the `namespace glslang {` block in
//     glslang/Public/ShaderLang.h).
//   * EShSource / EShClient / EShTargetLanguage / EShTargetClientVersion —
//     inside the `glslang` namespace.
// Classes such as TShader / TProgram / TIntermediate live in `glslang`.
// We alias the global ones here so the call sites stay readable.
using Lang = EShLanguage;

glslang::EShSource mapGLSLSource() {
    return glslang::EShSourceGlsl;
}

EShLanguage mapShaderStage(ShaderStage stage) {
    switch (stage) {
    case ShaderStage::Vertex:
        return EShLangVertex;
    case ShaderStage::Pixel:
        return EShLangFragment;
    case ShaderStage::Compute:
        return EShLangCompute;
    case ShaderStage::Geometry:
        return EShLangGeometry;
    case ShaderStage::Hull:
        return EShLangTessControl;
    case ShaderStage::Domain:
        return EShLangTessEvaluation;
    case ShaderStage::Mesh:
        return EShLangMesh;
    case ShaderStage::Amplification:
        return EShLangTask;
    case ShaderStage::RayGeneration:
        return EShLangRayGen;
    case ShaderStage::ClosestHit:
        return EShLangClosestHit;
    case ShaderStage::Miss:
        return EShLangMiss;
    case ShaderStage::Intersection:
        return EShLangIntersect;
    case ShaderStage::AnyHit:
        return EShLangAnyHit;
    case ShaderStage::Callable:
        return EShLangCallable;
    }
    return EShLangVertex;
}

} // namespace

bool GLSLCompiler::initialize() {
    std::call_once(g_init_once, []() {
        if (glslang::InitializeProcess()) {
            s_initialized = true;
        }
    });
    return s_initialized;
}

void GLSLCompiler::shutdown() {
    if (s_initialized) {
        glslang::FinalizeProcess();
        s_initialized = false;
    }
}

bool GLSLCompiler::isAvailable() {
    if (s_initialized)
        return true;
    return initialize();
}

bool GLSLCompiler::compileToSPIRV(const char* source, ShaderStage stage, const GLSLVersion& version,
                                  std::vector<uint32_t>& spirvOut, std::string& errorLog) {
    spirvOut.clear();
    errorLog.clear();

    if (!source) {
        errorLog = "GLSLCompiler: source is null";
        return false;
    }

    if (!initialize()) {
        errorLog = "GLSLCompiler: glslang::InitializeProcess() failed";
        return false;
    }

    const EShLanguage esStage = mapShaderStage(stage);

    glslang::TShader shader(esStage);

    // Pick the dialect. The OpenGL bridge mostly feeds us desktop GLSL, but
    // we also accept ES sources via the GLSLVersion::isES flag. We always
    // target SPIR-V 1.0 because that is what Metal/SPIRV-Cross expect on the
    // Apple stack; SPIRV-Cross can promote to newer SPIR-V dialects internally.
    const glslang::EShSource sourceLang = mapGLSLSource();
    const glslang::EShClient dialect = glslang::EShClientOpenGL;
    const int dialectVersion = version.valid ? static_cast<int>(version.major * 100 + version.minor) : 450;

    shader.setEnvInput(sourceLang, esStage, dialect, dialectVersion);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);

    const char* sources[1] = {source};
    shader.setStrings(sources, 1);

    const EShMessages messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);

    const TBuiltInResource* resources = GetDefaultResources();

    if (!shader.parse(resources, 100, false, messages)) {
        errorLog = "GLSLCompiler: parse failed: ";
        errorLog += shader.getInfoLog();
        errorLog += shader.getInfoDebugLog();
        return false;
    }

    glslang::TProgram program;
    program.addShader(&shader);

    if (!program.link(messages)) {
        errorLog = "GLSLCompiler: link failed: ";
        errorLog += program.getInfoLog();
        errorLog += shader.getInfoLog();
        errorLog += shader.getInfoDebugLog();
        return false;
    }

    const glslang::TIntermediate* intermediate = program.getIntermediate(esStage);
    if (!intermediate) {
        errorLog = "GLSLCompiler: no intermediate representation after link";
        return false;
    }

    glslang::SpvOptions options;
    options.disableOptimizer = true;
    options.stripDebugInfo = true;

    glslang::GlslangToSpv(*intermediate, spirvOut, &options);

    if (spirvOut.empty()) {
        errorLog = "GLSLCompiler: GlslangToSpv produced empty SPIR-V output";
        return false;
    }

    return true;
}

} // namespace metalsharp