#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void* id;

typedef long NTSTATUS;
typedef unsigned long ULONG;

#define STATUS_SUCCESS          ((NTSTATUS)0x00000000)
#define STATUS_UNSUCCESSFUL     ((NTSTATUS)0xC0000001)
#define STATUS_ACCESS_VIOLATION ((NTSTATUS)0xC0000005)

typedef NTSTATUS (*unix_call_func)(void* args);

static void* g_d3dmetal_handle;
static void* g_d3dshared_handle;
static unix_call_func g_shared_funcs[2];

static void dbg(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    FILE* f = fopen("/tmp/d3dmetal_unix.log", "a");
    if (f) {
        fprintf(f, "[d3dmetal_unix] ");
        vfprintf(f, fmt, ap);
        fprintf(f, "\n");
        fclose(f);
    }
    va_end(ap);
}

struct GFXTDispatch {
    void* entries[44];
};

struct Win32Dispatch {
    void* D3DKMTEnumAdapters2;
    struct {
        struct {
            void* address;
            uint64_t length;
        } dxgi;
    } image;
};

struct macdrv_functions {
    void* init_display_devices;
    void* (*get_win_data)(void*);
    void (*release_win_data)(void*);
    void* (*get_cocoa_window)(void*);
    void* (*create_metal_device)(void);
    void (*release_metal_device)(void*);
    void* (*view_create_metal_view)(void*, void*);
    void* (*view_get_metal_layer)(void*);
    void (*view_release_metal_view)(void*, void*);
    void* on_main_thread;
};

static struct macdrv_functions* g_macdrv;

typedef void (*GFXT_Initialize_t)(void* os_interface);
typedef int (*D3D12CreateDevice_t)(void* adapter, int feature_level, const void* riid, void** device);
typedef int (*CreateDXGIFactory2_t)(unsigned int flags, const void* riid, void** factory);
typedef int (*D3D11CreateDevice_t)(void* adapter, int driver_type, void* software, unsigned int flags,
                                   const void* feature_levels, unsigned int feature_levels_count, const void* riid,
                                   void** device, void** immediate_context, void* feature_level);
typedef int (*D3D11CreateDeviceAndSwapChain_t)(void* adapter, int driver_type, void* software, unsigned int flags,
                                               const void* feature_levels, unsigned int feature_levels_count,
                                               const void* riid, void** device, void* feature_level,
                                               const void* swap_chain_desc, void** swap_chain,
                                               void** immediate_context);
typedef void* (*D3D11DeferredDeviceContext_GetVTable_t)(void);
typedef void* (*D3D10CreateBlob_t)(unsigned int size, void** buffer);
typedef void* (*D3D12CreateRootSignatureDeserializer_t)(const void* data, unsigned long long data_size, const void* iid,
                                                        void** deserializer);
typedef void* (*D3D12CreateVersionedRootSignatureDeserializer_t)(const void* data, unsigned long long data_size,
                                                                 const void* iid, void** deserializer);
typedef int (*D3D12SerializeRootSignature_t)(const void* desc, int version, void** blob, void** error_blob);
typedef int (*D3D12SerializeVersionedRootSignature_t)(const void* desc, void** blob, void** error_blob);
typedef void* (*nvapi_QueryInterface_t)(unsigned int);
typedef void* (*nvapi_Direct_GetMethod_t)(unsigned short);

static void* g_gfxtdispatch[44];

typedef int (*GFXTInterfaceVersion_t)(void);

typedef struct {
    void** vtable;
} GFXTOSInterface;

typedef struct {
    void** vtable;
    char data[0x18];
} WineMonitorSub;

typedef struct {
    void** vtable;
} WineRegistrySub;

typedef struct {
    void** vtable;
    char data[0x720];
} WineEventCallbacksSub;

typedef struct {
    void** vtable;
} WineAdaptersSub;

typedef struct {
    void** vtable;
    char data[0x10];
} WinePathsSub;

typedef struct {
    void** vtable;
    char data[0x10];
} WineAllocationsSub;

typedef struct {
    void** vtable;
} WineLibrariesSub;

typedef struct {
    void** vtable;
    WineMonitorSub* monitor;
    WineRegistrySub* registry;
    WineEventCallbacksSub* event_callbacks;
    WineAdaptersSub* adapters;
    WinePathsSub* paths;
    WineAllocationsSub* allocations;
    WineLibrariesSub* libraries;
    id mtl_device;
} WineOSObject;

