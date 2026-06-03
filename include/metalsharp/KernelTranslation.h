/// @file KernelTranslation.h
/// @brief macOS XNU <-> Windows NT kernel API translation layer for anti-cheat compatibility.
///
/// Defines the type bridge, syscall mapping table, and host capability probe interface
/// for translating Windows NT kernel calls to macOS XNU equivalents. This is the backbone
/// of MetalSharp's anti-cheat compatibility layer.
///
/// Reference documents:
///   docs/research/kernel-translation/xnu-kernel-reference.md
///   docs/research/kernel-translation/windows-nt-kernel-reference.md
///   docs/research/kernel-translation/wine-macos-implementation-mapping.md
///   docs/research/kernel-translation/wine-kernel-compatibility-gap-analysis.md

#pragma once

#include <cstdint>
#include <string>

namespace metalsharp {
namespace kernel_translation {

enum class PairQuality : uint8_t {
    Direct = 0,
    Close = 1,
    UserspaceEmulation = 2,
    Partial = 3,
    Blocked = 4,
    NotNeeded = 5,
};

struct SyscallMapping {
    const char* nt_name;
    const char* xnu_name;
    const char* xnu_syscall_id;
    PairQuality quality;
    const char* wine_implementation;
};

struct StructMapping {
    const char* nt_name;
    const char* xnu_name;
    PairQuality quality;
    const char* notes;
};

struct BlockerDetail {
    const char* id;
    const char* description;
    const char* affected_anticheat[4];
    const char* workaround;
};

struct CoverageReport {
    uint32_t total;
    uint32_t direct;
    uint32_t close;
    uint32_t userspace;
    uint32_t partial;
    uint32_t blocked;
};

constexpr uint32_t NT_STATUS_SUCCESS = 0x00000000;
constexpr uint32_t NT_STATUS_NOT_SUPPORTED = 0xC00000BB;
constexpr uint32_t NT_STATUS_ACCESS_DENIED = 0xC0000022;
constexpr uint32_t NT_STATUS_NOT_IMPLEMENTED = 0xC0000002;

constexpr uint32_t XNU_BSD_SYSCALL_MMAP = 197;
constexpr uint32_t XNU_BSD_SYSCALL_MPROTECT = 74;
constexpr uint32_t XNU_BSD_SYSCALL_PTRACE = 26;
constexpr uint32_t XNU_BSD_SYSCALL_PROC_INFO = 336;
constexpr uint32_t XNU_BSD_SYSCALL_CSOPS = 169;
constexpr uint32_t XNU_BSD_SYSCALL_SYSCTL = 202;
constexpr uint32_t XNU_BSD_SYSCALL_POSIX_SPAWN = 244;
constexpr uint32_t XNU_BSD_SYSCALL_BSDTHREAD_CREATE = 360;
constexpr uint32_t XNU_BSD_SYSCALL_KQUEUE = 362;
constexpr uint32_t XNU_BSD_SYSCALL_KEVENT = 363;
constexpr uint32_t XNU_BSD_SYSCALL_ULOCK_WAIT = 515;
constexpr uint32_t XNU_BSD_SYSCALL_ULOCK_WAKE = 516;
constexpr uint32_t XNU_BSD_SYSCALL_PID_SUSPEND = 433;
constexpr uint32_t XNU_BSD_SYSCALL_PID_RESUME = 434;

constexpr uint32_t XNU_MACH_TRAP_MACH_MSG = 6;
constexpr uint32_t XNU_MACH_TRAP_TASK_FOR_PID = 50;
constexpr uint32_t XNU_MACH_TRAP_MK_TIMER_CREATE = 45;
constexpr uint32_t XNU_MACH_TRAP_MK_TIMER_ARM = 47;
constexpr uint32_t XNU_MACH_TRAP_MK_TIMER_CANCEL = 49;
constexpr uint32_t XNU_MACH_TRAP_MACH_VM_ALLOCATE = 17;
constexpr uint32_t XNU_MACH_TRAP_MACH_VM_DEALLOCATE = 18;
constexpr uint32_t XNU_MACH_TRAP_MACH_VM_PROTECT = 19;
constexpr uint32_t XNU_MACH_TRAP_SEMAPHORE_WAIT_SIGNAL = 13;

constexpr SyscallMapping SYSCALL_TABLE[] = {
    {"NtCreateProcess", "fork + execve", "BSD 2, 59", PairQuality::Direct, "fork() then execve()"},
    {"NtCreateUserProcess", "posix_spawn", "BSD 244", PairQuality::Direct, "posix_spawn()"},
    {"NtCreateThread", "bsdthread_create", "BSD 360", PairQuality::Direct, "bsdthread_create()"},
    {"NtTerminateProcess", "exit/kill", "BSD 1, 37", PairQuality::Direct, "kill(pid, SIGKILL)"},
    {"NtSuspendProcess", "pid_suspend", "BSD 433", PairQuality::Direct, "pid_suspend(pid)"},
    {"NtSuspendThread", "thread_suspend", "Mach IPC", PairQuality::Direct, "thread_suspend(mach_thread)"},
    {"NtAllocateVirtualMemory", "mmap/mach_vm_allocate", "BSD 197, Mach 17", PairQuality::Direct, "mmap()"},
    {"NtProtectVirtualMemory", "mprotect/mach_vm_protect", "BSD 74, Mach 19", PairQuality::Direct, "mprotect()"},
    {"NtReadVirtualMemory", "mach_vm_read", "Mach IPC", PairQuality::Direct, "mach_vm_read(task, addr, size)"},
    {"NtWriteVirtualMemory", "mach_vm_write", "Mach IPC", PairQuality::Direct, "mach_vm_write(task, addr, data)"},
    {"NtGetContextThread", "thread_get_state", "Mach IPC", PairQuality::Direct, "thread_get_state(ARM_THREAD_STATE64)"},
    {"NtSetContextThread", "thread_set_state", "Mach IPC", PairQuality::Direct, "thread_set_state(ARM_THREAD_STATE64)"},
    {"NtCreateFile", "open/openat", "BSD 5, 463", PairQuality::Direct, "openat()"},
    {"NtReadFile", "read/pread", "BSD 3, 153", PairQuality::Direct, "pread()"},
    {"NtDeviceIoControlFile", "ioctl", "BSD 54", PairQuality::Direct, "ioctl()"},
    {"NtCreateEvent", "ulock/kqueue", "BSD 515, 362", PairQuality::Close, "ulock_wait/ulock_wake"},
    {"NtCreateMutant", "psynch_mutexwait", "BSD 301", PairQuality::Close, "psynch_mutexwait/mutexdrop"},
    {"NtCreateSemaphore", "semaphore_create", "Mach IPC", PairQuality::Direct, "semaphore_create()"},
    {"NtCreateTimer", "mk_timer_create", "Mach 45", PairQuality::Direct, "mk_timer_create()"},
    {"NtWaitForSingleObject", "kevent", "BSD 363", PairQuality::Direct, "kevent()"},
    {"NtCreatePort", "mach_port_allocate", "Mach 22", PairQuality::Direct, "mach_port_allocate()"},
    {"NtConnectPort", "mach_msg", "Mach 6", PairQuality::Direct, "mach_msg()"},
    {"NtOpenProcess", "proc_info + task_for_pid", "BSD 336, Mach 50", PairQuality::Direct, "task_for_pid()"},
    {"NtQuerySystemInformation", "sysctl + proc_info", "BSD 202, 336", PairQuality::Close, "sysctl() + proc_info()"},
    {"NtDebugActiveProcess", "ptrace(PT_ATTACH)", "BSD 26", PairQuality::Direct, "ptrace(PT_ATTACH)"},
    {"NtGetCachedSigningLevel", "csops", "BSD 169", PairQuality::Direct, "csops(CS_OPS_GETSIGNINGINFO)"},
    {"NtQueueApcThread", "thread_set_state (trampoline)", "Mach IPC", PairQuality::Partial,
     "Suspend, modify ARM_CONTEXT pc, resume"},
    {"NtSetCachedSigningLevel", "—", "—", PairQuality::Blocked, "SIP prevents; return ACCESS_DENIED"},
    {"NtLoadDriver", "kext_load", "IOKit", PairQuality::Blocked, "Cannot load Windows drivers"},
    {"NtQueryObject (handle enum)", "—", "—", PairQuality::Blocked, "No Mach port enumeration; return fake data"},
};

constexpr BlockerDetail CRITICAL_BLOCKERS[] = {
    {"HANDLE_ENUM",
     "No API to enumerate another process's Mach ports/fds",
     {"EAC", "BattlEye", "Vanguard", nullptr},
     "Return fake handle data from Wine virtual handle table"},
    {"KERNEL_DRIVER",
     "Apple requires notarized kexts; anti-cheat drivers are proprietary",
     {"Vanguard", "EAC", "BattlEye", nullptr},
     "Vendor ships macOS system extension or EndpointSecurity companion"},
    {"THREAD_NOTIFY",
     "No per-thread creation callback on XNU",
     {"Vanguard", nullptr, nullptr, nullptr},
     "EndpointSecurity could add this in future macOS"},
    {"HANDLE_CALLBACK",
     "No Mach port access callback (ObRegisterCallbacks equiv)",
     {"Vanguard", nullptr, nullptr, nullptr},
     "MACF mac_proc_check_get_task provides partial coverage"},
    {"CODE_INTEGRITY",
     "SIP prevents customizing code signing levels",
     {"Vanguard", "EAC", nullptr, nullptr},
     "Return fake signing results; csops for real queries"},
};

inline CoverageReport computeSyscallCoverage() {
    CoverageReport r = {};
    for (const auto& e : SYSCALL_TABLE) {
        r.total++;
        switch (e.quality) {
        case PairQuality::Direct:
            r.direct++;
            break;
        case PairQuality::Close:
            r.close++;
            break;
        case PairQuality::UserspaceEmulation:
            r.userspace++;
            break;
        case PairQuality::Partial:
            r.partial++;
            break;
        case PairQuality::Blocked:
            r.blocked++;
            break;
        case PairQuality::NotNeeded:
            break;
        }
    }
    return r;
}

} // namespace kernel_translation
} // namespace metalsharp
