#include <windows.h>

typedef HRESULT(WINAPI *PFN_CreateDXGIFactory)(REFIID, void **);
typedef HRESULT(WINAPI *PFN_CreateDXGIFactory2)(UINT, REFIID, void **);

static HMODULE dxgi_self;
static HMODULE dxgi_real;

static char *find_last_path_separator(char *path) {
  char *last = NULL;
  for (char *cursor = path; *cursor; cursor++) {
    if (*cursor == '\\' || *cursor == '/')
      last = cursor;
  }
  return last;
}

static HMODULE load_real_dxgi(void) {
  if (dxgi_real)
    return dxgi_real;

  char path[MAX_PATH];
  DWORD len = GetModuleFileNameA(dxgi_self, path, MAX_PATH);
  if (!len || len >= MAX_PATH)
    return NULL;

  char *slash = find_last_path_separator(path);
  if (!slash)
    return NULL;

  slash[1] = '\0';
  lstrcatA(path, "dxgi_dxmt.dll");
  dxgi_real = LoadLibraryA(path);
  return dxgi_real;
}

static FARPROC resolve_real_dxgi_proc(const char *name) {
  HMODULE real = load_real_dxgi();
  return real ? GetProcAddress(real, name) : NULL;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
  (void)reserved;
  if (reason == DLL_PROCESS_ATTACH)
    dxgi_self = instance;
  return TRUE;
}

HRESULT WINAPI CreateDXGIFactory(REFIID riid, void **factory) {
  PFN_CreateDXGIFactory proc = (PFN_CreateDXGIFactory)resolve_real_dxgi_proc("CreateDXGIFactory");
  return proc ? proc(riid, factory) : HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
}

HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void **factory) {
  PFN_CreateDXGIFactory proc = (PFN_CreateDXGIFactory)resolve_real_dxgi_proc("CreateDXGIFactory1");
  return proc ? proc(riid, factory) : HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
}

HRESULT WINAPI CreateDXGIFactory2(UINT flags, REFIID riid, void **factory) {
  PFN_CreateDXGIFactory2 proc = (PFN_CreateDXGIFactory2)resolve_real_dxgi_proc("CreateDXGIFactory2");
  return proc ? proc(flags, riid, factory) : HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
}

HRESULT WINAPI DXGID3D10CreateDevice(void) {
  FARPROC proc = resolve_real_dxgi_proc("DXGID3D10CreateDevice");
  return proc ? ((HRESULT(WINAPI *)(void))proc)() : E_NOTIMPL;
}

HRESULT WINAPI DXGID3D10RegisterLayers(void) {
  FARPROC proc = resolve_real_dxgi_proc("DXGID3D10RegisterLayers");
  return proc ? ((HRESULT(WINAPI *)(void))proc)() : S_OK;
}

HRESULT WINAPI DXGIGetDebugInterface(REFIID riid, void **debug) {
  PFN_CreateDXGIFactory proc = (PFN_CreateDXGIFactory)resolve_real_dxgi_proc("DXGIGetDebugInterface");
  return proc ? proc(riid, debug) : E_NOINTERFACE;
}

HRESULT WINAPI DXGIGetDebugInterface1(UINT flags, REFIID riid, void **debug) {
  PFN_CreateDXGIFactory2 proc = (PFN_CreateDXGIFactory2)resolve_real_dxgi_proc("DXGIGetDebugInterface1");
  return proc ? proc(flags, riid, debug) : E_NOINTERFACE;
}
