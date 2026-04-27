# MetalSharp Roadmap: PE Loader → Playable Steam Games

**Current state:** Phases 8-10 complete. Real file I/O, registry, environment variables, NSWindow-backed HWND system with message pump and input translation, full networking stack (winsock, named pipes, SSL/TLS). PE loader works, steam.exe CRT init completes. D3D11/DXGI Metal backend is 90% implemented. 339/339 imports resolved across 14 DLLs. ~14K lines C++/ObjC++, ~600 lines TypeScript, ~800 lines Rust.

**End state:** Launch Steam from Electron app, login, download a game, play it with D3D→Metal rendering.

---

## Phase 8: Win32 Foundation — File I/O, Registry, Environment ✅ DONE

*steam.exe needs to read/write files, query registry, and check environment variables before it does anything useful.*

### 8.1 Real File I/O (~800 lines)

- `CreateFileW` → POSIX open with path translation: `C:\...` → `~/.metalsharp/prefix/drive_c/...`, `/` separator conversion, handle tracking
- `ReadFile` / `WriteFile` — POSIX read/write with tracked fd→handle mapping
- `CloseHandle` — actually close tracked file handles (currently NOP)
- `GetFileSize` / `SetFilePointer` / `SetFilePointerEx` — fstat / lseek
- `FindFirstFileW` / `FindNextFileW` / `FindClose` — opendir/readdir with wildcard matching
- `GetFileAttributesW` / `GetFileAttributesExW` — stat-based, return proper WIN32_FIND_DATA
- `CopyFileExW` — read + write loop
- `GetFullPathNameW` — path resolution relative to virtual C: drive
- Handle table — global `unordered_map<HANDLE, FileState>` with fd, position, path

### 8.2 Real Registry (~400 lines)

- In-memory registry store: `map<string, map<string, vector<uint8_t>>>` — hive→key→value
- `RegOpenKeyExA/W` — key lookup, return real HKEY
- `RegQueryValueExA/W` — return stored values, not ERROR_FILE_NOT_FOUND
- `RegSetValueExA/W` — actually store values
- `RegCreateKeyExA` — create missing keys
- `RegCloseKey` — release key handle
- Pre-seed Steam keys: `HKLM\Software\Valve\Steam`, `HKCU\Software\Valve\Steam` with install path, language, etc.
- Persist to disk: JSON/TOML file at `~/.metalsharp/prefix/registry.json`, loaded on startup

### 8.3 Environment Variables (~150 lines)

- `GetEnvironmentVariableW` — real getenv() + virtual env store
- `SetEnvironmentVariableW` — store in virtual map + call setenv
- `ExpandEnvironmentStringsW` — `%VAR%` substitution
- `GetEnvironmentStringsW` — build double-null-terminated block from env
- Pre-seed: `PATH`, `APPDATA`, `USERPROFILE`, `PROGRAMFILES`, `WINDIR`, `TEMP`, `HOMEDRIVE`, `HOMEPATH`, `COMPUTERNAME`, `USERNAME` mapped to prefix paths

**Deliverable:** steam.exe can read its config files, query registry for install paths, check environment variables. Should progress past init and start trying to create windows / connect to network.

**Estimated effort:** 2-3 days

---

## Phase 9: Window Management & Message Pump ✅ DONE

*Steam needs a real window to render its UI. No window = no login screen.*

### 9.1 NSWindow-backed HWND System (~600 lines)

