#include <metalsharp/PELoader.h>
#include <metalsharp/Win32Types.h>
#include <metalsharp/Logger.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
#include <chrono>
#include <ctime>
#include <cerrno>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>

namespace metalsharp {
namespace win32 {

static thread_local DWORD t_lastError = 0;

static void* stub_zero() { return nullptr; }
static BOOL stub_true() { return 1; }
static DWORD stub_zero_dword() { return 0; }
static void stub_void() {}

static void* shim_VirtualProtect(void* lpAddress, SIZE_T dwSize, DWORD flNewProtect, DWORD* lpflOldProtect) {
    (void)dwSize; (void)flNewProtect;
    if (lpflOldProtect) *lpflOldProtect = PAGE_EXECUTE_READWRITE;
    return reinterpret_cast<void*>(1);
}

static BOOL shim_VirtualQuery(void* lpAddress, void* lpBuffer, SIZE_T dwLength, SIZE_T* lpResultLength) {
    (void)lpAddress; (void)dwLength;
    auto* mbi = reinterpret_cast<MEMORY_BASIC_INFORMATION*>(lpBuffer);
    memset(mbi, 0, sizeof(MEMORY_BASIC_INFORMATION));
    mbi->BaseAddress = lpAddress;
    mbi->AllocationBase = lpAddress;
    mbi->RegionSize = 65536;
    mbi->State = 0x1000;
    mbi->Protect = PAGE_EXECUTE_READWRITE;
    mbi->Type = 0x20000;
    if (lpResultLength) *lpResultLength = sizeof(MEMORY_BASIC_INFORMATION);
    return 1;
}

static void shim_ExitProcess(UINT uExitCode) {
    MS_INFO("PELoader: ExitProcess(%u)", uExitCode);
    exit(uExitCode);
}

static void shim_RaiseException(DWORD dwExceptionCode, DWORD dwExceptionFlags,
    DWORD nNumberOfArguments, void* lpArguments) {
    (void)dwExceptionFlags; (void)nNumberOfArguments; (void)lpArguments;
    MS_INFO("PELoader: RaiseException(0x%08X)", dwExceptionCode);
    abort();
}

static void* shim_SetUnhandledExceptionFilter(void* lpTopLevelExceptionFilter) {
    (void)lpTopLevelExceptionFilter;
    return nullptr;
}

static void* shim_UnhandledExceptionFilter(void* exceptionInfo) {
    (void)exceptionInfo;
    MS_INFO("PELoader: UnhandledExceptionFilter called");
    abort();
    return nullptr;
}

static void shim_DebugBreak() {
    MS_INFO("PELoader: DebugBreak");
}

static char s_cmdLineA[4096] = "";
static wchar_t s_cmdLineW[4096] = {0};

void setCommandLine(const char* cmd) {
    strncpy(s_cmdLineA, cmd, sizeof(s_cmdLineA) - 1);
    for (size_t i = 0; i < sizeof(s_cmdLineW)/sizeof(wchar_t) - 1 && cmd[i]; i++) {
        s_cmdLineW[i] = (wchar_t)(unsigned char)cmd[i];
    }
}

static char* shim_GetCommandLineA() { return s_cmdLineA; }
static wchar_t* shim_GetCommandLineW() { return s_cmdLineW; }

static DWORD shim_GetEnvironmentVariableW(const wchar_t* lpName, wchar_t* lpBuffer, DWORD nSize) {
    (void)lpName; (void)lpBuffer; (void)nSize;
    return 0;
}

static DWORD shim_GetEnvironmentVariableA(const char* lpName, char* lpBuffer, DWORD nSize) {
    (void)lpName; (void)lpBuffer; (void)nSize;
    return 0;
}

static BOOL shim_SetEnvironmentVariableW(const wchar_t* lpName, const wchar_t* lpValue) {
    (void)lpName; (void)lpValue;
    return 1;
}

static DWORD shim_ExpandEnvironmentStringsW(const wchar_t* lpSrc, wchar_t* lpDst, DWORD nSize) {
    (void)lpSrc;
    if (lpDst && nSize > 0) lpDst[0] = 0;
    return 0;
}

static void shim_GetStartupInfoW(STARTUPINFOW* lpStartupInfo) {
    memset(lpStartupInfo, 0, sizeof(STARTUPINFOW));
    lpStartupInfo->cb = sizeof(STARTUPINFOW);
}

static void* shim_GetStdHandle(DWORD nStdHandle) {
    switch (nStdHandle) {
        case 0xFFFFFFF6: return reinterpret_cast<void*>(stdin);
        case 0xFFFFFFF5: return reinterpret_cast<void*>(stdout);
        case 0xFFFFFFF4: return reinterpret_cast<void*>(stderr);
        default: return INVALID_HANDLE_VALUE;
    }
}

static BOOL shim_SetStdHandle(DWORD nStdHandle, HANDLE hHandle) {
    (void)nStdHandle; (void)hHandle;
    return 1;
}

static BOOL shim_FreeLibrary(HMODULE hLibModule) {
    (void)hLibModule;
    return 1;
}

static HMODULE shim_LoadLibraryExA(const char* lpLibFileName, HANDLE hFile, DWORD dwFlags) {
    (void)hFile; (void)dwFlags;
    MS_INFO("PELoader: LoadLibraryExA(\"%s\")", lpLibFileName ? lpLibFileName : "(null)");
    return reinterpret_cast<HMODULE>(0x2);
}

static HMODULE shim_LoadLibraryExW(const wchar_t* lpLibFileName, HANDLE hFile, DWORD dwFlags) {
    (void)hFile; (void)dwFlags;
    MS_INFO("PELoader: LoadLibraryExW(...) ");
    return reinterpret_cast<HMODULE>(0x2);
}

static HMODULE shim_GetModuleHandleExA(DWORD dwFlags, const char* lpModuleName, HMODULE* phModule) {
    (void)dwFlags; (void)lpModuleName;
    if (phModule) *phModule = reinterpret_cast<HMODULE>(0x2);
    return reinterpret_cast<HMODULE>(0x2);
}

static HMODULE shim_GetModuleHandleExW(DWORD dwFlags, const wchar_t* lpModuleName, HMODULE* phModule) {
    (void)dwFlags; (void)lpModuleName;
    if (phModule) *phModule = reinterpret_cast<HMODULE>(0x2);
    return reinterpret_cast<HMODULE>(0x2);
}

static void* shim_CreateEventA(void* lpEventAttributes, BOOL bManualReset, BOOL bInitialState, const char* lpName) {
    (void)lpEventAttributes; (void)bManualReset; (void)bInitialState; (void)lpName;
    return reinterpret_cast<void*>(0x100);
}

static BOOL shim_ResetEvent(HANDLE hEvent) { (void)hEvent; return 1; }
static BOOL shim_SetEvent(HANDLE hEvent) { (void)hEvent; return 1; }

static HANDLE shim_OpenEventA(DWORD dwDesiredAccess, BOOL bInheritHandle, const char* lpName) {
    (void)dwDesiredAccess; (void)bInheritHandle; (void)lpName;
    return reinterpret_cast<HANDLE>(0x100);
}

static void* shim_CreateIoCompletionPort(HANDLE FileHandle, HANDLE ExistingCompletionPort,
    void* CompletionKey, DWORD NumberOfConcurrentThreads) {
    (void)FileHandle; (void)ExistingCompletionPort; (void)CompletionKey; (void)NumberOfConcurrentThreads;
    return reinterpret_cast<void*>(0x101);
}

static BOOL shim_PostQueuedCompletionStatus(HANDLE CompletionPort, DWORD dwNumberOfBytesTransferred,
    void* dwCompletionKey, void* lpOverlapped) {
    (void)CompletionPort; (void)dwNumberOfBytesTransferred; (void)dwCompletionKey; (void)lpOverlapped;
    return 1;
}

static BOOL shim_DuplicateHandle(HANDLE hSourceProcessHandle, HANDLE hSourceHandle,
    HANDLE hTargetProcessHandle, HANDLE* lpTargetHandle, DWORD dwDesiredAccess,
    BOOL bInheritHandle, DWORD dwOptions) {
    (void)hSourceProcessHandle; (void)hSourceHandle; (void)hTargetProcessHandle;
    (void)dwDesiredAccess; (void)bInheritHandle; (void)dwOptions;
    if (lpTargetHandle) *lpTargetHandle = hSourceHandle;
    return 1;
}

static HANDLE shim_OpenProcess(DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwProcessId) {
    (void)dwDesiredAccess; (void)bInheritHandle; (void)dwProcessId;
    return reinterpret_cast<HANDLE>(0x200);
}

static HANDLE shim_OpenThread(DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwThreadId) {
    (void)dwDesiredAccess; (void)bInheritHandle; (void)dwThreadId;
    return reinterpret_cast<HANDLE>(0x201);
}

static BOOL shim_TerminateProcess(HANDLE hProcess, UINT uExitCode) {
    (void)hProcess; (void)uExitCode;
    return 1;
}

static BOOL shim_GetExitCodeProcess(HANDLE hProcess, DWORD* lpExitCode) {
    (void)hProcess;
    if (lpExitCode) *lpExitCode = 259;
    return 1;
}

static BOOL shim_GetExitCodeThread(HANDLE hThread, DWORD* lpExitCode) {
    (void)hThread;
    if (lpExitCode) *lpExitCode = 259;
    return 1;
}

static BOOL shim_TerminateThread(HANDLE hThread, DWORD dwExitCode) {
    (void)hThread; (void)dwExitCode;
    return 1;
}

static DWORD shim_SuspendThread(HANDLE hThread) { (void)hThread; return 0; }
static DWORD shim_ResumeThread(HANDLE hThread) { (void)hThread; return 0; }
static BOOL shim_SetThreadPriority(HANDLE hThread, int nPriority) { (void)hThread; (void)nPriority; return 1; }
static DWORD shim_SetThreadAffinityMask(HANDLE hThread, DWORD_PTR dwThreadAffinityMask) {
    (void)hThread; (void)dwThreadAffinityMask; return 1;
}
static BOOL shim_GetProcessAffinityMask(HANDLE hProcess, DWORD_PTR* lpProcessAffinityMask, DWORD_PTR* lpSystemAffinityMask) {
    (void)hProcess;
    if (lpProcessAffinityMask) *lpProcessAffinityMask = 1;
    if (lpSystemAffinityMask) *lpSystemAffinityMask = 1;
    return 1;
}
static BOOL shim_SetProcessAffinityMask(HANDLE hProcess, DWORD_PTR dwProcessAffinityMask) {
    (void)hProcess; (void)dwProcessAffinityMask; return 1;
}

static BOOL shim_SwitchToThread() { sched_yield(); return 1; }

static void shim_InitializeSListHead(void* ListHead) { memset(ListHead, 0, 16); }
static void* shim_InterlockedPushEntrySList(void* ListHead, void* ListEntry) {
    (void)ListHead; (void)ListEntry; return nullptr;
}

static void shim_AcquireSRWLockExclusive(void* SRWLock) {
    auto* mtx = static_cast<pthread_mutex_t*>(SRWLock);
    pthread_mutex_lock(mtx);
}

static void shim_ReleaseSRWLockExclusive(void* SRWLock) {
    auto* mtx = static_cast<pthread_mutex_t*>(SRWLock);
    pthread_mutex_unlock(mtx);
}

static BOOL shim_TryAcquireSRWLockExclusive(void* SRWLock) {
    (void)SRWLock; return 1;
}

static BOOL shim_SleepConditionVariableSRW(void* ConditionVariable, void* SRWLock, DWORD dwMilliseconds, ULONG Flags) {
    (void)ConditionVariable; (void)SRWLock; (void)dwMilliseconds; (void)Flags;
    usleep(1000);
    return 1;
}

static void shim_WakeAllConditionVariable(void* ConditionVariable) { (void)ConditionVariable; }

static void shim_InitializeCriticalSectionAndSpinCount(void* lpCriticalSection, DWORD dwSpinCount) {
    auto* cs = reinterpret_cast<CRITICAL_SECTION*>(lpCriticalSection);
    auto* mtx = new pthread_mutex_t();
    pthread_mutex_init(mtx, nullptr);
    cs->DebugInfo = mtx;
    cs->SpinCount = dwSpinCount;
}

static void shim_InitializeCriticalSectionEx(void* lpCriticalSection, DWORD dwSpinCount, DWORD dwFlags) {
    (void)dwFlags;
    shim_InitializeCriticalSectionAndSpinCount(lpCriticalSection, dwSpinCount);
}

static BOOL shim_TryEnterCriticalSection(void* lpCriticalSection) {
    (void)lpCriticalSection; return 1;
}

static BOOL shim_InitOnceBeginInitialize(void* InitOnce, DWORD dwFlags, BOOL* fPending, void** lpContext) {
    (void)InitOnce; (void)dwFlags; (void)lpContext;
    if (fPending) *fPending = 0;
    return 1;
}

static BOOL shim_InitOnceComplete(void* InitOnce, DWORD dwFlags, void* lpContext) {
    (void)InitOnce; (void)dwFlags; (void)lpContext; return 1;
}

static thread_local void* s_tlsSlots[1088];
static DWORD s_nextTlsSlot = 64;

static DWORD shim_TlsAlloc() {
    if (s_nextTlsSlot >= 1088) return 0xFFFFFFFF;
    return s_nextTlsSlot++;
}

static BOOL shim_TlsFree(DWORD dwTlsIndex) { (void)dwTlsIndex; return 1; }

static void* shim_TlsGetValue(DWORD dwTlsIndex) {
    if (dwTlsIndex >= 1088) return nullptr;
    return s_tlsSlots[dwTlsIndex];
}

static BOOL shim_TlsSetValue(DWORD dwTlsIndex, void* lpTlsValue) {
    if (dwTlsIndex >= 1088) return 0;
    s_tlsSlots[dwTlsIndex] = lpTlsValue;
    return 1;
}

static DWORD s_flsNext = 0;
static void* s_flsSlots[256];

static DWORD shim_FlsAlloc(void* lpCallback) {
    (void)lpCallback;
    if (s_flsNext >= 256) return 0xFFFFFFFF;
    s_flsSlots[s_flsNext] = nullptr;
    return s_flsNext++;
}

static BOOL shim_FlsFree(DWORD dwFlsIndex) { (void)dwFlsIndex; return 1; }
static void* shim_FlsGetValue(DWORD dwFlsIndex) {
    if (dwFlsIndex >= 256) return nullptr;
    return s_flsSlots[dwFlsIndex];
}
static BOOL shim_FlsSetValue(DWORD dwFlsIndex, void* lpFlsValue) {
    if (dwFlsIndex >= 256) return 0;
    s_flsSlots[dwFlsIndex] = lpFlsValue;
    return 1;
}

static void* shim_ConvertThreadToFiber(void* lpParameter) { (void)lpParameter; return reinterpret_cast<void*>(0x300); }
static BOOL shim_ConvertFiberToThread() { return 1; }
static void* shim_CreateFiber(SIZE_T dwStackSize, void* lpStartAddress, void* lpParameter) {
    (void)dwStackSize; (void)lpStartAddress; (void)lpParameter;
    return reinterpret_cast<void*>(0x301);
}
static void shim_DeleteFiber(void* lpFiber) { (void)lpFiber; }
static void shim_SwitchToFiber(void* lpFiber) { (void)lpFiber; }

static void shim_GetSystemTime(void* lpSystemTime) {
    auto* st = reinterpret_cast<WORD*>(lpSystemTime);
    time_t now = time(nullptr);
    struct tm t;
    gmtime_r(&now, &t);
    st[0] = (WORD)(1900 + t.tm_year);
    st[1] = (WORD)(1 + t.tm_mon);
    st[2] = (WORD)t.tm_wday;
    st[3] = (WORD)t.tm_mday;
    st[4] = (WORD)t.tm_hour;
    st[5] = (WORD)t.tm_min;
    st[6] = (WORD)t.tm_sec;
    st[7] = 0;
}

static void shim_GetSystemTimeAsFileTime(void* lpFileTime) {
    auto* ft = reinterpret_cast<uint64_t*>(lpFileTime);
    auto now = std::chrono::system_clock::now();
    auto dur = now.time_since_epoch();
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(dur).count();
    *ft = static_cast<uint64_t>((micros + 11644473600000000LL) * 10);
}

static void shim_FileTimeToSystemTime(const void* lpFileTime, void* lpSystemTime) {
    (void)lpFileTime; (void)lpSystemTime;
}

static void shim_SystemTimeToFileTime(const void* lpSystemTime, void* lpFileTime) {
    (void)lpSystemTime; (void)lpFileTime;
}

static void shim_SystemTimeToTzSpecificLocalTime(void* lpTimeZone, const void* lpUniversalTime, void* lpLocalTime) {
    (void)lpTimeZone;
    if (lpLocalTime && lpUniversalTime) memcpy(lpLocalTime, lpUniversalTime, 16);
}

static DWORD shim_GetTimeZoneInformation(void* lpTimeZoneInformation) {
    (void)lpTimeZoneInformation; return 0;
}

static int shim_GetDateFormatW(void* Locale, DWORD dwFlags, const void* lpDate,
    const wchar_t* lpFormat, wchar_t* lpDateStr, int cchDate) {
    (void)Locale; (void)dwFlags; (void)lpDate; (void)lpFormat;
    if (lpDateStr && cchDate > 0) { lpDateStr[0] = 0; return 0; }
    return 0;
}

static int shim_GetTimeFormatW(void* Locale, DWORD dwFlags, const void* lpTime,
    const wchar_t* lpFormat, wchar_t* lpTimeStr, int cchTime) {
    (void)Locale; (void)dwFlags; (void)lpTime; (void)lpFormat;
    if (lpTimeStr && cchTime > 0) { lpTimeStr[0] = 0; return 0; }
    return 0;
}

static BOOL shim_GetVersionExA(void* lpVersionInformation) {
    auto* vi = reinterpret_cast<BYTE*>(lpVersionInformation);
    if (vi[0] < 156) return 0;
    memset(vi, 0, 156);
    vi[0] = 156;
    auto* dw = reinterpret_cast<DWORD*>(vi);
    dw[1] = 0x00000006;
    dw[2] = 0x00000001;
    dw[3] = 0x00000002;
    auto* sz = reinterpret_cast<char*>(vi + 20);
    strcpy(sz, "Microsoft Windows 10");
    return 1;
}

static BOOL shim_VerifyVersionInfoW(void* lpVersionInformation, DWORD dwTypeMask, uint64_t dwlConditionMask) {
    (void)lpVersionInformation; (void)dwTypeMask; (void)dwlConditionMask;
    return 1;
}

static uint64_t shim_VerSetConditionMask(uint64_t dwlConditionMask, DWORD dwTypeBitMask, BYTE dwCondition) {
    (void)dwTypeBitMask; (void)dwCondition;
    return dwlConditionMask;
}

static void* shim_GlobalLock(HANDLE hMem) { return hMem; }
static BOOL shim_GlobalUnlock(HANDLE hMem) { (void)hMem; return 1; }

static void shim_GlobalMemoryStatusEx(void* lpBuffer) {
    auto* ms = reinterpret_cast<DWORD*>(lpBuffer);
    memset(ms, 0, 32);
    ms[0] = 32;
    ms[1] = 100;
    ms[2] = static_cast<DWORD>(16ULL * 1024 * 1024 * 1024);
    ms[3] = static_cast<DWORD>(8ULL * 1024 * 1024 * 1024);
}

static BOOL shim_HeapLock(HANDLE hHeap) { (void)hHeap; return 1; }
static BOOL shim_HeapUnlock(HANDLE hHeap) { (void)hHeap; return 1; }
static BOOL shim_HeapWalk(HANDLE hHeap, void* lpEntry) { (void)hHeap; (void)lpEntry; return 0; }
static BOOL shim_HeapQueryInformation(HANDLE hHeap, DWORD HeapInformationClass,
    void* HeapInformation, SIZE_T HeapInformationLength, SIZE_T* ReturnLength) {
    (void)hHeap; (void)HeapInformationClass; (void)HeapInformation; (void)HeapInformationLength; (void)ReturnLength;
    return 1;
}
static BOOL shim_HeapSetInformation(HANDLE hHeap, DWORD HeapInformationClass,
    void* HeapInformation, SIZE_T HeapInformationLength) {
    (void)hHeap; (void)HeapInformationClass; (void)HeapInformation; (void)HeapInformationLength;
    return 1;
}
static SIZE_T shim_HeapSize(HANDLE hHeap, DWORD dwFlags, const void* lpMem) {
    (void)hHeap; (void)dwFlags; (void)lpMem;
    return 1024;
}
static void* shim_HeapReAlloc(HANDLE hHeap, DWORD dwFlags, void* lpMem, SIZE_T dwBytes) {
    (void)hHeap; (void)dwFlags;
    return realloc(lpMem, dwBytes);
}

static DWORD shim_GetProcessHeaps(DWORD NumberOfHeaps, HANDLE* ProcessHeaps) {
    (void)NumberOfHeaps;
    if (ProcessHeaps) ProcessHeaps[0] = reinterpret_cast<HANDLE>(0x1);
    return 1;
}

static BOOL shim_IsBadWritePtr(void* lp, size_t ucb) { (void)lp; (void)ucb; return 0; }
static BOOL shim_DeviceIoControl(HANDLE hDevice, DWORD dwIoControlCode, void* lpInBuffer,
    DWORD nInBufferSize, void* lpOutBuffer, DWORD nOutBufferSize, DWORD* lpBytesReturned, void* lpOverlapped) {
    (void)hDevice; (void)dwIoControlCode; (void)lpInBuffer; (void)nInBufferSize;
    (void)lpOutBuffer; (void)nOutBufferSize; (void)lpOverlapped;
    if (lpBytesReturned) *lpBytesReturned = 0;
    return 0;
}

static BOOL shim_ProcessIdToSessionId(DWORD dwProcessId, DWORD* pSessionId) {
    (void)dwProcessId;
    if (pSessionId) *pSessionId = 0;
    return 1;
}

static void shim_OutputDebugStringW(const wchar_t* lpOutputString) {
    (void)lpOutputString;
}

static BOOL shim_PeekNamedPipe(HANDLE hNamedPipe, void* lpBuffer, DWORD nBufferSize,
    DWORD* lpBytesRead, DWORD* lpTotalBytesAvail, DWORD* lpBytesLeftThisMessage) {
    (void)hNamedPipe; (void)lpBuffer; (void)nBufferSize;
    if (lpBytesRead) *lpBytesRead = 0;
    if (lpTotalBytesAvail) *lpTotalBytesAvail = 0;
    if (lpBytesLeftThisMessage) *lpBytesLeftThisMessage = 0;
    return 0;
}

static BOOL shim_SetConsoleCtrlHandler(void* HandlerRoutine, BOOL Add) {
    (void)HandlerRoutine; (void)Add; return 1;
}
static BOOL shim_SetConsoleMode(HANDLE hConsoleHandle, DWORD dwMode) {
    (void)hConsoleHandle; (void)dwMode; return 1;
}
static BOOL shim_GetConsoleMode(HANDLE hConsoleHandle, DWORD* lpMode) {
    (void)hConsoleHandle;
    if (lpMode) *lpMode = 0x1FF;
    return 1;
}
static UINT shim_GetConsoleOutputCP() { return 437; }
static UINT shim_GetOEMCP() { return 437; }

static BOOL shim_GetCPInfo(UINT CodePage, void* lpCPInfo) {
    (void)CodePage;
    memset(lpCPInfo, 0, 28);
    reinterpret_cast<DWORD*>(lpCPInfo)[0] = 1;
    reinterpret_cast<BYTE*>(lpCPInfo)[4] = 1;
    return 1;
}

static void* shim_GetCurrentThread() { return reinterpret_cast<void*>(static_cast<intptr_t>(-2)); }

static int shim_CompareStringW(void* Locale, DWORD dwCmpFlags, const wchar_t* lpString1, int cchCount1,
    const wchar_t* lpString2, int cchCount2) {
    (void)Locale; (void)dwCmpFlags;
    int len1 = cchCount1 > 0 ? cchCount1 : (int)wcslen(lpString1);
    int len2 = cchCount2 > 0 ? cchCount2 : (int)wcslen(lpString2);
    int minLen = len1 < len2 ? len1 : len2;
    for (int i = 0; i < minLen; i++) {
        if (lpString1[i] < lpString2[i]) return 1;
        if (lpString1[i] > lpString2[i]) return 3;
    }
    if (len1 < len2) return 1;
    if (len1 > len2) return 3;
    return 2;
}

static int shim_LCMapStringW(void* Locale, DWORD dwMapFlags, const wchar_t* lpSrcStr, int cchSrc,
    wchar_t* lpDestStr, int cchDest) {
    (void)Locale; (void)dwMapFlags;
    if (!lpSrcStr) return 0;
    int len = cchSrc > 0 ? cchSrc : (int)wcslen(lpSrcStr);
    if (cchDest == 0) return len;
    int copyLen = len < cchDest ? len : cchDest;
    for (int i = 0; i < copyLen; i++) lpDestStr[i] = lpSrcStr[i];
    return copyLen;
}

static BOOL shim_GetStringTypeW(DWORD dwInfoType, const wchar_t* lpSrcStr, int cchSrc, WORD* lpCharType) {
    (void)dwInfoType;
    int len = cchSrc > 0 ? cchSrc : (int)wcslen(lpSrcStr);
    for (int i = 0; i < len; i++) {
        lpCharType[i] = 0;
        wchar_t c = lpSrcStr[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) lpCharType[i] |= 0x0001 | 0x0002;
        if (c >= '0' && c <= '9') lpCharType[i] |= 0x0004;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') lpCharType[i] |= 0x0008;
    }
    return 1;
}

static void* shim_FreeEnvironmentStringsW(wchar_t* lpszEnvironmentBlock) {
    (void)lpszEnvironmentBlock; return reinterpret_cast<void*>(1);
}

static wchar_t* shim_GetEnvironmentStringsW() {
    static wchar_t env[] = {0};
    return env;
}

static BOOL shim_CopyFileExW(const wchar_t* lpExistingFileName, const wchar_t* lpNewFileName,
    void* lpProgressRoutine, void* lpData, BOOL* pbCancel, DWORD dwCopyFlags) {
    (void)lpExistingFileName; (void)lpNewFileName; (void)lpProgressRoutine;
    (void)lpData; (void)pbCancel; (void)dwCopyFlags;
    return 0;
}

static BOOL shim_CreateDirectoryW(const wchar_t* lpPathName, void* lpSecurityAttributes) {
    (void)lpSecurityAttributes;
    char path[1024];
    for (int i = 0; lpPathName[i] && i < 1023; i++) path[i] = (char)(lpPathName[i] & 0xFF);
    path[1023] = 0;
    return mkdir(path, 0755) == 0 ? 1 : 0;
}

static BOOL shim_RemoveDirectoryA(const char* lpPathName) { return rmdir(lpPathName) == 0 ? 1 : 0; }
static BOOL shim_RemoveDirectoryW(const wchar_t* lpPathName) {
    char path[1024];
    for (int i = 0; lpPathName[i] && i < 1023; i++) path[i] = (char)(lpPathName[i] & 0xFF);
    path[1023] = 0;
    return rmdir(path) == 0 ? 1 : 0;
}
static BOOL shim_DeleteFileW(const wchar_t* lpFileName) {
    char path[1024];
    for (int i = 0; lpFileName[i] && i < 1023; i++) path[i] = (char)(lpFileName[i] & 0xFF);
    path[1023] = 0;
    return unlink(path) == 0 ? 1 : 0;
}
static BOOL shim_MoveFileExW(const wchar_t* lpExistingFileName, const wchar_t* lpNewFileName, DWORD dwFlags) {
    (void)dwFlags;
    char src[1024], dst[1024];
    for (int i = 0; lpExistingFileName[i] && i < 1023; i++) src[i] = (char)(lpExistingFileName[i] & 0xFF);
    src[1023] = 0;
    for (int i = 0; lpNewFileName[i] && i < 1023; i++) dst[i] = (char)(lpNewFileName[i] & 0xFF);
    dst[1023] = 0;
    return rename(src, dst) == 0 ? 1 : 0;
}
static BOOL shim_ReplaceFileW(const wchar_t* lpReplacedFileName, const wchar_t* lpReplacementFileName,
    const wchar_t* lpBackupFileName, DWORD dwReplaceFlags, void* lpExclude, void* lpReserved) {
    (void)lpReplacedFileName; (void)lpReplacementFileName; (void)lpBackupFileName;
    (void)dwReplaceFlags; (void)lpExclude; (void)lpReserved;
    return 0;
}
static BOOL shim_CreateSymbolicLinkW(const wchar_t* lpSymlinkFileName, const wchar_t* lpTargetFileName, DWORD dwFlags) {
    (void)lpSymlinkFileName; (void)lpTargetFileName; (void)dwFlags;
    return 0;
}
static BOOL shim_SetFileAttributesW(const wchar_t* lpFileName, DWORD dwFileAttributes) {
    (void)lpFileName; (void)dwFileAttributes; return 1;
}
static BOOL shim_SetCurrentDirectoryW(const wchar_t* lpPathName) {
    char path[1024];
    for (int i = 0; lpPathName[i] && i < 1023; i++) path[i] = (char)(lpPathName[i] & 0xFF);
    path[1023] = 0;
    return chdir(path) == 0 ? 1 : 0;
}

static DWORD shim_GetFullPathNameW(const wchar_t* lpFileName, DWORD nBufferLength, wchar_t* lpBuffer, wchar_t** lpFilePart) {
    (void)lpFileName; (void)lpFilePart;
    if (nBufferLength > 0 && lpBuffer) lpBuffer[0] = 0;
    return 0;
}

static BOOL shim_CreateProcessW(const wchar_t* lpApplicationName, const wchar_t* lpCommandLine,
    void* lpProcessAttributes, void* lpThreadAttributes, BOOL bInheritHandles,
    DWORD dwCreationFlags, void* lpEnvironment, const wchar_t* lpCurrentDirectory,
    void* lpStartupInfo, void* lpProcessInformation) {
    (void)lpApplicationName; (void)lpCommandLine; (void)lpProcessAttributes;
    (void)lpThreadAttributes; (void)bInheritHandles; (void)dwCreationFlags;
    (void)lpEnvironment; (void)lpCurrentDirectory; (void)lpStartupInfo; (void)lpProcessInformation;
    MS_INFO("PELoader: CreateProcessW stub");
    return 0;
}

static BOOL shim_FindFirstFileExW(const wchar_t* lpFileName, DWORD fInfoLevelId,
    void* lpFindFileData, DWORD fSearchOp, void* lpSearchFilter, DWORD dwAdditionalFlags) {
    (void)lpFileName; (void)fInfoLevelId; (void)lpFindFileData;
    (void)fSearchOp; (void)lpSearchFilter; (void)dwAdditionalFlags;
    return 0;
}

static void* shim_FindFirstFileW(const wchar_t* lpFileName, void* lpFindFileData) {
    (void)lpFileName; (void)lpFindFileData;
    return INVALID_HANDLE_VALUE;
}

static BOOL shim_FindNextFileW(void* hFindFile, void* lpFindFileData) {
    (void)hFindFile; (void)lpFindFileData; return 0;
}

static BOOL shim_GetFileAttributesExW(const wchar_t* lpFileName, DWORD fInfoLevelId, void* lpFileInformation) {
    (void)lpFileName; (void)fInfoLevelId; (void)lpFileInformation; return 0;
}

static DWORD shim_GetFileAttributesW(const wchar_t* lpFileName) {
    (void)lpFileName; return 0xFFFFFFFF;
}

static BOOL shim_GetFileInformationByHandle(HANDLE hFile, void* lpFileInformation) {
    (void)hFile; (void)lpFileInformation; return 0;
}

static BOOL shim_GetFileTime(HANDLE hFile, void* lpCreationTime, void* lpLastAccessTime, void* lpLastWriteTime) {
    (void)hFile; (void)lpCreationTime; (void)lpLastAccessTime; (void)lpLastWriteTime; return 0;
}
static BOOL shim_SetFileTime(HANDLE hFile, const void* lpCreationTime, const void* lpLastAccessTime, const void* lpLastWriteTime) {
    (void)hFile; (void)lpCreationTime; (void)lpLastAccessTime; (void)lpLastWriteTime; return 1;
}

static DWORD shim_GetFileType(HANDLE hFile) { (void)hFile; return 0x0001; }

static BOOL shim_GetDiskFreeSpaceA(const char* lpRootPathName, DWORD* lpSectorsPerCluster,
    DWORD* lpBytesPerSector, DWORD* lpNumberOfFreeClusters, DWORD* lpTotalNumberOfClusters) {
    (void)lpRootPathName;
    if (lpSectorsPerCluster) *lpSectorsPerCluster = 8;
    if (lpBytesPerSector) *lpBytesPerSector = 512;
    if (lpNumberOfFreeClusters) *lpNumberOfFreeClusters = 1000000;
    if (lpTotalNumberOfClusters) *lpTotalNumberOfClusters = 2000000;
    return 1;
}

static BOOL shim_GetDiskFreeSpaceExW(const wchar_t* lpDirectoryName, uint64_t* lpFreeBytesAvailable,
    uint64_t* lpTotalNumberOfBytes, uint64_t* lpTotalNumberOfFreeBytes) {
    (void)lpDirectoryName;
    if (lpFreeBytesAvailable) *lpFreeBytesAvailable = 8ULL * 1024 * 1024 * 1024 * 1024;
    if (lpTotalNumberOfBytes) *lpTotalNumberOfBytes = 16ULL * 1024 * 1024 * 1024 * 1024;
    if (lpTotalNumberOfFreeBytes) *lpTotalNumberOfFreeBytes = 8ULL * 1024 * 1024 * 1024 * 1024;
    return 1;
}

static UINT shim_GetDriveTypeW(const wchar_t* lpRootPathName) {
    (void)lpRootPathName; return 3;
}

static BOOL shim_FlushFileBuffers(HANDLE hFile) { (void)hFile; return 1; }
static BOOL shim_SetEndOfFile(HANDLE hFile) { (void)hFile; return 1; }
static BOOL shim_SetHandleInformation(HANDLE hObject, DWORD dwMask, DWORD dwFlags) {
    (void)hObject; (void)dwMask; (void)dwFlags; return 1;
}
static DWORD shim_SetErrorMode(DWORD uMode) { (void)uMode; return 0; }

static void* shim_FindResourceA(HMODULE hModule, const char* lpName, const char* lpType) {
    (void)hModule; (void)lpName; (void)lpType; return nullptr;
}
static void* shim_LoadResource(HMODULE hModule, void* hResInfo) {
    (void)hModule; (void)hResInfo; return nullptr;
}
static void* shim_LockResource(void* hResData) { (void)hResData; return nullptr; }
static DWORD shim_SizeofResource(HMODULE hModule, void* hResInfo) {
    (void)hModule; (void)hResInfo; return 0;
}

static int shim_MulDiv(int nNumber, int nNumerator, int nDenominator) {
    if (nDenominator == 0) return -1;
    return (int)((int64_t)nNumber * nNumerator / nDenominator);
}

static BOOL shim_ReadConsoleA(HANDLE hConsoleInput, void* lpBuffer, DWORD nNumberOfCharsToRead,
    DWORD* lpNumberOfCharsRead, void* pInputControl) {
    (void)hConsoleInput; (void)lpBuffer; (void)nNumberOfCharsToRead; (void)pInputControl;
    if (lpNumberOfCharsRead) *lpNumberOfCharsRead = 0;
    return 0;
}

static BOOL shim_ReadConsoleW(HANDLE hConsoleInput, void* lpBuffer, DWORD nNumberOfCharsToRead,
    DWORD* lpNumberOfCharsRead, void* pInputControl) {
    (void)hConsoleInput; (void)lpBuffer; (void)nNumberOfCharsToRead; (void)pInputControl;
    if (lpNumberOfCharsRead) *lpNumberOfCharsRead = 0;
    return 0;
}

static BOOL shim_WriteConsoleW(HANDLE hConsoleOutput, const void* lpBuffer, DWORD nNumberOfCharsToWrite,
    DWORD* lpNumberOfCharsWritten, void* lpReserved) {
    (void)hConsoleOutput; (void)lpReserved;
    if (lpNumberOfCharsWritten) *lpNumberOfCharsWritten = nNumberOfCharsToWrite;
    return 1;
}

static BOOL shim_GetProductInfo(DWORD dwOSMajorVersion, DWORD dwOSMinorVersion,
    DWORD dwSpMajorVersion, DWORD dwSpMinorVersion, DWORD* pdwReturnedProductType) {
    (void)dwOSMajorVersion; (void)dwOSMinorVersion;
    (void)dwSpMajorVersion; (void)dwSpMinorVersion;
    if (pdwReturnedProductType) *pdwReturnedProductType = 1;
    return 1;
}

static void shim_RtlCaptureContext(void* ContextRecord) { (void)ContextRecord; }
static void* shim_RtlCaptureStackBackTrace(DWORD FramesToSkip, DWORD FramesToCapture,
    void** BackTrace, DWORD* BackTraceHash) {
    (void)FramesToSkip; (void)FramesToCapture; (void)BackTrace;
    if (BackTraceHash) *BackTraceHash = 0;
    return nullptr;
}
static void* shim_RtlLookupFunctionEntry(uint64_t ControlPoint, uint64_t* ImageBase, void* HistoryTable) {
    (void)ControlPoint; (void)ImageBase; (void)HistoryTable;
    return nullptr;
}
static void* shim_RtlPcToFileHeader(uint64_t PcValue, uint64_t* ImageBase) {
    (void)PcValue;
    if (ImageBase) *ImageBase = 0;
    return nullptr;
}
static void shim_RtlUnwind(void* TargetFrame, void* TargetIp, void* ExceptionRecord, void* ReturnValue) {
    (void)TargetFrame; (void)TargetIp; (void)ExceptionRecord; (void)ReturnValue;
}
static void shim_RtlUnwindEx(void* TargetFrame, void* TargetIp, void* ExceptionRecord, void* ReturnValue, void* ContextRecord) {
    (void)TargetFrame; (void)TargetIp; (void)ExceptionRecord; (void)ReturnValue; (void)ContextRecord;
}
static void shim_RtlVirtualUnwind(DWORD HandlerType, uint64_t ImageBase, uint64_t ControlPc,
    void* FunctionEntry, void* ContextRecord, void** HandlerData, uint64_t* EstablisherFrame, void* ContextPointers) {
    (void)HandlerType; (void)ImageBase; (void)ControlPc; (void)FunctionEntry;
    (void)ContextRecord; (void)HandlerData; (void)EstablisherFrame; (void)ContextPointers;
}

static void* shim_EncodePointer(void* Ptr) { return Ptr; }
static void* shim_DecodePointer(void* Ptr) { return Ptr; }

void addMissingKernel32(ShimLibrary& lib) {
    auto fn = [](void* ptr) -> ExportedFunction {
        return [ptr]() -> void* { return ptr; };
    };

    lib.functions["VirtualProtect"] = fn((void*)shim_VirtualProtect);
    lib.functions["VirtualQuery"] = fn((void*)shim_VirtualQuery);
    lib.functions["ExitProcess"] = fn((void*)shim_ExitProcess);
    lib.functions["RaiseException"] = fn((void*)shim_RaiseException);
    lib.functions["SetUnhandledExceptionFilter"] = fn((void*)shim_SetUnhandledExceptionFilter);
    lib.functions["UnhandledExceptionFilter"] = fn((void*)shim_UnhandledExceptionFilter);
    lib.functions["DebugBreak"] = fn((void*)shim_DebugBreak);
    lib.functions["GetCommandLineA"] = fn((void*)shim_GetCommandLineA);
    lib.functions["GetCommandLineW"] = fn((void*)shim_GetCommandLineW);
    lib.functions["GetEnvironmentVariableW"] = fn((void*)shim_GetEnvironmentVariableW);
    lib.functions["GetEnvironmentVariableA"] = fn((void*)shim_GetEnvironmentVariableA);
    lib.functions["SetEnvironmentVariableW"] = fn((void*)shim_SetEnvironmentVariableW);
    lib.functions["ExpandEnvironmentStringsA"] = fn((void*)stub_zero_dword);
    lib.functions["ExpandEnvironmentStringsW"] = fn((void*)shim_ExpandEnvironmentStringsW);
    lib.functions["GetStartupInfoW"] = fn((void*)shim_GetStartupInfoW);
    lib.functions["FreeLibrary"] = fn((void*)shim_FreeLibrary);
    lib.functions["LoadLibraryExA"] = fn((void*)shim_LoadLibraryExA);
    lib.functions["LoadLibraryExW"] = fn((void*)shim_LoadLibraryExW);
    lib.functions["GetModuleHandleExA"] = fn((void*)shim_GetModuleHandleExA);
    lib.functions["GetModuleHandleExW"] = fn((void*)shim_GetModuleHandleExW);
    lib.functions["CreateEventA"] = fn((void*)shim_CreateEventA);
    lib.functions["ResetEvent"] = fn((void*)shim_ResetEvent);
    lib.functions["SetEvent"] = fn((void*)shim_SetEvent);
    lib.functions["OpenEventA"] = fn((void*)shim_OpenEventA);
    lib.functions["CreateIoCompletionPort"] = fn((void*)shim_CreateIoCompletionPort);
    lib.functions["PostQueuedCompletionStatus"] = fn((void*)shim_PostQueuedCompletionStatus);
    lib.functions["DuplicateHandle"] = fn((void*)shim_DuplicateHandle);
    lib.functions["OpenProcess"] = fn((void*)shim_OpenProcess);
    lib.functions["OpenThread"] = fn((void*)shim_OpenThread);
    lib.functions["TerminateProcess"] = fn((void*)shim_TerminateProcess);
    lib.functions["GetExitCodeProcess"] = fn((void*)shim_GetExitCodeProcess);
    lib.functions["GetExitCodeThread"] = fn((void*)shim_GetExitCodeThread);
    lib.functions["TerminateThread"] = fn((void*)shim_TerminateThread);
    lib.functions["SuspendThread"] = fn((void*)shim_SuspendThread);
    lib.functions["ResumeThread"] = fn((void*)shim_ResumeThread);
    lib.functions["SetThreadPriority"] = fn((void*)shim_SetThreadPriority);
    lib.functions["SetThreadAffinityMask"] = fn((void*)shim_SetThreadAffinityMask);
    lib.functions["GetProcessAffinityMask"] = fn((void*)shim_GetProcessAffinityMask);
    lib.functions["SetProcessAffinityMask"] = fn((void*)shim_SetProcessAffinityMask);
    lib.functions["SwitchToThread"] = fn((void*)shim_SwitchToThread);
    lib.functions["InitializeSListHead"] = fn((void*)shim_InitializeSListHead);
    lib.functions["InterlockedPushEntrySList"] = fn((void*)shim_InterlockedPushEntrySList);
    lib.functions["AcquireSRWLockExclusive"] = fn((void*)shim_AcquireSRWLockExclusive);
    lib.functions["ReleaseSRWLockExclusive"] = fn((void*)shim_ReleaseSRWLockExclusive);
    lib.functions["TryAcquireSRWLockExclusive"] = fn((void*)shim_TryAcquireSRWLockExclusive);
    lib.functions["SleepConditionVariableSRW"] = fn((void*)shim_SleepConditionVariableSRW);
    lib.functions["WakeAllConditionVariable"] = fn((void*)shim_WakeAllConditionVariable);
    lib.functions["InitializeCriticalSectionAndSpinCount"] = fn((void*)shim_InitializeCriticalSectionAndSpinCount);
    lib.functions["InitializeCriticalSectionEx"] = fn((void*)shim_InitializeCriticalSectionEx);
    lib.functions["TryEnterCriticalSection"] = fn((void*)shim_TryEnterCriticalSection);
    lib.functions["InitOnceBeginInitialize"] = fn((void*)shim_InitOnceBeginInitialize);
    lib.functions["InitOnceComplete"] = fn((void*)shim_InitOnceComplete);
    lib.functions["TlsAlloc"] = fn((void*)shim_TlsAlloc);
    lib.functions["TlsFree"] = fn((void*)shim_TlsFree);
    lib.functions["TlsGetValue"] = fn((void*)shim_TlsGetValue);
    lib.functions["TlsSetValue"] = fn((void*)shim_TlsSetValue);
    lib.functions["FlsAlloc"] = fn((void*)shim_FlsAlloc);
    lib.functions["FlsFree"] = fn((void*)shim_FlsFree);
    lib.functions["FlsGetValue"] = fn((void*)shim_FlsGetValue);
    lib.functions["FlsSetValue"] = fn((void*)shim_FlsSetValue);
    lib.functions["ConvertThreadToFiber"] = fn((void*)shim_ConvertThreadToFiber);
    lib.functions["ConvertFiberToThread"] = fn((void*)shim_ConvertFiberToThread);
    lib.functions["CreateFiber"] = fn((void*)shim_CreateFiber);
    lib.functions["DeleteFiber"] = fn((void*)shim_DeleteFiber);
    lib.functions["SwitchToFiber"] = fn((void*)shim_SwitchToFiber);
    lib.functions["GetSystemTime"] = fn((void*)shim_GetSystemTime);
    lib.functions["GetSystemTimeAsFileTime"] = fn((void*)shim_GetSystemTimeAsFileTime);
    lib.functions["FileTimeToSystemTime"] = fn((void*)shim_FileTimeToSystemTime);
    lib.functions["SystemTimeToFileTime"] = fn((void*)shim_SystemTimeToFileTime);
    lib.functions["SystemTimeToTzSpecificLocalTime"] = fn((void*)shim_SystemTimeToTzSpecificLocalTime);
    lib.functions["GetTimeZoneInformation"] = fn((void*)shim_GetTimeZoneInformation);
    lib.functions["GetDateFormatW"] = fn((void*)shim_GetDateFormatW);
    lib.functions["GetTimeFormatW"] = fn((void*)shim_GetTimeFormatW);
    lib.functions["GetVersionExA"] = fn((void*)shim_GetVersionExA);
    lib.functions["GetVersionExW"] = fn((void*)shim_GetVersionExA);
    lib.functions["VerifyVersionInfoW"] = fn((void*)shim_VerifyVersionInfoW);
    lib.functions["VerSetConditionMask"] = fn((void*)shim_VerSetConditionMask);
    lib.functions["GlobalLock"] = fn((void*)shim_GlobalLock);
    lib.functions["GlobalUnlock"] = fn((void*)shim_GlobalUnlock);
    lib.functions["GlobalMemoryStatusEx"] = fn((void*)shim_GlobalMemoryStatusEx);
    lib.functions["HeapLock"] = fn((void*)shim_HeapLock);
    lib.functions["HeapUnlock"] = fn((void*)shim_HeapUnlock);
    lib.functions["HeapWalk"] = fn((void*)shim_HeapWalk);
    lib.functions["HeapQueryInformation"] = fn((void*)shim_HeapQueryInformation);
    lib.functions["HeapSetInformation"] = fn((void*)shim_HeapSetInformation);
    lib.functions["HeapSize"] = fn((void*)shim_HeapSize);
    lib.functions["HeapReAlloc"] = fn((void*)shim_HeapReAlloc);
    lib.functions["GetProcessHeaps"] = fn((void*)shim_GetProcessHeaps);
    lib.functions["IsBadWritePtr"] = fn((void*)shim_IsBadWritePtr);
    lib.functions["DeviceIoControl"] = fn((void*)shim_DeviceIoControl);
    lib.functions["ProcessIdToSessionId"] = fn((void*)shim_ProcessIdToSessionId);
    lib.functions["OutputDebugStringW"] = fn((void*)shim_OutputDebugStringW);
    lib.functions["PeekNamedPipe"] = fn((void*)shim_PeekNamedPipe);
    lib.functions["SetConsoleCtrlHandler"] = fn((void*)shim_SetConsoleCtrlHandler);
    lib.functions["SetConsoleMode"] = fn((void*)shim_SetConsoleMode);
    lib.functions["GetConsoleMode"] = fn((void*)shim_GetConsoleMode);
    lib.functions["GetConsoleOutputCP"] = fn((void*)shim_GetConsoleOutputCP);
    lib.functions["GetOEMCP"] = fn((void*)shim_GetOEMCP);
    lib.functions["GetCPInfo"] = fn((void*)shim_GetCPInfo);
    lib.functions["GetCurrentThread"] = fn((void*)shim_GetCurrentThread);
    lib.functions["CompareStringW"] = fn((void*)shim_CompareStringW);
    lib.functions["LCMapStringW"] = fn((void*)shim_LCMapStringW);
    lib.functions["GetStringTypeW"] = fn((void*)shim_GetStringTypeW);
    lib.functions["FreeEnvironmentStringsW"] = fn((void*)shim_FreeEnvironmentStringsW);
    lib.functions["GetEnvironmentStringsW"] = fn((void*)shim_GetEnvironmentStringsW);
    lib.functions["CopyFileExW"] = fn((void*)shim_CopyFileExW);
    lib.functions["CreateDirectoryW"] = fn((void*)shim_CreateDirectoryW);
    lib.functions["RemoveDirectoryA"] = fn((void*)shim_RemoveDirectoryA);
    lib.functions["RemoveDirectoryW"] = fn((void*)shim_RemoveDirectoryW);
    lib.functions["DeleteFileW"] = fn((void*)shim_DeleteFileW);
    lib.functions["MoveFileExW"] = fn((void*)shim_MoveFileExW);
    lib.functions["ReplaceFileW"] = fn((void*)shim_ReplaceFileW);
    lib.functions["CreateSymbolicLinkW"] = fn((void*)shim_CreateSymbolicLinkW);
    lib.functions["SetFileAttributesW"] = fn((void*)shim_SetFileAttributesW);
    lib.functions["SetCurrentDirectoryW"] = fn((void*)shim_SetCurrentDirectoryW);
    lib.functions["GetFullPathNameW"] = fn((void*)shim_GetFullPathNameW);
    lib.functions["CreateProcessW"] = fn((void*)shim_CreateProcessW);
    lib.functions["FindFirstFileExW"] = fn((void*)shim_FindFirstFileExW);
    lib.functions["FindFirstFileW"] = fn((void*)shim_FindFirstFileW);
    lib.functions["FindNextFileW"] = fn((void*)shim_FindNextFileW);
    lib.functions["GetFileAttributesExW"] = fn((void*)shim_GetFileAttributesExW);
    lib.functions["GetFileAttributesW"] = fn((void*)shim_GetFileAttributesW);
    lib.functions["GetFileInformationByHandle"] = fn((void*)shim_GetFileInformationByHandle);
    lib.functions["GetFileTime"] = fn((void*)shim_GetFileTime);
    lib.functions["SetFileTime"] = fn((void*)shim_SetFileTime);
    lib.functions["GetFileType"] = fn((void*)shim_GetFileType);
    lib.functions["GetDiskFreeSpaceA"] = fn((void*)shim_GetDiskFreeSpaceA);
    lib.functions["GetDiskFreeSpaceExW"] = fn((void*)shim_GetDiskFreeSpaceExW);
    lib.functions["GetDriveTypeW"] = fn((void*)shim_GetDriveTypeW);
    lib.functions["FlushFileBuffers"] = fn((void*)shim_FlushFileBuffers);
    lib.functions["SetEndOfFile"] = fn((void*)shim_SetEndOfFile);
    lib.functions["SetHandleInformation"] = fn((void*)shim_SetHandleInformation);
    lib.functions["SetErrorMode"] = fn((void*)shim_SetErrorMode);
    lib.functions["FindResourceA"] = fn((void*)shim_FindResourceA);
    lib.functions["LoadResource"] = fn((void*)shim_LoadResource);
    lib.functions["LockResource"] = fn((void*)shim_LockResource);
    lib.functions["SizeofResource"] = fn((void*)shim_SizeofResource);
    lib.functions["MulDiv"] = fn((void*)shim_MulDiv);
    lib.functions["ReadConsoleA"] = fn((void*)shim_ReadConsoleA);
    lib.functions["ReadConsoleW"] = fn((void*)shim_ReadConsoleW);
    lib.functions["WriteConsoleW"] = fn((void*)shim_WriteConsoleW);
    lib.functions["GetProductInfo"] = fn((void*)shim_GetProductInfo);
    lib.functions["RtlCaptureContext"] = fn((void*)shim_RtlCaptureContext);
    lib.functions["RtlCaptureStackBackTrace"] = fn((void*)shim_RtlCaptureStackBackTrace);
    lib.functions["RtlLookupFunctionEntry"] = fn((void*)shim_RtlLookupFunctionEntry);
    lib.functions["RtlPcToFileHeader"] = fn((void*)shim_RtlPcToFileHeader);
    lib.functions["RtlUnwind"] = fn((void*)shim_RtlUnwind);
    lib.functions["RtlUnwindEx"] = fn((void*)shim_RtlUnwindEx);
    lib.functions["RtlVirtualUnwind"] = fn((void*)shim_RtlVirtualUnwind);
    lib.functions["EncodePointer"] = fn((void*)shim_EncodePointer);
    lib.functions["DecodePointer"] = fn((void*)shim_DecodePointer);
    lib.functions["SetStdHandle"] = fn((void*)shim_SetStdHandle);
    lib.functions["GetFileSizeEx"] = fn((void*)static_cast<BOOL(*)(HANDLE, int64_t*)>([](HANDLE, int64_t* s) -> BOOL { if(s) *s = 0; return 1; }));
    lib.functions["SetFilePointer"] = fn((void*)static_cast<DWORD(*)(HANDLE, LONG, LONG*, DWORD)>([](HANDLE, LONG, LONG*, DWORD) -> DWORD { return 0; }));
    lib.functions["SetFilePointerEx"] = fn((void*)static_cast<BOOL(*)(HANDLE, int64_t, int64_t*, DWORD)>([](HANDLE, int64_t, int64_t*, DWORD) -> BOOL { return 1; }));
}

}
}
