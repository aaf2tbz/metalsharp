#include <metalsharp/NtdllShim.h>
#include <metalsharp/Win32Types.h>
#include <metalsharp/Logger.h>
#include <cstring>
#include <cstdlib>
#include <sched.h>

namespace metalsharp {
namespace win32 {

static int64_t MSABI shim_RtlGetVersion(void* lpVersionInformation) {
    (void)lpVersionInformation;
    return 0;
}

static void* MSABI shim_RtlAllocateHeap(void* HeapHandle, uint32_t Flags, size_t Size) {
    if (Flags & 0x8) return calloc(1, Size);
    return malloc(Size);
}

static int MSABI shim_RtlFreeHeap(void* HeapHandle, uint32_t Flags, void* BaseAddress) {
    free(BaseAddress);
    return 1;
}

static size_t MSABI shim_RtlSizeHeap(void* HeapHandle, uint32_t Flags, void* BaseAddress) {
    (void)HeapHandle; (void)Flags; (void)BaseAddress;
    return 0;
}

static uint32_t MSABI shim_RtlComputeCrc32(uint32_t PartialCrc, const void* Buffer, size_t Length) {
    const uint8_t* data = static_cast<const uint8_t*>(Buffer);
    uint32_t crc = PartialCrc ^ 0xFFFFFFFF;
    for (size_t i = 0; i < Length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
    }
    return crc ^ 0xFFFFFFFF;
}

static void MSABI shim_RtlInitializeCriticalSection(void* lpCriticalSection) {
    auto** mtx = reinterpret_cast<pthread_mutex_t**>(lpCriticalSection);
    *mtx = new pthread_mutex_t();
    pthread_mutex_init(*mtx, nullptr);
}

static void MSABI shim_RtlEnterCriticalSection(void* lpCriticalSection) {
    auto** mtx = reinterpret_cast<pthread_mutex_t**>(lpCriticalSection);
    if (*mtx) pthread_mutex_lock(*mtx);
}

static void MSABI shim_RtlLeaveCriticalSection(void* lpCriticalSection) {
    auto** mtx = reinterpret_cast<pthread_mutex_t**>(lpCriticalSection);
    if (*mtx) pthread_mutex_unlock(*mtx);
}

static void MSABI wrap_memset(void* d, int c, size_t n) { memset(d, c, n); }
static void MSABI wrap_memcpy(void* d, const void* s, size_t n) { memcpy(d, s, n); }
static void MSABI wrap_memmove(void* d, const void* s, size_t n) { memmove(d, s, n); }
static int MSABI wrap_memcmp(const void* a, const void* b, size_t n) { return memcmp(a, b, n); }

static int MSABI stub_NtYieldExecution() { sched_yield(); return 0; }

static void MSABI stub_NtTerminateProcess(void*, int code) { exit(code); }

static void MSABI wrap_RtlZeroMemory(void* d, size_t n) { memset(d, 0, n); }

ShimLibrary createNtdllShim() {
    ShimLibrary lib;
    lib.name = "ntdll.dll";

    auto fn = [](void* ptr) -> ExportedFunction {
        return [ptr]() -> void* { return ptr; };
    };

    lib.functions["RtlGetVersion"] = fn((void*)shim_RtlGetVersion);
    lib.functions["RtlAllocateHeap"] = fn((void*)shim_RtlAllocateHeap);
    lib.functions["RtlFreeHeap"] = fn((void*)shim_RtlFreeHeap);
    lib.functions["RtlSizeHeap"] = fn((void*)shim_RtlSizeHeap);
    lib.functions["RtlComputeCrc32"] = fn((void*)shim_RtlComputeCrc32);
    lib.functions["RtlInitializeCriticalSection"] = fn((void*)shim_RtlInitializeCriticalSection);
    lib.functions["RtlEnterCriticalSection"] = fn((void*)shim_RtlEnterCriticalSection);
    lib.functions["RtlLeaveCriticalSection"] = fn((void*)shim_RtlLeaveCriticalSection);
    lib.functions["RtlDeleteCriticalSection"] = fn((void*)nullptr);
    lib.functions["RtlTryEnterCriticalSection"] = fn((void*)nullptr);
    lib.functions["RtlInitializeCriticalSectionAndSpinCount"] = fn((void*)shim_RtlInitializeCriticalSection);
    lib.functions["RtlSetCriticalSectionSpinCount"] = fn((void*)nullptr);
    lib.functions["RtlZeroMemory"] = fn((void*)wrap_RtlZeroMemory);
    lib.functions["RtlFillMemory"] = fn((void*)wrap_memset);
    lib.functions["RtlCopyMemory"] = fn((void*)wrap_memcpy);
    lib.functions["RtlMoveMemory"] = fn((void*)wrap_memmove);
    lib.functions["RtlCompareMemory"] = fn((void*)wrap_memcmp);
    lib.functions["RtlInitUnicodeString"] = fn((void*)nullptr);
    lib.functions["RtlAnsiStringToUnicodeString"] = fn((void*)nullptr);
    lib.functions["RtlFreeUnicodeString"] = fn((void*)nullptr);
    lib.functions["RtlUnicodeStringToAnsiString"] = fn((void*)nullptr);
    lib.functions["RtlFreeAnsiString"] = fn((void*)nullptr);
    lib.functions["NtClose"] = fn((void*)nullptr);
    lib.functions["NtCreateFile"] = fn((void*)nullptr);
    lib.functions["NtReadFile"] = fn((void*)nullptr);
    lib.functions["NtWriteFile"] = fn((void*)nullptr);
    lib.functions["NtQueryInformationProcess"] = fn((void*)nullptr);
    lib.functions["NtQueryVirtualMemory"] = fn((void*)nullptr);
    lib.functions["NtProtectVirtualMemory"] = fn((void*)nullptr);
    lib.functions["NtAllocateVirtualMemory"] = fn((void*)nullptr);
    lib.functions["NtFreeVirtualMemory"] = fn((void*)nullptr);
    lib.functions["NtQuerySystemInformation"] = fn((void*)nullptr);
    lib.functions["NtQueryPerformanceCounter"] = fn((void*)nullptr);
    lib.functions["NtQueryTimerResolution"] = fn((void*)nullptr);
    lib.functions["RtlMultiByteToUnicodeN"] = fn((void*)nullptr);
    lib.functions["RtlUnicodeToMultiByteN"] = fn((void*)nullptr);
    lib.functions["RtlMultiByteToUnicodeSize"] = fn((void*)nullptr);
    lib.functions["RtlUnicodeToMultiByteSize"] = fn((void*)nullptr);
    lib.functions["NtQueryAttributesFile"] = fn((void*)nullptr);
    lib.functions["NtOpenFile"] = fn((void*)nullptr);
    lib.functions["NtSetInformationFile"] = fn((void*)nullptr);
    lib.functions["NtQueryInformationFile"] = fn((void*)nullptr);
    lib.functions["NtQueryDirectoryFile"] = fn((void*)nullptr);
    lib.functions["NtQueryVolumeInformationFile"] = fn((void*)nullptr);
    lib.functions["RtlGetLastNtStatus"] = fn((void*)nullptr);
    lib.functions["RtlNtStatusToDosError"] = fn((void*)nullptr);
    lib.functions["NtDeviceIoControlFile"] = fn((void*)nullptr);
    lib.functions["NtYieldExecution"] = fn((void*)stub_NtYieldExecution);
    lib.functions["RtlDecompressBuffer"] = fn((void*)nullptr);
    lib.functions["RtlDecompressFragment"] = fn((void*)nullptr);
    lib.functions["RtlLookupFunctionEntry"] = fn((void*)nullptr);
    lib.functions["RtlVirtualUnwind"] = fn((void*)nullptr);
    lib.functions["RtlRaiseException"] = fn((void*)nullptr);
    lib.functions["KiUserExceptionDispatcher"] = fn((void*)nullptr);
    lib.functions["NtTerminateProcess"] = fn((void*)stub_NtTerminateProcess);

    return lib;
}

}
}
