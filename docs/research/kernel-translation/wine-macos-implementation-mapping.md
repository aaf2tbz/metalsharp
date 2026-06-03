# Wine for macOS — Complete NT Kernel → XNU Implementation Mapping
## Every Windows NT kernel function/class/call paired to its macOS XNU equivalent, with Wine implementation instructions

> Purpose: Build Wine for macOS. This is the master implementation reference.
> Target: macOS 26.6 (Tahoe) / arm64 (Apple Silicon)
> Source references: xnu-kernel-reference.md, windows-nt-kernel-reference.md, wine-kernel-compatibility-gap-analysis.md

---

## Legend

| Symbol | Meaning | Wine Action |
|--------|---------|-------------|
| → | Direct pair | Call the XNU API directly |
| ≈ | Close match, translation needed | Wrap with semantic translation |
| 🔄 | Userspace emulation | Implement entirely in Wine ntdll/kernel32 |
| ⚠️ | Partial, subset works | Implement critical paths, stub the rest |
| ❌ | No equivalent, structural gap | Blocker — requires workaround or vendor cooperation |
| — | Not needed for Wine/anti-cheat | No action |

---

## Part 1: NT Native Syscalls (NtXxx) — Complete Implementation Table

### 1.1 Process & Thread Lifecycle

| # | Windows NT API | macOS XNU API | XNU Syscall/Trap | Pair | Wine ntdll Implementation |
|---|---------------|---------------|-------------------|------|--------------------------|
| 1 | NtCreateProcess | fork + execve | BSD 2, 59 | → | `fork()` then `execve()` in child. Set up PEB, TEB, ld.so preload path. |
| 2 | NtCreateProcessEx | posix_spawn | BSD 244 | → | `posix_spawn()` with file_actions for handle inheritance. Map STARTUPINFO to spawn attrs. |
| 3 | NtCreateUserProcess | posix_spawn | BSD 244 | → | `posix_spawn()` with extended attributes. Map CREATE_SUSPENDED to spawn attr POSIX_SPAWN_START_SUSPENDED. |
| 4 | NtCreateThread | bsdthread_create | BSD 360 | → | `bsdthread_create(start_addr, param, stack, pthread, flags)`. Allocate TEB on stack. |
| 5 | NtCreateThreadEx | bsdthread_create | BSD 360 | ≈ | Same as above. Map CREATE_SUSPENDED flag → create suspended, then `thread_suspend()` on mach thread. |
| 6 | NtCreateProcessStateChange | pid_suspend | BSD 433 | ⚠️ | No state change objects. Wine creates internal tracking struct, calls `pid_suspend()` on state change. |
| 7 | NtCreateThreadStateChange | thread_suspend | Mach IPC | ⚠️ | Same pattern — internal struct + `thread_suspend()`. |
| 8 | NtTerminateProcess | exit / kill / terminate_with_payload | BSD 1, 37, 520 | → | `kill(pid, SIGKILL)` for external, `exit(code)` for self. |
| 9 | NtTerminateThread | thread_terminate | Mach IPC | → | `thread_terminate(mach_thread)`. |
| 10 | NtTerminateEnclave | — | — | — | SGX only. Not on macOS. Stub STATUS_NOT_SUPPORTED. |
| 11 | NtTerminateJobObject | kill | BSD 37 | ≈ | Wine iterates job process list, sends SIGKILL to each. |
| 12 | NtSuspendProcess | pid_suspend | BSD 433 | → | `pid_suspend(pid)`. Direct. |
| 13 | NtSuspendThread | thread_suspend | Mach IPC | → | `thread_suspend(mach_thread)`. Direct. |
| 14 | NtResumeProcess | pid_resume | BSD 434 | → | `pid_resume(pid)`. Direct. |
| 15 | NtResumeThread | thread_resume | Mach IPC | → | `thread_resume(mach_thread)`. Direct. |
| 16 | NtAlertThread | ulock_wake | BSD 516 | 🔄 | Wine uses `ulock_wake()` on per-thread ulock address to simulate alert. |
| 17 | NtAlertResumeThread | ulock_wake + thread_resume | BSD 516, Mach IPC | 🔄 | Alert then resume. Two-step in Wine. |
| 18 | NtAlertThreadByThreadId | ulock_wake | BSD 516 | 🔄 | `ulock_wake(UL_UNFAIR_LOCK, &tid_addr, 0)`. |
| 19 | NtAlertThreadByThreadIdEx | ulock_wake | BSD 516 | 🔄 | Same, with extra flags ignored. |
| 20 | NtAlertMultipleThreadByThreadId | ulock_wake (loop) | BSD 516 | 🔄 | Loop over thread IDs, `ulock_wake` each. |
| 21 | NtTestAlert | — | — | 🔄 | Check Wine-internal alert flag. Pure userspace. |
| 22 | NtYieldExecution | swtch | Mach trap 41 | → | `swtch()`. Direct. |
| 23 | NtDelayExecution | nanosleep / clock_sleep | BSD 36, Mach trap 16 | → | `nanosleep(req, rem)` for relative, `clock_sleep()` for absolute. |
| 24 | NtContinue | sigreturn | BSD 184 | ≈ | `sigreturn(ucontext)` with modified register context. Wine sets context from CONTEXT struct. |
| 25 | NtContinueEx | sigreturn | BSD 184 | ≈ | Same as NtContinue. |
| 26 | NtQueueApcThread | thread_set_state + Mach exception | Mach IPC | ⚠️ | **Hard.** No native APC. Wine: (1) suspend target thread, (2) modify ARM_CONTEXT pc/lr to APC handler trampoline, (3) `thread_resume`. Alternative: deliver via Mach exception port. |
| 27 | NtQueueApcThreadEx | (same as NtQueueApcThread) | Mach IPC | ⚠️ | Same as #26. |
| 28 | NtQueueApcThreadEx2 | (same as NtQueueApcThread) | Mach IPC | ⚠️ | Same as #26. |
| 29 | NtGetContextThread | thread_get_state | Mach IPC | → | `thread_get_state(thread, ARM_THREAD_STATE64, &state, &count)`. Map ARM regs → CONTEXT. |
| 30 | NtSetContextThread | thread_set_state | Mach IPC | → | `thread_set_state(thread, ARM_THREAD_STATE64, &state, count)`. Map CONTEXT → ARM regs. |

### 1.2 Virtual Memory

| # | Windows NT API | macOS XNU API | XNU Syscall/Trap | Pair | Wine ntdll Implementation |
|---|---------------|---------------|-------------------|------|--------------------------|
| 31 | NtAllocateVirtualMemory | mmap / mach_vm_allocate | BSD 197, Mach trap 17 | → | `mmap(addr, size, prot, MAP_ANON|MAP_PRIVATE, -1, 0)` or `mach_vm_allocate(task, &addr, size, flags)`. Map MEM_COMMIT/RESERVE → mmap. |
| 32 | NtAllocateVirtualMemoryEx | mmap | BSD 197 | → | Same with extended attributes. Map MEM_EXTENDED_PARAMETER_TYPE to mmap flags. |
| 33 | NtFreeVirtualMemory | munmap / mach_vm_deallocate | BSD 73, Mach trap 18 | → | `munmap(addr, size)` for MEM_RELEASE, `mprotect(addr, size, PROT_NONE)` for MEM_DECOMMIT. |
| 34 | NtProtectVirtualMemory | mprotect / mach_vm_protect | BSD 74, Mach trap 19 | → | `mprotect(addr, len, prot)`. Map PAGE_EXECUTE_READ → PROT_READ|PROT_EXEC. |
| 35 | NtQueryVirtualMemory | mach_vm_region / mach_vm_region_recurse | Mach IPC | → | `mach_vm_region(task, &addr, &size, &count, &info, &count)` → MEMORY_BASIC_INFORMATION. |
| 36 | NtLockVirtualMemory | mlock | BSD 203 | → | `mlock(addr, len)`. Direct. |
| 37 | NtUnlockVirtualMemory | munlock | BSD 204 | → | `munlock(addr, len)`. Direct. |
| 38 | NtFlushVirtualMemory | msync | BSD 65 | → | `msync(addr, len, MS_SYNC)`. Direct. |
| 39 | NtReadVirtualMemory | mach_vm_read | Mach IPC | → | `mach_vm_read(task, addr, size, &data, &data_count)`. Requires task port from `task_for_pid`. |
| 40 | NtReadVirtualMemoryEx | mach_vm_read | Mach IPC | → | Same as #39. |
| 41 | NtWriteVirtualMemory | mach_vm_write | Mach IPC | → | `mach_vm_write(task, addr, data, count)`. Requires task port. |
| 42 | NtGetWriteWatch | mincore | BSD 78 | ⚠️ | `mincore(addr, len, vec)` gives residency, not dirty bits. Wine tracks writes via mprotect + SIGSEGV handler as fallback. |
| 43 | NtResetWriteWatch | — | — | 🔄 | Wine resets internal write-tracking bitmap. |
| 44 | NtCreateSection | shm_open + mmap | BSD 266, 197 | → | `shm_open(name, O_CREAT|O_RDWR, mode)` for named, `mmap()` for anonymous. Map SEC_IMAGE → MAP_JIT on arm64. |
| 45 | NtCreateSectionEx | shm_open + mmap | BSD 266, 197 | 🔄 | Extended params stubbed. |
| 46 | NtOpenSection | shm_open | BSD 266 | → | `shm_open(name, O_RDWR, 0)`. |
| 47 | NtExtendSection | mremap_encrypted / munmap + mmap | BSD 489 | 🔄 | Unmap old region, mmap new larger region. Copy data. |
| 48 | NtMapViewOfSection | mmap | BSD 197 | → | `mmap(addr, size, prot, MAP_SHARED|MAP_FIXED, fd, offset)`. |
| 49 | NtMapViewOfSectionEx | mmap | BSD 197 | 🔄 | Same, extended params stubbed. |
| 50 | NtUnmapViewOfSection | munmap | BSD 73 | → | `munmap(addr, size)`. Direct. |
| 51 | NtUnmapViewOfSectionEx | munmap | BSD 73 | 🔄 | Same. |
| 52 | NtAreMappedFilesTheSame | fstat (compare st_dev/st_ino) | BSD 189 | 🔄 | `fstat(fd1, &sb1); fstat(fd2, &sb2); return sb1.st_dev==sb2.st_dev && sb1.st_ino==sb2.st_ino`. |
| 53 | NtSetInformationVirtualMemory | madvise | BSD 75 | ≈ | `madvise(addr, len, MADV_DONTNEED)` for VM_PURGE, etc. Subset of info classes. |
| 54 | NtAllocateUserPhysicalPages | — | — | ❌ | No physical page API. Return STATUS_NOT_SUPPORTED. |
| 55 | NtFreeUserPhysicalPages | — | — | ❌ | Same. |
| 56 | NtMapUserPhysicalPages | — | — | ❌ | Same. |
| 57 | NtMapUserPhysicalPagesScatter | — | — | ❌ | Same. |

