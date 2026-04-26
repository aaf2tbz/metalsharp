#include <metalsharp/MetalBackend.h>
#include <cstdio>

int main() {
    auto* device = metalsharp::MetalDevice::create();
    if (!device) {
        fprintf(stderr, "FAIL: Could not create Metal device\n");
        return 1;
    }

    if (!device->nativeDevice()) {
        fprintf(stderr, "FAIL: nativeDevice() returned null\n");
        return 1;
    }

    if (!device->nativeCommandQueue()) {
        fprintf(stderr, "FAIL: nativeCommandQueue() returned null\n");
        return 1;
    }

    delete device;
    printf("PASS: Metal device creation\n");
    return 0;
}
