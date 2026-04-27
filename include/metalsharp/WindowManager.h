#pragma once

#include <metalsharp/Win32Types.h>
#include <string>
#include <unordered_map>
#include <queue>
#include <functional>
#include <mutex>

namespace metalsharp {
namespace win32 {

constexpr UINT WM_DESTROY = 0x0002;
constexpr UINT WM_CLOSE = 0x0010;
constexpr UINT WM_QUIT = 0x0012;
constexpr UINT WM_PAINT = 0x000F;
constexpr UINT WM_SIZE = 0x0005;
constexpr UINT WM_ACTIVATE = 0x0006;
constexpr UINT WM_SETFOCUS = 0x0007;
constexpr UINT WM_KILLFOCUS = 0x0008;
constexpr UINT WM_ERASEBKGND = 0x0014;
constexpr UINT WM_SHOWWINDOW = 0x0018;
constexpr UINT WM_LBUTTONDOWN = 0x0201;
constexpr UINT WM_LBUTTONUP = 0x0202;
constexpr UINT WM_MOUSEMOVE = 0x0200;
constexpr UINT WM_RBUTTONDOWN = 0x0204;
constexpr UINT WM_RBUTTONUP = 0x0205;
constexpr UINT WM_KEYDOWN = 0x0100;
constexpr UINT WM_KEYUP = 0x0101;
constexpr UINT WM_CHAR = 0x0102;
constexpr UINT WM_TIMER = 0x0113;
constexpr UINT WM_MOUSEWHEEL = 0x020A;

struct POINT {
    LONG x;
    LONG y;
};

struct MSG {
    HANDLE hwnd;
    UINT message;
    uintptr_t wParam;
    intptr_t lParam;
    DWORD time;
    POINT pt;
};

typedef intptr_t (MSABI *WNDPROC)(HANDLE, UINT, uintptr_t, intptr_t);

struct WNDCLASS_STORE {
    std::string className;
    WNDPROC lpfnWndProc;
    int cbWndExtra;
    int cbClsExtra;
};

class WindowManager {
public:
    static WindowManager& instance();

    void init();

    HANDLE createWindow(DWORD dwExStyle, const wchar_t* lpClassName, const wchar_t* lpWindowName,
        DWORD dwStyle, int x, int y, int nWidth, int nHeight,
        HANDLE hWndParent, HANDLE hMenu, HANDLE hInstance, void* lpParam);

    WORD registerClass(const void* lpwcx);
    BOOL destroyWindow(HANDLE hWnd);
    BOOL showWindow(HANDLE hWnd, int nCmdShow);
    BOOL updateWindow(HANDLE hWnd);
    BOOL getWindowRect(HANDLE hWnd, void* lpRect);
    BOOL getClientRect(HANDLE hWnd, void* lpRect);
    BOOL moveWindow(HANDLE hWnd, int x, int y, int w, int h, BOOL repaint);
    BOOL setWindowPos(HANDLE hWnd, HANDLE hWndAfter, int x, int y, int w, int h, UINT flags);
    BOOL setWindowTextW(HANDLE hWnd, const wchar_t* lpString);

    BOOL getMessage(MSG* lpMsg, HANDLE hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax);
    BOOL peekMessage(MSG* lpMsg, HANDLE hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);
    BOOL translateMessage(const MSG* lpMsg);
    intptr_t dispatchMessage(const MSG* lpMsg);
    intptr_t sendMessage(HANDLE hWnd, UINT Msg, uintptr_t wParam, intptr_t lParam);
    BOOL postMessage(HANDLE hWnd, UINT Msg, uintptr_t wParam, intptr_t lParam);
    void postQuitMessage(int nExitCode);
    intptr_t defWindowProc(HANDLE hWnd, UINT Msg, uintptr_t wParam, intptr_t lParam);

    void pumpEvents();

    void* getNSWindow(HANDLE hWnd);

private:
    WindowManager();

    std::unordered_map<std::string, WNDCLASS_STORE> m_classes;
    std::unordered_map<uintptr_t, void*> m_hwndToNSWindow;
    std::unordered_map<void*, uintptr_t> m_nsWindowToHwnd;
    std::unordered_map<uintptr_t, WNDPROC> m_wndProcs;
    std::unordered_map<uintptr_t, std::string> m_hwndTitles;
    std::unordered_map<uintptr_t, std::queue<MSG>> m_messageQueues;
    std::queue<MSG> m_globalQueue;

    uintptr_t m_nextHwnd = 0x500000;
    bool m_quitReceived = false;
    int m_quitExitCode = 0;
    std::mutex m_mutex;
};

}
}
