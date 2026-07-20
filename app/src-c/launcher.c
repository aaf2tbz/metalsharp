#include "launcher.h"

#include "bottles.h"

#include <CommonCrypto/CommonDigest.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static const MetalsharpLaunchPolicy policies[] = {
    {
        "m12",
        "bin/metalsharp-wine",
        "dxmt",
        "lib/dxmt_m12/x86_64-windows",
        "lib/dxmt_m12/x86_64-unix:lib/wine/x86_64-unix",
        "winemetal.so",
        "opengl32,winemetal,d3d12,dxgi,dxgi_dxmt,d3d11,d3d10core=n,b;gameoverlayrenderer,gameoverlayrenderer64=d",
        true,
        true,
    },
    {
        "m11",
        "bin/metalsharp-wine",
        "dxmt",
        "lib/dxmt/x86_64-windows:lib/metalsharp/x86_64-windows",
        "lib/wine/x86_64-unix:lib/dxmt/x86_64-unix",
        "winemetal.so",
        "winemetal,dxgi,d3d11,d3d10core=n,b;gameoverlayrenderer,gameoverlayrenderer64=d",
        true,
        true,
    },
    {
        "m10",
        "bin/metalsharp-wine",
        "dxmt",
        "lib/wine/x86_64-windows:lib/dxmt/x86_64-windows:lib/metalsharp/x86_64-windows",
        "lib/wine/x86_64-unix:lib/dxmt/x86_64-unix",
        "winemetal.so",
        "winemetal,d3d10,d3d10_1,dxgi,d3d11,d3d10core=n,b;gameoverlayrenderer,gameoverlayrenderer64=d",
        true,
        true,
    },
    {
        "m11_32",
        "bin/metalsharp-wine",
        "dxmt",
        "lib/dxmt/i386-windows:lib/wine/i386-windows:lib/wine/x86_64-windows",
        "lib/wine/x86_64-unix:lib/dxmt/i386-unix:lib/wine",
        "winemetal.so",
        "d3d11,dxgi,winemetal=n,b;gameoverlayrenderer,gameoverlayrenderer64=d",
        true,
        true,
    },
    {
        "m10_32",
        "bin/metalsharp-wine",
        "dxmt",
        "lib/wine/i386-windows:lib/dxmt/i386-windows:lib/wine/x86_64-windows",
        "lib/wine/x86_64-unix:lib/dxmt/i386-unix:lib/wine",
        "winemetal.so",
        "d3d10,d3d10_1,d3d10core,d3d11,dxgi,winemetal=n,b;gameoverlayrenderer,gameoverlayrenderer64=d",
        true,
        true,
    },
};

const MetalsharpLaunchPolicy* metalsharp_launch_policy(const char* pipeline_id) {
    if (pipeline_id == NULL)
        return NULL;
    for (size_t i = 0; i < sizeof(policies) / sizeof(policies[0]); ++i)
        if (strcmp(pipeline_id, policies[i].pipeline_id) == 0)
            return &policies[i];
    return NULL;
}

bool metalsharp_launch_policy_valid(const MetalsharpLaunchPolicy* policy) {
    if (policy == NULL || metalsharp_bottle_policy(policy->pipeline_id) == NULL ||
        strcmp(policy->wine_binary, "bin/metalsharp-wine") != 0 || strcmp(policy->graphics_backend, "dxmt") != 0 ||
        strcmp(policy->winemetal_unixlib, "winemetal.so") != 0 || !policy->direct_executable ||
        !policy->steam_background_client || policy->windows_dll_path == NULL || policy->unix_library_path == NULL ||
        policy->dll_overrides == NULL)
        return false;
    if (strcmp(policy->pipeline_id, "m12") == 0)
        return strstr(policy->windows_dll_path, "dxmt_m12/x86_64-windows") != NULL &&
               strstr(policy->unix_library_path, "dxmt_m12/x86_64-unix") != NULL &&
               strstr(policy->dll_overrides, "d3d12") != NULL;
    return strstr(policy->dll_overrides, "d3d12") == NULL;
}

