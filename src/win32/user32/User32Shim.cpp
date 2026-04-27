#include <metalsharp/PELoader.h>
#include <metalsharp/Win32Types.h>
#include <metalsharp/Logger.h>
#include <metalsharp/WindowManager.h>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <unistd.h>

namespace metalsharp {
namespace win32 {

static int msabi_wsprintfA_impl(char* buf, const char* fmt, void* ap) {
    (void)fmt; (void)ap;
    if (buf) buf[0] = 0;
    return 0;
}

static int MSABI shim_wsprintfA(char* buf, const char* fmt, ...) {
    if (buf) buf[0] = 0;
    (void)fmt;
    return 0;
}

static void* MSABI shim_CreateWindowExW(DWORD dwExStyle, const wchar_t* lpClassName, const wchar_t* lpWindowName,
    DWORD dwStyle, int x, int y, int nWidth, int nHeight,
    void* hWndParent, void* hMenu, void* hInstance, void* lpParam) {
    return WindowManager::instance().createWindow(dwExStyle, lpClassName, lpWindowName, dwStyle,
        x, y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
}

static WORD MSABI shim_RegisterClassExW(void* lpwcx) {
    return WindowManager::instance().registerClass(lpwcx);
}

static BOOL MSABI shim_DestroyWindow(void* hWnd) {
    return WindowManager::instance().destroyWindow(hWnd);
}

static BOOL MSABI shim_ShowWindow(void* hWnd, int nCmdShow) {
    return WindowManager::instance().showWindow(hWnd, nCmdShow);
}

static BOOL MSABI shim_UpdateWindow(void* hWnd) {
    return WindowManager::instance().updateWindow(hWnd);
}

static BOOL MSABI shim_GetMessageW(void* msg, void* hWnd, UINT min, UINT max) {
    return WindowManager::instance().getMessage(reinterpret_cast<MSG*>(msg), hWnd, min, max);
}

static BOOL MSABI shim_PeekMessageW(void* msg, void* hWnd, UINT min, UINT max, UINT remove) {
    return WindowManager::instance().peekMessage(reinterpret_cast<MSG*>(msg), hWnd, min, max, remove);
}

static BOOL MSABI shim_TranslateMessage(const void* msg) {
    return WindowManager::instance().translateMessage(reinterpret_cast<const MSG*>(msg));
}

static intptr_t MSABI shim_DispatchMessageW(const void* msg) {
    return WindowManager::instance().dispatchMessage(reinterpret_cast<const MSG*>(msg));
}

static BOOL MSABI shim_PostThreadMessageW(DWORD threadId, UINT msg, intptr_t wParam, intptr_t lParam) {
    MSG m;
    m.hwnd = nullptr;
    m.message = msg;
    m.wParam = static_cast<uintptr_t>(wParam);
    m.lParam = lParam;
    m.time = 0;
    m.pt = {0, 0};
    (void)threadId;
    return WindowManager::instance().postMessage(nullptr, msg, static_cast<uintptr_t>(wParam), lParam);
}

static intptr_t MSABI shim_SendMessageW(void* hWnd, UINT msg, intptr_t wParam, intptr_t lParam) {
    return WindowManager::instance().sendMessage(hWnd, msg, static_cast<uintptr_t>(wParam), lParam);
}

static intptr_t MSABI shim_DefWindowProcW(void* hWnd, UINT msg, intptr_t wParam, intptr_t lParam) {
    return WindowManager::instance().defWindowProc(hWnd, msg, static_cast<uintptr_t>(wParam), lParam);
}

static void* MSABI stub_BeginPaint(void* hWnd, void* ps) {
    (void)hWnd;
    memset(ps, 0, 72);
    return reinterpret_cast<void*>(0x6000);
}

static BOOL MSABI stub_EndPaint(void*, void*) { return 1; }

static void* MSABI stub_GetDC(void*) { return reinterpret_cast<void*>(0x6000); }

static int MSABI stub_ReleaseDC(void*, void*) { return 1; }

static BOOL MSABI stub_RedrawWindow(void*, const void*, void*, UINT) { return 1; }

static BOOL MSABI shim_GetWindowRect(void* hWnd, void* rect) {
    return WindowManager::instance().getWindowRect(hWnd, rect);
}

static BOOL MSABI shim_MoveWindow(void* hWnd, int x, int y, int w, int h, BOOL repaint) {
    return WindowManager::instance().moveWindow(hWnd, x, y, w, h, repaint);
}

static BOOL MSABI shim_SetWindowPos(void* hWnd, void* after, int x, int y, int w, int h, UINT flags) {
    return WindowManager::instance().setWindowPos(hWnd, after, x, y, w, h, flags);
}

static BOOL MSABI shim_SetWindowTextW(void* hWnd, const wchar_t* text) {
    return WindowManager::instance().setWindowTextW(hWnd, text);
}

static BOOL MSABI shim_GetClientRect(void* hWnd, void* rect) {
    return WindowManager::instance().getClientRect(hWnd, rect);
}

static intptr_t MSABI stub_SetWindowLongPtrW(void*, int, intptr_t) { return 0; }
static intptr_t MSABI stub_GetWindowLongPtrW(void*, int) { return 0; }
static ULONG MSABI stub_SetClassLongPtrW(void*, int, LONG) { return 0; }

static void* MSABI stub_GetDesktopWindow() {
    return reinterpret_cast<void*>(0x5000);
}

static BOOL MSABI stub_IsWindowVisible(void*) { return 1; }

static DWORD MSABI stub_GetWindowThreadProcessId(void*, DWORD* pid) {
    if (pid) *pid = (DWORD)getpid();
    return 1;
}

static BOOL MSABI stub_EnumWindows(void*, intptr_t) { return 1; }
static BOOL MSABI stub_EnumChildWindows(void*, void*, intptr_t) { return 1; }

static void* MSABI stub_LoadCursorW(void*, const wchar_t*) {
    return reinterpret_cast<void*>(0x7000);
}

static void* MSABI stub_LoadIconW(void*, const wchar_t*) {
    return reinterpret_cast<void*>(0x7001);
}

static int MSABI stub_MessageBoxA(void*, const char*, const char*, UINT) { return 1; }
static int MSABI stub_MessageBoxW(void*, const wchar_t*, const wchar_t*, UINT) { return 1; }

static BOOL MSABI stub_GetMonitorInfoW(void*, void* mi) {
    memset(mi, 0, 72);
    reinterpret_cast<DWORD*>(mi)[0] = 72;
    auto* r = reinterpret_cast<LONG*>((uint8_t*)mi + 4);
    r[0] = 0; r[1] = 0; r[2] = 1920; r[3] = 1080;
    return 1;
}

static void* MSABI stub_MonitorFromPoint(LONG, LONG, DWORD) {
    return reinterpret_cast<void*>(0x5000);
}

static void* MSABI stub_MonitorFromWindow(void*, DWORD) {
    return reinterpret_cast<void*>(0x5000);
}

static DWORD MSABI stub_MsgWaitForMultipleObjects(DWORD, void*, BOOL, DWORD, DWORD) {
    return WAIT_OBJECT_0;
}

static BOOL MSABI stub_OpenClipboard(void*) { return 1; }
static BOOL MSABI stub_CloseClipboard() { return 1; }
static BOOL MSABI stub_EmptyClipboard() { return 1; }
static void* MSABI stub_SetClipboardData(UINT, void*) { return nullptr; }
static BOOL MSABI stub_GetClassInfoExW(void*, const wchar_t*, void*) { return 0; }
static intptr_t MSABI stub_DialogBoxParamA(void*, const char*, void*, void*, intptr_t) { return 0; }
static BOOL MSABI stub_EndDialog(void*, intptr_t) { return 1; }
static void* MSABI stub_GetDlgItem(void*, int) { return reinterpret_cast<void*>(0x5000); }
static UINT MSABI stub_GetDlgItemInt(void*, int, BOOL*, BOOL) { return 0; }
static BOOL MSABI stub_SetDlgItemInt(void*, int, UINT, BOOL) { return 1; }
static BOOL MSABI stub_SetDlgItemTextA(void*, int, const char*) { return 1; }
static int MSABI stub_GetWindowTextLengthA(void*) { return 0; }
static int MSABI stub_MapWindowPoints(void*, void*, void*, UINT) { return 0; }
static BOOL MSABI stub_AllowSetForegroundWindow(DWORD) { return 1; }
static BOOL MSABI stub_KillTimer(void*, intptr_t) { return 1; }
static intptr_t MSABI stub_SetTimer(void*, intptr_t, UINT, void*) { return 1; }
static BOOL MSABI stub_UnregisterClassW(const wchar_t*, void*) { return 1; }
static void* MSABI stub_GetProcessWindowStation() { return reinterpret_cast<void*>(0x8000); }
static BOOL MSABI stub_GetUserObjectInformationW(void*, int, void*, DWORD, DWORD*) { return 0; }

static BOOL MSABI shim_PostMessageW(void* hWnd, UINT msg, uintptr_t wParam, intptr_t lParam) {
    return WindowManager::instance().postMessage(hWnd, msg, wParam, lParam);
}

ShimLibrary createUser32Shim() {
    ShimLibrary lib;
    lib.name = "USER32.dll";

    auto fn = [](void* ptr) -> ExportedFunction {
        return [ptr]() -> void* { return ptr; };
    };

    lib.functions["CreateWindowExW"] = fn((void*)shim_CreateWindowExW);
    lib.functions["RegisterClassExW"] = fn((void*)shim_RegisterClassExW);
    lib.functions["DestroyWindow"] = fn((void*)shim_DestroyWindow);
    lib.functions["ShowWindow"] = fn((void*)shim_ShowWindow);
    lib.functions["UpdateWindow"] = fn((void*)shim_UpdateWindow);
    lib.functions["GetMessageW"] = fn((void*)shim_GetMessageW);
    lib.functions["PeekMessageW"] = fn((void*)shim_PeekMessageW);
    lib.functions["TranslateMessage"] = fn((void*)shim_TranslateMessage);
    lib.functions["DispatchMessageW"] = fn((void*)shim_DispatchMessageW);
    lib.functions["PostThreadMessageW"] = fn((void*)shim_PostThreadMessageW);
    lib.functions["SendMessageW"] = fn((void*)shim_SendMessageW);
    lib.functions["SendMessageA"] = fn((void*)shim_SendMessageW);
    lib.functions["DefWindowProcW"] = fn((void*)shim_DefWindowProcW);
    lib.functions["PostMessageW"] = fn((void*)shim_PostMessageW);
    lib.functions["BeginPaint"] = fn((void*)stub_BeginPaint);
    lib.functions["EndPaint"] = fn((void*)stub_EndPaint);
    lib.functions["GetDC"] = fn((void*)stub_GetDC);
    lib.functions["ReleaseDC"] = fn((void*)stub_ReleaseDC);
    lib.functions["RedrawWindow"] = fn((void*)stub_RedrawWindow);
    lib.functions["GetWindowRect"] = fn((void*)shim_GetWindowRect);
    lib.functions["GetClientRect"] = fn((void*)shim_GetClientRect);
    lib.functions["MoveWindow"] = fn((void*)shim_MoveWindow);
    lib.functions["SetWindowPos"] = fn((void*)shim_SetWindowPos);
    lib.functions["SetWindowTextW"] = fn((void*)shim_SetWindowTextW);
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
