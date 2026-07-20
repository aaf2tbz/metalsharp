#ifndef MS_OPENGL_PE_SHIM_H
#define MS_OPENGL_PE_SHIM_H

#include <windows.h>

/* Forward a GL function from the PE DLL to the macOS dylib via dlsym.
 * On mingw-w64, dlsym is declared in dlfcn.h. Each function's first call
 * resolves the symbol from the host process; subsequent calls reuse the
 * cached pointer. */
#define FORWARD_GL_VOID(name)                                                                                          \
    __declspec(dllexport) void __stdcall name(void) {                                                                  \
        static void (*fn)(void) = NULL;                                                                                \
        if (!fn)                                                                                                       \
            fn = (void (*)(void))GetProcAddress(GetModuleHandleA("opengl32.dll"), #name);                              \
        if (fn)                                                                                                        \
            fn();                                                                                                      \
    }

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved);

#endif
