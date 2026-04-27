#include <metalsharp/PELoader.h>
#include <metalsharp/Win32Types.h>
#include <metalsharp/Logger.h>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <unistd.h>

namespace metalsharp {
namespace win32 {

static void* s_fakeHwnd = reinterpret_cast<void*>(0x5000);
static DWORD s_msgResult = 0;

static int MSABI shim_wsprintfA(char* buf, const char* fmt, ...) {
    (void)fmt;
    if (buf) buf[0] = 0;
    return 0;
}

static void* MSABI stub_CreateWindowExW(DWORD, const wchar_t*, const wchar_t*, DWORD, int, int, int, int, void*, void*, void*, void*) {
    return s_fakeHwnd;
}

static WORD MSABI stub_RegisterClassExW(void*) {
    return 1;
}

static BOOL MSABI stub_DestroyWindow(void*) {
    return 1;
}

static BOOL MSABI stub_ShowWindow(void*, int) {
    return 1;
}

static BOOL MSABI stub_UpdateWindow(void*) {
    return 1;
}

static BOOL MSABI stub_GetMessageW(void* msg, void*, UINT, UINT) {
    memset(msg, 0, 48);
    reinterpret_cast<DWORD*>(msg)[1] = 0x0012;
    return 0;
}

static BOOL MSABI stub_PeekMessageW(void* msg, void*, UINT, UINT, UINT) {
    memset(msg, 0, 48);
    return 0;
}

static BOOL MSABI stub_TranslateMessage(const void*) {
    return 0;
}

static intptr_t MSABI stub_DispatchMessageW(const void*) {
    return 0;
}

static BOOL MSABI stub_PostThreadMessageW(DWORD, UINT, intptr_t, intptr_t) {
    return 1;
}

static intptr_t MSABI stub_SendMessageW(void*, UINT, intptr_t, intptr_t) {
    return 0;
}

static intptr_t MSABI stub_DefWindowProcW(void*, UINT, intptr_t, intptr_t) {
    return 0;
}

static void* MSABI stub_BeginPaint(void*, void* ps) {
    memset(ps, 0, 72);
    return reinterpret_cast<void*>(0x6000);
}

static BOOL MSABI stub_EndPaint(void*, void*) {
    return 1;
}

static void* MSABI stub_GetDC(void*) {
    return reinterpret_cast<void*>(0x6000);
}

static int MSABI stub_ReleaseDC(void*, void*) {
    return 1;
}

static BOOL MSABI stub_RedrawWindow(void*, const void*, void*, UINT) {
    return 1;
}

static BOOL MSABI stub_GetWindowRect(void*, void* rect) {
    auto* r = reinterpret_cast<LONG*>(rect);
    r[0] = 0; r[1] = 0; r[2] = 1920; r[3] = 1080;
    return 1;
}

static BOOL MSABI stub_MoveWindow(void*, int, int, int, int, BOOL) {
    return 1;
}

static BOOL MSABI stub_SetWindowPos(void*, void*, int, int, int, int, UINT) {
    return 1;
}

static BOOL MSABI stub_SetWindowTextW(void*, const wchar_t*) {
    return 1;
}

static intptr_t MSABI stub_SetWindowLongPtrW(void*, int, intptr_t) {
    return 0;
}

static intptr_t MSABI stub_GetWindowLongPtrW(void*, int) {
    return 0;
}

static ULONG MSABI stub_SetClassLongPtrW(void*, int, LONG) {
    return 0;
}

static void* MSABI stub_GetDesktopWindow() {
    return s_fakeHwnd;
}

static BOOL MSABI stub_IsWindowVisible(void*) {
    return 1;
}

static DWORD MSABI stub_GetWindowThreadProcessId(void*, DWORD* pid) {
    if (pid) *pid = (DWORD)getpid();
    return 1;
}

static BOOL MSABI stub_EnumWindows(void*, intptr_t) {
    return 1;
}

static BOOL MSABI stub_EnumChildWindows(void*, void*, intptr_t) {
    return 1;
}

static void* MSABI stub_LoadCursorW(void*, const wchar_t*) {
    return reinterpret_cast<void*>(0x7000);
}

static void* MSABI stub_LoadIconW(void*, const wchar_t*) {
    return reinterpret_cast<void*>(0x7001);
}

static int MSABI stub_MessageBoxA(void*, const char*, const char*, UINT) {
    return 1;
}

static int MSABI stub_MessageBoxW(void*, const wchar_t*, const wchar_t*, UINT) {
    return 1;
}

static BOOL MSABI stub_GetMonitorInfoW(void*, void* mi) {
    memset(mi, 0, 72);
    reinterpret_cast<DWORD*>(mi)[0] = 72;
    auto* r = reinterpret_cast<LONG*>((uint8_t*)mi + 4);
    r[0] = 0; r[1] = 0; r[2] = 1920; r[3] = 1080;
    return 1;
}

static void* MSABI stub_MonitorFromPoint(LONG, LONG, DWORD) {
    return s_fakeHwnd;
}

static void* MSABI stub_MonitorFromWindow(void*, DWORD) {
    return s_fakeHwnd;
}

static DWORD MSABI stub_MsgWaitForMultipleObjects(DWORD, void*, BOOL, DWORD, DWORD) {
    return WAIT_OBJECT_0;
}

static BOOL MSABI stub_OpenClipboard(void*) {
    return 1;
}

static BOOL MSABI stub_CloseClipboard() {
    return 1;
}

static BOOL MSABI stub_EmptyClipboard() {
    return 1;
}

static void* MSABI stub_SetClipboardData(UINT, void*) {
    return nullptr;
}

static BOOL MSABI stub_GetClassInfoExW(void*, const wchar_t*, void*) {
    return 0;
}

static intptr_t MSABI stub_DialogBoxParamA(void*, const char*, void*, void*, intptr_t) {
    return 0;
}

static BOOL MSABI stub_EndDialog(void*, intptr_t) {
    return 1;
}

static void* MSABI stub_GetDlgItem(void*, int) {
    return s_fakeHwnd;
}

static UINT MSABI stub_GetDlgItemInt(void*, int, BOOL*, BOOL) {
    return 0;
}

static BOOL MSABI stub_SetDlgItemInt(void*, int, UINT, BOOL) {
    return 1;
}

static BOOL MSABI stub_SetDlgItemTextA(void*, int, const char*) {
    return 1;
}

static int MSABI stub_GetWindowTextLengthA(void*) {
    return 0;
}

static int MSABI stub_MapWindowPoints(void*, void*, void*, UINT) {
    return 0;
}

static BOOL MSABI stub_AllowSetForegroundWindow(DWORD) {
    return 1;
}

static BOOL MSABI stub_KillTimer(void*, intptr_t) {
    return 1;
}

static intptr_t MSABI stub_SetTimer(void*, intptr_t, UINT, void*) {
    return 1;
}

static BOOL MSABI stub_UnregisterClassW(const wchar_t*, void*) {
    return 1;
}

static void* MSABI stub_GetProcessWindowStation() {
    return reinterpret_cast<void*>(0x8000);
}

static BOOL MSABI stub_GetUserObjectInformationW(void*, int, void*, DWORD, DWORD*) {
    return 0;
}

ShimLibrary createUser32Shim() {
    ShimLibrary lib;
    lib.name = "USER32.dll";

    auto fn = [](void* ptr) -> ExportedFunction {
        return [ptr]() -> void* { return ptr; };
    };

    (void)s_msgResult;

    lib.functions["CreateWindowExW"] = fn((void*)stub_CreateWindowExW);
    lib.functions["RegisterClassExW"] = fn((void*)stub_RegisterClassExW);
    lib.functions["DestroyWindow"] = fn((void*)stub_DestroyWindow);
    lib.functions["ShowWindow"] = fn((void*)stub_ShowWindow);
    lib.functions["UpdateWindow"] = fn((void*)stub_UpdateWindow);
    lib.functions["GetMessageW"] = fn((void*)stub_GetMessageW);
    lib.functions["PeekMessageW"] = fn((void*)stub_PeekMessageW);
    lib.functions["TranslateMessage"] = fn((void*)stub_TranslateMessage);
    lib.functions["DispatchMessageW"] = fn((void*)stub_DispatchMessageW);
    lib.functions["PostThreadMessageW"] = fn((void*)stub_PostThreadMessageW);
    lib.functions["SendMessageW"] = fn((void*)stub_SendMessageW);
    lib.functions["DefWindowProcW"] = fn((void*)stub_DefWindowProcW);
    lib.functions["BeginPaint"] = fn((void*)stub_BeginPaint);
    lib.functions["EndPaint"] = fn((void*)stub_EndPaint);
    lib.functions["GetDC"] = fn((void*)stub_GetDC);
    lib.functions["ReleaseDC"] = fn((void*)stub_ReleaseDC);
    lib.functions["RedrawWindow"] = fn((void*)stub_RedrawWindow);
    lib.functions["GetWindowRect"] = fn((void*)stub_GetWindowRect);
    lib.functions["MoveWindow"] = fn((void*)stub_MoveWindow);
    lib.functions["SetWindowPos"] = fn((void*)stub_SetWindowPos);
    lib.functions["SetWindowTextW"] = fn((void*)stub_SetWindowTextW);
    lib.functions["SetWindowLongPtrW"] = fn((void*)stub_SetWindowLongPtrW);
    lib.functions["GetWindowLongPtrW"] = fn((void*)stub_GetWindowLongPtrW);
    lib.functions["SetClassLongPtrW"] = fn((void*)stub_SetClassLongPtrW);
    lib.functions["GetDesktopWindow"] = fn((void*)stub_GetDesktopWindow);
    lib.functions["IsWindowVisible"] = fn((void*)stub_IsWindowVisible);
    lib.functions["GetWindowThreadProcessId"] = fn((void*)stub_GetWindowThreadProcessId);
    lib.functions["EnumWindows"] = fn((void*)stub_EnumWindows);
    lib.functions["EnumChildWindows"] = fn((void*)stub_EnumChildWindows);
    lib.functions["LoadCursorW"] = fn((void*)stub_LoadCursorW);
    lib.functions["LoadIconW"] = fn((void*)stub_LoadIconW);
    lib.functions["MessageBoxA"] = fn((void*)stub_MessageBoxA);
    lib.functions["MessageBoxW"] = fn((void*)stub_MessageBoxW);
    lib.functions["GetMonitorInfoW"] = fn((void*)stub_GetMonitorInfoW);
    lib.functions["MonitorFromPoint"] = fn((void*)stub_MonitorFromPoint);
    lib.functions["MonitorFromWindow"] = fn((void*)stub_MonitorFromWindow);
    lib.functions["MsgWaitForMultipleObjects"] = fn((void*)stub_MsgWaitForMultipleObjects);
    lib.functions["OpenClipboard"] = fn((void*)stub_OpenClipboard);
    lib.functions["CloseClipboard"] = fn((void*)stub_CloseClipboard);
    lib.functions["EmptyClipboard"] = fn((void*)stub_EmptyClipboard);
    lib.functions["SetClipboardData"] = fn((void*)stub_SetClipboardData);
    lib.functions["GetClassInfoExW"] = fn((void*)stub_GetClassInfoExW);
    lib.functions["DialogBoxParamA"] = fn((void*)stub_DialogBoxParamA);
    lib.functions["EndDialog"] = fn((void*)stub_EndDialog);
    lib.functions["GetDlgItem"] = fn((void*)stub_GetDlgItem);
    lib.functions["GetDlgItemInt"] = fn((void*)stub_GetDlgItemInt);
    lib.functions["SetDlgItemInt"] = fn((void*)stub_SetDlgItemInt);
    lib.functions["SetDlgItemTextA"] = fn((void*)stub_SetDlgItemTextA);
    lib.functions["GetWindowTextLengthA"] = fn((void*)stub_GetWindowTextLengthA);
    lib.functions["MapWindowPoints"] = fn((void*)stub_MapWindowPoints);
    lib.functions["AllowSetForegroundWindow"] = fn((void*)stub_AllowSetForegroundWindow);
    lib.functions["KillTimer"] = fn((void*)stub_KillTimer);
    lib.functions["SetTimer"] = fn((void*)stub_SetTimer);
    lib.functions["UnregisterClassW"] = fn((void*)stub_UnregisterClassW);
    lib.functions["GetProcessWindowStation"] = fn((void*)stub_GetProcessWindowStation);
    lib.functions["GetUserObjectInformationW"] = fn((void*)stub_GetUserObjectInformationW);
    lib.functions["wsprintfA"] = fn((void*)shim_wsprintfA);

    return lib;
}

}
}
