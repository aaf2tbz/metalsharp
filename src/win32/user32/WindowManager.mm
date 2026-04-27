#import <AppKit/AppKit.h>
#include <metalsharp/WindowManager.h>
#include <metalsharp/Logger.h>
#include <cstring>
#include <cstdlib>
#include <chrono>

@class MetalSharpWindowDelegate;

@interface MetalSharpWindowDelegate : NSObject <NSWindowDelegate>
@property (assign) metalsharp::win32::WindowManager* manager;
@property (assign) uintptr_t hwnd;
@end

@implementation MetalSharpWindowDelegate

- (void)windowWillClose:(NSNotification*)notification {
    (void)notification;
    _manager->postMessage(reinterpret_cast<metalsharp::win32::HANDLE>(_hwnd),
        metalsharp::win32::WM_CLOSE, 0, 0);
}

- (void)windowDidResize:(NSNotification*)notification {
    (void)notification;
    NSWindow* window = [notification object];
    NSRect content = [window contentRectForFrameRect:[window frame]];
    uintptr_t w = static_cast<uintptr_t>(content.size.width);
    uintptr_t h = static_cast<uintptr_t>(content.size.height);
    _manager->postMessage(reinterpret_cast<metalsharp::win32::HANDLE>(_hwnd),
        metalsharp::win32::WM_SIZE, 0, (intptr_t)((h << 16) | (w & 0xFFFF)));
}

- (void)windowDidBecomeKey:(NSNotification*)notification {
    (void)notification;
    _manager->postMessage(reinterpret_cast<metalsharp::win32::HANDLE>(_hwnd),
        metalsharp::win32::WM_ACTIVATE, 1, 0);
}

- (void)windowDidResignKey:(NSNotification*)notification {
    (void)notification;
    _manager->postMessage(reinterpret_cast<metalsharp::win32::HANDLE>(_hwnd),
        metalsharp::win32::WM_ACTIVATE, 0, 0);
}

@end

static uint16_t macKeyToVK(NSString* chars, unichar keyCode) {
    if ([chars length] > 0) {
        unichar ch = [chars characterAtIndex:0];
        if (ch >= 'A' && ch <= 'Z') return ch;
        if (ch >= 'a' && ch <= 'z') return toupper(ch);
        if (ch >= '0' && ch <= '9') return ch;
    }
    switch (keyCode) {
        case 0x7B: return 0x25;
        case 0x7C: return 0x27;
        case 0x7E: return 0x26;
        case 0x7D: return 0x28;
        case 0x24: return 0x0D;
        case 0x35: return 0x08;
        case 0x31: return 0x20;
        case 0x30: return 0x09;
        case 0x7A: return 0x70;
        case 0x78: return 0x71;
        case 0x63: return 0x72;
        case 0x76: return 0x73;
        case 0x60: return 0x74;
        case 0x61: return 0x75;
        case 0x62: return 0x76;
        case 0x64: return 0x77;
        case 0x65: return 0x78;
        case 0x6D: return 0x79;
        case 0x67: return 0x7A;
        case 0x6F: return 0x7B;
        case 0x69: return 0x7C;
        case 0x6B: return 0x7D;
        case 0x73: return 0x24;
        case 0x74: return 0x23;
        case 0x79: return 0x2D;
        default: return 0;
    }
}

