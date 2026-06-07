#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>

typedef int HRESULT;
typedef unsigned int UINT;
typedef unsigned long ULONG;
typedef long LONG;
typedef void* REFIID;

#ifdef __APPLE__
#define CDECL __attribute__((cdecl))
#else
#define CDECL
#endif

#define E_FAIL ((HRESULT)0x80004005)

static void* g_framework;

static HRESULT (*g_D3D12CreateDevice)(void*, unsigned, void*, void**);
static HRESULT (*g_D3D12GetDebugInterface)(void*, void**);
static HRESULT (*g_D3D12SerializeRootSignature)(const void*, unsigned, void*, void*);
static HRESULT (*g_D3D12CreateRootSignatureDeserializer)(const void*, unsigned long long, void*, void**);
static HRESULT (*g_D3D12SerializeVersionedRootSignature)(const void*, void*, void*);
static HRESULT (*g_D3D12CreateVersionedRootSignatureDeserializer)(const void*, unsigned long long, void*, void**);
static HRESULT (*g_D3D12EnableExperimentalFeatures)(unsigned, void*, void*, unsigned*);

static HRESULT (*g_CreateDXGIFactory)(REFIID, void**);
static HRESULT (*g_CreateDXGIFactory1)(REFIID, void**);
static HRESULT (*g_CreateDXGIFactory2)(UINT, REFIID, void**);
static HRESULT (*g_DXGIDeclareAdapterRemovalSupport)(void);

static HRESULT (*g_D3D11CreateDevice)(void*, unsigned, void*, unsigned, const void*, unsigned, unsigned, void**, void*,
                                      void**);
static HRESULT (*g_D3D11CreateDeviceAndSwapChain)(void*, unsigned, void*, unsigned, const void*, unsigned, unsigned,
                                                  void*, void**, void**, void*, void**);

static int load_framework(void) {
    static int loaded = 0;
    static int result = 0;
    if (loaded)
        return result;
    loaded = 1;

    const char* paths[] = {
        "@executable_path/../lib/external/D3DMetal.framework/Versions/A/D3DMetal",
        "@loader_path/../../../external/D3DMetal.framework/Versions/A/D3DMetal",
        "D3DMetal.framework/Versions/A/D3DMetal",
        NULL,
    };

    for (int i = 0; paths[i]; i++) {
        g_framework = dlopen(paths[i], RTLD_NOW);
        if (g_framework)
            break;
    }

    if (!g_framework) {
        fprintf(stderr, "d3dmetal_bridge: failed to load D3DMetal.framework: %s\n", dlerror());
        result = -1;
        return result;
    }

#define LOAD(name)                                                                                                     \
    do {                                                                                                               \
        g_##name = dlsym(g_framework, #name);                                                                          \
        if (!g_##name)                                                                                                 \
            fprintf(stderr, "d3dmetal_bridge: missing " #name "\n");                                                   \
    } while (0)

    LOAD(D3D12CreateDevice);
    LOAD(D3D12GetDebugInterface);
    LOAD(D3D12SerializeRootSignature);
    LOAD(D3D12CreateRootSignatureDeserializer);
    LOAD(D3D12SerializeVersionedRootSignature);
    LOAD(D3D12CreateVersionedRootSignatureDeserializer);
    LOAD(D3D12EnableExperimentalFeatures);
    LOAD(CreateDXGIFactory);
    LOAD(CreateDXGIFactory1);
    LOAD(CreateDXGIFactory2);
    LOAD(DXGIDeclareAdapterRemovalSupport);
    LOAD(D3D11CreateDevice);
    LOAD(D3D11CreateDeviceAndSwapChain);

#undef LOAD

    result = 1;
    return result;
}

static LONG CDECL unix_dispatch(ULONG ordinal, void* args) {
    if (load_framework() < 0)
        return E_FAIL;

    switch (ordinal) {
    case 1: {
        void** a = (void**)args;
        return g_D3D12CreateDevice(a[0], (unsigned)(uintptr_t)a[1], a[2], (void**)a[3]);
    }
    case 2: {
        void** a = (void**)args;
        return g_D3D12GetDebugInterface(a[0], (void**)a[1]);
    }
    case 3: {
        void** a = (void**)args;
        return g_D3D12SerializeRootSignature(a[0], (unsigned)(uintptr_t)a[1], a[2], a[3]);
    }
    case 4: {
        void** a = (void**)args;
        return g_D3D12CreateRootSignatureDeserializer(a[0], (unsigned long long)(uintptr_t)a[1], a[2], (void**)a[3]);
    }
    case 5: {
        void** a = (void**)args;
        return g_D3D12SerializeVersionedRootSignature(a[0], a[1], a[2]);
    }
    case 6: {
        void** a = (void**)args;
        return g_D3D12CreateVersionedRootSignatureDeserializer(a[0], (unsigned long long)(uintptr_t)a[1], a[2],
                                                               (void**)a[3]);
    }
    case 7: {
        void** a = (void**)args;
        return g_D3D12EnableExperimentalFeatures((unsigned)(uintptr_t)a[0], a[1], a[2], (unsigned*)a[3]);
    }
    case 8: {
        struct {
            REFIID a;
            void** b;
        }* a = args;
        return g_CreateDXGIFactory(a->a, a->b);
    }
    case 9: {
        struct {
            REFIID a;
            void** b;
        }* a = args;
        return g_CreateDXGIFactory1(a->a, a->b);
    }
    case 10: {
        struct {
            UINT a;
            REFIID b;
            void** c;
        }* a = args;
        return g_CreateDXGIFactory2(a->a, a->b, a->c);
    }
    case 11:
        return g_DXGIDeclareAdapterRemovalSupport();
    case 12: {
        void** a = (void**)args;
        return g_D3D11CreateDevice(a[0], (unsigned)(uintptr_t)a[1], a[2], (unsigned)(uintptr_t)a[3], a[4],
                                   (unsigned)(uintptr_t)a[5], (unsigned)(uintptr_t)a[6], (void**)a[7], a[8],
                                   (void**)a[9]);
    }
    case 13: {
        void** a = (void**)args;
        return g_D3D11CreateDeviceAndSwapChain(a[0], (unsigned)(uintptr_t)a[1], a[2], (unsigned)(uintptr_t)a[3], a[4],
                                               (unsigned)(uintptr_t)a[5], (unsigned)(uintptr_t)a[6], a[7], (void**)a[8],
                                               (void**)a[9], a[10], (void**)a[11]);
    }
    }
    return E_FAIL;
}

static const struct {
    LONG(CDECL* call)(ULONG, void*);
} unix_call_entry = {unix_dispatch};

void* __wine_unix_call_funcs[] = {&unix_call_entry, NULL};
void* __wine_unix_call_wow64_funcs[] = {&unix_call_entry, NULL};
