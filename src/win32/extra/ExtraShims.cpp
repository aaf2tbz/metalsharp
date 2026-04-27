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

static std::unordered_map<int, int> g_sockets;
static int g_nextSock = 100;

static int sockFromHandle(intptr_t h) {
    auto it = g_sockets.find((int)h);
    return it != g_sockets.end() ? it->second : -1;
}

static std::unordered_map<std::string, std::string> s_registry;
static uintptr_t s_nextKey = 0xA000;

static void* MSABI gdi32_CreateCompatibleDC(void*) {
    return reinterpret_cast<void*>(0x9000);
}

static void* MSABI gdi32_CreateDIBSection(void*, const void*, UINT, void** ppv, void*, DWORD) {
    if (ppv) *ppv = calloc(1, 1920 * 1080 * 4);
    return reinterpret_cast<void*>(0x9001);
}

static void* MSABI gdi32_CreateFontW(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, const wchar_t*) {
    return reinterpret_cast<void*>(0x9002);
}

static void* MSABI gdi32_CreateICW(const wchar_t*, const wchar_t*, const wchar_t*, const void*) {
    return reinterpret_cast<void*>(0x9003);
}

static BOOL MSABI gdi32_DeleteDC(void*) { return 1; }

static BOOL MSABI gdi32_DeleteObject(void*) { return 1; }

static void* MSABI gdi32_SelectObject(void*, void* old) { return old; }

static int MSABI gdi32_GetDeviceCaps(void*, int index) {
    switch (index) { case 0: return 1920; case 1: return 1080; case 12: return 96; case 88: return 32; default: return 0; }
}

static void* MSABI gdi32_GetStockObject(int) { return reinterpret_cast<void*>(0x9004); }

static BOOL MSABI gdi32_GetTextExtentPoint32W(void*, const wchar_t*, int, void* sz) {
    auto* s = reinterpret_cast<DWORD*>(sz); s[0] = 8; s[1] = 16; return 1;
}

static DWORD MSABI gdi32_SetBkColor(void*, DWORD c) { return c; }

static int MSABI gdi32_SetBkMode(void*, int m) { return m; }

static DWORD MSABI gdi32_SetTextColor(void*, DWORD c) { return c; }

static BOOL MSABI gdi32_TextOutW(void*, int, int, const wchar_t*, int) { return 1; }

static BOOL MSABI gdi32_SwapBuffers(void*) { return 1; }

static int MSABI gdi32_ChoosePixelFormat(void*, const void*) { return 1; }

static BOOL MSABI gdi32_SetPixelFormat(void*, int, const void*) { return 1; }

static HANDLE MSABI gdi32_AddFontMemResourceEx(const void*, DWORD, void*, DWORD* cnt) {
    if (cnt) *cnt = 1; return reinterpret_cast<void*>(0x9005);
}

static BOOL MSABI gdi32_RemoveFontMemResourceEx(void*) { return 1; }

ShimLibrary createGdi32Shim() {
    ShimLibrary lib;
    lib.name = "GDI32.dll";
    auto fn = [](void* ptr) -> ExportedFunction { return [ptr]() -> void* { return ptr; }; };

    lib.functions["CreateCompatibleDC"] = fn((void*)gdi32_CreateCompatibleDC);
    lib.functions["CreateDIBSection"] = fn((void*)gdi32_CreateDIBSection);
    lib.functions["CreateFontW"] = fn((void*)gdi32_CreateFontW);
    lib.functions["CreateICW"] = fn((void*)gdi32_CreateICW);
    lib.functions["DeleteDC"] = fn((void*)gdi32_DeleteDC);
    lib.functions["DeleteObject"] = fn((void*)gdi32_DeleteObject);
    lib.functions["SelectObject"] = fn((void*)gdi32_SelectObject);
    lib.functions["GetDeviceCaps"] = fn((void*)gdi32_GetDeviceCaps);
    lib.functions["GetStockObject"] = fn((void*)gdi32_GetStockObject);
    lib.functions["GetTextExtentPoint32W"] = fn((void*)gdi32_GetTextExtentPoint32W);
    lib.functions["SetBkColor"] = fn((void*)gdi32_SetBkColor);
    lib.functions["SetBkMode"] = fn((void*)gdi32_SetBkMode);
    lib.functions["SetTextColor"] = fn((void*)gdi32_SetTextColor);
    lib.functions["TextOutW"] = fn((void*)gdi32_TextOutW);
    lib.functions["SwapBuffers"] = fn((void*)gdi32_SwapBuffers);
    lib.functions["ChoosePixelFormat"] = fn((void*)gdi32_ChoosePixelFormat);
    lib.functions["SetPixelFormat"] = fn((void*)gdi32_SetPixelFormat);
    lib.functions["AddFontMemResourceEx"] = fn((void*)gdi32_AddFontMemResourceEx);
    lib.functions["RemoveFontMemResourceEx"] = fn((void*)gdi32_RemoveFontMemResourceEx);

    return lib;
}

