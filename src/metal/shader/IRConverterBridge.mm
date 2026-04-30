#include <metalsharp/IRConverterBridge.h>
#include <metalsharp/Logger.h>
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

namespace metalsharp {

void* createMTLLibraryFromMetallib(const uint8_t* data, size_t size) {
    if (!data || size == 0) return nullptr;

    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) return nullptr;

    dispatch_data_t dispatchData = dispatch_data_create(
        data, size, dispatch_get_main_queue(), DISPATCH_DATA_DESTRUCTOR_DEFAULT
    );
    if (!dispatchData) return nullptr;

    NSError* error = nil;
    id<MTLLibrary> library = [device newLibraryWithData:dispatchData error:&error];

    if (!library) {
        if (error) {
            MS_ERROR("createMTLLibraryFromMetallib: %s", [[error localizedDescription] UTF8String]);
        }
        return nullptr;
    }

    return (__bridge_retained void*)library;
}

void* getFunctionFromLibrary(void* libraryPtr, const char* name) {
    if (!libraryPtr || !name) return nullptr;

    id<MTLLibrary> library = (__bridge id<MTLLibrary>)libraryPtr;
    NSString* nsName = [NSString stringWithUTF8String:name];
    id<MTLFunction> func = [library newFunctionWithName:nsName];

    return func ? (__bridge_retained void*)func : nullptr;
}

void releaseMTLObject(void* obj) {
    if (!obj) return;
    id nsObj = (__bridge_transfer id)obj;
    (void)nsObj;
}

}