static WineMonitorSub g_monitor;
static WineRegistrySub g_registry;
static WineEventCallbacksSub g_event_callbacks;
static WineAdaptersSub g_adapters;
static WinePathsSub g_paths;
static WineAllocationsSub g_allocations;
static WineLibrariesSub g_libraries;
static WineOSObject g_wineos;

static int gfx_os_version(void) {
    return 5;
}
static void wineos_dtor1(void* self) {}
static void wineos_dtor0(void* self) {
    wineos_dtor1(self);
}

static void* wineos_create_monitor(const void* version) {
    dbg("  CreateMonitorInterface called, version=%d", version ? *(int*)version : -1);
    return &g_monitor;
}
static void* wineos_create_registry(const void* version) {
    dbg("  CreateRegistryInterface called");
    return &g_registry;
}
static void* wineos_create_event(const void* version) {
    dbg("  CreateEventInterface called");
    return &g_event_callbacks;
}
static void* wineos_create_swapchain(const void* version, void* mtl_device) {
    dbg("  CreateSwapchainInterface called, mtl_device=%p", mtl_device);
    return NULL;
}
static void* wineos_create_adapter(const void* version) {
    dbg("  CreateAdapterInterface called");
    return &g_adapters;
}
static void* wineos_create_path(const void* version) {
    dbg("  CreatePathInterface called");
    return &g_paths;
}
static void* wineos_create_allocation(const void* version) {
    dbg("  CreateAllocationInterface called");
    return &g_allocations;
}
static void* wineos_create_library(const void* version) {
    dbg("  CreateLibraryInterface called");
    return &g_libraries;
}

static void* s_wineos_vtable[] = {
    (void*)0,
    (void*)0,
    (void*)gfx_os_version,
    (void*)wineos_dtor1,
    (void*)wineos_dtor0,
    (void*)wineos_create_monitor,
    (void*)wineos_create_registry,
    (void*)wineos_create_event,
    (void*)wineos_create_swapchain,
    (void*)wineos_create_adapter,
    (void*)wineos_create_path,
    (void*)wineos_create_allocation,
    (void*)wineos_create_library,
};

static int monitor_version(void) {
    return 2;
}
static void monitor_dtor1(void* self) {}
static void monitor_dtor0(void* self) {
    monitor_dtor1(self);
}
static void monitor_query_monitor_info(unsigned int idx, void* info) {
    dbg("    Monitor::QueryMonitorInfo(%u)", idx);
    memset(info, 0, 0x40);
}
static void monitor_query_display_mode(unsigned long long luid, const void* devmode, unsigned int mode_idx,
                                       void* info) {
    dbg("    Monitor::QueryDisplayMode(luid=0x%llx, mode=%u)", luid, mode_idx);
    memset(info, 0, 0x40);
}
static void monitor_query_description(unsigned long long luid, void* desc) {
    dbg("    Monitor::QueryDescription(luid=0x%llx)", luid);
    memset(desc, 0, 0x40);
}
static void monitor_change_display_mode(unsigned long long luid, const void* devmode, const void* info) {
    dbg("    Monitor::ChangeDisplayMode(luid=0x%llx)", luid);
}

static void* s_monitor_vtable[] = {
    (void*)0,
    (void*)0,
    (void*)monitor_version,
    (void*)monitor_dtor1,
    (void*)monitor_dtor0,
    (void*)monitor_query_monitor_info,
    (void*)monitor_query_display_mode,
    (void*)monitor_query_description,
    (void*)monitor_change_display_mode,
};

static int registry_version(void) {
    return 2;
}
static void registry_dtor1(void* self) {}
static void registry_dtor0(void* self) {
    registry_dtor1(self);
}
static void* registry_open_key(int main_key, const char* name) {
    dbg("    Registry::OpenKey(%d, %s)", main_key, name ? name : "(null)");
    return (void*)0xDEAD;
}
static void* registry_create_key(int main_key, const char* name, int vol, int* created) {
    dbg("    Registry::CreateKey(%d, %s)", main_key, name ? name : "(null)");
    if (created)
        *created = 1;
    return (void*)0xDEAD;
}
static void registry_close_key(void* key) {}
static void registry_set_value_dword(void* key, const char* name, unsigned int val) {}
static void registry_set_value_qword(void* key, const char* name, unsigned long long val) {}
static void registry_set_value_string(void* key, const char* name, const char* val) {}
static void registry_set_value_bytes(void* key, const char* name, const void* val) {}
static int registry_get_value_dword(void* key, const char* name, unsigned int* val) {
    return 0;
}
static int registry_get_value_qword(void* key, const char* name, unsigned long long* val) {
    return 0;
}
static int registry_get_value_string(void* key, const char* name, void* val) {
    return 0;
}
static int registry_get_value_bytes(void* key, const char* name, void* val) {
    return 0;
}
static void registry_delete_value(void* key, const char* name, const char* subkey) {}

