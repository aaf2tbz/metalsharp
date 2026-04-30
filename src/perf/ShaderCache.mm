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

            NSArray<NSString*>* names = [library functionNames];
            for (NSString* name in names) {
                id<MTLFunction> func = [library newFunctionWithName:name];
                if (!func) continue;

                std::string nameStr = [name UTF8String];
                if (nameStr.find("vertex") != std::string::npos || nameStr == "vs" || nameStr == "VS") {
                    entry.vertexFunction = (__bridge_retained void*)func;
                } else if (nameStr.find("fragment") != std::string::npos || nameStr == "ps" || nameStr == "FS") {
                    entry.fragmentFunction = (__bridge_retained void*)func;
                } else if (nameStr.find("compute") != std::string::npos || nameStr == "cs" || nameStr == "CS") {
                    entry.computeFunction = (__bridge_retained void*)func;
                } else {
                    entry.entryPoint = nameStr;
                }
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
