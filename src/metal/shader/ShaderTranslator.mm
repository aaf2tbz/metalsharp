#include <metalsharp/ShaderTranslator.h>
#include <Foundation/Foundation.h>
#include <Metal/Metal.h>
#include <cstring>

namespace metalsharp {

struct ShaderTranslator::Impl {
    id<MTLLibrary> library = nil;
    id<MTLFunction> vertexFunction = nil;
    id<MTLFunction> fragmentFunction = nil;
    id<MTLFunction> computeFunction = nil;
    NSData* compiledMSL = nil;
};

ShaderTranslator::ShaderTranslator() : m_impl(new Impl()) {}
ShaderTranslator::~ShaderTranslator() { delete m_impl; }

bool ShaderTranslator::translateDXIL(const ShaderBinary& dxil, ShaderBinary& outMSL) {
    return false;
}

bool ShaderTranslator::translateDXBC(const ShaderBinary& dxbc, ShaderBinary& outMSL) {
    return false;
}

void* ShaderTranslator::compiledLibrary() const {
    return (__bridge void*)m_impl->library;
}

void* ShaderTranslator::functionForStage(ShaderStage stage) const {
    switch (stage) {
        case ShaderStage::Vertex: return (__bridge void*)m_impl->vertexFunction;
        case ShaderStage::Pixel: return (__bridge void*)m_impl->fragmentFunction;
        case ShaderStage::Compute: return (__bridge void*)m_impl->computeFunction;
        default: return nullptr;
    }
}

}