static LONG MSABI advapi32_RegOpenKeyA(void*, const char*, void** key) {
    if (key) *key = reinterpret_cast<void*>(s_nextKey++);
    return 0;
}

static LONG MSABI advapi32_RegOpenKeyExA(void*, const char*, DWORD, DWORD, void** key) {
    if (key) *key = reinterpret_cast<void*>(s_nextKey++);
    return 0;
}

static LONG MSABI advapi32_RegCreateKeyExA(void*, const char*, DWORD, char*, DWORD, DWORD, void*, void** key, void*) {
    if (key) *key = reinterpret_cast<void*>(s_nextKey++);
    return 0;
}

static LONG MSABI advapi32_RegCloseKey(void*) { return 0; }

static LONG MSABI advapi32_RegQueryValueExA(void*, const char*, DWORD*, DWORD*, BYTE*, DWORD*) { return 2; }

static LONG MSABI advapi32_RegQueryValueExW(void*, const wchar_t*, DWORD*, DWORD*, BYTE*, DWORD*) { return 2; }

static LONG MSABI advapi32_RegSetValueExA(void*, const char*, DWORD, DWORD, const BYTE*, DWORD) { return 0; }

static LONG MSABI advapi32_RegSetValueExW(void*, const wchar_t*, DWORD, DWORD, const BYTE*, DWORD) { return 0; }

static LONG MSABI advapi32_RegDeleteValueA(void*, const char*) { return 0; }

static BOOL MSABI advapi32_InitializeSecurityDescriptor(void* sd, DWORD) { memset(sd, 0, 32); return 1; }

static BOOL MSABI advapi32_SetSecurityDescriptorDacl(void*, BOOL, void*, BOOL) { return 1; }

static void* MSABI advapi32_RegisterEventSourceW(const wchar_t*, const wchar_t*) { return reinterpret_cast<void*>(0xA100); }

static BOOL MSABI advapi32_DeregisterEventSource(void*) { return 1; }

static BOOL MSABI advapi32_ReportEventW(void*, WORD, WORD, DWORD, void*, WORD, DWORD, const wchar_t**, void*) { return 1; }

ShimLibrary createAdvapi32Shim() {
    ShimLibrary lib;
    lib.name = "ADVAPI32.dll";
    auto fn = [](void* ptr) -> ExportedFunction { return [ptr]() -> void* { return ptr; }; };

    lib.functions["RegOpenKeyA"] = fn((void*)advapi32_RegOpenKeyA);
    lib.functions["RegOpenKeyExA"] = fn((void*)advapi32_RegOpenKeyExA);
    lib.functions["RegCreateKeyExA"] = fn((void*)advapi32_RegCreateKeyExA);
    lib.functions["RegCloseKey"] = fn((void*)advapi32_RegCloseKey);
    lib.functions["RegQueryValueExA"] = fn((void*)advapi32_RegQueryValueExA);
    lib.functions["RegQueryValueExW"] = fn((void*)advapi32_RegQueryValueExW);
    lib.functions["RegSetValueExA"] = fn((void*)advapi32_RegSetValueExA);
    lib.functions["RegSetValueExW"] = fn((void*)advapi32_RegSetValueExW);
    lib.functions["RegDeleteValueA"] = fn((void*)advapi32_RegDeleteValueA);
    lib.functions["InitializeSecurityDescriptor"] = fn((void*)advapi32_InitializeSecurityDescriptor);
    lib.functions["SetSecurityDescriptorDacl"] = fn((void*)advapi32_SetSecurityDescriptorDacl);
    lib.functions["RegisterEventSourceW"] = fn((void*)advapi32_RegisterEventSourceW);
    lib.functions["DeregisterEventSource"] = fn((void*)advapi32_DeregisterEventSource);
    lib.functions["ReportEventW"] = fn((void*)advapi32_ReportEventW);

    return lib;
}