static void* s_registry_vtable[] = {
    (void*)0,
    (void*)0,
    (void*)registry_version,
    (void*)registry_dtor1,
    (void*)registry_dtor0,
    (void*)registry_open_key,
    (void*)registry_create_key,
    (void*)registry_close_key,
    (void*)registry_set_value_dword,
    (void*)registry_set_value_qword,
    (void*)registry_set_value_string,
    (void*)registry_set_value_bytes,
    (void*)registry_get_value_dword,
    (void*)registry_get_value_qword,
    (void*)registry_get_value_string,
    (void*)registry_get_value_bytes,
    (void*)registry_delete_value,
};

static int event_version(void) {
    return 3;
}
static void event_dtor1(void* self) {}
static void event_dtor0(void* self) {
    event_dtor1(self);
}
static void* event_create_event(unsigned int flags, int initial) {
    return NULL;
}
static void event_set_event(void* handle) {}
static void event_clear_event(void* handle) {}
static void event_pulse_event(void* handle) {}
static void event_close_event(void* handle) {}
static void* event_duplicate_event(void* handle) {
    return NULL;
}
static void* event_create_semaphore(unsigned int initial, unsigned int max) {
    return NULL;
}
static void event_signal_semaphore(void* handle, unsigned int count) {}
static void event_close_semaphore(void* handle) {}
static void* event_duplicate_semaphore(void* handle) {
    return NULL;
}
static void event_dispatch_internal(void (*cb)(const void*), int flag, const void* data, unsigned int size) {
    if (cb)
        cb(data);
}

static void* s_event_vtable[] = {
    (void*)0,
    (void*)0,
    (void*)event_version,
    (void*)event_dtor1,
    (void*)event_dtor0,
    (void*)event_create_event,
    (void*)event_set_event,
    (void*)event_clear_event,
    (void*)event_pulse_event,
    (void*)event_close_event,
    (void*)event_duplicate_event,
    (void*)event_create_semaphore,
    (void*)event_signal_semaphore,
    (void*)event_close_semaphore,
    (void*)event_duplicate_semaphore,
    (void*)event_dispatch_internal,
};

static int adapter_version(void) {
    return 1;
}
static void adapter_dtor1(void* self) {}
static void adapter_dtor0(void* self) {
    adapter_dtor1(self);
}
static unsigned long adapter_get_adapter_luids(void* luids, unsigned long count) {
    dbg("    Adapters::getAdapterLUIDs(count=%lu)", count);
    if (luids && count > 0) {
        memset(luids, 0, 8);
    }
    return 0;
}

static void* s_adapter_vtable[] = {
    (void*)0,
    (void*)0,
    (void*)adapter_version,
    (void*)adapter_dtor1,
    (void*)adapter_dtor0,
    (void*)adapter_get_adapter_luids,
};

static int path_version(void) {
    return 3;
}
static void path_dtor1(void* self) {}
static void path_dtor0(void* self) {
    path_dtor1(self);
}
static int path_windows_to_unix(const void* wide, char* buf, unsigned long* size) {
    if (buf && size) {
        buf[0] = 0;
        *size = 1;
    }
    return 0;
}
static int path_unix_to_windows(const char* utf8, void* wide, unsigned long* size) {
    return 0;
}
static int path_windows_system_dir(void* wide, unsigned long* size) {
    return 0;
}
static int path_get_executable(char* buf, unsigned int size) {
    if (buf && size > 0)
        buf[0] = 0;
    return 0;
}
static int path_get_module(void* module, char* buf, unsigned int size) {
    if (buf && size > 0)
        buf[0] = 0;
    return 0;
}

