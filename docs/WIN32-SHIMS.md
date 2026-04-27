# Win32 Shims

MetalSharp implements Win32 API functions as shim libraries that the PE loader resolves at load time. When Windows code calls `CreateFileW`, it jumps to MetalSharp's implementation which delegates to POSIX/macOS APIs.

## Shim Registration

Shims are registered before loading the PE executable:

```cpp
auto kernel32 = win32::Kernel32Shim::create();
loader.registerShim("kernel32.dll", std::move(kernel32));
loader.registerShim("KERNEL32.dll", std::move(kernel32_copy));  // case-insensitive
```

Each shim library contains:
- `functions: map<string, function<void*()>>` — named exports
- `ordinals: map<uint16_t, function<void*()>>` — ordinal exports

The factory function returns a lambda that returns the function pointer. This indirection allows the loader to call the factory once per import resolution.

## DLL Coverage

### KERNEL32 (`src/win32/kernel32/Kernel32Shim.cpp`, `Kernel32Extra.cpp`)

220+ functions covering:
- **Memory**: VirtualAlloc (mmap), VirtualFree (munmap), HeapAlloc/Free (malloc/free), VirtualProtect (mprotect)
- **Threading**: CreateThread (pthread_create), ExitThread (pthread_exit), GetCurrentThreadId
- **Synchronization**: CreateEvent/SetEvent/ResetEvent (pthread_cond), CreateMutex (pthread_mutex), CreateSemaphore (pthread_cond + counter), SleepConditionVariableSRW (pthread_cond_timedwait), InitializeCriticalSection (pthread_mutex)
- **File I/O**: CreateFileW (open), ReadFile/WriteFile (read/write), CloseHandle (close), FindFirstFileW/FindNextFileW (opendir/readdir), GetFileSize (fstat), GetFileAttributesW (stat)
- **Process**: CreateProcessW (fork+exec), ExitProcess (exit), GetCommandLineW (returns METALSHARP_CMDLINE env var)
- **Time**: GetTickCount (clock_gettime), GetSystemTime, GetLocalTime, QueryPerformanceCounter
- **Module**: GetModuleHandleW, LoadLibraryW, GetProcAddress, FreeLibrary
- **TLS**: TlsAlloc/TlsFree/TlsGetValue/TlsSetValue (pthread_key_t)
- **Environment**: GetEnvironmentVariableW (getenv), SetEnvironmentVariableW (setenv), ExpandEnvironmentStringsW
- **Error handling**: GetLastError/SetLastError (thread-local), RaiseException, SetUnhandledExceptionFilter, AddVectoredExceptionHandler

### USER32 (`src/win32/user32/User32Shim.cpp`, `WindowManager.mm`)

