#include <metalsharp/PELoader.h>
#include <metalsharp/Win32Types.h>
#include <metalsharp/Logger.h>
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <unordered_map>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>

#if __has_include(<openssl/rand.h>)
#include <openssl/rand.h>
#endif

namespace metalsharp {
namespace win32 {

ShimLibrary createGdi32Shim() {
    ShimLibrary lib;
    lib.name = "GDI32.dll";
    auto fn = [](void* ptr) -> ExportedFunction { return [ptr]() -> void* { return ptr; }; };

    lib.functions["CreateCompatibleDC"] = fn((void*)static_cast<void*(*)(void*)>([](void*) -> void* { return reinterpret_cast<void*>(0x9000); }));
    lib.functions["CreateDIBSection"] = fn((void*)static_cast<void*(*)(void*, const void*, UINT, void**, void*, DWORD)>([](void*, const void*, UINT, void** ppv, void*, DWORD) -> void* {
        if (ppv) *ppv = calloc(1, 1920 * 1080 * 4);
        return reinterpret_cast<void*>(0x9001);
    }));
    lib.functions["CreateFontW"] = fn((void*)static_cast<void*(*)(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, const wchar_t*)>(
        [](int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, const wchar_t*) -> void* { return reinterpret_cast<void*>(0x9002); }));
    lib.functions["CreateICW"] = fn((void*)static_cast<void*(*)(const wchar_t*, const wchar_t*, const wchar_t*, const void*)>(
        [](const wchar_t*, const wchar_t*, const wchar_t*, const void*) -> void* { return reinterpret_cast<void*>(0x9003); }));
    lib.functions["DeleteDC"] = fn((void*)static_cast<BOOL(*)(void*)>([](void*) -> BOOL { return 1; }));
    lib.functions["DeleteObject"] = fn((void*)static_cast<BOOL(*)(void*)>([](void*) -> BOOL { return 1; }));
    lib.functions["SelectObject"] = fn((void*)static_cast<void*(*)(void*, void*)>([](void*, void* old) -> void* { return old; }));
    lib.functions["GetDeviceCaps"] = fn((void*)static_cast<int(*)(void*, int)>([](void*, int index) -> int {
        switch (index) { case 0: return 1920; case 1: return 1080; case 12: return 96; case 88: return 32; default: return 0; }
    }));
    lib.functions["GetStockObject"] = fn((void*)static_cast<void*(*)(int)>([](int) -> void* { return reinterpret_cast<void*>(0x9004); }));
    lib.functions["GetTextExtentPoint32W"] = fn((void*)static_cast<BOOL(*)(void*, const wchar_t*, int, void*)>([](void*, const wchar_t*, int, void* sz) -> BOOL {
        auto* s = reinterpret_cast<DWORD*>(sz); s[0] = 8; s[1] = 16; return 1;
    }));
    lib.functions["SetBkColor"] = fn((void*)static_cast<DWORD(*)(void*, DWORD)>([](void*, DWORD c) -> DWORD { return c; }));
    lib.functions["SetBkMode"] = fn((void*)static_cast<int(*)(void*, int)>([](void*, int m) -> int { return m; }));
    lib.functions["SetTextColor"] = fn((void*)static_cast<DWORD(*)(void*, DWORD)>([](void*, DWORD c) -> DWORD { return c; }));
    lib.functions["TextOutW"] = fn((void*)static_cast<BOOL(*)(void*, int, int, const wchar_t*, int)>([](void*, int, int, const wchar_t*, int) -> BOOL { return 1; }));
    lib.functions["SwapBuffers"] = fn((void*)static_cast<BOOL(*)(void*)>([](void*) -> BOOL { return 1; }));
    lib.functions["ChoosePixelFormat"] = fn((void*)static_cast<int(*)(void*, const void*)>([](void*, const void*) -> int { return 1; }));
    lib.functions["SetPixelFormat"] = fn((void*)static_cast<BOOL(*)(void*, int, const void*)>([](void*, int, const void*) -> BOOL { return 1; }));
    lib.functions["AddFontMemResourceEx"] = fn((void*)static_cast<HANDLE(*)(const void*, DWORD, void*, DWORD*)>([](const void*, DWORD, void*, DWORD* cnt) -> HANDLE {
        if (cnt) *cnt = 1; return reinterpret_cast<void*>(0x9005);
    }));
    lib.functions["RemoveFontMemResourceEx"] = fn((void*)static_cast<BOOL(*)(void*)>([](void*) -> BOOL { return 1; }));

    return lib;
}

ShimLibrary createAdvapi32Shim() {
    ShimLibrary lib;
    lib.name = "ADVAPI32.dll";
    auto fn = [](void* ptr) -> ExportedFunction { return [ptr]() -> void* { return ptr; }; };

    static std::unordered_map<std::string, std::string> s_registry;
    static uintptr_t s_nextKey = 0xA000;

    lib.functions["RegOpenKeyA"] = fn((void*)static_cast<LONG(*)(void*, const char*, void**)>([](void*, const char*, void** key) -> LONG {
        if (key) *key = reinterpret_cast<void*>(s_nextKey++);
        return 0;
    }));
    lib.functions["RegOpenKeyExA"] = fn((void*)static_cast<LONG(*)(void*, const char*, DWORD, DWORD, void**)>([](void*, const char*, DWORD, DWORD, void** key) -> LONG {
        if (key) *key = reinterpret_cast<void*>(s_nextKey++);
        return 0;
    }));
    lib.functions["RegCreateKeyExA"] = fn((void*)static_cast<LONG(*)(void*, const char*, DWORD, char*, DWORD, DWORD, void*, void**, void*)>(
        [](void*, const char*, DWORD, char*, DWORD, DWORD, void*, void** key, void*) -> LONG {
            if (key) *key = reinterpret_cast<void*>(s_nextKey++);
            return 0;
        }));
    lib.functions["RegCloseKey"] = fn((void*)static_cast<LONG(*)(void*)>([](void*) -> LONG { return 0; }));
    lib.functions["RegQueryValueExA"] = fn((void*)static_cast<LONG(*)(void*, const char*, DWORD*, DWORD*, BYTE*, DWORD*)>(
        [](void*, const char*, DWORD*, DWORD*, BYTE* data, DWORD* size) -> LONG { return 2; }));
    lib.functions["RegQueryValueExW"] = fn((void*)static_cast<LONG(*)(void*, const wchar_t*, DWORD*, DWORD*, BYTE*, DWORD*)>(
        [](void*, const wchar_t*, DWORD*, DWORD*, BYTE*, DWORD*) -> LONG { return 2; }));
    lib.functions["RegSetValueExA"] = fn((void*)static_cast<LONG(*)(void*, const char*, DWORD, DWORD, const BYTE*, DWORD)>([](void*, const char*, DWORD, DWORD, const BYTE*, DWORD) -> LONG { return 0; }));
    lib.functions["RegSetValueExW"] = fn((void*)static_cast<LONG(*)(void*, const wchar_t*, DWORD, DWORD, const BYTE*, DWORD)>([](void*, const wchar_t*, DWORD, DWORD, const BYTE*, DWORD) -> LONG { return 0; }));
    lib.functions["RegDeleteValueA"] = fn((void*)static_cast<LONG(*)(void*, const char*)>([](void*, const char*) -> LONG { return 0; }));
    lib.functions["InitializeSecurityDescriptor"] = fn((void*)static_cast<BOOL(*)(void*, DWORD)>([](void* sd, DWORD) -> BOOL { memset(sd, 0, 32); return 1; }));
    lib.functions["SetSecurityDescriptorDacl"] = fn((void*)static_cast<BOOL(*)(void*, BOOL, void*, BOOL)>([](void*, BOOL, void*, BOOL) -> BOOL { return 1; }));
    lib.functions["RegisterEventSourceW"] = fn((void*)static_cast<void*(*)(const wchar_t*, const wchar_t*)>([](const wchar_t*, const wchar_t*) -> void* { return reinterpret_cast<void*>(0xA100); }));
    lib.functions["DeregisterEventSource"] = fn((void*)static_cast<BOOL(*)(void*)>([](void*) -> BOOL { return 1; }));
    lib.functions["ReportEventW"] = fn((void*)static_cast<BOOL(*)(void*, WORD, WORD, DWORD, void*, WORD, DWORD, const wchar_t**, void*)>(
        [](void*, WORD, WORD, DWORD, void*, WORD, DWORD, const wchar_t**, void*) -> BOOL { return 1; }));

    return lib;
}

static std::unordered_map<int, int> g_sockets;
static int g_nextSock = 100;

static int sockFromHandle(intptr_t h) {
    auto it = g_sockets.find((int)h);
    return it != g_sockets.end() ? it->second : -1;
}

ShimLibrary createWs2_32Shim() {
    ShimLibrary lib;
    lib.name = "WS2_32.dll";
    auto fn = [](void* ptr) -> ExportedFunction { return [ptr]() -> void* { return ptr; }; };

    lib.functions["WSAStartup"] = fn((void*)static_cast<int(*)(WORD, void*)>([](WORD, void* data) -> int {
        if (data) { memset(data, 0, 400); reinterpret_cast<WORD*>(data)[0] = 0x0202; }
        return 0;
    }));
    lib.functions["WSACleanup"] = fn((void*)static_cast<int(*)()>([]() -> int { return 0; }));
    lib.functions["WSAGetLastError"] = fn((void*)static_cast<int(*)()>([]() -> int { return errno; }));
    lib.functions["WSASocketA"] = fn((void*)static_cast<void*(*)(int, int, int, void*, DWORD, DWORD)>([](int af, int type, int proto, void*, DWORD, DWORD) -> void* {
        int sock = socket(af, type, proto);
        if (sock < 0) return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
        int id = g_nextSock++;
        g_sockets[id] = sock;
        return reinterpret_cast<void*>(static_cast<intptr_t>(id));
    }));
    lib.functions["closesocket"] = fn((void*)static_cast<int(*)(void*)>([](void* s) -> int {
        int fd = sockFromHandle((intptr_t)s);
        if (fd >= 0) { close(fd); g_sockets.erase((int)(intptr_t)s); }
        return 0;
    }));
    lib.functions["connect"] = fn((void*)static_cast<int(*)(void*, const void*, int)>([](void* s, const void* addr, int len) -> int {
        int fd = sockFromHandle((intptr_t)s);
        if (fd < 0) return -1;
        return ::connect(fd, (const sockaddr*)addr, (socklen_t)len);
    }));
    lib.functions["send"] = fn((void*)static_cast<int(*)(void*, const char*, int, int)>([](void* s, const char* buf, int len, int flags) -> int {
        int fd = sockFromHandle((intptr_t)s);
        if (fd < 0) return -1;
        return (int)::send(fd, buf, (size_t)len, flags);
    }));
    lib.functions["recv"] = fn((void*)static_cast<int(*)(void*, char*, int, int)>([](void* s, char* buf, int len, int flags) -> int {
        int fd = sockFromHandle((intptr_t)s);
        if (fd < 0) return -1;
        return (int)::recv(fd, buf, (size_t)len, flags);
    }));
    lib.functions["bind"] = fn((void*)static_cast<int(*)(void*, const void*, int)>([](void* s, const void* addr, int len) -> int {
        int fd = sockFromHandle((intptr_t)s);
        if (fd < 0) return -1;
        return ::bind(fd, (const sockaddr*)addr, (socklen_t)len);
    }));
    lib.functions["listen"] = fn((void*)static_cast<int(*)(void*, int)>([](void* s, int backlog) -> int {
        int fd = sockFromHandle((intptr_t)s);
        if (fd < 0) return -1;
        return ::listen(fd, backlog);
    }));
    lib.functions["accept"] = fn((void*)static_cast<void*(*)(void*, void*, int*)>([](void* s, void* addr, int* len) -> void* {
        int fd = sockFromHandle((intptr_t)s);
        if (fd < 0) return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
        int nfd = ::accept(fd, (sockaddr*)addr, (socklen_t*)len);
        if (nfd < 0) return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
        int id = g_nextSock++;
        g_sockets[id] = nfd;
        return reinterpret_cast<void*>(static_cast<intptr_t>(id));
    }));
    lib.functions["shutdown"] = fn((void*)static_cast<int(*)(void*, int)>([](void* s, int how) -> int {
        int fd = sockFromHandle((intptr_t)s);
        if (fd < 0) return -1;
        return ::shutdown(fd, how);
    }));
    lib.functions["ioctlsocket"] = fn((void*)static_cast<int(*)(void*, long, void*)>([](void* s, long cmd, void* argp) -> int {
        int fd = sockFromHandle((intptr_t)s);
        if (fd < 0) return -1;
        if (cmd == 0x8004667E) {
            int flags = fcntl(fd, F_GETFL, 0);
            if (*(reinterpret_cast<uint32_t*>(argp))) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
            else fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
        }
        return 0;
    }));
    lib.functions["getsockname"] = fn((void*)static_cast<int(*)(void*, void*, int*)>([](void* s, void* addr, int* len) -> int {
        int fd = sockFromHandle((intptr_t)s);
        if (fd < 0) return -1;
        return ::getsockname(fd, (sockaddr*)addr, (socklen_t*)len);
    }));
    lib.functions["getpeername"] = fn((void*)static_cast<int(*)(void*, void*, int*)>([](void* s, void* addr, int* len) -> int {
        int fd = sockFromHandle((intptr_t)s);
        if (fd < 0) return -1;
        return ::getpeername(fd, (sockaddr*)addr, (socklen_t*)len);
    }));
    lib.functions["getsockopt"] = fn((void*)static_cast<int(*)(void*, int, int, char*, int*)>([](void* s, int level, int opt, char* val, int* len) -> int {
        int fd = sockFromHandle((intptr_t)s);
        if (fd < 0) return -1;
        return ::getsockopt(fd, level, opt, val, (socklen_t*)len);
    }));
    lib.functions["setsockopt"] = fn((void*)static_cast<int(*)(void*, int, int, const char*, int)>([](void* s, int level, int opt, const char* val, int len) -> int {
        int fd = sockFromHandle((intptr_t)s);
        if (fd < 0) return -1;
        return ::setsockopt(fd, level, opt, val, (socklen_t)len);
    }));
    lib.functions["select"] = fn((void*)static_cast<int(*)(int, void*, void*, void*, const void*)>([](int, void*, void*, void*, const void*) -> int { return 0; }));
    lib.functions["getaddrinfo"] = fn((void*)(int(*)(const char*, const char*, const void*, void**))getaddrinfo);
    lib.functions["freeaddrinfo"] = fn((void*)(void(*)(void*))freeaddrinfo);
    auto htons_fn = [](uint16_t v) -> uint16_t { return __builtin_bswap16(v); };
    auto ntohs_fn = [](uint16_t v) -> uint16_t { return __builtin_bswap16(v); };
    auto htonl_fn = [](uint32_t v) -> uint32_t { return __builtin_bswap32(v); };
    auto ntohl_fn = [](uint32_t v) -> uint32_t { return __builtin_bswap32(v); };
    lib.functions["htons"] = fn((void*)(uint16_t(*)(uint16_t))htons_fn);
    lib.functions["ntohs"] = fn((void*)(uint16_t(*)(uint16_t))ntohs_fn);
    lib.functions["htonl"] = fn((void*)(uint32_t(*)(uint32_t))htonl_fn);
    lib.functions["ntohl"] = fn((void*)(uint32_t(*)(uint32_t))ntohl_fn);
    lib.functions["inet_addr"] = fn((void*)(uint32_t(*)(const char*))inet_addr);
    lib.functions["inet_ntoa"] = fn((void*)(char*(*)(struct in_addr))inet_ntoa);
    lib.functions["WSAIoctl"] = fn((void*)static_cast<int(*)(void*, DWORD, void*, DWORD, void*, DWORD, DWORD*, void*, void*)>(
        [](void*, DWORD, void*, DWORD, void*, DWORD, DWORD*, void*, void*) -> int { return 0; }));
    lib.functions["WSARecv"] = fn((void*)static_cast<int(*)(void*, void*, DWORD, DWORD*, DWORD*, void*, void*)>(
        [](void*, void*, DWORD, DWORD*, DWORD*, void*, void*) -> int { return -1; }));
    lib.functions["WSARecvFrom"] = fn((void*)static_cast<int(*)(void*, void*, DWORD, DWORD*, DWORD*, void*, int*, void*, void*)>(
        [](void*, void*, DWORD, DWORD*, DWORD*, void*, int*, void*, void*) -> int { return -1; }));
    lib.functions["WSASend"] = fn((void*)static_cast<int(*)(void*, void*, DWORD, DWORD*, DWORD, void*, void*)>(
        [](void*, void*, DWORD, DWORD*, DWORD, void*, void*) -> int { return -1; }));
    lib.functions["WSASendTo"] = fn((void*)static_cast<int(*)(void*, void*, DWORD, DWORD*, DWORD, const void*, int, void*, void*)>(
        [](void*, void*, DWORD, DWORD*, DWORD, const void*, int, void*, void*) -> int { return -1; }));

    lib.ordinals[2] = fn((void*)static_cast<int(*)(int, void*, void*, void*, const void*)>([](int, void*, void*, void*, const void*) -> int { return 0; }));
    lib.ordinals[3] = fn((void*)static_cast<int(*)(void*)>([](void*) -> int { return 0; }));
    lib.ordinals[4] = fn((void*)static_cast<int(*)(void*, int, int)>([](void*, int, int) -> int { return 0; }));
    lib.ordinals[6] = fn((void*)static_cast<void*(*)(void*, void*, int*)>([](void*, void*, int*) -> void* { return reinterpret_cast<void*>(static_cast<intptr_t>(-1)); }));
    lib.ordinals[8] = fn((void*)static_cast<int(*)(void*, int, int, char*, int*)>([](void*, int, int, char*, int*) -> int { return 0; }));
    lib.ordinals[9] = fn((void*)static_cast<int(*)(void*, int, int, const char*, int)>([](void*, int, int, const char*, int) -> int { return 0; }));
    lib.ordinals[10] = fn((void*)static_cast<int(*)(void*, void*, int)>([](void*, void*, int) -> int { return 0; }));
    lib.ordinals[14] = fn((void*)static_cast<int(*)(void*)>([](void*) -> int { return 0; }));
    lib.ordinals[15] = fn((void*)static_cast<int(*)(void*, int)>([](void*, int) -> int { return 0; }));
    lib.ordinals[16] = fn((void*)static_cast<int(*)(void*, int, const void*, int)>([](void*, int, const void*, int) -> int { return 0; }));
    lib.ordinals[18] = fn((void*)static_cast<int(*)(void*, const void*, int)>([](void*, const void*, int) -> int { return 0; }));
    lib.ordinals[19] = fn((void*)static_cast<int(*)(void*, int, long, void*)>([](void*, int, long, void*) -> int { return 0; }));
    lib.ordinals[21] = fn((void*)static_cast<int(*)(void*)>([](void*) -> int { return 0; }));
    lib.ordinals[22] = fn((void*)static_cast<int(*)(void*, int)>([](void*, int) -> int { return 0; }));
    lib.ordinals[23] = fn((void*)static_cast<int(*)(void*, void*, int*)>([](void*, void*, int*) -> int { return 0; }));
    lib.ordinals[111] = fn((void*)static_cast<int(*)(void*, int, int, const char*, int)>([](void*, int, int, const char*, int) -> int { return 0; }));
    lib.ordinals[112] = fn((void*)static_cast<int(*)(void*, int, int, char*, int*)>([](void*, int, int, char*, int*) -> int { return 0; }));
    lib.ordinals[115] = fn((void*)static_cast<int(*)(void*, void*, int*)>([](void*, void*, int*) -> int { return 0; }));
    lib.ordinals[116] = fn((void*)static_cast<int(*)(void*, int, int, int, char*, int*)>([](void*, int, int, int, char*, int*) -> int { return 0; }));
    lib.ordinals[151] = fn((void*)static_cast<int(*)(void*, const char*, const char*, const void*, void**)>([](void*, const char*, const char*, const void*, void**) -> int { return 0; }));

    return lib;
}

ShimLibrary createShell32Shim() {
    ShimLibrary lib;
    lib.name = "SHELL32.dll";
    auto fn = [](void* ptr) -> ExportedFunction { return [ptr]() -> void* { return ptr; }; };

    lib.functions["CommandLineToArgvW"] = fn((void*)static_cast<wchar_t**(*)(const wchar_t*, int*)>([](const wchar_t*, int* argc) -> wchar_t** {
        if (argc) *argc = 1;
        auto** argv = (wchar_t**)malloc(sizeof(wchar_t*) * 2);
        argv[0] = (wchar_t*)L"steam.exe";
        argv[1] = nullptr;
        return argv;
    }));
    lib.functions["SHGetFileInfoW"] = fn((void*)static_cast<DWORD(*)(const wchar_t*, DWORD, void*, UINT, UINT)>([](const wchar_t*, DWORD, void*, UINT, UINT) -> DWORD { return 0; }));
    lib.functions["SHGetKnownFolderPath"] = fn((void*)static_cast<LONG(*)(const void*, DWORD, void*, wchar_t**)>([](const void*, DWORD, void*, wchar_t** ppszPath) -> LONG {
        if (ppszPath) { *ppszPath = (wchar_t*)malloc(64); wcscpy(*ppszPath, L"C:\\Users\\user"); }
        return 0;
    }));
    lib.functions["SHGetFolderPathW"] = fn((void*)static_cast<LONG(*)(void*, int, void*, DWORD, wchar_t*)>([](void*, int, void*, DWORD, wchar_t* path) -> LONG {
        if (path) wcscpy(path, L"C:\\Users\\user");
        return 0;
    }));

    lib.ordinals[680] = fn((void*)static_cast<LONG(*)(void*, int, void*, DWORD, wchar_t*)>([](void*, int, void*, DWORD, wchar_t* path) -> LONG {
        if (path) wcscpy(path, L"C:\\Users\\user");
        return 0;
    }));

    return lib;
}

ShimLibrary createOle32Shim() {
    ShimLibrary lib;
    lib.name = "ole32.dll";
    auto fn = [](void* ptr) -> ExportedFunction { return [ptr]() -> void* { return ptr; }; };
    lib.functions["CoTaskMemFree"] = fn((void*)(void(*)(void*))free);
    return lib;
}

ShimLibrary createOleAut32Shim() {
    ShimLibrary lib;
    lib.name = "OLEAUT32.dll";
    auto fn = [](void* ptr) -> ExportedFunction { return [ptr]() -> void* { return ptr; }; };
    lib.ordinals[9] = fn((void*)static_cast<void*(*)(const wchar_t*)>([](const wchar_t* str) -> void* {
        if (!str) return nullptr;
        size_t len = wcslen(str);
        wchar_t* buf = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
        wcscpy(buf, str);
        return buf;
    }));
    return lib;
}

ShimLibrary createCrypt32Shim() {
    ShimLibrary lib;
    lib.name = "CRYPT32.dll";
    auto fn = [](void* ptr) -> ExportedFunction { return [ptr]() -> void* { return ptr; }; };

    lib.functions["CertOpenStore"] = fn((void*)static_cast<void*(*)(const char*, DWORD, void*, DWORD, const void*)>([](const char*, DWORD, void*, DWORD, const void*) -> void* { return reinterpret_cast<void*>(0xC000); }));
    lib.functions["CertCloseStore"] = fn((void*)static_cast<BOOL(*)(void*, DWORD)>([](void*, DWORD) -> BOOL { return 1; }));
    lib.functions["CertCreateCertificateContext"] = fn((void*)static_cast<void*(*)(DWORD, const BYTE*, DWORD)>([](DWORD, const BYTE*, DWORD) -> void* { return reinterpret_cast<void*>(0xC001); }));
    lib.functions["CertFreeCertificateContext"] = fn((void*)static_cast<BOOL(*)(void*)>([](void*) -> BOOL { return 1; }));
    lib.functions["CertGetCertificateChain"] = fn((void*)static_cast<BOOL(*)(void*, void*, void*, void*, const void*, DWORD, void*, void**)>([](void*, void*, void*, void*, const void*, DWORD, void*, void** chain) -> BOOL {
        if (chain) *chain = reinterpret_cast<void*>(0xC002);
        return 1;
    }));
    lib.functions["CertFreeCertificateChain"] = fn((void*)static_cast<void(*)(void*)>([](void*) {}));
    lib.functions["CertAddCertificateContextToStore"] = fn((void*)static_cast<BOOL(*)(void*, void*, DWORD, void**)>([](void*, void*, DWORD, void**) -> BOOL { return 1; }));

    return lib;
}

ShimLibrary createPsapiShim() {
    ShimLibrary lib;
    lib.name = "PSAPI.DLL";
    auto fn = [](void* ptr) -> ExportedFunction { return [ptr]() -> void* { return ptr; }; };

    lib.functions["GetModuleFileNameExW"] = fn((void*)static_cast<DWORD(*)(void*, void*, wchar_t*, DWORD)>([](void*, void*, wchar_t* buf, DWORD sz) -> DWORD {
        if (buf && sz > 0) { buf[0] = 0; } return 0;
    }));
    lib.functions["GetModuleInformation"] = fn((void*)static_cast<BOOL(*)(void*, void*, void*, DWORD)>([](void*, void*, void*, DWORD) -> BOOL { return 1; }));
    lib.functions["GetProcessMemoryInfo"] = fn((void*)static_cast<BOOL(*)(void*, void*, DWORD)>([](void*, void* pmc, DWORD sz) -> BOOL {
        memset(pmc, 0, sz); return 1;
    }));

    return lib;
}

ShimLibrary createVersionShim() {
    ShimLibrary lib;
    lib.name = "VERSION.dll";
    auto fn = [](void* ptr) -> ExportedFunction { return [ptr]() -> void* { return ptr; }; };
    lib.functions["VerQueryValueW"] = fn((void*)static_cast<BOOL(*)(const void*, const wchar_t*, void**, UINT*)>([](const void*, const wchar_t*, void**, UINT*) -> BOOL { return 0; }));
    return lib;
}

ShimLibrary createBcryptShim() {
    ShimLibrary lib;
    lib.name = "bcrypt.dll";
    auto fn = [](void* ptr) -> ExportedFunction { return [ptr]() -> void* { return ptr; }; };
    lib.functions["BCryptGenRandom"] = fn((void*)static_cast<LONG(*)(void*, BYTE*, ULONG, ULONG)>([](void*, BYTE* buf, ULONG len, ULONG) -> LONG {
        for (ULONG i = 0; i < len; i++) buf[i] = (BYTE)(rand() & 0xFF);
        return 0;
    }));
    return lib;
}

ShimLibrary createComCtl32Shim() {
    ShimLibrary lib;
    lib.name = "COMCTL32.dll";
    auto fn = [](void* ptr) -> ExportedFunction { return [ptr]() -> void* { return ptr; }; };
    lib.functions["InitCommonControlsEx"] = fn((void*)static_cast<BOOL(*)(const void*)>([](const void*) -> BOOL { return 1; }));
    return lib;
}

ShimLibrary createWsock32Shim() {
    ShimLibrary lib;
    lib.name = "WSOCK32.dll";
    auto fn = [](void* ptr) -> ExportedFunction { return [ptr]() -> void* { return ptr; }; };
    lib.ordinals[1142] = fn((void*)static_cast<int(*)(WORD, void*)>([](WORD, void* data) -> int {
        if (data) { memset(data, 0, 400); reinterpret_cast<WORD*>(data)[0] = 0x0202; }
        return 0;
    }));
    return lib;
}

}
}
