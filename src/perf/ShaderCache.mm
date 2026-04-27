#include <metalsharp/ShaderCache.h>
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#include <metalsharp/Logger.h>
#include <fstream>

namespace metalsharp {

bool ShaderCache::loadEntry(const std::string& path, uint64_t hash) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string source((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    if (source.empty()) return false;

    CachedShader entry;
    entry.hash = hash;
    entry.mslSource = source;

    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (device) {
        NSString* nsSource = [NSString stringWithUTF8String:source.c_str()];
        NSError* error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:nsSource options:nil error:&error];
        if (library) {
            entry.library = (__bridge_retained void*)library;

            if ([library newFunctionWithName:@"vertexShader"]) {
                id<MTLFunction> func = [library newFunctionWithName:@"vertexShader"];
                entry.vertexFunction = func ? (__bridge_retained void*)func : nullptr;
            }
            if ([library newFunctionWithName:@"fragmentShader"]) {
                id<MTLFunction> func = [library newFunctionWithName:@"fragmentShader"];
                entry.fragmentFunction = func ? (__bridge_retained void*)func : nullptr;
            }
            if ([library newFunctionWithName:@"computeShader"]) {
                id<MTLFunction> func = [library newFunctionWithName:@"computeShader"];
                entry.computeFunction = func ? (__bridge_retained void*)func : nullptr;
            }
        } else if (error) {
            MS_WARN("ShaderCache: failed to precompile %s: %s",
                    path.c_str(), [[error localizedDescription] UTF8String]);
        }
    }

    m_cache[hash] = entry;
    return true;
}

}
