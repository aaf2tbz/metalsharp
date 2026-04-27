#include <metalsharp/PELoader.h>
#include <metalsharp/Win32Types.h>
#include <metalsharp/Logger.h>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <unistd.h>

namespace metalsharp {
namespace win32 {

static int shim_wsprintfA(char* buf, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int r = vsnprintf(buf, 1024, fmt, args);
    va_end(args);
    return r;
}

ShimLibrary createUser32Shim() {
    ShimLibrary lib;
    lib.name = "USER32.dll";

    auto fn = [](void* ptr) -> ExportedFunction {
        return [ptr]() -> void* { return ptr; };
    };

    static void* s_fakeHwnd = reinterpret_cast<void*>(0x5000);
    static DWORD s_msgResult = 0;

    lib.functions["CreateWindowExW"] = fn((void*)static_cast<void*(*)(DWORD, const wchar_t*, const wchar_t*, DWORD, int, int, int, int, void*, void*, void*, void*)>(
        [](DWORD, const wchar_t*, const wchar_t*, DWORD, int, int, int, int, void*, void*, void*, void*) -> void* {
            return s_fakeHwnd;
        }));

    lib.functions["RegisterClassExW"] = fn((void*)static_cast<WORD(*)(void*)>([](void*) -> WORD { return 1; }));
    lib.functions["DestroyWindow"] = fn((void*)static_cast<BOOL(*)(void*)>([](void*) -> BOOL { return 1; }));
    lib.functions["ShowWindow"] = fn((void*)static_cast<BOOL(*)(void*, int)>([](void*, int) -> BOOL { return 1; }));
    lib.functions["UpdateWindow"] = fn((void*)static_cast<BOOL(*)(void*)>([](void*) -> BOOL { return 1; }));
    lib.functions["GetMessageW"] = fn((void*)static_cast<BOOL(*)(void*, void*, UINT, UINT)>([](void* msg, void*, UINT, UINT) -> BOOL {
        memset(msg, 0, 48);
        reinterpret_cast<DWORD*>(msg)[1] = 0x0012;
        return 0;
    }));
    lib.functions["PeekMessageW"] = fn((void*)static_cast<BOOL(*)(void*, void*, UINT, UINT, UINT)>([](void* msg, void*, UINT, UINT, UINT) -> BOOL {
        memset(msg, 0, 48);
        return 0;
    }));
    lib.functions["TranslateMessage"] = fn((void*)static_cast<BOOL(*)(const void*)>([](const void*) -> BOOL { return 0; }));
    lib.functions["DispatchMessageW"] = fn((void*)static_cast<intptr_t(*)(const void*)>([](const void*) -> intptr_t { return 0; }));
    lib.functions["PostThreadMessageW"] = fn((void*)static_cast<BOOL(*)(DWORD, UINT, intptr_t, intptr_t)>([](DWORD, UINT, intptr_t, intptr_t) -> BOOL { return 1; }));
    lib.functions["SendMessageW"] = fn((void*)static_cast<intptr_t(*)(void*, UINT, intptr_t, intptr_t)>([](void*, UINT, intptr_t, intptr_t) -> intptr_t { return 0; }));
    lib.functions["DefWindowProcW"] = fn((void*)static_cast<intptr_t(*)(void*, UINT, intptr_t, intptr_t)>([](void*, UINT, intptr_t, intptr_t) -> intptr_t { return 0; }));

    lib.functions["BeginPaint"] = fn((void*)static_cast<void*(*)(void*, void*)>([](void*, void* ps) -> void* {
        memset(ps, 0, 72);
        return reinterpret_cast<void*>(0x6000);
    }));
    lib.functions["EndPaint"] = fn((void*)static_cast<BOOL(*)(void*, void*)>([](void*, void*) -> BOOL { return 1; }));
    lib.functions["GetDC"] = fn((void*)static_cast<void*(*)(void*)>([](void*) -> void* { return reinterpret_cast<void*>(0x6000); }));
    lib.functions["ReleaseDC"] = fn((void*)static_cast<int(*)(void*, void*)>([](void*, void*) -> int { return 1; }));
    lib.functions["RedrawWindow"] = fn((void*)static_cast<BOOL(*)(void*, const void*, void*, UINT)>([](void*, const void*, void*, UINT) -> BOOL { return 1; }));

    lib.functions["GetWindowRect"] = fn((void*)static_cast<BOOL(*)(void*, void*)>([](void*, void* rect) -> BOOL {
        auto* r = reinterpret_cast<LONG*>(rect);
        r[0] = 0; r[1] = 0; r[2] = 1920; r[3] = 1080;
        return 1;
    }));
    lib.functions["MoveWindow"] = fn((void*)static_cast<BOOL(*)(void*, int, int, int, int, BOOL)>([](void*, int, int, int, int, BOOL) -> BOOL { return 1; }));
    lib.functions["SetWindowPos"] = fn((void*)static_cast<BOOL(*)(void*, void*, int, int, int, int, UINT)>([](void*, void*, int, int, int, int, UINT) -> BOOL { return 1; }));
    lib.functions["SetWindowTextW"] = fn((void*)static_cast<BOOL(*)(void*, const wchar_t*)>([](void*, const wchar_t*) -> BOOL { return 1; }));
    lib.functions["SetWindowLongPtrW"] = fn((void*)static_cast<intptr_t(*)(void*, int, intptr_t)>([](void*, int, intptr_t) -> intptr_t { return 0; }));
    lib.functions["GetWindowLongPtrW"] = fn((void*)static_cast<intptr_t(*)(void*, int)>([](void*, int) -> intptr_t { return 0; }));
    lib.functions["SetClassLongPtrW"] = fn((void*)static_cast<ULONG(*)(void*, int, LONG)>([](void*, int, LONG) -> ULONG { return 0; }));

    lib.functions["GetDesktopWindow"] = fn((void*)static_cast<void*(*)()>([]() -> void* { return s_fakeHwnd; }));
    lib.functions["IsWindowVisible"] = fn((void*)static_cast<BOOL(*)(void*)>([](void*) -> BOOL { return 1; }));
    lib.functions["GetWindowThreadProcessId"] = fn((void*)static_cast<DWORD(*)(void*, DWORD*)>([](void*, DWORD* pid) -> DWORD {
        if (pid) *pid = (DWORD)getpid();
        return 1;
    }));
    lib.functions["EnumWindows"] = fn((void*)static_cast<BOOL(*)(void*, intptr_t)>([](void*, intptr_t) -> BOOL { return 1; }));
    lib.functions["EnumChildWindows"] = fn((void*)static_cast<BOOL(*)(void*, void*, intptr_t)>([](void*, void*, intptr_t) -> BOOL { return 1; }));

    lib.functions["LoadCursorW"] = fn((void*)static_cast<void*(*)(void*, const wchar_t*)>([](void*, const wchar_t*) -> void* { return reinterpret_cast<void*>(0x7000); }));
    lib.functions["LoadIconW"] = fn((void*)static_cast<void*(*)(void*, const wchar_t*)>([](void*, const wchar_t*) -> void* { return reinterpret_cast<void*>(0x7001); }));

    lib.functions["MessageBoxA"] = fn((void*)static_cast<int(*)(void*, const char*, const char*, UINT)>([](void*, const char*, const char*, UINT) -> int { return 1; }));
    lib.functions["MessageBoxW"] = fn((void*)static_cast<int(*)(void*, const wchar_t*, const wchar_t*, UINT)>([](void*, const wchar_t*, const wchar_t*, UINT) -> int { return 1; }));

    lib.functions["GetMonitorInfoW"] = fn((void*)static_cast<BOOL(*)(void*, void*)>([](void*, void* mi) -> BOOL {
        memset(mi, 0, 72);
        reinterpret_cast<DWORD*>(mi)[0] = 72;
        auto* r = reinterpret_cast<LONG*>((uint8_t*)mi + 4);
        r[0] = 0; r[1] = 0; r[2] = 1920; r[3] = 1080;
        return 1;
    }));
    lib.functions["MonitorFromPoint"] = fn((void*)static_cast<void*(*)(LONG, LONG, DWORD)>([](LONG, LONG, DWORD) -> void* { return s_fakeHwnd; }));
    lib.functions["MonitorFromWindow"] = fn((void*)static_cast<void*(*)(void*, DWORD)>([](void*, DWORD) -> void* { return s_fakeHwnd; }));

    lib.functions["MsgWaitForMultipleObjects"] = fn((void*)static_cast<DWORD(*)(DWORD, void*, BOOL, DWORD, DWORD)>([](DWORD, void*, BOOL, DWORD, DWORD) -> DWORD { return WAIT_OBJECT_0; }));

    lib.functions["OpenClipboard"] = fn((void*)static_cast<BOOL(*)(void*)>([](void*) -> BOOL { return 1; }));
    lib.functions["CloseClipboard"] = fn((void*)static_cast<BOOL(*)()>([]() -> BOOL { return 1; }));
    lib.functions["EmptyClipboard"] = fn((void*)static_cast<BOOL(*)()>([]() -> BOOL { return 1; }));
    lib.functions["SetClipboardData"] = fn((void*)static_cast<void*(*)(UINT, void*)>([](UINT, void*) -> void* { return nullptr; }));

    lib.functions["GetClassInfoExW"] = fn((void*)static_cast<BOOL(*)(void*, const wchar_t*, void*)>([](void*, const wchar_t*, void*) -> BOOL { return 0; }));

    lib.functions["DialogBoxParamA"] = fn((void*)static_cast<intptr_t(*)(void*, const char*, void*, void*, intptr_t)>([](void*, const char*, void*, void*, intptr_t) -> intptr_t { return 0; }));
    lib.functions["EndDialog"] = fn((void*)static_cast<BOOL(*)(void*, intptr_t)>([](void*, intptr_t) -> BOOL { return 1; }));
    lib.functions["GetDlgItem"] = fn((void*)static_cast<void*(*)(void*, int)>([](void*, int) -> void* { return s_fakeHwnd; }));
    lib.functions["GetDlgItemInt"] = fn((void*)static_cast<UINT(*)(void*, int, BOOL*, BOOL)>([](void*, int, BOOL*, BOOL) -> UINT { return 0; }));
    lib.functions["SetDlgItemInt"] = fn((void*)static_cast<BOOL(*)(void*, int, UINT, BOOL)>([](void*, int, UINT, BOOL) -> BOOL { return 1; }));
    lib.functions["SetDlgItemTextA"] = fn((void*)static_cast<BOOL(*)(void*, int, const char*)>([](void*, int, const char*) -> BOOL { return 1; }));

    lib.functions["GetWindowTextLengthA"] = fn((void*)static_cast<int(*)(void*)>([](void*) -> int { return 0; }));
    lib.functions["MapWindowPoints"] = fn((void*)static_cast<int(*)(void*, void*, void*, UINT)>([](void*, void*, void*, UINT) -> int { return 0; }));

    lib.functions["AllowSetForegroundWindow"] = fn((void*)static_cast<BOOL(*)(DWORD)>([](DWORD) -> BOOL { return 1; }));
    lib.functions["KillTimer"] = fn((void*)static_cast<BOOL(*)(void*, intptr_t)>([](void*, intptr_t) -> BOOL { return 1; }));
    lib.functions["SetTimer"] = fn((void*)static_cast<intptr_t(*)(void*, intptr_t, UINT, void*)>([](void*, intptr_t, UINT, void*) -> intptr_t { return 1; }));

    lib.functions["UnregisterClassW"] = fn((void*)static_cast<BOOL(*)(const wchar_t*, void*)>([](const wchar_t*, void*) -> BOOL { return 1; }));

    lib.functions["GetProcessWindowStation"] = fn((void*)static_cast<void*(*)()>([]() -> void* { return reinterpret_cast<void*>(0x8000); }));
    lib.functions["GetUserObjectInformationW"] = fn((void*)static_cast<BOOL(*)(void*, int, void*, DWORD, DWORD*)>([](void*, int, void*, DWORD, DWORD*) -> BOOL { return 0; }));

    lib.functions["wsprintfA"] = fn((void*)shim_wsprintfA);

    return lib;
}

}
}
