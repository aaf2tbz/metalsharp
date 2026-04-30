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
