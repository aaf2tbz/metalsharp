#include "installer.h"
#include "bottles.h"
#include "launcher.h"
#include "runtime_surface.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

static const char* const m12_required_files[] = {
    "x86_64-windows/d3d10core.dll", "x86_64-windows/d3d11.dll",      "x86_64-windows/d3d12.dll",
    "x86_64-windows/dxgi.dll",      "x86_64-windows/dxgi_dxmt.dll",  "x86_64-windows/winemetal.dll",
    "x86_64-windows/nvapi64.dll",   "x86_64-windows/nvngx.dll",      "x86_64-unix/winemetal.so",
    "x86_64-unix/libc++.1.dylib",   "x86_64-unix/libc++abi.1.dylib", "x86_64-unix/libunwind.1.dylib",
};

static bool copy_slice(char* out, size_t out_size, const char* value, size_t value_len) {
    if (value == NULL || value_len == 0 || value_len >= out_size) {
        return false;
    }
    memcpy(out, value, value_len);
    out[value_len] = '\0';
    return true;
}

static bool regular_nonempty_file(const char* path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0;
}

bool metalsharp_find_bytes(const void* haystack, size_t haystack_len, const void* needle, size_t needle_len,
                           size_t* offset) {
    if (haystack == NULL || needle == NULL || needle_len == 0 || haystack_len < needle_len) {
        return false;
    }

    const uint8_t* bytes = haystack;
    const uint8_t* marker = needle;
    size_t index = 0;
    while (index <= haystack_len - needle_len) {
        const uint8_t* candidate = memchr(bytes + index, marker[0], haystack_len - needle_len - index + 1);
        if (candidate == NULL) {
            return false;
        }
        index = (size_t)(candidate - bytes);
        if (memcmp(candidate, marker, needle_len) == 0) {
            if (offset != NULL) {
                *offset = index;
            }
            return true;
        }
        ++index;
    }
    return false;
}

static bool executable_declares_agility(const char* path) {
    static const char* const markers[] = {
        "D3D12SDKVersion",
        "D3D12SDKPath",
        ".\\D3D12\\x64\\",
    };
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }

    enum { chunk_size = 64 * 1024, overlap_size = 32 };
    uint8_t buffer[chunk_size + overlap_size];
    size_t overlap = 0;
    bool found = false;
    while (!found) {
        const size_t read = fread(buffer + overlap, 1, chunk_size, file);
        const size_t available = overlap + read;
        for (size_t index = 0; index < sizeof(markers) / sizeof(markers[0]); ++index) {
            if (metalsharp_find_bytes(buffer, available, markers[index], strlen(markers[index]), NULL)) {
                found = true;
                break;
            }
        }
        if (read < chunk_size) {
            break;
        }
        overlap = available < overlap_size ? available : overlap_size;
        memmove(buffer, buffer + available - overlap, overlap);
    }
    fclose(file);
    return found;
}

static bool has_exe_extension(const char* name) {
    const size_t length = strlen(name);
    return length >= 4 && strcasecmp(name + length - 4, ".exe") == 0;
}