static int MSABI ws2_32_WSAStartup(WORD, void* data) {
    if (data) { memset(data, 0, 400); reinterpret_cast<WORD*>(data)[0] = 0x0202; }
    return 0;
}

static int MSABI ws2_32_WSACleanup() { return 0; }

static int MSABI ws2_32_WSAGetLastError() { return errno; }

static void* MSABI ws2_32_WSASocketA(int af, int type, int proto, void*, DWORD, DWORD) {
    int sock = socket(af, type, proto);
    if (sock < 0) return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    int id = g_nextSock++;
    g_sockets[id] = sock;
    return reinterpret_cast<void*>(static_cast<intptr_t>(id));
}

static int MSABI ws2_32_closesocket(void* s) {
    int fd = sockFromHandle((intptr_t)s);
    if (fd >= 0) { close(fd); g_sockets.erase((int)(intptr_t)s); }
    return 0;
}

static int MSABI ws2_32_connect(void* s, const void* addr, int len) {
    int fd = sockFromHandle((intptr_t)s);
    if (fd < 0) return -1;
    return ::connect(fd, (const sockaddr*)addr, (socklen_t)len);
}

static int MSABI ws2_32_send(void* s, const char* buf, int len, int flags) {
    int fd = sockFromHandle((intptr_t)s);
    if (fd < 0) return -1;
    return (int)::send(fd, buf, (size_t)len, flags);
}

static int MSABI ws2_32_recv(void* s, char* buf, int len, int flags) {
    int fd = sockFromHandle((intptr_t)s);
    if (fd < 0) return -1;
    return (int)::recv(fd, buf, (size_t)len, flags);
}

static int MSABI ws2_32_bind(void* s, const void* addr, int len) {
    int fd = sockFromHandle((intptr_t)s);
    if (fd < 0) return -1;
    return ::bind(fd, (const sockaddr*)addr, (socklen_t)len);
}

static int MSABI ws2_32_listen(void* s, int backlog) {
    int fd = sockFromHandle((intptr_t)s);
    if (fd < 0) return -1;
    return ::listen(fd, backlog);
}

static void* MSABI ws2_32_accept(void* s, void* addr, int* len) {
    int fd = sockFromHandle((intptr_t)s);
    if (fd < 0) return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    int nfd = ::accept(fd, (sockaddr*)addr, (socklen_t*)len);
    if (nfd < 0) return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    int id = g_nextSock++;
    g_sockets[id] = nfd;
    return reinterpret_cast<void*>(static_cast<intptr_t>(id));
}

static int MSABI ws2_32_shutdown(void* s, int how) {
    int fd = sockFromHandle((intptr_t)s);
    if (fd < 0) return -1;
    return ::shutdown(fd, how);
}

static int MSABI ws2_32_ioctlsocket(void* s, long cmd, void* argp) {
    int fd = sockFromHandle((intptr_t)s);
    if (fd < 0) return -1;
    if (cmd == 0x8004667E) {
        int flags = fcntl(fd, F_GETFL, 0);
        if (*(reinterpret_cast<uint32_t*>(argp))) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        else fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    }
    return 0;
}