bool metalsharp_launcher_reserved_env_key(const char* pipeline_id, const char* key) {
    if (pipeline_id == NULL || key == NULL || strcmp(pipeline_id, "m12") != 0)
        return false;
    static const char* const reserved[] = {
        "WINEDLLOVERRIDES",       "WINEDLLPATH",      "DYLD_LIBRARY_PATH",   "DYLD_FALLBACK_LIBRARY_PATH",
        "DXMT_WINEMETAL_UNIXLIB", "DXMT_CONFIG_FILE", "MS_GRAPHICS_BACKEND", "WINEMSYNC",
        "DXMT_LOG_PATH",
    };
    for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); ++i)
        if (strcmp(key, reserved[i]) == 0)
            return true;
    return false;
}

bool metalsharp_launcher_runtime_ready(const char* pipeline_id, const char* m12_root, size_t m12_root_len) {
    const MetalsharpLaunchPolicy* policy = metalsharp_launch_policy(pipeline_id);
    return metalsharp_launch_policy_valid(policy) &&
           metalsharp_bottle_runtime_ready(pipeline_id, m12_root, m12_root_len);
}

static bool copy_slice(char* output, size_t size, const char* value, size_t length) {
    if (value == NULL || length == 0 || length >= size)
        return false;
    memcpy(output, value, length);
    output[length] = '\0';
    return true;
}

static bool join_path(char* output, size_t size, const char* left, const char* right) {
    const int count = snprintf(output, size, "%s/%s", left, right);
    return count >= 0 && (size_t)count < size;
}

static const char* filename_of(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash == NULL ? path : slash + 1;
}

static bool regular_file(const char* path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0;
}

static bool file_digest(const char* path, unsigned char digest[CC_SHA256_DIGEST_LENGTH], off_t* size) {
    int descriptor = open(path, O_RDONLY);
    if (descriptor < 0)
        return false;
    struct stat info;
    bool ok = fstat(descriptor, &info) == 0 && S_ISREG(info.st_mode);
    CC_SHA256_CTX context;
    CC_SHA256_Init(&context);
    unsigned char buffer[1024 * 1024];
    while (ok) {
        const ssize_t count = read(descriptor, buffer, sizeof(buffer));
        if (count == 0)
            break;
        if (count < 0) {
            if (errno == EINTR)
                continue;
            ok = false;
            break;
        }
        CC_SHA256_Update(&context, buffer, (CC_LONG)count);
    }
    close(descriptor);
    if (!ok)
        return false;
    CC_SHA256_Final(digest, &context);
    *size = info.st_size;
    return true;
}

static bool files_equal(const char* left, const char* right) {
    unsigned char left_digest[CC_SHA256_DIGEST_LENGTH], right_digest[CC_SHA256_DIGEST_LENGTH];
    off_t left_size = 0, right_size = 0;
    return file_digest(left, left_digest, &left_size) && file_digest(right, right_digest, &right_size) &&
           left_size == right_size && memcmp(left_digest, right_digest, sizeof(left_digest)) == 0;
}

static bool read_small_file(const char* path, char* output, size_t size) {
    FILE* file = fopen(path, "rb");
    if (file == NULL)
        return false;
    const size_t count = fread(output, 1, size - 1, file);
    const bool ok = !ferror(file) && count > 0;
    output[count] = '\0';
    fclose(file);
    return ok;
}

static bool receipt_has(const char* contents, char* expected, size_t size, const char* key, const char* value) {
    const int count = snprintf(expected, size, "\"%s\": \"%s\"", key, value);
    return count > 0 && (size_t)count < size && strstr(contents, expected) != NULL;
}

static const char* pipeline_id_for_code(unsigned char code) {
    switch (code) {
    case 2:
        return "m10";
    case 3:
        return "m10_32";
    case 4:
        return "m11";
    case 5:
        return "m11_32";
    case 6:
        return "m12";
    default:
        return NULL;
    }
}

