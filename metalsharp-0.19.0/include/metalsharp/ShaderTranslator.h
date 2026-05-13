/// @file ShaderTranslator.h
/// @brief Unified shader compilation pipeline — picks the best available path.
///
/// Shader compilation follows a priority chain:
///
///   1. DXIL path (if IRConverter available):
///      DXBC blob → extract DXIL chunk → IRConverter → metallib → MTLLibrary
///
///   2. MSL path (if DXBC→MSL translator available):
///      DXBC bytecode → DXBCParser → DXBCtoMSL → MSL source string → MTLLibrary
///
///   3. Raw MSL path:
///      Already-MSL source → newLibraryWithSource → MTLLibrary
///
/// The translator is owned by D3D11Device and shared across all shader creation
/// calls (CreateVertexShader, CreatePixelShader, CreateComputeShader, etc.).
/// Compiled MTLLibraries are cached by ShaderCache using FNV-1a hashing of the
/// input bytecode to avoid recompilation across frames and sessions.

#pragma once

#include <metalsharp/Platform.h>
#include <metalsharp/ShaderStage.h>
#include <metalsharp/IRConverterBridge.h>
#include <stddef.h>
#include <stdint.h>
#include <string>

namespace metalsharp {

struct CompiledShader {
    void* library;
    void* vertexFunction;
    void* fragmentFunction;
    void* computeFunction;
    std::string entryPointName;
    IRConverterReflection reflection;
};

class ShaderTranslator {
public:
    ShaderTranslator();
    ~ShaderTranslator();

    bool compileMSL(const char* source, const char* vertexEntry, const char* fragmentEntry, CompiledShader& out);
    bool translateDXIL(const uint8_t* data, size_t size, ShaderStage stage, CompiledShader& out);
    bool translateDXBC(const uint8_t* data, size_t size, ShaderStage stage, CompiledShader& out);

    bool translateDXBCWithRootSignature(
        const uint8_t* data, size_t size,
        ShaderStage stage,
        const void* rootSigData, size_t rootSigSize,
        CompiledShader& out
    );

    ShaderTranslator(const ShaderTranslator&) = delete;
    ShaderTranslator& operator=(const ShaderTranslator&) = delete;

private:
    bool translateViaIRConverter(
        const uint8_t* dxilData, size_t dxilSize,
        ShaderStage stage, const char* entryPoint,
        const void* rootSigData, size_t rootSigSize,
        CompiledShader& out
    );
    bool translateViaFallbackDXBC(const uint8_t* data, size_t size, ShaderStage stage, CompiledShader& out);

    struct Impl;
    Impl* m_impl;
};

}