static int MSABI ws2_32_getsockname(void* s, void* addr, int* len) {
    int fd = sockFromHandle((intptr_t)s);
    if (fd < 0) return -1;
    return ::getsockname(fd, (sockaddr*)addr, (socklen_t*)len);
}

static int MSABI ws2_32_getpeername(void* s, void* addr, int* len) {
    int fd = sockFromHandle((intptr_t)s);
    if (fd < 0) return -1;
    return ::getpeername(fd, (sockaddr*)addr, (socklen_t*)len);
}

static int MSABI ws2_32_getsockopt(void* s, int level, int opt, char* val, int* len) {
    int fd = sockFromHandle((intptr_t)s);
    if (fd < 0) return -1;
    return ::getsockopt(fd, level, opt, val, (socklen_t*)len);
}

static int MSABI ws2_32_setsockopt(void* s, int level, int opt, const char* val, int len) {
    int fd = sockFromHandle((intptr_t)s);
    if (fd < 0) return -1;
    return ::setsockopt(fd, level, opt, val, (socklen_t)len);
}

static int MSABI ws2_32_select(int, void*, void*, void*, const void*) { return 0; }

static int MSABI ws2_32_getaddrinfo(const char* node, const char* service, const void* hints, void** res) {
    return getaddrinfo(node, service, (const addrinfo*)hints, (addrinfo**)res);
}

static void MSABI ws2_32_freeaddrinfo(void* p) { freeaddrinfo((addrinfo*)p); }

static uint16_t MSABI ws2_32_htons(uint16_t v) { return __builtin_bswap16(v); }

static uint16_t MSABI ws2_32_ntohs(uint16_t v) { return __builtin_bswap16(v); }

static uint32_t MSABI ws2_32_htonl(uint32_t v) { return __builtin_bswap32(v); }

static uint32_t MSABI ws2_32_ntohl(uint32_t v) { return __builtin_bswap32(v); }

static uint32_t MSABI ws2_32_inet_addr(const char* cp) { return inet_addr(cp); }

static char* MSABI ws2_32_inet_ntoa(struct in_addr in) { return inet_ntoa(in); }

static int MSABI ws2_32_WSAIoctl(void*, DWORD, void*, DWORD, void*, DWORD, DWORD*, void*, void*) { return 0; }

static int MSABI ws2_32_WSARecv(void*, void*, DWORD, DWORD*, DWORD*, void*, void*) { return -1; }

static int MSABI ws2_32_WSARecvFrom(void*, void*, DWORD, DWORD*, DWORD*, void*, int*, void*, void*) { return -1; }

static int MSABI ws2_32_WSASend(void*, void*, DWORD, DWORD*, DWORD, void*, void*) { return -1; }

static int MSABI ws2_32_WSASendTo(void*, void*, DWORD, DWORD*, DWORD, const void*, int, void*, void*) { return -1; }

static int MSABI ws2_32_ord3(void*) { return 0; }

static int MSABI ws2_32_ord4(void*, int, int) { return 0; }

static void* MSABI ws2_32_ord6(void*, void*, int*) { return reinterpret_cast<void*>(static_cast<intptr_t>(-1)); }

static int MSABI ws2_32_ord8(void*, int, int, char*, int*) { return 0; }

static int MSABI ws2_32_ord9(void*, int, int, const char*, int) { return 0; }

static int MSABI ws2_32_ord10(void*, void*, int) { return 0; }

static int MSABI ws2_32_ord15(void*, int) { return 0; }

static int MSABI ws2_32_ord16(void*, int, const void*, int) { return 0; }

static int MSABI ws2_32_ord18(void*, const void*, int) { return 0; }

static int MSABI ws2_32_ord19(void*, int, long, void*) { return 0; }

static int MSABI ws2_32_ord23(void*, void*, int*) { return 0; }

static int MSABI ws2_32_ord116(void*, int, int, int, char*, int*) { return 0; }

static int MSABI ws2_32_ord151(void*, const char*, const char*, const void*, void**) { return 0; }