static bool game_path_requires_agility(const char* path, unsigned depth) {
    struct stat info;
    if (stat(path, &info) != 0) {
        return false;
    }
    if (S_ISREG(info.st_mode)) {
        return has_exe_extension(path) && executable_declares_agility(path);
    }
    if (!S_ISDIR(info.st_mode) || depth > 6) {
        return false;
    }

    DIR* directory = opendir(path);
    if (directory == NULL) {
        return false;
    }
    bool found = false;
    struct dirent* entry;
    while (!found && (entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char child[PATH_MAX];
        const int written = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        if (written >= 0 && (size_t)written < sizeof(child)) {
            found = game_path_requires_agility(child, depth + 1);
        }
    }
    closedir(directory);
    return found;
}

bool metalsharp_game_requires_agility(const char* game_path, size_t game_path_len) {
    char path[PATH_MAX];
    return copy_slice(path, sizeof(path), game_path, game_path_len) && game_path_requires_agility(path, 0);
}

static bool run_tool(const char* tool, char* const arguments[]) {
    pid_t process = 0;
    const int spawn_result = posix_spawn(&process, tool, NULL, NULL, arguments, environ);
    if (spawn_result != 0) {
        return false;
    }

    int status = 0;
    while (waitpid(process, &status, 0) < 0) {
        if (errno != EINTR) {
            return false;
        }
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static bool remove_tree(const char* path) {
    char* const arguments[] = {"rm", "-rf", "--", (char*)path, NULL};
    return run_tool("/bin/rm", arguments);
}

static bool ensure_parent_directories(const char* path) {
    char parent[PATH_MAX];
    const size_t length = strlen(path);
    if (length == 0 || length >= sizeof(parent)) {
        return false;
    }
    memcpy(parent, path, length + 1);
    char* final_separator = strrchr(parent, '/');
    if (final_separator == NULL) {
        return true;
    }
    *final_separator = '\0';
    for (char* cursor = parent + 1; *cursor != '\0'; ++cursor) {
        if (*cursor != '/') {
            continue;
        }
        *cursor = '\0';
        if (mkdir(parent, 0700) != 0 && errno != EEXIST) {
            return false;
        }
        *cursor = '/';
    }
    if (mkdir(parent, 0700) != 0 && errno != EEXIST) {
        return false;
    }
    struct stat info;
    return stat(parent, &info) == 0 && S_ISDIR(info.st_mode);
}

static bool agility_payload_complete(const char* root) {
    static const char* const required[] = {
        "build/native/bin/x64/D3D12Core.dll",
        "build/native/bin/x64/d3d12SDKLayers.dll",
    };
    for (size_t index = 0; index < sizeof(required) / sizeof(required[0]); ++index) {
        char artifact[PATH_MAX];
        const int written = snprintf(artifact, sizeof(artifact), "%s/%s", root, required[index]);
        if (written < 0 || (size_t)written >= sizeof(artifact) || !regular_nonempty_file(artifact)) {
            return false;
        }
    }
    return true;
}

bool metalsharp_extract_agility_package(const char* archive, size_t archive_len, const char* destination,
                                        size_t destination_len) {
    char archive_path[PATH_MAX];
    char destination_path[PATH_MAX];
    if (!copy_slice(archive_path, sizeof(archive_path), archive, archive_len) ||
        !copy_slice(destination_path, sizeof(destination_path), destination, destination_len) ||
        !regular_nonempty_file(archive_path)) {
        return false;
    }

    char extracting[PATH_MAX];
    char previous[PATH_MAX];
    const int extracting_len = snprintf(extracting, sizeof(extracting), "%s.extracting", destination_path);
    const int previous_len = snprintf(previous, sizeof(previous), "%s.previous", destination_path);
    if (extracting_len < 0 || (size_t)extracting_len >= sizeof(extracting) || previous_len < 0 ||
        (size_t)previous_len >= sizeof(previous)) {
        return false;
    }

    if (!ensure_parent_directories(extracting) || !remove_tree(extracting) || mkdir(extracting, 0700) != 0) {
        return false;
    }

    char* const unzip_arguments[] = {
        "unzip", "-qq", archive_path, "build/native/bin/x64/*", "-d", extracting, NULL,
    };
    if (!run_tool("/usr/bin/unzip", unzip_arguments) || !agility_payload_complete(extracting)) {
        remove_tree(extracting);
        return false;
    }

    if (!remove_tree(previous)) {
        remove_tree(extracting);
        return false;
    }
    const bool had_destination = rename(destination_path, previous) == 0;
    if (rename(extracting, destination_path) != 0) {
        if (had_destination) {
            rename(previous, destination_path);
        }
        remove_tree(extracting);
        return false;
    }
    if (had_destination) {
        remove_tree(previous);
    }
    return true;
}

static bool allowed_relative_path(const char* relative_path) {
    const size_t count = sizeof(m12_required_files) / sizeof(m12_required_files[0]);
    for (size_t index = 0; index < count; ++index) {
        if (strcmp(relative_path, m12_required_files[index]) == 0) {
            return true;
        }
    }
    return false;
}

bool metalsharp_m12_runtime_complete(const char* root, size_t root_len) {
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
    return metalsharp_reconcile_dxmt_surface(root_path, strlen(root_path)) &&
           metalsharp_launcher_runtime_ready("m12", root_path, strlen(root_path));
}

bool metalsharp_m12_runtime_artifact_valid(const char* home, size_t home_len, const char* relative_path,
                                           size_t relative_path_len) {
    char home_path[PATH_MAX];
    char relative[PATH_MAX];
    if (!copy_slice(home_path, sizeof(home_path), home, home_len) ||
        !copy_slice(relative, sizeof(relative), relative_path, relative_path_len) || !allowed_relative_path(relative)) {
        return false;
    }

    char root[PATH_MAX];
    const char* metalsharp_home = getenv("METALSHARP_HOME");
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
    const MetalsharpBottlePolicy* policy = metalsharp_bottle_policy("m12");
    return written >= 0 && (size_t)written < sizeof(artifact) &&
           metalsharp_bottle_artifact_required(policy, relative) &&
           metalsharp_launcher_runtime_ready("m12", root, strlen(root)) &&
           metalsharp_dxmt_artifact_current(root, strlen(root), relative, strlen(relative));
}
