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

struct ShaderBinary {
    const uint8_t* data;
    size_t size;
    ShaderStage stage;
};

class ShaderTranslator {
public:
    ShaderTranslator();
    ~ShaderTranslator();

    bool translateDXIL(const ShaderBinary& dxil, ShaderBinary& outMSL);
    bool translateDXBC(const ShaderBinary& dxbc, ShaderBinary& outMSL);

    void* compiledLibrary() const;
    void* functionForStage(ShaderStage stage) const;

    ShaderTranslator(const ShaderTranslator&) = delete;
    ShaderTranslator& operator=(const ShaderTranslator&) = delete;

private:
    struct Impl;
    Impl* m_impl;
};

}