### 1.3 File I/O

| # | Windows NT API | macOS XNU API | XNU Syscall/Trap | Pair | Wine ntdll Implementation |
|---|---------------|---------------|-------------------|------|--------------------------|
| 58 | NtCreateFile | open / openat | BSD 5, 463 | → | `openat(dirfd, path, oflags, mode)`. Map GENERIC_READ/WRITE → O_RDONLY/O_RDWR. Map FILE_SHARE_* via Wine advisory locks. |
| 59 | NtOpenFile | open / openat | BSD 5, 463 | → | Same. |
| 60 | NtReadFile | read / pread | BSD 3, 153 | → | `pread(fd, buf, count, offset)` if async, else `read(fd, buf, count)`. |
| 61 | NtReadFileScatter | readv | BSD 120 | → | `readv(fd, iovec, iovcnt)`. Map scatter buffers to iovec. |
| 62 | NtWriteFile | write / pwrite | BSD 4, 154 | → | `pwrite(fd, buf, count, offset)` or `write(fd, buf, count)`. |
| 63 | NtWriteFileGather | writev | BSD 121 | → | `writev(fd, iovec, iovcnt)`. |
| 64 | NtClose | close | BSD 6 | → | `close(fd)` for file handles. Wine handle table cleanup for other types. |
| 65 | NtDeleteFile | unlink / unlinkat | BSD 10, 472 | → | `unlinkat(dirfd, path, 0)`. |
| 66 | NtFlushBuffersFile | fsync | BSD 95 | → | `fsync(fd)`. Direct. |
| 67 | NtFlushBuffersFileEx | fdatasync | BSD 187 | → | `fdatasync(fd)`. |
| 68 | NtDeviceIoControlFile | ioctl | BSD 54 | → | `ioctl(fd, cmd, data)`. Wine translates IOCTL codes to macOS ioctl commands. |
| 69 | NtFsControlFile | fsctl / ffsctl | BSD 242, 245 | ⚠️ | Subset of FSCTLs: FSCTL_SET_REPARSE_POINT → setxattr, FSCTL_GET_REPARSE_POINT → getxattr. |
| 70 | NtQueryInformationFile | fstat / fgetattrlist | BSD 189, 228 | → | `fgetattrlist(fd, &alist, buf, bufsize, options)` for rich attrs. `fstat()` for basic. Map FILE_INFORMATION_CLASS → attrlist fields. |
| 71 | NtSetInformationFile | fsetattrlist / fchmod / utimes | BSD 229, 15, 138 | ≈ | Map FileDispositionInformation → `unlinkat(REMOVE)`, FileEndOfFileInformation → `ftruncate()`, FileBasicInfo → `utimes()`. |
| 72 | NtQueryAttributesFile | stat / getattrlist | BSD 188, 220 | → | `getattrlist(path, &alist, ...)` → FILE_BASIC_INFORMATION. |
| 73 | NtQueryFullAttributesFile | stat | BSD 188 | → | `stat(path, &sb)` → FILE_NETWORK_OPEN_INFORMATION. |
| 74 | NtQueryDirectoryFile | getdirentries64 | BSD 344 | → | `getdirentries64(fd, buf, bufsize, &pos)` → FILE_DIRECTORY_INFORMATION. |
| 75 | NtQueryDirectoryFileEx | getdirentries64 | BSD 344 | → | Same with extended flags. |
| 76 | NtQueryEaFile | getxattr / fgetxattr | BSD 234 | ≈ | `fgetxattr(fd, name, buf, size, pos, 0)` maps to EA read. |
| 77 | NtSetEaFile | setxattr / fsetxattr | BSD 236 | ≈ | `fsetxattr(fd, name, buf, size, pos, 0)`. |
| 78 | NtQueryVolumeInformationFile | statfs / fstatfs | BSD 157 | → | `fstatfs(fd, &sf)` → FILE_FS_VOLUME_INFORMATION. |
| 79 | NtSetVolumeInformationFile | — | — | 🔄 | Most volume info classes are no-ops. |
| 80 | NtQueryQuotaInformationFile | quotactl | BSD 165 | ⚠️ | `quotactl(path, cmd, id, addr)`. Partial. |
| 81 | NtSetQuotaInformationFile | quotactl | BSD 165 | ⚠️ | Same. |
| 82 | NtNotifyChangeDirectoryFile | kqueue + EVFILT_VNODE | BSD 362, 363 | → | `kqueue()`, `kevent(kq, EVFILT_VNODE, EV_ADD|EV_CLEAR, NOTE_WRITE, ...)` → FILE_NOTIFY_INFORMATION. |
| 83 | NtNotifyChangeDirectoryFileEx | kqueue + EVFILT_VNODE | BSD 362, 363 | → | Same. |
| 84 | NtLockFile | flock / fcntl(F_SETLK) | BSD 131 | → | `flock(fd, LOCK_EX|LOCK_NB)` or `fcntl(fd, F_SETLK, &fl)`. |
| 85 | NtUnlockFile | flock / fcntl(F_SETLKW) | BSD 131 | → | `flock(fd, LOCK_UN)` or `fcntl(fd, F_UNLCK, &fl)`. |
| 86 | NtCancelIoFile | — | — | 🔄 | Wine cancels pending async I/O by closing or canceling the fd operation. |
| 87 | NtCancelIoFileEx | — | — | 🔄 | Same. |
| 88 | NtCancelSynchronousIoFile | — | — | 🔄 | Same. |
| 89 | NtCopyFileChunk | sendfile / copyfile | BSD 337, 227 | → | `sendfile(src_fd, dst_fd, offset, &nbytes, NULL, 0)` or `copyfile(src, dst, mode, flags)`. |

### 1.4 Synchronization

