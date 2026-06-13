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

static unixlib_module_t loaded_unixlib_module;

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

  NTSTATUS status = __wine_load_unix_lib(&name, &module, &__wine_unixlib_handle);
  loaded_unixlib_module = module;
  return status;
}

static int
winemetal_debug_enabled(void) {
  const char *value = getenv("DXMT_WINEMETAL_DEBUG");
  return value && value[0] && strcmp(value, "0") != 0;
}

static FILE *
winemetal_open_log(const char *fallback_name) {
  const char *root = getenv("METALSHARP_M12_LOG_DIR");
  const char *file = getenv("DXMT_LOG_FILE");
  char path[4096];
  size_t len;

  if (!root || !root[0])
    root = getenv("DXMT_LOG_PATH");
  if (!file || !file[0])
    file = fallback_name && fallback_name[0] ? fallback_name : "winemetal-pe.log";
  if (!root || !root[0])
    return fopen(file, "a");

  snprintf(path, sizeof(path), "%s%s%s", root, (root[strlen(root) - 1] == '/' || root[strlen(root) - 1] == '\\') ? "" : "/", file);
  path[sizeof(path) - 1] = '\0';
  len = strlen(path);
  if (len == 0)
    return NULL;
  return fopen(path, "a");
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
    log = winemetal_open_log("winemetal-pe.log");
    if (log) {
      fprintf(
          log,
          "DllMain PROCESS_ATTACH unix_call_init status=0x%08lx "
          "handle=0x%llx module=0x%llx dispatcher=%p unixlib=%s\n",
          (unsigned long)status,
          (unsigned long long)__wine_unixlib_handle,
          (unsigned long long)loaded_unixlib_module,
          (void *)__wine_unix_call_dispatcher,
          getenv("DXMT_WINEMETAL_UNIXLIB") ? getenv("DXMT_WINEMETAL_UNIXLIB") : "<unset>"
      );
      fclose(log);
    }
  }
  return !status;
}

extern BOOL WINAPI DllMainCRTStartup(HANDLE hDllHandle, DWORD dwReason, LPVOID lpreserved);
