#include <stdint.h>
#include <windows.h>

typedef LONG NTSTATUS;
typedef unsigned long long uint64_t;
typedef NTSTATUS(WINAPI* unix_call_fn)(uint64_t, unsigned int, void*);

static HMODULE g_hModule;
static uint64_t g_handle;
static unix_call_fn g_unix_call;

static BOOL init(void) {
    HMODULE ntdll;
    NTSTATUS(NTAPI * NtQVM)(HANDLE, void*, ULONG, void*, unsigned long, unsigned long*);
    void** ptr;

    if (g_unix_call)
        return TRUE;

    ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll)
        return FALSE;

    NtQVM = (void*)GetProcAddress(ntdll, "NtQueryVirtualMemory");
    if (!NtQVM)
        return FALSE;

    g_handle = 0;
    NtQVM((HANDLE)(LONG_PTR)-1, (void*)g_hModule, 0x3E8, &g_handle, sizeof(g_handle), NULL);
    if (!g_handle)
        return FALSE;

    ptr = (void**)GetProcAddress(ntdll, "__wine_unix_call_dispatcher");
    if (!ptr || !*ptr)
        return FALSE;
    g_unix_call = (unix_call_fn)*ptr;

    return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID ctx) {
    (void)ctx;
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = (HMODULE)inst;
        DisableThreadLibraryCalls(inst);
        init();
    }
    return TRUE;
}

__declspec(dllexport) HRESULT WINAPI D3D10CreateDevice(void) {
    return 0x80004005;
}
