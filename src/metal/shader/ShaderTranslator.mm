#include <metalsharp/ShaderTranslator.h>
#include <metalsharp/MetalBackend.h>
#include <metalsharp/DXBCParser.h>
#include <metalsharp/DXBCtoMSL.h>
#include <metalsharp/Logger.h>
#include <Foundation/Foundation.h>
#include <Metal/Metal.h>

namespace metalsharp {

void* createMTLLibraryFromMetallib(const uint8_t* data, size_t size);
void* getFunctionFromLibrary(void* libraryPtr, const char* name);
void releaseMTLObject(void* obj);

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

bool ShaderTranslator::translateViaIRConverter(
    const uint8_t* dxilData, size_t dxilSize,
    ShaderStage stage, const char* entryPoint,
    const void* rootSigData, size_t rootSigSize,
    CompiledShader& out
) {
    auto& bridge = IRConverterBridge::instance();
    if (!bridge.isAvailable()) return false;

    std::vector<uint8_t> metallib;
    IRConverterReflection reflection;

    bool ok;
    if (rootSigData && rootSigSize > 0) {
        ok = bridge.compileDXILToMetallibWithRootSignature(
            dxilData, dxilSize, stage, entryPoint,
            rootSigData, rootSigSize,
            metallib, reflection
        );
    } else {
        ok = bridge.compileDXILToMetallib(
            dxilData, dxilSize, stage, entryPoint,
            metallib, reflection
        );
    }

    if (!ok || metallib.empty()) return false;

    void* lib = createMTLLibraryFromMetallib(metallib.data(), metallib.size());
    if (!lib) {
        MS_ERROR("ShaderTranslator: failed to create MTLLibrary from metallib");
        return false;
    }

    const char* funcName = reflection.entryPointName.empty() ? entryPoint : reflection.entryPointName.c_str();
    if (!funcName) funcName = "main0";

    void* func = getFunctionFromLibrary(lib, funcName);

    out.library = lib;
    out.vertexFunction = nullptr;
    out.fragmentFunction = nullptr;
    out.computeFunction = nullptr;
    out.reflection = std::move(reflection);
    out.entryPointName = funcName;

    switch (stage) {
        case ShaderStage::Vertex: out.vertexFunction = func; break;
        case ShaderStage::Pixel: out.fragmentFunction = func; break;
        case ShaderStage::Compute: out.computeFunction = func; break;
        default: break;
    }

    MS_INFO("ShaderTranslator: compiled via IRConverter (stage=%d, entry=%s)", (int)stage, funcName);
    return true;
}

bool ShaderTranslator::translateViaFallbackDXBC(const uint8_t* data, size_t size, ShaderStage stage, CompiledShader& out) {
    ParsedDXBC parsed;
    if (!DXBCParser::parse(data, size, parsed)) return false;

    std::string mslSource = DXBCtoMSL::translate(parsed);
    if (mslSource.empty()) return false;

    const char* entryName = "main0";
    switch (stage) {
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
            NSLog(@"MetalSharp DXBC->MSL fallback compile error: %@", [error localizedDescription]);
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

    switch (stage) {
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

    MS_INFO("ShaderTranslator: compiled via DXBC->MSL fallback (stage=%d)", (int)stage);
    return func != nil;
}

bool ShaderTranslator::translateDXIL(const uint8_t* data, size_t size, ShaderStage stage, CompiledShader& out) {
    if (!data || size == 0) return false;
    return translateViaIRConverter(data, size, stage, nullptr, nullptr, 0, out);
}

bool ShaderTranslator::translateDXBC(const uint8_t* data, size_t size, ShaderStage stage, CompiledShader& out) {
    return translateDXBCWithRootSignature(data, size, stage, nullptr, 0, out);
}

bool ShaderTranslator::translateDXBCWithRootSignature(
    const uint8_t* data, size_t size,
    ShaderStage stage,
    const void* rootSigData, size_t rootSigSize,
    CompiledShader& out
) {
    if (!data || size == 0) return false;

    auto& bridge = IRConverterBridge::instance();

    if (bridge.isAvailable()) {
        std::vector<uint8_t> dxil;

        if (bridge.isDXIL(data, size)) {
            dxil.assign(data, data + size);
        } else {
            bool extracted = bridge.extractDXILFromDXBC(data, size, dxil);
            if (!extracted) {
                uint32_t sm = bridge.detectShaderModel(data, size);
                MS_INFO("ShaderTranslator: DXBC SM %u.%u — no DXIL chunk, using MSL fallback",
                         sm / 10, sm % 10);
                return translateViaFallbackDXBC(data, size, stage, out);
            }
        }

        bool ok = translateViaIRConverter(
            dxil.data(), dxil.size(), stage, nullptr,
            rootSigData, rootSigSize, out
        );
        if (ok) return true;

        MS_WARN("ShaderTranslator: IRConverter failed, falling back to DXBC->MSL");
    }

    return translateViaFallbackDXBC(data, size, stage, out);
}

}