| # | Windows NT API | macOS XNU API | XNU Syscall/Trap | Pair | Wine ntdll Implementation |
|---|---------------|---------------|-------------------|------|--------------------------|
| 90 | NtCreateEvent | ulock / eventfd / kqueue | BSD 515, 362 | ≈ | Wine uses `ulock_wait`/`ulock_wake` for auto-reset events. For named events: file-backed in Wine prefix. |
| 91 | NtSetEvent | ulock_wake | BSD 516 | ≈ | `ulock_wake(UL_COMPARE_AND_WAIT, &event_state, 1)`. |
| 92 | NtSetEventBoostPriority | ulock_wake | BSD 516 | 🔄 | Same as NtSetEvent. Priority boost ignored. |
| 93 | NtSetEventEx | ulock_wake | BSD 516 | 🔄 | Same. |
| 94 | NtResetEvent | ulock_wait (try) | BSD 515 | 🔄 | Compare-and-swap the event state to nonsignaled. |
| 95 | NtClearEvent | (same as ResetEvent) | — | 🔄 | Same. |
| 96 | NtPulseEvent | ulock_wake + ulock_wait | BSD 516, 515 | 🔄 | Signal then immediately unsignal. Race-prone but matches NT semantics. |
| 97 | NtCreateEventPair | ulock × 2 | BSD 515 | 🔄 | Two ulock addresses. Wine emulates high/low pair. |
| 98 | NtSetHighEventPair | ulock_wake | BSD 516 | 🔄 | `ulock_wake(UL_COMPARE_AND_WAIT, &pair->high, 1)`. |
| 99 | NtSetLowEventPair | ulock_wake | BSD 516 | 🔄 | `ulock_wake(UL_COMPARE_AND_WAIT, &pair->low, 1)`. |
| 100 | NtWaitHighEventPair | ulock_wait | BSD 515 | 🔄 | `ulock_wait(UL_COMPARE_AND_WAIT, &pair->high, 0, timeout)`. |
| 101 | NtWaitLowEventPair | ulock_wait | BSD 515 | 🔄 | Same for low. |
| 102 | NtSetHighWaitLowEventPair | ulock_wake + ulock_wait | BSD 516, 515 | 🔄 | Two-step. |
| 103 | NtSetLowWaitHighEventPair | ulock_wake + ulock_wait | BSD 516, 515 | 🔄 | Two-step. |
| 104 | NtCreateMutant | os_unfair_lock / psynch_mutexwait | BSD 301 | ≈ | For kernel-shared: `psynch_mutexwait(addr, mgen, ugen, tid, flags)`. For process-local: `os_unfair_lock`. |
| 105 | NtReleaseMutant | os_unfair_unlock / psynch_mutexdrop | BSD 302 | ≈ | `psynch_mutexdrop(addr, mgen, ugen, tid, flags)`. |
| 106 | NtOpenMutant | — | — | 🔄 | Named mutex lookup in Wine prefix directory. |
| 107 | NtCreateSemaphore | semaphore_create / dispatch_semaphore | Mach IPC | → | `semaphore_create(task, &sem, SYNC_POLICY_FIFO, count)`. Or `dispatch_semaphore_create(count)`. |
| 108 | NtReleaseSemaphore | semaphore_signal / dispatch_semaphore_signal | Mach trap 9 | → | `semaphore_signal(sem)`. |
| 109 | NtOpenSemaphore | sem_open | BSD 268 | ≈ | `sem_open(name, 0)` for named semaphores. |
| 110 | NtCreateTimer | mk_timer_create | Mach trap 45 | → | `mk_timer_create()` returns Mach port. |
| 111 | NtCreateTimer2 | mk_timer_create + mk_timer_arm_leeway | Mach trap 45, 48 | ≈ | `mk_timer_create()` then `mk_timer_arm_leeway(port, flags, expire, leeway)`. |
| 112 | NtSetTimer | mk_timer_arm | Mach trap 47 | → | `mk_timer_arm(port, deadline)`. |
| 113 | NtSetTimer2 | mk_timer_arm_leeway | Mach trap 48 | → | `mk_timer_arm_leeway(port, flags, deadline, leeway)`. |
| 114 | NtSetTimerEx | mk_timer_arm_leeway | Mach trap 48 | → | Same. |
| 115 | NtCancelTimer | mk_timer_cancel | Mach trap 49 | → | `mk_timer_cancel(port, &result_time)`. |
| 116 | NtCancelTimer2 | mk_timer_cancel | Mach trap 49 | → | Same. |
| 117 | NtCreateIRTimer | — | — | 🔄 | Wine stubs with mk_timer internally. |
| 118 | NtSetIRTimer | — | — | 🔄 | Same. |
| 119 | NtReadStateTimer | mk_timer_cancel (query) | Mach trap 49 | ⚠️ | `mk_timer_cancel(port, &remaining)` reads remaining without canceling. Re-arm if needed. |
| 120 | NtCreateKeyedEvent | ulock | BSD 515 | → | Wine uses `ulock_wait`/`ulock_wake` with keyed address. |
| 121 | NtWaitForKeyedEvent | ulock_wait | BSD 515 | → | `ulock_wait(UL_COMPARE_AND_WAIT64, key_addr, 0, timeout)`. |
| 122 | NtReleaseKeyedEvent | ulock_wake | BSD 516 | → | `ulock_wake(UL_COMPARE_AND_WAIT64, key_addr, 1)`. |
| 123 | NtOpenKeyedEvent | — | — | 🔄 | Named lookup in Wine prefix. |
| 124 | NtCreateIoCompletion | kqueue | BSD 362 | → | `kqueue()` → IOCP. Wine posts completion packets via `kevent(EVFILT_USER)`. |
| 125 | NtOpenIoCompletion | — | — | 🔄 | Named lookup. |
| 126 | NtCreateWaitablePort | mach_port_allocate | Mach trap 22 | ≈ | `mach_port_allocate(task, MACH_PORT_RIGHT_RECEIVE, &name)`. |
| 127 | NtCreateWaitCompletionPacket | — | — | 🔄 | Wine stubs with kevent. |
| 128 | NtWaitForSingleObject | kevent / select / poll | BSD 363, 93, 230 | → | `kevent(kq, NULL, 0, &ev, 1, &timeout)`. Dispatches based on object type. |
| 129 | NtWaitForMultipleObjects | kevent | BSD 363 | → | `kevent(kq, NULL, 0, events, count, &timeout)`. |
| 130 | NtWaitForMultipleObjects32 | kevent | BSD 363 | 🔄 | Same as #129. |
| 131 | NtSignalAndWaitForSingleObject | semaphore_wait_signal | Mach trap 13 | → | `semaphore_wait_signal(wait_sem, signal_sem)`. Direct. |
| 132 | NtWaitForAlertByThreadId | ulock_wait | BSD 515 | 🔄 | `ulock_wait(UL_COMPARE_AND_WAIT, &tid_addr, 0, timeout)`. |

### 1.5 IPC / LPC / ALPC

| # | Windows NT API | macOS XNU API | XNU Syscall/Trap | Pair | Wine ntdll Implementation |
|---|---------------|---------------|-------------------|------|--------------------------|
| 133 | NtCreatePort | mach_port_allocate | Mach trap 22 | → | `mach_port_allocate(task, MACH_PORT_RIGHT_RECEIVE, &name)`. Configure queue depth. |
| 134 | NtCreateWaitablePort | mach_port_allocate | Mach trap 22 | → | Same. |
| 135 | NtConnectPort | mach_msg (send+receive) | Mach trap 6 | → | `mach_msg(msg, MACH_SEND_MSG|MACH_RCV_MSG, ...)` to bootstrap port. |
| 136 | NtSecureConnectPort | mach_msg + bootstrap_check_in | Mach trap 6 | ≈ | `bootstrap_check_in(bootstrap_port, name, &port)` then `mach_msg()`. |
| 137 | NtListenPort | mach_msg (receive only) | Mach trap 6 | → | `mach_msg(msg, MACH_RCV_MSG, 0, rcv_size, port, timeout, notify)`. |
| 138 | NtAcceptConnectPort | mach_msg (reply) | Mach trap 6 | ≈ | Send acceptance message back via Mach IPC. |
| 139 | NtCompleteConnectPort | mach_msg | Mach trap 6 | 🔄 | Send completion message. |
| 140 | NtRequestPort | mach_msg (send) | Mach trap 6 | → | `mach_msg(msg, MACH_SEND_MSG, send_size, 0, 0, 0, 0)`. |
| 141 | NtRequestWaitReplyPort | mach_msg (send+receive) | Mach trap 6 | → | `mach_msg(msg, MACH_SEND_MSG|MACH_RCV_MSG, send_size, rcv_size, port, timeout, 0)`. |
| 142 | NtReplyPort | mach_msg (send) | Mach trap 6 | → | `mach_msg(reply, MACH_SEND_MSG, ...)`. |
| 143 | NtReplyWaitReceivePort | mach_msg | Mach trap 6 | → | `mach_msg(reply, MACH_SEND_MSG|MACH_RCV_MSG, ...)`. |
| 144 | NtReplyWaitReceivePortEx | mach_msg | Mach trap 6 | → | Same with extended timeout. |
| 145 | NtReplyWaitReplyPort | mach_msg | Mach trap 6 | 🔄 | Two-phase Mach message. |
| 146 | NtReadRequestData | — | — | 🔄 | Wine embeds data inline in Mach message body. |
| 147 | NtWriteRequestData | — | — | 🔄 | Same. |
| 148 | NtImpersonateClientOfPort | — | — | ❌ | No impersonation. Return STATUS_NOT_IMPLEMENTED for server-side LPC. |
| 149 | NtAlpcCreatePort | mach_port_allocate | Mach trap 22 | ≈ | `mach_port_allocate(task, MACH_PORT_RIGHT_RECEIVE, &name)`. |
| 150 | NtAlpcConnectPort | mach_msg | Mach trap 6 | ≈ | `mach_msg()` with connection request. |
| 151 | NtAlpcConnectPortEx | mach_msg | Mach trap 6 | 🔄 | Same, extended attrs stubbed. |
| 152 | NtAlpcAcceptConnectPort | mach_msg | Mach trap 6 | ≈ | Reply with acceptance. |
| 153 | NtAlpcDisconnectPort | mach_port_mod_refs (deallocate) | Mach trap 24 | ≈ | `mach_port_mod_refs(task, port, MACH_PORT_RIGHT_RECEIVE, -1)`. |
| 154 | NtAlpcSendWaitReceivePort | mach_msg | Mach trap 6 | ≈ | `mach_msg(msg, MACH_SEND_MSG|MACH_RCV_MSG, ...)`. |
| 155 | NtAlpcCreatePortSection | mmap (shared) | BSD 197 | 🔄 | `mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)` for ALPC section. |
| 156 | NtAlpcCreateSectionView | mmap | BSD 197 | 🔄 | Same. |
| 157 | NtAlpcCreateResourceReserve | — | — | 🔄 | Wine stubs — allocates internal quota. |
| 158 | NtAlpcCreateSecurityContext | — | — | ❌ | No security context in Mach IPC. Stub. |
| 159-170 | NtAlpc* (10 remaining) | — | — | 🔄 | Various ALPC management functions. Wine stubs each with appropriate status codes. |
| 171 | NtRegisterThreadTerminatePort | mach_port_request_notification | Mach trap 57 | ≈ | `mach_port_request_notification(task, port, MACH_NOTIFY_DEAD_NAME, ...)`. |

### 1.6 Security / Token