namespace metalsharp {
namespace win32 {

WindowManager::WindowManager() {}

WindowManager& WindowManager::instance() {
    static WindowManager wm;
    return wm;
}

void WindowManager::init() {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    MS_INFO("WindowManager: NSApplication initialized");
}

HANDLE WindowManager::createWindow(DWORD dwExStyle, const wchar_t* lpClassName, const wchar_t* lpWindowName,
    DWORD dwStyle, int x, int y, int nWidth, int nHeight,
    HANDLE hWndParent, HANDLE hMenu, HANDLE hInstance, void* lpParam) {
    (void)dwExStyle; (void)hWndParent; (void)hMenu; (void)hInstance; (void)lpParam;

    std::string className;
    if (lpClassName) {
        for (int i = 0; lpClassName[i]; i++) className += (char)(lpClassName[i] & 0xFF);
    }

    WNDPROC wndProc = nullptr;
    auto it = m_classes.find(className);
    if (it != m_classes.end()) {
        wndProc = it->second.lpfnWndProc;
    }

    if (nWidth <= 0 || nWidth > 4096) nWidth = 800;
    if (nHeight <= 0 || nHeight > 4096) nHeight = 600;
    if (x < 0) x = 100;
    if (y < 0) y = 100;

    NSRect frame = NSMakeRect(x, 0, nWidth, nHeight);
    NSScreen* screen = [NSScreen mainScreen];
    if (screen) {
        NSRect screenFrame = [screen frame];
        frame.origin.y = screenFrame.size.height - y - nHeight;
    }

    NSUInteger styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
    if (!(dwStyle & 0x00C00000)) {
        styleMask &= ~NSWindowStyleMaskTitled;
    }

    NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
        styleMask:styleMask
        backing:NSBackingStoreBuffered
        defer:NO];

    if (lpWindowName) {
        std::vector<unichar> uchars;
        for (int i = 0; lpWindowName[i]; i++) uchars.push_back(lpWindowName[i]);
        NSString* title = uchars.empty() ? @"" :
            [NSString stringWithCharacters:uchars.data() length:uchars.size()];
        [window setTitle:title];
    }

    MetalSharpWindowDelegate* delegate = [[MetalSharpWindowDelegate alloc] init];
    delegate.manager = this;
    delegate.hwnd = m_nextHwnd;
    [window setDelegate:delegate];

    [window setAcceptsMouseMovedEvents:YES];

    uintptr_t h = m_nextHwnd;
    m_nextHwnd += 8;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_hwndToNSWindow[h] = (__bridge_retained void*)window;
        m_nsWindowToHwnd[(__bridge void*)window] = h;
        if (wndProc) m_wndProcs[h] = wndProc;
        m_messageQueues[h] = std::queue<MSG>();
    }

    MS_INFO("WindowManager: CreateWindow(\"%s\") -> HWND=0x%llX, NSWindow=%p, WNDPROC=%p",
        className.c_str(), (unsigned long long)h, window, wndProc);

    if (wndProc) {
        wndProc(reinterpret_cast<HANDLE>(h), 0x0001, 0, 0);
    }

    return reinterpret_cast<HANDLE>(h);
}