static bool receipt_ready(const char* home, unsigned int appid, const MetalsharpBottlePolicy* bottle) {
    char receipt[PATH_MAX], contents[8192], expected[512];
    if (snprintf(receipt, sizeof(receipt), "%s/bottles/steam_%u/dxmt-deployment.json", home, appid) >=
            (int)sizeof(receipt) ||
        !read_small_file(receipt, contents, sizeof(contents)))
        return false;
    const char* fields[] = {
        "\"schema\": \"metalsharp.dxmt-bottle-deployment.v1\"",
        "\"status\": \"ready\"",
        "\"canonical_runtime_verified\": true",
        "\"wine_loader_mirrors_verified\": true",
        "\"prefix_route_verified\": true",
        "\"game_local_verified\": true",
    };
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
        if (strstr(contents, fields[i]) == NULL)
            return false;
    const char* architecture = bottle->architecture == METALSHARP_BOTTLE_ARCH_I386 ? "i386" : "x86_64";
    return receipt_has(contents, expected, sizeof(expected), "profile", bottle->profile_id) &&
           receipt_has(contents, expected, sizeof(expected), "architecture", architecture) &&
           receipt_has(contents, expected, sizeof(expected), "surface_id", bottle->surface_id) &&
           receipt_has(contents, expected, sizeof(expected), "manifest_sha256", bottle->manifest_sha256);
}

static bool json_safe(const char* value) {
    return value != NULL && strpbrk(value, "\"\\\n\r") == NULL;
}

static bool write_launch_receipt(const char* home, unsigned int appid, const MetalsharpLaunchPolicy* launch,
                                 const MetalsharpBottlePolicy* bottle, const char* prefix,
                                 const char* working_directory, const char* executable) {
    char directory[PATH_MAX], path[PATH_MAX], temporary[PATH_MAX];
    if (!json_safe(prefix) || !json_safe(working_directory) || !json_safe(executable) ||
        snprintf(directory, sizeof(directory), "%s/bottles/steam_%u", home, appid) >= (int)sizeof(directory) ||
        !join_path(path, sizeof(path), directory, "dxmt-launch-preflight.json") ||
        snprintf(temporary, sizeof(temporary), "%s.tmp-%d", path, getpid()) >= (int)sizeof(temporary))
        return false;
    FILE* file = fopen(temporary, "wb");
    if (file == NULL)
        return false;
    const int result =
        fprintf(file,
                "{\n  \"schema\": \"metalsharp.dxmt-launch.v1\",\n  \"appid\": %u,\n  \"pipeline\": \"%s\",\n"
                "  \"surface_id\": \"%s\",\n  \"wine_binary\": \"%s\",\n  \"graphics_backend\": \"%s\",\n"
                "  \"winemetal_unixlib\": \"%s\",\n  \"windows_dll_path\": \"%s\",\n"
                "  \"unix_library_path\": \"%s\",\n  \"dll_overrides\": \"%s\",\n"
                "  \"prefix\": \"%s\",\n  \"working_directory\": \"%s\",\n  \"executable\": \"%s\",\n"
                "  \"steam_client\": \"background\",\n  \"launch_mode\": \"direct_executable\",\n"
                "  \"status\": \"ready\"\n}\n",
                appid, launch->pipeline_id, bottle->surface_id, launch->wine_binary, launch->graphics_backend,
                launch->winemetal_unixlib, launch->windows_dll_path, launch->unix_library_path, launch->dll_overrides,
                prefix, working_directory, executable);
    bool ok = result > 0 && fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (fclose(file) != 0)
        ok = false;
    if (ok)
        ok = rename(temporary, path) == 0;
    if (!ok)
        unlink(temporary);
    return ok;
}

static bool mapped_eac_executable(unsigned int appid, const char** executable) {
    switch (appid) {
    case 1245620:
        *executable = "eldenring.exe";
        return true;
    case 1888160:
        *executable = "armoredcore6.exe";
        return true;
    default:
        return false;
    }
}

