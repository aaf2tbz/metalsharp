#include <windows.h>
#include <stdio.h>
#include <string.h>

static int has_flag(const char *cmd, const char *flag) {
    return cmd && flag && strstr(cmd, flag) != NULL;
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
    if (!CreateProcessA(real, command_line, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        return 1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)exit_code;
}
