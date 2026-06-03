#[derive(Debug, Clone, serde::Serialize)]
pub struct NtStatus(pub u32);

impl NtStatus {
    pub const SUCCESS: u32 = 0x00000000;
    pub const NOT_SUPPORTED: u32 = 0xC00000BB;
    pub const ACCESS_DENIED: u32 = 0xC0000022;
    pub const NOT_IMPLEMENTED: u32 = 0xC0000002;
    pub const INVALID_PARAMETER: u32 = 0xC000000D;
    pub const PORT_NOT_SET: u32 = 0xC0000353;
}

#[derive(Debug, Clone, Copy, serde::Serialize)]
pub enum PairQuality {
    Direct,
    Close,
    Userspace,
    Partial,
    Blocked,
    NotNeeded,
}

#[derive(Debug, serde::Serialize)]
pub struct SyscallMapping {
    pub nt_name: &'static str,
    pub xnu_name: &'static str,
    pub xnu_syscall: &'static str,
    pub quality: PairQuality,
    pub wine_impl: &'static str,
}

#[derive(Debug, serde::Serialize)]
pub struct StructMapping {
    pub nt_name: &'static str,
    pub xnu_name: &'static str,
    pub quality: PairQuality,
    pub notes: &'static str,
}

#[derive(Debug, serde::Serialize)]
pub struct CoverageReport {
    pub total: usize,
    pub direct: usize,
    pub close: usize,
    pub userspace: usize,
    pub partial: usize,
    pub blocked: usize,
}

#[derive(Debug, serde::Serialize)]
pub struct BlockerSummary {
    pub blocker_count: usize,
    pub blockers: Vec<BlockerDetail>,
}

#[derive(Debug, Clone, serde::Serialize)]
pub struct BlockerDetail {
    pub id: &'static str,
    pub description: &'static str,
    pub affected_anticheat: &'static [&'static str],
    pub workaround: &'static str,
}

pub const CRITICAL_BLOCKERS: &[BlockerDetail] = &[
    BlockerDetail {
        id: "HANDLE_ENUM",
        description: "No API to enumerate another process's Mach ports or file descriptors",
        affected_anticheat: &["EAC", "BattlEye", "Vanguard"],
        workaround: "Return fake handle data; maintain Wine virtual handle table",
    },
    BlockerDetail {
        id: "KERNEL_DRIVER",
        description: "Apple requires notarized kexts; anti-cheat drivers are proprietary Windows PE",
        affected_anticheat: &["Vanguard", "EAC", "BattlEye"],
        workaround: "Vendor must ship macOS system extension or MetalSharp ships EndpointSecurity companion",
    },
    BlockerDetail {
        id: "THREAD_NOTIFY",
        description: "No per-thread creation callback on XNU",
        affected_anticheat: &["Vanguard"],
        workaround: "EndpointSecurity could add this; currently no workaround",
    },
    BlockerDetail {
        id: "HANDLE_CALLBACK",
        description: "No Mach port access callback (ObRegisterCallbacks equivalent)",
        affected_anticheat: &["Vanguard"],
        workaround: "MACF mac_proc_check_get_task provides partial coverage",
    },
    BlockerDetail {
        id: "CODE_INTEGRITY",
        description: "SIP prevents customizing code signing levels (NtSetCachedSigningLevel)",
        affected_anticheat: &["Vanguard", "EAC"],
        workaround: "Return fake signing results from Wine; csops for real queries",
    },
];