- `CreateWindowExW` — create real NSWindow via Objective-C++, map HWND↔NSWindow in global table
- `RegisterClassExW` — store WNDCLASS structs (window proc, icon, cursor, etc.)
- `DestroyWindow` — close and release NSWindow
- `ShowWindow` / `UpdateWindow` — NSWindow makeKeyAndOrderFront / display
- `GetMessageW` / `PeekMessageW` — macOS event loop integration, NSEvent→MSG translation
- `TranslateMessage` / `DispatchMessageW` — call registered WNDPROC with translated MSG
- `SendMessageW` / `PostMessageW` — direct WNDPROC call / queue message
- `DefWindowProcW` — default handling (WM_DESTROY→PostQuitMessage, etc.)
- `GetWindowRect` / `GetClientRect` — read from NSWindow frame
- `SetWindowPos` / `MoveWindow` — set NSWindow frame
- `SetWindowTextW` / `GetWindowTextW` — NSWindow title
- Window proc dispatch — global `map<HWND, WNDPROC>`, PE code's WNDPROC called via ms_abi trampoline
- Message queue — per-window `std::queue<MSG>` fed by macOS event loop timer

### 9.2 Input Event Translation (~300 lines)

- Mouse events: NSEvent mouseDown/Up/Dragged → WM_LBUTTONDOWN/UP/MOUSEMOVE
- Keyboard events: NSEvent keyDown/Up → WM_KEYDOWN/UP with virtual key code translation
- Window resize: NSWindow resize → WM_SIZE message
- Window close: NSWindow close → WM_CLOSE → WM_DESTROY

**Deliverable:** Steam window appears on screen, shows its UI layout (even if rendering is broken). Mouse and keyboard input reach Steam's message loop.

**Estimated effort:** 3-4 days

---

## Phase 10: Networking — Steam Connectivity ✅ DONE

*Steam needs to talk to Valve's servers for login, game list, downloads.*

### 10.1 Async Networking (~400 lines)