static void* s_path_vtable[] = {
    (void*)0,
    (void*)0,
    (void*)path_version,
    (void*)path_dtor1,
    (void*)path_dtor0,
    (void*)path_windows_to_unix,
    (void*)path_unix_to_windows,
    (void*)path_windows_system_dir,
    (void*)path_get_executable,
    (void*)path_get_module,
};

static int allocation_version(void) {
    return 2;
}
static void allocation_dtor1(void* self) {}
static void allocation_dtor0(void* self) {
    allocation_dtor1(self);
}
static void* allocation_malloc(unsigned long size) {
    return malloc(size);
}
static void allocation_free(void* ptr) {
    free(ptr);
}
static void* allocation_alloc_new_pages(unsigned long size) {
    return malloc(size);
}
static void allocation_free_pages(void* ptr, unsigned long size) {
    free(ptr);
}
static void* allocation_alloc_from_image(const char* image, unsigned long size) {
    return malloc(size);
}
static int allocation_make_executable(void* ptr, unsigned long size) {
    return 0;
}

static void* s_allocation_vtable[] = {
    (void*)0,
    (void*)0,
    (void*)allocation_version,
    (void*)allocation_dtor1,
    (void*)allocation_dtor0,
    (void*)allocation_malloc,
    (void*)allocation_free,
    (void*)allocation_alloc_new_pages,
    (void*)allocation_free_pages,
    (void*)allocation_alloc_from_image,
    (void*)allocation_make_executable,
};

static int library_version(void) {
    return 2;
}
static void library_dtor1(void* self) {}
static void library_dtor0(void* self) {
    library_dtor1(self);
}
static void* library_load(const char* name) {
    dbg("    Library::loadLibrary(%s)", name ? name : "(null)");
    return dlopen(name, RTLD_NOW);
}
static void* library_get_module_handle(const char* name) {
    return dlopen(name, RTLD_NOW);
}
static void* library_get_proc_address(void* handle, const char* name) {
    return dlsym(handle, name);
}
static void library_free(void* handle) {
    if (handle)
        dlclose(handle);
}
static void* library_load_from_system_dir(const char* name) {
    return dlopen(name, RTLD_NOW);
}

static void* s_library_vtable[] = {
    (void*)0,
    (void*)0,
    (void*)library_version,
    (void*)library_dtor1,
    (void*)library_dtor0,
    (void*)library_load,
    (void*)library_get_module_handle,
    (void*)library_get_proc_address,
    (void*)library_free,
    (void*)library_load_from_system_dir,
};

static void init_os_objects(void) {
    static int done = 0;
    if (done)
        return;
    done = 1;

    g_macdrv = (struct macdrv_functions*)dlsym(RTLD_DEFAULT, "macdrv_functions");
    dbg("macdrv_functions = %p", (void*)g_macdrv);
    if (g_macdrv) {
        dbg("  macdrv[0]=%p [1]=%p [2]=%p [3]=%p [4]=%p [5]=%p [6]=%p [7]=%p [8]=%p [9]=%p",
            g_macdrv->init_display_devices, (void*)g_macdrv->get_win_data, (void*)g_macdrv->release_win_data,
            (void*)g_macdrv->get_cocoa_window, (void*)g_macdrv->create_metal_device,
            (void*)g_macdrv->release_metal_device, (void*)g_macdrv->view_create_metal_view,
            (void*)g_macdrv->view_get_metal_layer, (void*)g_macdrv->view_release_metal_view, g_macdrv->on_main_thread);
    }

    g_monitor.vtable = &s_monitor_vtable[2];
    memset(g_monitor.data, 0, sizeof(g_monitor.data));

    g_registry.vtable = &s_registry_vtable[2];

    g_event_callbacks.vtable = &s_event_vtable[2];
    memset(g_event_callbacks.data, 0, sizeof(g_event_callbacks.data));

    g_adapters.vtable = &s_adapter_vtable[2];

    g_paths.vtable = &s_path_vtable[2];
    memset(g_paths.data, 0, sizeof(g_paths.data));

    g_allocations.vtable = &s_allocation_vtable[2];
    memset(g_allocations.data, 0, sizeof(g_allocations.data));

    g_libraries.vtable = &s_library_vtable[2];

    if (g_macdrv && g_macdrv->create_metal_device) {
        g_wineos.mtl_device = (id)g_macdrv->create_metal_device();
        dbg("MTLDevice from macdrv = %p", (void*)g_wineos.mtl_device);
    }

    g_wineos.vtable = &s_wineos_vtable[2];
    g_wineos.monitor = &g_monitor;
    g_wineos.registry = &g_registry;
    g_wineos.event_callbacks = &g_event_callbacks;
    g_wineos.adapters = &g_adapters;
    g_wineos.paths = &g_paths;
    g_wineos.allocations = &g_allocations;
    g_wineos.libraries = &g_libraries;
}