pub fn syscall_table() -> Vec<SyscallMapping> {
    vec![
        SyscallMapping {
            nt_name: "NtCreateProcess",
            xnu_name: "fork + execve",
            xnu_syscall: "BSD 2, 59",
            quality: PairQuality::Direct,
            wine_impl: "fork() then execve()",
        },
        SyscallMapping {
            nt_name: "NtCreateUserProcess",
            xnu_name: "posix_spawn",
            xnu_syscall: "BSD 244",
            quality: PairQuality::Direct,
            wine_impl: "posix_spawn()",
        },
        SyscallMapping {
            nt_name: "NtCreateThread",
            xnu_name: "bsdthread_create",
            xnu_syscall: "BSD 360",
            quality: PairQuality::Direct,
            wine_impl: "bsdthread_create()",
        },
        SyscallMapping {
            nt_name: "NtTerminateProcess",
            xnu_name: "exit/kill",
            xnu_syscall: "BSD 1, 37",
            quality: PairQuality::Direct,
            wine_impl: "kill(pid, SIGKILL)",
        },
        SyscallMapping {
            nt_name: "NtSuspendProcess",
            xnu_name: "pid_suspend",
            xnu_syscall: "BSD 433",
            quality: PairQuality::Direct,
            wine_impl: "pid_suspend(pid)",
        },
        SyscallMapping {
            nt_name: "NtSuspendThread",
            xnu_name: "thread_suspend",
            xnu_syscall: "Mach IPC",
            quality: PairQuality::Direct,
            wine_impl: "thread_suspend(mach_thread)",
        },
        SyscallMapping {
            nt_name: "NtAllocateVirtualMemory",
            xnu_name: "mmap/mach_vm_allocate",
            xnu_syscall: "BSD 197, Mach 17",
            quality: PairQuality::Direct,
            wine_impl: "mmap()",
        },
        SyscallMapping {
            nt_name: "NtProtectVirtualMemory",
            xnu_name: "mprotect/mach_vm_protect",
            xnu_syscall: "BSD 74, Mach 19",
            quality: PairQuality::Direct,
            wine_impl: "mprotect()",
        },
        SyscallMapping {
            nt_name: "NtReadVirtualMemory",
            xnu_name: "mach_vm_read",
            xnu_syscall: "Mach IPC",
            quality: PairQuality::Direct,
            wine_impl: "mach_vm_read(task, addr, size)",
        },
        SyscallMapping {
            nt_name: "NtWriteVirtualMemory",
            xnu_name: "mach_vm_write",
            xnu_syscall: "Mach IPC",
            quality: PairQuality::Direct,
            wine_impl: "mach_vm_write(task, addr, data)",
        },
        SyscallMapping {
            nt_name: "NtGetContextThread",
            xnu_name: "thread_get_state",
            xnu_syscall: "Mach IPC",
            quality: PairQuality::Direct,
            wine_impl: "thread_get_state(ARM_THREAD_STATE64)",
        },
        SyscallMapping {
            nt_name: "NtSetContextThread",
            xnu_name: "thread_set_state",
            xnu_syscall: "Mach IPC",
            quality: PairQuality::Direct,
            wine_impl: "thread_set_state(ARM_THREAD_STATE64)",
        },
        SyscallMapping {
            nt_name: "NtCreateFile",
            xnu_name: "open/openat",
            xnu_syscall: "BSD 5, 463",
            quality: PairQuality::Direct,
            wine_impl: "openat()",
        },
        SyscallMapping {
            nt_name: "NtReadFile",
            xnu_name: "read/pread",
            xnu_syscall: "BSD 3, 153",
            quality: PairQuality::Direct,
            wine_impl: "pread()",
        },
        SyscallMapping {
            nt_name: "NtDeviceIoControlFile",
            xnu_name: "ioctl",
            xnu_syscall: "BSD 54",
            quality: PairQuality::Direct,
            wine_impl: "ioctl()",
        },
        SyscallMapping {
            nt_name: "NtCreateEvent",
            xnu_name: "ulock/kqueue",
            xnu_syscall: "BSD 515, 362",
            quality: PairQuality::Close,
            wine_impl: "ulock_wait/ulock_wake",
        },
        SyscallMapping {
            nt_name: "NtCreateMutant",
            xnu_name: "psynch_mutexwait",
            xnu_syscall: "BSD 301",
            quality: PairQuality::Close,
            wine_impl: "psynch_mutexwait/mutexdrop",
        },
        SyscallMapping {
            nt_name: "NtCreateSemaphore",
            xnu_name: "semaphore_create",
            xnu_syscall: "Mach IPC",
            quality: PairQuality::Direct,
            wine_impl: "semaphore_create()",
        },
        SyscallMapping {
            nt_name: "NtCreateTimer",
            xnu_name: "mk_timer_create",
            xnu_syscall: "Mach 45",
            quality: PairQuality::Direct,
            wine_impl: "mk_timer_create()",
        },
        SyscallMapping {
            nt_name: "NtWaitForSingleObject",
            xnu_name: "kevent",
            xnu_syscall: "BSD 363",
            quality: PairQuality::Direct,
            wine_impl: "kevent()",
        },
        SyscallMapping {
            nt_name: "NtCreatePort",
            xnu_name: "mach_port_allocate",
            xnu_syscall: "Mach 22",
            quality: PairQuality::Direct,
            wine_impl: "mach_port_allocate()",
        },
        SyscallMapping {
            nt_name: "NtConnectPort",
            xnu_name: "mach_msg",
            xnu_syscall: "Mach 6",
            quality: PairQuality::Direct,
            wine_impl: "mach_msg()",
        },
        SyscallMapping {
            nt_name: "NtOpenProcess",
            xnu_name: "proc_info + task_for_pid",
            xnu_syscall: "BSD 336, Mach 50",
            quality: PairQuality::Direct,
            wine_impl: "task_for_pid()",
        },
        SyscallMapping {
            nt_name: "NtQuerySystemInformation",
            xnu_name: "sysctl + proc_info",
            xnu_syscall: "BSD 202, 336",
            quality: PairQuality::Close,
            wine_impl: "sysctl() + proc_info()",
        },
        SyscallMapping {
            nt_name: "NtDebugActiveProcess",
            xnu_name: "ptrace(PT_ATTACH)",
            xnu_syscall: "BSD 26",
            quality: PairQuality::Direct,
            wine_impl: "ptrace(PT_ATTACH)",
        },
        SyscallMapping {
            nt_name: "NtGetCachedSigningLevel",
            xnu_name: "csops",
            xnu_syscall: "BSD 169",
            quality: PairQuality::Direct,
            wine_impl: "csops(CS_OPS_GETSIGNINGINFO)",
        },
        SyscallMapping {
            nt_name: "NtQueryVirtualMemory",
            xnu_name: "mach_vm_region",
            xnu_syscall: "Mach IPC",
            quality: PairQuality::Direct,
            wine_impl: "mach_vm_region()",
        },
        SyscallMapping {
            nt_name: "NtQueueApcThread",
            xnu_name: "thread_set_state (trampoline)",
            xnu_syscall: "Mach IPC",
            quality: PairQuality::Partial,
            wine_impl: "Suspend, modify ARM_CONTEXT pc, resume",
        },
        SyscallMapping {
            nt_name: "NtSetCachedSigningLevel",
            xnu_name: "—",
            xnu_syscall: "—",
            quality: PairQuality::Blocked,
            wine_impl: "SIP prevents; return ACCESS_DENIED",
        },
        SyscallMapping {
            nt_name: "NtLoadDriver",
            xnu_name: "kext_load",
            xnu_syscall: "IOKit",
            quality: PairQuality::Blocked,
            wine_impl: "Cannot load Windows drivers; return ACCESS_DENIED",
        },
        SyscallMapping {
            nt_name: "NtQueryObject (handle enum)",
            xnu_name: "—",
            xnu_syscall: "—",
            quality: PairQuality::Blocked,
            wine_impl: "No Mach port enumeration; return fake data",
        },
    ]
}