bool metalsharp_launcher_preflight(unsigned int appid, unsigned char pipeline_code, const char* prefix_value,
                                   size_t prefix_len, const char* working_directory_value, size_t working_directory_len,
                                   const char* executable_value, size_t executable_len) {
    char prefix[PATH_MAX], working_directory[PATH_MAX], executable[PATH_MAX];
    const char* pipeline_id = pipeline_id_for_code(pipeline_code);
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL || home[0] == '\0') {
        const char* user_home = getenv("HOME");
        static char default_home[PATH_MAX];
        if (user_home == NULL || !join_path(default_home, sizeof(default_home), user_home, ".metalsharp"))
            return false;
        home = default_home;
    }
    if (pipeline_id == NULL || !copy_slice(prefix, sizeof(prefix), prefix_value, prefix_len) ||
        !copy_slice(working_directory, sizeof(working_directory), working_directory_value, working_directory_len) ||
        !copy_slice(executable, sizeof(executable), executable_value, executable_len))
        return false;
    const MetalsharpLaunchPolicy* launch = metalsharp_launch_policy(pipeline_id);
    const MetalsharpBottlePolicy* bottle = metalsharp_bottle_policy(pipeline_id);
    char m12_root[PATH_MAX], lane_root[PATH_MAX], wine[PATH_MAX], executable_path[PATH_MAX], system_directory[PATH_MAX];
    if (!metalsharp_launch_policy_valid(launch) || !metalsharp_bottle_policy_valid(bottle) ||
        !join_path(m12_root, sizeof(m12_root), home, "runtime/wine/lib/dxmt_m12") ||
        !metalsharp_launcher_runtime_ready(pipeline_id, m12_root, strlen(m12_root)) ||
        !receipt_ready(home, appid, bottle) ||
        !join_path(lane_root, sizeof(lane_root), home,
                   strcmp(bottle->runtime_lane, "dxmt_m12") == 0 ? "runtime/wine/lib/dxmt_m12"
                                                                 : "runtime/wine/lib/dxmt") ||
        !join_path(wine, sizeof(wine), home, "runtime/wine/bin/metalsharp-wine") || access(wine, X_OK) != 0 ||
        !join_path(executable_path, sizeof(executable_path), working_directory, executable) ||
        !regular_file(executable_path) ||
        !join_path(system_directory, sizeof(system_directory), prefix,
                   bottle->architecture == METALSHARP_BOTTLE_ARCH_I386 ? "drive_c/windows/syswow64"
                                                                       : "drive_c/windows/system32"))
        return false;

    const char* mapped_executable = NULL;
    if (mapped_eac_executable(appid, &mapped_executable)) {
        char real_executable_path[PATH_MAX], protected_backup[PATH_MAX];
        if (strcasecmp(executable, "start_protected_game.exe") != 0 ||
            !join_path(real_executable_path, sizeof(real_executable_path), working_directory, mapped_executable) ||
            !join_path(protected_backup, sizeof(protected_backup), working_directory, "start_protected_game.old") ||
            !regular_file(protected_backup) || !files_equal(executable_path, real_executable_path))
            return false;
    }

    for (size_t i = 0; i < bottle->artifact_count; ++i) {
        if (strstr(bottle->artifacts[i], "-windows/") == NULL)
            continue;
        char source[PATH_MAX], prefix_target[PATH_MAX], game_target[PATH_MAX];
        const char* filename = filename_of(bottle->artifacts[i]);
        if (!join_path(source, sizeof(source), lane_root, bottle->artifacts[i]) ||
            !join_path(prefix_target, sizeof(prefix_target), system_directory, filename) ||
            !join_path(game_target, sizeof(game_target), working_directory, filename) ||
            !files_equal(source, prefix_target) || !files_equal(source, game_target))
            return false;
    }
    if (!bottle->includes_d3d12) {
        char prefix_d3d12[PATH_MAX], game_d3d12[PATH_MAX];
        if (!join_path(prefix_d3d12, sizeof(prefix_d3d12), system_directory, "d3d12.dll") ||
            !join_path(game_d3d12, sizeof(game_d3d12), working_directory, "d3d12.dll") ||
            access(prefix_d3d12, F_OK) == 0 || access(game_d3d12, F_OK) == 0)
            return false;
    }
    return write_launch_receipt(home, appid, launch, bottle, prefix, working_directory, executable);
}

