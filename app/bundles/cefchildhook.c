#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef BOOL(WINAPI *CreateProcessAType)(LPCSTR, LPSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCSTR, LPSTARTUPINFOA, LPPROCESS_INFORMATION);
typedef BOOL(WINAPI *CreateProcessWType)(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION);

static CreateProcessAType real_CreateProcessA = NULL;
static CreateProcessWType real_CreateProcessW = NULL;
static volatile LONG patching = 0;
static volatile LONG create_process_a_hooks = 0;
static volatile LONG create_process_w_hooks = 0;

static void log_line(const char *msg) {
    char temp[MAX_PATH] = {0};
    if (!GetTempPathA(MAX_PATH, temp)) {
        return;
    }
    char path[MAX_PATH] = {0};
    snprintf(path, MAX_PATH, "%smetalsharp-cefchildhook.log", temp);
    HANDLE file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(file, msg, (DWORD)strlen(msg), &written, NULL);
    WriteFile(file, "\r\n", 2, &written, NULL);
    CloseHandle(file);
}

static int has_flag_a(const char *cmd, const char *flag) {
    return cmd && flag && strstr(cmd, flag) != NULL;
}

static int has_flag_w(const wchar_t *cmd, const wchar_t *flag) {
    return cmd && flag && wcsstr(cmd, flag) != NULL;
}

static int should_patch_child_a(const char *cmd) {
    return has_flag_a(cmd, "--type=");
}

static int should_patch_child_w(const wchar_t *cmd) {
    return has_flag_w(cmd, L"--type=");
}

static char *append_cef_flags_a(const char *cmd) {
    const char *renderer_flags[] = {" --disable-gpu-compositing", NULL};
    const char *utility_flags[] = {" --use-angle=swiftshader-webgl", " --use-gl=angle", NULL};
    const char **flags = NULL;
    if (has_flag_a(cmd, "--type=renderer")) {
        flags = renderer_flags;
    } else if (has_flag_a(cmd, "--type=utility")) {
        flags = utility_flags;
    } else {
        return NULL;
    }
    size_t len = strlen(cmd) + 1;
    for (int i = 0; flags[i]; i++) {
        if (!has_flag_a(cmd, flags[i] + 1)) {
            len += strlen(flags[i]);
        }
    }
    char *out = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len);
    if (!out) {
        return NULL;
    }
    strcpy(out, cmd);
    for (int i = 0; flags[i]; i++) {
        if (!has_flag_a(cmd, flags[i] + 1)) {
            strcat(out, flags[i]);
        }
    }
    return out;
}

static wchar_t *append_cef_flags_w(const wchar_t *cmd) {
    const wchar_t *renderer_flags[] = {L" --disable-gpu-compositing", NULL};
    const wchar_t *utility_flags[] = {L" --use-angle=swiftshader-webgl", L" --use-gl=angle", NULL};
    const wchar_t **flags = NULL;
    if (has_flag_w(cmd, L"--type=renderer")) {
        flags = renderer_flags;
    } else if (has_flag_w(cmd, L"--type=utility")) {
        flags = utility_flags;
    } else {
        return NULL;
    }
    size_t len = wcslen(cmd) + 1;
    for (int i = 0; flags[i]; i++) {
        if (!has_flag_w(cmd, flags[i] + 1)) {
            len += wcslen(flags[i]);
        }
    }
    wchar_t *out = (wchar_t *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len * sizeof(wchar_t));
    if (!out) {
        return NULL;
    }
    wcscpy(out, cmd);
    for (int i = 0; flags[i]; i++) {
        if (!has_flag_w(cmd, flags[i] + 1)) {
            wcscat(out, flags[i]);
        }
    }
    return out;
}

BOOL WINAPI hook_CreateProcessA(
    LPCSTR app,
    LPSTR cmd,
    LPSECURITY_ATTRIBUTES proc_attrs,
    LPSECURITY_ATTRIBUTES thread_attrs,
    BOOL inherit_handles,
    DWORD flags,
    LPVOID env,
    LPCSTR cwd,
    LPSTARTUPINFOA startup,
    LPPROCESS_INFORMATION process_info
) {
    if (cmd && should_patch_child_a(cmd)) {
        char *patched = append_cef_flags_a(cmd);
        if (patched) {
            log_line("patched CreateProcessA CEF child");
            BOOL ok = real_CreateProcessA(app, patched, proc_attrs, thread_attrs, inherit_handles, flags, env, cwd, startup, process_info);
            HeapFree(GetProcessHeap(), 0, patched);
            return ok;
        }
    }
    return real_CreateProcessA(app, cmd, proc_attrs, thread_attrs, inherit_handles, flags, env, cwd, startup, process_info);
}

