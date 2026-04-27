# PE Loader

MetalSharp's native PE loader loads and executes Windows x86_64 PE32+ executables directly on macOS via Rosetta 2, without Wine.

## Loading Process

### 1. Parse PE Headers (`PELoader::parsePE`)

- Validates DOS header magic (`MZ` = `0x5A4D`)
- Reads `e_lfanew` to find PE signature (`PE\0\0` = `0x00004550`)
- Validates machine type is `IMAGE_FILE_MACHINE_AMD64`
- Reads optional header (PE32+): image base, section alignment, size of image
- Extracts data directory entries (import, export, TLS, reloc, exception, etc.)

### 2. Map Sections (`PELoader::mapSections`)

- Allocates `SizeOfImage` bytes via `mmap(MAP_PRIVATE | MAP_ANONYMOUS)`
- Copies PE headers to base address
- For each section: copies raw data from file offset to virtual address
- Sets initial protection to RWX for relocation processing

### 3. Process Relocations (`PELoader::processRelocations`)

- Walks the `.reloc` section (BASE_RELOCATION directory)
- For each `IMAGE_REL_BASED_DIR64` entry: applies `*target += delta` where delta = actual_base - preferred_base
- Skips `IMAGE_REL_BASED_ABSOLUTE` (padding entries)

### 4. Resolve Imports (`PELoader::resolveImports`)

- Walks import descriptor array
- For each DLL: looks up registered shim library by name (case-insensitive)
- For each import: resolves by name or ordinal from shim's function map
- Writes resolved addresses into the Import Address Table (IAT)
- Falls back to loading real PE DLLs from search paths

### 5. Delay-Load Imports (`PELoader::resolveDelayImports`)

- Same as regular imports but from the delay-load directory
- Resolves immediately rather than lazily (simplification)
- Stores module handles for potential future unloading

### 6. TLS Callbacks (`PELoader::processTLS`)

- Reads TLS directory from data directory entry
- Walks null-terminated callback array
- Calls each callback with `(hModule, DLL_PROCESS_ATTACH, nullptr)`

### 7. CFG Initialization (`PELoader::initCFG`)

- Reads LoadConfig directory (directory index 10)
- Extracts `GuardCFCheckFunctionPointer` and `GuardCFDispatchFunctionPointer`
- Allocates a small RWX page with `mov eax, 1; ret` (allow-all stub)
- Writes stub address into the CFG function pointer slots in the PE image

### 8. Section Protections (`PELoader::applySectionProtections`)

- For each section: reads `Characteristics` flags
- Maps to mprotect flags: `IMAGE_SCN_MEM_EXECUTE` → PROT_EXEC, `IMAGE_SCN_MEM_WRITE` → PROT_WRITE
- Calls mprotect on each section's virtual address range

### 9. Entry Point Call

The NativeLauncher sets up the execution environment:

1. **Fake TEB/PEB** — Allocates a 64KB mmap'd region as the Thread Environment Block:
   - Offset 0x30: pointer to self (TEB chain)
   - Offset 0x08/0x10: stack limits
   - Offset 0x60: PEB pointer
   - Offset 0x1480/0x1488: TLS slots
2. **Set `%gs:0x30`** — Inline assembly writes TEB address to the thread-local segment register
3. **Jump to entry** — Calls the PE entry point via an ms_abi function pointer

## Import Resolution Order

When resolving an import, the loader checks in this order:

1. **Registered shims** — Case-insensitive DLL name lookup in `m_shims` map
2. **Already-loaded DLLs** — Check `m_loadedDLLs` map
3. **Load from search paths** — Try to find and load a real PE DLL from:
   - The main executable's directory
   - Any paths added via `addSearchPath()`

## Export Forwarding

Some DLL exports forward to another DLL's function (e.g., `kernel32.GetStringTypeW → ntdll.GetStringTypeW`). The loader detects this when the export RVA falls within the export directory range, parses the forward string (`DLL.Function`), and recursively resolves it.

## DLL Loading

`loadDLL()` loads a PE DLL through the same pipeline: parse → map → reloc → imports → TLS → section protections → DllMain. DLLs are stored in `m_loadedDLLs` for reuse.

## MSABI Calling Convention

All shim functions use `__attribute__((ms_abi))` to use the Windows x86_64 calling convention. This is critical because PE code expects:
- RCX, RDX, R8, R9 for integer args (Windows) vs RDI, RSI, RDX, RCX (SystemV)
- Shadow space (32 bytes) on the stack
- Different return value handling for large types

The `MSABITrampolines.h` header provides `msabi_*` function declarations that PE code can call directly.

## Search Paths

The loader searches for DLLs in:
1. The main executable's directory (where the .exe is)
2. Paths added via `addSearchPath()`
3. Registered shim libraries (in-memory, no file needed)

## Handle System

Windows uses `HANDLE` (void pointers) for kernel objects. MetalSharp maintains:
- File handles: fd → HANDLE mapping in VirtualFileSystem
- Thread handles: pthread_t → HANDLE mapping
- Event/mutex/semaphore handles: pthread sync primitives
- Module handles: HMODULE → DLL name mapping for GetProcAddress
- Socket handles: fd → HANDLE mapping in NetworkContext

## Crash Diagnostics

The NativeLauncher installs a SIGSEGV/SIGBUS handler that prints:
- Signal number and faulting address
- Full x86_64 register dump (RIP, RSP, RAX, RBX, RCX, RDX)
- Crash RVA within the PE image (if RIP falls inside loaded image)
- 16-entry stack dump with PE RVA resolution for code pointers