ShimLibrary createWs2_32Shim() {
    ShimLibrary lib;
    lib.name = "WS2_32.dll";
    auto fn = [](void* ptr) -> ExportedFunction { return [ptr]() -> void* { return ptr; }; };

    lib.functions["WSAStartup"] = fn((void*)ws2_32_WSAStartup);
    lib.functions["WSACleanup"] = fn((void*)ws2_32_WSACleanup);
    lib.functions["WSAGetLastError"] = fn((void*)ws2_32_WSAGetLastError);
    lib.functions["WSASocketA"] = fn((void*)ws2_32_WSASocketA);
    lib.functions["closesocket"] = fn((void*)ws2_32_closesocket);
    lib.functions["connect"] = fn((void*)ws2_32_connect);
    lib.functions["send"] = fn((void*)ws2_32_send);
    lib.functions["recv"] = fn((void*)ws2_32_recv);
    lib.functions["bind"] = fn((void*)ws2_32_bind);
    lib.functions["listen"] = fn((void*)ws2_32_listen);
    lib.functions["accept"] = fn((void*)ws2_32_accept);
    lib.functions["shutdown"] = fn((void*)ws2_32_shutdown);
    lib.functions["ioctlsocket"] = fn((void*)ws2_32_ioctlsocket);
    lib.functions["getsockname"] = fn((void*)ws2_32_getsockname);
    lib.functions["getpeername"] = fn((void*)ws2_32_getpeername);
    lib.functions["getsockopt"] = fn((void*)ws2_32_getsockopt);
    lib.functions["setsockopt"] = fn((void*)ws2_32_setsockopt);
    lib.functions["select"] = fn((void*)ws2_32_select);
    lib.functions["getaddrinfo"] = fn((void*)ws2_32_getaddrinfo);
    lib.functions["freeaddrinfo"] = fn((void*)ws2_32_freeaddrinfo);
    lib.functions["htons"] = fn((void*)ws2_32_htons);
    lib.functions["ntohs"] = fn((void*)ws2_32_ntohs);
    lib.functions["htonl"] = fn((void*)ws2_32_htonl);
    lib.functions["ntohl"] = fn((void*)ws2_32_ntohl);
    lib.functions["inet_addr"] = fn((void*)ws2_32_inet_addr);
    lib.functions["inet_ntoa"] = fn((void*)ws2_32_inet_ntoa);
    lib.functions["WSAIoctl"] = fn((void*)ws2_32_WSAIoctl);
    lib.functions["WSARecv"] = fn((void*)ws2_32_WSARecv);
    lib.functions["WSARecvFrom"] = fn((void*)ws2_32_WSARecvFrom);
    lib.functions["WSASend"] = fn((void*)ws2_32_WSASend);
    lib.functions["WSASendTo"] = fn((void*)ws2_32_WSASendTo);

    lib.ordinals[2] = fn((void*)ws2_32_select);
    lib.ordinals[3] = fn((void*)ws2_32_ord3);
    lib.ordinals[4] = fn((void*)ws2_32_ord4);
    lib.ordinals[6] = fn((void*)ws2_32_ord6);
    lib.ordinals[8] = fn((void*)ws2_32_ord8);
    lib.ordinals[9] = fn((void*)ws2_32_ord9);
    lib.ordinals[10] = fn((void*)ws2_32_ord10);
    lib.ordinals[14] = fn((void*)ws2_32_ord3);
    lib.ordinals[15] = fn((void*)ws2_32_ord15);
    lib.ordinals[16] = fn((void*)ws2_32_ord16);
    lib.ordinals[18] = fn((void*)ws2_32_ord18);
    lib.ordinals[19] = fn((void*)ws2_32_ord19);
    lib.ordinals[21] = fn((void*)ws2_32_ord3);
    lib.ordinals[22] = fn((void*)ws2_32_ord15);
    lib.ordinals[23] = fn((void*)ws2_32_ord23);
    lib.ordinals[111] = fn((void*)ws2_32_ord9);
    lib.ordinals[112] = fn((void*)ws2_32_ord8);
    lib.ordinals[115] = fn((void*)ws2_32_ord23);
    lib.ordinals[116] = fn((void*)ws2_32_ord116);
    lib.ordinals[151] = fn((void*)ws2_32_ord151);

    return lib;
}

