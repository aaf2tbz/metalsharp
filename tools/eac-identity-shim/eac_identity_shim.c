#include <windows.h>

__declspec(dllexport) const char* CDECL wine_get_version(void) {
    return NULL;
}

__declspec(dllexport) const char* CDECL wine_get_build_id(void) {
    return NULL;
}

__declspec(dllexport) const char* CDECL wine_get_unix_file_name(LPCWSTR dos) {
    (void)dos;
    return NULL;
}

__declspec(dllexport) const WCHAR* CDECL wine_get_dos_file_name(const char* unix_path) {
    (void)unix_path;
    return NULL;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved) {
    (void)h;
    (void)reserved;
    return reason == DLL_PROCESS_ATTACH ? TRUE : TRUE;
}
