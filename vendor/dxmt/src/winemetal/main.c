#include "windef.h"
#include "winbase.h"
#include "wineunixlib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000)
#endif
#ifndef STATUS_BUFFER_OVERFLOW
#define STATUS_BUFFER_OVERFLOW ((NTSTATUS)0x80000005)
#endif

static NTSTATUS
load_unixlib_from_env(void) {
  const char *path = getenv("DXMT_WINEMETAL_UNIXLIB");
  WCHAR wide_path[MAX_PATH * 4];
  UNICODE_STRING name;
  unixlib_module_t module = 0;
  size_t i;

  if (!path || !path[0])
    return STATUS_DLL_NOT_FOUND;

  for (i = 0; path[i] && i < (sizeof(wide_path) / sizeof(wide_path[0])) - 1; i++)
    wide_path[i] = (WCHAR)(unsigned char)path[i];
  if (path[i])
    return STATUS_BUFFER_OVERFLOW;
  wide_path[i] = 0;

  name.Buffer = wide_path;
  name.Length = i * sizeof(WCHAR);
  name.MaximumLength = (i + 1) * sizeof(WCHAR);

  return __wine_load_unix_lib(&name, &module, &__wine_unixlib_handle);
}

static int
winemetal_debug_enabled(void) {
  const char *value = getenv("DXMT_WINEMETAL_DEBUG");
  return value && value[0] && strcmp(value, "0") != 0;
}

BOOL WINAPI
DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
  NTSTATUS status;
  FILE *log;

  if (reason != DLL_PROCESS_ATTACH)
    return TRUE;

  DisableThreadLibraryCalls(instance);
  status = load_unixlib_from_env();
  if ((DWORD)status == STATUS_DLL_NOT_FOUND)
    status = __wine_init_unix_call();
  {
    HMODULE hook = LoadLibraryA("metalsharp_ntdll_hook.dll");
    (void)hook;
  }
  if (winemetal_debug_enabled()) {
    log = fopen("Z:\\tmp\\winemetal_pe_debug.log", "a");
    if (log) {
      fprintf(
          log,
          "DllMain PROCESS_ATTACH unix_call_init status=0x%08lx "
          "unixlib=%s\n",
          (unsigned long)status, getenv("DXMT_WINEMETAL_UNIXLIB") ? getenv("DXMT_WINEMETAL_UNIXLIB") : "<unset>"
      );
      fclose(log);
    }
  }
  return !status;
}

extern BOOL WINAPI DllMainCRTStartup(HANDLE hDllHandle, DWORD dwReason, LPVOID lpreserved);