| # | Windows NT API | macOS XNU API | XNU Syscall/Trap | Pair | Wine ntdll Implementation |
|---|---------------|---------------|-------------------|------|--------------------------|
| 172 | NtCreateToken | — | — | ❌ | Wine creates fake TOKEN object in userspace with uid/gid/groups data. |
| 173 | NtCreateTokenEx | — | — | ❌ | Same. |
| 174 | NtCreateLowBoxToken | — | — | ❌ | Fake token with restricted SIDs. |
| 175 | NtDuplicateToken | — | — | 🔄 | Copy Wine's internal token struct. |
| 176 | NtOpenProcessToken | getuid / getgid / getgroups | BSD 24, 25, 79 | 🔄 | `getuid()` etc. to populate fake token. |
| 177 | NtOpenProcessTokenEx | (same) | — | 🔄 | Same. |
| 178 | NtOpenThreadToken | — | — | 🔄 | Thread tokens are always NULL unless impersonating. |
| 179 | NtOpenThreadTokenEx | — | — | 🔄 | Same. |
| 180 | NtAdjustGroupsToken | setgroups | BSD 80 | 🔄 | `setgroups(count, groups)` + update Wine token. |
| 181 | NtAdjustPrivilegesToken | — | — | 🔄 | Wine tracks privilege enable/disable internally. No kernel mapping. |
| 182 | NtCompareTokens | — | — | 🔄 | Compare Wine token structs. |
| 183 | NtFilterToken | — | — | 🔄 | Create restricted Wine token. |
| 184 | NtFilterTokenEx | — | — | 🔄 | Same. |
| 185 | NtAccessCheck | mac_vnode_check_access | MACF | ≈ | `access(path, mode)` for simple checks. `mac_vnode_check_access` via MACF for kernel path. |
| 186 | NtPrivilegeCheck | mac_priv_check | MACF | ≈ | `mac_priv_check(cred, priv)`. Map NT privileges to POSIX capabilities. |
| 187 | NtQueryInformationToken | — | — | 🔄 | Return data from Wine's internal token struct. Map TokenUser → uid, TokenGroups → groups. |
| 188 | NtSetInformationToken | — | — | 🔄 | Update Wine token struct. |
| 189 | NtQuerySecurityObject | fstat + acl_get_fd | BSD 189 | ≈ | `acl_get_fd(fd)` → SECURITY_DESCRIPTOR. Map POSIX ACL → NT DACL. |
| 190 | NtSetSecurityObject | fchmod + acl_set_fd | BSD 15 | ≈ | `acl_set_fd(fd, acl)` from NT DACL → POSIX ACL. |
| 191-212 | Nt*Token/Security (22 remaining) | — | — | 🔄 | Wine stubs with STATUS_NOT_IMPLEMENTED or fakes data from uid/gid. |

### 1.7 Process / Job / System Information

| # | Windows NT API | macOS XNU API | XNU Syscall/Trap | Pair | Wine ntdll Implementation |
|---|---------------|---------------|-------------------|------|--------------------------|
| 213 | NtOpenProcess | proc_info + task_for_pid | BSD 336, Mach trap 50 | → | `proc_info(PROC_INFO_CALL_PIDINFO, pid, ...)` to find process. `task_for_pid(mach_task_self(), pid, &task)` for task port. Requires task_for_pid entitlement. |
| 214 | NtOpenThread | thread_act via task_threads | Mach IPC | → | `task_threads(task, &threads, &count)` → iterate to find TID. |
| 215 | NtQueryInformationProcess | proc_info | BSD 336 | ≈ | `proc_info(PROC_INFO_CALL_PIDINFO, pid, PROC_PIDTBSDINFO, ...)` for basic. `PROC_PIDTASKINFO` for memory. Map info classes. |
| 216 | NtSetInformationProcess | process_policy | BSD 323 | ≈ | `process_policy(scope, action, policy, subtype, attr, pid, tid)` for subset. |
| 217 | NtQueryInformationThread | thread_info | Mach IPC | → | `thread_info(thread, THREAD_BASIC_INFO, &info, &count)` → THREAD_BASIC_INFORMATION. |
| 218 | NtSetInformationThread | thread_policy_set | Mach IPC | → | `thread_policy_set(thread, flavor, policy, count)` for priority/affinity. |
| 219 | NtQuerySystemInformation | sysctl + proc_info | BSD 202, 336 | ≈ | `sysctl(CTL_KERN, KERN_PROC, ...)` for process list. `proc_info(PROC_INFO_CALL_LISTPIDS, ...)` for PID enumeration. Map SystemProcessInformation → proc_info. |
| 220 | NtQuerySystemInformationEx | sysctl | BSD 202 | ≈ | Same. |
| 221 | NtSetSystemInformation | sysctl (write) | BSD 202 | ⚠️ | `sysctl(name, namelen, NULL, 0, &new, newlen)`. Subset writable. |
| 222 | NtCreateJobObject | coalition | BSD 458 | ≈ | `coalition(COALITION_CREATE, &cid, flags)` creates job-like container. |
| 223 | NtOpenJobObject | — | — | 🔄 | Lookup in Wine's internal job list. |
| 224 | NtAssignProcessToJobObject | coalition (join) | BSD 458 | ≈ | `coalition(COALITION_JOIN, &cid, pid)`. |
| 225 | NtQueryInformationJobObject | coalition_info | BSD 459 | ≈ | `coalition_info(COALITION_INFO, &cid, &buffer, &size)`. Map JobObjectBasicAccountingInformation → coalition_info. |
| 226 | NtSetInformationJobObject | — | — | 🔄 | Wine tracks job limits internally. Coalition supports some resource limits. |
| 227 | NtIsProcessInJob | coalition_info | BSD 459 | ⚠️ | Check if process is in coalition. Partial. |
| 228 | NtGetNextProcess | proc_info (PID list) | BSD 336 | → | `proc_info(PROC_INFO_CALL_LISTPIDS, ...)` to enumerate PIDs. |
| 229 | NtGetNextThread | task_threads | Mach IPC | → | `task_threads(task, &threads, &count)` to enumerate. |
| 230 | NtGetCurrentProcessorNumber | — | — | 🔄 | `sched_getcpu()` or `mach_thread_self()` + affinity query. |

### 1.8 Debugging

| # | Windows NT API | macOS XNU API | XNU Syscall/Trap | Pair | Wine ntdll Implementation |
|---|---------------|---------------|-------------------|------|--------------------------|
| 231 | NtCreateDebugObject | — | — | ❌ | Wine creates internal debug object (linked list of events + waitable). |
| 232 | NtDebugActiveProcess | ptrace (PT_ATTACH) | BSD 26 | → | `ptrace(PT_ATTACH, pid, 0, 0)`. Then `task_for_pid()` for full access. |
| 233 | NtWaitForDebugEvent | waitid + Mach exceptions | BSD 173, Mach IPC | ≈ | Wine routes Mach exceptions (EXC_BAD_ACCESS, EXC_BREAKPOINT, etc.) → DEBUG_EVENT. Uses `mach_msg` on exception port. |
| 234 | NtDebugContinue | ptrace (PT_CONTINUE) | BSD 26 | → | `ptrace(PT_CONTINUE, pid, addr, signal)`. |
| 235 | NtRemoveProcessDebug | — | — | 🔄 | Wine detaches: `ptrace(PT_DETACH, pid, 0, 0)`. |
| 236 | NtSetInformationDebugObject | — | — | 🔄 | Update Wine's internal debug object flags. |
| 237 | NtSystemDebugControl | — | — | ❌ | Return STATUS_ACCESS_DENIED. No kernel debug control from userspace. |

### 1.9 Registry

| # | Windows NT API | macOS XNU API | XNU Syscall/Trap | Pair | Wine ntdll Implementation |
|---|---------------|---------------|-------------------|------|--------------------------|
| 238-279 | NtCreateKey through NtThawRegistry (42 APIs) | — | — | 🔄 | **All 42 registry APIs → Wine file-backed registry.** Wine stores registry hives in `~/.wine/*.reg` (text format) and `~/.wine/*.reg.conf` (binary). NtCreateKey → create dir/file. NtSetValueKey → write value to file. NtQueryValueKey → read from file. NtEnumerateKey → readdir. NtDeleteKey → unlink. Full implementation exists in Wine's `server/registry.c`. |

### 1.10 Object Manager

| # | Windows NT API | macOS XNU API | XNU Syscall/Trap | Pair | Wine ntdll Implementation |
|---|---------------|---------------|-------------------|------|--------------------------|
| 280 | NtCreateDirectoryObject | mkdir | BSD 136 | 🔄 | `mkdir(wine_prefix + "\\??\\" + name, mode)`. Wine maps NT object namespace to prefix directory tree. |
| 281 | NtCreateDirectoryObjectEx | mkdir | BSD 136 | 🔄 | Same. |
| 282 | NtOpenDirectoryObject | opendir | BSD 196 | 🔄 | `opendir(wine_prefix + path)`. |
| 283 | NtCreateSymbolicLinkObject | symlink | BSD 57 | → | `symlink(target, link_path)` in Wine prefix. |
| 284 | NtOpenSymbolicLinkObject | readlink | BSD 58 | → | `readlink(path, buf, size)`. |
| 285 | NtQuerySymbolicLinkObject | readlink | BSD 58 | → | Same. |
| 286 | NtCreatePrivateNamespace | mkdir (in prefix) | BSD 136 | 🔄 | Wine creates private dir in prefix. |
| 287 | NtOpenPrivateNamespace | opendir | — | 🔄 | Wine opens private dir. |
| 288 | NtDeletePrivateNamespace | rmdir | BSD 137 | 🔄 | `rmdir(path)`. |
| 289 | NtQueryObject | — | — | ❌ | Return fake data from Wine's internal handle table. |
| 290 | NtSetInformationObject | — | — | 🔄 | Update Wine handle metadata. |
| 291 | NtDuplicateObject | dup / dup2 / mach_port_insert_right | BSD 41, 90, Mach trap 26 | ≈ | `dup2(oldfd, newfd)` for fd-backed handles. `mach_port_insert_right(task, name, right, poly)` for Mach port handles. |
| 292 | NtMakePermanentObject | — | — | 🔄 | No-op. Wine objects are always temporary. |
| 293 | NtMakeTemporaryObject | — | — | 🔄 | No-op. |
| 294 | NtCompareObjects | — | — | 🔄 | Compare Wine handle table entries. |

