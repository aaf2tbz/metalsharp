#include <metalsharp/ShaderTranslator.h>
#include <metalsharp/MetalBackend.h>
#include <Foundation/Foundation.h>
#include <Metal/Metal.h>

namespace metalsharp {

struct ShaderTranslator::Impl {
    id<MTLDevice> device = nil;
};

ShaderTranslator::ShaderTranslator() : m_impl(new Impl()) {
    m_impl->device = MTLCreateSystemDefaultDevice();
}

ShaderTranslator::~ShaderTranslator() { delete m_impl; }

bool ShaderTranslator::compileMSL(const char* source, const char* vertexEntry, const char* fragmentEntry, CompiledShader& out) {
    if (!m_impl->device || !source) return false;

    NSString* sourceNS = [NSString stringWithUTF8String:source];
    NSError* error = nil;
    id<MTLLibrary> library = [m_impl->device newLibraryWithSource:sourceNS options:nil error:&error];
    if (!library) {
        if (error) {
            NSLog(@"MetalSharp shader compile error: %@", [error localizedDescription]);
        }
        return false;
    }

    NSString* vertName = [NSString stringWithUTF8String:vertexEntry];
    NSString* fragName = [NSString stringWithUTF8String:fragmentEntry];

    id<MTLFunction> vertexFunc = [library newFunctionWithName:vertName];
    id<MTLFunction> fragFunc = [library newFunctionWithName:fragName];

    out.library = (__bridge_retained void*)library;
    out.vertexFunction = vertexFunc ? (__bridge_retained void*)vertexFunc : nullptr;
    out.fragmentFunction = fragFunc ? (__bridge_retained void*)fragFunc : nullptr;
    out.computeFunction = nullptr;

    return true;
}

bool ShaderTranslator::translateDXIL(const uint8_t*, size_t, ShaderStage, CompiledShader&) {
    return false;
}

bool ShaderTranslator::translateDXBC(const uint8_t*, size_t, ShaderStage, CompiledShader&) {
    return false;
}

}
