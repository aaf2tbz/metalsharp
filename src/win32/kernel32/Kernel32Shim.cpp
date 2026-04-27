#include <metalsharp/Kernel32Shim.h>
#include <metalsharp/Win32Types.h>
#include <metalsharp/Logger.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>

namespace metalsharp {
namespace win32 {

bool Kernel32Shim::s_initialized = false;
std::unordered_map<uintptr_t, size_t> Kernel32Shim::s_allocations;
std::unordered_map<uintptr_t, std::string> Kernel32Shim::s_fileHandles;
uintptr_t Kernel32Shim::s_nextHandle = 0x00010000;

static thread_local DWORD t_lastError = 0;

static DWORD shim_GetLastError() {
    return t_lastError;
}

static void shim_SetLastError(DWORD dwErrCode) {
    t_lastError = dwErrCode;
}

static void* shim_VirtualAlloc(void* lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect) {
    int prot = PROT_READ | PROT_WRITE;
    if (flProtect & PAGE_EXECUTE || flProtect & PAGE_EXECUTE_READ || flProtect & PAGE_EXECUTE_READWRITE) {
        prot = PROT_READ | PROT_WRITE | PROT_EXEC;
    }

    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    void* ptr = mmap(lpAddress, dwSize, prot, flags, -1, 0);
    if (ptr == MAP_FAILED) {
        t_lastError = errno;
        return nullptr;
    }

    Kernel32Shim::s_allocations[reinterpret_cast<uintptr_t>(ptr)] = dwSize;
    return ptr;
}

static BOOL shim_VirtualFree(void* lpAddress, SIZE_T dwSize, DWORD dwFreeType) {
    auto it = Kernel32Shim::s_allocations.find(reinterpret_cast<uintptr_t>(lpAddress));
    if (it == Kernel32Shim::s_allocations.end()) return 0;

    size_t size = it->second;
    Kernel32Shim::s_allocations.erase(it);

    if (dwFreeType & MEM_RELEASE) {
        munmap(lpAddress, size);
    }
    return 1;
}

static void* shim_GetProcessHeap() {
    return reinterpret_cast<void*>(0x1);
}

static void* shim_HeapAlloc(void* hHeap, DWORD dwFlags, SIZE_T dwBytes) {
    if (dwFlags & 0x8) {
        return calloc(1, dwBytes);
    }
    return malloc(dwBytes);
}

static BOOL shim_HeapFree(void* hHeap, DWORD dwFlags, void* lpMem) {
    free(lpMem);
    return 1;
}

static void* shim_LocalAlloc(UINT uFlags, SIZE_T uBytes) {
    if (uFlags & 0x40) return calloc(1, uBytes);
    return malloc(uBytes);
}

static void shim_LocalFree(void* hMem) {
    free(hMem);
}

static void* shim_GlobalAlloc(UINT uFlags, SIZE_T uBytes) {
    if (uFlags & 0x40) return calloc(1, uBytes);
    return malloc(uBytes);
}

static void shim_GlobalFree(void* hMem) {
    free(hMem);
}

static HANDLE shim_CreateFileA(const char* lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
    void* lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
    (void)lpFileName; (void)dwDesiredAccess; (void)dwShareMode;
    (void)lpSecurityAttributes; (void)dwCreationDisposition;
    (void)dwFlagsAndAttributes; (void)hTemplateFile;
    return INVALID_HANDLE_VALUE;
}

static BOOL shim_ReadFile(HANDLE hFile, void* lpBuffer, DWORD nNumberOfBytesToRead,
    DWORD* lpNumberOfBytesRead, void* lpOverlapped) {
    (void)hFile; (void)lpBuffer; (void)nNumberOfBytesToRead;
    (void)lpNumberOfBytesRead; (void)lpOverlapped;
    return 0;
}

static BOOL shim_WriteFile(HANDLE hFile, const void* lpBuffer, DWORD nNumberOfBytesToWrite,
    DWORD* lpNumberOfBytesWritten, void* lpOverlapped) {
    (void)hFile; (void)lpBuffer; (void)nNumberOfBytesToWrite;
    (void)lpNumberOfBytesWritten; (void)lpOverlapped;
    return 0;
}

static BOOL shim_CloseHandle(HANDLE hObject) {
    (void)hObject;
    return 1;
}

static DWORD shim_GetFileSize(HANDLE hFile, DWORD* lpFileSizeHigh) {
    (void)hFile; (void)lpFileSizeHigh;
    return 0;
}

static DWORD shim_GetCurrentDirectoryA(DWORD nBufferLength, char* lpBuffer) {
    if (nBufferLength == 0) return 0;
    if (getcwd(lpBuffer, nBufferLength)) {
        return static_cast<DWORD>(strlen(lpBuffer));
    }
    return 0;
}

static DWORD shim_SetCurrentDirectoryA(const char* lpPathName) {
    return chdir(lpPathName) == 0 ? 1 : 0;
}

static DWORD shim_GetModuleFileNameA(HMODULE hModule, char* lpFilename, DWORD nSize) {
    (void)hModule;
    if (nSize > 0) {
        const char* exe = "/metalsharp/game.exe";
        size_t len = strlen(exe);
        if (len >= nSize) len = nSize - 1;
        memcpy(lpFilename, exe, len);
        lpFilename[len] = 0;
        return static_cast<DWORD>(len);
    }
    return 0;
}

static HMODULE shim_GetModuleHandleA(const char* lpModuleName) {
    (void)lpModuleName;
    return reinterpret_cast<HMODULE>(0x2);
}

static FARPROC shim_GetProcAddress(HMODULE hModule, const char* lpProcName) {
    (void)hModule; (void)lpProcName;
    return nullptr;
}

static DWORD shim_GetCurrentProcessId() {
    return static_cast<DWORD>(getpid());
}

static HANDLE shim_GetCurrentProcess() {
    return reinterpret_cast<HANDLE>(static_cast<intptr_t>(-1));
}

static void shim_GetSystemInfo(SYSTEM_INFO* lpSystemInfo) {
    memset(lpSystemInfo, 0, sizeof(SYSTEM_INFO));
    lpSystemInfo->wProcessorArchitecture = 9; // PROCESSOR_ARCHITECTURE_AMD64
    lpSystemInfo->dwPageSize = 4096;
    lpSystemInfo->dwNumberOfProcessors = static_cast<DWORD>(sysconf(_SC_NPROCESSORS_ONLN));
    lpSystemInfo->dwAllocationGranularity = 65536;
}

static void shim_InitializeCriticalSection(CRITICAL_SECTION* lpCriticalSection) {
    auto* mtx = new pthread_mutex_t();
    pthread_mutex_init(mtx, nullptr);
    lpCriticalSection->DebugInfo = mtx;
}

static void shim_EnterCriticalSection(CRITICAL_SECTION* lpCriticalSection) {
    if (lpCriticalSection->DebugInfo) {
        pthread_mutex_lock(static_cast<pthread_mutex_t*>(lpCriticalSection->DebugInfo));
    }
}

static void shim_LeaveCriticalSection(CRITICAL_SECTION* lpCriticalSection) {
    if (lpCriticalSection->DebugInfo) {
        pthread_mutex_unlock(static_cast<pthread_mutex_t*>(lpCriticalSection->DebugInfo));
    }
}

static void shim_DeleteCriticalSection(CRITICAL_SECTION* lpCriticalSection) {
    if (lpCriticalSection->DebugInfo) {
        auto* mtx = static_cast<pthread_mutex_t*>(lpCriticalSection->DebugInfo);
        pthread_mutex_destroy(mtx);
        delete mtx;
        lpCriticalSection->DebugInfo = nullptr;
    }
}

static HANDLE shim_CreateThread(void* lpThreadAttributes, SIZE_T dwStackSize,
    void* lpStartAddress, void* lpParameter, DWORD dwCreationFlags, DWORD* lpThreadId) {
    (void)lpThreadAttributes; (void)dwStackSize; (void)dwCreationFlags;

    pthread_t thread;
    int result = pthread_create(&thread, nullptr,
        reinterpret_cast<void*(*)(void*)>(lpStartAddress), lpParameter);
    if (result != 0) {
        t_lastError = result;
        return nullptr;
    }

    if (lpThreadId) *lpThreadId = static_cast<DWORD>(reinterpret_cast<uintptr_t>(thread));
    return reinterpret_cast<HANDLE>(thread);
}

static DWORD shim_WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds) {
    (void)hHandle; (void)dwMilliseconds;
    return WAIT_OBJECT_0;
}

static void shim_Sleep(DWORD dwMilliseconds) {
    usleep(dwMilliseconds * 1000);
}

static DWORD shim_GetTickCount() {
    static auto startTime = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime);
    return static_cast<DWORD>(ms.count());
}

