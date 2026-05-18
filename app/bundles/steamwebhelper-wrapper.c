#include <windows.h>
#include <stdio.h>
#include <string.h>

static void read_mode_file(const char *dir, char *mode, size_t mode_size) {
    char mp[MAX_PATH];
    snprintf(mp, MAX_PATH, "%s\\steamwebhelper_mode.txt", dir);
    FILE *f = fopen(mp, "rb");
    if (!f) return;
    if (fgets(mode, (int)mode_size, f)) {
        size_t n = strcspn(mode, "\r\n\t ");
        mode[n] = 0;
    }
    fclose(f);
}

static const char *cef_flags_for_mode(const char *mode) {
    if (strcmp(mode, "passthrough") == 0)
        return "";
    if (strcmp(mode, "swiftshader") == 0)
        return " --use-angle=swiftshader-webgl --use-gl=angle --disable-gpu-compositing";
    return " --in-process-gpu --disable-gpu";
}

int WINAPI WinMain(HINSTANCE h, HINSTANCE p, LPSTR cmd, int show) {
    char ep[MAX_PATH];
    GetModuleFileNameA(NULL, ep, MAX_PATH);
    char *sl = strrchr(ep, '\\');
    if (!sl) return 1;
    *sl = 0;
    char re[MAX_PATH];
    snprintf(re, MAX_PATH, "%s\\steamwebhelper_real.exe", ep);
    if (GetFileAttributesA(re) == INVALID_FILE_ATTRIBUTES) return 1;
    char mode[64] = {0};
    DWORD env_len = GetEnvironmentVariableA("METALSHARP_STEAM_CEF_MODE", mode, sizeof(mode));
    if (env_len == 0 || env_len >= sizeof(mode))
        read_mode_file(ep, mode, sizeof(mode));
    const char *flags = cef_flags_for_mode(mode);
    char cl[8192];
    if (strlen(cmd) > 0)
        snprintf(cl, sizeof(cl), "\"%s\" %s%s", re, cmd, flags);
    else
        snprintf(cl, sizeof(cl), "\"%s\"%s", re, flags);
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi = {0};
    if (!CreateProcessA(re, cl, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
        return 1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD ec = 1;
    GetExitCodeProcess(pi.hProcess, &ec);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)ec;
}