- `select` — real POSIX select() with timeout (currently returns 0 immediately)
- `WSAAsyncSelect` — map socket events to window messages (for Steam's UI-driven networking)
- `WSAEventSelect` — map socket events to event objects
- `WSARecv` / `WSASend` — overlapped I/O with completion callbacks
- `WSARecvFrom` / `WSASendTo` — UDP support (Steam uses UDP for game server browser)
- `WSAIoctl` — SIO_GET_EXTENSION_FUNCTION_POINTER for AcceptEx, ConnectEx
- SSL/TLS — Steam requires HTTPS — wrap sockets with OpenSSL/LibreSSL or use Security.framework

### 10.2 Winsock Completion (~200 lines)

- Remaining ordinal exports (3,4,6,8,9,10,15,16,18,19,23,116,151) need real implementations
- `gethostbyname` — DNS resolution (POSIX already available)
- `WSAConnectByNameW` — for Steam's HTTP connections
- Error code mapping — WSAGetLastError should map errno→WSA error codes

### 10.3 Named Pipes & IPC (~200 lines)

- `CreateNamedPipeW` — Unix domain socket at `~/.metalsharp/prefix/pipe/...`
- `ConnectNamedPipe` — accept() on Unix socket
- `CallNamedPipeW` / `TransactNamedPipe` — send+recv on Unix socket
- Steam client pipe — `\\.\pipe\steam_client` — the main IPC channel Steam uses for game launches

**Deliverable:** Steam can connect to Valve servers, user can type credentials and login, game library loads.

**Estimated effort:** 3-4 days

---

## Phase 11: Threading & Synchronization — Real

*Steam is heavily multithreaded. Fake sync primitives will cause deadlocks and races.*

### 11.1 Real Synchronization Primitives (~400 lines)

- `WaitForSingleObject` — pthread_cond + timeout for events/mutexes/threads
- `WaitForMultipleObjects` — multiple condition variables with timeout
- `CreateEventA` / `SetEvent` / `ResetEvent` — real pthread_cond + state flag
- `CreateMutexA` / `ReleaseMutex` — real pthread_mutex (recursive)
- `CreateSemaphoreA` / `ReleaseSemaphore` — pthread_cond + counter
- `SleepConditionVariableSRW` — real pthread_cond_wait
- `WakeConditionVariable` / `WakeAllConditionVariable` — real pthread_cond_signal/broadcast
- Handle unification — single handle table covering files, events, mutexes, threads, pipes, sockets

### 11.2 Thread Improvements (~150 lines)

- `WaitForSingleObject` on thread HANDLE — pthread_join with timeout
- `GetExitCodeThread` — read actual thread return value
- Thread-local FLS — FLS callbacks should fire on thread exit; current FLS is global (bug)
- `TlsAlloc` per-thread cleanup

**Deliverable:** Steam's background threads (networking, downloads, UI rendering) run without deadlocking.

**Estimated effort:** 2-3 days

---

## Phase 12: PE Loader Hardening

*Before running game code, the loader needs to handle everything real apps throw at it.*

### 12.1 Missing Loader Features (~500 lines)

- TLS callbacks — walk TLS directory, call callbacks before entry point (anti-tamper needs this)
- Delay-load imports — parse delay import descriptor, lazy-resolve on first call
- Resource loading — `FindResourceA` / `LoadResource` / `LockResource` — walk .rsrc section
- Export forwarding — handle `module.function` exports that redirect to another DLL
- DLL thread attach — call `DllMain(DLL_THREAD_ATTACH)` when CreateThread runs
- Proper section protections — .text→RX, .rdata→R, .data→RW after relocation (debugging aid)

### 12.2 SEH / Exception Handling (~300 lines)

- VEH chain — `AddVectoredExceptionHandler` / `RemoveVectoredExceptionHandler`
- SEH chain — proper EXCEPTION_REGISTRATION chain via TEB
- `RtlVirtualUnwind` real impl — walk actual unwind info, restore full context
- `SetUnhandledExceptionFilter` — store and call real filter, not just log
- Crash recovery — individual thread crashes don't take down the process

**Deliverable:** Loader handles all PE features that real games and anti-cheat systems require.

**Estimated effort:** 3-4 days

---

## Phase 13: D3D ↔ PE Loader Integration

*The D3D→Metal backend exists but is wired for DLL injection via Wine. Now we need it working through the native PE loader.*

### 13.1 D3D Shim Registration (~300 lines)

- Register d3d11.dll shim — `D3D11CreateDevice` / `D3D11CreateDeviceAndSwapChain` in shim table
- Register dxgi.dll shim — `CreateDXGIFactory` / `CreateDXGIFactory1` / `CreateSwapChain`
- Register d3d12.dll shim — `D3D12CreateDevice` etc.
- Window integration — DXGI SwapChain's CreateWindowExW uses our real HWND system
- Present loop — SwapChain::Present → CAMetalLayer present → trigger NSEvent processing

### 13.2 HWND ↔ NSWindow ↔ MTLDrawable Pipeline (~200 lines)

- SwapChain HWND — when D3D creates a swap chain, attach CAMetalLayer to the HWND's NSWindow.contentView
- Present → MTLDrawable — get next drawable from layer, render to it, present
- Resize handling — WM_SIZE → ResizeBuffers → CAMetalLayer.drawableSize update
- Fullscreen — SetFullscreenState → NSWindow toggleFullScreen

**Deliverable:** A game calling `D3D11CreateDeviceAndSwapChain` gets a real Metal-backed window with working Present.

**Estimated effort:** 2-3 days

---

## Phase 14: First Game — Validation Target

*Pick a simple D3D11 game to validate the full pipeline. Candidate: a simple DirectX 11 sample app or an indie game with minimal Win32 usage.*

### 14.1 Test Infrastructure (~200 lines)

- Test PE build — build a minimal D3D11 triangle app with MSVC, test through MetalSharp
- Automated test runner — script that launches test exe, captures output, checks for crashes
- Frame validation — screenshot capture after first Present, compare to reference

### 14.2 Bug Fixes (iterative, ~500 lines)

- GDI font rendering — Steam UI uses GDI for text, `CreateFontW`, `DrawTextW` need basic rendering
- `wsprintfA` — real sprintf implementation (currently returns empty string)
- `GetModuleHandleExW` — proper module refcounting
- Timer callbacks — `SetTimer` / `KillTimer` with real NSTimer integration
- Clipboard — `GetClipboardData` / `SetClipboardData` via NSPasteboard

**Deliverable:** A simple D3D11 game renders a triangle (or simple scene) through MetalSharp.

**Estimated effort:** 2-3 days

---

## Phase 15: Steam Full Run — Integration Sprint

*Getting Steam itself to render its UI and be interactive.*

### 15.1 Steam-Specific Fixes (~400 lines)

- Steam service pipe — handle `\\.\pipe\steam` IPC protocol basics
- Steam registry keys — pre-seed all registry paths Steam queries on startup
- GDI rendering — `BitBlt`, `StretchBlt`, `DrawTextW`, `TextOutW` — basic software rendering to MTLTexture
- OLE/COM init — `CoInitialize`, `CoCreateInstance` — Steam uses COM for web rendering
- Version info — `GetFileVersionInfoW` / `VerQueryValueW` — Steam checks its own version
- Process creation — `CreateProcessW` — Steam launches child processes (steamwebhelper, etc.)

### 15.2 Electron App Updates (~200 lines)

- Rust backend: metalsharp launch mode — switch from Wine launch to NativeLauncher launch
- Config panel — select PE loader vs Wine mode
- Steam status detection — detect Windows Steam in prefix, show login state
- Game library integration — scan Steam library from Electron, show games

**Deliverable:** Steam window renders, user can navigate UI, type credentials, login.

**Estimated effort:** 3-4 days

---

## Phase 16: Download & Launch a Game

*The final mile — download a game through Steam, launch it, play it.*

### 16.1 Steam Downloads (~200 lines)

- File I/O stress test — Steam downloads write hundreds of files simultaneously, handle tracking
- Progress reporting — Rust backend monitors download progress, reports to Electron UI
- Manifest parsing — Steam .manifest / .acf files for download verification

### 16.2 Game Launch through Steam (~300 lines)

- `CreateProcessW` — launch game exe as new PE-loaded process
- Command line passing — Steam passes `-game`, `-steam`, app ID to launched games
- Working directory — set correct CWD for launched game
- Environment inheritance — pass Steam-related env vars to child process
- D3D hook — game loads d3d11.dll → gets our Metal shims automatically

### 16.3 Polish (~200 lines)

- Shader cache — persist DXBC→MSL compilations to disk (header exists, no impl)
- Pipeline cache — cache Metal pipeline states for faster subsequent launches
- Error reporting — graceful error dialogs in Electron when game crashes
- Logging — structured log viewer in Electron for debugging

**Deliverable:** Click Play in Electron → Steam launches → Login → Download game → Play through MetalSharp → Game renders via Metal.

**Estimated effort:** 3-4 days

---

## Summary Timeline

| Phase | Description | Effort | Cumulative | Status |
|-------|-------------|--------|------------|--------|
| **8** | File I/O, Registry, Environment | 2-3 days | 2-3 days | ✅ Done |
| **9** | Window Management & Input | 3-4 days | 5-7 days | ✅ Done |
| **10** | Networking (Steam connectivity) | 3-4 days | 8-11 days | ✅ Done |
| **11** | Real Threading & Sync | 2-3 days | 10-14 days | |
| **12** | PE Loader Hardening | 3-4 days | 13-18 days | |
| **13** | D3D ↔ PE Integration | 2-3 days | 15-21 days | |
| **14** | First Game Validation | 2-3 days | 17-24 days | |
| **15** | Steam Full Run | 3-4 days | 20-28 days | |
| **16** | Download & Launch Game | 3-4 days | 23-32 days | |

**Rough estimate: 4-6 weeks of focused work.**

**Critical path:** File I/O → Window Management → Networking → Steam Login → D3D Integration → Game Launch

Phases 11-12 (threading/SEH) can be interleaved as bugs surface. Phase 14 (test game) should happen as early as possible to validate the D3D pipeline before Steam's complexity is on top.
