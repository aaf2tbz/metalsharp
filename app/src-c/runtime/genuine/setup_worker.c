#include "setup_worker.h"

#include "server.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <spawn.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

extern char** environ;

#define SETUP_TOTAL_STEPS 15u

static _Atomic bool g_setup_installing = false;

static const char* setup_home(void) {
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL || home[0] == '\0')
        home = getenv("HOME");
    return home != NULL ? home : "";
}

static bool join_path(char* output, size_t output_size, const char* left, const char* right) {
    int written = snprintf(output, output_size, "%s%s%s", left,
                           left[0] != '\0' && left[strlen(left) - 1u] == '/' ? "" : "/", right);
    return written >= 0 && (size_t)written < output_size;
}

static bool regular_nonempty(const char* path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0;
}

static bool directory_exists(const char* path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static bool mkdir_p(const char* path) {
    char copy[PATH_MAX];
    int written = snprintf(copy, sizeof(copy), "%s", path);
    if (written < 0 || (size_t)written >= sizeof(copy))
        return false;
    for (char* cursor = copy + 1; *cursor != '\0'; cursor++) {
        if (*cursor != '/')
            continue;
        *cursor = '\0';
        if (mkdir(copy, 0755) != 0 && errno != EEXIST)
            return false;
        *cursor = '/';
    }
    return mkdir(copy, 0755) == 0 || errno == EEXIST;
}

static bool remove_tree(const char* path) {
    struct stat info;
    if (lstat(path, &info) != 0)
        return errno == ENOENT;
    if (!S_ISDIR(info.st_mode) || S_ISLNK(info.st_mode))
        return unlink(path) == 0;
    DIR* stream = opendir(path);
    if (stream == NULL)
        return false;
    bool ok = true;
    struct dirent* entry;
    while ((entry = readdir(stream)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char child[PATH_MAX];
        if (!join_path(child, sizeof(child), path, entry->d_name) || !remove_tree(child)) {
            ok = false;
            break;
        }
    }
    closedir(stream);
    return ok && rmdir(path) == 0;
}

static bool files_equal(const char* left, const char* right) {
    struct stat left_info, right_info;
    if (stat(left, &left_info) != 0 || stat(right, &right_info) != 0 || left_info.st_size != right_info.st_size)
        return false;
    FILE* left_file = fopen(left, "rb");
    FILE* right_file = fopen(right, "rb");
    if (left_file == NULL || right_file == NULL) {
        if (left_file != NULL)
            fclose(left_file);
        if (right_file != NULL)
            fclose(right_file);
        return false;
    }
    bool equal = true;
    unsigned char left_buffer[16384], right_buffer[16384];
    for (;;) {
        size_t left_count = fread(left_buffer, 1u, sizeof(left_buffer), left_file);
        size_t right_count = fread(right_buffer, 1u, sizeof(right_buffer), right_file);
        if (left_count != right_count || memcmp(left_buffer, right_buffer, left_count) != 0) {
            equal = false;
            break;
        }
        if (left_count == 0u) {
            if (ferror(left_file) || ferror(right_file))
                equal = false;
            break;
        }
    }
    fclose(left_file);
    fclose(right_file);
    return equal;
}

static bool copy_file(const char* source, const char* destination, mode_t mode) {
    char parent[PATH_MAX];
    snprintf(parent, sizeof(parent), "%s", destination);
    char* slash = strrchr(parent, '/');
    if (slash != NULL) {
        *slash = '\0';
        if (!mkdir_p(parent))
            return false;
    }
    FILE* input = fopen(source, "rb");
    if (input == NULL)
        return false;
    FILE* output = fopen(destination, "wb");
    if (output == NULL) {
        fclose(input);
        return false;
    }
    char buffer[65536];
    bool ok = true;
    size_t count;
    while ((count = fread(buffer, 1u, sizeof(buffer), input)) > 0u) {
        if (fwrite(buffer, 1u, count, output) != count) {
            ok = false;
            break;
        }
    }
    if (ferror(input))
        ok = false;
    if (fclose(input) != 0)
        ok = false;
    if (fclose(output) != 0)
        ok = false;
    if (ok)
        (void)chmod(destination, mode & 0777u);
    return ok;
}

static bool copy_tree(const char* source, const char* destination) {
    struct stat info;
    if (lstat(source, &info) != 0)
        return false;
    if (S_ISLNK(info.st_mode)) {
        char target[PATH_MAX];
        ssize_t length = readlink(source, target, sizeof(target) - 1u);
        if (length < 0)
            return false;
        target[length] = '\0';
        char parent[PATH_MAX];
        snprintf(parent, sizeof(parent), "%s", destination);
        char* slash = strrchr(parent, '/');
        if (slash != NULL) {
            *slash = '\0';
            if (!mkdir_p(parent))
                return false;
        }
        (void)unlink(destination);
        return symlink(target, destination) == 0;
    }
    if (S_ISREG(info.st_mode))
        return copy_file(source, destination, info.st_mode);
    if (!S_ISDIR(info.st_mode))
        return true;
    if (!mkdir_p(destination))
        return false;
    DIR* stream = opendir(source);
    if (stream == NULL)
        return false;
    bool ok = true;
    struct dirent* entry;
    while ((entry = readdir(stream)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char source_child[PATH_MAX], destination_child[PATH_MAX];
        if (!join_path(source_child, sizeof(source_child), source, entry->d_name) ||
            !join_path(destination_child, sizeof(destination_child), destination, entry->d_name) ||
            !copy_tree(source_child, destination_child)) {
            ok = false;
            break;
        }
    }
    closedir(stream);
    return ok;
}

static void json_escape(const char* input, char* output, size_t output_size) {
    size_t used = 0u;
    for (const unsigned char* cursor = (const unsigned char*)input; *cursor != '\0' && used + 7u < output_size;
         cursor++) {
        const char* escaped = NULL;
        if (*cursor == '"')
            escaped = "\\\"";
        else if (*cursor == '\\')
            escaped = "\\\\";
        else if (*cursor == '\n')
            escaped = "\\n";
        else if (*cursor == '\r')
            escaped = "\\r";
        else if (*cursor == '\t')
            escaped = "\\t";
        if (escaped != NULL) {
            size_t length = strlen(escaped);
            memcpy(output + used, escaped, length);
            used += length;
        } else if (*cursor >= 0x20u) {
            output[used++] = (char)*cursor;
        }
    }
    output[used] = '\0';
}

static void write_progress(unsigned step, unsigned total, const char* current, const char* status, const char* log,
                           const char* error) {
    char path[PATH_MAX], temporary[PATH_MAX];
    if (!join_path(path, sizeof(path), setup_home(), "install_progress.json"))
        return;
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    char escaped_current[512], escaped_log[2048], escaped_error[2048];
    json_escape(current != NULL ? current : "", escaped_current, sizeof(escaped_current));
    json_escape(log != NULL ? log : "", escaped_log, sizeof(escaped_log));
    json_escape(error != NULL ? error : "", escaped_error, sizeof(escaped_error));
    FILE* output = fopen(temporary, "wb");
    if (output == NULL)
        return;
    fprintf(output,
            "{\"step\":%u,\"total\":%u,\"current\":\"%s\",\"status\":\"%s\",\"log\":\"%s\","
            "\"error\":",
            step, total, escaped_current, status, escaped_log);
    if (error != NULL)
        fprintf(output, "\"%s\"}", escaped_error);
    else
        fprintf(output, "null}");
    if (fclose(output) == 0)
        (void)rename(temporary, path);
    else
        (void)unlink(temporary);
}

static int spawn_wait(const char* executable, char* const argv[]) {
    pid_t pid = 0;
    int spawned = posix_spawn(&pid, executable, NULL, NULL, argv, environ);
    if (spawned != 0)
        return -spawned;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}

static bool find_command(const char* name, char* output, size_t output_size) {
    static const char* prefixes[] = {"/opt/homebrew/bin", "/usr/local/bin", "/usr/bin", "/bin", "/usr/sbin"};
    for (size_t i = 0u; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        int written = snprintf(output, output_size, "%s/%s", prefixes[i], name);
        if (written > 0 && (size_t)written < output_size && access(output, X_OK) == 0)
            return true;
    }
    const char* path = getenv("PATH");
    if (path == NULL)
        return false;
    char* copy = strdup(path);
    if (copy == NULL)
        return false;
    bool found = false;
    char* save = NULL;
    for (char* directory = strtok_r(copy, ":", &save); directory != NULL; directory = strtok_r(NULL, ":", &save)) {
        int written = snprintf(output, output_size, "%s/%s", directory, name);
        if (written > 0 && (size_t)written < output_size && access(output, X_OK) == 0) {
            found = true;
            break;
        }
    }
    free(copy);
    return found;
}

static bool resources_directory(char* output, size_t output_size) {
#ifdef __APPLE__
    uint32_t executable_size = PATH_MAX;
    char executable[PATH_MAX];
    if (_NSGetExecutablePath(executable, &executable_size) == 0) {
        char* slash = strrchr(executable, '/');
        if (slash != NULL) {
            *slash = '\0';
            int written = snprintf(output, output_size, "%s/..", executable);
            return written > 0 && (size_t)written < output_size;
        }
    }
#endif
    output[0] = '\0';
    return false;
}

static bool find_bundle(const char* filename, char* output, size_t output_size) {
    char resources[PATH_MAX] = "", resource_bundle[PATH_MAX] = "", cached[PATH_MAX] = "";
    if (resources_directory(resources, sizeof(resources)))
        snprintf(resource_bundle, sizeof(resource_bundle), "%s/bundles/%s", resources, filename);
    snprintf(cached, sizeof(cached), "%s/cache/bundles/%s", setup_home(), filename);
    const char* candidates[] = {resource_bundle, "app/bundles", "../bundles", cached};
    for (size_t i = 0u; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        char candidate[PATH_MAX];
        if (i == 1u || i == 2u)
            snprintf(candidate, sizeof(candidate), "%s/%s", candidates[i], filename);
        else
            snprintf(candidate, sizeof(candidate), "%s", candidates[i]);
        if (candidate[0] != '\0' && regular_nonempty(candidate)) {
            snprintf(output, output_size, "%s", candidate);
            return true;
        }
    }
    return false;
}

static bool download_bundle(const char* filename, char* output, size_t output_size, char* error, size_t error_size) {
    char cache_directory[PATH_MAX], destination[PATH_MAX], temporary[PATH_MAX], url[1024];
    snprintf(cache_directory, sizeof(cache_directory), "%s/cache/bundles", setup_home());
    if (!mkdir_p(cache_directory)) {
        snprintf(error, error_size, "create bundle cache: %s", strerror(errno));
        return false;
    }
    snprintf(destination, sizeof(destination), "%s/%s", cache_directory, filename);
    snprintf(temporary, sizeof(temporary), "%s.download", destination);
    snprintf(url, sizeof(url), "https://github.com/aaf2tbz/metalsharp/releases/download/bundles/%s", filename);
    (void)unlink(temporary);
    char* argv[] = {"/usr/bin/curl", "--fail", "--location",        "--silent", "--show-error",
                    "--retry",       "2",      "--connect-timeout", "30",       "--max-time",
                    "600",           "-o",     temporary,           url,        NULL};
    int status = spawn_wait("/usr/bin/curl", argv);
    if (status != 0 || !regular_nonempty(temporary) || rename(temporary, destination) != 0) {
        (void)unlink(temporary);
        snprintf(error, error_size, "Missing bundle: %s", filename);
        return false;
    }
    snprintf(output, output_size, "%s", destination);
    return true;
}

static bool extract_archive(const char* archive, const char* destination, char* error, size_t error_size) {
    char zstd[PATH_MAX];
    if (!find_command("zstd", zstd, sizeof(zstd)) && !find_command("unzstd", zstd, sizeof(zstd))) {
        snprintf(error, error_size, "zstd command not found");
        return false;
    }
    if (!mkdir_p(destination)) {
        snprintf(error, error_size, "create extract directory: %s", strerror(errno));
        return false;
    }
    int descriptors[2];
    if (pipe(descriptors) != 0) {
        snprintf(error, error_size, "extract pipe: %s", strerror(errno));
        return false;
    }
    posix_spawn_file_actions_t zstd_actions, tar_actions;
    posix_spawn_file_actions_init(&zstd_actions);
    posix_spawn_file_actions_adddup2(&zstd_actions, descriptors[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&zstd_actions, descriptors[0]);
    posix_spawn_file_actions_addclose(&zstd_actions, descriptors[1]);
    posix_spawn_file_actions_init(&tar_actions);
    posix_spawn_file_actions_adddup2(&tar_actions, descriptors[0], STDIN_FILENO);
    posix_spawn_file_actions_addclose(&tar_actions, descriptors[0]);
    posix_spawn_file_actions_addclose(&tar_actions, descriptors[1]);
    pid_t zstd_pid = 0, tar_pid = 0;
    char* zstd_argv[] = {zstd, "-dc", (char*)archive, NULL};
    char* tar_argv[] = {"/usr/bin/tar", "-xf", "-", "-C", (char*)destination, NULL};
    int zstd_spawn = posix_spawn(&zstd_pid, zstd, &zstd_actions, NULL, zstd_argv, environ);
    int tar_spawn = posix_spawn(&tar_pid, "/usr/bin/tar", &tar_actions, NULL, tar_argv, environ);
    posix_spawn_file_actions_destroy(&zstd_actions);
    posix_spawn_file_actions_destroy(&tar_actions);
    close(descriptors[0]);
    close(descriptors[1]);
    int zstd_status = 0, tar_status = 0;
    if (zstd_spawn == 0)
        while (waitpid(zstd_pid, &zstd_status, 0) < 0 && errno == EINTR) {
        }
    if (tar_spawn == 0)
        while (waitpid(tar_pid, &tar_status, 0) < 0 && errno == EINTR) {
        }
    if (zstd_spawn != 0 || tar_spawn != 0 || !WIFEXITED(zstd_status) || WEXITSTATUS(zstd_status) != 0 ||
        !WIFEXITED(tar_status) || WEXITSTATUS(tar_status) != 0) {
        snprintf(error, error_size, "extract archive failed: %s", archive);
        return false;
    }
    return true;
}

static bool temporary_directory(const char* label, char* output, size_t output_size) {
    int written = snprintf(output, output_size, "/tmp/metalsharp-%s-%ld", label, (long)getpid());
    if (written < 0 || (size_t)written >= output_size)
        return false;
    (void)remove_tree(output);
    return mkdir_p(output);
}

static bool step_system_tools(bool* changed, char* error, size_t error_size) {
    (void)changed;
    char clang[PATH_MAX];
    if (find_command("clang", clang, sizeof(clang)))
        return true;
    snprintf(error, error_size, "Xcode CLI tools installation failed — install manually with: xcode-select --install");
    return false;
}

static bool step_rosetta(bool* changed, char* error, size_t error_size) {
    if (access("/Library/Apple/System/Library/LaunchDaemons/com.apple.oahd.plist", F_OK) == 0) {
        *changed = false;
        return true;
    }
    char pgrep[PATH_MAX];
    if (find_command("pgrep", pgrep, sizeof(pgrep))) {
        char* pgrep_argv[] = {pgrep, "-q", "oahd", NULL};
        if (spawn_wait(pgrep, pgrep_argv) == 0) {
            *changed = false;
            return true;
        }
    }
    char* argv[] = {"/usr/sbin/softwareupdate", "--install-rosetta", "--agree-to-license", NULL};
    if (spawn_wait("/usr/sbin/softwareupdate", argv) == 0) {
        *changed = true;
        return true;
    }
    snprintf(error, error_size, "rosetta install failed");
    return false;
}

static bool step_zstd(bool* changed, char* error, size_t error_size) {
    char zstd[PATH_MAX];
    if (find_command("unzstd", zstd, sizeof(zstd)) || find_command("zstd", zstd, sizeof(zstd))) {
        *changed = false;
        return true;
    }
    char brew[PATH_MAX];
    if (!find_command("brew", brew, sizeof(brew))) {
        snprintf(error, error_size, "Homebrew not found — install it first");
        return false;
    }
    char* argv[] = {brew, "install", "zstd", NULL};
    if (spawn_wait(brew, argv) != 0) {
        snprintf(error, error_size, "brew install failed");
        return false;
    }
    *changed = true;
    return true;
}

static const char* const bundle_files[] = {
    "metalsharp-runtime.tar.zst",       "metalsharp-graphics-dll.tar.zst",
    "metalsharp-assets.tar.zst",        "fnalibs.tar.zst",
    "metalsharp-scripts-tools.tar.zst", "metalsharp-steam.tar.zst",
};

static bool step_bundles(bool* changed, char* error, size_t error_size) {
    *changed = false;
    for (size_t i = 0u; i < sizeof(bundle_files) / sizeof(bundle_files[0]); i++) {
        char archive[PATH_MAX];
        if (find_bundle(bundle_files[i], archive, sizeof(archive)))
            continue;
        write_progress(4u, SETUP_TOTAL_STEPS, "Runtime Bundle Downloads", "downloading", bundle_files[i], NULL);
        if (!download_bundle(bundle_files[i], archive, sizeof(archive), error, error_size))
            return false;
        *changed = true;
    }
    return true;
}

static bool extract_and_copy(const char* filename, const char* source_relative, const char* destination,
                             const char* temporary_label, bool replace, char* error, size_t error_size) {
    char archive[PATH_MAX], temporary[PATH_MAX], source[PATH_MAX];
    if (!find_bundle(filename, archive, sizeof(archive))) {
        snprintf(error, error_size, "Missing bundle: %s", filename);
        return false;
    }
    if (!temporary_directory(temporary_label, temporary, sizeof(temporary)) ||
        !extract_archive(archive, temporary, error, error_size) ||
        !join_path(source, sizeof(source), temporary, source_relative) || !directory_exists(source)) {
        (void)remove_tree(temporary);
        if (error[0] == '\0')
            snprintf(error, error_size, "bundle payload missing: %s/%s", filename, source_relative);
        return false;
    }
    if (replace)
        (void)remove_tree(destination);
    bool copied = copy_tree(source, destination);
    (void)remove_tree(temporary);
    if (!copied)
        snprintf(error, error_size, "copy bundle payload failed: %s", source_relative);
    return copied;
}

static bool bundle_sha256(const char* archive, char hash[65]) {
    int output_pipe[2];
    if (pipe(output_pipe) != 0)
        return false;
    pid_t pid = fork();
    if (pid < 0) {
        close(output_pipe[0]);
        close(output_pipe[1]);
        return false;
    }
    if (pid == 0) {
        close(output_pipe[0]);
        (void)dup2(output_pipe[1], STDOUT_FILENO);
        close(output_pipe[1]);
        execl("/usr/bin/shasum", "shasum", "-a", "256", archive, (char*)NULL);
        _exit(127);
    }
    close(output_pipe[1]);
    char output[256];
    ssize_t count = read(output_pipe[0], output, sizeof(output) - 1u);
    close(output_pipe[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    if (count < 64 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return false;
    output[count] = '\0';
    for (size_t i = 0u; i < 64u; i++)
        if (!((output[i] >= '0' && output[i] <= '9') || (output[i] >= 'a' && output[i] <= 'f') ||
              (output[i] >= 'A' && output[i] <= 'F')))
            return false;
    memcpy(hash, output, 64u);
    hash[64] = '\0';
    return true;
}

static bool bundle_current(const char* bundle_name, const char* archive) {
    char expected[65], path[PATH_MAX], existing[80];
    if (!bundle_sha256(archive, expected))
        return false;
    snprintf(path, sizeof(path), "%s/runtime/bundle-state/%s.sha256", setup_home(), bundle_name);
    FILE* input = fopen(path, "rb");
    if (input == NULL)
        return false;
    size_t count = fread(existing, 1u, sizeof(existing) - 1u, input);
    fclose(input);
    existing[count] = '\0';
    while (count > 0u && (existing[count - 1u] == '\n' || existing[count - 1u] == '\r' || existing[count - 1u] == ' '))
        existing[--count] = '\0';
    return strcmp(existing, expected) == 0;
}

static void mark_bundle_installed(const char* bundle_name, const char* archive) {
    char hash[65], directory[PATH_MAX], path[PATH_MAX];
    if (!bundle_sha256(archive, hash))
        return;
    snprintf(directory, sizeof(directory), "%s/runtime/bundle-state", setup_home());
    if (!mkdir_p(directory))
        return;
    snprintf(path, sizeof(path), "%s/%s.sha256", directory, bundle_name);
    FILE* output = fopen(path, "wb");
    if (output != NULL) {
        (void)fwrite(hash, 1u, 64u, output);
        fclose(output);
    }
}

static bool step_runtime(bool* changed, char* error, size_t error_size) {
    char wine[PATH_MAX], destination[PATH_MAX];
    snprintf(wine, sizeof(wine), "%s/runtime/wine/bin/metalsharp-wine", setup_home());
    snprintf(destination, sizeof(destination), "%s/runtime", setup_home());
    char archive[PATH_MAX];
    if (regular_nonempty(wine)) {
        *changed = false;
        if (find_bundle("metalsharp-runtime.tar.zst", archive, sizeof(archive)))
            mark_bundle_installed("metalsharp-runtime", archive);
        return true;
    }
    *changed = true;
    bool installed = extract_and_copy("metalsharp-runtime.tar.zst", "runtime", destination, "runtime-extract", false,
                                      error, error_size);
    if (installed && find_bundle("metalsharp-runtime.tar.zst", archive, sizeof(archive)))
        mark_bundle_installed("metalsharp-runtime", archive);
    return installed;
}

static bool step_host(bool* changed, char* error, size_t error_size) {
    char manifest[PATH_MAX], abi[PATH_MAX], library[PATH_MAX];
    snprintf(manifest, sizeof(manifest), "%s/runtime/host/manifest.json", setup_home());
    snprintf(abi, sizeof(abi), "%s/runtime/host/HostRuntimeABI.h", setup_home());
    snprintf(library, sizeof(library), "%s/runtime/host/libmetalsharp_host_runtime.dylib", setup_home());
    *changed = false;
    if (regular_nonempty(manifest) && regular_nonempty(abi) && regular_nonempty(library))
        return true;
    snprintf(error, error_size, "MetalSharp runtime bundle installed but host runtime ABI assets are missing");
    return false;
}

static void fix_moltenvk_icd_paths(void) {
    char library[PATH_MAX], directory[PATH_MAX];
    snprintf(library, sizeof(library), "%s/runtime/wine/lib/wine/x86_64-unix/libMoltenVK.dylib", setup_home());
    snprintf(directory, sizeof(directory), "%s/runtime/wine/etc/vulkan/icd.d", setup_home());
    if (!regular_nonempty(library) || !directory_exists(directory))
        return;
    DIR* entries = opendir(directory);
    if (entries == NULL)
        return;
    struct dirent* entry;
    while ((entry = readdir(entries)) != NULL) {
        if (strncmp(entry->d_name, "MoltenVK", 8u) != 0 || strstr(entry->d_name, ".json") == NULL)
            continue;
        char path[PATH_MAX];
        if (!join_path(path, sizeof(path), directory, entry->d_name))
            continue;
        FILE* output = fopen(path, "wb");
        if (output == NULL)
            continue;
        fprintf(output,
                "{\n  \"ICD\": {\n    \"api_version\": \"1.4.0\",\n"
                "    \"is_portability_driver\": true,\n    \"library_path\": \"%s\"\n"
                "  },\n  \"file_format_version\": \"1.0.0\"\n}",
                library);
        fclose(output);
    }
    closedir(entries);
}

static bool step_support(bool* changed, char* error, size_t error_size) {
    char archive[PATH_MAX], temporary[PATH_MAX] = "", assets[PATH_MAX], source[PATH_MAX], destination[PATH_MAX];
    if (!find_bundle("metalsharp-assets.tar.zst", archive, sizeof(archive))) {
        snprintf(error, error_size, "Support assets not found — metalsharp-assets.tar.zst is missing");
        return false;
    }
    char fna_ready[PATH_MAX];
    snprintf(fna_ready, sizeof(fna_ready), "%s/runtime/fnalibs/libFNA3D.0.dylib", setup_home());
    if (bundle_current("metalsharp-assets", archive) && regular_nonempty(fna_ready)) {
        *changed = false;
        return true;
    }
    if (!temporary_directory("assets-extract", temporary, sizeof(temporary)) ||
        !extract_archive(archive, temporary, error, error_size) ||
        !join_path(assets, sizeof(assets), temporary, "assets")) {
        (void)remove_tree(temporary);
        if (error[0] == '\0')
            snprintf(error, error_size, "Support assets not found — metalsharp-assets.tar.zst is missing");
        return false;
    }
    static const char* selected[] = {"mono-x86", "mono-arm64", "dxvk-1.10.3",  "goldberg",
                                     "shims",    "fnalibs",    "fna-kickstart"};
    for (size_t i = 0u; i < sizeof(selected) / sizeof(selected[0]); i++) {
        if (!join_path(source, sizeof(source), assets, selected[i]) || !directory_exists(source))
            continue;
        snprintf(destination, sizeof(destination), "%s/runtime/%s", setup_home(), selected[i]);
        (void)remove_tree(destination);
        if (!copy_tree(source, destination)) {
            (void)remove_tree(temporary);
            snprintf(error, error_size, "copy support asset failed: %s", selected[i]);
            return false;
        }
    }
    char stale[PATH_MAX];
    snprintf(stale, sizeof(stale), "%s/runtime/gptk", setup_home());
    (void)remove_tree(stale);
    snprintf(stale, sizeof(stale), "%s/runtime/shader-cache", setup_home());
    (void)remove_tree(stale);
    snprintf(stale, sizeof(stale), "%s/runtime/wine/lib/gptk", setup_home());
    (void)remove_tree(stale);
    snprintf(stale, sizeof(stale), "%s/runtime/wine/lib/external/D3DMetal.framework", setup_home());
    (void)remove_tree(stale);
    snprintf(stale, sizeof(stale), "%s/runtime/wine/lib/external/libd3dshared.dylib", setup_home());
    (void)remove_tree(stale);
    if (join_path(source, sizeof(source), assets, "wine/etc") && directory_exists(source)) {
        snprintf(destination, sizeof(destination), "%s/runtime/wine/etc", setup_home());
        if (!copy_tree(source, destination)) {
            (void)remove_tree(temporary);
            snprintf(error, error_size, "copy Wine support configuration failed");
            return false;
        }
    }
    if (join_path(source, sizeof(source), assets, "shader-cache") && directory_exists(source)) {
        snprintf(destination, sizeof(destination), "%s/shader-cache", setup_home());
        if (!copy_tree(source, destination)) {
            (void)remove_tree(temporary);
            snprintf(error, error_size, "copy shader cache support failed");
            return false;
        }
    }
    fix_moltenvk_icd_paths();
    (void)remove_tree(temporary);
    mark_bundle_installed("metalsharp-assets", archive);
    *changed = true;
    return true;
}

static bool step_scripts(bool* changed, char* error, size_t error_size) {
    char destination[PATH_MAX], archive[PATH_MAX];
    snprintf(destination, sizeof(destination), "%s/scripts/tools", setup_home());
    if (find_bundle("metalsharp-scripts-tools.tar.zst", archive, sizeof(archive)) &&
        bundle_current("metalsharp-scripts-tools", archive)) {
        *changed = false;
        return true;
    }
    *changed = true;
    bool installed = extract_and_copy("metalsharp-scripts-tools.tar.zst", "scripts/tools", destination,
                                      "scripts-extract", true, error, error_size);
    if (installed && find_bundle("metalsharp-scripts-tools.tar.zst", archive, sizeof(archive)))
        mark_bundle_installed("metalsharp-scripts-tools", archive);
    return installed;
}

static bool write_graphics_manifest(const char* directory, bool m12) {
    char path[PATH_MAX];
    if (!mkdir_p(directory) || !join_path(path, sizeof(path), directory, "metalsharp-dxmt-runtime.json"))
        return false;
    FILE* output = fopen(path, "wb");
    if (output == NULL)
        return false;
    if (m12) {
        fprintf(output, "{\n  \"schema\": \"metalsharp.dxmt-runtime.v1\",\n"
                        "  \"version\": \"0.56.0-m12-isolated-surface-v1\",\n"
                        "  \"surface_id\": \"metalsharp-dxmt-m12-pr230-v2\",\n"
                        "  \"lane\": \"dxmt_m12\",\n"
                        "  \"source\": \"bundled:metalsharp-graphics-dll.tar.zst\",\n"
                        "  \"source_bundle\": \"Dependency Bundles\",\n"
                        "  \"artifact_count\": 12\n}\n");
    } else {
        fprintf(output,
                "{\n  \"installedAtUnix\": %lld,\n  \"requiredFiles\": {\n"
                "    \"dxmt/i386-unix\": [\"winemetal.so\"],\n"
                "    \"dxmt/x86_64-unix\": [\"winemetal.so\"],\n"
                "    \"dxmt_m12/x86_64-unix\": [\"winemetal.so\", \"libc++.1.dylib\", "
                "\"libc++abi.1.dylib\", \"libunwind.1.dylib\"],\n"
                "    \"dxmt_m12/x86_64-windows\": [\"d3d10core.dll\", \"d3d11.dll\", \"d3d12.dll\", "
                "\"dxgi.dll\", \"dxgi_dxmt.dll\", \"winemetal.dll\", \"nvapi64.dll\", \"nvngx.dll\"],\n"
                "    \"i386-windows\": [\"d3d11.dll\", \"dxgi.dll\", \"dxgi_dxmt.dll\", "
                "\"d3d10core.dll\", \"winemetal.dll\"],\n"
                "    \"x86_64-windows\": [\"d3d10core.dll\", \"d3d11.dll\", \"d3d12.dll\", "
                "\"dxgi.dll\", \"dxgi_dxmt.dll\", \"winemetal.dll\", \"nvapi64.dll\", \"nvngx.dll\"]\n"
                "  },\n  \"schema\": \"metalsharp.dxmt-runtime.v1\",\n"
                "  \"source\": \"bundled:metalsharp-graphics-dll.tar.zst\",\n"
                "  \"version\": \"0.56.0-m12-isolated-surface-v1\"\n}\n",
                (long long)time(NULL));
    }
    return fclose(output) == 0;
}

static bool step_graphics(bool* changed, char* error, size_t error_size) {
    char archive[PATH_MAX], temporary[PATH_MAX] = "", graphics[PATH_MAX], source[PATH_MAX], destination[PATH_MAX];
    if (find_bundle("metalsharp-graphics-dll.tar.zst", archive, sizeof(archive)) &&
        bundle_current("metalsharp-graphics-dll", archive)) {
        char dxmt[PATH_MAX], m12[PATH_MAX];
        snprintf(dxmt, sizeof(dxmt), "%s/runtime/wine/lib/dxmt/metalsharp-dxmt-runtime.json", setup_home());
        snprintf(m12, sizeof(m12), "%s/runtime/wine/lib/dxmt_m12/x86_64-windows/d3d12.dll", setup_home());
        if (regular_nonempty(dxmt) && regular_nonempty(m12)) {
            *changed = false;
            return true;
        }
    }
    if (!find_bundle("metalsharp-graphics-dll.tar.zst", archive, sizeof(archive)) ||
        !temporary_directory("graphics-extract", temporary, sizeof(temporary)) ||
        !extract_archive(archive, temporary, error, error_size) ||
        !join_path(graphics, sizeof(graphics), temporary, "Graphics/dll")) {
        (void)remove_tree(temporary);
        return false;
    }
    const char* source_names[] = {"dxmt", "dxmt_m12"};
    const char* destination_names[] = {"dxmt", "dxmt_m12"};
    for (size_t i = 0u; i < 2u; i++) {
        join_path(source, sizeof(source), graphics, source_names[i]);
        if (!directory_exists(source) && i == 1u)
            join_path(source, sizeof(source), graphics, "dxmt-m12");
        snprintf(destination, sizeof(destination), "%s/runtime/wine/lib/%s", setup_home(), destination_names[i]);
        (void)remove_tree(destination);
        if (!copy_tree(source, destination) || !write_graphics_manifest(destination, i == 1u)) {
            (void)remove_tree(temporary);
            snprintf(error, error_size, "DXMT graphics runtime copy failed: %s", source_names[i]);
            return false;
        }
    }
    char empty_directory[PATH_MAX];
    snprintf(empty_directory, sizeof(empty_directory), "%s/runtime/wine/lib/dxmt_m12/i386-unix", setup_home());
    (void)mkdir_p(empty_directory);
    snprintf(empty_directory, sizeof(empty_directory), "%s/runtime/wine/lib/dxmt_m12/i386-windows", setup_home());
    (void)mkdir_p(empty_directory);
    (void)remove_tree(temporary);
    mark_bundle_installed("metalsharp-graphics-dll", archive);
    *changed = true;
    return true;
}

static bool step_goldberg(bool* changed, char* error, size_t error_size) {
    char x86[PATH_MAX], x64[PATH_MAX];
    snprintf(x86, sizeof(x86), "%s/runtime/goldberg/x86/steam_api.dll", setup_home());
    snprintf(x64, sizeof(x64), "%s/runtime/goldberg/x64/steam_api64.dll", setup_home());
    *changed = false;
    if (regular_nonempty(x86) && regular_nonempty(x64))
        return true;
    snprintf(error, error_size, "Goldberg Steam emulator not found — goldberg.tar.zst missing from bundles");
    return false;
}

static bool step_steam_bridge(bool* changed, char* error, size_t error_size) {
    char source[PATH_MAX], destination[PATH_MAX];
    snprintf(source, sizeof(source), "%s/runtime/shims/libsteam_api.dylib", setup_home());
    snprintf(destination, sizeof(destination), "%s/runtime/steam-bridge/libsteam_api.dylib", setup_home());
    if (regular_nonempty(destination)) {
        *changed = false;
        return true;
    }
    if (!regular_nonempty(source)) {
        *changed = false;
        return true;
    }
    struct stat info;
    stat(source, &info);
    if (!copy_file(source, destination, info.st_mode)) {
        snprintf(error, error_size, "copy steam bridge shim: %s", strerror(errno));
        return false;
    }
    *changed = true;
    return true;
}

static bool copy_resource_config(const char* name, bool* changed, char* error, size_t error_size) {
    char resources[PATH_MAX] = "", source[PATH_MAX] = "", destination[PATH_MAX];
    if (resources_directory(resources, sizeof(resources)))
        snprintf(source, sizeof(source), "%s/configs/%s", resources, name);
    if (!regular_nonempty(source)) {
        const char* candidates[] = {"configs", "../configs", "../../configs"};
        for (size_t i = 0u; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
            snprintf(source, sizeof(source), "%s/%s", candidates[i], name);
            if (regular_nonempty(source))
                break;
        }
    }
    if (!regular_nonempty(source)) {
        *changed = false;
        return true;
    }
    snprintf(destination, sizeof(destination), "%s/configs/%s", setup_home(), name);
    if (files_equal(source, destination)) {
        *changed = false;
        return true;
    }
    if (strcmp(name, "mtsp-rules.toml") == 0 && regular_nonempty(destination)) {
        char backup[PATH_MAX];
        snprintf(backup, sizeof(backup), "%s.bak", destination);
        struct stat destination_info;
        if (stat(destination, &destination_info) == 0)
            (void)copy_file(destination, backup, destination_info.st_mode);
    }
    struct stat info;
    if (stat(source, &info) != 0 || !copy_file(source, destination, info.st_mode)) {
        snprintf(error, error_size, "write %s: %s", name, strerror(errno));
        return false;
    }
    *changed = true;
    return true;
}

static bool step_rules(bool* changed, char* error, size_t error_size) {
    return copy_resource_config("mtsp-rules.toml", changed, error, error_size);
}

static bool step_mono_configs(bool* changed, char* error, size_t error_size) {
    static const char* names[] = {"terraria-mono.config", "celeste-x86-mono.config", "stardew-mono.config",
                                  "generic-fna-mono.config"};
    *changed = false;
    for (size_t i = 0u; i < sizeof(names) / sizeof(names[0]); i++) {
        bool copied = false;
        if (!copy_resource_config(names[i], &copied, error, error_size))
            return false;
        *changed = *changed || copied;
    }
    return true;
}

static bool step_runtime_support(bool* changed, char* error, size_t error_size) {
    char mono[PATH_MAX];
    if (find_command("mono", mono, sizeof(mono))) {
        *changed = false;
        return true;
    }
    snprintf(mono, sizeof(mono), "%s/runtime/mono-arm64/bin/mono", setup_home());
    if (access(mono, X_OK) == 0) {
        *changed = false;
        return true;
    }
    char brew[PATH_MAX];
    if (!find_command("brew", brew, sizeof(brew))) {
        snprintf(error, error_size, "Homebrew not found — install it first");
        return false;
    }
    char* argv[] = {brew, "install", "mono", NULL};
    if (spawn_wait(brew, argv) != 0) {
        snprintf(error, error_size, "brew install failed");
        return false;
    }
    *changed = true;
    return true;
}

static bool find_fna_shim_source(const char* filename, bool terraria, char* output, size_t output_size) {
    char resources[PATH_MAX] = "";
    if (resources_directory(resources, sizeof(resources))) {
        const char* relative[] = {"runtime/shim-sources/fna/shims", "scripts/tools/fna/shims"};
        for (size_t i = 0u; i < sizeof(relative) / sizeof(relative[0]); i++) {
            int written = snprintf(output, output_size, "%s/%s/%s", resources, relative[i], filename);
            if (written >= 0 && (size_t)written < output_size && regular_nonempty(output))
                return true;
        }
    }
    const char* directory = terraria ? "src/fna/terraria" : "src/fna/shims";
    int written = snprintf(output, output_size, "%s/%s", directory, filename);
    return written >= 0 && (size_t)written < output_size && regular_nonempty(output);
}

static bool find_native_shim(const char* filename, char* output, size_t output_size) {
    char resources[PATH_MAX] = "";
    if (resources_directory(resources, sizeof(resources))) {
        const char* relative[] = {"scripts/tools/native", "runtime/shims"};
        for (size_t i = 0u; i < sizeof(relative) / sizeof(relative[0]); i++) {
            int written = snprintf(output, output_size, "%s/%s/%s", resources, relative[i], filename);
            if (written >= 0 && (size_t)written < output_size && regular_nonempty(output))
                return true;
        }
    }
    int written = snprintf(output, output_size, "app/native/%s", filename);
    return written >= 0 && (size_t)written < output_size && regular_nonempty(output);
}

static bool compile_fna_shim(const char* source, const char* destination, const char* install_name, bool objective_c,
                             bool cocoa_carbon, bool undefined_dynamic_lookup) {
    char install_name_arg[PATH_MAX];
    snprintf(install_name_arg, sizeof(install_name_arg), "@loader_path/%s", install_name);
    char* argv[24];
    size_t count = 0u;
    argv[count++] = "/usr/bin/clang";
    argv[count++] = "-shared";
    argv[count++] = "-arch";
    argv[count++] = "arm64";
    argv[count++] = "-arch";
    argv[count++] = "x86_64";
    if (objective_c)
        argv[count++] = "-fobjc-arc";
    if (undefined_dynamic_lookup) {
        argv[count++] = "-undefined";
        argv[count++] = "dynamic_lookup";
    }
    argv[count++] = "-o";
    argv[count++] = (char*)destination;
    argv[count++] = (char*)source;
    argv[count++] = "-install_name";
    argv[count++] = install_name_arg;
    if (cocoa_carbon) {
        argv[count++] = "-framework";
        argv[count++] = "Cocoa";
        argv[count++] = "-framework";
        argv[count++] = "Carbon";
    }
    argv[count] = NULL;
    if (spawn_wait("/usr/bin/clang", argv) != 0)
        return false;
    char* sign_argv[] = {"/usr/bin/codesign", "--force", "-s", "-", (char*)destination, NULL};
    (void)spawn_wait("/usr/bin/codesign", sign_argv);
    return regular_nonempty(destination);
}

static bool step_fna(bool* changed, char* error, size_t error_size) {
    typedef struct {
        const char* output;
        const char* source;
        bool terraria;
        bool objective_c;
        bool cocoa_carbon;
        bool undefined_dynamic_lookup;
        const char* symlink_a;
        const char* symlink_b;
    } ShimSpec;
    static const ShimSpec specs[] = {
        {"libkernel32.dylib", "kernel32_shim.c", false, false, false, false, NULL, NULL},
        {"libuser32.dylib", "user32_shim.c", false, false, false, false, NULL, NULL},
        {"libCarbon.dylib", "carbon_hiview_shim.m", false, true, true, false, NULL, NULL},
        {"libmetalsharp_carbon_interpose.dylib", "carbon_interpose.c", false, false, false, false, NULL, NULL},
        {"xaudio2_9.dylib", NULL, false, false, false, false, "xaudio2_8.dylib", "xaudio2_7.dylib"},
        {"libSystem.Native.dylib", "system_native_stub.c", false, false, false, false, NULL, NULL},
        {"xinput1_4.dylib", NULL, false, false, false, false, "xinput1_3.dylib", "xinput9_1_0.dylib"},
        {"libgdiplus.dylib", "gdiplus_stub.c", true, false, false, false, NULL, NULL},
        {"libFAudio.0.dylib", "faudio_stub.c", true, false, false, false, "libFAudio.dylib", NULL},
        {"libCSteamworks.dylib", "csteamworks_shim.c", false, false, false, true, NULL, NULL},
        {"libfmod.dylib", "fmod_stub.c", false, false, false, true, NULL, NULL},
        {"libfmodstudio.dylib", "fmodstudio_stub.c", false, false, false, true, NULL, NULL},
    };
    char shims[PATH_MAX];
    snprintf(shims, sizeof(shims), "%s/runtime/shims", setup_home());
    if (!mkdir_p(shims)) {
        snprintf(error, error_size, "FNA shim directory could not be created");
        return false;
    }
    *changed = false;
    for (size_t i = 0u; i < sizeof(specs) / sizeof(specs[0]); i++) {
        char destination[PATH_MAX];
        snprintf(destination, sizeof(destination), "%s/%s", shims, specs[i].output);
        if (!regular_nonempty(destination)) {
            char source[PATH_MAX];
            bool installed = specs[i].source != NULL
                                 ? find_fna_shim_source(specs[i].source, specs[i].terraria, source, sizeof(source)) &&
                                       compile_fna_shim(source, destination, specs[i].output, specs[i].objective_c,
                                                        specs[i].cocoa_carbon, specs[i].undefined_dynamic_lookup)
                                 : find_native_shim(specs[i].output, source, sizeof(source));
            if (specs[i].source == NULL && installed) {
                struct stat info;
                installed = stat(source, &info) == 0 && copy_file(source, destination, info.st_mode);
                if (installed) {
                    char* sign_argv[] = {"/usr/bin/codesign", "--force", "-s", "-", destination, NULL};
                    (void)spawn_wait("/usr/bin/codesign", sign_argv);
                }
            }
            *changed = *changed || installed;
        }
        if (regular_nonempty(destination)) {
            const char* links[] = {specs[i].symlink_a, specs[i].symlink_b};
            for (size_t link_index = 0u; link_index < 2u; link_index++) {
                if (links[link_index] == NULL)
                    continue;
                char link[PATH_MAX];
                snprintf(link, sizeof(link), "%s/%s", shims, links[link_index]);
                if (lstat(link, &(struct stat){0}) != 0) {
                    (void)unlink(link);
                    (void)symlink(specs[i].output, link);
                }
            }
        }
    }
    char fna[PATH_MAX];
    snprintf(fna, sizeof(fna), "%s/runtime/fnalibs/libFNA3D.0.dylib", setup_home());
    if (regular_nonempty(fna))
        return true;
    snprintf(error, error_size, "FNA support assets are missing");
    return false;
}

typedef bool (*SetupStepFn)(bool*, char*, size_t);

typedef struct {
    const char* name;
    SetupStepFn function;
} SetupStep;

static const SetupStep setup_steps[] = {
    {"System Tools", step_system_tools},       {"Rosetta 2", step_rosetta},
    {"Extract Tools (zstd)", step_zstd},       {"Runtime Bundle Downloads", step_bundles},
    {"Runtime Assets", step_runtime},          {"Host Runtime ABI", step_host},
    {"Support Assets", step_support},          {"Scripts and Tools", step_scripts},
    {"DXMT Graphics Runtimes", step_graphics}, {"Goldberg Steam Emulator", step_goldberg},
    {"Steam Bridge Shim", step_steam_bridge},  {"Pipeline Rules", step_rules},
    {"Mono Configs", step_mono_configs},       {"Runtime Support", step_runtime_support},
    {"FNA Shim Precompile", step_fna},
};

static void setup_sleep_millis(unsigned int milliseconds) {
    struct timespec remaining = {(time_t)(milliseconds / 1000u), (long)(milliseconds % 1000u) * 1000000L};
    while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {
    }
}

static void* setup_worker_main(void* unused) {
    (void)unused;
    write_progress(0u, SETUP_TOTAL_STEPS, "Starting...", "starting", "Verifying prerequisites...", NULL);
    /* The Rust worker performs prerequisite discovery before advancing the first
     * stage. Preserve that observable starting window for progress pollers. */
    setup_sleep_millis(350u);
    if (access("/usr/bin/tar", X_OK) != 0) {
        write_progress(0u, SETUP_TOTAL_STEPS, "Prerequisites", "error",
                       "tar not found — macOS should have this. Is your system intact?", "tar command not found");
        atomic_store(&g_setup_installing, false);
        return NULL;
    }
    if (access("/usr/bin/curl", X_OK) != 0) {
        write_progress(0u, SETUP_TOTAL_STEPS, "Prerequisites", "error",
                       "curl not found — install curl before installing runtime assets.", "curl command not found");
        atomic_store(&g_setup_installing, false);
        return NULL;
    }
    char brew[PATH_MAX];
    if (!find_command("brew", brew, sizeof(brew))) {
        write_progress(0u, SETUP_TOTAL_STEPS, "Homebrew", "error",
                       "Homebrew is required but not installed. Please install it first from the setup wizard.",
                       "Homebrew not installed — install from https://brew.sh");
        atomic_store(&g_setup_installing, false);
        return NULL;
    }
    for (size_t i = 0u; i < sizeof(setup_steps) / sizeof(setup_steps[0]); i++) {
        char log[512], error[1024] = "";
        snprintf(log, sizeof(log), "Installing %s...", setup_steps[i].name);
        write_progress((unsigned)i + 1u, SETUP_TOTAL_STEPS, setup_steps[i].name, "installing", log, NULL);
        bool changed = false;
        if (!setup_steps[i].function(&changed, error, sizeof(error))) {
            snprintf(log, sizeof(log), "%s failed: %s", setup_steps[i].name, error);
            write_progress((unsigned)i + 1u, SETUP_TOTAL_STEPS, setup_steps[i].name, "error", log, error);
            atomic_store(&g_setup_installing, false);
            return NULL;
        }
        snprintf(log, sizeof(log), "%s %s", setup_steps[i].name, changed ? "installed" : "ready");
        write_progress((unsigned)i + 1u, SETUP_TOTAL_STEPS, setup_steps[i].name, "done", log, NULL);
        setup_sleep_millis(200u);
    }
    write_progress(SETUP_TOTAL_STEPS, SETUP_TOTAL_STEPS, "Complete", "complete", "All assets installed!", NULL);
    atomic_store(&g_setup_installing, false);
    return NULL;
}

bool setup_worker_start(void) {
    bool expected = false;
    if (!atomic_compare_exchange_strong(&g_setup_installing, &expected, true))
        return false;
    pthread_t thread;
    if (pthread_create(&thread, NULL, setup_worker_main, NULL) != 0) {
        atomic_store(&g_setup_installing, false);
        return false;
    }
    pthread_detach(thread);
    return true;
}

bool setup_worker_is_installing(void) {
    return atomic_load(&g_setup_installing);
}