### 1.11 Driver / Module

| # | Windows NT API | macOS XNU API | XNU Syscall/Trap | Pair | Wine ntdll Implementation |
|---|---------------|---------------|-------------------|------|--------------------------|
| 295 | NtLoadDriver | kext_load | IOKit | ❌ | Return STATUS_ACCESS_DENIED. Wine cannot load kernel drivers. |
| 296 | NtUnloadDriver | kext_unload | IOKit | ❌ | Same. |
| 297 | NtAddBootEntry through NtFilterBootOption | — | — | 🔄 | All stubbed. Return STATUS_NOT_SUPPORTED. |
| 298 | NtLoadHotPatch / NtManageHotPatch | — | — | ❌ | Stub STATUS_NOT_SUPPORTED. |
| 299-303 | Nt*Enclave (5 APIs) | — | — | ❌ | SGX only. Stub STATUS_NOT_SUPPORTED. |

### 1.12 Transaction Manager (KTM)

| # | Windows NT API | macOS XNU API | XNU Syscall/Trap | Pair | Wine ntdll Implementation |
|---|---------------|---------------|-------------------|------|--------------------------|
| 304-343 | Nt*Transaction* (40 APIs) | — | — | 🔄 | All KTM APIs stubbed. Wine returns STATUS_NOT_IMPLEMENTED. No KTM equivalent on macOS. Most apps don't use KTM directly. |

### 1.13 Misc System

| # | Windows NT API | macOS XNU API | XNU Syscall/Trap | Pair | Wine ntdll Implementation |
|---|---------------|---------------|-------------------|------|--------------------------|
| 344 | NtQuerySystemTime | gettimeofday / mach_absolute_time | BSD 116 | → | `gettimeofday(&tv, NULL)` or `mach_absolute_time()`. Convert to 100ns NT ticks. |
| 345 | NtSetSystemTime | settimeofday | BSD 122 | → | `settimeofday(&tv, NULL)`. Requires root. |
| 346 | NtQueryTimerResolution | — | — | 🔄 | Return 1ms resolution (mach tick granularity). |
| 347 | NtSetTimerResolution | — | — | 🔄 | No-op. |
| 348 | NtQueryPerformanceCounter | mach_absolute_time | Mach trap -3 | → | `mach_absolute_time()` → LARGE_INTEGER. |
| 349 | NtRaiseException | raise / kill | BSD 37, 328 | ≈ | `__pthread_kill(mach_thread_self(), SIGABRT)` or `raise(SIGILL)`. |
| 350 | NtShutdownSystem | reboot | BSD 55 | → | `reboot(RB_HALT_SYSTEM)` or `reboot(RB_POWER_OFF)`. |
| 351 | NtPowerInformation | — | — | 🔄 | Return fake power info. |
| 352 | NtPlugPlayControl | IOKit matching | IOKit | 🔄 | Wine maps PnP to IOKit registry queries. |
| 353 | NtTraceEvent | kdebug_trace | BSD 179, 180 | ≈ | `kdebug_trace(code, arg1, arg2, arg3, arg4)`. Map ETW events → kdebug. |
| 354 | NtTraceControl | kdebug_typefilter | BSD 177 | ⚠️ | `kdebug_typefilter(&addr, &size)`. Partial. |
| 355 | NtCreateWorkerFactory | workq_open | BSD 367 | ≈ | `workq_open()` for thread pool. Wine maps TpAllocPool → workq. |
| 356 | NtReleaseWorkerFactoryWorker | workq_kernreturn | BSD 368 | ≈ | `workq_kernreturn(WQOPS_THREAD_RETURN, ...)`. |
| 357 | NtWaitForWorkViaWorkerFactory | workq_kernreturn | BSD 368 | ≈ | Same. |
| 358 | NtGetCachedSigningLevel | csops | BSD 169 | → | `csops(pid, CS_OPS_GETSIGNINGINFO, buf, size)` → signing level. |
| 359 | NtSetCachedSigningLevel | — | — | ❌ | SIP prevents. Return STATUS_ACCESS_DENIED. |
| 360 | NtCallbackReturn | — | — | 🔄 | Return from callback context. Wine-specific. |
| 361 | NtCreatePagingFile | macx_swapon | Mach trap 35 | ≈ | `macx_swapon(filename, flags, size, priority)`. |
| 362 | NtAllocateLocallyUniqueId | — | — | 🔄 | Wine allocates from internal LUID counter. |
| 363 | NtAllocateReserveObject | — | — | 🔄 | Wine creates internal event object. |
| 364-385 | Nt*Wnf*/Nt*CpuPartition*/Nt*Partition*/Nt*IoRing*/etc. | — | — | 🔄 | Stub STATUS_NOT_IMPLEMENTED. |

---

## Part 2: Kernel Executive Functions (ntoskrnl.exe exports)

These are kernel-mode functions called by drivers. Wine does NOT call these directly — they're listed for understanding what kernel-level anti-cheat drivers need.

### 2.1 Memory Manager (MmXxx) — 85 functions

| NT Function | macOS XNU Equivalent | Pair | Notes for Wine (if needed) |
|-------------|---------------------|------|---------------------------|
| MmAllocateContiguousMemory | IOMemoryDescriptor::withPhysicalRange | ❌ | IOKit only. Not accessible from Wine. |
| MmAllocateNonCachedMemory | mmap(MAP_SHARED, VM_PROT_NONE cache) | ❌ | |
| MmBuildMdlForNonPagedPool | IOMemoryDescriptor | ❌ | |
| MmCopyMemory | mach_vm_read + mach_vm_write | ≈ | Userspace equivalent via task port. |
| MmCreateMdl | IOMemoryDescriptor | ❌ | |
| MmFreeContiguousMemory | IOMemoryDescriptor::release | ❌ | |
| MmGetPhysicalAddress | IOMemoryCursor::getPhysicalSegments | ❌ | |
| MmGetSystemAddressForMdl | IOMemoryDescriptor::getBytes | ❌ | |
| MmIsAddressValid | mach_vm_region | ≈ | Userspace: check if region exists. |
| MmMapIoSpace | IODeviceMemory | ❌ | |
| MmMapLockedPages | IOMemoryDescriptor::map | ❌ | |
| MmProbeAndLockPages | IOMemoryDescriptor::prepare | ❌ | |
| MmProtectVirtualMemory | mprotect | → | Userspace: `mprotect()`. |
| MmSecureVirtualMemory | — | 🔄 | No equivalent. |
| MmSizeOfMdl | sizeof(IOMemoryDescriptor) | ❌ | |
| MmUnmapIoSpace | IOMemoryDescriptor::release | ❌ | |
| MmUnmapLockedPages | IOMemoryDescriptor::complete | ❌ | |
| *(65 remaining MmXxx)* | — | ❌ | All kernel-mode only. Not callable from Wine. |

### 2.2 Process/Thread Manager (PsXxx) — 66 functions

| NT Function | macOS XNU Equivalent | Pair | Notes |
|-------------|---------------------|------|-------|
| PsCreateSystemThread | bsdthread_create | → | `bsdthread_create(func, arg, stack, pthread, flags)`. |
| PsGetCurrentProcess | mach_task_self | → | `mach_task_self()` returns task port. |
| PsGetCurrentProcessId | getpid | → | `getpid()`. |
| PsGetCurrentThread | mach_thread_self | → | `mach_thread_self()`. |
| PsGetCurrentThreadId | thread_selfid | → | `thread_selfid()`. |
| PsGetCurrentThreadTeb | — | 🔄 | Wine reads from ARM TPIDR_EL0 register (TLS pointer). |
| PsGetProcessCreateTimeQuadPart | proc_info (PROC_PIDTBSDINFO) | ≈ | `proc_info(PROC_INFO_CALL_PIDINFO, pid, PROC_PIDTBSDINFO, ...)`. |
| PsGetProcessId | getpid | → | `getpid()`. |
| PsGetThreadId | thread_selfid | → | `thread_selfid()`. |
| PsGetThreadProcessId | getpid | → | `getpid()`. |
| PsGetVersion | sysctl (KERN_OSRELEASE) | → | `sysctl(CTL_KERN, KERN_OSRELEASE, ...)`. |
| PsIsSystemThread | — | 🔄 | Wine checks if thread is from workq. |
| PsQueryTotalCycleTimeProcess | proc_info (PROC_PIDTASKINFO) | ≈ | `proc_info(PROC_INFO_CALL_PIDINFO, pid, PROC_PIDTASKINFO, ...)`. |
| PsSetCreateProcessNotifyRoutineEx2 | mac_proc_check_fork / EndpointSecurity | ⚠️ | **Anti-cheat critical.** Requires EndpointSecurity system extension. |
| PsSetCreateThreadNotifyRoutineEx | — | ❌ | **No thread creation callback.** |
| PsSetLoadImageNotifyRoutineEx | mac_vnode_check_exec / EndpointSecurity | ⚠️ | EndpointSecurity ES_EVENT_TYPE_NOTIFY_EXEC. |
| PsTerminateSystemThread | bsdthread_terminate | → | `bsdthread_terminate(mach_thread, stack, stack_size, kport)`. |
| *(49 remaining PsXxx)* | — | — | Silo/context management. Not needed for Wine. |

