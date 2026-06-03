# XNU Kernel Complete API Reference
## macOS 26.6 (Tahoe) / arm64 — For Wine Anti-Cheat Compatibility Mapping

> Source: apple-oss-distributions/xnu (latest)
> Architecture: arm64 (Apple Silicon)
> Kernel: XNU = Mach microkernel + BSD layer + IOKit driver framework + Security (MACF)

---

## Table of Contents
1. [XNU Architecture Overview](#1-xnu-architecture-overview)
2. [BSD Syscall Table (0-557)](#2-bsd-syscall-table)
3. [Mach Traps](#3-mach-traps)
4. [Mach IPC Interfaces](#4-mach-ipc-interfaces)
5. [Virtual Memory Management](#5-virtual-memory-management)
6. [Process/Thread Management](#6-processthread-management)
7. [IOKit Class Hierarchy](#7-iokit-class-hierarchy)
8. [Security Framework (MACF)](#8-security-framework-macf)
9. [Network Stack](#9-network-stack)
10. [VFS / Filesystem](#10-vfs-filesystem)
11. [Anti-Cheat Relevance Matrix](#11-anti-cheat-relevance-matrix)

---

## 1. XNU Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    User Space                                │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
│  │ POSIX App │  │ Mach App │  │ IOKit App│  │  Wine     │   │
│  └─────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘   │
────────│ syscalls    │ traps    │ iokit_usr │     │ NT APIs ────
┌───────┴────────────┴──────────┴───────────┴─────┴──────────┐
│                     XNU Kernel                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │                  BSD Layer (bsd/)                     │   │
│  │  syscalls · VFS · networking · process mgmt · audit  │   │
│  ├──────────────────────────────────────────────────────┤   │
│  │            Mach Microkernel (osfmk/)                  │   │
│  │  IPC · VM · scheduling · traps · host · task · thread│   │
│  ├──────────────────────────────────────────────────────┤   │
│  │             IOKit Framework (iokit/)                  │   │
│  │  driver registry · power mgmt · DMA · user clients   │   │
│  ├──────────────────────────────────────────────────────┤   │
│  │          Security Framework (security/)               │   │
│  │  MACF · AMFI · code signing · Sandbox · quarantine   │   │
│  └──────────────────────────────────────────────────────┘   │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐                  │
│  │ libkern  │  │ pexpert  │  │  san     │                  │
│  └──────────┘  └──────────┘  └──────────┘                  │
└─────────────────────────────────────────────────────────────┘
```

### Subsystem Directories

| Directory | Purpose |
|-----------|---------|
| `bsd/` | BSD syscall layer, VFS, networking, process management, audit |
| `osfmk/` | Mach microkernel: IPC, VM, scheduler, traps, task/thread/host |
| `iokit/` | I/O Kit: C++ driver framework, registry, DMA, user clients |
| `security/` | MAC Framework, AMFI, code signing hooks |
| `libkern/` | Kernel support library (OS containers, C++ runtime) |
| `pexpert/` | Platform expert (device tree, boot args) |
| `san/` | SAN (Secure Arbitrary Nested) — exclaves support |

---

## 2. BSD Syscall Table

### 2.1 Process Lifecycle

| # | Name | Signature | Notes |
|---|------|-----------|-------|
| 1 | exit | `void exit(int rval)` | |
| 2 | fork | `int fork(void)` | |
| 59 | execve | `int execve(char *fname, char **argp, char **envp)` | |
| 380 | __mac_execve | `int __mac_execve(char *fname, char **argp, char **envp, struct mac *mac_p)` | MAC label exec |
| 244 | posix_spawn | `int posix_spawn(pid_t *pid, const char *path, const struct _posix_spawn_args_desc *adesc, char **argv, char **envp)` | |
| 7 | wait4 | `int wait4(int pid, user_addr_t status, int options, user_addr_t rusage)` | |
| 173 | waitid | `int waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options)` | |
| 520 | terminate_with_payload | `int terminate_with_payload(int pid, uint32_t reason_namespace, uint64_t reason_code, void *payload, uint32_t payload_size, const char *reason_string, uint64_t reason_flags)` | |
| 521 | abort_with_payload | `void abort_with_payload(uint32_t reason_namespace, uint64_t reason_code, void *payload, uint32_t payload_size, const char *reason_string, uint64_t reason_flags)` | |
| 433 | pid_suspend | `int pid_suspend(int pid)` | |
| 434 | pid_resume | `int pid_resume(int pid)` | |
| 435 | pid_hibernate | `int pid_hibernate(int pid)` | embedded only |
| 436 | pid_shutdown_sockets | `int pid_shutdown_sockets(int pid, int level)` | |

### 2.2 File I/O

| # | Name | Signature |
|---|------|-----------|
| 3 | read | `user_ssize_t read(int fd, user_addr_t cbuf, user_size_t nbyte)` |
| 4 | write | `user_ssize_t write(int fd, user_addr_t cbuf, user_size_t nbyte)` |
| 5 | open | `int open(user_addr_t path, int flags, int mode)` |
| 6 | close | `int sys_close(int fd)` |
| 73 | munmap | `int munmap(caddr_ut addr, size_ut len)` |
| 74 | mprotect | `int mprotect(caddr_ut addr, size_ut len, int prot)` |
| 75 | madvise | `int madvise(caddr_ut addr, size_ut len, int behav)` |
| 78 | mincore | `int mincore(caddr_ut addr, size_ut len, user_addr_t vec)` |
| 90 | dup2 | `int sys_dup2(u_int from, u_int to)` |
| 92 | fcntl | `int sys_fcntl(int fd, int cmd, long arg)` |
| 93 | select | `int select(int nd, u_int32_t *in, u_int32_t *ou, u_int32_t *ex, struct timeval *tv)` |
| 95 | fsync | `int fsync(int fd)` |
| 120 | readv | `user_ssize_t readv(int fd, struct iovec *iovp, u_int iovcnt)` |
| 121 | writev | `user_ssize_t writev(int fd, struct iovec *iovp, u_int iovcnt)` |
| 153 | pread | `user_ssize_t pread(int fd, user_addr_t buf, user_size_t nbyte, off_t offset)` |
| 154 | pwrite | `user_ssize_t pwrite(int fd, user_addr_t buf, user_size_t nbyte, off_t offset)` |
| 187 | fdatasync | `int fdatasync(int fd)` |
| 197 | mmap | `user_addr_t mmap(caddr_ut addr, size_ut len, int prot, int flags, int fd, off_t pos)` |
| 199 | lseek | `off_t lseek(int fd, off_t offset, int whence)` |
| 230 | poll | `int poll(struct pollfd *fds, u_int nfds, int timeout)` |
| 362 | kqueue | `int kqueue(void)` |
| 363 | kevent | `int kevent(int fd, const struct kevent *changelist, int nchanges, struct kevent *eventlist, int nevents, const struct timespec *timeout)` |
| 369 | kevent64 | `int kevent64(int fd, const struct kevent64_s *changelist, int nchanges, struct kevent64_s *eventlist, int nevents, unsigned int flags, const struct timespec *timeout)` |
| 374 | kevent_qos | `int kevent_qos(int fd, ...)` |
| 375 | kevent_id | `int kevent_id(uint64_t id, ...)` |
| 530 | kqueue_workloop_ctl | `int kqueue_workloop_ctl(...)` |
| 540 | preadv | `user_ssize_t sys_preadv(int fd, struct iovec *iovp, int iovcnt, off_t offset)` |
| 541 | pwritev | `user_ssize_t sys_pwritev(int fd, struct iovec *iovp, int iovcnt, off_t offset)` |

### 2.3 File System Operations

| # | Name | Signature |
|---|------|-----------|
| 9 | link | `int link(user_addr_t path, user_addr_t link)` |
| 10 | unlink | `int unlink(user_addr_t path)` |
| 12 | chdir | `int sys_chdir(user_addr_t path)` |
| 13 | fchdir | `int sys_fchdir(int fd)` |
| 14 | mknod | `int mknod(user_addr_t path, int mode, int dev)` |
| 15 | chmod | `int chmod(user_addr_t path, int mode)` |
| 16 | chown | `int chown(user_addr_t path, int uid, int gid)` |
| 18 | getfsstat | `int getfsstat(user_addr_t buf, int bufsize, int flags)` |
| 33 | access | `int access(user_addr_t path, int flags)` |
| 34 | chflags | `int chflags(char *path, int flags)` |
| 35 | fchflags | `int fchflags(int fd, int flags)` |
| 41 | dup | `int sys_dup(u_int fd)` |
| 42 | pipe | `int pipe(void)` |
| 57 | symlink | `int symlink(char *path, char *link)` |
| 58 | readlink | `int readlink(char *path, char *buf, int count)` |
| 60 | umask | `int umask(int newmask)` |
| 61 | chroot | `int chroot(user_addr_t path)` |
| 65 | msync | `int msync(caddr_ut addr, size_ut len, int flags)` |
| 128 | rename | `int rename(char *from, char *to)` |
| 136 | mkdir | `int mkdir(user_addr_t path, int mode)` |
| 137 | rmdir | `int rmdir(char *path)` |
| 138 | utimes | `int utimes(char *path, struct timeval *tptr)` |
| 139 | futimes | `int futimes(int fd, struct timeval *tptr)` |
| 159 | unmount | `int unmount(user_addr_t path, int flags)` |
| 161 | getfh | `int getfh(char *fname, fhandle_t *fhp)` | NFS only |
| 167 | mount | `int mount(char *type, char *path, int flags, caddr_t data)` |
| 188 | stat | `int stat(user_addr_t path, user_addr_t ub)` |
| 189 | fstat | `int sys_fstat(int fd, user_addr_t ub)` |
| 190 | lstat | `int lstat(user_addr_t path, user_addr_t ub)` |
| 191 | pathconf | `int pathconf(char *path, int name)` |
| 192 | fpathconf | `int sys_fpathconf(int fd, int name)` |
| 196 | getdirentries | `int getdirentries(int fd, char *buf, u_int count, long *basep)` |
| 200 | truncate | `int truncate(char *path, off_t length)` |
| 201 | ftruncate | `int ftruncate(int fd, off_t length)` |
| 216 | open_dprotected_np | `int open_dprotected_np(...)` |
| 220 | getattrlist | `int getattrlist(const char *path, struct attrlist *alist, ...)` |
| 221 | setattrlist | `int setattrlist(const char *path, struct attrlist *alist, ...)` |
| 222 | getdirentriesattr | `int getdirentriesattr(...)` |
| 223 | exchangedata | `int exchangedata(const char *path1, const char *path2, u_long options)` |
| 226 | delete | `int delete(user_addr_t path)` |
| 227 | copyfile | `int copyfile(char *from, char *to, int mode, int flags)` |
| 228 | fgetattrlist | `int fgetattrlist(int fd, ...)` |
| 229 | fsetattrlist | `int fsetattrlist(int fd, ...)` |
| 234-241 | getxattr/fgetxattr/setxattr/fsetxattr/removexattr/fremovexattr/listxattr/flistxattr | Extended attributes |
| 242 | fsctl | `int fsctl(const char *path, u_long cmd, caddr_t data, u_int options)` |
| 245 | ffsctl | `int ffsctl(int fd, u_long cmd, caddr_t data, u_int options)` |
| 338 | stat64 | `int stat64(user_addr_t path, user_addr_t ub)` |
| 339 | fstat64 | `int sys_fstat64(int fd, user_addr_t ub)` |
| 340 | lstat64 | `int lstat64(user_addr_t path, user_addr_t ub)` |
| 344 | getdirentries64 | `user_ssize_t getdirentries64(int fd, void *buf, user_size_t bufsize, off_t *position)` |
| 345 | statfs64 | `int statfs64(char *path, struct statfs64 *buf)` |
| 346 | fstatfs64 | `int fstatfs64(int fd, struct statfs64 *buf)` |
| 347 | getfsstat64 | `int getfsstat64(user_addr_t buf, int bufsize, int flags)` |
| 427 | fsgetpath | `user_ssize_t fsgetpath(...)` |
| 461 | getattrlistbulk | `int getattrlistbulk(int dirfd, ...)` |
| 462 | clonefileat | `int clonefileat(int src_dirfd, user_addr_t src, int dst_dirfd, user_addr_t dst, uint32_t flags)` |
| 463 | openat | `int openat(int fd, user_addr_t path, int flags, int mode)` |
| 465 | renameat | `int renameat(int fromfd, char *from, int tofd, char *to)` |
| 466 | faccessat | `int faccessat(int fd, user_addr_t path, int amode, int flag)` |
| 467 | fchmodat | `int fchmodat(int fd, user_addr_t path, int mode, int flag)` |
| 468 | fchownat | `int fchownat(int fd, user_addr_t path, uid_t uid, gid_t gid, int flag)` |
| 469 | fstatat | `int fstatat(int fd, user_addr_t path, user_addr_t ub, int flag)` |
| 471 | linkat | `int linkat(int fd1, user_addr_t path, int fd2, user_addr_t link, int flag)` |
| 472 | unlinkat | `int unlinkat(int fd, user_addr_t path, int flag)` |
| 473 | readlinkat | `int readlinkat(int fd, user_addr_t path, user_addr_t buf, size_t bufsize)` |
| 474 | symlinkat | `int symlinkat(user_addr_t *path1, int fd, user_addr_t path2)` |
| 475 | mkdirat | `int mkdirat(int fd, user_addr_t path, int mode)` |
| 476 | getattrlistat | `int getattrlistat(int fd, ...)` |
| 517 | fclonefileat | `int fclonefileat(int src_fd, int dst_dirfd, user_addr_t dst, uint32_t flags)` |
| 518 | fs_snapshot | `int fs_snapshot(uint32_t op, ...)` |
| 524 | setattrlistat | `int setattrlistat(int fd, ...)` |
| 526 | fmount | `int fmount(const char *type, int fd, int flags, void *data)` |
| 537 | pivot_root | `int pivot_root(...)` |
| 549 | graftdmg | `int graftdmg(int dmg_fd, ...)` |
| 551 | freadlink | `int freadlink(int fd, user_addr_t buf, user_size_t bufsize)` |
| 553 | mkfifoat | `int mkfifoat(int fd, user_addr_t path, int mode)` |
| 554 | mknodat | `int mknodat(int fd, user_addr_t path, int mode, int dev)` |
| 555 | ungraftdmg | `int ungraftdmg(const char *mountdir, uint64_t flags)` |

### 2.4 Memory Management

| # | Name | Signature | Wine Relevance |
|---|------|-----------|---------------|
| 65 | msync | `int msync(caddr_ut addr, size_ut len, int flags)` | FlushViewOfFile |
| 73 | munmap | `int munmap(caddr_ut addr, size_ut len)` | VirtualFree |
| 74 | mprotect | `int mprotect(caddr_ut addr, size_ut len, int prot)` | VirtualProtect |
| 75 | madvise | `int madvise(caddr_ut addr, size_ut len, int behav)` | |
| 78 | mincore | `int mincore(caddr_ut addr, size_ut len, user_addr_t vec)` | |
| 197 | mmap | `user_addr_t mmap(caddr_ut addr, size_ut len, int prot, int flags, int fd, off_t pos)` | VirtualAlloc/MapViewOfFile |
| 203 | mlock | `int mlock(caddr_ut addr, size_ut len)` | VirtualLock |
| 204 | munlock | `int munlock(caddr_ut addr, size_ut len)` | VirtualUnlock |
| 250 | minherit | `int minherit(caddr_ut addr, size_ut len, int inherit)` | |
| 324 | mlockall | `int mlockall(int how)` | |
| 325 | munlockall | `int munlockall(int how)` | |
| 489 | mremap_encrypted | `int mremap_encrypted(caddr_ut addr, size_ut len, uint32_t cryptid, uint32_t cputype, uint32_t cpusubtype)` | |

### 2.5 Signals

| # | Name | Signature |
|---|------|-----------|
| 37 | kill | `int kill(int pid, int signum, int posix)` |
| 46 | sigaction | `int sigaction(int signum, struct __sigaction *nsa, struct sigaction *osa)` |
| 48 | sigprocmask | `int sigprocmask(int how, user_addr_t mask, user_addr_t omask)` |
| 52 | sigpending | `int sigpending(struct sigvec *osv)` |
| 53 | sigaltstack | `int sigaltstack(struct sigaltstack *nss, struct sigaltstack *oss)` |
| 111 | sigsuspend | `int sigsuspend(sigset_t mask)` |
| 184 | sigreturn | `int sigreturn(struct ucontext *uctx, int infostyle, user_addr_t token)` |
| 330 | __sigwait | `int __sigwait(user_addr_t set, user_addr_t sig)` |
| 328 | __pthread_kill | `int __pthread_kill(int thread_port, int sig)` |
| 329 | __pthread_sigmask | `int __pthread_sigmask(int how, user_addr_t set, user_addr_t oset)` |

### 2.6 Networking (Sockets)

| # | Name | Signature | Wine Mapping |
|---|------|-----------|-------------|
| 27 | recvmsg | `int recvmsg(int s, struct msghdr *msg, int flags)` | WSARecvMsg |
| 28 | sendmsg | `int sendmsg(int s, caddr_t msg, int flags)` | WSASendMsg |
| 29 | recvfrom | `int recvfrom(int s, void *buf, size_t len, int flags, struct sockaddr *from, int *fromlenaddr)` | recvfrom |
| 30 | accept | `int accept(int s, caddr_t name, socklen_t *anamelen)` | accept |
| 31 | getpeername | `int getpeername(int fdes, caddr_t asa, socklen_t *alen)` | getpeername |
| 32 | getsockname | `int getsockname(int fdes, caddr_t asa, socklen_t *alen)` | getsockname |
| 97 | socket | `int socket(int domain, int type, int protocol)` | WSASocket |
| 98 | connect | `int connect(int s, caddr_t name, socklen_t namelen)` | WSAConnect |
| 104 | bind | `int bind(int s, caddr_t name, socklen_t namelen)` | bind |
| 105 | setsockopt | `int setsockopt(int s, int level, int name, caddr_t val, socklen_t valsize)` | setsockopt |
| 106 | listen | `int listen(int s, int backlog)` | listen |
| 118 | getsockopt | `int getsockopt(int s, int level, int name, caddr_t val, socklen_t *avalsize)` | getsockopt |
| 133 | sendto | `int sendto(int s, caddr_t buf, size_t len, int flags, caddr_t to, socklen_t tolen)` | sendto |
| 134 | shutdown | `int shutdown(int s, int how)` | shutdown |
| 135 | socketpair | `int socketpair(int domain, int type, int protocol, int *rsv)` | |
| 337 | sendfile | `int sendfile(int fd, int s, off_t offset, off_t *nbytes, struct sf_hdtr *hdtr, int flags)` | TransmitFile |
| 447 | connectx | `int connectx(int socket, const sa_endpoints_t *endpoints, ...)` | |
| 448 | disconnectx | `int disconnectx(int s, sae_associd_t aid, sae_connid_t cid)` | |
| 449 | peeloff | `int peeloff(int s, sae_associd_t aid)` | |
| 450 | socket_delegate | `int socket_delegate(int domain, int type, int protocol, pid_t epid)` | |
| 480 | recvmsg_x | `user_ssize_t recvmsg_x(int s, struct msghdr_x *msgp, u_int cnt, int flags)` | |
| 481 | sendmsg_x | `user_ssize_t sendmsg_x(int s, struct msghdr_x *msgp, u_int cnt, int flags)` | |
| 503-514 | __nexus_* / __channel_* | Skywalk nexus/channel APIs | |

### 2.7 Threading / Synchronization

| # | Name | Signature |
|---|------|-----------|
| 42 | pipe | `int pipe(void)` |
| 85 | swapon | `int swapon(void)` |
| 266 | shm_open | `int shm_open(const char *name, int oflag, int mode)` |
| 267 | shm_unlink | `int shm_unlink(const char *name)` |
| 268 | sem_open | `user_addr_t sem_open(const char *name, int oflag, int mode, int value)` |
| 269 | sem_close | `int sem_close(sem_t *sem)` |
| 270 | sem_unlink | `int sem_unlink(const char *name)` |
| 271 | sem_wait | `int sem_wait(sem_t *sem)` |
| 272 | sem_trywait | `int sem_trywait(sem_t *sem)` |
| 273 | sem_post | `int sem_post(sem_t *sem)` |
| 297-309 | psynch_* | psync rw/mutex/cv operations |
| 312 | psynch_cvclrprepost | `int psynch_cvclrprepost(...)` |
| 334 | __semwait_signal | `int __semwait_signal(int cond_sem, int mutex_sem, int timeout, int relative, int64_t tv_sec, int32_t tv_nsec)` |
| 360 | bsdthread_create | `user_addr_t bsdthread_create(user_addr_t func, user_addr_t func_arg, user_addr_t stack, user_addr_t pthread, uint32_t flags)` |
| 361 | bsdthread_terminate | `int bsdthread_terminate(...)` |
| 366 | bsdthread_register | `int bsdthread_register(...)` |
| 367 | workq_open | `int workq_open(void)` |
| 368 | workq_kernreturn | `int workq_kernreturn(int options, user_addr_t item, int affinity, int prio)` |
| 372 | thread_selfid | `uint64_t thread_selfid(void)` |
| 378 | bsdthread_ctl | `int bsdthread_ctl(...)` |
| 482 | thread_selfusage | `uint64_t thread_selfusage(void)` |
| 496 | mach_eventlink_signal | `uint64_t mach_eventlink_signal(mach_port_name_t eventlink_port, uint64_t signal_count)` |
| 497 | mach_eventlink_wait_until | `uint64_t mach_eventlink_wait_until(...)` |
| 498 | mach_eventlink_signal_wait_until | `uint64_t mach_eventlink_signal_wait_until(...)` |
| 499 | work_interval_ctl | `int work_interval_ctl(uint32_t operation, uint64_t work_interval_id, void *arg, size_t len)` |
| 515 | ulock_wait | `int sys_ulock_wait(uint32_t operation, void *addr, uint64_t value, uint32_t timeout)` |
| 516 | ulock_wake | `int sys_ulock_wake(uint32_t operation, void *addr, uint64_t wake_value)` |
| 544 | ulock_wait2 | `int sys_ulock_wait2(uint32_t operation, void *addr, uint64_t value, uint64_t timeout, uint64_t value2)` |

### 2.8 System Information / Control

| # | Name | Signature |
|---|------|-----------|
| 20 | getpid | `int getpid(void)` |
| 24 | getuid | `int getuid(void)` |
| 25 | geteuid | `int geteuid(void)` |
| 39 | getppid | `int getppid(void)` |
| 43 | getegid | `int getegid(void)` |
| 47 | getgid | `int getgid(void)` |
| 54 | ioctl | `int ioctl(int fd, u_long com, caddr_t data)` |
| 55 | reboot | `int reboot(int opt, char *msg)` |
| 79 | getgroups | `int getgroups(u_int gidsetsize, gid_t *gidset)` |
| 80 | setgroups | `int setgroups(u_int gidsetsize, gid_t *gidset)` |
| 81 | getpgrp | `int getpgrp(void)` |
| 82 | setpgid | `int setpgid(int pid, int pgid)` |
| 89 | getdtablesize | `int sys_getdtablesize(void)` |
| 116 | gettimeofday | `int gettimeofday(struct timeval *tp, struct timezone *tzp, uint64_t *mach_absolute_time)` |
| 117 | getrusage | `int getrusage(int who, struct rusage *rusage)` |
| 147 | setsid | `int setsid(void)` |
| 151 | getpgid | `int getpgid(pid_t pid)` |
| 169 | csops | `int csops(pid_t pid, uint32_t ops, user_addr_t useraddr, user_size_t usersize)` | Code signing ops |
| 170 | csops_audittoken | `int csops_audittoken(pid_t pid, uint32_t ops, user_addr_t useraddr, user_size_t usersize, user_addr_t uaudittoken)` | |
| 181 | setgid | `int setgid(gid_t gid)` |
| 182 | setegid | `int setegid(gid_t egid)` |
| 183 | seteuid | `int seteuid(uid_t euid)` |
| 186 | thread_selfcounts | `int thread_selfcounts(uint32_t kind, user_addr_t buf, user_size_t size)` |
| 194 | getrlimit | `int getrlimit(u_int which, struct rlimit *rlp)` |
| 195 | setrlimit | `int setrlimit(u_int which, struct rlimit *rlp)` |
| 202 | sysctl | `int sysctl(int *name, u_int namelen, void *old, size_t *oldlenp, void *new, size_t newlen)` |
| 236 | sysctlbyname | `int sys_sysctlbyname(const char *name, size_t namelen, void *old, size_t *oldlenp, void *new, size_t newlen)` |
| 26 | ptrace | `int ptrace(int req, pid_t pid, caddr_t addr, int data)` | Anti-cheat debug |
| 310 | getsid | `int getsid(pid_t pid)` |
| 322 | iopolicysys | `int iopolicysys(int cmd, void *arg)` |
| 323 | process_policy | `int process_policy(int scope, int action, int policy, int policy_subtype, user_addr_t attrp, pid_t target_pid, uint64_t target_threadid)` |
| 327 | issetugid | `int issetugid(void)` |
| 336 | proc_info | `int proc_info(int32_t callnum, int32_t pid, uint32_t flavor, uint64_t arg, user_addr_t buffer, int32_t buffersize)` | NtQuerySystemInformation equiv |
| 373 | ledger | `int ledger(int cmd, caddr_t arg1, caddr_t arg2, caddr_t arg3)` |
| 38 | crossarch_trap | `int sys_crossarch_trap(uint32_t name)` | Rosetta |
| 142 | gethostuuid | `int gethostuuid(unsigned char *uuid_buf, const struct timespec *timeoutp)` |
| 311 | settid_with_pid | `int sys_settid_with_pid(pid_t pid, int assume)` |
| 339 | sysctl | sysctl variants |
| 377-379 | kdebug_* | kdebug tracing |
| 439 | kas_info | `int kas_info(int selector, void *value, size_t *size)` | Kernel ASLR info |
| 440 | memorystatus_control | `int memorystatus_control(uint32_t command, int32_t pid, uint32_t flags, user_addr_t buffer, size_t buffersize)` |
| 451 | telemetry | `int telemetry(uint64_t cmd, ...)` |
| 452 | proc_uuid_policy | `int proc_uuid_policy(uint32_t operation, uuid_t uuid, size_t uuidlen, uint32_t flags)` |
| 456 | sfi_ctl | `int sfi_ctl(uint32_t operation, uint32_t sfi_class, uint64_t time, uint64_t *out_time)` |
| 457 | sfi_pidctl | `int sfi_pidctl(uint32_t operation, pid_t pid, uint32_t sfi_flags, uint32_t *out_sfi_flags)` |
| 458 | coalition | `int coalition(uint32_t operation, uint64_t *cid, uint32_t flags)` |
| 459 | coalition_info | `int coalition_info(uint32_t flavor, uint64_t *cid, void *buffer, size_t *bufsize)` |
| 460 | necp_match_policy | `int necp_match_policy(uint8_t *parameters, size_t parameters_size, struct necp_aggregate_result *returned_result)` |
| 477 | proc_trace_log | `int proc_trace_log(pid_t pid, uint64_t uniqueid)` |
| 483 | csrctl | `int csrctl(uint32_t op, user_addr_t useraddr, user_addr_t usersize)` | SIP control |
| 500 | getentropy | `int getentropy(void *buffer, size_t size)` |
| 501 | necp_open | `int necp_open(int flags)` |
| 502 | necp_client_action | `int necp_client_action(int necp_fd, uint32_t action, ...)` |
| 522 | necp_session_open | `int necp_session_open(int flags)` |
| 523 | necp_session_action | `int necp_session_action(int necp_fd, uint32_t action, ...)` |
| 534 | memorystatus_available_memory | `uint64_t memorystatus_available_memory(void)` |
| 538 | task_inspect_for_pid | `int task_inspect_for_pid(mach_port_name_t target_tport, int pid, mach_port_name_t *t)` |
| 539 | task_read_for_pid | `int task_read_for_pid(mach_port_name_t target_tport, int pid, mach_port_name_t *t)` |
| 545 | proc_info_extended_id | `int proc_info_extended_id(...)` |
| 546 | tracker_action | `int tracker_action(int action, char *buffer, size_t buffer_size)` |
| 547 | debug_syscall_reject | `int debug_syscall_reject(uint64_t packed_selectors)` |
| 552 | record_system_event | `int sys_record_system_event(uint32_t type, uint32_t subsystem, const char *event, const char *payload)` |
| 556 | coalition_policy_set | `int sys_coalition_policy_set(uint64_t cid, uint32_t flavor, uint32_t value)` |
| 557 | coalition_policy_get | `int sys_coalition_policy_get(uint64_t cid, uint32_t flavor)` |

### 2.9 Security / Code Signing / Audit

| # | Name | Signature |
|---|------|-----------|
| 169 | csops | `int csops(pid_t pid, uint32_t ops, user_addr_t useraddr, user_size_t usersize)` |
| 170 | csops_audittoken | `int csops_audittoken(...)` |
| 350 | audit | `int audit(void *record, int length)` |
| 351 | auditon | `int auditon(int cmd, void *data, int length)` |
| 353 | getauid | `int getauid(au_id_t *auid)` |
| 354 | setauid | `int setauid(au_id_t *auid)` |
| 357 | getaudit_addr | `int getaudit_addr(struct auditinfo_addr *auditinfo_addr, int length)` |
| 358 | setaudit_addr | `int setaudit_addr(struct auditinfo_addr *auditinfo_addr, int length)` |
| 359 | auditctl | `int auditctl(char *path)` |
| 380 | __mac_execve | MAC-labeled exec |
| 381 | __mac_syscall | `int __mac_syscall(char *policy, int call, user_addr_t arg)` |
| 382 | __mac_get_file | `int __mac_get_file(char *path_p, struct mac *mac_p)` |
| 383 | __mac_set_file | `int __mac_set_file(char *path_p, struct mac *mac_p)` |
| 384 | __mac_get_link | `int __mac_get_link(char *path_p, struct mac *mac_p)` |
| 385 | __mac_set_link | `int __mac_set_link(char *path_p, struct mac *mac_p)` |
| 386 | __mac_get_proc | `int __mac_get_proc(struct mac *mac_p)` |
| 387 | __mac_set_proc | `int __mac_set_proc(struct mac *mac_p)` |
| 388 | __mac_get_fd | `int __mac_get_fd(int fd, struct mac *mac_p)` |
| 389 | __mac_set_fd | `int __mac_set_fd(int fd, struct mac *mac_p)` |
| 390 | __mac_get_pid | `int __mac_get_pid(pid_t pid, struct mac *mac_p)` |
| 424 | __mac_mount | MAC-labeled mount |
| 425 | __mac_get_mount | MAC mount get |
| 426 | __mac_getfsstat | MAC getfsstat |
| 428 | audit_session_self | `mach_port_name_t audit_session_self(void)` |
| 429 | audit_session_join | `int audit_session_join(mach_port_name_t port)` |
| 430 | fileport_makeport | `int sys_fileport_makeport(int fd, user_addr_t portnamep)` |
| 431 | fileport_makefd | `int sys_fileport_makefd(mach_port_name_t port)` |
| 432 | audit_session_port | `int audit_session_port(au_asid_t asid, user_addr_t portnamep)` |
| 441 | guarded_open_np | `int guarded_open_np(...)` |
| 442 | guarded_close_np | `int guarded_close_np(int fd, const guardid_t *guard)` |
| 443 | guarded_kqueue_np | `int guarded_kqueue_np(const guardid_t *guard, u_int guardflags)` |
| 444 | change_fdguard_np | `int change_fdguard_np(...)` |
| 445 | usrctl | `int usrctl(uint32_t flags)` |
| 446 | proc_rlimit_control | `int proc_rlimit_control(pid_t pid, int flavor, void *arg)` |
| 483 | csrctl | `int csrctl(uint32_t op, user_addr_t useraddr, user_addr_t usersize)` | SIP |
| 547 | debug_syscall_reject | `int debug_syscall_reject(uint64_t packed_selectors)` | Syscall filtering |

### 2.10 Shared Memory / IPC

| # | Name | Signature |
|---|------|-----------|
| 251 | semsys | `int semsys(u_int which, ...)` |
| 252 | msgsys | `int msgsys(u_int which, ...)` |
| 253 | shmsys | `int shmsys(u_int which, ...)` |
| 254 | semctl | `int semctl(int semid, int semnum, int cmd, semun_t arg)` |
| 255 | semget | `int semget(key_t key, int nsems, int semflg)` |
| 256 | semop | `int semop(int semid, struct sembuf *sops, int nsops)` |
| 258 | msgctl | `int msgctl(int msqid, int cmd, struct msqid_ds *buf)` |
| 259 | msgget | `int msgget(key_t key, int msgflg)` |
| 260 | msgsnd | `int msgsnd(int msqid, void *msgp, size_t msgsz, int msgflg)` |
| 261 | msgrcv | `user_ssize_t msgrcv(int msqid, void *msgp, size_t msgsz, long msgtyp, int msgflg)` |
| 262 | shmat | `user_addr_t shmat(int shmid, void *shmaddr, int shmflg)` |
| 263 | shmctl | `int shmctl(int shmid, int cmd, struct shmid_ds *buf)` |
| 264 | shmdt | `int shmdt(void *shmaddr)` |
| 265 | shmget | `int shmget(key_t key, size_t size, int shmflg)` |
| 266 | shm_open | `int shm_open(const char *name, int oflag, int mode)` |
| 267 | shm_unlink | `int shm_unlink(const char *name)` |
| 430 | fileport_makeport | Mach port from fd |
| 431 | fileport_makefd | fd from Mach port |
| 536 | shared_region_map_and_slide_2_np | Shared cache mapping |
| 550 | map_with_linkning_np | Linked mapping |

### 2.11 Misc / Diagnostic

| # | Name | Signature |
|---|------|-----------|
| 36 | sync | `int sync(void)` |
| 56 | revoke | `int revoke(char *path)` |
| 67 | oslog_coproc_reg | Coprocessor log registration |
| 68 | oslog_coproc | Coprocessor log write |
| 164 | funmount | `int funmount(int fd, int flags)` |
| 177 | kdebug_typefilter | `int kdebug_typefilter(void** addr, size_t* size)` |
| 178 | kdebug_trace_string | `uint64_t kdebug_trace_string(uint32_t debugid, uint64_t str_id, const char *str)` |
| 179 | kdebug_trace64 | `int kdebug_trace64(uint32_t code, uint64_t arg1, ...)` |
| 180 | kdebug_trace | `int kdebug_trace(uint32_t code, u_long arg1, ...)` |
| 185 | panic_with_data | `int sys_panic_with_data(uuid_t uuid, void *addr, uint32_t len, uint32_t flags, const char *msg)` |
| 294 | shared_region_check_np | `int shared_region_check_np(uint64_t *start_address)` |
| 296 | vm_pressure_monitor | `int vm_pressure_monitor(int wait_for_pressure, int nsecs_monitored, uint32_t *pages_reclaimed)` |
| 313-320 | aio_* | Async I/O family |
| 335 | proc_info | Process info queries |
| 374-375 | kevent_qos/kevent_id | Quality-of-service kevents |
| 38 | crossarch_trap | Rosetta cross-arch trap |
| 490 | netagent_trigger | Network agent trigger |
| 491 | stack_snapshot_with_config | Kernel stack snapshot |
| 492 | microstackshot | Micro stackshot |
| 493 | grab_pgo_data | PGO profile data |
| 494 | persona | Persona management |
| 500 | getentropy | `int getentropy(void *buffer, size_t size)` |
| 503-514 | __nexus_* / __channel_* | Skywalk networking |
| 525 | net_qos_guideline | Network QoS |
| 527 | ntp_adjtime | NTP adjust |
| 528 | ntp_gettime | NTP time |
| 529 | os_fault_with_payload | Kernel fault with payload |
| 531 | __mach_bridge_remote_time | Bridge OS time sync |
| 533 | log_data | Kernel log data |
| 535 | objc_bp_assist_cfg_np | ObjC breakpoint assist |
| 548 | debug_syscall_reject_config | Syscall reject config |

---

## 3. Mach Traps

Fast kernel-entry points (not IPC-based). These bypass the Mach message layer.

| Trap # | Name | Signature | Category |
|--------|------|-----------|----------|
| -3 | MACH_ARM_TRAP_ABSTIME | (arm64 fast absolute time) | Timer |
| -4 | MACH_ARM_TRAP_CONTTIME | (arm64 fast continuous time) | Timer |
| 1 | mach_reply_port | `mach_port_name_t mach_reply_port()` | Port |
| 2 | thread_get_special_reply_port | `mach_port_name_t thread_get_special_reply_port()` | Port |
| 3 | thread_self_trap | `mach_port_name_t thread_self_trap()` | Thread |
| 4 | task_self_trap | `mach_port_name_t task_self_trap()` | Task |
| 5 | host_self_trap | `mach_port_name_t host_self_trap()` | Host |
| 6 | mach_msg_trap | `mach_msg_return_t mach_msg_trap(msg, option, send_size, rcv_size, rcv_name, timeout, notify)` | IPC |
| 7 | mach_msg_overwrite_trap | `mach_msg_return_t mach_msg_overwrite_trap(msg, option, ...)` | IPC |
| 8 | mach_msg2_trap | `mach_msg_return_t mach_msg2_trap(data, options, ...)` | IPC |
| 9 | semaphore_signal_trap | `kern_return_t semaphore_signal_trap(signal_name)` | Sync |
| 10 | semaphore_signal_all_trap | `kern_return_t semaphore_signal_all_trap(signal_name)` | Sync |
| 11 | semaphore_signal_thread_trap | `kern_return_t semaphore_signal_thread_trap(signal_name, thread_name)` | Sync |
| 12 | semaphore_wait_trap | `kern_return_t semaphore_wait_trap(wait_name)` | Sync |
| 13 | semaphore_wait_signal_trap | `kern_return_t semaphore_wait_signal_trap(wait_name, signal_name)` | Sync |
| 14 | semaphore_timedwait_trap | `kern_return_t semaphore_timedwait_trap(wait_name, sec, nsec)` | Sync |
| 15 | semaphore_timedwait_signal_trap | `kern_return_t semaphore_timedwait_signal_trap(wait_name, signal_name, sec, nsec)` | Sync |
| 16 | clock_sleep_trap | `kern_return_t clock_sleep_trap(clock_name, sleep_type, sleep_sec, sleep_nsec, wakeup_time)` | Timer |
| 17 | _kernelrpc_mach_vm_allocate_trap | `kern_return_t(target, addr, size, flags)` | VM |
| 18 | _kernelrpc_mach_vm_deallocate_trap | `kern_return_t(target, address, size)` | VM |
| 19 | _kernelrpc_mach_vm_protect_trap | `kern_return_t(target, address, size, set_maximum, new_protection)` | VM |
| 20 | _kernelrpc_mach_vm_map_trap | `kern_return_t(target, address, size, mask, flags, cur_protection)` | VM |
| 21 | _kernelrpc_mach_vm_purgable_control_trap | `kern_return_t(target, address, control, state)` | VM |
| 22 | _kernelrpc_mach_port_allocate_trap | `kern_return_t(target, right, name)` | Port |
| 23 | _kernelrpc_mach_port_deallocate_trap | `kern_return_t(target, name)` | Port |
| 24 | _kernelrpc_mach_port_mod_refs_trap | `kern_return_t(target, name, right, delta)` | Port |
| 25 | _kernelrpc_mach_port_move_member_trap | `kern_return_t(target, member, after)` | Port |
| 26 | _kernelrpc_mach_port_insert_right_trap | `kern_return_t(target, name, poly, polyPoly)` | Port |
| 27 | _kernelrpc_mach_port_get_attributes_trap | `kern_return_t(target, name, flavor, port_info_out, port_info_outCnt)` | Port |
| 28 | _kernelrpc_mach_port_insert_member_trap | `kern_return_t(target, name, pset)` | Port |
| 29 | _kernelrpc_mach_port_extract_member_trap | `kern_return_t(target, name, pset)` | Port |
| 30 | _kernelrpc_mach_port_construct_trap | `kern_return_t(target, options, context, name)` | Port |
| 31 | _kernelrpc_mach_port_destruct_trap | `kern_return_t(target, name, srdelta, guard)` | Port |
| 32 | _kernelrpc_mach_port_guard_trap | `kern_return_t(target, name, guard, strict)` | Port |
| 33 | _kernelrpc_mach_port_unguard_trap | `kern_return_t(target, name, guard)` | Port |
| 34 | mach_generate_activity_id | `kern_return_t(target, count, activity_id)` | Activity |
| 35 | macx_swapon | `kern_return_t(filename, flags, size, priority)` | Swap |
| 36 | macx_swapoff | `kern_return_t(filename, flags)` | Swap |
| 37 | macx_triggers | `kern_return_t(hi_water, low_water, flags, alert_port)` | Swap |
| 38 | macx_backing_store_suspend | `kern_return_t(suspend)` | Swap |
| 39 | macx_backing_store_recovery | `kern_return_t(pid)` | Swap |
| 40 | swtch_pri | `boolean_t swtch_pri(pri)` | Sched |
| 41 | swtch | `boolean_t swtch()` | Sched |
| 42 | thread_switch | `kern_return_t(thread_name, option, option_time)` | Sched |
| 43 | mach_timebase_info_trap | `kern_return_t(info)` | Timer |
| 44 | mach_wait_until_trap | `kern_return_t(deadline)` | Timer |
| 45 | mk_timer_create_trap | `mach_port_name_t()` | Timer |
| 46 | mk_timer_destroy_trap | `kern_return_t(name)` | Timer |
| 47 | mk_timer_arm_trap | `kern_return_t(name, expire_time)` | Timer |
| 48 | mk_timer_arm_leeway_trap | `kern_return_t(name, mk_timer_flags, expire_time, mk_leeway)` | Timer |
| 49 | mk_timer_cancel_trap | `kern_return_t(name, result_time)` | Timer |
| 50 | task_for_pid | `kern_return_t(target_tport, pid, t)` | Task |
| 51 | task_name_for_pid | `kern_return_t(target_tport, pid, tn)` | Task |
| 52 | pid_for_task | `kern_return_t(t, x)` | Task |
| 53 | debug_control_port_for_pid | `kern_return_t(target_tport, pid, t)` | Debug |
| 54 | host_create_mach_voucher_trap | `kern_return_t(host, recipes, recipes_size, voucher)` | Voucher |
| 55 | mach_voucher_extract_attr_recipe_trap | `kern_return_t(voucher_name, key, recipe, recipe_size)` | Voucher |
| 56 | _kernelrpc_mach_port_type_trap | `kern_return_t(task, name, ptype)` | Port |
| 57 | _kernelrpc_mach_port_request_notification_trap | `kern_return_t(task, name, msgid, sync, notify, notifyPoly, previous)` | Port |
| 58 | iokit_user_client_trap | `kern_return_t(userClientRef, index, p1-p6)` | IOKit |
| 59 | task_dyld_process_info_notify_get | `kern_return_t(names_addr, names_count_addr)` | Dyld |
| 60 | _exclaves_ctl_trap | `kern_return_t(name, operation_and_flags, identifier, buffer, size, size2, offset, status)` | Exclaves |
| 61 | mach_vm_reclaim_update_kernel_accounting_trap | `mach_error_t(target_tport, bytes_reclaimed)` | VM |

---

## 4. Mach IPC Interfaces

Mach RPC interfaces (via mach_msg). Defined in `osfmk/mach/` headers.

### 4.1 Task Interface (`mach/task.h`)

| Function | Purpose |
|----------|---------|
| task_create | Create a child task |
| task_terminate | Terminate a task |
| task_threads | Get thread list for task |
| task_info | Get task info (multiple flavors) |
| task_set_info | Set task info |
| task_suspend | Suspend task execution |
| task_resume | Resume task execution |
| task_get_special_port | Get task special port |
| task_set_special_port | Set task special port |
| task_get_exception_ports | Get exception ports |
| task_set_exception_ports | Set exception ports |
| task_swap_exception_ports | Swap exception ports |
| task_policy_set | Set task scheduling policy |
| task_policy_get | Get task scheduling policy |
| task_sample | Sample task |
| task_get_state | Get task state |
| task_set_state | Set task state |
| task_set_phys_footprint_limit | Set physical footprint limit |
| task_get_phys_footprint_limit | Get physical footprint limit |
| task_suspend2 | Suspend task (v2) |
| task_resume2 | Resume task (v2) |
| task_purgable_info | Purgable memory info |
| task_get_mach_voucher | Get task voucher |
| task_set_mach_voucher | Set task voucher |
| task_swap_mach_voucher | Swap task voucher |
| task_generate_activity_id | Generate activity ID |
| task_t | Task port (send right) |
| task_inspect_t | Task inspect port |

### 4.2 Thread Interface (`mach/thread_act.h`)

| Function | Purpose |
|----------|---------|
| thread_terminate | Terminate thread |
| thread_get_state | Get thread state (register context) |
| thread_set_state | Set thread state |
| thread_info | Get thread info |
| thread_set_policy | Set thread scheduling policy |
| thread_policy | Thread policy |
| thread_policy_set | Set thread policy (v2) |
| thread_policy_get | Get thread policy (v2) |
| thread_sample | Sample thread |
| thread_get_special_port | Get thread special port |
| thread_set_special_port | Set thread special port |
| thread_create | Create thread |
| thread_create_running | Create running thread |
| thread_suspend | Suspend thread |
| thread_resume | Resume thread |
| thread_abort | Abort thread (deactivate) |
| thread_abort_safely | Safely abort thread |
| thread_depress_abort | Abort depression |
| thread_get_exception_ports | Get thread exception ports |
| thread_set_exception_ports | Set thread exception ports |
| thread_swap_exception_ports | Swap thread exception ports |
| thread_assign | Assign thread to processor set |
| thread_assign_default | Assign thread to default processor set |
| thread_get_assignment | Get thread's processor set assignment |
| thread_set_voucher | Set thread voucher |
| thread_get_voucher | Get thread voucher |

### 4.3 Host Interface (`mach/host_priv.h`, `mach/mach_host.h`)

| Function | Purpose |
|----------|---------|
| host_info | Get host info |
| host_kernel_version | Get kernel version string |
| host_page_size | Get page size |
| host_processor_info | Get processor info |
| host_get_io_master | Get I/O master port |
| host_get_clock_service | Get clock service |
| host_get_clock_control | Get clock control |
| host_get_special_port | Get host special port |
| host_set_special_port | Set host special port |
| host_get_exception_ports | Get host exception ports |
| host_set_exception_ports | Set host exception ports |
| host_processor_slots | Get processor slots |
| host_default_memory_manager | Get/set default memory manager |
| host_create_mach_voucher | Create voucher |
| host_register_mach_voucher_attr | Register voucher attribute |
| host_register_well_known_mach_voucher_attr | Register well-known voucher attr |
| host_set_atm_diagnostic_flag | Set ATM diagnostic flag |
| host_get_atm_diagnostic_flag | Get ATM diagnostic flag |
| host_set_multiuser_config_flags | Set multiuser config |
| host_get_multiuser_config_flags | Get multiuser config |
| host_check_multiuser_mode | Check multiuser mode |
| host_create_thread_cluster | Create thread cluster |
| host_get_primary_host_info | Get primary host info |
| mach_host_self | Get host self port |

### 4.4 Processor / Processor Set (`mach/processor.h`, `mach/processor_set.h`)

| Function | Purpose |
|----------|---------|
| processor_info | Get processor info |
| processor_start | Start processor |
| processor_exit | Exit processor |
| processor_control | Control processor |
| processor_get_assignment | Get processor set assignment |
| processor_set_default | Get default processor set |
| processor_set_create | Create processor set |
| processor_set_destroy | Destroy processor set |
| processor_set_info | Get processor set info |
| processor_set_tasks | Get tasks in processor set |
| processor_set_threads | Get threads in processor set |
| processor_set_policy_control | Control processor set policy |
| processor_set_max_priority | Set max priority |
| processor_set_policy_enable | Enable policy |
| processor_set_policy_disable | Disable policy |

### 4.5 VM Map Interface (`mach/vm_map.h`)

| Function | Purpose | Wine Mapping |
|----------|---------|-------------|
| mach_vm_allocate | Allocate VM | VirtualAlloc |
| mach_vm_deallocate | Deallocate VM | VirtualFree |
| mach_vm_protect | Change protection | VirtualProtect |
| mach_vm_inherit | Set inheritance | |
| mach_vm_read | Read VM | ReadProcessMemory |
| mach_vm_read_list | Read VM list | |
| mach_vm_write | Write VM | WriteProcessMemory |
| mach_vm_copy | Copy VM | |
| mach_vm_read_overwrite | Read with overwrite | |
| mach_vm_map | Map memory object | MapViewOfFile |
| mach_vm_machine_attribute | Set machine attributes | |
| mach_vm_remap | Remap VM | |
| mach_vm_page_query | Query page info | VirtualQuery |
| mach_vm_region_recurse | Get VM region info (recursive) | VirtualQueryEx |
| mach_vm_region | Get VM region info | VirtualQuery |
| mach_vm_purgable_control | Control purgable memory | |
| mach_vm_page_info | Get page info | |
| mach_vm_page_range_query | Query page range | |
| mach_vm_deferred_reclaim_buffer_create | Create reclaim buffer | |
| mach_vm_deferred_reclaim_buffer_reclaim | Reclaim from buffer | |
| mach_vm_deferred_reclaim_buffer_reclaim_all | Reclaim all | |
| vm_map | Map (32-bit) | |
| vm_allocate | Allocate (32-bit) | |
| vm_deallocate | Deallocate (32-bit) | |
| vm_protect | Protect (32-bit) | |
| vm_inherit | Inherit (32-bit) | |
| vm_read | Read (32-bit) | |
| vm_write | Write (32-bit) | |
| vm_copy | Copy (32-bit) | |
| vm_region | Region (32-bit) | |

### 4.6 Mach Port Interface (`mach/mach_port.h`)

| Function | Purpose |
|----------|---------|
| mach_port_allocate | Allocate port right |
| mach_port_deallocate | Deallocate port right |
| mach_port_insert_right | Insert right into port |
| mach_port_extract_right | Extract right from port |
| mach_port_mod_refs | Modify port reference count |
| mach_port_get_refs | Get port reference count |
| mach_port_move_member | Move port to port set |
| mach_port_destroy | Destroy port |
| mach_port_request_notification | Request port notification |
| mach_port_insert_member | Insert port into set |
| mach_port_extract_member | Extract port from set |
| mach_port_set_mscount | Set make-send count |
| mach_port_get_set_status | Get port set status |
| mach_port_names | Get port names |
| mach_port_type | Get port type |
| mach_port_info | Get port info |
| mach_port_get_attributes | Get port attributes |
| mach_port_set_attributes | Set port attributes |
| mach_port_construct | Construct port with options |
| mach_port_destruct | Destruct port with guard |
| mach_port_guard | Guard port |
| mach_port_unguard | Unguard port |
| mach_port_space_info | Get space info |
| mach_port_space_basic_info | Get basic space info |

### 4.7 Clock Interface (`mach/clock.h`, `mach/clock_priv.h`)

| Function | Purpose |
|----------|---------|
| clock_get_time | Get clock time |
| clock_set_time | Set clock time |
| clock_get_attributes | Get clock attributes |
| clock_alarm | Set clock alarm |
| clock_sleep | Sleep until clock time |

### 4.8 Semaphore Interface (`mach/semaphore.h`)

| Function | Purpose |
|----------|---------|
| semaphore_create | Create semaphore |
| semaphore_destroy | Destroy semaphore |
| semaphore_signal | Signal semaphore |
| semaphore_signal_all | Signal all waiters |
| semaphore_signal_thread | Signal specific thread |
| semaphore_wait | Wait on semaphore |
| semaphore_timedwait | Timed wait on semaphore |

### 4.9 Memory Object Interface (`mach/memory_object.h`)

| Function | Purpose |
|----------|---------|
| memory_object_data_initialize | Initialize data |
| memory_object_data_supply | Supply data |
| memory_object_data_request | Request data |
| memory_object_data_return | Return data |
| memory_object_data_unlock | Unlock data |
| memory_object_synchronize | Synchronize |
| memory_object_map | Map memory object |
| memory_object_last_unlock | Last unlock notification |
| memory_object_data_reclaim | Reclaim data |
| memory_object_signed | Mark signed |

---

## 5. Virtual Memory Management

### Internal Subsystem (`osfmk/vm/`)

| Component | Headers | Purpose |
|-----------|---------|---------|
| vm_map | `vm_map.h`, `vm_map_internal.h` | Virtual address space management |
| vm_object | `vm_object_internal.h`, `vm_object_xnu.h` | Memory object (backing store) |
| vm_page | `vm_page.h`, `vm_page_internal.h` | Physical page management |
| vm_fault | `vm_fault.h`, `vm_fault_internal.h` | Page fault handling |
| pmap | `pmap.h` | Physical map (HW page tables) |
| pmap_cs | `pmap_cs.h` | Physical map code signing |
| vm_kern | `vm_kern.h` | Kernel VM allocation |
| vm_pageout | `vm_pageout.h` | Pageout daemon |
| vm_compressor | `vm_compressor_*.h` | Memory compression |
| vm_shared_region | `vm_shared_region.h` | Shared region (dyld shared cache) |
| vm_reclaim | `vm_reclaim.h`, `vm_reclaim_internal.h` | Deferred VM reclamation |
| vm_ubc | `vm_ubc.h` | Unified buffer cache |
| vm_upl | `vm_upl.h` | Universal Page List |
| vm_purgeable | `vm_purgeable_*.h` | Purgable memory |
| memory_object | `memory_object.h`, `memory_object_internal.h` | External memory management |
| vm_sanitize | `vm_sanitize_internal.h` | VM bounds checking |

---

## 6. Process/Thread Management

### Internal Kernel Structures (`osfmk/kern/`)

| Component | Header | Purpose |
|-----------|--------|---------|
| task | `task.h` | Task (process) structure |
| thread | `thread.h` | Thread structure |
| processor | `processor.h` | CPU processor abstraction |
| sched | `sched.h`, `sched_prim.h` | Scheduler |
| sched_clutch | `sched_clutch.h` | Clutch scheduler |
| coalition | `coalition.h` | Process coalitions |
| ledger | `ledger.h` | Resource ledgers |
| exception | `exception.h` | Exception handling |
| ast | `ast.h` | Asynchronous system traps |
| ipc_tt | `ipc_tt.h` | IPC task/thread ports |
| ipc_kobject | `ipc_kobject.h` | IPC kernel objects |
| kpc | `kpc.h` | Kernel performance counters |
| kalloc | `kalloc.h` | Kernel memory allocation |
| zalloc | `zalloc.h` | Zone allocator |
| cs_blobs | `cs_blobs.h` | Code signing blobs |
| trustcache | `trustcache.h` | Trust cache |
|turnstile | `turnstile.h` | Turnstile (priority inheritance) |
| waitq | `waitq.h` | Wait queues |
| thread_call | `thread_call.h` | Deferred function calls |
| timer | `timer.h`, `timer_call.h` | Kernel timers |
| lock | `lock.h` | General locks |
| lock_mtx | `lock_mtx.h` | Mutexes |
| lock_rw | `lock_rw.h` | Read-write locks |
| exclaves_* | `exclaves_*.h` | Exclaves (secure enclaves) |

---

## 7. IOKit Class Hierarchy

### 7.1 Core Classes

| Class | Header | Purpose |
|-------|--------|---------|
| OSObject | (libkern) | Root base class (refcounted) |
| OSArray | (libkern) | Array container |
| OSDictionary | (libkern) | Dictionary container |
| OSString | (libkern) | String container |
| OSNumber | (libkern) | Number container |
| OSData | (libkern) | Data blob container |
| OSBoolean | (libkern) | Boolean container |
| OSSet | (libkern) | Set container |
| OSCollectionIterator | (libkern) | Collection iterator |
| IORegistryEntry | `IORegistryEntry.h` | Registry node (name, properties, path) |
| IOService | `IOService.h` | Base driver service (matching, lifecycle, power) |
| IOUserClient | `IOUserClient.h` | User-space → kernel RPC bridge |
| IOUserServer | `IOUserServer.h` | User-space driver server |
| IOWorkLoop | `IOWorkLoop.h` | Single-threaded event processing |
| IOEventSource | `IOEventSource.h` | Event source (base) |
| IOInterruptEventSource | `IOInterruptEventSource.h` | Hardware interrupt handler |
| IOFilterInterruptEventSource | `IOFilterInterruptEventSource.h` | Filtered interrupt handler |
| IOTimerEventSource | `IOTimerEventSource.h` | Timer event |
| IOCommandGate | `IOCommandGate.h` | Synchronized command execution |
| IOCommandQueue | `IOCommandQueue.h` | Asynchronous command queue |
| IOCommandPool | `IOCommandPool.h` | Command object pool |
| IOCommand | `IOCommand.h` | Command base |
| IONotifier | `IONotifier.h` | Interest notification |

### 7.2 Memory / DMA

| Class | Header | Purpose |
|-------|--------|---------|
| IOMemoryDescriptor | `IOMemoryDescriptor.h` | Abstract memory descriptor |
| IOBufferMemoryDescriptor | `IOBufferMemoryDescriptor.h` | Buffer memory descriptor |
| IOMultiMemoryDescriptor | `IOMultiMemoryDescriptor.h` | Multi-segment descriptor |
| IOInterleavedMemoryDescriptor | `IOInterleavedMemoryDescriptor.h` | Interleaved descriptor |
| IOSubMemoryDescriptor | `IOSubMemoryDescriptor.h` | Sub-range descriptor |
| IODeviceMemory | `IODeviceMemory.h` | Device memory range |
| IOGuardPageMemoryDescriptor | `IOGuardPageMemoryDescriptor.h` | Guard page descriptor |
| IODMACommand | `IODMACommand.h` | DMA command (modern) |
| IODMAController | `IODMAController.h` | DMA controller |
| IODMAEventSource | `IODMAEventSource.h` | DMA completion event |
| IOMemoryCursor | `IOMemoryCursor.h` | Physical address scatter/gather |
| IOMapper | `IOMapper.h` | IOMMU mapping |

### 7.3 Platform / Power

| Class | Header | Purpose |
|-------|--------|---------|
| IOPlatformExpert | `IOPlatformExpert.h` | Platform discovery / expert |
| ApplePlatformExpert | `platform/ApplePlatformExpert.h` | Apple-specific platform |
| AppleMacIO | `platform/AppleMacIO.h` | Mac I/O device |
| AppleMacIODevice | `platform/AppleMacIODevice.h` | Mac I/O device nub |
| AppleNMI | `platform/AppleNMI.h` | NMI handler |
| IOPlatformIO | `platform/IOPlatformIO.h` | Platform I/O |
| IOCPU | `IOCPU.h` | CPU abstraction |
| IOInterruptController | `IOInterruptController.h` | Interrupt controller |
| PassthruInterruptController | `PassthruInterruptController.h` | Passthrough interrupt |
| IOPwrController | `power/IOPwrController.h` | Power controller |
| RootDomain | `pwr_mgt/RootDomain.h` | Power domain root |
| IOPMpowerState | `pwr_mgt/IOPMpowerState.h` | Power state management |
| IOPMPowerSource | `pwr_mgt/IOPMPowerSource.h` | Power source |
| IOPowerConnection | `pwr_mgt/IOPowerConnection.h` | Power tree connection |
| IOServicePM | `IOServicePM.h` | Service power management |

### 7.4 Data Queue / IPC

| Class | Header | Purpose |
|-------|--------|---------|
| IODataQueue | `IODataQueue.h` | Lockless data queue |
| IOSharedDataQueue | `IOSharedDataQueue.h` | Shared data queue |
| IOCircularDataQueue | `IOCircularDataQueue.h` | Circular data queue |
| IODataQueueShared | `IODataQueueShared.h` | Shared queue structures |
| OSMessageNotification | `OSMessageNotification.h` | Message notification |

### 7.5 Storage / NVRAM / RTC

| Class | Header | Purpose |
|-------|--------|---------|
| IONVRAM | `IONVRAM.h` | NVRAM access |
| IONVRAMController | `nvram/IONVRAMController.h` | NVRAM controller |
| IORTCController | `rtc/IORTCController.h` | Real-time clock |
| IOWatchDogTimer | `system_management/IOWatchDogTimer.h` | Watchdog timer |
| AppleKeyStoreInterface | `AppleKeyStoreInterface.h` | Key store |

### 7.6 Diagnostics / Reporting

| Class | Header | Purpose |
|-------|--------|---------|
| IOReportTypes | `IOReportTypes.h` | Report types |
| IOKernelReporters | `IOKernelReporters.h` | Kernel reporters |
| IOStatistics | `IOStatistics.h` | IOKit statistics |
| IOKitDebug | `IOKitDebug.h` | Debug flags |
| IOKitDiagnosticsUserClient | `IOKitDiagnosticsUserClient.h` | Diagnostics UC |

### 7.7 Other

| Class | Header | Purpose |
|-------|--------|---------|
| IOCatalogue | `IOCatalogue.h` | Driver catalogue |
| IOBSD | `IOBSD.h` | BSD integration |
| IOExtensiblePaniclog | `IOExtensiblePaniclog.h` | Extensible panic log |
| IOHibernatePrivate | `IOHibernatePrivate.h` | Hibernate support |
| IOPolledInterface | `IOPolledInterface.h` | Polled I/O (hibernate) |
| IOPerfControl | `perfcontrol/IOPerfControl.h` | Performance control |
| IOSkywalkSupport | `skywalk/IOSkywalkSupport.h` | Skywalk networking |
| IOPlatformActions | `IOPlatformActions.h` | Platform actions |
| IORangeAllocator | `IORangeAllocator.h` | Range allocator |
| IOSyncer | `IOSyncer.h` | Synchronization primitive |
| IOConditionLock | `IOConditionLock.h` | Condition lock |
| IOLocks | `IOLocks.h` | Locking primitives |
| IOLib | `IOLib.h` | Library functions |
| IORPC | `IORPC.h` | RPC infrastructure |
| system | `system.h` | System include |

---

## 8. Security Framework (MACF)

### 8.1 MAC Framework Hooks (`security/mac_framework.h`)

The Mandatory Access Control Framework provides hooks for every security-relevant operation. These are the functions the kernel calls, which dispatch to registered policy modules (AMFI, Sandbox, etc.).

#### Credential Operations

| Hook | Purpose |
|------|---------|
| mac_cred_check_label_update | Check credential relabel |
| mac_cred_check_label_update_execve | Check exec-time relabel |
| mac_cred_check_visible | Check process visibility |
| mac_cred_label_alloc | Allocate credential label |
| mac_cred_label_associate | Associate credential label |
| mac_cred_label_associate_fork | Fork label |
| mac_cred_label_associate_kernel | Kernel label |
| mac_cred_label_associate_user | User label |
| mac_cred_label_destroy | Destroy label |
| mac_cred_label_externalize_audit | Externalize for audit |
| mac_cred_label_free | Free label |
| mac_cred_label_init | Initialize label |
| mac_cred_label_seal | Seal label |
| mac_cred_label_update | Update label |
| mac_cred_label_update_execve | Update at exec time |

#### Process Operations

| Hook | Purpose | Anti-Cheat Relevance |
|------|---------|---------------------|
| mac_proc_check_debug | Check ptrace/debug | **HIGH** — anti-cheat debug detection |
| mac_proc_check_dump_core | Check core dump | |
| mac_proc_check_proc_info | Check proc_info access | **HIGH** — process enumeration |
| mac_proc_check_get_cs_info | Check code signing info | **HIGH** — signing validation |
| mac_proc_check_set_cs_info | Set code signing info | **HIGH** — signing manipulation |
| mac_proc_check_fork | Check fork | |
| mac_proc_check_suspend_resume | Check suspend/resume | |
| mac_proc_check_get_task | Check get task port | **CRITICAL** — task port access |
| mac_proc_check_expose_task | Check expose task port | **CRITICAL** — task port exposure |
| mac_proc_check_get_movable_control_port | Check control port | |
| mac_proc_check_inherit_ipc_ports | Check IPC port inheritance | |
| mac_proc_check_iopolicysys | Check I/O policy | |
| mac_proc_check_getaudit | Check get audit info | |
| mac_proc_check_getauid | Check get audit UID | |
| mac_proc_check_dyld_process_info_notify_register | Check dyld notify | |
| mac_proc_check_ledger | Check ledger access | |
| mac_proc_check_map_anon | Check anonymous mmap | |
| mac_proc_check_memorystatus_control | Check memory status | |
| mac_proc_check_mprotect | Check mprotect | |
| mac_proc_check_run_cs_invalid | Check run with invalid CS | **CRITICAL** — code signing bypass |
| mac_proc_notify_cs_invalidated | CS invalidated notification | **HIGH** — signing invalidation |
| mac_proc_check_sched | Check scheduling | |
| mac_proc_check_setaudit | Check set audit | |
| mac_proc_check_setauid | Check set audit UID | |
| mac_proc_check_seteuid/setegid/setuid/setgid | Check UID/GID changes | |
| mac_proc_check_setreuid/setregid | Check real/effective ID | |
| mac_proc_check_settid | Check set TID | |
| mac_proc_check_signal | Check signal delivery | |
| mac_proc_check_syscall_unix | Check any BSD syscall | **CRITICAL** — syscall filtering |
| mac_proc_check_wait | Check wait | |
| mac_proc_notify_exit | Exit notification | |
| mac_proc_check_launch_constraints | Check launch constraints | **HIGH** — launch validation |

#### File/Vnode Operations

| Hook | Purpose |
|------|---------|
| mac_vnode_check_access | Check file access |
| mac_vnode_check_chdir | Check chdir |
| mac_vnode_check_chroot | Check chroot |
| mac_vnode_check_clone | Check file clone |
| mac_vnode_check_copyfile | Check file copy |
| mac_vnode_check_create | Check file create |
| mac_vnode_check_exec | Check exec | **HIGH** |
| mac_vnode_check_getattr | Check getattr |
| mac_vnode_check_ioctl | Check ioctl |
| mac_vnode_check_link | Check link |
| mac_vnode_check_lookup | Check path lookup |
| mac_vnode_check_mmap | Check mmap | **HIGH** |
| mac_vnode_check_open | Check open |
| mac_vnode_check_read | Check read |
| mac_vnode_check_rename | Check rename |
| mac_vnode_check_signature | Check code signature | **CRITICAL** |
| mac_vnode_check_supplemental_signature | Check supplemental signature | **HIGH** |
| mac_vnode_check_unlink | Check unlink |
| mac_vnode_check_write | Check write |
| mac_vnode_notify_create | Create notification |
| mac_vnode_notify_open | Open notification |
| mac_vnode_notify_rename | Rename notification |

#### Network Operations

| Hook | Purpose |
|------|---------|
| mac_socket_check_accept | Check accept |
| mac_socket_check_bind | Check bind |
| mac_socket_check_connect | Check connect |
| mac_socket_check_create | Check socket create |
| mac_socket_check_ioctl | Check socket ioctl |
| mac_socket_check_listen | Check listen |
| mac_socket_check_receive | Check receive |
| mac_socket_check_send | Check send |
| mac_socket_check_getsockopt | Check getsockopt |
| mac_socket_check_setsockopt | Check setsockopt |
| mac_socket_check_stat | Check socket stat |
| mac_skywalk_flow_check_connect | Check Skywalk flow |
| mac_skywalk_flow_check_listen | Check Skywalk flow listen |

#### IOKit Operations

| Hook | Purpose |
|------|---------|
| mac_iokit_check_open | Check IOKit open | **HIGH** — hardware access |
| mac_iokit_check_open_service | Check IOKit service open |
| mac_iokit_check_set_properties | Check set properties |
| mac_iokit_check_filter_properties | Check filter properties |
| mac_iokit_check_get_property | Check get property |
| mac_iokit_check_hid_control | Check HID control | **HIGH** — input simulation |

#### System Operations

| Hook | Purpose |
|------|---------|
| mac_system_check_acct | Check acct |
| mac_system_check_audit | Check audit |
| mac_system_check_auditctl | Check auditctl |
| mac_system_check_auditon | Check auditon |
| mac_system_check_host_priv | Check host privilege |
| mac_system_check_info | Check system info |
| mac_system_check_nfsd | Check NFS daemon |
| mac_system_check_reboot | Check reboot |
| mac_system_check_settime | Check set time |
| mac_system_check_swapoff | Check swapoff |
| mac_system_check_swapon | Check swapon |
| mac_system_check_sysctlbyname | Check sysctl |
| mac_system_check_kas_info | Check KAS info |
| mac_priv_check | Check privilege |
| mac_priv_grant | Grant privilege |

#### Pipe / POSIX IPC / SysV IPC

| Hook | Purpose |
|------|---------|
| mac_pipe_check_ioctl | Pipe ioctl |
| mac_pipe_check_read/write | Pipe read/write |
| mac_posixsem_check_* | POSIX semaphore checks |
| mac_posixshm_check_* | POSIX shared memory checks |
| mac_sysvmsg_check_* | SysV message queue checks |
| mac_sysvsem_check_* | SysV semaphore checks |
| mac_sysvshm_check_* | SysV shared memory checks |

#### Mount/VFS Operations

| Hook | Purpose |
|------|---------|
| mac_mount_check_mount | Check mount |
| mac_mount_check_umount | Check umount |
| mac_mount_check_stat | Check statfs |
| mac_mount_check_remount | Check remount |
| mac_mount_check_snapshot_* | Check snapshot operations |

### 8.2 MAC Policy Module Entry Points (`security/mac_policy.h`)

These are the typedefs that MAC policy modules (AMFI, Sandbox, EndpointSecurity) implement:

| Entry Point | Purpose |
|-------------|---------|
| mpo_audit_check_postselect | Audit post-selection |
| mpo_audit_check_preselect | Audit pre-selection |
| mpo_cred_check_label_update | Credential relabel check |
| mpo_cred_check_label_update_execve | Exec-time relabel check |
| mpo_cred_check_visible | Process visibility |
| mpo_cred_label_associate | Credential label create |
| mpo_cred_label_associate_fork | Fork label |
| mpo_cred_label_associate_kernel | Kernel process label |
| mpo_cred_label_associate_user | User label |
| mpo_cred_label_destroy | Label destroy |
| mpo_cred_label_externalize | Label externalize |
| mpo_cred_label_init | Label init |
| mpo_cred_label_internalize | Label internalize |
| mpo_cred_label_update | Label update |
| mpo_cred_label_update_execve | Exec-time label update |
| mpo_devfs_label_associate_device | Devfs device label |
| mpo_devfs_label_associate_directory | Devfs dir label |
| mpo_devfs_label_copy | Devfs label copy |
| mpo_devfs_label_destroy | Devfs label destroy |
| mpo_devfs_label_init | Devfs label init |
| mpo_devfs_label_update | Devfs label update |
| mpo_exc_action_check_exception_send | Exception action check |
| mpo_exc_action_label_associate | Exception action label |
| mpo_exc_action_label_destroy | Exception action label destroy |
| mpo_exc_action_label_populate | Exception action populate |
| mpo_exc_action_label_init | Exception action label init |
| mpo_exc_action_label_update | Exception action label update |
| mpo_file_check_change_offset | File offset change |
| mpo_file_check_create | File descriptor create |
| mpo_file_check_dup | File descriptor dup |
| mpo_file_check_fcntl | File fcntl |
| mpo_file_check_get | File get label |
| mpo_file_check_get_offset | File get offset |
| mpo_file_check_inherit | File inherit |
| mpo_file_check_ioctl | File ioctl |
| mpo_file_check_lock | File lock |
| mpo_file_check_library_validation | Library validation |
| mpo_file_check_mmap | File mmap |
| mpo_file_check_mmap_downgrade | Mmap protection downgrade |
| mpo_file_check_receive | File receive |
| mpo_file_check_set | File set label |
| mpo_file_notify_close | File close notification |
| mpo_file_label_associate | File label associate |
| mpo_file_label_destroy | File label destroy |
| mpo_file_label_init | File label init |
| mpo_iokit_check_open | IOKit open check |
| mpo_iokit_check_open_service | IOKit service open check |
| mpo_iokit_check_set_properties | IOKit set properties |
| mpo_iokit_check_filter_properties | IOKit filter properties |
| mpo_iokit_check_get_property | IOKit get property |
| mpo_iokit_check_hid_control | HID control check |
| mpo_mount_check_* | Mount operation checks |
| mpo_pipe_check_* | Pipe operation checks |
| mpo_posixsem_check_* | POSIX sem checks |
| mpo_posixshm_check_* | POSIX shm checks |
| mpo_proc_check_launch_constraints | Launch constraint check |
| mpo_proc_check_debug | Debug check |
| mpo_proc_check_get_task | Get task port check |
| mpo_proc_check_expose_task | Expose task port check |
| mpo_proc_check_fork | Fork check |
| mpo_proc_check_signal | Signal check |
| mpo_proc_check_syscall_unix | Unix syscall check |
| mpo_socket_check_* | Socket operation checks |
| mpo_sysvmsg_check_* | SysV message checks |
| mpo_sysvsem_check_* | SysV sem checks |
| mpo_sysvshm_check_* | SysV shm checks |
| mpo_vnode_check_* | Vnode operation checks |

---

## 9. Network Stack

### BSD Layer Network Sources (`bsd/net/`)

| Component | Files | Purpose |
|-----------|-------|---------|
| Socket layer | `bsd/sys/socket.h`, `bsd/kern/uipc_socket.c` | Socket API implementation |
| TCP/UDP | `bsd/netinet/tcp_*`, `bsd/netinet/udp_*` | Transport protocols |
| IP | `bsd/netinet/ip_*` | Internet protocol |
| ICMP | `bsd/netinet/ip_icmp.*` | ICMP |
| RAW IP | `bsd/netinet/raw_ip.*` | Raw sockets |
| PF (packet filter) | `bsd/net/pf_*` | Packet filter firewall |
| BPF | `bsd/net/bpf.*` | Berkeley packet filter |
| Network interfaces | `bsd/net/if_*` | Interface management |
| Routes | `bsd/net/route.*` | Routing |
| NECP | `bsd/net/necp.*` | Network Extension Control Policy |
| Skywalk | `bsd/skywalk/*` | Kernel-bypass networking |
| Network agent | `bsd/net/netagent.*` | Network agent framework |
| Tracker | `bsd/net/network_agent_*` | Network tracker |

---

## 10. VFS / Filesystem

### BSD VFS Layer (`bsd/vfs/`)

| Component | Files | Purpose |
|-----------|-------|---------|
| VFS core | `bsd/vfs/vfs_*` | VFS operations |
| Vnode | `bsd/vfs/vnode_if.*` | Vnode interface |
| Name cache | `bsd/vfs/vfs_cache.c` | Path name cache |
| Bio | `bsd/vfs/vfs_bio.c` | Buffer cache |
| Mount | `bsd/vfs/vfs_mount.*` | Mount operations |
| Lookup | `bsd/vfs/vfs_lookup.c` | Path lookup |
| Subr | `bsd/vfs/vfs_subr.c` | VFS utilities |
| Support | `bsd/vfs/vfs_support.c` | VFS support |
| Syscalls | `bsd/vfs/vfs_syscalls.c` | VFS syscall impl |
| Attrlist | `bsd/vfs/vfs_attrlist.c` | Attribute list ops |
| Clone | `bsd/vfs/vfs_clone.c` | Clone file support |
| Dmapi | `bsd/vfs/vfs_dmapi.c` | DMAPI |
| Doc access | `bsd/vfs/vfs_doc_tombstone.c` | Document access |

### Filesystem Implementations

| FS | Location | Purpose |
|----|----------|---------|
| APFS | `bsd/sg/` (not in open source) | Apple File System |
| HFS+ | `bsd/hfs/` | HFS Plus |
| NFS | `bsd/nfs/` | Network File System |
| msdos | `bsd/msdos/` | FAT filesystem |
| NTFS | `bsd/ntfs/` | NTFS (read-only) |
| smb | `bsd/smbfs/` | SMB/CIFS |
| autofs | `bsd/autofs/` | Automounter |
| fdesc | `bsd/fdesc/` | File descriptor FS |
| nullfs | `bsd/nullfs/` | Null/loopback FS |
| union | `bsd/unionfs/` | Union filesystem |

---

## 11. Anti-Cheat Relevance Matrix

### Key syscall mappings for Wine anti-cheat compatibility:

| Windows NT API | macOS XNU Equivalent | Syscall/Trap | Priority |
|----------------|---------------------|--------------|----------|
| NtCreateProcess | fork/posix_spawn + execve | 2, 59, 244 | HIGH |
| NtTerminateProcess | exit/kill/terminate_with_payload | 1, 37, 520 | HIGH |
| NtOpenProcess | proc_info + task_for_pid | 336, 538, 539 | CRITICAL |
| NtOpenThread | thread_act IPC | mach trap 3 | CRITICAL |
| NtQuerySystemInformation | sysctl/proc_info | 202, 336, 545 | CRITICAL |
| NtQueryInformationProcess | proc_info | 336 | HIGH |
| NtReadVirtualMemory | mach_vm_read | mach IPC | CRITICAL |
| NtWriteVirtualMemory | mach_vm_write | mach IPC | CRITICAL |
| NtVirtualAlloc | mmap/mach_vm_allocate | 197, mach trap 17 | CRITICAL |
| NtVirtualFree | munmap/mach_vm_deallocate | 73, mach trap 18 | CRITICAL |
| NtVirtualProtect | mprotect/mach_vm_protect | 74, mach trap 19 | CRITICAL |
| NtVirtualQuery | mach_vm_region | mach IPC | HIGH |
| NtMapViewOfFile | mmap | 197 | CRITICAL |
| NtUnmapViewOfFile | munmap | 73 | CRITICAL |
| NtCreateFile | open/openat | 5, 463 | HIGH |
| NtReadFile | read/pread | 3, 153 | HIGH |
| NtWriteFile | write/pwrite | 4, 154 | HIGH |
| NtClose | close | 6 | HIGH |
| NtDeviceIoControlFile | ioctl | 54 | CRITICAL |
| NtCreateSection | shm_open/mmap | 266, 197 | HIGH |
| NtOpenSection | shm_open | 266 | MEDIUM |
| NtQueryVirtualMemory | mach_vm_region_recurse | mach IPC | HIGH |
| NtSetContextThread | thread_set_state | mach IPC | CRITICAL |
| NtGetContextThread | thread_get_state | mach IPC | CRITICAL |
| NtSuspendThread | task_suspend/thread_suspend | mach IPC | HIGH |
| NtResumeThread | task_resume/thread_resume | mach IPC | HIGH |
| NtCreateMutex | psynch_mutexwait/mutexdrop | 301, 302 | HIGH |
| NtCreateSemaphore | sem_open/semaphore_create | 268, mach IPC | MEDIUM |
| NtCreateEvent | psynch_cv* | 303-305 | MEDIUM |
| NtWaitForSingleObject | kevent/select/poll | 363, 93, 230 | MEDIUM |
| NtConnectPort | mach_msg/connect | mach trap 6 | HIGH |
| NtRequestPort | mach_msg | mach trap 6 | HIGH |
| NtRequestWaitReplyPort | mach_msg | mach trap 6 | HIGH |
| NtCreatePort | mach_port_allocate | mach trap 22 | HIGH |
| NtSetInformationProcess | proc_info/sysctl | 336, 202 | MEDIUM |
| NtProtectVirtualMemory | mprotect | 74 | CRITICAL |
| NtAllocateVirtualMemory | mmap/mach_vm_allocate | 197, mach trap 17 | CRITICAL |
| NtFreeVirtualMemory | munmap/mach_vm_deallocate | 73, mach trap 18 | CRITICAL |
| NtQueryInformationThread | thread_info | mach IPC | HIGH |
| NtSetInformationThread | thread_policy_set | mach IPC | MEDIUM |
| NtSignalAndWaitForSingleObject | semaphore_wait_signal | mach trap 13 | LOW |
| NtDelayExecution | clock_sleep/nanosleep | mach trap 16 | LOW |
| NtLoadDriver | kext_load (no direct syscall) | IOKit | MEDIUM |
| NtUnloadDriver | kext_unload (no direct syscall) | IOKit | MEDIUM |
| DbgUiConnectToDbg | ptrace + task_for_pid | 26, mach trap 50 | CRITICAL |
| DbgkpQueueMessage | mach_msg exception | mach IPC | CRITICAL |

### Security-sensitive anti-cheat touch points:

| Mechanism | macOS Equivalent | Used By |
|-----------|-----------------|---------|
| Kernel32 module enumeration | proc_info, dyld info | EAC, BattlEye |
| NtQuerySystemInformation process list | proc_info (callnum=1) | All AC |
| Handle table enumeration | No direct equivalent (ports) | BE, Vanguard |
| Memory scanning | mach_vm_read | All AC |
| Code signature verification | csops, mac_vnode_check_signature | All AC |
| Driver loading | IOKit matching + kext loading | Vanguard |
| Process hollowing detection | csops + code signing | EAC, BE |
| Thread context manipulation | thread_get/set_state | All AC |
- Register/DR modification | thread_set_state (ARM context) | All AC |
| Anti-debug (IsDebuggerPresent) | ptrace + P_TRACED | All AC |
| System call filtering | debug_syscall_reject | Sandbox, AC |
| HID input injection | mac_iokit_check_hid_control | EAC |
| Code integrity | csops + AMFI + MACF | All AC |
| Process memory protection | mprotect + VM_PROT | All AC |
| File integrity | getattrlist + code signatures | All AC |
