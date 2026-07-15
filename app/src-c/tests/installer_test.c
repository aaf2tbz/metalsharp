#include "../installer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *const required[] = {
    "x86_64-windows/d3d10core.dll", "x86_64-windows/d3d11.dll",
    "x86_64-windows/d3d12.dll", "x86_64-windows/dxgi.dll",
    "x86_64-windows/dxgi_dxmt.dll", "x86_64-windows/winemetal.dll",
    "x86_64-windows/nvapi64.dll", "x86_64-windows/nvngx.dll",
    "x86_64-unix/winemetal.so", "x86_64-unix/libc++.1.dylib",
    "x86_64-unix/libc++abi.1.dylib", "x86_64-unix/libunwind.1.dylib",
};

static void mkdirs(const char *path) {
    char buffer[4096];
    snprintf(buffer, sizeof(buffer), "%s", path);
    for (char *cursor = buffer + 1; *cursor != '\0'; ++cursor) {
        if (*cursor == '/') {
            *cursor = '\0';
            mkdir(buffer, 0700);
            *cursor = '/';
        }
    }
    mkdir(buffer, 0700);
}

int main(void) {
    char temp[] = "/tmp/metalsharp-installer-c.XXXXXX";
    assert(mkdtemp(temp) != NULL);

    for (size_t index = 0; index < sizeof(required) / sizeof(required[0]); ++index) {
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", temp, required[index]);
        char parent[4096];
        snprintf(parent, sizeof(parent), "%s", path);
        *strrchr(parent, '/') = '\0';
        mkdirs(parent);
        FILE *file = fopen(path, "wb");
        assert(file != NULL);
        assert(fwrite("rebuilt", 1, 7, file) == 7);
        assert(fclose(file) == 0);
    }

    assert(metalsharp_m12_runtime_complete(temp, strlen(temp)));
    char missing[4096];
    snprintf(missing, sizeof(missing), "%s/%s", temp, required[0]);
    assert(unlink(missing) == 0);
    assert(!metalsharp_m12_runtime_complete(temp, strlen(temp)));
    assert(!metalsharp_m12_runtime_artifact_valid(temp, strlen(temp), "../../escape", 12));

    char command[8192];
    snprintf(command, sizeof(command), "/bin/rm -rf -- '%s'", temp);
    assert(system(command) == 0);
    puts("C installer runtime validation passed.");
    return 0;
}
