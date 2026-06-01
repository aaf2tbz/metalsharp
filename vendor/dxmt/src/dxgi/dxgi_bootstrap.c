#include <windows.h>

typedef HRESULT(WINAPI *PFN_CreateDXGIFactory)(REFIID, void **);
typedef HRESULT(WINAPI *PFN_CreateDXGIFactory2)(UINT, REFIID, void **);

static HMODULE dxgi_self;
static HMODULE dxgi_real;

static BOOL bootstrap_trace_enabled(void) {
  char value[8];
  DWORD len = GetEnvironmentVariableA("DXMT_DXGI_TRACE", value, sizeof(value));
  return len && len < sizeof(value) && value[0] && value[0] != '0';
}

static void append_hex(char *dst, unsigned long value) {
  static const char digits[] = "0123456789abcdef";
  dst[0] = '0';
  dst[1] = 'x';
  for (int i = 0; i < 8; i++)
    dst[2 + i] = digits[(value >> ((7 - i) * 4)) & 0xf];
  dst[10] = '\0';
}

static void bootstrap_trace(const char *message) {
  if (!bootstrap_trace_enabled())
    return;

  HANDLE file = CreateFileA(
      "Z:\\tmp\\dxmt_dxgi_bootstrap.log",
      FILE_APPEND_DATA,
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      NULL,
      OPEN_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      NULL);
  if (file == INVALID_HANDLE_VALUE)
    return;

  DWORD written;
  WriteFile(file, message, lstrlenA(message), &written, NULL);
  WriteFile(file, "\r\n", 2, &written, NULL);
  CloseHandle(file);
}

static void bootstrap_trace_hex(const char *message, unsigned long value) {
  char line[256];
  char hex[11];
  line[0] = '\0';
  lstrcpynA(line, message, sizeof(line));
  lstrcatA(line, " ");
  append_hex(hex, value);
  lstrcatA(line, hex);
  bootstrap_trace(line);
}

static void bootstrap_trace_path(const char *message, const char *path) {
  char line[MAX_PATH + 96];
  line[0] = '\0';
  lstrcpynA(line, message, sizeof(line));
  lstrcatA(line, " ");
  lstrcatA(line, path ? path : "(null)");
  bootstrap_trace(line);
}

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
  if (!len || len >= MAX_PATH) {
    bootstrap_trace_hex("GetModuleFileNameA failed", GetLastError());
    return NULL;
  }
  bootstrap_trace_path("bootstrap module", path);

  char *slash = find_last_path_separator(path);
  if (!slash) {
    bootstrap_trace("bootstrap path has no separator");
    return NULL;
  }

  slash[1] = '\0';
  lstrcatA(path, "dxgi_dxmt.dll");
  bootstrap_trace_path("loading real dxgi", path);
  dxgi_real = LoadLibraryA(path);
  if (!dxgi_real) {
    bootstrap_trace_hex("LoadLibraryA(dxgi_dxmt.dll) failed", GetLastError());
  } else {
    bootstrap_trace_hex("LoadLibraryA(dxgi_dxmt.dll) ok", (unsigned long)(ULONG_PTR)dxgi_real);
  }
  return dxgi_real;
}

static FARPROC resolve_real_dxgi_proc(const char *name) {
  HMODULE real = load_real_dxgi();
  if (!real)
    return NULL;

  FARPROC proc = GetProcAddress(real, name);
  if (!proc)
    bootstrap_trace_hex(name, GetLastError());
  return proc;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
  (void)reserved;
  if (reason == DLL_PROCESS_ATTACH) {
    dxgi_self = instance;
    bootstrap_trace_hex("dxgi bootstrap attach", (unsigned long)(ULONG_PTR)instance);
  }
  return TRUE;
}

HRESULT WINAPI CreateDXGIFactory(REFIID riid, void **factory) {
  PFN_CreateDXGIFactory proc = (PFN_CreateDXGIFactory)resolve_real_dxgi_proc("CreateDXGIFactory");
  HRESULT hr = proc ? proc(riid, factory) : HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
  bootstrap_trace_hex("CreateDXGIFactory hr", (unsigned long)hr);
  return hr;
}

HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void **factory) {
  PFN_CreateDXGIFactory proc = (PFN_CreateDXGIFactory)resolve_real_dxgi_proc("CreateDXGIFactory1");
  HRESULT hr = proc ? proc(riid, factory) : HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
  bootstrap_trace_hex("CreateDXGIFactory1 hr", (unsigned long)hr);
  return hr;
}

HRESULT WINAPI CreateDXGIFactory2(UINT flags, REFIID riid, void **factory) {
  PFN_CreateDXGIFactory2 proc = (PFN_CreateDXGIFactory2)resolve_real_dxgi_proc("CreateDXGIFactory2");
  HRESULT hr = proc ? proc(flags, riid, factory) : HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
  bootstrap_trace_hex("CreateDXGIFactory2 hr", (unsigned long)hr);
  return hr;
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