static int load_d3dmetal(void) {
    static int attempted = 0;
    static int loaded = 0;
    if (attempted)
        return loaded;
    attempted = 1;

    const char* paths[] = {"/System/Library/Frameworks/D3DMetal.framework/D3DMetal",
                           "@rpath/D3DMetal.framework/D3DMetal",
                           "@loader_path/../../external/D3DMetal.framework/Versions/A/D3DMetal", NULL};

    for (int i = 0; paths[i]; i++) {
        dbg("  trying D3DMetal: %s", paths[i]);
        g_d3dmetal_handle = dlopen(paths[i], RTLD_NOW);
        if (g_d3dmetal_handle) {
            dbg("  loaded D3DMetal from %s", paths[i]);
            break;
        }
        dbg("  failed: %s", dlerror());
    }

    if (!g_d3dmetal_handle) {
        dbg("FATAL: cannot dlopen D3DMetal.framework: %s", dlerror());
        return 0;
    }

    loaded = 1;
    return 1;
}

static int d3dmetal_dispatch_init_direct(void* args) {
    dbg("ordinal 0: D3DRMDispatch_Init DIRECT (bypassing libd3dshared)");

    init_os_objects();

    if (!load_d3dmetal()) {
        return STATUS_UNSUCCESSFUL;
    }

    GFXT_Initialize_t gfxt_init = (GFXT_Initialize_t)dlsym(g_d3dmetal_handle, "GFXT_Initialize");
    if (!gfxt_init) {
        dbg("FATAL: GFXT_Initialize not found in D3DMetal: %s", dlerror());
        return STATUS_UNSUCCESSFUL;
    }
    dbg("GFXT_Initialize = %p", (void*)gfxt_init);

    dbg("calling GFXT_Initialize with WineOS at %p", (void*)&g_wineos);
    gfxt_init(&g_wineos);
    dbg("GFXT_Initialize returned");

    void** dispatch = (void**)args;
    memset(dispatch, 0, 0x160);

    dbg("resolving D3D function pointers from D3DMetal...");

    struct {
        const char* name;
        int idx;
    } syms[] = {{"D3D12CreateDevice", 1},
                {"D3D12CreateRootSignatureDeserializer", 5},
                {"D3D12CreateVersionedRootSignatureDeserializer", 6},
                {"D3D12SerializeRootSignature", 9},
                {"D3D12SerializeVersionedRootSignature", 11},
                {"CreateDXGIFactory2", 20},
                {"D3D11CreateDevice", 30},
                {"D3D11CreateDeviceAndSwapChain", 31},
                {"D3D11DeferredDeviceContext_GetVTable", 40},
                {"D3D10CreateBlob", 42},
                {NULL, -1}};

    for (int i = 0; syms[i].name; i++) {
        void* sym = dlsym(g_d3dmetal_handle, syms[i].name);
        if (sym) {
            dispatch[syms[i].idx] = sym;
            dbg("  [%d] %s = %p", syms[i].idx, syms[i].name, sym);
        } else {
            dbg("  [%d] %s = NOT FOUND", syms[i].idx, syms[i].name);
        }
    }

    dbg("ordinal 0: dispatch table filled, returning SUCCESS");
    return STATUS_SUCCESS;
}

static NTSTATUS d3dmetal_dispatch_init(void* args) {
    dbg("ordinal 0: D3DRMDispatch_Init ENTER");
    NTSTATUS status = d3dmetal_dispatch_init_direct(args);
    dbg("ordinal 0 returned 0x%08lx", (long)status);
    return status;
}

static NTSTATUS d3dmetal_win32_dispatch_init(void* args) {
    dbg("ordinal 1: Win32DispatchInit");
    return STATUS_SUCCESS;
}

__attribute__((visibility("default"))) const unix_call_func __wine_unix_call_funcs[] = {
    d3dmetal_dispatch_init,
    d3dmetal_win32_dispatch_init,
};