static BOOL shim_QueryPerformanceCounter(int64_t* lpPerformanceCount) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    *lpPerformanceCount = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    return 1;
}

static BOOL shim_QueryPerformanceFrequency(int64_t* lpFrequency) {
    *lpFrequency = 1000000000LL;
    return 1;
}

static void shim_OutputDebugStringA(const char* lpOutputString) {
    MS_INFO("Game debug: %s", lpOutputString);
}

static BOOL shim_IsProcessorFeaturePresent(DWORD ProcessorFeature) {
    (void)ProcessorFeature;
    return 1;
}

static int shim_MultiByteToWideChar(UINT CodePage, DWORD dwFlags,
    const char* lpMultiByteStr, int cbMultiByte, wchar_t* lpWideCharStr, int cchWideChar) {
    (void)CodePage; (void)dwFlags;
    if (!lpMultiByteStr) return 0;

    int len = cbMultiByte > 0 ? cbMultiByte : static_cast<int>(strlen(lpMultiByteStr));
    if (cchWideChar == 0) return len;

    int copyLen = len < cchWideChar ? len : cchWideChar;
    for (int i = 0; i < copyLen; i++) {
        lpWideCharStr[i] = static_cast<wchar_t>(static_cast<unsigned char>(lpMultiByteStr[i]));
    }
    return copyLen;
}