WORD WindowManager::registerClass(const void* lpwcx) {
    const uint8_t* data = static_cast<const uint8_t*>(lpwcx);

    WNDPROC wndProc;
    memcpy(&wndProc, data + 8, 8);

    const wchar_t* className = *reinterpret_cast<const wchar_t* const*>(data + 40);
    std::string name;
    if (className) {
        for (int i = 0; className[i]; i++) name += (char)(className[i] & 0xFF);
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_classes[name] = {name, wndProc, 0, 0};

    MS_INFO("WindowManager: RegisterClass(\"%s\", WNDPROC=%p)", name.c_str(), wndProc);

    static WORD atom = 0xC000;
    return atom++;
}

BOOL WindowManager::destroyWindow(HANDLE hWnd) {
    uintptr_t h = reinterpret_cast<uintptr_t>(hWnd);
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_hwndToNSWindow.find(h);
    if (it == m_hwndToNSWindow.end()) return 0;

    NSWindow* window = (__bridge NSWindow*)it->second;

    auto wndIt = m_wndProcs.find(h);
    if (wndIt != m_wndProcs.end()) {
        wndIt->second(hWnd, WM_DESTROY, 0, 0);
    }

    [window close];
    m_hwndToNSWindow.erase(h);
    m_nsWindowToHwnd.erase(it->second);
    m_wndProcs.erase(h);
    m_messageQueues.erase(h);

    MS_INFO("WindowManager: DestroyWindow(0x%llX)", (unsigned long long)h);
    return 1;
}

BOOL WindowManager::showWindow(HANDLE hWnd, int nCmdShow) {
    uintptr_t h = reinterpret_cast<uintptr_t>(hWnd);
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_hwndToNSWindow.find(h);
    if (it == m_hwndToNSWindow.end()) return 0;

    NSWindow* window = (__bridge NSWindow*)it->second;
    if (nCmdShow == 5 || nCmdShow == 1 || nCmdShow == 3) {
        [window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
    } else if (nCmdShow == 0 || nCmdShow == 6) {
        [window orderOut:nil];
    } else if (nCmdShow == 2) {
        [window miniaturize:nil];
    }

    return 1;
}

BOOL WindowManager::updateWindow(HANDLE hWnd) {
    (void)hWnd;
    return 1;
}

BOOL WindowManager::getWindowRect(HANDLE hWnd, void* lpRect) {
    uintptr_t h = reinterpret_cast<uintptr_t>(hWnd);
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_hwndToNSWindow.find(h);
    if (it == m_hwndToNSWindow.end()) return 0;

    NSWindow* window = (__bridge NSWindow*)it->second;
    NSRect frame = [window frame];
    NSScreen* screen = [NSScreen mainScreen];
    CGFloat screenHeight = screen ? [screen frame].size.height : 1080;

    auto* r = reinterpret_cast<LONG*>(lpRect);
    r[0] = static_cast<LONG>(frame.origin.x);
    r[1] = static_cast<LONG>(screenHeight - frame.origin.y - frame.size.height);
    r[2] = static_cast<LONG>(frame.origin.x + frame.size.width);
    r[3] = static_cast<LONG>(screenHeight - frame.origin.y);
    return 1;
}

BOOL WindowManager::getClientRect(HANDLE hWnd, void* lpRect) {
    uintptr_t h = reinterpret_cast<uintptr_t>(hWnd);
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_hwndToNSWindow.find(h);
    if (it == m_hwndToNSWindow.end()) return 0;

    NSWindow* window = (__bridge NSWindow*)it->second;
    NSRect content = [window contentRectForFrameRect:[window frame]];

    auto* r = reinterpret_cast<LONG*>(lpRect);
    r[0] = 0;
    r[1] = 0;
    r[2] = static_cast<LONG>(content.size.width);
    r[3] = static_cast<LONG>(content.size.height);
    return 1;
}

BOOL WindowManager::moveWindow(HANDLE hWnd, int x, int y, int w, int h, BOOL repaint) {
    (void)repaint;
    uintptr_t hwnd = reinterpret_cast<uintptr_t>(hWnd);
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_hwndToNSWindow.find(hwnd);
    if (it == m_hwndToNSWindow.end()) return 0;

    NSWindow* window = (__bridge NSWindow*)it->second;
    NSScreen* screen = [NSScreen mainScreen];
    CGFloat screenHeight = screen ? [screen frame].size.height : 1080;
    NSRect frame = NSMakeRect(x, screenHeight - y - h, w, h);
    [window setFrame:frame display:YES];
    return 1;
}

BOOL WindowManager::setWindowPos(HANDLE hWnd, HANDLE hWndAfter, int x, int y, int w, int h, UINT flags) {
    (void)hWndAfter; (void)flags;
    return moveWindow(hWnd, x, y, w, h, 1);
}

BOOL WindowManager::setWindowTextW(HANDLE hWnd, const wchar_t* lpString) {
    uintptr_t h = reinterpret_cast<uintptr_t>(hWnd);
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_hwndToNSWindow.find(h);
    if (it == m_hwndToNSWindow.end()) return 0;

    NSWindow* window = (__bridge NSWindow*)it->second;
    if (lpString) {
        std::vector<unichar> uchars;
        for (int i = 0; lpString[i]; i++) uchars.push_back(lpString[i]);
        NSString* title = uchars.empty() ? @"" :
            [NSString stringWithCharacters:uchars.data() length:uchars.size()];
        [window setTitle:title];
    }
    return 1;
}

void WindowManager::pumpEvents() {
    @autoreleasepool {
        for (;;) {
            NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                untilDate:[NSDate distantPast]
                inMode:NSDefaultRunLoopMode
                dequeue:YES];

            if (!event) break;

            NSWindow* eventWindow = [event window];
            uintptr_t targetHwnd = 0;

            if (eventWindow) {
                auto hit = m_nsWindowToHwnd.find((__bridge void*)eventWindow);
                if (hit != m_nsWindowToHwnd.end()) targetHwnd = hit->second;
            }

            switch ([event type]) {
                case NSEventTypeLeftMouseDown: {
                    if (targetHwnd) {
                        NSPoint loc = [event locationInWindow];
                        postMessage(reinterpret_cast<HANDLE>(targetHwnd), WM_LBUTTONDOWN, 0,
                            (intptr_t)((static_cast<int>(loc.y) << 16) | (static_cast<int>(loc.x) & 0xFFFF)));
                    }
                    break;
                }
                case NSEventTypeLeftMouseUp: {
                    if (targetHwnd) {
                        NSPoint loc = [event locationInWindow];
                        postMessage(reinterpret_cast<HANDLE>(targetHwnd), WM_LBUTTONUP, 0,
                            (intptr_t)((static_cast<int>(loc.y) << 16) | (static_cast<int>(loc.x) & 0xFFFF)));
                    }
                    break;
                }
                case NSEventTypeMouseMoved:
                case NSEventTypeLeftMouseDragged: {
                    if (targetHwnd) {
                        NSPoint loc = [event locationInWindow];
                        postMessage(reinterpret_cast<HANDLE>(targetHwnd), WM_MOUSEMOVE, 0,
                            (intptr_t)((static_cast<int>(loc.y) << 16) | (static_cast<int>(loc.x) & 0xFFFF)));
                    }
                    break;
                }
                case NSEventTypeRightMouseDown: {
                    if (targetHwnd) {
                        NSPoint loc = [event locationInWindow];
                        postMessage(reinterpret_cast<HANDLE>(targetHwnd), WM_RBUTTONDOWN, 0,
                            (intptr_t)((static_cast<int>(loc.y) << 16) | (static_cast<int>(loc.x) & 0xFFFF)));
                    }
                    break;
                }
                case NSEventTypeRightMouseUp: {
                    if (targetHwnd) {
                        NSPoint loc = [event locationInWindow];
                        postMessage(reinterpret_cast<HANDLE>(targetHwnd), WM_RBUTTONUP, 0,
                            (intptr_t)((static_cast<int>(loc.y) << 16) | (static_cast<int>(loc.x) & 0xFFFF)));
                    }
                    break;
                }
                case NSEventTypeKeyDown: {
                    if (targetHwnd) {
                        uint16_t vk = macKeyToVK([event characters], [event keyCode]);
                        postMessage(reinterpret_cast<HANDLE>(targetHwnd), WM_KEYDOWN, vk, 1);
                        NSString* chars = [event characters];
                        if ([chars length] > 0) {
                            unichar ch = [chars characterAtIndex:0];
                            if (ch >= 32 && ch < 127) {
                                postMessage(reinterpret_cast<HANDLE>(targetHwnd), WM_CHAR, ch, 1);
                            }
                        }
                    }
                    break;
                }
                case NSEventTypeKeyUp: {
                    if (targetHwnd) {
                        uint16_t vk = macKeyToVK([event characters], [event keyCode]);
                        postMessage(reinterpret_cast<HANDLE>(targetHwnd), WM_KEYUP, vk, 1);
                    }
                    break;
                }
                case NSEventTypeScrollWheel: {
                    if (targetHwnd) {
                        CGFloat delta = [event deltaY];
                        if (delta != 0) {
                            uintptr_t wParam = (static_cast<uintptr_t>(static_cast<int>(delta * 120)) << 16);
                            NSPoint loc = [event locationInWindow];
                            postMessage(reinterpret_cast<HANDLE>(targetHwnd), WM_MOUSEWHEEL, wParam,
                                (intptr_t)((static_cast<int>(loc.y) << 16) | (static_cast<int>(loc.x) & 0xFFFF)));
                        }
                    }
                    break;
                }
                default:
                    break;
            }

            [NSApp sendEvent:event];
        }
    }
}

BOOL WindowManager::getMessage(MSG* lpMsg, HANDLE hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) {
    (void)hWnd; (void)wMsgFilterMin; (void)wMsgFilterMax;

    while (!m_quitReceived) {
        pumpEvents();

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_globalQueue.empty()) {
                *lpMsg = m_globalQueue.front();
                m_globalQueue.pop();
                if (lpMsg->message == WM_QUIT) {
                    m_quitReceived = true;
                    return 0;
                }
                return 1;
            }

            for (auto& [hwnd, queue] : m_messageQueues) {
                if (!queue.empty()) {
                    *lpMsg = queue.front();
                    queue.pop();
                    return 1;
                }
            }
        }

        usleep(1000);
    }

    lpMsg->hwnd = nullptr;
    lpMsg->message = WM_QUIT;
    lpMsg->wParam = m_quitExitCode;
    lpMsg->lParam = 0;
    return 0;
}