static wchar_t** MSABI shell32_CommandLineToArgvW(const wchar_t*, int* argc) {
    if (argc) *argc = 1;
    auto** argv = (wchar_t**)malloc(sizeof(wchar_t*) * 2);
    argv[0] = (wchar_t*)L"steam.exe";
    argv[1] = nullptr;
    return argv;
}

static DWORD MSABI shell32_SHGetFileInfoW(const wchar_t*, DWORD, void*, UINT, UINT) { return 0; }

static LONG MSABI shell32_SHGetKnownFolderPath(const void*, DWORD, void*, wchar_t** ppszPath) {
    if (ppszPath) { *ppszPath = (wchar_t*)malloc(64); wcscpy(*ppszPath, L"C:\\Users\\user"); }
    return 0;
}

static LONG MSABI shell32_SHGetFolderPathW(void*, int, void*, DWORD, wchar_t* path) {
    if (path) wcscpy(path, L"C:\\Users\\user");
    return 0;
}

ShimLibrary createShell32Shim() {
    ShimLibrary lib;
    lib.name = "SHELL32.dll";
    auto fn = [](void* ptr) -> ExportedFunction { return [ptr]() -> void* { return ptr; }; };

    lib.functions["CommandLineToArgvW"] = fn((void*)shell32_CommandLineToArgvW);
    lib.functions["SHGetFileInfoW"] = fn((void*)shell32_SHGetFileInfoW);
    lib.functions["SHGetKnownFolderPath"] = fn((void*)shell32_SHGetKnownFolderPath);
    lib.functions["SHGetFolderPathW"] = fn((void*)shell32_SHGetFolderPathW);

    lib.ordinals[680] = fn((void*)shell32_SHGetFolderPathW);

    return lib;
}

static void MSABI ole32_CoTaskMemFree(void* p) { free(p); }

ShimLibrary createOle32Shim() {
    ShimLibrary lib;
    lib.name = "ole32.dll";
    auto fn = [](void* ptr) -> ExportedFunction { return [ptr]() -> void* { return ptr; }; };
    lib.functions["CoTaskMemFree"] = fn((void*)ole32_CoTaskMemFree);
    return lib;
}

static void* MSABI oleaut32_SysAllocString(const wchar_t* str) {
    if (!str) return nullptr;
    size_t len = wcslen(str);
    wchar_t* buf = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
    wcscpy(buf, str);
    return buf;
}

ShimLibrary createOleAut32Shim() {
    ShimLibrary lib;
    lib.name = "OLEAUT32.dll";
    auto fn = [](void* ptr) -> ExportedFunction { return [ptr]() -> void* { return ptr; }; };
    lib.ordinals[9] = fn((void*)oleaut32_SysAllocString);
    return lib;
}

static void* MSABI crypt32_CertOpenStore(const char*, DWORD, void*, DWORD, const void*) { return reinterpret_cast<void*>(0xC000); }

static BOOL MSABI crypt32_CertCloseStore(void*, DWORD) { return 1; }

static void* MSABI crypt32_CertCreateCertificateContext(DWORD, const BYTE*, DWORD) { return reinterpret_cast<void*>(0xC001); }

static BOOL MSABI crypt32_CertFreeCertificateContext(void*) { return 1; }

static BOOL MSABI crypt32_CertGetCertificateChain(void*, void*, void*, void*, const void*, DWORD, void*, void** chain) {
    if (chain) *chain = reinterpret_cast<void*>(0xC002);
    return 1;
}

static void MSABI crypt32_CertFreeCertificateChain(void*) {}

static BOOL MSABI crypt32_CertAddCertificateContextToStore(void*, void*, DWORD, void**) { return 1; }

