# Windows NT Kernel Complete API Reference
**Updated:** 2026-07-08

## Windows 10/11 (NT 10.0+) — For Wine Anti-Cheat Compatibility Mapping

> Architecture: x86-64 / ARM64
> Kernel: ntoskrnl.exe + hal.dll + win32k.sys + drivers
> Source: j00ru syscall tables, Microsoft WDK docs, Windows Internals

---

## Table of Contents
1. [NT Architecture Overview](#1-nt-architecture-overview)
2. [NT Native Syscalls (NtXxx)](#2-nt-native-syscalls)
3. [Kernel Executive Functions](#3-kernel-executive-functions)
4. [Core Kernel Library (KeXxx)](#4-core-kernel-library)
5. [Memory Manager (MmXxx)](#5-memory-manager)
6. [Process/Thread Manager (PsXxx)](#6-processthread-manager)
7. [I/O Manager (IoXxx)](#7-io-manager)
8. [Object Manager (ObXxx)](#8-object-manager)
9. [Executive Library (ExXxx)](#9-executive-library)
10. [Run-Time Library (RtlXxx)](#10-run-time-library)
11. [Security Reference Monitor (SeXxx)](#11-security-reference-monitor)
12. [Configuration Manager (CmXxx)](#12-configuration-manager)
13. [Power Manager (PoXxx)](#13-power-manager)
14. [HAL Library (HalXxx)](#14-hal-library)
15. [KTM Routines](#15-ktm-routines)
16. [DMA Library](#16-dma-library)
17. [Win32k Syscalls (GDI/Windowing)](#17-win32k-syscalls)
18. [Kernel Object Types](#18-kernel-object-types)
19. [Key Structures](#19-key-structures)
20. [Anti-Cheat Relevance Matrix](#20-anti-cheat-relevance-matrix)

---

## 1. NT Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    User Mode                                 │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
│  │ Win32 App │  │  Wine     │  │  NT App  │  │ POSIX/WSL│   │
│  └─────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘   │
────────│ Win32API     │ NtXxx       │ NtXxx       │───────────
│        └──────┬───────┴────────────┴────────────┘           │
│               │ NtXxx / ZwXxx syscalls                       │
├───────────────┼─────────────────────────────────────────────┤
│               │                NT Kernel                     │
│  ┌────────────┼──────────────────────────────────────────┐  │
│  │        Executive Services                             │  │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌──────────┐   │  │
│  │  │ I/O Mgr │ │  Mgr    │ │  Proc   │ │ Object   │   │  │
│  │  │         │ │  Mem    │ │ Thread  │ │ Manager  │   │  │
│  │  └─────────┘ └─────────┘ └─────────┘ └──────────┘   │  │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌──────────┐   │  │
│  │  │ Config  │ │ Security│ │  KTM    │ │   LPC    │   │  │
│  │  │  Mgr    │ │ Ref Mon │ │         │ │   ALPC   │   │  │
│  │  └─────────┘ └─────────┘ └─────────┘ └──────────┘   │  │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐               │  │
│  │  │  PnP    │ │ Power   │ │  WMI    │               │  │
│  │  │  Mgr    │ │  Mgr    │ │         │               │  │
│  │  └─────────┘ └─────────┘ └─────────┘               │  │
│  ├───────────────────────────────────────────────────────┤  │
│  │              Microkernel                              │  │
│  │  Thread scheduling · Interrupts · DPC/APC · Sync     │  │
│  ├───────────────────────────────────────────────────────┤  │
│  │              HAL (hal.dll)                            │  │
│  │  Interrupt · DMA · Timer · Bus · Port I/O            │  │
│  ├───────────────────────────────────────────────────────┤  │
│  │              Win32k (win32k.sys)                      │  │
│  │  Window manager · GDI · DirectX kernel               │  │
│  └───────────────────────────────────────────────────────┘  │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐                  │
│  │ Drivers  │  │  KMDF    │  │  UMDF    │                  │
│  │  (WDM)   │  │ Framework│  │ Framework│                  │
│  └──────────┘  └──────────┘  └──────────┘                  │
└─────────────────────────────────────────────────────────────┘
```

### Key Components

| Component | Binary | Purpose |
|-----------|--------|---------|
| ntoskrnl.exe | Kernel core | Executive + kernel + drivers |
| hal.dll | HAL | Hardware abstraction layer |
| win32k.sys | Windowing | GDI, window manager, input |
| kdcom.dll | KD | Kernel debugger transport |
| bootvid.dll | Boot | Boot video driver |
| pshed.dll | Platform | Platform-specific hardware error |
| clfs.sys | CLFS | Common Log File System |
| fltmgr.sys | Filter mgr | Filesystem filter manager |

---

## 2. NT Native Syscalls (NtXxx)

Full syscall table from j00ru's research. ~500 syscalls across Windows versions.

### 2.1 Process/Thread Lifecycle

| Syscall | Purpose | macOS XNU Equivalent |
|---------|---------|---------------------|
| NtCreateProcess | Create process | fork + execve (2, 59) |
| NtCreateProcessEx | Create process (extended) | posix_spawn (244) |
| NtCreateUserProcess | Create user process | posix_spawn |
| NtCreateProcessStateChange | Create state change object | pid_suspend (433) |
| NtCreateThread | Create thread | bsdthread_create (360) |
| NtCreateThreadEx | Create thread (extended) | bsdthread_create (360) |
| NtCreateThreadStateChange | Create thread state change | — |
| NtTerminateProcess | Terminate process | exit (1) / kill (37) |
| NtTerminateThread | Terminate thread | thread_terminate (mach) |
| NtTerminateEnclave | Terminate enclave | — |
| NtTerminateJobObject | Terminate job | kill (37) |
| NtSuspendProcess | Suspend process | pid_suspend (433) |
| NtSuspendThread | Suspend thread | thread_suspend (mach) |
| NtResumeProcess | Resume process | pid_resume (434) |
| NtResumeThread | Resume thread | thread_resume (mach) |
| NtAlertThread | Alert thread | — |
| NtAlertResumeThread | Alert + resume | — |
| NtAlertThreadByThreadId | Alert by TID | — |
| NtAlertThreadByThreadIdEx | Alert by TID (extended) | — |
| NtAlertMultipleThreadByThreadId | Alert multiple | — |
| NtTestAlert | Test alert state | — |
| NtYieldExecution | Yield timeslice | swtch (mach trap 41) |
| NtDelayExecution | Sleep | nanosleep / clock_sleep |
| NtContinue | Continue after exception | sigreturn (184) |
| NtContinueEx | Continue (extended) | — |
| NtQueueApcThread | Queue APC | — |
| NtQueueApcThreadEx | Queue APC (extended) | — |
| NtQueueApcThreadEx2 | Queue APC (v2) | — |
| NtGetContextThread | Get thread context | thread_get_state (mach) |
| NtSetContextThread | Set thread context | thread_set_state (mach) |

### 2.2 Virtual Memory

| Syscall | Purpose | macOS XNU Equivalent |
|---------|---------|---------------------|
| NtAllocateVirtualMemory | Allocate VM | mmap (197) / mach_vm_allocate |
| NtAllocateVirtualMemoryEx | Allocate VM (extended) | mmap |
| NtFreeVirtualMemory | Free VM | munmap (73) / mach_vm_deallocate |
| NtProtectVirtualMemory | Change protection | mprotect (74) / mach_vm_protect |
| NtQueryVirtualMemory | Query VM info | mach_vm_region |
| NtLockVirtualMemory | Lock VM | mlock (203) |
| NtUnlockVirtualMemory | Unlock VM | munlock (204) |
| NtFlushVirtualMemory | Flush VM | msync (65) |
| NtReadVirtualMemory | Read process memory | mach_vm_read |
| NtReadVirtualMemoryEx | Read (extended) | mach_vm_read |
| NtWriteVirtualMemory | Write process memory | mach_vm_write |
| NtAllocateUserPhysicalPages | Allocate physical pages | — |
| NtAllocateUserPhysicalPagesEx | Allocate physical (ext) | — |
| NtFreeUserPhysicalPages | Free physical pages | — |
| NtMapUserPhysicalPages | Map physical pages | — |
| NtMapUserPhysicalPagesScatter | Scatter-map physical | — |
| NtGetWriteWatch | Get write-watch pages | mincore (78) |
| NtResetWriteWatch | Reset write-watch | — |
| NtCreateSection | Create section (shared mem) | shm_open (266) + mmap |
| NtCreateSectionEx | Create section (extended) | — |
| NtOpenSection | Open section | shm_open |
| NtExtendSection | Extend section | — |
| NtMapViewOfSection | Map view of section | mmap (197) |
| NtMapViewOfSectionEx | Map view (extended) | — |
| NtUnmapViewOfSection | Unmap view | munmap (73) |
| NtUnmapViewOfSectionEx | Unmap (extended) | — |
| NtAreMappedFilesTheSame | Check mapped file identity | — |
| NtSetInformationVirtualMemory | Set VM info | madvise (75) |

### 2.3 File I/O

| Syscall | Purpose | macOS XNU Equivalent |
|---------|---------|---------------------|
| NtCreateFile | Create/open file | open (5) / openat (463) |
| NtOpenFile | Open file | open |
| NtReadFile | Read file | read (3) / pread (153) |
| NtReadFileScatter | Scatter read | readv (120) |
| NtWriteFile | Write file | write (4) / pwrite (154) |
| NtWriteFileGather | Gather write | writev (121) |
| NtClose | Close handle | close (6) |
| NtDeleteFile | Delete file | unlink (10) |
| NtFlushBuffersFile | Flush buffers | fsync (95) |
| NtFlushBuffersFileEx | Flush (extended) | fdatasync (187) |
| NtDeviceIoControlFile | Device I/O control | ioctl (54) |
| NtFsControlFile | Filesystem control | fsctl (242) |
| NtQueryInformationFile | Query file info | fstat (189) |
| NtSetInformationFile | Set file info | — |
| NtQueryAttributesFile | Query file attributes | stat (188) |
| NtQueryFullAttributesFile | Query full attributes | stat |
| NtQueryDirectoryFile | Enumerate directory | getdirentries (196) |
| NtQueryDirectoryFileEx | Enumerate (extended) | getdirentries64 (344) |
| NtQueryEaFile | Query extended attributes | getxattr (234) |
| NtSetEaFile | Set extended attributes | setxattr (236) |
| NtQueryVolumeInformationFile | Query volume info | statfs (157) |
| NtSetVolumeInformationFile | Set volume info | — |
| NtQueryQuotaInformationFile | Query quota | quotactl (165) |
| NtSetQuotaInformationFile | Set quota | quotactl |
| NtNotifyChangeDirectoryFile | Directory change notify | kqueue (362) |
| NtNotifyChangeDirectoryFileEx | Dir change (extended) | — |
| NtLockFile | Lock file | flock (131) |
| NtUnlockFile | Unlock file | flock |
| NtCancelIoFile | Cancel I/O | — |
| NtCancelIoFileEx | Cancel I/O (extended) | — |
| NtCancelSynchronousIoFile | Cancel sync I/O | — |
| NtCopyFileChunk | Copy file chunk | sendfile (337) / copyfile (227) |

### 2.4 Synchronization

| Syscall | Purpose | macOS XNU Equivalent |
|---------|---------|---------------------|
| NtCreateEvent | Create event | kevent (363) |
| NtCreateEventPair | Create event pair | — |
| NtCreateMutant | Create mutex | psynch_mutex (301/302) |
| NtCreateSemaphore | Create semaphore | sem_open (268) / semaphore_create |
| NtCreateTimer | Create timer | mk_timer_create (mach) |
| NtCreateTimer2 | Create timer (v2) | mk_timer_create |
| NtCreateIRTimer | Create IR timer | — |
| NtCreateKeyedEvent | Create keyed event | ulock (515/516) |
| NtCreateWaitablePort | Create waitable port | — |
| NtCreateIoCompletion | Create I/O completion | kqueue (362) |
| NtCreateWaitCompletionPacket | Create wait completion | — |
| NtOpenEvent | Open event | — |
| NtOpenEventPair | Open event pair | — |
| NtOpenMutant | Open mutex | — |
| NtOpenSemaphore | Open semaphore | sem_open |
| NtOpenTimer | Open timer | — |
| NtOpenKeyedEvent | Open keyed event | — |
| NtOpenIoCompletion | Open I/O completion | — |
| NtSetEvent | Set event | kevent |
| NtSetEventBoostPriority | Set event + boost | — |
| NtSetEventEx | Set event (extended) | — |
| NtResetEvent | Reset event | — |
| NtClearEvent | Clear event | — |
| NtPulseEvent | Pulse event | — |
| NtReleaseMutant | Release mutex | psynch_mutexdrop (302) |
| NtReleaseSemaphore | Release semaphore | sem_post (273) |
| NtCancelTimer | Cancel timer | mk_timer_cancel |
| NtCancelTimer2 | Cancel timer (v2) | — |
| NtSetTimer | Set timer | mk_timer_arm |
| NtSetTimer2 | Set timer (v2) | mk_timer_arm_leeway |
| NtSetTimerEx | Set timer (extended) | — |
| NtSetIRTimer | Set IR timer | — |
| NtWaitForSingleObject | Wait on object | kevent (363) |
| NtWaitForMultipleObjects | Wait on multiple | kevent |
| NtWaitForMultipleObjects32 | Wait (32-bit) | — |
| NtSignalAndWaitForSingleObject | Signal + wait | semaphore_wait_signal |
| NtWaitForKeyedEvent | Wait on keyed event | ulock_wait (515) |
| NtReleaseKeyedEvent | Release keyed event | ulock_wake (516) |
| NtWaitForAlertByThreadId | Wait for alert | — |
| NtSetHighEventPair | Set high event pair | — |
| NtSetLowEventPair | Set low event pair | — |
| NtWaitHighEventPair | Wait high | — |
| NtWaitLowEventPair | Wait low | — |
| NtSetHighWaitLowEventPair | Set high wait low | — |
| NtSetLowWaitHighEventPair | Set low wait high | — |

### 2.5 IPC / Ports (LPC/ALPC)

| Syscall | Purpose | macOS XNU Equivalent |
|---------|---------|---------------------|
| NtCreatePort | Create LPC port | mach_port_allocate |
| NtCreateWaitablePort | Create waitable port | mach_port_allocate |
| NtConnectPort | Connect to port | mach_msg |
| NtSecureConnectPort | Secure connect | mach_msg |
| NtListenPort | Listen on port | mach_msg (receive) |
| NtAcceptConnectPort | Accept connection | mach_msg |
| NtCompleteConnectPort | Complete connection | — |
| NtRequestPort | Send request | mach_msg (send) |
| NtRequestWaitReplyPort | Request + wait reply | mach_msg (send+receive) |
| NtReplyPort | Reply | mach_msg (send) |
| NtReplyWaitReceivePort | Reply + wait receive | mach_msg |
| NtReplyWaitReceivePortEx | Reply + wait (extended) | — |
| NtReplyWaitReplyPort | Reply + wait reply | — |
| NtReadRequestData | Read request data | — |
| NtWriteRequestData | Write request data | — |
| NtImpersonateClientOfPort | Impersonate client | — |
| NtAlpcCreatePort | ALPC create port | mach_port_allocate |
| NtAlpcConnectPort | ALPC connect | mach_msg |
| NtAlpcConnectPortEx | ALPC connect (extended) | — |
| NtAlpcAcceptConnectPort | ALPC accept | — |
| NtAlpcDisconnectPort | ALPC disconnect | — |
| NtAlpcSendWaitReceivePort | ALPC send/wait/recv | mach_msg |
| NtAlpcCreatePortSection | ALPC port section | — |
| NtAlpcCreateSectionView | ALPC section view | — |
| NtAlpcCreateResourceReserve | ALPC resource reserve | — |
| NtAlpcCreateSecurityContext | ALPC security context | — |
| NtAlpcDeletePortSection | ALPC delete section | — |
| NtAlpcDeleteSectionView | ALPC delete view | — |
| NtAlpcDeleteResourceReserve | ALPC delete reserve | — |
| NtAlpcDeleteSecurityContext | ALPC delete security | — |
| NtAlpcImpersonateClientOfPort | ALPC impersonate | — |
| NtAlpcImpersonateClientContainerOfPort | ALPC container imp | — |
| NtAlpcOpenSenderProcess | ALPC open sender proc | — |
| NtAlpcOpenSenderThread | ALPC open sender thread | — |
| NtAlpcQueryInformation | ALPC query info | — |
| NtAlpcQueryInformationMessage | ALPC query message | — |
| NtAlpcRevokeSecurityContext | ALPC revoke security | — |
| NtAlpcSetInformation | ALPC set info | — |
| NtAlpcCancelMessage | ALPC cancel message | — |
| NtRegisterThreadTerminatePort | Register term port | — |
| NtAssociateWaitCompletionPacket | Associate wait comp | — |
| NtCancelWaitCompletionPacket | Cancel wait comp | — |

### 2.6 Security / Token

| Syscall | Purpose | macOS XNU Equivalent |
|---------|---------|---------------------|
| NtAccessCheck | Access check | mac_vnode_check_access |
| NtAccessCheckAndAuditAlarm | Access check + audit | mac_audit_check_preselect |
| NtAccessCheckByType | Access check by type | — |
| NtAccessCheckByTypeAndAuditAlarm | AC by type + audit | — |
| NtAccessCheckByTypeResultList | AC result list | — |
| NtAccessCheckByTypeResultListAndAuditAlarm | AC result + audit | — |
| NtAccessCheckByTypeResultListAndAuditAlarmByHandle | AC by handle | — |
| NtCreateToken | Create token | — |
| NtCreateTokenEx | Create token (extended) | — |
| NtCreateLowBoxToken | Create lowbox token | — |
| NtDuplicateToken | Duplicate token | — |
| NtOpenProcessToken | Open process token | — |
| NtOpenProcessTokenEx | Open process token (ext) | — |
| NtOpenThreadToken | Open thread token | — |
| NtOpenThreadTokenEx | Open thread token (ext) | — |
| NtAdjustGroupsToken | Adjust token groups | setgroups (80) |
| NtAdjustPrivilegesToken | Adjust token privileges | — |
| NtAdjustTokenClaimsAndDeviceGroups | Adjust claims | — |
| NtCompareTokens | Compare tokens | — |
| NtFilterToken | Filter token | — |
| NtFilterTokenEx | Filter token (extended) | — |
| NtImpersonateAnonymousToken | Impersonate anonymous | — |
| NtImpersonateThread | Impersonate thread | — |
| NtPrivilegeCheck | Privilege check | mac_priv_check |
| NtPrivilegeObjectAuditAlarm | Privilege audit | — |
| NtPrivilegedServiceAuditAlarm | Service audit | — |
| NtQueryInformationToken | Query token info | — |
| NtSetInformationToken | Set token info | — |
| NtQuerySecurityAttributesToken | Query sec attrs | — |
| NtQuerySecurityObject | Query security | — |
| NtSetSecurityObject | Set security | — |
| NtQuerySecurityPolicy | Query security policy | — |
| NtCloseObjectAuditAlarm | Close audit alarm | — |
| NtDeleteObjectAuditAlarm | Delete audit alarm | — |
| NtOpenObjectAuditAlarm | Open audit alarm | — |
| NtPrivilegedServiceAuditAlarm | Privilege audit | — |
| NtRevertContainerImpersonation | Revert impersonation | — |

### 2.7 Process / Job / System Info

| Syscall | Purpose | macOS XNU Equivalent |
|---------|---------|---------------------|
| NtOpenProcess | Open process | proc_info (336) + task_for_pid |
| NtOpenThread | Open thread | thread_act (mach) |
| NtQueryInformationProcess | Query process info | proc_info (336) |
| NtSetInformationProcess | Set process info | process_policy (323) |
| NtQueryInformationThread | Query thread info | thread_info (mach) |
| NtSetInformationThread | Set thread info | thread_policy_set (mach) |
| NtQuerySystemInformation | Query system info | sysctl (202) |
| NtQuerySystemInformationEx | Query system (extended) | sysctl |
| NtSetSystemInformation | Set system info | sysctl (write) |
| NtCreateJobObject | Create job object | coalition (458) |
| NtOpenJobObject | Open job | — |
| NtAssignProcessToJobObject | Assign process to job | coalition |
| NtQueryInformationJobObject | Query job info | coalition_info (459) |
| NtSetInformationJobObject | Set job info | — |
| NtIsProcessInJob | Check if in job | — |
| NtCreateDebugObject | Create debug object | — |
| NtDebugActiveProcess | Debug process | ptrace (26) |
| NtDebugContinue | Continue debugging | ptrace (PT_CONTINUE) |
| NtRemoveProcessDebug | Remove debug | — |
| NtWaitForDebugEvent | Wait for debug event | — |
| NtSetInformationDebugObject | Set debug info | — |
| NtGetNextProcess | Enumerate processes | proc_info (336) |
| NtGetNextThread | Enumerate threads | task_threads (mach) |
| NtSystemDebugControl | System debug control | — |
| NtGetCurrentProcessorNumber | Current CPU | — |
| NtGetCurrentProcessorNumberEx | Current CPU (ext) | — |

### 2.8 Registry

| Syscall | Purpose | macOS XNU Equivalent |
|---------|---------|---------------------|
| NtCreateKey | Create registry key | — (no registry on macOS) |
| NtCreateKeyTransacted | Create key (transacted) | — |
| NtOpenKey | Open registry key | — |
| NtOpenKeyEx | Open key (extended) | — |
| NtOpenKeyTransacted | Open key (transacted) | — |
| NtOpenKeyTransactedEx | Open key (transacted ext) | — |
| NtDeleteKey | Delete key | — |
| NtDeleteValueKey | Delete value | — |
| NtEnumerateKey | Enumerate subkeys | — |
| NtEnumerateValueKey | Enumerate values | — |
| NtQueryKey | Query key info | — |
| NtQueryValueKey | Query value | — |
| NtQueryMultipleValueKey | Query multiple values | — |
| NtSetValueKey | Set value | — |
| NtFlushKey | Flush key | — |
| NtSaveKey | Save key to file | — |
| NtSaveKeyEx | Save key (extended) | — |
| NtSaveMergedKeys | Save merged keys | — |
| NtRestoreKey | Restore key from file | — |
| NtReplaceKey | Replace key | — |
| NtRenameKey | Rename key | — |
| NtLoadKey | Load hive | — |
| NtLoadKey2 | Load hive (v2) | — |
| NtLoadKey3 | Load hive (v3) | — |
| NtLoadKeyEx | Load hive (extended) | — |
| NtUnloadKey | Unload hive | — |
| NtUnloadKey2 | Unload hive (v2) | — |
| NtUnloadKeyEx | Unload hive (extended) | — |
| NtNotifyChangeKey | Notify key change | — |
| NtNotifyChangeMultipleKeys | Notify multiple | — |
| NtNotifyChangeSession | Notify session | — |
| NtCompactKeys | Compact keys | — |
| NtCompressKey | Compress key | — |
| NtCreateRegistryTransaction | Create reg transaction | — |
| NtOpenRegistryTransaction | Open reg transaction | — |
| NtCommitRegistryTransaction | Commit reg transaction | — |
| NtRollbackRegistryTransaction | Rollback reg transaction | — |
| NtFreezeRegistry | Freeze registry | — |
| NtThawRegistry | Thaw registry | — |

### 2.9 Object Manager

| Syscall | Purpose | macOS XNU Equivalent |
|---------|---------|---------------------|
| NtCreateDirectoryObject | Create directory obj | — |
| NtCreateDirectoryObjectEx | Create dir obj (ext) | — |
| NtOpenDirectoryObject | Open directory obj | — |
| NtCreateSymbolicLinkObject | Create symlink obj | symlink (57) |
| NtOpenSymbolicLinkObject | Open symlink obj | readlink (58) |
| NtQuerySymbolicLinkObject | Query symlink | readlink |
| NtCreatePrivateNamespace | Create private namespace | — |
| NtOpenPrivateNamespace | Open private namespace | — |
| NtDeletePrivateNamespace | Delete private namespace | — |
| NtQueryObject | Query object info | — |
| NtSetInformationObject | Set object info | — |
| NtDuplicateObject | Duplicate handle | dup (41) / dup2 (90) |
| NtMakePermanentObject | Make permanent | — |
| NtMakeTemporaryObject | Make temporary | — |
| NtCompareObjects | Compare objects | — |

### 2.10 Driver / Module

| Syscall | Purpose | macOS XNU Equivalent |
|---------|---------|---------------------|
| NtLoadDriver | Load driver | kext_load |
| NtUnloadDriver | Unload driver | kext_unload |
| NtAddBootEntry | Add boot entry | — |
| NtDeleteBootEntry | Delete boot entry | — |
| NtModifyBootEntry | Modify boot entry | — |
| NtEnumerateBootEntries | Enumerate boot entries | — |
| NtQueryBootEntryOrder | Query boot order | — |
| NtSetBootEntryOrder | Set boot order | — |
| NtQueryBootOptions | Query boot options | — |
| NtSetBootOptions | Set boot options | — |
| NtAddDriverEntry | Add driver entry | — |
| NtDeleteDriverEntry | Delete driver entry | — |
| NtModifyDriverEntry | Modify driver entry | — |
| NtEnumerateDriverEntries | Enumerate drivers | — |
| NtQueryDriverEntryOrder | Query driver order | — |
| NtSetDriverEntryOrder | Set driver order | — |
| NtFilterBootOption | Filter boot options | — |
| NtLoadHotPatch | Load hot patch | — |
| NtManageHotPatch | Manage hot patch | — |
| NtLoadEnclaveData | Load enclave data | — |
| NtInitializeEnclave | Initialize enclave | — |
| NtCreateEnclave | Create enclave | — |
| NtCallEnclave | Call enclave | — |
| NtTerminateEnclave | Terminate enclave | — |

### 2.11 Transaction Manager (KTM)

| Syscall | Purpose |
|---------|---------|
| NtCreateTransactionManager | Create TM |
| NtOpenTransactionManager | Open TM |
| NtCreateTransaction | Create transaction |
| NtOpenTransaction | Open transaction |
| NtCreateEnlistment | Create enlistment |
| NtOpenEnlistment | Open enlistment |
| NtCreateResourceManager | Create resource manager |
| NtOpenResourceManager | Open resource manager |
| NtCommitTransaction | Commit transaction |
| NtRollbackTransaction | Rollback transaction |
| NtQueryInformationTransaction | Query transaction |
| NtSetInformationTransaction | Set transaction info |
| NtQueryInformationTransactionManager | Query TM info |
| NtEnumerateTransactionObject | Enumerate txn objects |
| NtRecoverTransactionManager | Recover TM |
| NtRollforwardTransactionManager | Rollforward TM |
| NtRenameTransactionManager | Rename TM |
| NtCommitComplete | Commit complete |
| NtRollbackComplete | Rollback complete |
| NtPrepareComplete | Prepare complete |
| NtPrePrepareComplete | Pre-prepare complete |
| NtCommitEnlistment | Commit enlistment |
| NtPrePrepareEnlistment | Pre-prepare enlistment |
| NtPrepareEnlistment | Prepare enlistment |
| NtReadOnlyEnlistment | Read-only enlistment |
| NtRollbackEnlistment | Rollback enlistment |
| NtRecoverEnlistment | Recover enlistment |
| NtSinglePhaseReject | Single phase reject |
| NtGetNotificationResourceManager | Get RM notification |
| NtMarshallTransaction | Marshall transaction |
| NtPullTransaction | Pull transaction |
| NtPropagationComplete | Propagation complete |
| NtPropagationFailed | Propagation failed |
| NtSavepointTransaction | Savepoint |
| NtSavepointComplete | Savepoint complete |
| NtClearSavepointTransaction | Clear savepoint |
| NtClearAllSavepointsTransaction | Clear all savepoints |
| NtRollbackSavepointTransaction | Rollback savepoint |
| NtFreezeTransactions | Freeze transactions |
| NtThawTransactions | Thaw transactions |
| NtListTransactions | List transactions |

### 2.12 Misc System

| Syscall | Purpose | macOS XNU Equivalent |
|---------|---------|---------|
| NtQuerySystemTime | Get system time | gettimeofday (116) |
| NtSetSystemTime | Set system time | settimeofday (122) |
| NtQueryTimerResolution | Query timer res | — |
| NtSetTimerResolution | Set timer res | — |
| NtQueryPerformanceCounter | Performance counter | mach_absolute_time |
| NtQueryInstallUILanguage | Query UI language | — |
| NtSetDefaultUILanguage | Set UI language | — |
| NtQueryDefaultLocale | Query default locale | — |
| NtSetDefaultLocale | Set default locale | — |
| NtSetDefaultHardErrorPort | Set hard error port | — |
| NtRaiseException | Raise exception | — |
| NtRaiseHardError | Raise hard error | panic_with_data (185) |
| NtDisplayString | Display string | — |
| NtDrawText | Draw text | — |
| NtShutdownSystem | Shutdown system | reboot (55) |
| NtSetSystemPowerState | Set power state | — |
| NtPowerInformation | Power info | — |
| NtInitiatePowerAction | Initiate power action | — |
| NtIsSystemResumeAutomatic | Check system resume | — |
| NtGetDevicePowerState | Get device power | — |
| NtRequestDeviceWakeup | Request wakeup | — |
| NtRequestWakeupLatency | Request wakeup latency | — |
| NtCancelDeviceWakeupRequest | Cancel wakeup | — |
| NtSetThreadExecutionState | Set execution state | — |
| NtPlugPlayControl | PnP control | IOKit matching |
| NtGetPlugPlayEvent | Get PnP event | — |
| NtEnableLastKnownGood | Enable LKG | — |
| NtDisableLastKnownGood | Disable LKG | — |
| NtSerializeBoot | Serialize boot | — |
| NtInitializeRegistry | Initialize registry | — |
| NtInitializeNlsFiles | Initialize NLS | — |
| NtGetNlsSectionPtr | Get NLS section | — |
| NtFlushInstallUILanguage | Flush UI language | — |
| NtIsUILanguageComitted | Check UI language | — |
| NtQueryLicenseValue | Query license value | — |
| NtApphelpCacheControl | AppHelp cache | — |
| NtTranslateFilePath | Translate file path | — |
| NtVdmControl | VDM control | — |
| NtCallbackReturn | Callback return | — |
| NtCreatePagingFile | Create paging file | macx_swapon |
| NtCreateProfile | Create profile | — |
| NtStartProfile | Start profile | — |
| NtStopProfile | Stop profile | — |
| NtSetIntervalProfile | Set profile interval | — |
| NtQueryIntervalProfile | Query profile interval | — |
| NtTraceEvent | Trace event | kdebug_trace (179/180) |
| NtTraceControl | Trace control | kdebug_typefilter (177) |
| NtSetUuidSeed | Set UUID seed | — |
| NtAllocateUuids | Allocate UUIDs | — |
| NtAllocateLocallyUniqueId | Allocate LUID | — |
| NtAllocateReserveObject | Allocate reserve obj | — |
| NtQueryDebugFilterState | Query debug filter | — |
| NtSetDebugFilterState | Set debug filter | — |
| NtDisplayString | Display string | — |
| NtCreateWorkerFactory | Create worker factory | workq_open (367) |
| NtReleaseWorkerFactoryWorker | Release factory worker | workq_kernreturn (368) |
| NtWaitForWorkViaWorkerFactory | Wait for factory work | workq_kernreturn |
| NtShutdownWorkerFactory | Shutdown factory | — |
| NtQueryInformationWorkerFactory | Query factory info | — |
| NtSetInformationWorkerFactory | Set factory info | — |
| NtCreateIoRing | Create I/O ring | — |
| NtSubmitIoRing | Submit I/O ring | — |
| NtQueryIoRingCapabilities | Query I/O ring caps | — |
| NtSetInformationIoRing | Set I/O ring info | — |
| NtCreateCpuPartition | Create CPU partition | — |
| NtOpenCpuPartition | Open CPU partition | — |
| NtQueryInformationCpuPartition | Query CPU partition | — |
| NtSetInformationCpuPartition | Set CPU partition | — |
| NtManagePartition | Manage partition | — |
| NtCreatePartition | Create partition | — |
| NtOpenPartition | Open partition | — |
| NtReplacePartitionUnit | Replace partition | — |
| NtChangeProcessState | Change process state | — |
| NtChangeThreadState | Change thread state | — |
| NtConvertBetweenAuxiliaryCounterAndPerformanceCounter | Counter conversion | — |
| NtQueryAuxiliaryCounterFrequency | Query aux counter | — |
| NtGetCachedSigningLevel | Get cached signing | csops (169) |
| NtSetCachedSigningLevel | Set cached signing | csops |
| NtSetCachedSigningLevel2 | Set signing (v2) | — |
| NtCompareSigningLevels | Compare signing levels | — |
| NtSetInformationSymbolicLink | Set symlink info | — |
| NtCreateWnfStateName | Create WNF state | — |
| NtDeleteWnfStateData | Delete WNF data | — |
| NtDeleteWnfStateName | Delete WNF name | — |
| NtQueryWnfStateData | Query WNF data | — |
| NtQueryWnfStateNameInformation | Query WNF name info | — |
| NtSetWnfProcessNotificationEvent | Set WNF event | — |
| NtSubscribeWnfStateChange | Subscribe WNF | — |
| NtUnsubscribeWnfStateChange | Unsubscribe WNF | — |
| NtUpdateWnfStateData | Update WNF data | — |
| NtGetCompleteWnfStateSubscription | Get complete WNF sub | — |
| NtWaitForWnfNotifications | Wait for WNF | — |
| NtAcquireCMFViewOwnership | Acquire CMF view | — |
| NtReleaseCMFViewOwnership | Release CMF view | — |
| NtMapCMFModule | Map CMF module | — |
| NtRegisterProtocolAddressInformation | Register protocol addr | — |
| NtCreateCrossVmEvent | Create cross-VM event | — |
| NtCreateCrossVmMutant | Create cross-VM mutex | — |
| NtAcquireCrossVmMutant | Acquire cross-VM mutex | — |
| NtAcquireProcessActivityReference | Acquire process ref | — |
| NtDirectGraphicsCall | Direct graphics call | — |
| NtPssCaptureVaSpaceBulk | PSS capture VA | — |

---

## 3. Kernel Executive Functions

Functions exported by ntoskrnl.exe for kernel-mode drivers. Organized by subsystem.

### 3.1 Memory Manager (MmXxx) — 85 functions

| Function | Purpose |
|----------|---------|
| MmAddPhysicalMemory | Add physical memory |
| MmAddPhysicalMemoryEx | Add physical memory (ext) |
| MmAddVerifierThunks | Add verifier thunks |
| MmAdvanceMdl | Advance MDL |
| MmAllocateContiguousMemory | Allocate contiguous memory |
| MmAllocateContiguousMemoryEx | Allocate contiguous (ext) |
| MmAllocateContiguousMemorySpecifyCache | Allocate contiguous with cache |
| MmAllocateContiguousMemorySpecifyCacheNode | Allocate contiguous NUMA |
| MmAllocateContiguousNodeMemory | Allocate contiguous NUMA |
| MmAllocateMappingAddress | Allocate mapping address |
| MmAllocateMappingAddressEx | Allocate mapping (ext) |
| MmAllocateMdlForIoSpace | Allocate MDL for I/O space |
| MmAllocateNodePagesForMdlEx | Allocate node pages for MDL |
| MmAllocateNonCachedMemory | Allocate non-cached memory |
| MmAllocatePagesForMdl | Allocate pages for MDL |
| MmAllocatePagesForMdlEx | Allocate pages for MDL (ext) |
| MmBuildMdlForNonPagedPool | Build MDL for nonpaged pool |
| MmCopyMemory | Copy memory |
| MmCreateMdl | Create MDL |
| MmCreateMirror | Create mirror |
| MmFreeContiguousMemory | Free contiguous memory |
| MmFreeContiguousMemorySpecifyCache | Free contiguous with cache |
| MmFreeMappingAddress | Free mapping address |
| MmFreeNonCachedMemory | Free non-cached memory |
| MmFreePagesFromMdl | Free pages from MDL |
| MmGetMdlBaseVa | Get MDL base VA |
| MmGetMdlByteCount | Get MDL byte count |
| MmGetMdlByteOffset | Get MDL byte offset |
| MmGetMdlPfnArray | Get MDL PFN array |
| MmGetMdlVirtualAddress | Get MDL virtual address |
| MmGetPhysicalAddress | Get physical address |
| MmGetPhysicalMemoryRanges | Get physical memory ranges |
| MmGetProcedureAddress | Get procedure address |
| MmGetSystemAddressForMdl | Get system address for MDL |
| MmGetSystemAddressForMdlSafe | Get system address (safe) |
| MmGetSystemRoutineAddress | Get system routine address |
| MmGetSystemRoutineAddressEx | Get system routine (ext) |
| MmGetVirtualForPhysical | Get VA for physical |
| MmInitializeMdl | Initialize MDL |
| MmIsAddressValid | Check address valid |
| MmIsDriverSuspectForVerifier | Check driver for verifier |
| MmIsDriverVerifying | Check driver verifying |
| MmIsDriverVerifyingByAddress | Check verifying by address |
| MmIsIoSpaceActive | Check I/O space active |
| MmIsThisAnNtAsSystem | Check if server |
| MmIsVerifierEnabled | Check verifier enabled |
| MmLockPagableCodeSection | Lock pagable code |
| MmLockPagableDataSection | Lock pagable data |
| MmLockPagableSectionByHandle | Lock pagable by handle |
| MmMapIoSpace | Map I/O space |
| MmMapIoSpaceEx | Map I/O space (ext) |
| MmMapLockedPages | Map locked pages |
| MmMapLockedPagesSpecifyCache | Map locked with cache |
| MmMapLockedPagesWithReservedMapping | Map with reserved mapping |
| MmMapMdl | Map MDL |
| MmMapMemoryDumpMdlEx | Map memory dump MDL |
| MmMapUserAddressesToPage | Map user addrs to page |
| MmMapVideoDisplay | Map video display |
| MmMapVideoDisplayEx | Map video display (ext) |
| MmMapViewInSessionSpace | Map view in session |
| MmMapViewInSystemSpace | Map view in system space |
| MmMarkPhysicalMemoryAsBad | Mark physical as bad |
| MmMarkPhysicalMemoryAsGood | Mark physical as good |
| MmPageEntireDriver | Page entire driver |
| MmPrepareMdlForReuse | Prepare MDL for reuse |
| MmProbeAndLockPages | Probe and lock pages |
| MmProbeAndLockProcessPages | Probe and lock process pages |
| MmProbeAndLockSelectedPages | Probe and lock selected |
| MmProtectDriverSection | Protect driver section |
| MmProtectMdlSystemAddress | Protect MDL sysaddr |
| MmQuerySystemSize | Query system size |
| MmRemovePhysicalMemory | Remove physical memory |
| MmRemovePhysicalMemoryEx | Remove physical (ext) |
| MmResetDriverPaging | Reset driver paging |
| MmRotatePhysicalView | Rotate physical view |
| MmSecureVirtualMemory | Secure virtual memory |
| MmSecureVirtualMemoryEx | Secure VM (ext) |
| MmSizeOfMdl | Size of MDL |
| MmUnlockPagableImageSection | Unlock pagable image |
| MmUnlockPages | Unlock pages |
| MmUnmapIoSpace | Unmap I/O space |
| MmUnmapLockedPages | Unmap locked pages |
| MmUnmapReservedMapping | Unmap reserved mapping |
| MmUnmapVideoDisplay | Unmap video display |
| MmUnmapViewInSessionSpace | Unmap view in session |
| MmUnmapViewInSystemSpace | Unmap view in system space |
| MmUnsecureVirtualMemory | Unsecure VM |

### 3.2 Process/Thread Manager (PsXxx) — 66 functions

| Function | Purpose |
|----------|---------|
| PsAllocateAffinityToken | Allocate affinity token |
| PsAllocSiloContextSlot | Allocate silo context slot |
| PsAttachSiloToCurrentThread | Attach silo to thread |
| PsCreateSiloContext | Create silo context |
| PsCreateSystemThread | Create system thread |
| PsDereferenceSiloContext | Dereference silo context |
| PsDetachSiloFromCurrentThread | Detach silo |
| PsFreeAffinityToken | Free affinity token |
| PsFreeSiloContextSlot | Free silo context slot |
| PsGetCurrentProcess | Get current process |
| PsGetCurrentProcessId | Get current PID |
| PsGetCurrentServerSilo | Get server silo |
| PsGetCurrentSilo | Get current silo |
| PsGetCurrentThread | Get current thread |
| PsGetCurrentThreadId | Get current TID |
| PsGetCurrentThreadTeb | Get current TEB |
| PsGetEffectiveServerSilo | Get effective server silo |
| PsGetHostSilo | Get host silo |
| PsGetJobServerSilo | Get job server silo |
| PsGetJobSilo | Get job silo |
| PsGetParentSilo | Get parent silo |
| PsGetPermanentSiloContext | Get permanent silo context |
| PsGetProcessCreateTimeQuadPart | Get process create time |
| PsGetProcessExitStatus | Get process exit status |
| PsGetProcessId | Get process ID |
| PsGetProcessStartKey | Get process start key |
| PsGetServerSiloActiveConsoleId | Get silo console ID |
| PsGetSiloContainerId | Get silo container ID |
| PsGetSiloContext | Get silo context |
| PsGetSiloMonitorContextSlot | Get silo monitor slot |
| PsGetThreadCreateTime | Get thread create time |
| PsGetThreadExitStatus | Get thread exit status |
| PsGetThreadId | Get thread ID |
| PsGetThreadProcessId | Get thread's process ID |
| PsGetThreadProperty | Get thread property |
| PsGetThreadServerSilo | Get thread server silo |
| PsGetVersion | Get OS version |
| PsInsertPermanentSiloContext | Insert permanent silo |
| PsInsertSiloContext | Insert silo context |
| PsIsHostSilo | Check host silo |
| PsIsSystemThread | Check system thread |
| PsMakeSiloContextPermanent | Make silo permanent |
| PsQueryProcessAvailableCpus | Query process CPUs |
| PsQueryProcessAvailableCpusCount | Query process CPU count |
| PsQuerySystemAvailableCpus | Query system CPUs |
| PsQuerySystemAvailableCpusCount | Query system CPU count |
| PsQueryTotalCycleTimeProcess | Query process cycle time |
| PsReferenceSiloContext | Reference silo context |
| PsRegisterProcessAvailableCpusChangeNotification | Register CPU change |
| PsRegisterSiloMonitor | Register silo monitor |
| PsRegisterSystemAvailableCpusChangeNotification | Register CPU change |
| PsRemoveCreateThreadNotifyRoutine | Remove thread notify |
| PsRemoveLoadImageNotifyRoutine | Remove image notify |
| PsRemoveSiloContext | Remove silo context |
| PsReplaceSiloContext | Replace silo context |
| PsRevertToUserMultipleGroupAffinityThread | Revert affinity |
| PsSetCreateProcessNotifyRoutine | Set process notify |
| PsSetCreateProcessNotifyRoutineEx | Set process notify (ext) |
| PsSetCreateProcessNotifyRoutineEx2 | Set process notify (v2) |
| PsSetCreateThreadNotifyRoutine | Set thread notify |
| PsSetCreateThreadNotifyRoutineEx | Set thread notify (ext) |
| PsSetLoadImageNotifyRoutine | Set image load notify |
| PsSetLoadImageNotifyRoutineEx | Set image notify (ext) |
| PsSetSystemMultipleGroupAffinityThread | Set thread affinity |
| PsStartSiloMonitor | Start silo monitor |
| PsTerminateServerSilo | Terminate server silo |
| PsTerminateSystemThread | Terminate system thread |
| PsUnregisterAvailableCpusChangeNotification | Unregister CPU change |
| PsUnregisterSiloMonitor | Unregister silo monitor |

### 3.3 I/O Manager (IoXxx) — 174 functions

See Section 7 for the complete list.

### 3.4 Object Manager (ObXxx) — 15 functions

| Function | Purpose |
|----------|---------|
| ObCloseHandle | Close object handle |
| ObDereferenceObject | Dereference object |
| ObDereferenceObjectDeferDelete | Dereference + defer delete |
| ObDereferenceObjectDeferDeleteWithTag | Dereference defer + tag |
| ObDereferenceObjectWithTag | Dereference with tag |
| ObGetObjectSecurity | Get object security |
| ObfReferenceObject | Reference object (function) |
| ObReferenceObject | Reference object |
| ObReferenceObjectByHandle | Reference by handle |
| ObReferenceObjectByHandleWithTag | Reference by handle + tag |
| ObReferenceObjectByPointer | Reference by pointer |
| ObReferenceObjectByPointerWithTag | Reference ptr + tag |
| ObReferenceObjectSafe | Safe reference |
| ObReferenceObjectWithTag | Reference with tag |
| ObRegisterCallbacks | Register object callbacks |
| ObReleaseObjectSecurity | Release object security |
| ObUnRegisterCallbacks | Unregister callbacks |

### 3.5 Security Reference Monitor (SeXxx) — 7 functions

| Function | Purpose |
|----------|---------|
| SeAccessCheck | Access check |
| SeAssignSecurity | Assign security descriptor |
| SeAssignSecurityEx | Assign security (extended) |
| SeDeassignSecurity | Deassign security |
| SeFreePrivileges | Free privileges |
| SeSinglePrivilegeCheck | Single privilege check |
| SeValidSecurityDescriptor | Validate security descriptor |

### 3.6 Configuration Manager (CmXxx) — 9 functions

| Function | Purpose |
|----------|---------|
| CmCallbackGetKeyObjectID | Get key object ID |
| CmCallbackGetKeyObjectIDEx | Get key ID (extended) |
| CmCallbackReleaseKeyObjectIDEx | Release key ID |
| CmGetBoundTransaction | Get bound transaction |
| CmGetCallbackVersion | Get callback version |
| CmRegisterCallback | Register callback |
| CmRegisterCallbackEx | Register callback (ext) |
| CmSetCallbackObjectContext | Set callback context |
| CmUnRegisterCallback | Unregister callback |

---

## 4. Core Kernel Library (KeXxx)

Scheduling, interrupts, synchronization, DPC/APC, timers.

### 4.1 Synchronization Primitives

| Function | Purpose |
|----------|---------|
| KeInitializeSpinLock | Initialize spin lock |
| KeAcquireSpinLock | Acquire spin lock |
| KeAcquireSpinLockAtDpcLevel | Acquire at DPC level |
| KeAcquireSpinLockForDpc | Acquire for DPC |
| KeAcquireSpinLockRaiseToDpc | Acquire raising to DPC |
| KeAcquireInStackQueuedSpinLock | Acquire queued spin lock |
| KeAcquireInStackQueuedSpinLockAtDpcLevel | Acquire queued at DPC |
| KeAcquireInStackQueuedSpinLockForDpc | Acquire queued for DPC |
| KeAcquireInterruptSpinLock | Acquire interrupt spin lock |
| KeReleaseSpinLock | Release spin lock |
| KeReleaseSpinLockForDpc | Release for DPC |
| KeReleaseSpinLockFromDpcLevel | Release from DPC level |
| KeReleaseInStackQueuedSpinLock | Release queued spin lock |
| KeReleaseInStackQueuedSpinLockForDpc | Release queued for DPC |
| KeReleaseInStackQueuedSpinLockFromDpcLevel | Release queued from DPC |
| KeReleaseInterruptSpinLock | Release interrupt spin lock |
| KeTestSpinLock | Test spin lock |
| KeTryToAcquireSpinLockAtDpcLevel | Try acquire at DPC |
| KeInitializeEvent | Initialize event |
| KeReadStateEvent | Read event state |
| KeClearEvent | Clear event |
| KeResetEvent | Reset event |
| KeSetEvent | Set event |
| KeSetEventBoostPriority | Set event + boost |
| KePulseEvent | Pulse event |
| KeInitializeMutex | Initialize mutex |
| KeReadStateMutex | Read mutex state |
| KeReleaseMutex | Release mutex |
| KeInitializeSemaphore | Initialize semaphore |
| KeReadStateSemaphore | Read semaphore state |
| KeReleaseSemaphore | Release semaphore |
| KeInitializeTimer | Initialize timer |
| KeInitializeTimerEx | Initialize timer (ext) |
| KeSetTimer | Set timer |
| KeSetTimerEx | Set timer (ext) |
| KeSetCoalescableTimer | Set coalescable timer |
| KeCancelTimer | Cancel timer |
| KeReadStateTimer | Read timer state |

### 4.2 Thread/Processor/IRQL

| Function | Purpose |
|----------|---------|
| KeGetCurrentIrql | Get current IRQL |
| KeGetCurrentProcessorNumber | Get current CPU |
| KeGetCurrentProcessorNumberEx | Get current CPU (ext) |
| KeGetCurrentProcessorIndex | Get current CPU index |
| KeGetCurrentNodeNumber | Get current NUMA node |
| KeGetCurrentThread | Get current thread |
| KeRaiseIrql | Raise IRQL |
| KeRaiseIrqlToDpcLevel | Raise to DPC |
| KeRaiseIrqlToSynchLevel | Raise to synch |
| KeLowerIrql | Lower IRQL |
| KeSetBasePriorityThread | Set thread priority |
| KeSetPriorityThread | Set thread priority |
| KeQueryPriorityThread | Query thread priority |
| KeQueryRuntimeThread | Query thread runtime |
| KeQueryTotalCycleTimeThread | Query thread cycles |
| KeSetSystemAffinityThread | Set system affinity |
| KeSetSystemAffinityThreadEx | Set affinity (ext) |
| KeSetSystemGroupAffinityThread | Set group affinity |
| KeRevertToUserAffinityThreadEx | Revert to user affinity |
| KeRevertToUserGroupAffinityThread | Revert group affinity |
| KeDelayExecutionThread | Delay execution |
| KeYieldExecution | Yield execution |
| KeShouldYieldProcessor | Should yield |
| KeEnterCriticalRegion | Enter critical region |
| KeLeaveCriticalRegion | Leave critical region |
| KeEnterGuardedRegion | Enter guarded region |
| KeLeaveGuardedRegion | Leave guarded region |
| KeAreApcsDisabled | Are APCs disabled |
| KeAreAllApcsDisabled | Are all APCs disabled |
| KeExpandKernelStackAndCallout | Expand stack + callout |
| KeExpandKernelStackAndCalloutEx | Expand stack (ext) |
| KeSetKernelStackSwapEnable | Set stack swap |
| KeIsExecutingDpc | Is executing DPC |

### 4.3 DPC / APC

| Function | Purpose |
|----------|---------|
| KeInitializeDpc | Initialize DPC |
| KeInitializeThreadedDpc | Initialize threaded DPC |
| KeInsertQueueDpc | Insert DPC queue |
| KeRemoveQueueDpc | Remove DPC from queue |
| KeSetImportanceDpc | Set DPC importance |
| KeSetTargetProcessorDpc | Set DPC target CPU |
| KeSetTargetProcessorDpcEx | Set DPC target (ext) |
| KeFlushQueuedDpcs | Flush queued DPCs |
| KeGenericCallDpc | Generic DPC call |
| KeSignalCallDpcSynchronize | Signal DPC sync |
| KeSignalCallDpcDone | Signal DPC done |

### 4.4 Time / Counters

| Function | Purpose |
|----------|---------|
| KeQueryPerformanceCounter | Performance counter |
| KeQuerySystemTime | System time |
| KeQuerySystemTimePrecise | Precise system time |
| KeQueryInterruptTime | Interrupt time |
| KeQueryInterruptTimePrecise | Precise interrupt time |
| KeQueryTickCount | Tick count |
| KeQueryTimeIncrement | Time increment |
| KeQueryUnbiasedInterruptTime | Unbiased interrupt time |
| KeQueryActiveProcessorCount | Active processor count |
| KeQueryActiveProcessorCountEx | Active count (ext) |
| KeQueryMaximumProcessorCount | Max processor count |
| KeQueryMaximumProcessorCountEx | Max count (ext) |
| KeQueryActiveGroupCount | Active group count |
| KeQueryMaximumGroupCount | Max group count |
| KeQueryLogicalProcessorRelationship | Logical processor info |
| KeQueryNodeActiveAffinity | NUMA node affinity |
| KeQueryNodeActiveAffinity2 | Node affinity (v2) |
| KeQueryNodeActiveProcessorCount | Node processor count |
| KeQueryNodeMaximumProcessorCount | Node max processor count |
| KeQueryHighestNodeNumber | Highest node number |
| KeQueryGroupAffinity | Group affinity |
| KeQueryAuxiliaryCounterFrequency | Aux counter frequency |
| KeConvertAuxiliaryCounterToPerformanceCounter | Counter conversion |
| KeConvertPerformanceCounterToAuxiliaryCounter | Counter conversion |

### 4.5 Crash / Debug / Misc

| Function | Purpose |
|----------|---------|
| KeBugCheck | Bug check (crash) |
| KeBugCheckEx | Bug check (extended) |
| KeRegisterBugCheckCallback | Register bug check cb |
| KeDeregisterBugCheckCallback | Deregister bug check cb |
| KeRegisterBugCheckReasonCallback | Register reason cb |
| KeDeregisterBugCheckReasonCallback | Deregister reason cb |
| KeInitializeCrashDumpHeader | Init crash dump header |
| KeGetBugMessageText | Get bug message text |
| KeRegisterNmiCallback | Register NMI callback |
| KeDeregisterNmiCallback | Deregister NMI callback |
| KeRegisterBoundCallback | Register bound callback |
| KeDeregisterBoundCallback | Deregister bound callback |
| KeRegisterProcessorChangeCallback | Register CPU change cb |
| KeDeregisterProcessorChangeCallback | Deregister CPU change |
| KeEnterKernelDebugger | Enter kernel debugger |
| KeBreakinBreakpoint | Breakin breakpoint |
| KeStallExecutionProcessor | Stall processor |
| KeSynchronizeExecution | Synchronize execution |
| KeFlushWriteBuffer | Flush write buffer |
| KeFlushIoBuffers | Flush I/O buffers |
| KeMemoryBarrier | Memory barrier |
| KeIpiGenericCall | IPI generic call |
| KeInvalidateAllCaches | Invalidate all caches |
| KeInvalidateRangeAllCaches | Invalidate range caches |
| KeSaveExtendedProcessorState | Save processor state |
| KeRestoreExtendedProcessorState | Restore processor state |
| KeSaveFloatingPointState | Save FP state |
| KeRestoreFloatingPointState | Restore FP state |
| KeGetRecommendedSharedDataAlignment | Shared data alignment |
| KeGetProcessorIndexFromNumber | CPU index from number |
| KeGetProcessorNumberFromIndex | CPU number from index |
| KeSetHardwareCounterConfiguration | Set HW counter config |
| KeQueryHardwareCounterConfiguration | Query HW counter config |
| KeSetTimeUpdateNotifyRoutine | Set time update notify |
| KeAddTriageDumpDataBlock | Add triage dump block |
| KeInitializeCallbackRecord | Init callback record |
| KeInitializeDeviceQueue | Init device queue |
| KeInsertByKeyDeviceQueue | Insert by key |
| KeInsertDeviceQueue | Insert device queue |
| KeRemoveByKeyDeviceQueue | Remove by key |
| KeRemoveByKeyDeviceQueueIfBusy | Remove by key if busy |
| KeRemoveDeviceQueue | Remove device queue |
| KeRemoveEntryDeviceQueue | Remove entry |
| KeRcuReadLock | RCU read lock |
| KeRcuReadUnlock | RCU read unlock |
| KeRcuSynchronize | RCU synchronize |
| KeSrcuAllocate | SRCU allocate |
| KeSrcuFree | SRCU free |
| KeSrcuReadLock | SRCU read lock |
| KeSrcuReadUnlock | SRCU read unlock |
| KeSrcuSynchronize | SRCU synchronize |
| KeWaitForSingleObject | Wait for single object |
| KeWaitForMultipleObjects | Wait for multiple objects |
| KeWaitForMutexObject | Wait for mutex |

---

## 5. Memory Manager (MmXxx)

See Section 3.1 for the complete 85-function list.

---

## 6. Process/Thread Manager (PsXxx)

See Section 3.2 for the complete 66-function list.

---

## 7. I/O Manager (IoXxx) — 174 functions

### 7.1 Device/Object Management

IoCreateDevice, IoDeleteDevice, IoGetDeviceObjectPointer, IoAttachDeviceToDeviceStack,
IoGetAttachedDeviceReference, IoDetachDevice, IoAllocateDriverObjectExtension,
IoGetDriverObjectExtension, IoCreateController, IoDeleteController, IoCreateDeviceSecure

### 7.2 Device Interface

IoRegisterDeviceInterface, IoSetDeviceInterfaceState, IoGetDeviceInterfaces,
IoGetDeviceInterfaceAlias, IoGetDeviceInterfacePropertyData, IoSetDeviceInterfacePropertyData,
IoOpenDeviceInterfaceRegistryKey

### 7.3 Interrupt Handling

IoConnectInterrupt, IoDisconnectInterrupt, IoConnectInterruptEx, IoDisconnectInterruptEx,
IoGetAffinityInterrupt, IoReportInterruptActive, IoReportInterruptInactive,
IoInitializeDpcRequest, IoRequestDpc

### 7.4 IRP Management

IoAllocateIrp, IoAllocateIrpEx, IoFreeIrp, IoInitializeIrp, IoReuseIrp,
IoMakeAssociatedIrp, IoSizeOfIrp, IoGetCurrentIrpStackLocation,
IoGetNextIrpStackLocation, IoSetNextIrpStackLocation, IoSkipCurrentIrpStackLocation,
IoCopyCurrentIrpStackLocationToNext, IoGetInitialStack, IoGetStackLimits,
IoWithinStackLimits, IoGetRemainingStackSize, IoMarkIrpPending, IoCompleteRequest,
IoCallDriver, IoForwardIrpSynchronously, IoSetCompletionRoutine, IoSetCompletionRoutineEx,
IoSetMasterIrpStatus, IoCancelIrp, IoSetCancelRoutine, IoValidateDeviceIoControlAccess

### 7.5 DMA

IoGetDmaAdapter, IoAllocateAdapterChannel, IoMapTransfer, IoAllocateMdl, IoFreeMdl,
IoBuildPartialMdl, IoAllocateErrorLogEntry, IoFreeErrorLogEntry, IoWriteErrorLogEntry

### 7.6 Work Items / Threads

IoAllocateWorkItem, IoFreeWorkItem, IoQueueWorkItem, IoQueueWorkItemEx,
IoTryQueueWorkItem, IoInitializeWorkItem, IoUninitializeWorkItem, IoSizeofWorkItem,
IoCreateSystemThread

### 7.7 Plug and Play / WMI

IoRegisterPlugPlayNotification, IoUnregisterPlugPlayNotification,
IoUnregisterPlugPlayNotificationEx, IoInvalidateDeviceRelations,
IoInvalidateDeviceState, IoReportDetectedDevice, IoRequestDeviceEject,
IoReportTargetDeviceChange, IoReportTargetDeviceChangeAsynchronous,
IoWMI* (30+ functions for WMI)

### 7.8 Other Key Functions

IoCreateSymbolicLink, IoDeleteSymbolicLink, IoCreateNotificationEvent,
IoCreateSynchronizationEvent, IoBuildSynchronousFsdRequest,
IoBuildAsynchronousFsdRequest, IoBuildDeviceIoControlRequest,
IoGetConfigurationInformation, IoGetCurrentProcess, IoGetFunctionCodeFromCtlCode

---

## 8. Object Manager (ObXxx)

See Section 3.4 for the complete list.

---

## 9. Executive Library (ExXxx) — 104 functions

### 9.1 Pool Allocation

ExAllocatePool, ExAllocatePool2, ExAllocatePool3, ExAllocatePoolZero,
ExAllocatePoolUninitialized, ExAllocatePoolWithTag, ExAllocatePoolWithQuota,
ExAllocatePoolWithQuotaTag, ExAllocatePoolWithTagPriority, ExAllocatePoolQuotaZero,
ExAllocatePoolQuotaUninitialized, ExAllocatePoolPriorityZero,
ExAllocatePoolPriorityUninitialized, ExFreePool, ExFreePool2, ExFreePoolWithTag,
ExCreatePool, ExDestroyPool

### 9.2 Lookaside Lists

ExInitializeLookasideListEx, ExInitializeNPagedLookasideList,
ExInitializePagedLookasideList, ExDeleteLookasideListEx, ExDeleteNPagedLookasideList,
ExDeletePagedLookasideList, ExAllocateFromLookasideListEx,
ExAllocateFromNPagedLookasideList, ExAllocateFromPagedLookasideList,
ExFreeToLookasideListEx, ExFreeToNPagedLookasideList, ExFreeToPagedLookasideList,
ExFlushLookasideListEx

### 9.3 Synchronization

ExInitializeFastMutex, ExAcquireFastMutex, ExReleaseFastMutex,
ExAcquireFastMutexUnsafe, ExReleaseFastMutexUnsafe, ExTryToAcquireFastMutex,
ExInitializePushLock, ExAcquirePushLockExclusive, ExAcquirePushLockShared,
ExReleasePushLockExclusive, ExReleasePushLockShared, ExTryAcquirePushLockExclusive,
ExTryAcquirePushLockShared, ExInitializeResourceLite, ExReinitializeResourceLite,
ExDeleteResourceLite, ExAcquireResourceExclusiveLite, ExAcquireResourceSharedLite,
ExAcquireSharedStarveExclusive, ExAcquireSharedWaitForExclusive,
ExConvertExclusiveToSharedLite, ExReleaseResourceLite, ExReleaseResourceForThreadLite,
ExReleaseResourceAndLeaveCriticalRegion, ExSetResourceOwnerPointer,
ExSetResourceOwnerPointerEx, ExIsResourceAcquiredExclusiveLite,
ExIsResourceAcquiredSharedLite, ExGetCurrentResourceThread,
ExGetExclusiveWaiterCount, ExGetSharedWaiterCount

### 9.4 Rundown Protection

ExInitializeRundownProtection, ExReInitializeRundownProtection,
ExAcquireRundownProtection, ExReleaseRundownProtection,
ExAcquireRundownProtectionEx, ExReleaseRundownProtectionEx,
ExWaitForRundownProtectionRelease, ExRundownCompleted

### 9.5 Timer

ExAllocateTimer, ExDeleteTimer, ExSetTimer, ExCancelTimer,
ExInitializeSetTimerParameters, ExInitializeDeleteTimerParameters,
ExQueryTimerResolution, ExSetTimerResolution

### 9.6 Interlocked Operations

ExInterlockedAddLargeInteger, ExInterlockedAddLargeStatistic,
ExInterlockedAddUlong, ExInterlockedCompareExchange64,
ExInterlockedFlushSList, ExInterlockedInsertHeadList,
ExInterlockedInsertTailList, ExInterlockedPopEntryList,
ExInterlockedPopEntrySList, ExInterlockedPushEntryList,
ExInterlockedPushEntrySList, ExInterlockedRemoveHeadList

### 9.7 Misc

ExInitializeSListHead, ExQueryDepthSList, ExUuidCreate, ExVerifySuite,
ExGetPreviousMode, ExRaiseStatus, ExRaiseAccessViolation,
ExRaiseDatatypeMisalignment, ExNotifyCallback, ExRegisterCallback,
ExUnregisterCallback, ExCreateCallback, ExLocalTimeToSystemTime,
ExSystemTimeToLocalTime, ExGetFirmwareEnvironmentVariable,
ExSetFirmwareEnvironmentVariable, ExGetFirmwareType

---

## 10. Run-Time Library (RtlXxx)

### 10.1 Memory Operations

RtlCopyMemory, RtlCopyDeviceMemory, RtlCopyMemoryNonTemporal, RtlCopyVolatileMemory,
RtlMoveMemory, RtlMoveVolatileMemory, RtlFillMemory, RtlFillDeviceMemory,
RtlFillMemoryNonTemporal, RtlFillVolatileMemory, RtlZeroMemory, RtlSecureZeroMemory,
RtlSecureZeroMemory2, RtlCompareMemory, RtlCompareDeviceMemory, RtlEqualMemory,
RtlEqualDeviceMemory, RtlConstantTimeEqualMemory, RtlIsZeroMemory,
RtlPrefetchMemoryNonTemporal, RtlSetVolatileMemory

### 10.2 String Operations

RtlInitString, RtlInitStringEx, RtlInitAnsiString, RtlInitUnicodeString,
RtlInitUTF8String, RtlInitUTF8StringEx, RtlFreeAnsiString, RtlFreeUnicodeString,
RtlFreeUTF8String, RtlAppendUnicodeStringToString, RtlAppendUnicodeToString,
RtlCopyString, RtlCopyUnicodeString, RtlCompareString, RtlCompareUnicodeString,
RtlEqualString, RtlEqualUnicodeString, RtlUpperString, RtlUpcaseUnicodeString,
RtlDowncaseUnicodeChar, RtlPrefixUnicodeString, RtlHashUnicodeString,
RtlAnsiStringToUnicodeString, RtlAnsiStringToUnicodeSize, RtlUnicodeStringToAnsiString,
RtlUnicodeStringToInteger, RtlIntegerToUnicodeString, RtlInt64ToUnicodeString,
RtlIntPtrToUnicodeString, RtlStringFromGUID, RtlGUIDFromString,
RtlCharToInteger, RtlLengthSecurityDescriptor, RtlSanitizeUnicodeStringPadding

### 10.3 Bitmap Operations

RtlInitializeBitMap, RtlClearAllBits, RtlSetAllBits, RtlClearBit, RtlSetBit,
RtlTestBit, RtlClearBits, RtlSetBits, RtlAreBitsClear, RtlAreBitsSet,
RtlFindClearBits, RtlFindClearBitsAndSet, RtlFindClearRuns, RtlFindSetBits,
RtlFindSetBitsAndClear, RtlFindFirstRunClear, RtlFindLastBackwardRunClear,
RtlFindLeastSignificantBit, RtlFindMostSignificantBit, RtlFindLongestRunClear,
RtlFindNextForwardRunClear, RtlNumberOfClearBits, RtlNumberOfSetBits,
RtlNumberOfSetBitsUlongPtr, RtlCheckBit

### 10.4 Registry

RtlCheckRegistryKey, RtlCreateRegistryKey, RtlDeleteRegistryValue,
RtlQueryRegistryValues, RtlQueryRegistryValueWithFallback, RtlWriteRegistryValue

### 10.5 Security

RtlCreateSecurityDescriptor, RtlValidSecurityDescriptor, RtlSetDaclSecurityDescriptor,
RtlMapGenericMask, RtlNormalizeSecurityDescriptor

### 10.6 Version/Misc

RtlGetVersion, RtlGetEnabledExtendedFeatures, RtlGetPersistedStateLocation,
RtlIsNtDdiVersionAvailable, RtlIsServicePackVersionInstalled, RtlIsStateSeparationEnabled,
RtlRaiseCustomSystemEventTrigger

### 10.7 Correlation Vector

RtlInitializeCorrelationVector, RtlIncrementCorrelationVector, RtlExtendCorrelationVector

### 10.8 Resource/Time

RtlCmDecodeMemIoResource, RtlCmEncodeMemIoResource, RtlIoDecodeMemIoResource,
RtlIoEncodeMemIoResource, RtlTimeFieldsToTime, RtlTimeToTimeFields,
RtlConvertLongToLargeInteger, RtlConvertUlongToLargeInteger

### 10.9 Run-Once

RtlRunOnceBeginInitialize, RtlRunOnceComplete, RtlRunOnceExecuteOnce, RtlRunOnceInitialize

### 10.10 Safe String Functions (80+)

RtlStringCbCopy/Cat/Printf/Length (A/W/Ex/N variants),
RtlStringCchCopy/Cat/Printf/Length (A/W/Ex/N variants),
RtlUnicodeStringCopy/Cat/Printf/Init/Validate (Ex variants)

### 10.11 Safe Integer Functions (150+)

RtlInt8Add/Mult/Sub, RtlInt16Add/Mult/Sub, RtlInt32Add/Mult/Sub,
RtlInt64Add/Mult/Sub, RtlLongAdd/Mult/Sub, RtlULongAdd/Mult/Sub,
RtlLongLongAdd/Mult/Sub, RtlULongLongAdd/Mult/Sub,
RtlIntPtrToChar, RtlLongToChar, RtlInt8ToUChar, etc.

---

## 11. Security Reference Monitor (SeXxx)

SeAccessCheck, SeAssignSecurity, SeAssignSecurityEx, SeDeassignSecurity,
SeFreePrivileges, SeSinglePrivilegeCheck, SeValidSecurityDescriptor

---

## 12. Configuration Manager (CmXxx)

CmCallbackGetKeyObjectID, CmCallbackGetKeyObjectIDEx, CmCallbackReleaseKeyObjectIDEx,
CmGetBoundTransaction, CmGetCallbackVersion, CmRegisterCallback, CmRegisterCallbackEx,
CmSetCallbackObjectContext, CmUnRegisterCallback

---

## 13. Power Manager (PoXxx) — 57 functions

### System Power

PoCallDriver, PoRegisterSystemState, PoSetSystemState, PoUnregisterSystemState,
PoRequestPowerIrp, PoSetPowerState, PoGetSystemWake, PoSetSystemWake,
PoSetSystemWakeDevice, PoStartNextPowerIrp, PoRegisterPowerSettingCallback,
PoUnregisterPowerSettingCallback, PoRegisterForEffectivePowerModeNotifications,
PoUnregisterFromEffectivePowerModeNotifications

### Device Power

PoRegisterDeviceForIdleDetection, PoSetDeviceBusy, PoSetDeviceBusyEx,
PoStartDeviceBusy, PoEndDeviceBusy, PoCreatePowerRequest, PoDeletePowerRequest,
PoSetPowerRequest, PoClearPowerRequest, PoQueryWatchdogTime, PoPowerOnCrashdumpDevice

### PoFx (Power Framework)

PoFxActivateComponent, PoFxCompleteDevicePowerNotRequired, PoFxCompleteIdleCondition,
PoFxCompleteIdleState, PoFxIdleComponent, PoFxIssueComponentPerfStateChange,
PoFxIssueComponentPerfStateChangeMultiple, PoFxNotifySurprisePowerOn, PoFxPowerControl,
PoFxQueryCurrentComponentPerfState, PoFxRegisterComponentPerfStates,
PoFxRegisterDevice, PoFxReportDevicePoweredOn, PoFxSetComponentLatency,
PoFxSetComponentResidency, PoFxSetComponentWake, PoFxSetDeviceIdleTimeout,
PoFxStartDevicePowerManagement, PoFxUnregisterDevice

### PoFx Plugin (PEP)

PoFxRegisterPlugin, PoFxRegisterPluginEx, PoFxRegisterCoreDevice,
PoFxRegisterCrashdumpDevice, PoFxCompleteDirectedPowerDown,
PoFxSetTargetDripsDevicePowerState

### Power Limits

PoCreatePowerLimitRequest, PoDeletePowerLimitRequest, PoQueryPowerLimitAttributes,
PoQueryPowerLimitValue, PoSetPowerLimitValue

---

## 14. HAL Library (HalXxx) — 24 functions

| Function | Purpose |
|----------|---------|
| HalAcquireDisplayOwnership | Acquire display |
| HalAllocateAdapterChannel | Allocate DMA channel |
| HalAllocateCommonBuffer | Allocate common buffer |
| HalAllocateCrashDumpRegisters | Allocate crash dump regs |
| HalAllocateHardwareCounters | Allocate HW counters |
| HalAllocateMapRegisters | Allocate map registers |
| HalAssignSlotResources | Assign slot resources |
| HalExamineMBR | Examine MBR |
| HalFreeCommonBuffer | Free common buffer |
| HalFreeHardwareCounters | Free HW counters |
| HalGetAdapter | Get DMA adapter |
| HalGetBusData | Get bus data |
| HalGetBusDataByOffset | Get bus data by offset |
| HalGetDmaAlignmentRequirement | Get DMA alignment |
| HalGetInterruptVector | Get interrupt vector |
| HalGetScatterGatherList | Get S/G list |
| HalMakeBeep | Beep |
| HalPutDmaAdapter | Put DMA adapter |
| HalPutScatterGatherList | Put S/G list |
| HalReadDmaCounter | Read DMA counter |
| HalReturnToFirmware | Return to firmware |
| HalSetBusData | Set bus data |
| HalSetBusDataByOffset | Set bus data by offset |
| HalTranslateBusAddress | Translate bus address |

---

## 15. KTM Routines

### TmXxx — 24 functions

TmCommitComplete, TmCommitEnlistment, TmCommitTransaction, TmCreateEnlistment,
TmDereferenceEnlistmentKey, TmEnableCallbacks, TmGetTransactionId,
TmInitializeTransactionManager, TmIsTransactionActive, TmPrePrepareComplete,
TmPrePrepareEnlistment, TmPrepareComplete, TmPrepareEnlistment, TmReadOnlyEnlistment,
TmRecoverEnlistment, TmRecoverResourceManager, TmRecoverTransactionManager,
TmReferenceEnlistmentKey, TmRenameTransactionManager, TmRequestOutcomeEnlistment,
TmRollbackComplete, TmRollbackEnlistment, TmRollbackTransaction, TmSinglePhaseReject

### ZwXxx KTM — 33 functions

See Section 2.11 for the complete list.

---

## 16. DMA Library

Accessed via DMA_OPERATIONS function table (returned by IoGetDmaAdapter):

| Function | Purpose |
|----------|---------|
| AllocateAdapterChannel | Allocate DMA channel |
| AllocateCommonBuffer | Allocate common buffer |
| FreeAdapterChannel | Free DMA channel |
| FreeCommonBuffer | Free common buffer |
| FreeMapRegisters | Free map registers |
| GetDmaAlignment | Get DMA alignment |
| GetScatterGatherList | Get scatter/gather list |
| BuildScatterGatherList | Build S/G list |
| BuildMdlFromScatterGatherList | Build MDL from S/G |
| CalculateScatterGatherList | Calculate S/G size |
| FlushAdapterBuffers | Flush adapter buffers |
| MapTransfer | Map DMA transfer |
| PutDmaAdapter | Put DMA adapter |
| PutScatterGatherList | Put S/G list |
| ReadDmaCounter | Read DMA counter |

---

## 17. Win32k Syscalls (GDI/Windowing)

win32k.sys provides ~2000+ syscall entries for windowing, GDI, and input. Key categories:

### Window Management

NtUserCreateWindowEx, NtUserDestroyWindow, NtUserShowWindow, NtUserMoveWindow,
NtUserSetWindowLong, NtUserGetWindowLong, NtUserSetWindowPos, NtUserFindWindowEx,
NtUserGetAncestor, NtUserGetParent, NtUserSetParent, NtUserGetClassName,
NtUserGetWindowText, NtUserSetWindowText, NtUserGetDC, NtUserReleaseDC,
NtUserBeginPaint, NtUserEndPaint, NtUserInvalidateRect, NtUserUpdateWindow,
NtUserGetMessage, NtUserPeekMessage, NtUserDispatchMessage, NtUserPostMessage,
NtUserSendMessage, NtUserCallMsgFilter, NtUserTranslateMessage, NtUserWaitMessage

### Input

NtUserGetAsyncKeyState, NtUserGetKeyState, NtUserSetCursor, NtUserGetCursorPos,
NtUserSetCursorPos, NtUserGetKeyboardState, NtUserSetKeyboardState,
NtUserMapVirtualKeyEx, NtUserToUnicodeEx, NtUserVkKeyScanEx,
NtUserRegisterHotKey, NtUserUnregisterHotKey, NtUserBlockInput,
NtUserGetRawInputData, NtUserGetRawInputDeviceInfo, NtUserRegisterRawInputDevices,
NtUserSetWindowsHookEx, NtUserUnhookWindowsHookEx, NtUserCallNextHookEx

### GDI Drawing

NtGdiBitBlt, NtGdiStretchBlt, NtGdiPatBlt, NtGdiRectangle, NtGdiEllipse,
NtGdiLineTo, NtGdiMoveTo, NtGdiFillPath, NtGdiStrokePath, NtGdiSelectClipPath,
NtGdiDrawStream, NtGdiGradientFill, NtGdiAlphaBlend, NtGdiTransparentBlt

### GDI Objects

NtGdiCreateBitmap, NtGdiCreateCompatibleBitmap, NtGdiCreateDIBitmap,
NtGdiCreateCompatibleDC, NtGdiCreateSolidBrush, NtGdiCreatePen,
NtGdiCreateFontIndirect, NtGdiSelectObject, NtGdiDeleteObject,
NtGdiGetStockObject, NtGdiGetNearestColor, NtGdiGetPixel, NtGdiSetPixel

### Text

NtGdiDrawText, NtGdiTextOut, NtGdiExtTextOut, NtGdiGetTextExtent,
NtGdiGetTextMetrics, NtGdiGetCharABCWidths, NtGdiGetGlyphIndices,
NtGdiAddFontResource, NtGdiRemoveFontResource, NtGdiCreateFontIndirectEx

### Clipboard / DDE

NtUserOpenClipboard, NtUserCloseClipboard, NtUserEmptyClipboard,
NtUserGetClipboardData, NtUserSetClipboardData, NtUserIsClipboardFormatAvailable,
NtUserGetClipboardFormatName, NtUserCountClipboardFormats,
NtUserEnumClipboardFormats, NtUserRegisterClipboardFormat

### Desktop / Station

NtUserCreateDesktopEx, NtUserOpenDesktop, NtUserCloseDesktop,
NtUserSwitchDesktop, NtUserGetThreadDesktop, NtUserSetThreadDesktop,
NtUserCreateWindowStation, NtUserOpenWindowStation, NtUserCloseWindowStation,
NtUserGetProcessWindowStation, NtUserSetProcessWindowStation

### Menu

NtUserCreateMenu, NtUserInsertMenu, NtUserAppendMenu, NtUserDeleteMenu,
NtUserRemoveMenu, NtUserModifyMenu, NtUserTrackPopupMenu, NtUserHiliteMenuItem,
NtUserGetMenu, NtUserSetMenu, NtUserDestroyMenu, NtUserLoadMenu

### Misc

NtUserSystemParametersInfo, NtUserGetSystemMetrics, NtUserLoadImage,
NtUserCopyImage, NtUserDrawIconEx, NtUserSetTimer, NtUserKillTimer,
NtUserMessageCall, NtUserCallOneParam, NtUserCallTwoParam, NtUserCallHwnd,
NtUserCallHwndLock, NtUserCallHwndParam, NtUserCallHwndParamLock,
NtUserNotifyProcessCreate, NtUserInitialize, NtUserGetForegroundWindow,
NtUserSetForegroundWindow, NtUserSetFocus, NtUserGetFocus,
NtUserSetActiveWindow, NtUserGetActiveWindow, NtUserBuildHwndList,
NtUserRealChildWindowFromPoint, NtUserChildWindowFromPointEx,
NtUserQueryWindow, NtUserValidateRect, NtUserRedrawWindow,
NtUserLockWindowUpdate, NtUserScrollWindowEx, NtUserSetScrollInfo,
NtUserGetScrollInfo, NtUserEnableWindow, NtUserIsWindowEnabled,
NtUserIsWindowVisible, NtUserIsWindow, NtUserGetWindowThreadProcessId

---

## 18. Kernel Object Types

| Object Type | Creator | Purpose |
|-------------|---------|---------|
| Process | NtCreateProcess | Process object |
| Thread | NtCreateThread | Thread object |
| Job | NtCreateJobObject | Job (process group) |
| Section | NtCreateSection | Shared memory / file mapping |
| File | NtCreateFile | File / device handle |
| Port | NtCreatePort | LPC/ALPC port |
| Event | NtCreateEvent | Synchronization event |
| EventPair | NtCreateEventPair | Event pair |
| Mutant | NtCreateMutant | Mutex |
| Semaphore | NtCreateSemaphore | Semaphore |
| Timer | NtCreateTimer | Timer |
| Key | NtCreateKey | Registry key |
| Token | NtCreateToken | Security token |
| Directory | NtCreateDirectoryObject | Object directory |
| SymbolicLink | NtCreateSymbolicLinkObject | Symbolic link |
| IoCompletion | NtCreateIoCompletion | I/O completion port |
| Debug | NtCreateDebugObject | Debug object |
| Profile | NtCreateProfile | Profile object |
| KeyedEvent | NtCreateKeyedEvent | Keyed event |
| WaitablePort | NtCreateWaitablePort | Waitable LPC port |
| Transaction | NtCreateTransaction | KTM transaction |
| TransactionManager | NtCreateTransactionManager | KTM TM |
| ResourceManager | NtCreateResourceManager | KTM RM |
| Enlistment | NtCreateEnlistment | KTM enlistment |
| WorkerFactory | NtCreateWorkerFactory | Thread pool |
| IoRing | NtCreateIoRing | I/O ring |
| CpuPartition | NtCreateCpuPartition | CPU partition |
| Partition | NtCreatePartition | Partition |
| Enclave | NtCreateEnclave | Enclave |
| PrivateNamespace | NtCreatePrivateNamespace | Private namespace |
| RegistryTransaction | NtCreateRegistryTransaction | Registry txn |
| WaitCompletionPacket | NtCreateWaitCompletionPacket | Wait completion |
| ProcessStateChange | NtCreateProcessStateChange | Process state |
| ThreadStateChange | NtCreateThreadStateChange | Thread state |
| ReserveObject | NtAllocateReserveObject | Reserve object |

---

## 19. Key Structures

| Structure | Purpose |
|-----------|---------|
| EPROCESS | Executive process block |
| ETHREAD | Executive thread block |
| KPROCESS | Kernel process (dispatcher) |
| KTHREAD | Kernel thread (scheduler) |
| KPRCB | Processor control block |
| KINTERRUPT | Interrupt object |
| KDPC | Deferred procedure call |
| KAPC | Asynchronous procedure call |
| KSPIN_LOCK | Spin lock |
| KMUTEX | Kernel mutex |
| KEVENT | Kernel event |
| KSEMAPHORE | Kernel semaphore |
| KTIMER | Kernel timer |
| IRP | I/O request packet |
| MDL | Memory descriptor list |
| DEVICE_OBJECT | Device object |
| DRIVER_OBJECT | Driver object |
| IO_STACK_LOCATION | IRP stack location |
| OBJECT_ATTRIBUTES | Object attributes |
| UNICODE_STRING | Unicode string |
| ANSI_STRING | ANSI string |
| LARGE_INTEGER | 64-bit integer |
| SECURITY_DESCRIPTOR | Security descriptor |
| TOKEN | Access token |
| SID | Security identifier |
| ACL | Access control list |
| CLIENT_ID | Process/thread ID pair |
| CONTEXT | Thread context (registers) |
| EXCEPTION_RECORD | Exception information |
| MEMORY_BASIC_INFORMATION | VM region info |
| SYSTEM_INFO | System information |
| FILE_OBJECT | File object |
| SECTION_OBJECT | Section (shared memory) |
| CM_RESOURCE_LIST | Config manager resources |
| IO_RESOURCE_DESCRIPTOR | I/O resource descriptor |
| DMA_ADAPTER | DMA adapter |
| SCATTER_GATHER_LIST | Scatter/gather DMA list |
| RTL_BITMAP | Bitmap |
| LIST_ENTRY | Doubly-linked list |
| SINGLE_LIST_ENTRY | Singly-linked list |
| SLIST_HEADER | Sequenced singly-linked list |
| KSPIN_LOCK_QUEUE | Queued spin lock |
| EX_RUNDOWN_REF | Rundown protection |
| EX_PUSH_LOCK | Push lock |
| DEVICE_RELATIONS | Device relations |
| IO_STATUS_BLOCK | I/O status |
| IO_SECURITY_CONTEXT | I/O security |
| VPB | Volume parameter block |
| OBJECT_TYPE | Object type descriptor |
| OBJECT_HEADER | Object header |
| HANDLE_TABLE | Handle table |
| WORK_QUEUE_ITEM | Work queue item |
| KDPC (threaded) | Threaded DPC |
| EX_TIMER | Executive timer |
| EX_CALLBACK | Executive callback |
| EX_RUNDOWN_PROTECTION_CACHE_AWARE | Cache-aware rundown |

---

## 20. Anti-Cheat Relevance Matrix

### Critical syscall touch points for game anti-cheat:

| Anti-Cheat Check | Windows NT API | macOS XNU Equivalent | Priority |
|-----------------|----------------|---------------------|----------|
| Process enumeration | NtQuerySystemInformation(SystemProcessInformation) | proc_info (336) | CRITICAL |
| Open process handle | NtOpenProcess | task_for_pid (mach) | CRITICAL |
| Read process memory | NtReadVirtualMemory | mach_vm_read | CRITICAL |
| Write process memory | NtWriteVirtualMemory | mach_vm_write | CRITICAL |
| Thread context get/set | NtGet/SetContextThread | thread_get/set_state | CRITICAL |
| Debug attach | NtDebugActiveProcess | ptrace (26) | CRITICAL |
| Module enumeration | NtQueryVirtualMemory (MEM_IMAGE) | proc_info (dyld info) | HIGH |
| Code signature check | NtGetCachedSigningLevel | csops (169) | CRITICAL |
| Code signing bypass | NtSetCachedSigningLevel | — (SIP protects) | CRITICAL |
| Driver load | NtLoadDriver | kext_load | HIGH |
| Handle enumeration | NtQuerySystemInformation(SystemHandleInformation) | No direct equiv | HIGH |
| Object info | NtQueryObject | — | MEDIUM |
| Thread enumeration | NtGetNextThread | task_threads | HIGH |
| Process suspend | NtSuspendProcess/Thread | pid_suspend/thread_suspend | HIGH |
| Memory protection change | NtProtectVirtualMemory | mprotect (74) | HIGH |
| Memory allocate | NtAllocateVirtualMemory | mmap (197) | HIGH |
| Memory map | NtMapViewOfSection | mmap | HIGH |
| File integrity | NtQueryInformationFile | getattrlist (220) | MEDIUM |
| Registry check | NtQueryValueKey | — (no registry) | LOW |
| Input injection | NtUserSendInput | mac_iokit_check_hid_control | HIGH |
| Window detection | NtUserFindWindowEx | CGWindowListCopyWindowInfo | MEDIUM |
| Screenshot detection | NtGdiBitBlt | CGWindowListCreateImage | MEDIUM |
| Keyboard state | NtUserGetAsyncKeyState | NSEvent + CGEventTap | HIGH |
| Hook detection | NtUserSetWindowsHookEx | — | MEDIUM |
| APC injection | NtQueueApcThread | thread_set_state + ARM context | CRITICAL |
| Thread creation remote | NtCreateThreadEx | task_create + thread_create | CRITICAL |
| Section mapping remote | NtMapViewOfSection (remote) | mach_vm_map (remote) | CRITICAL |
| Process creation | NtCreateUserProcess | posix_spawn (244) | HIGH |
| Process hollowing | NtUnmapViewOfSection + write | mach_vm_deallocate + write | CRITICAL |
| DLL injection | NtCreateSection + NtMapViewOfSection | dyld insert | CRITICAL |

### Kernel-level anti-cheat techniques:

| Technique | Windows Mechanism | macOS Mechanism |
|-----------|------------------|-----------------|
| Process notify callbacks | PsSetCreateProcessNotifyRoutineEx2 | mac_proc_check_fork |
| Thread notify callbacks | PsSetCreateThreadNotifyRoutineEx | — |
| Image load notify | PsSetLoadImageNotifyRoutineEx | mac_vnode_check_exec |
| Object callbacks | ObRegisterCallbacks | mac_proc_check_get_task |
| Registry callbacks | CmRegisterCallbackEx | — |
| Filesystem minifilter | FltRegisterFilter | mac_vnode_check_* |
| Network filter | WFP (WFPSAM) | mac_socket_check_* |
| Code integrity | CI.dll + NtSetCachedSigningLevel | AMFI + csops + MACF |
| Kernel patch protection | PatchGuard (x64) | KPP not needed (static kernel) |
| Hypervisor-based | HyperGuard / HVCI | — |
| Process protection | PS_PROTECTED_LEVEL | csops + entitlements |
| Handle protection | OBJECT_TYPE callbacks | port guards |
| ETW tracing | NtTraceEvent | kdebug_trace (179/180) |
| Performance counters | NtQueryPerformanceCounter | kpc (mach) |
