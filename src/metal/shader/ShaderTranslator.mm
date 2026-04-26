#include <metalsharp/ShaderTranslator.h>
#include <metalsharp/MetalBackend.h>
#include <metalsharp/DXBCParser.h>
#include <metalsharp/DXBCtoMSL.h>
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

bool ShaderTranslator::translateDXBC(const uint8_t* data, size_t size, ShaderStage stage, CompiledShader& out) {
    if (!data || size == 0) return false;

    ParsedDXBC parsed;
    if (!DXBCParser::parse(data, size, parsed)) return false;

    std::string mslSource = DXBCtoMSL::translate(parsed);
    if (mslSource.empty()) return false;

    ShaderStage determinedStage = stage;
    switch (parsed.shaderType) {
        case DXBCShaderType::Vertex: determinedStage = ShaderStage::Vertex; break;
        case DXBCShaderType::Pixel: determinedStage = ShaderStage::Pixel; break;
        case DXBCShaderType::Compute: determinedStage = ShaderStage::Compute; break;
        default: break;
    }

    const char* entryName = "main0";
    switch (determinedStage) {
        case ShaderStage::Vertex: entryName = "vertexShader"; break;
        case ShaderStage::Pixel: entryName = "fragmentShader"; break;
        case ShaderStage::Compute: entryName = "computeShader"; break;
        default: break;
    }

    if (!m_impl->device) return false;

    NSString* sourceNS = [NSString stringWithUTF8String:mslSource.c_str()];
    NSError* error = nil;
    id<MTLLibrary> library = [m_impl->device newLibraryWithSource:sourceNS options:nil error:&error];
    if (!library) {
        if (error) {
            NSLog(@"MetalSharp DXBC->MSL compile error: %@", [error localizedDescription]);
            NSLog(@"Generated MSL:\n%@", sourceNS);
        }
        return false;
    }

    NSString* funcName = [NSString stringWithUTF8String:entryName];
    id<MTLFunction> func = [library newFunctionWithName:funcName];

    out.library = (__bridge_retained void*)library;
    out.vertexFunction = nullptr;
    out.fragmentFunction = nullptr;
    out.computeFunction = nullptr;

    switch (determinedStage) {
        case ShaderStage::Vertex:
            out.vertexFunction = func ? (__bridge_retained void*)func : nullptr;
            break;
        case ShaderStage::Pixel:
            out.fragmentFunction = func ? (__bridge_retained void*)func : nullptr;
            break;
        case ShaderStage::Compute:
            out.computeFunction = func ? (__bridge_retained void*)func : nullptr;
            break;
        default: break;
    }

    return func != nil;
}

}