pub fn struct_table() -> Vec<StructMapping> {
    vec![
        StructMapping {
            nt_name: "EPROCESS",
            xnu_name: "task_t (osfmk/kern/task.h)",
            quality: PairQuality::Close,
            notes: "task→PID, task→vm_map, task->t_flags",
        },
        StructMapping {
            nt_name: "ETHREAD",
            xnu_name: "thread_t (osfmk/kern/thread.h)",
            quality: PairQuality::Close,
            notes: "thread->continuation, thread->state",
        },
        StructMapping {
            nt_name: "CONTEXT",
            xnu_name: "arm_thread_state64_t",
            quality: PairQuality::Direct,
            notes: "x0-x28→X0-X28, pc→PC, sp→SP, lr→LR",
        },
        StructMapping {
            nt_name: "EXCEPTION_RECORD",
            xnu_name: "mach_exception_data_t",
            quality: PairQuality::Close,
            notes: "EXC_BAD_ACCESS→ACCESS_VIOLATION",
        },
        StructMapping {
            nt_name: "MEMORY_BASIC_INFORMATION",
            xnu_name: "vm_region_submap_info_64_t",
            quality: PairQuality::Direct,
            notes: "From mach_vm_region_recurse",
        },
        StructMapping {
            nt_name: "SECURITY_DESCRIPTOR",
            xnu_name: "acl_t (POSIX ACL)",
            quality: PairQuality::Userspace,
            notes: "Map DACL→ACL entries, Owner SID→uid",
        },
        StructMapping {
            nt_name: "TOKEN",
            xnu_name: "uid_t + gid_t + groups",
            quality: PairQuality::Userspace,
            notes: "Wine wraps POSIX creds in fake TOKEN",
        },
        StructMapping {
            nt_name: "HANDLE",
            xnu_name: "int (fd) or mach_port_name_t",
            quality: PairQuality::Close,
            notes: "Dual namespace; Wine maintains separate handle table",
        },
        StructMapping {
            nt_name: "FILE_OBJECT",
            xnu_name: "vnode_t + fileproc_t",
            quality: PairQuality::Close,
            notes: "vnode for filesystem, fileproc for fd",
        },
        StructMapping {
            nt_name: "IRP",
            xnu_name: "—",
            quality: PairQuality::Blocked,
            notes: "No IRP equivalent; Wine implements IRP-like struct internally",
        },
        StructMapping {
            nt_name: "MDL",
            xnu_name: "IOMemoryDescriptor",
            quality: PairQuality::Blocked,
            notes: "IOKit only; not accessible from Wine userspace",
        },
    ]
}
