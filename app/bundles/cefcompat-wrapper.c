#include <windows.h>
#include <stdio.h>
#include <string.h>

static int has_flag(const char *cmd, const char *flag) {
    return cmd && flag && strstr(cmd, flag) != NULL;
}

static int sibling_path(char *out, size_t out_size, const char *exe, const char *name) {
    if (!out || !exe || !name || out_size == 0) {
        return 0;
    }
    snprintf(out, out_size, "%s", exe);
    char *slash = strrchr(out, '\\');
    if (!slash) {
        return 0;
    }
    *(slash + 1) = 0;
    strncat(out, name, out_size - strlen(out) - 1);
    return 1;
}

static void inject_child_hook(HANDLE process, const char *hook_path) {
    if (!process || !hook_path || GetFileAttributesA(hook_path) == INVALID_FILE_ATTRIBUTES) {
        return;
    }
    SIZE_T bytes = strlen(hook_path) + 1;
    LPVOID remote = VirtualAllocEx(process, NULL, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        return;
    }
    if (!WriteProcessMemory(process, remote, hook_path, bytes, NULL)) {
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return;
    }
    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    FARPROC load_library = kernel32 ? GetProcAddress(kernel32, "LoadLibraryA") : NULL;
    if (!load_library) {
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return;
    }
    HANDLE thread = CreateRemoteThread(process, NULL, 0, (LPTHREAD_START_ROUTINE)load_library, remote, 0, NULL);
    if (thread) {
        WaitForSingleObject(thread, 5000);
        CloseHandle(thread);
    }
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
}

int WINAPI WinMain(HINSTANCE h, HINSTANCE p, LPSTR cmd, int show) {
    (void)h;
    (void)p;
    (void)show;

    char exe[MAX_PATH];
    if (!GetModuleFileNameA(NULL, exe, MAX_PATH)) {
        return 1;
    }

    char real[MAX_PATH];
    snprintf(real, MAX_PATH, "%s", exe);
    char *slash = strrchr(real, '\\');
    char *dot = strrchr(real, '.');
    if (!slash || !dot || dot < slash) {
        return 1;
    }

    *dot = 0;
    strncat(real, "_real.exe", MAX_PATH - strlen(real) - 1);
    if (GetFileAttributesA(real) == INVALID_FILE_ATTRIBUTES) {
        return 1;
    }

    char hook[MAX_PATH];
    int has_hook = sibling_path(hook, MAX_PATH, exe, "metalsharp-cefchildhook.dll");

    int is_child_process = has_flag(cmd, "--type=");
    char flags[256] = "";
    if (!is_child_process && !has_flag(cmd, "--in-process-gpu")) {
        strncat(flags, " --in-process-gpu", sizeof(flags) - strlen(flags) - 1);
    }
    if (!has_flag(cmd, "--disable-gpu")) {
        strncat(flags, " --disable-gpu", sizeof(flags) - strlen(flags) - 1);
    }

    char command_line[8192];
    if (cmd && strlen(cmd) > 0) {
        snprintf(command_line, sizeof(command_line), "\"%s\" %s%s", real, cmd, flags);
    } else {
        snprintf(command_line, sizeof(command_line), "\"%s\"%s", real, flags);
    }

    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi = {0};
    DWORD creation_flags = 0;
    if (!CreateProcessA(real, command_line, NULL, NULL, FALSE, creation_flags, NULL, NULL, &si, &pi)) {
        return 1;
    }

    if (has_hook && !is_child_process) {
        inject_child_hook(pi.hProcess, hook);
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)exit_code;
}
