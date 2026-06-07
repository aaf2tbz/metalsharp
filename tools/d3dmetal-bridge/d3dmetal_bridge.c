#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int HRESULT;
typedef unsigned int UINT;
typedef long LONG;
typedef unsigned long ULONG;
typedef LONG NTSTATUS;

#define E_FAIL ((HRESULT)0x80004005)

static void* g_d3dshared;

typedef NTSTATUS (*entry_fn)(void*);
static entry_fn* g_dispatch;
static int g_dispatch_count;

static int init_d3dshared(void) {
    static int attempted = 0;
    if (attempted)
        return g_dispatch != NULL;
    attempted = 1;

    const char* paths[] = {
        "@executable_path/../lib/external/libd3dshared.dylib",
        "@loader_path/../../../external/libd3dshared.dylib",
        "@rpath/libd3dshared.dylib",
        NULL,
    };

    for (int i = 0; paths[i]; i++) {
        g_d3dshared = dlopen(paths[i], RTLD_NOW);
        if (g_d3dshared)
            break;
    }
    if (!g_d3dshared) {
        fprintf(stderr, "d3dmetal_bridge: libd3dshared.dylib: %s\n", dlerror());
        return 0;
    }

    entry_fn* funcs = (entry_fn*)dlsym(g_d3dshared, "__wine_unix_call_funcs");
    if (!funcs) {
        fprintf(stderr, "d3dmetal_bridge: __wine_unix_call_funcs not found\n");
        return 0;
    }

    g_dispatch = funcs;

    fprintf(stderr, "d3dmetal_bridge: calling process_attach via dispatch[0]\n");
    NTSTATUS ret = funcs[0](NULL);
    fprintf(stderr, "d3dmetal_bridge: process_attach returned 0x%08x\n", (unsigned)ret);

    return 1;
}

static LONG unix_dispatch(ULONG ordinal, void* args) {
    if (!init_d3dshared())
        return E_FAIL;

    if (ordinal >= 64 || !g_dispatch[ordinal]) {
        fprintf(stderr, "d3dmetal_bridge: ordinal %lu not available\n", ordinal);
        return E_FAIL;
    }

    return g_dispatch[ordinal](args);
}

static const struct {
    LONG (*call)(ULONG, void*);
} unix_call_entry = {unix_dispatch};

void* __wine_unix_call_funcs[] = {(void*)&unix_call_entry, NULL};
void* __wine_unix_call_wow64_funcs[] = {(void*)&unix_call_entry, NULL};