ShimLibrary createCrypt32Shim() {
    ShimLibrary lib;
    lib.name = "CRYPT32.dll";
    auto fn = [](void* ptr) -> ExportedFunction { return [ptr]() -> void* { return ptr; }; };

    lib.functions["CertOpenStore"] = fn((void*)crypt32_CertOpenStore);
    lib.functions["CertCloseStore"] = fn((void*)crypt32_CertCloseStore);
    lib.functions["CertCreateCertificateContext"] = fn((void*)crypt32_CertCreateCertificateContext);
    lib.functions["CertFreeCertificateContext"] = fn((void*)crypt32_CertFreeCertificateContext);
    lib.functions["CertGetCertificateChain"] = fn((void*)crypt32_CertGetCertificateChain);
    lib.functions["CertFreeCertificateChain"] = fn((void*)crypt32_CertFreeCertificateChain);
    lib.functions["CertAddCertificateContextToStore"] = fn((void*)crypt32_CertAddCertificateContextToStore);

    return lib;
}

static DWORD MSABI psapi_GetModuleFileNameExW(void*, void*, wchar_t* buf, DWORD sz) {
    if (buf && sz > 0) { buf[0] = 0; } return 0;
}

static BOOL MSABI psapi_GetModuleInformation(void*, void*, void*, DWORD) { return 1; }

static BOOL MSABI psapi_GetProcessMemoryInfo(void*, void* pmc, DWORD sz) {
    memset(pmc, 0, sz); return 1;
}

ShimLibrary createPsapiShim() {
    ShimLibrary lib;
    lib.name = "PSAPI.DLL";
    auto fn = [](void* ptr) -> ExportedFunction { return [ptr]() -> void* { return ptr; }; };

    lib.functions["GetModuleFileNameExW"] = fn((void*)psapi_GetModuleFileNameExW);
    lib.functions["GetModuleInformation"] = fn((void*)psapi_GetModuleInformation);
    lib.functions["GetProcessMemoryInfo"] = fn((void*)psapi_GetProcessMemoryInfo);

    return lib;
}

static BOOL MSABI version_VerQueryValueW(const void*, const wchar_t*, void**, UINT*) { return 0; }

ShimLibrary createVersionShim() {
    ShimLibrary lib;
    lib.name = "VERSION.dll";
    auto fn = [](void* ptr) -> ExportedFunction { return [ptr]() -> void* { return ptr; }; };
    lib.functions["VerQueryValueW"] = fn((void*)version_VerQueryValueW);
    return lib;
}

static LONG MSABI bcrypt_BCryptGenRandom(void*, BYTE* buf, ULONG len, ULONG) {
    for (ULONG i = 0; i < len; i++) buf[i] = (BYTE)(rand() & 0xFF);
    return 0;
}

ShimLibrary createBcryptShim() {
    ShimLibrary lib;
    lib.name = "bcrypt.dll";
    auto fn = [](void* ptr) -> ExportedFunction { return [ptr]() -> void* { return ptr; }; };
    lib.functions["BCryptGenRandom"] = fn((void*)bcrypt_BCryptGenRandom);
    return lib;
}

static BOOL MSABI comctl32_InitCommonControlsEx(const void*) { return 1; }

ShimLibrary createComCtl32Shim() {
    ShimLibrary lib;
    lib.name = "COMCTL32.dll";
    auto fn = [](void* ptr) -> ExportedFunction { return [ptr]() -> void* { return ptr; }; };
    lib.functions["InitCommonControlsEx"] = fn((void*)comctl32_InitCommonControlsEx);
    return lib;
}

static int MSABI wsock32_ord1142(WORD, void* data) {
    if (data) { memset(data, 0, 400); reinterpret_cast<WORD*>(data)[0] = 0x0202; }
    return 0;
}

ShimLibrary createWsock32Shim() {
    ShimLibrary lib;
    lib.name = "WSOCK32.dll";
    auto fn = [](void* ptr) -> ExportedFunction { return [ptr]() -> void* { return ptr; }; };
    lib.ordinals[1142] = fn((void*)wsock32_ord1142);
    return lib;
}

}
}