### 2.3 I/O Manager (IoXxx) — 174 functions

| NT Function Category | macOS XNU Equivalent | Pair | Notes |
|---------------------|---------------------|------|-------|
| IoCreateDevice | IOService::registerService | ❌ | IOKit driver model is completely different. |
| IoDeleteDevice | IOService::terminate | ❌ | |
| IoCreateSymbolicLink | symlink | → | `symlink(target, link)` in /dev. |
| IoDeleteSymbolicLink | unlink | → | `unlink(path)`. |
| IoCreateNotificationEvent | kqueue + EVFILT_USER | ≈ | `kqueue()`, `kevent(kq, EVFILT_USER, EV_ADD, ...)`. |
| IoCreateSynchronizationEvent | ulock / dispatch_semaphore | ≈ | Auto-reset event. |
| IoAllocateWorkItem | dispatch_async_f | ≈ | GCD work item. |
| IoQueueWorkItem | dispatch_async_f | ≈ | `dispatch_async_f(queue, context, func)`. |
| IoConnectInterrupt | IOInterruptEventSource | ❌ | IOKit interrupt. |
| IoAllocateIrp / IoCompleteRequest / IoCallDriver | — | ❌ | IRP model has no XNU equivalent. |
| IoCreateFile | open / openat | → | `openat(dirfd, path, flags, mode)`. |
| IoDeviceIoControlFile | ioctl | → | `ioctl(fd, cmd, data)`. |
| IoGetCurrentProcess | mach_task_self | → | `mach_task_self()`. |
| *(remaining IoXxx)* | — | ❌ | IRP/device/driver model. No Wine mapping. |

### 2.4 Object Manager (ObXxx) — 15 functions

| NT Function | macOS XNU Equivalent | Pair | Wine Implementation |
|-------------|---------------------|------|-------------------|
| ObCloseHandle | close / mach_port_deallocate | → | `close(fd)` or `mach_port_deallocate(task, name)`. |
| ObDereferenceObject | — | 🔄 | Decrement refcount in Wine handle table. |
| ObReferenceObjectByHandle | — | 🔄 | Look up Wine handle table, validate type, return kernel object pointer. |
| ObReferenceObjectByPointer | — | 🔄 | Increment refcount. |
| ObRegisterCallbacks | mac_proc_check_get_task | ⚠️ | **Anti-cheat critical.** Partial via MACF. Full requires EndpointSecurity. |
| ObUnRegisterCallbacks | — | 🔄 | Unregister MACF hook or EndpointSecurity subscription. |
| ObGetObjectSecurity | fstat + acl_get_fd | ≈ | `acl_get_fd(fd)` → SECURITY_DESCRIPTOR. |
| ObReleaseObjectSecurity | acl_free | ≈ | `acl_free(acl)`. |

### 2.5 Security Reference Monitor (SeXxx) — 7 functions

| NT Function | macOS XNU Equivalent | Pair | Wine Implementation |
|-------------|---------------------|------|-------------------|
| SeAccessCheck | mac_vnode_check_access / access | ≈ | `access(path, R_OK|W_OK|X_OK)`. |
| SeAssignSecurity | — | 🔄 | Wine creates default SECURITY_DESCRIPTOR from parent. |
| SeFreePrivileges | — | 🔄 | Free Wine privilege struct. |
| SeSinglePrivilegeCheck | mac_priv_check | ≈ | `mac_priv_check(cred, priv)`. |
| SeValidSecurityDescriptor | — | 🔄 | Validate in-memory SECURITY_DESCRIPTOR layout. |

### 2.6 Configuration Manager (CmXxx) — 9 functions

| NT Function | macOS XNU Equivalent | Pair | Wine Implementation |
|-------------|---------------------|------|-------------------|
| CmRegisterCallback | — | ❌ | No registry callback on macOS. Wine stubs. |
| CmRegisterCallbackEx | — | ❌ | Same. |
| CmUnRegisterCallback | — | 🔄 | Remove from Wine callback list. |
| CmCallbackGetKeyObjectID | — | 🔄 | Return Wine-internal key ID. |
| CmGetCallbackVersion | — | 🔄 | Return version 1. |
| CmSetCallbackObjectContext | — | 🔄 | Set Wine callback context. |

### 2.7 Executive Library (ExXxx) — 104 functions

| NT Function Category | macOS XNU Equivalent | Pair | Notes |
|---------------------|---------------------|------|-------|
| ExAllocatePool/ExAllocatePool2/ExAllocatePool3 | malloc / kalloc | ≈ | Userspace: `malloc()`. Kernel: `kalloc()`. |
| ExFreePool | free | → | `free()`. |
| ExInitializeFastMutex | os_unfair_lock | → | `os_unfair_lock_t`. |
| ExAcquireFastMutex | os_unfair_lock_lock | → | `os_unfair_lock_lock(&lock)`. |
| ExReleaseFastMutex | os_unfair_lock_unlock | → | `os_unfair_lock_unlock(&lock)`. |
| ExInitializePushLock | os_unfair_lock | → | Same. |
| ExAcquirePushLockExclusive | os_unfair_lock_lock | → | Exclusive = unfair lock. |
| ExAcquirePushLockShared | pthread_rwlock_rdlock | ≈ | `pthread_rwlock_rdlock(&rw)`. |
| ExInitializeResourceLite | pthread_rwlock_init | → | `pthread_rwlock_init(&rw, NULL)`. |
| ExAcquireResourceExclusiveLite | pthread_rwlock_wrlock | → | `pthread_rwlock_wrlock(&rw)`. |
| ExAcquireResourceSharedLite | pthread_rwlock_rdlock | → | `pthread_rwlock_rdlock(&rw)`. |
| ExReleaseResourceLite | pthread_rwlock_unlock | → | `pthread_rwlock_unlock(&rw)`. |
| ExInitializeRundownProtection | — | 🔄 | Wine uses atomic refcount. |
| ExAcquireRundownProtection | __sync_fetch_and_add | 🔄 | `__atomic_fetch_add(&count, 1, __ATOMIC_ACQ_REL)`. |
| ExReleaseRundownProtection | __sync_fetch_and_sub | 🔄 | `__atomic_fetch_sub(&count, 1, __ATOMIC_ACQ_REL)`. |
| ExAllocateTimer | mk_timer_create | → | `mk_timer_create()`. |
| ExDeleteTimer | mk_timer_destroy | → | `mk_timer_destroy(port)`. |
| ExSetTimer | mk_timer_arm | → | `mk_timer_arm(port, deadline)`. |
| ExCancelTimer | mk_timer_cancel | → | `mk_timer_cancel(port, &result)`. |
| ExInitializeSListHead | OSAtomicFIFOQueue | ≈ | Lock-free linked list. |
| ExInterlockedInsertHeadList | OSAtomicEnqueue | ≈ | `OSAtomicEnqueue(&queue, &entry, offset)`. |
| ExInterlockedPopEntrySList | OSAtomicDequeue | ≈ | `OSAtomicDequeue(&queue, offset, &entry)`. |
| ExUuidCreate | uuid_generate | → | `uuid_generate(out)`. |
| ExGetPreviousMode | — | 🔄 | Wine tracks user vs kernel mode per thread. |
| ExNotifyCallback / ExRegisterCallback | dispatch_async_f | ≈ | `dispatch_async_f(queue, context, callback)`. |
| *(remaining ExXxx)* | — | 🔄 | Various. Wine implements via POSIX/GCD equivalents. |

### 2.8 Core Kernel Library (KeXxx)

| NT Function Category | macOS XNU Equivalent | Pair | Notes |
|---------------------|---------------------|------|-------|
| KeInitializeSpinLock / KeAcquireSpinLock | os_unfair_lock / hw_lock | ≈ | `os_unfair_lock_lock(&lock)`. |
| KeReleaseSpinLock | os_unfair_unlock | → | `os_unfair_lock_unlock(&lock)`. |
| KeInitializeEvent | ulock | ≈ | `UL_COMPARE_AND_WAIT` on address. |
| KeSetEvent | ulock_wake | ≈ | `ulock_wake(UL_COMPARE_AND_WAIT, &addr, 1)`. |
| KeResetEvent | CAS to 0 | 🔄 | `__sync_val_compare_and_swap(&state, 1, 0)`. |
| KeInitializeMutex | os_unfair_lock | → | `os_unfair_lock_t`. |
| KeReleaseMutex | os_unfair_unlock | → | `os_unfair_lock_unlock(&lock)`. |
| KeInitializeSemaphore | semaphore_create | → | `semaphore_create(task, &sem, SYNC_POLICY_FIFO, count)`. |
| KeReleaseSemaphore | semaphore_signal | → | `semaphore_signal(sem)`. |
| KeInitializeTimer | mk_timer_create | → | `mk_timer_create()`. |
| KeSetTimer | mk_timer_arm | → | `mk_timer_arm(port, deadline)`. |
| KeCancelTimer | mk_timer_cancel | → | `mk_timer_cancel(port, &result)`. |
| KeGetCurrentProcessorNumber | — | 🔄 | `sched_getcpu()` or affinity query. |
| KeGetCurrentThread | mach_thread_self | → | `mach_thread_self()`. |
| KeDelayExecutionThread | nanosleep / swtch | → | `nanosleep(&req, &rem)`. |
| KeWaitForSingleObject | kevent / ulock_wait | → | `ulock_wait(UL_COMPARE_AND_WAIT, &addr, 0, timeout)`. |
| KeWaitForMultipleObjects | kevent | → | `kevent(kq, NULL, 0, events, count, &timeout)`. |
| KeQueryPerformanceCounter | mach_absolute_time | → | `mach_absolute_time()`. |
| KeQuerySystemTime | gettimeofday | → | `gettimeofday(&tv, NULL)`. |
| KeQueryActiveProcessorCount | sysctl (HW_AVAILCPU) | → | `sysctl(CTL_HW, HW_AVAILCPU, &count, &size, NULL, 0)`. |
| KeInitializeDpc | dispatch_async_f | ≈ | `dispatch_async_f(queue, ctx, dpc_routine)`. |
| KeInsertQueueDpc | dispatch_async_f | ≈ | Same. |
| KeMemoryBarrier | __sync_synchronize | → | `__sync_synchronize()` or `dmb ish` on ARM. |
| KeEnterCriticalRegion | — | 🔄 | Wine blocks signals with `sigprocmask`. |
| KeLeaveCriticalRegion | — | 🔄 | Wine unblocks signals. |
| KeBugCheck / KeBugCheckEx | panic_with_data | BSD 185 | `panic_with_data(uuid, data, len, flags, msg)`. Kernel only. |
| *(remaining KeXxx)* | — | 🔄 | Various. Mapped to POSIX/GCD/ulock. |

