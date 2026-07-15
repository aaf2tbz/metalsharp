#include "installer.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *const m12_required_files[] = {
    "x86_64-windows/d3d10core.dll",
    "x86_64-windows/d3d11.dll",
    "x86_64-windows/d3d12.dll",
    "x86_64-windows/dxgi.dll",
    "x86_64-windows/dxgi_dxmt.dll",
    "x86_64-windows/winemetal.dll",
    "x86_64-windows/nvapi64.dll",
    "x86_64-windows/nvngx.dll",
    "x86_64-unix/winemetal.so",
    "x86_64-unix/libc++.1.dylib",
    "x86_64-unix/libc++abi.1.dylib",
    "x86_64-unix/libunwind.1.dylib",
};

static bool copy_slice(char *out, size_t out_size, const char *value, size_t value_len) {
    if (value == NULL || value_len == 0 || value_len >= out_size) {
        return false;
    }
    memcpy(out, value, value_len);
    out[value_len] = '\0';
    return true;
}

static bool regular_nonempty_file(const char *path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0;
}

static bool allowed_relative_path(const char *relative_path) {
    const size_t count = sizeof(m12_required_files) / sizeof(m12_required_files[0]);
    for (size_t index = 0; index < count; ++index) {
        if (strcmp(relative_path, m12_required_files[index]) == 0) {
            return true;
        }
    }
    return false;
}

bool metalsharp_m12_runtime_complete(const char *root, size_t root_len) {
    char root_path[PATH_MAX];
    if (!copy_slice(root_path, sizeof(root_path), root, root_len)) {
        return false;
    }

    const size_t count = sizeof(m12_required_files) / sizeof(m12_required_files[0]);
    for (size_t index = 0; index < count; ++index) {
        char artifact[PATH_MAX];
        const int written = snprintf(artifact, sizeof(artifact), "%s/%s", root_path, m12_required_files[index]);
        if (written < 0 || (size_t)written >= sizeof(artifact) || !regular_nonempty_file(artifact)) {
            return false;
        }
    }
    return true;
}

bool metalsharp_m12_runtime_artifact_valid(
    const char *home,
    size_t home_len,
    const char *relative_path,
    size_t relative_path_len
) {
    char home_path[PATH_MAX];
    char relative[PATH_MAX];
    if (!copy_slice(home_path, sizeof(home_path), home, home_len)
        || !copy_slice(relative, sizeof(relative), relative_path, relative_path_len)
        || !allowed_relative_path(relative)) {
        return false;
    }

    char root[PATH_MAX];
    const char *metalsharp_home = getenv("METALSHARP_HOME");
    int written;
    if (metalsharp_home != NULL && metalsharp_home[0] != '\0') {
        written = snprintf(root, sizeof(root), "%s/runtime/wine/lib/dxmt_m12", metalsharp_home);
    } else {
        written = snprintf(root, sizeof(root), "%s/.metalsharp/runtime/wine/lib/dxmt_m12", home_path);
    }
    if (written < 0 || (size_t)written >= sizeof(root)) {
        return false;
    }

    char artifact[PATH_MAX];
    written = snprintf(artifact, sizeof(artifact), "%s/%s", root, relative);
    return written >= 0 && (size_t)written < sizeof(artifact) && regular_nonempty_file(artifact);
}