BOOL WINAPI hook_CreateProcessW(
    LPCWSTR app,
    LPWSTR cmd,
    LPSECURITY_ATTRIBUTES proc_attrs,
    LPSECURITY_ATTRIBUTES thread_attrs,
    BOOL inherit_handles,
    DWORD flags,
    LPVOID env,
    LPCWSTR cwd,
    LPSTARTUPINFOW startup,
    LPPROCESS_INFORMATION process_info
) {
    if (cmd && should_patch_child_w(cmd)) {
        wchar_t *patched = append_cef_flags_w(cmd);
        if (patched) {
            log_line("patched CreateProcessW CEF child");
            BOOL ok = real_CreateProcessW(app, patched, proc_attrs, thread_attrs, inherit_handles, flags, env, cwd, startup, process_info);
            HeapFree(GetProcessHeap(), 0, patched);
            return ok;
        }
    }
    return real_CreateProcessW(app, cmd, proc_attrs, thread_attrs, inherit_handles, flags, env, cwd, startup, process_info);
}

static void patch_imports(HMODULE module) {
    if (!module) {
        return;
    }
    unsigned char *base = (unsigned char *)module;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return;
    }
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return;
    }
    DWORD import_rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!import_rva) {
        return;
    }
    IMAGE_IMPORT_DESCRIPTOR *desc = (IMAGE_IMPORT_DESCRIPTOR *)(base + import_rva);
    for (; desc->Name; desc++) {
        IMAGE_THUNK_DATA *orig = (IMAGE_THUNK_DATA *)(base + desc->OriginalFirstThunk);
        IMAGE_THUNK_DATA *thunk = (IMAGE_THUNK_DATA *)(base + desc->FirstThunk);
        if (!desc->OriginalFirstThunk) {
            orig = thunk;
        }
        for (; orig->u1.AddressOfData && thunk->u1.Function; orig++, thunk++) {
            if (IMAGE_SNAP_BY_ORDINAL(orig->u1.Ordinal)) {
                continue;
            }
            IMAGE_IMPORT_BY_NAME *name = (IMAGE_IMPORT_BY_NAME *)(base + orig->u1.AddressOfData);
            FARPROC replacement = NULL;
            if (strcmp((char *)name->Name, "CreateProcessA") == 0) {
                replacement = (FARPROC)hook_CreateProcessA;
            } else if (strcmp((char *)name->Name, "CreateProcessW") == 0) {
                replacement = (FARPROC)hook_CreateProcessW;
            }
            if (!replacement || (FARPROC)thunk->u1.Function == replacement) {
                continue;
            }
            DWORD old_protect = 0;
            if (VirtualProtect(&thunk->u1.Function, sizeof(void *), PAGE_READWRITE, &old_protect)) {
                thunk->u1.Function = (ULONG_PTR)replacement;
                VirtualProtect(&thunk->u1.Function, sizeof(void *), old_protect, &old_protect);
                if (replacement == (FARPROC)hook_CreateProcessA) {
                    if (InterlockedIncrement(&create_process_a_hooks) <= 8) {
                        log_line("hooked CreateProcessA import");
                    }
                } else if (replacement == (FARPROC)hook_CreateProcessW) {
                    if (InterlockedIncrement(&create_process_w_hooks) <= 8) {
                        log_line("hooked CreateProcessW import");
                    }
                }
            }
        }
    }
}

static void patch_all_modules(void) {
    if (InterlockedExchange(&patching, 1) != 0) {
        return;
    }
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snapshot != INVALID_HANDLE_VALUE) {
        MODULEENTRY32 entry;
        entry.dwSize = sizeof(entry);
        if (Module32First(snapshot, &entry)) {
            do {
                patch_imports(entry.hModule);
            } while (Module32Next(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }
    InterlockedExchange(&patching, 0);
}

static DWORD WINAPI patch_thread(LPVOID param) {
    (void)param;
    for (int i = 0; i < 120; i++) {
        patch_all_modules();
        Sleep(250);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
        real_CreateProcessA = (CreateProcessAType)GetProcAddress(kernel32, "CreateProcessA");
        real_CreateProcessW = (CreateProcessWType)GetProcAddress(kernel32, "CreateProcessW");
        log_line("metalsharp cef child hook loaded");
        HANDLE thread = CreateThread(NULL, 0, patch_thread, NULL, 0, NULL);
        if (thread) {
            CloseHandle(thread);
        }
    }
    return TRUE;
}