BOOL WindowManager::peekMessage(MSG* lpMsg, HANDLE hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) {
    (void)hWnd; (void)wMsgFilterMin; (void)wMsgFilterMax;

    pumpEvents();

    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_globalQueue.empty()) {
        *lpMsg = m_globalQueue.front();
        if (wRemoveMsg & 0x0001) m_globalQueue.pop();
        return lpMsg->message != WM_QUIT ? 1 : 0;
    }

    for (auto& [hwnd, queue] : m_messageQueues) {
        if (!queue.empty()) {
            *lpMsg = queue.front();
            if (wRemoveMsg & 0x0001) queue.pop();
            return 1;
        }
    }

    return 0;
}

BOOL WindowManager::translateMessage(const MSG* lpMsg) {
    (void)lpMsg;
    return 0;
}

intptr_t WindowManager::dispatchMessage(const MSG* lpMsg) {
    if (!lpMsg) return 0;

    uintptr_t h = reinterpret_cast<uintptr_t>(lpMsg->hwnd);
    auto it = m_wndProcs.find(h);
    if (it != m_wndProcs.end()) {
        return it->second(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
    }

    return defWindowProc(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
}

intptr_t WindowManager::sendMessage(HANDLE hWnd, UINT Msg, uintptr_t wParam, intptr_t lParam) {
    uintptr_t h = reinterpret_cast<uintptr_t>(hWnd);
    auto it = m_wndProcs.find(h);
    if (it != m_wndProcs.end()) {
        return it->second(hWnd, Msg, wParam, lParam);
    }
    return defWindowProc(hWnd, Msg, wParam, lParam);
}

BOOL WindowManager::postMessage(HANDLE hWnd, UINT Msg, uintptr_t wParam, intptr_t lParam) {
    MSG msg;
    msg.hwnd = hWnd;
    msg.message = Msg;
    msg.wParam = wParam;
    msg.lParam = lParam;
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    msg.time = static_cast<DWORD>(ms & 0xFFFFFFFF);
    msg.pt = {0, 0};

    std::lock_guard<std::mutex> lock(m_mutex);
    uintptr_t h = reinterpret_cast<uintptr_t>(hWnd);
    auto it = m_messageQueues.find(h);
    if (it != m_messageQueues.end()) {
        it->second.push(msg);
    } else {
        m_globalQueue.push(msg);
    }
    return 1;
}

void WindowManager::postQuitMessage(int nExitCode) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_quitExitCode = nExitCode;
    MSG msg;
    msg.hwnd = nullptr;
    msg.message = WM_QUIT;
    msg.wParam = static_cast<uintptr_t>(nExitCode);
    msg.lParam = 0;
    msg.time = 0;
    msg.pt = {0, 0};
    m_globalQueue.push(msg);
    m_quitReceived = true;
}

intptr_t WindowManager::defWindowProc(HANDLE hWnd, UINT Msg, uintptr_t wParam, intptr_t lParam) {
    switch (Msg) {
        case WM_DESTROY:
            postQuitMessage(0);
            return 0;
        case WM_CLOSE:
            destroyWindow(hWnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        default:
            return 0;
    }
}

void* WindowManager::getNSWindow(HANDLE hWnd) {
    uintptr_t h = reinterpret_cast<uintptr_t>(hWnd);
    auto it = m_hwndToNSWindow.find(h);
    if (it != m_hwndToNSWindow.end()) return it->second;
    return nullptr;
}

}
}