static bool find_case_insensitive(const char* directory, const char* name, char* output, size_t size) {
    DIR* entries = opendir(directory);
    if (entries == NULL)
        return false;
    bool found = false;
    struct dirent* entry;
    while ((entry = readdir(entries)) != NULL) {
        if (strcasecmp(entry->d_name, name) != 0)
            continue;
        found = join_path(output, size, directory, entry->d_name);
        break;
    }
    closedir(entries);
    return found;
}

static bool copy_file(const char* source, const char* destination) {
    int input = open(source, O_RDONLY);
    if (input < 0)
        return false;
    struct stat info;
    bool ok = fstat(input, &info) == 0 && S_ISREG(info.st_mode);
    int output = ok ? open(destination, O_WRONLY | O_CREAT | O_EXCL, info.st_mode & 0777) : -1;
    if (output < 0)
        ok = false;
    unsigned char buffer[1024 * 1024];
    while (ok) {
        const ssize_t count = read(input, buffer, sizeof(buffer));
        if (count == 0)
            break;
        if (count < 0) {
            if (errno == EINTR)
                continue;
            ok = false;
            break;
        }
        ssize_t written = 0;
        while (written < count) {
            const ssize_t result = write(output, buffer + written, (size_t)(count - written));
            if (result < 0 && errno == EINTR)
                continue;
            if (result <= 0) {
                ok = false;
                break;
            }
            written += result;
        }
    }
    if (ok)
        ok = fsync(output) == 0;
    if (output >= 0 && close(output) != 0)
        ok = false;
    close(input);
    if (!ok)
        unlink(destination);
    return ok;
}

bool metalsharp_launcher_prepare_eac(unsigned int appid, const char* game_directory_value, size_t game_directory_len) {
    const char* real_name = NULL;
    char game_directory[PATH_MAX], protected_path[PATH_MAX], real_path[PATH_MAX], old_path[PATH_MAX], parent[PATH_MAX];
    if (!mapped_eac_executable(appid, &real_name) ||
        !copy_slice(game_directory, sizeof(game_directory), game_directory_value, game_directory_len) ||
        !find_case_insensitive(game_directory, "start_protected_game.exe", protected_path, sizeof(protected_path)) ||
        !find_case_insensitive(game_directory, real_name, real_path, sizeof(real_path)))
        return false;
    const char* slash = strrchr(protected_path, '/');
    const size_t parent_len = slash == NULL ? 0 : (size_t)(slash - protected_path);
    if (parent_len == 0 || parent_len >= sizeof(parent))
        return false;
    memcpy(parent, protected_path, parent_len);
    parent[parent_len] = '\0';
    if (!join_path(old_path, sizeof(old_path), parent, "start_protected_game.old"))
        return false;
    if (regular_file(old_path))
        return regular_file(protected_path);
    if (rename(protected_path, old_path) != 0)
        return false;
    if (!copy_file(real_path, protected_path)) {
        rename(old_path, protected_path);
        return false;
    }
    return true;
}

bool metalsharp_launcher_validate_exe_args(unsigned int appid, unsigned char pipeline_code,
                                            const char* const* args, size_t arg_count) {
    (void)appid;
    (void)pipeline_code;
    if (arg_count == 0)
        return true; /* empty list is valid; NULL data pointer is fine */
    if (args == NULL)
        return false;
    for (size_t i = 0; i < arg_count; ++i) {
        if (args[i] == NULL || args[i][0] == '\0')
            return false;
        for (const char* p = args[i]; *p; ++p) {
            unsigned char c = (unsigned char)*p;
            if (c < 0x20 || c == 0x7f)
                return false;
            if (c == '"' || c == '\'' || c == ';' || c == '&' || c == '|' || c == '$' || c == '`')
                return false;
        }
        if (strstr(args[i], "..") != NULL)
            return false;
    }
    return true;
}