60+ functions covering:
- **Windows**: CreateWindowExW (NSWindow), ShowWindow, DestroyWindow, SetWindowPos, MoveWindow
- **Message pump**: GetMessageW, PeekMessageW, TranslateMessage, DispatchMessageW, SendMessageW, PostMessageW, PostQuitMessage
- **Input**: TranslateMessage (NSEvent → MSG), RegisterClassExW
- **Strings**: wsprintfA (custom format string parser — MSABI can't use va_start)
- **Misc**: GetWindowRect, GetClientRect, SetWindowTextW, GetWindowTextW, GetForegroundWindow, IsWindow, MessageBoxW

### GDI32 (`src/win32/extra/ExtraShims.cpp`)

30+ functions covering:
- **Drawing**: BitBlt, StretchBlt, PatBlt, Rectangle, FillRect, DrawTextW/A, TextOutW/A
- **Objects**: CreateFontIndirectW, CreateFontW, SelectObject, DeleteObject, GetObjectW
- **Bitmaps**: CreateCompatibleBitmap, CreateBitmap, CreateSolidBrush, SetDIBitsToDevice
- **DC management**: CreateCompatibleDC, DeleteDC, GdiFlush

### ADVAPI32 (`src/win32/extra/ExtraShims.cpp`)

14+ functions covering:
- **Registry**: RegOpenKeyExA/W, RegQueryValueExA/W, RegSetValueExA/W, RegCreateKeyExA, RegCloseKey
- Registry is backed by an in-memory store (`src/win32/kernel32/Registry.cpp`) with JSON persistence

### WS2_32 (`src/win32/kernel32/NetworkContext.cpp`)

28+ functions covering:
- **Sockets**: socket, connect, bind, listen, accept, send, recv, sendto, recvfrom
- **DNS**: getaddrinfo, gethostbyname, getpeername, getsockname
- **Options**: setsockopt, getsockopt, ioctlsocket
- **Async**: select (POSIX select with timeout)
- **SSL/TLS**: Backed by Security.framework (`src/win32/kernel32/SecureTransport.mm`)

### OLE32 / OLEAUT32 (`src/win32/extra/ExtraShims.cpp`)

- OLE32: CoInitialize, CoInitializeEx, CoUninitialize, CoCreateInstance (returns CLASS_NOT_AVAILABLE), CoTaskMemAlloc/Realloc/Free, StringFromGUID2, IIDFromString
- OLEAUT32: SysAllocString/Free/Len, SysAllocStringLen, VariantInit/Clear, SafeArrayCreate/Destroy/AccessData/UnaccessData/GetLBound/GetUBound

### VERSION (`src/win32/extra/ExtraShims.cpp`)

- GetFileVersionInfoSizeW, GetFileVersionInfoW (returns fake VS_FIXEDFILEINFO)
- VerQueryValueW (returns root block or translation table entry)

### ntdll (`src/win32/kernel32/NtdllShim.cpp`)

15+ functions covering:
- Heap allocation (RtlAllocateHeap → malloc)
- CRC32, memory comparisons
- Critical section management
- RtlCaptureContext (captures x86_64 register state)
- RtlLookupFunctionEntry (searches PE .pdata for exception info)

## Virtual File System (`src/win32/kernel32/VirtualFileSystem.cpp`)

Translates Windows paths to macOS paths:
- `C:\...` → `~/.metalsharp/prefix/drive_c/...`
- `\` → `/`
- Handles UNC paths, long paths (`\\?\C:\...`)
- Maintains fd → HANDLE mapping for file operations

## Registry (`src/win32/kernel32/Registry.cpp`)

In-memory registry with pre-seeded keys:
- `HKLM\Software\Valve\Steam` — Steam install path, language, PID
- `HKLM\Software\Valve\Steam\ActiveProcess` — Steam process info
- `HKCU\Software\Valve\Steam` — user settings
- `HKLM\System\CurrentControlSet\Control\Nls\CodePage` — ACP/OEM codepages
- Keyboard layout, DirectDraw, CTF entries

Persists to `~/.metalsharp/prefix/registry.json`.

## Handle Table Design

MetalSharp uses a unified handle table. Handles are tagged with type:

```cpp
enum class HandleType : uint8_t {
    File, Thread, Event, Mutex, Semaphore, Pipe, Socket, ...
};
```

Each handle stores:
- Type tag
- File descriptor (for files/sockets/pipes)
- pthread primitives (for events/mutexes/semaphores)
- Thread state (for thread handles)

`CloseHandle` checks the type and dispatches to the correct cleanup (close fd, pthread_join, etc.).

## MSABI Constraints

Functions decorated with `__attribute__((ms_abi))` use the Windows x86_64 calling convention. This means:

1. Arguments in RCX, RDX, R8, R9 (not RDI, RSI, RDX, RCX)
2. Shadow space (32 bytes) allocated by caller
3. Stack cleaned up by caller
4. `va_start` / `va_list` cannot be used — the compiler rejects it

For variadic functions like `wsprintfA`, MetalSharp implements a custom format string parser that reads arguments directly from the stack.

## Adding New Shims

To add a new Win32 function:

1. Implement as a `static` function with `MSABI` attribute in the appropriate `.cpp` file
2. Add it to the shim registration in the factory function (e.g., `addMissingKernel32`)
3. If it's in a new DLL, register it in `NativeLauncher.cpp`
4. Rebuild and test

Example:

```cpp
static BOOL MSABI shim_MyFunction(HANDLE hThing) {
    // translate to POSIX/macOS
    return 1;
}

// In the factory:
lib.functions["MyFunction"] = fn((void*)shim_MyFunction);
```