static int shim_WideCharToMultiByte(UINT CodePage, DWORD dwFlags,
    const wchar_t* lpWideCharStr, int cchWideChar, char* lpMultiByteStr,
    int cbMultiByte, const char* lpDefaultChar, BOOL* lpUsedDefaultChar) {
    (void)CodePage; (void)dwFlags; (void)lpDefaultChar; (void)lpUsedDefaultChar;
    if (!lpWideCharStr) return 0;

    int len = cchWideChar > 0 ? cchWideChar : static_cast<int>(wcslen(lpWideCharStr));
    if (cbMultiByte == 0) return len;

    int copyLen = len < cbMultiByte ? len : cbMultiByte;
    for (int i = 0; i < copyLen; i++) {
        lpMultiByteStr[i] = static_cast<char>(lpWideCharStr[i] & 0xFF);
    }
    return copyLen;
}

static HANDLE shim_GetStdHandle(DWORD nStdHandle) {
    (void)nStdHandle;
    return INVALID_HANDLE_VALUE;
}

static int shim_lstrcmpA(const char* str1, const char* str2) {
    return strcmp(str1, str2);
}

static int shim_lstrcmpiA(const char* str1, const char* str2) {
    return strcasecmp(str1, str2);
}

ShimLibrary Kernel32Shim::create() {
    if (!s_initialized) s_initialized = true;

    ShimLibrary lib;
    lib.name = "kernel32.dll";

    auto fn = [](void* ptr) -> ExportedFunction {
        return [ptr]() -> void* { return ptr; };
    };

    lib.functions["GetLastError"] = fn((void*)shim_GetLastError);
    lib.functions["SetLastError"] = fn((void*)shim_SetLastError);
    lib.functions["VirtualAlloc"] = fn((void*)shim_VirtualAlloc);
    lib.functions["VirtualFree"] = fn((void*)shim_VirtualFree);
    lib.functions["VirtualProtect"] = fn((void*)nullptr);
    lib.functions["GetProcessHeap"] = fn((void*)shim_GetProcessHeap);
    lib.functions["HeapAlloc"] = fn((void*)shim_HeapAlloc);
    lib.functions["HeapFree"] = fn((void*)shim_HeapFree);
    lib.functions["HeapSize"] = fn((void*)nullptr);
    lib.functions["HeapCreate"] = fn((void*)nullptr);
    lib.functions["HeapDestroy"] = fn((void*)nullptr);
    lib.functions["LocalAlloc"] = fn((void*)shim_LocalAlloc);
    lib.functions["LocalFree"] = fn((void*)shim_LocalFree);
    lib.functions["GlobalAlloc"] = fn((void*)shim_GlobalAlloc);
    lib.functions["GlobalFree"] = fn((void*)shim_GlobalFree);
    lib.functions["CreateFileA"] = fn((void*)shim_CreateFileA);
    lib.functions["CreateFileW"] = fn((void*)shim_CreateFileA);
    lib.functions["ReadFile"] = fn((void*)shim_ReadFile);
    lib.functions["WriteFile"] = fn((void*)shim_WriteFile);
    lib.functions["CloseHandle"] = fn((void*)shim_CloseHandle);
    lib.functions["GetFileSize"] = fn((void*)shim_GetFileSize);
    lib.functions["GetFileSizeEx"] = fn((void*)nullptr);
    lib.functions["GetCurrentDirectoryA"] = fn((void*)shim_GetCurrentDirectoryA);
    lib.functions["GetCurrentDirectoryW"] = fn((void*)shim_GetCurrentDirectoryA);
    lib.functions["SetCurrentDirectoryA"] = fn((void*)shim_SetCurrentDirectoryA);
    lib.functions["GetModuleFileNameA"] = fn((void*)shim_GetModuleFileNameA);
    lib.functions["GetModuleFileNameW"] = fn((void*)shim_GetModuleFileNameA);
    lib.functions["GetModuleHandleA"] = fn((void*)shim_GetModuleHandleA);
    lib.functions["GetModuleHandleW"] = fn((void*)shim_GetModuleHandleA);
    lib.functions["GetProcAddress"] = fn((void*)shim_GetProcAddress);
    lib.functions["GetCurrentProcessId"] = fn((void*)shim_GetCurrentProcessId);
    lib.functions["GetCurrentProcess"] = fn((void*)shim_GetCurrentProcess);
    lib.functions["GetSystemInfo"] = fn((void*)shim_GetSystemInfo);
    lib.functions["InitializeCriticalSection"] = fn((void*)shim_InitializeCriticalSection);
    lib.functions["EnterCriticalSection"] = fn((void*)shim_EnterCriticalSection);
    lib.functions["LeaveCriticalSection"] = fn((void*)shim_LeaveCriticalSection);
    lib.functions["DeleteCriticalSection"] = fn((void*)shim_DeleteCriticalSection);
    lib.functions["CreateThread"] = fn((void*)shim_CreateThread);
    lib.functions["WaitForSingleObject"] = fn((void*)shim_WaitForSingleObject);
    lib.functions["Sleep"] = fn((void*)shim_Sleep);
    lib.functions["SleepEx"] = fn((void*)shim_Sleep);
    lib.functions["GetTickCount"] = fn((void*)shim_GetTickCount);
    lib.functions["GetTickCount64"] = fn((void*)shim_GetTickCount);
    lib.functions["QueryPerformanceCounter"] = fn((void*)shim_QueryPerformanceCounter);
    lib.functions["QueryPerformanceFrequency"] = fn((void*)shim_QueryPerformanceFrequency);
    lib.functions["OutputDebugStringA"] = fn((void*)shim_OutputDebugStringA);
    lib.functions["IsProcessorFeaturePresent"] = fn((void*)shim_IsProcessorFeaturePresent);
    lib.functions["MultiByteToWideChar"] = fn((void*)shim_MultiByteToWideChar);
    lib.functions["WideCharToMultiByte"] = fn((void*)shim_WideCharToMultiByte);
    lib.functions["GetStdHandle"] = fn((void*)shim_GetStdHandle);
    lib.functions["lstrcmpA"] = fn((void*)shim_lstrcmpA);
    lib.functions["lstrcmpiA"] = fn((void*)shim_lstrcmpiA);
    lib.functions["lstrcpyA"] = fn((void*)strcpy);
    lib.functions["lstrlenA"] = fn((void*)strlen);
    lib.functions["LoadLibraryA"] = fn((void*)shim_GetModuleHandleA);
    lib.functions["LoadLibraryW"] = fn((void*)shim_GetModuleHandleA);
    lib.functions["FreeLibrary"] = fn((void*)nullptr);
    lib.functions["GetCommandLineA"] = fn((void*)nullptr);
    lib.functions["GetCommandLineW"] = fn((void*)nullptr);
    lib.functions["GetEnvironmentVariableA"] = fn((void*)nullptr);
    lib.functions["ExpandEnvironmentStringsA"] = fn((void*)nullptr);
    lib.functions["GetCurrentThreadId"] = fn((void*)(DWORD(*)())[]()->DWORD{ return (DWORD)pthread_mach_thread_np(pthread_self()); });
    lib.functions["TlsAlloc"] = fn((void*)nullptr);
    lib.functions["TlsFree"] = fn((void*)nullptr);
    lib.functions["TlsGetValue"] = fn((void*)nullptr);
    lib.functions["TlsSetValue"] = fn((void*)nullptr);
    lib.functions["FlsAlloc"] = fn((void*)nullptr);
    lib.functions["FlsFree"] = fn((void*)nullptr);
    lib.functions["FlsGetValue"] = fn((void*)nullptr);
    lib.functions["FlsSetValue"] = fn((void*)nullptr);
    lib.functions["GetExitCodeThread"] = fn((void*)nullptr);
    lib.functions["TerminateThread"] = fn((void*)nullptr);
    lib.functions["GetThreadLocale"] = fn((void*)nullptr);
    lib.functions["GetUserDefaultLCID"] = fn((void*)static_cast<DWORD(*)()>([]() -> DWORD { return 0x0409; }));
    lib.functions["GetUserDefaultLangID"] = fn((void*)static_cast<WORD(*)()>([]() -> WORD { return 0x0409; }));
    lib.functions["GetACP"] = fn((void*)static_cast<UINT(*)()>([]() -> UINT { return 65001; }));
    lib.functions["IsValidCodePage"] = fn((void*)static_cast<BOOL(*)(UINT)>([](UINT) -> BOOL { return 1; }));
    lib.functions["GetCPInfo"] = fn((void*)nullptr);
    lib.functions["HeapValidate"] = fn((void*)static_cast<BOOL(*)()>([]() -> BOOL { return 1; }));
    lib.functions["GetProcessAffinityMask"] = fn((void*)nullptr);
    lib.functions["SetThreadAffinityMask"] = fn((void*)nullptr);
    lib.functions["GetLogicalProcessorInformation"] = fn((void*)nullptr);
    lib.functions["VirtualQuery"] = fn((void*)nullptr);
    lib.functions["GetComputerNameA"] = fn((void*)nullptr);
    lib.functions["GetComputerNameW"] = fn((void*)nullptr);
    lib.functions["GetUserNameA"] = fn((void*)nullptr);
    lib.functions["GetUserNameW"] = fn((void*)nullptr);
    lib.functions["FindFirstFileA"] = fn((void*)static_cast<HANDLE(*)()>([]() -> HANDLE { return INVALID_HANDLE_VALUE; }));
    lib.functions["FindNextFileA"] = fn((void*)static_cast<BOOL(*)()>([]() -> BOOL { return 0; }));
    lib.functions["FindClose"] = fn((void*)static_cast<BOOL(*)()>([]() -> BOOL { return 1; }));
    lib.functions["GetFileAttributesA"] = fn((void*)static_cast<DWORD(*)(const char*)>([](const char* p) -> DWORD {
        return access(p, F_OK) == 0 ? FILE_ATTRIBUTE_NORMAL : 0xFFFFFFFF;
    }));
    lib.functions["SetFilePointer"] = fn((void*)nullptr);
    lib.functions["SetFilePointerEx"] = fn((void*)nullptr);
    lib.functions["FlushFileBuffers"] = fn((void*)nullptr);
    lib.functions["GetFileType"] = fn((void*)nullptr);
    lib.functions["GetTempPathA"] = fn((void*)static_cast<DWORD(*)(DWORD, char*)>([](DWORD n, char* b) -> DWORD {
        const char* t = "/tmp/";
        size_t l = strlen(t);
        if (n > l) memcpy(b, t, l + 1);
        return (DWORD)l;
    }));
    lib.functions["GetTempPathW"] = fn((void*)nullptr);
    lib.functions["RaiseException"] = fn((void*)nullptr);
    lib.functions["UnhandledExceptionFilter"] = fn((void*)nullptr);
    lib.functions["SetUnhandledExceptionFilter"] = fn((void*)nullptr);
    lib.functions["IsDebuggerPresent"] = fn((void*)static_cast<BOOL(*)()>([]() -> BOOL { return 0; }));
    lib.functions["DebugBreak"] = fn((void*)nullptr);
    lib.functions["GetVersionExA"] = fn((void*)nullptr);
    lib.functions["GetVersionExW"] = fn((void*)nullptr);
    lib.functions["GetNativeSystemInfo"] = fn((void*)shim_GetSystemInfo);

    return lib;
}

}
}
