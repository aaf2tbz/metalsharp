#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void* (*real_dlopen)(const char*, int) = NULL;

__attribute__((constructor)) static void init_interpose(void) {
    real_dlopen = dlsym(RTLD_NEXT, "dlopen");
}

void* dlopen(const char* path, int mode) {
    if (path && strstr(path, "Carbon.framework") && strstr(path, "Versions/Current/Carbon")) {
        const char* shim = getenv("METALSHARP_CARBON_SHIM");
        if (shim) {
            void* h = real_dlopen ? real_dlopen(shim, mode) : NULL;
            if (h)
                return h;
        }
    }
    return real_dlopen ? real_dlopen(path, mode) : NULL;
}
