#pragma once

#include <metalsharp/Platform.h>
#include <stddef.h>
#include <stdint.h>

namespace metalsharp {

enum class ShaderStage {
    Vertex,
    Pixel,
    Compute,
    Geometry,
    Hull,
    Domain,
};

struct CompiledShader {
    void* library;
    void* vertexFunction;
    void* fragmentFunction;
    void* computeFunction;
};

class ShaderTranslator {
public:
    ShaderTranslator();
    ~ShaderTranslator();

    bool compileMSL(const char* source, const char* vertexEntry, const char* fragmentEntry, CompiledShader& out);
    bool translateDXIL(const uint8_t* data, size_t size, ShaderStage stage, CompiledShader& out);
    bool translateDXBC(const uint8_t* data, size_t size, ShaderStage stage, CompiledShader& out);

    ShaderTranslator(const ShaderTranslator&) = delete;
    ShaderTranslator& operator=(const ShaderTranslator&) = delete;

private:
    struct Impl;
    Impl* m_impl;
};

}