### 2.9 Run-Time Library (RtlXxx)

| NT Function Category | macOS XNU Equivalent | Pair | Notes |
|---------------------|---------------------|------|-------|
| RtlCopyMemory | memcpy | → | Direct. |
| RtlMoveMemory | memmove | → | Direct. |
| RtlFillMemory | memset | → | Direct. |
| RtlZeroMemory | bzero / memset(0) | → | Direct. |
| RtlCompareMemory | memcmp | → | Direct. |
| RtlEqualMemory | memcmp (==0) | → | Direct. |
| RtlSecureZeroMemory | explicit_bzero | → | `explicit_bzero(buf, len)`. |
| RtlInitUnicodeString | — | 🔄 | Wine implements UNICODE_STRING management. |
| RtlCompareUnicodeString | wcscmp / memcmp | → | Direct. |
| RtlAppendUnicodeToString | wcscat | → | Direct. |
| RtlInitAnsiString | — | 🔄 | Wine implements ANSI_STRING. |
| RtlAnsiStringToUnicodeString | mbstowcs | → | `mbstowcs(dst, src, len)`. |
| RtlUnicodeStringToAnsiString | wcstombs | → | `wcstombs(dst, src, len)`. |
| RtlIntegerToUnicodeString | swprintf | → | `swprintf(buf, len, L"%d", value)`. |
| RtlStringFromGUID | uuid_unparse | → | `uuid_unparse(uuid, out)`. |
| RtlGUIDFromString | uuid_parse | → | `uuid_parse(str, uuid)`. |
| RtlGetVersion | sysctl (KERN_OSRELEASE) | → | `sysctl(CTL_KERN, KERN_OSRELEASE, ...)` + fake NT version. |
| RtlInitializeBitMap | — | 🔄 | Wine bitmap ops. Pure computation. |
| RtlFindSetBits / RtlFindClearBits | ffs / __builtin_ctz | ≈ | `ffs()` for find-first-set. |
| RtlQueryRegistryValues | Wine file-backed registry | 🔄 | Wine reads from .reg files. |
| RtlCreateSecurityDescriptor | — | 🔄 | Allocate + initialize SECURITY_DESCRIPTOR. |
| RtlCheckRegistryKey | access (Wine prefix) | 🔄 | `access(wine_prefix + path, F_OK)`. |
| *(remaining RtlXxx ~300+ safe string/integer functions)* | — | 🔄 | All pure userspace computation. No kernel mapping needed. |

### 2.10 Power Manager (PoXxx) — 57 functions

| NT Function Category | macOS XNU Equivalent | Pair | Notes |
|---------------------|---------------------|------|-------|
| All 57 PoXxx functions | IOPMPowerSource / RootDomain | ❌ | All kernel-mode only, IOKit power management. Wine returns STATUS_SUCCESS for all. Not anti-cheat relevant. |

### 2.11 HAL Library (HalXxx) — 24 functions

| NT Function Category | macOS XNU Equivalent | Pair | Notes |
|---------------------|---------------------|------|-------|
| All 24 HalXxx functions | Apple Silicon HAL (pexpert) | ❌ | All kernel-mode only. Not relevant for Wine userspace. |

---

## Part 3: Win32k Syscalls (GDI/Windowing)

| Win32k Category | Count | macOS XNU Equivalent | Pair | Wine Implementation |
|----------------|-------|---------------------|------|-------------------|
| Window management (Create/Show/Move/Destroy) | ~30 | CoreGraphics / NSWindow via Wine Mac driver | 🔄 | Wine's mac.drv uses CoreGraphics: `CGWindowCreate`, `CGWindowMove`, etc. via ObjC bridge to NSWindow. |
| Input (keyboard/mouse/raw input) | ~20 | CGEvent / IOKit HID | ≈ | `CGEventCreateKeyboardEvent(src, key, true)`. `CGEventSourceKeyState(src, key)` for state. `IOHIDManagerCreate` for raw input. |
| GDI drawing (BitBlt/StretchBlt/etc.) | ~20 | CoreGraphics / Cairo | 🔄 | Wine renders via CoreGraphics: `CGBitmapContextCreate`, `CGContextDrawImage`, etc. |
| GDI objects (Bitmap/Pen/Brush/Font) | ~15 | CoreGraphics objects | 🔄 | `CGImageCreate`, `CGColorSpaceCreate`, etc. |
| Text / Font rendering | ~10 | CoreText | 🔄 | `CTFontCreateWithName`, `CTLineCreateWithAttributedString`, `CTLineDraw`. |
| Clipboard operations | ~10 | NSPasteboard | → | `[NSPasteboard generalPasteboard]`, `declareTypes`, `setDataForType`. |
| Desktop/WindowStation | ~10 | — | 🔄 | Wine maintains virtual desktop/winstation objects. |
| Menu management | ~10 | — | 🔄 | Wine draws menus using CoreGraphics. |
| Misc (timers, hooks, system params) | ~30 | dispatch_after / CGEventTap | ≈ | `dispatch_after(time, queue, block)` for timers. `CGEventTapCreate` for input hooks. |

---

## Part 4: NT Structures → macOS XNU Structures

| NT Structure | macOS XNU Equivalent | Mapping Notes |
|-------------|---------------------|---------------|
| EPROCESS | task_t (osfmk/kern/task.h) | task→PID, task→vm_map, task->t_flags |
| ETHREAD | thread_t (osfmk/kern/thread.h) | thread->continuation, thread->state |
| KPROCESS | task->bsd_info (proc_t) | proc->p_pid, proc->p_comm |
| KTHREAD | thread_t (mach) | thread->machine (ARM context) |
| KPRCB | processor_t (osfmk/kern/processor.h) | processor->cpu_id, processor->state |
| KINTERRUPT | IOInterruptEventSource | IOKit interrupt source |
| KDPC | thread_call_t (osfmk/kern/thread_call.h) | `thread_call_enter(delayed_call)` |
| KAPC | Mach exception + AST | ast_propagate(task) for AST flags |
| IRP | — | **No equivalent.** Wine implements IRP-like struct internally for driver emulation. |
| MDL | IOMemoryDescriptor | `IOMemoryDescriptor::withAddressRange()` |
| DEVICE_OBJECT | IOService | IOKit service object |
| DRIVER_OBJECT | — | No equivalent. Wine manages "drivers" as userspace modules. |
| OBJECT_ATTRIBUTES | — | Wine converts to internal path + security descriptor. |
| UNICODE_STRING | — | Wine maintains as UTF-16 string with length prefix. |
| CLIENT_ID | pid_t + thread_act_t | `pid_t` for process, `mach_port_name_t` for thread. |
| CONTEXT | arm_thread_state64_t | Direct map: x0-x28 → X0-X28, pc → PC, sp → SP, lr → LR. FP/NEON state in arm_neon_state64_t. |
| EXCEPTION_RECORD | mach_exception_data_t | EXC_BAD_ACCESS → EXCEPTION_ACCESS_VIOLATION. EXC_BREAKPOINT → EXCEPTION_BREAKPOINT. |
| MEMORY_BASIC_INFORMATION | vm_region_submap_info_data_64_t | vm_region_submap_info_64 from `mach_vm_region_recurse`. |
| SECURITY_DESCRIPTOR | POSIX ACL (acl_t) | Map: Owner SID → uid, Group SID → gid, DACL → ACL entries. |
| TOKEN | uid_t + gid_t + groups | `getuid()`, `getgid()`, `getgroups()`. Wine wraps in fake TOKEN struct. |
| LIST_ENTRY | queue_head_t (osfmk/kern/queue.h) | `queue_enter()`, `queue_remove()`. |
| HANDLE | int (fd) or mach_port_name_t | Dual namespace: fd table + Mach port namespace. Wine maintains separate handle table. |
| FILE_OBJECT | vnode_t + fileproc_t | `vnode` for filesystem, `fileproc` for fd. |
| SECTION_OBJECT | vm_map_entry_t + memory_object_t | Mach memory object backed by vnode. |
| LARGE_INTEGER | uint64_t | Direct. Both are 64-bit. |

---

## Part 5: NT Object Types → macOS XNU Mechanisms

