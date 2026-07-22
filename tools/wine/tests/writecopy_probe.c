#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int fail(const char *step)
{
    fprintf(stderr, "%s failed: error=%lu\n", step, GetLastError());
    return 1;
}

int main(int argc, char **argv)
{
    char temp_path[MAX_PATH], file_path[MAX_PATH];
    HANDLE file = INVALID_HANDLE_VALUE, mapping = NULL;
    void *view = NULL;
    MEMORY_BASIC_INFORMATION info;
    DWORD written, old_protect = 0;
    BYTE data[4096] = {0};
    DWORD expected = argc > 1 && !strcmp(argv[1], "readwrite") ? PAGE_READWRITE : PAGE_WRITECOPY;
    int result = 1;

    if (!GetTempPathA(sizeof(temp_path), temp_path)) return fail("GetTempPathA");
    if (!GetTempFileNameA(temp_path, "wcp", 0, file_path)) return fail("GetTempFileNameA");

    file = CreateFileA(file_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (file == INVALID_HANDLE_VALUE) goto done;
    if (!WriteFile(file, data, sizeof(data), &written, NULL) || written != sizeof(data)) goto done;

    mapping = CreateFileMappingA(file, NULL, PAGE_WRITECOPY, 0, sizeof(data), NULL);
    if (!mapping) goto done;
    view = MapViewOfFile(mapping, FILE_MAP_COPY, 0, 0, sizeof(data));
    if (!view) goto done;
    if (!VirtualQuery(view, &info, sizeof(info))) goto done;
    if (!VirtualProtect(view, sizeof(data), PAGE_READONLY, &old_protect)) goto done;

    printf("initial=0x%lx old=0x%lx expected=0x%lx\n",
           (unsigned long)info.Protect, (unsigned long)old_protect, (unsigned long)expected);
    result = old_protect == expected ? 0 : 2;

done:
    if (result == 1) fprintf(stderr, "writecopy probe failed: error=%lu\n", GetLastError());
    if (view) UnmapViewOfFile(view);
    if (mapping) CloseHandle(mapping);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    DeleteFileA(file_path);
    return result;
}