| NT Object Type | Creator | macOS Mechanism | Wine Implementation |
|---------------|---------|----------------|-------------------|
| Process | NtCreateProcess | task_t + proc_t | `fork()`/`posix_spawn()` creates both. Wine PEB in userspace. |
| Thread | NtCreateThread | thread_act (Mach) | `bsdthread_create()` → Mach thread. Wine TEB in userspace. |
| Job | NtCreateJobObject | coalition | `coalition(COALITION_CREATE, &cid, flags)`. Wine tracks member PIDs. |
| Section | NtCreateSection | vm_map_entry + Mach MO | `shm_open()` + `mmap()` or anonymous `mmap()`. |
| File | NtCreateFile | vnode_t + fd | `open()` → fd + vnode. |
| Port | NtCreatePort | Mach port (mach_port_t) | `mach_port_allocate(task, MACH_PORT_RIGHT_RECEIVE, &name)`. |
| Event | NtCreateEvent | ulock address | `ulock_wait`/`ulock_wake` on address in process memory. |
| Mutant | NtCreateMutant | os_unfair_lock / psynch | `psynch_mutexwait()` for cross-process. |
| Semaphore | NtCreateSemaphore | Mach semaphore | `semaphore_create(task, &sem, SYNC_POLICY_FIFO, count)`. |
| Timer | NtCreateTimer | mk_timer (Mach port) | `mk_timer_create()` → Mach port, `mk_timer_arm()`. |
| Key (Registry) | NtCreateKey | File in Wine prefix | Wine directory tree in `~/.wine/system.reg`. |
| Token | NtCreateToken | uid/gid + Wine struct | Fake token wrapping POSIX credentials. |
| Directory | NtCreateDirectoryObject | Directory in Wine prefix | `mkdir()` in Wine prefix. |
| SymbolicLink | NtCreateSymbolicLinkObject | symlink | `symlink()` in Wine prefix. |
| IoCompletion | NtCreateIoCompletion | kqueue | `kqueue()` + `kevent(EVFILT_USER)` for completion packets. |
| Debug | NtCreateDebugObject | Wine internal | ptrace + Mach exception port. Wine event queue. |
| Profile | NtCreateProfile | — | Stub. |
| KeyedEvent | NtCreateKeyedEvent | ulock | `ulock_wait`/`ulock_wake` with keyed address. |
| WaitablePort | NtCreateWaitablePort | Mach port (receive right) | `mach_port_allocate(task, MACH_PORT_RIGHT_RECEIVE, &name)`. |
| Transaction | NtCreateTransaction | — | Stub. |
| WorkerFactory | NtCreateWorkerFactory | workq | `workq_open()` + `workq_kernreturn()`. |
| IoRing | NtCreateIoRing | — | Stub. |
| Enclave | NtCreateEnclave | — | Stub (no SGX on ARM). |
| ReserveObject | NtAllocateReserveObject | — | Stub. |

---

## Part 6: Wine-for-macOS Architecture Blueprint

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                        Windows Application (.exe)                            │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐         │
│  │ kernel32 │ │ user32   │ │ gdi32    │ │ ntdll    │ │ advapi32 │         │
│  └─────┬────┘ └─────┬────┘ └─────┬────┘ └─────┬────┘ └─────┬────┘         │
│        │            │            │            │             │               │
│  ──────┼────────────┼────────────┼────────────┼─────────────┼────────────  │
│        │     Wine Translation Layer (PE → Mach-O bridge)     │              │
│  ──────┼────────────┼────────────┼────────────┼─────────────┼────────────  │
│        │            │            │            │             │               │
│  ┌─────┴────────────┴────────────┴────────────┴─────────────┴───────────┐  │
│  │                        Wine ntdll.so                                  │  │
│  │  ┌──────────────────────────────────────────────────────────────────┐│  │
│  │  │ Syscall Emulation: NtXxx → BSD/Mach/IOKit                        ││  │
│  │  │                                                                  ││  │
│  │  │  Process:  fork/exec/posix_spawn/bsdthread_create               ││  │
│  │  │  Memory:   mmap/mprotect/mach_vm_*/mach_vm_read/write           ││  │
│  │  │  Files:    open/read/write/close/ioctl/stat/mmap                 ││  │
│  │  │  Sync:     ulock/kevent/psynch/mk_timer/semaphore               ││  │
│  │  │  IPC:      mach_msg/mach_port_*/task_for_pid                    ││  │
│  │  │  Debug:    ptrace/thread_get_state/Mach exceptions              ││  │
│  │  │  Security: csops/access/uid/gid                                 ││  │
│  │  │  Registry: file-backed virtual registry (~/.wine/*.reg)         ││  │
│  │  │  Handle:   Wine handle table (fd + Mach port + internal obj)    ││  │
│  │  └──────────────────────────────────────────────────────────────────┘│  │
│  └───────────────────────────────────────────────────────────────────────┘  │
│                                                                             │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │                     Wine mac.drv (Windowing)                         │  │
│  │  CoreGraphics / CoreText / NSPasteboard / CGEvent / IOKit HID       │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
│                                                                             │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │                     Wine dxmt / winemetal (Graphics)                 │  │
│  │  Metal / MetalFX / IOKit GPU / CoreVideo / IOSurface                │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                          macOS XNU Kernel                                    │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐         │
│  │ BSD      │ │ Mach     │ │ IOKit    │ │ MACF     │ │ Skywalk  │         │
│  │ Syscalls │ │ IPC/Traps│ │ Drivers  │ │ Security │ │ Network  │         │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────┘         │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Priority Implementation Order

| Phase | Subsystem | XNU APIs Used | NT APIs Covered | Status |
|-------|-----------|--------------|-----------------|--------|
| **P1** | Process/Thread | fork, execve, posix_spawn, bsdthread_create, pid_suspend/resume | NtCreateProcess, NtCreateThread, NtTerminateProcess, NtSuspend*, NtResume* | Wine has this |
| **P2** | Virtual Memory | mmap, munmap, mprotect, mach_vm_read, mach_vm_write, mach_vm_region | NtAllocateVirtualMemory, NtProtectVirtualMemory, NtReadVirtualMemory, NtWriteVirtualMemory | Wine has this |
| **P3** | File I/O | open, read, write, close, ioctl, stat, fsync | NtCreateFile, NtReadFile, NtWriteFile, NtClose | Wine has this |
| **P4** | Synchronization | ulock_wait/wake, kevent, psynch_*, mk_timer_*, semaphore_* | NtCreateEvent, NtCreateMutant, NtCreateSemaphore, NtCreateTimer, NtWaitForSingleObject | Wine has this |
| **P5** | IPC | mach_msg, mach_port_allocate, task_for_pid | NtCreatePort, NtConnectPort, NtRequestPort | Wine has this |
| **P6** | Handle/Object | Wine internal handle table + fd + Mach ports | NtDuplicateObject, NtClose, NtQueryObject | Wine has this |
| **P7** | Registry | file-backed registry | All 42 registry APIs | Wine has this |
| **P8** | Debugging | ptrace, thread_get/set_state, Mach exceptions | NtDebugActiveProcess, NtWaitForDebugEvent, NtGetContextThread | Wine has this |
| **P9** | Code Integrity | csops, proc_info | NtGetCachedSigningLevel | Wine partial |
| **P10** | Windowing | CoreGraphics, CGEvent, CoreText | All Win32k | Wine has this (mac.drv) |
| **P11** | Anti-Cheat Bridge | EndpointSecurity, MACF hooks | PsSet*NotifyRoutine, ObRegisterCallbacks | **NOT YET BUILT** |

### Phase 11: Anti-Cheat Bridge (Not Yet Implemented)

This is what would need to be built to support kernel-level anti-cheat:

```
┌────────────────────────────────────────────────────────────┐
│              Anti-Cheat System Extension (com.apple.es.*)    │
│                                                              │
│  ┌────────────────────────────────────────────────────────┐ │
│  │ EndpointSecurity Subscription                          │ │
│  │  - ES_EVENT_TYPE_NOTIFY_EXEC → PsSetLoadImageNotify    │ │
│  │  - ES_EVENT_TYPE_NOTIFY_FORK → PsSetCreateProcessNotify│ │
│  │  - ES_EVENT_TYPE_NOTIFY_MMAP → FltRegisterFilter (mem) │ │
│  │  - ES_EVENT_TYPE_NOTIFY_SIGNAL → Debug exception route │ │
│  └────────────────────────────────────────────────────────┘ │
│  ┌────────────────────────────────────────────────────────┐ │
│  │ MACF Policy Module (if kext allowed)                   │ │
│  │  - mac_proc_check_get_task → ObRegisterCallbacks       │ │
│  │  - mac_proc_check_debug → Anti-debug policy            │ │
│  │  - mac_vnode_check_signature → Code integrity          │ │
│  │  - mac_proc_check_syscall_unix → Syscall filtering     │ │
│  └────────────────────────────────────────────────────────┘ │
│  ┌────────────────────────────────────────────────────────┐ │
│  │ Mach Exception Server                                  │ │
│  │  - task_set_exception_ports → Route to Wine debugger   │ │
│  │  - EXC_BAD_ACCESS → EXCEPTION_ACCESS_VIOLATION        │ │
│  │  - EXC_BREAKPOINT → EXCEPTION_BREAKPOINT              │ │
│  │  - EXC_ARITHMETIC → EXCEPTION_FLT_DIV_BY_ZERO         │ │
│  └────────────────────────────────────────────────────────┘ │
│                                                              │
│  Communication: Mach IPC → Wine ntdll exception dispatcher  │
└────────────────────────────────────────────────────────────┘
```
