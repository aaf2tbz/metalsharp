#include "bottles.h"

#include "runtime_surface.h"

#include <CommonCrypto/CommonDigest.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static const char* const m12_artifacts[] = {
    "x86_64-windows/d3d12.dll",   "x86_64-windows/d3d11.dll",      "x86_64-windows/d3d10core.dll",
    "x86_64-windows/dxgi.dll",    "x86_64-windows/dxgi_dxmt.dll",  "x86_64-windows/winemetal.dll",
    "x86_64-windows/nvapi64.dll", "x86_64-windows/nvngx.dll",      "x86_64-unix/winemetal.so",
    "x86_64-unix/libc++.1.dylib", "x86_64-unix/libc++abi.1.dylib", "x86_64-unix/libunwind.1.dylib",
};

static const char* const m11_artifacts[] = {
    "x86_64-windows/d3d11.dll",     "x86_64-windows/d3d10core.dll", "x86_64-windows/dxgi.dll",
    "x86_64-windows/dxgi_dxmt.dll", "x86_64-windows/winemetal.dll", "x86_64-unix/winemetal.so",
};

static const char* const m10_artifacts[] = {
    "x86_64-windows/d3d10core.dll", "x86_64-windows/d3d11.dll",     "x86_64-windows/dxgi.dll",
    "x86_64-windows/dxgi_dxmt.dll", "x86_64-windows/winemetal.dll", "x86_64-unix/winemetal.so",
};

static const char* const m11_i386_artifacts[] = {
    "i386-windows/d3d11.dll",     "i386-windows/d3d10core.dll", "i386-windows/dxgi.dll",
    "i386-windows/dxgi_dxmt.dll", "i386-windows/winemetal.dll", "i386-unix/winemetal.so",
};

static const char* const m10_i386_artifacts[] = {
    "i386-windows/d3d10core.dll", "i386-windows/d3d11.dll",     "i386-windows/dxgi.dll",
    "i386-windows/dxgi_dxmt.dll", "i386-windows/winemetal.dll", "i386-unix/winemetal.so",
};

#define POLICY(profile, pipeline, lane, arch, surface, files, d3d12)                                                   \
    {profile, pipeline, lane, arch, surface, METALSHARP_DXMT_MANIFEST_SHA256, files, sizeof(files) / sizeof(files[0]), \
     d3d12}

static const MetalsharpBottlePolicy policies[] = {
    POLICY("m12", "m12", "dxmt_m12", METALSHARP_BOTTLE_ARCH_X86_64, METALSHARP_DXMT_SURFACE_ID, m12_artifacts, true),
    POLICY("m11", "m11", "dxmt", METALSHARP_BOTTLE_ARCH_X86_64, METALSHARP_DXMT_SURFACE_ID, m11_artifacts, false),
    POLICY("m10", "m10", "dxmt", METALSHARP_BOTTLE_ARCH_X86_64, METALSHARP_DXMT_SURFACE_ID, m10_artifacts, false),
    POLICY("m11_32", "m11_32", "dxmt", METALSHARP_BOTTLE_ARCH_I386, METALSHARP_DXMT_I386_SURFACE_ID, m11_i386_artifacts,
           false),
    POLICY("m10_32", "m10_32", "dxmt", METALSHARP_BOTTLE_ARCH_I386, METALSHARP_DXMT_I386_SURFACE_ID, m10_i386_artifacts,
           false),
};

const MetalsharpBottlePolicy* metalsharp_bottle_policy(const char* profile_id) {
    if (profile_id == NULL)
        return NULL;
    for (size_t i = 0; i < sizeof(policies) / sizeof(policies[0]); ++i)
        if (strcmp(profile_id, policies[i].profile_id) == 0)
            return &policies[i];
    return NULL;
}

bool metalsharp_bottle_artifact_required(const MetalsharpBottlePolicy* policy, const char* relative_path) {
    if (policy == NULL || relative_path == NULL)
        return false;
    for (size_t i = 0; i < policy->artifact_count; ++i)
        if (strcmp(relative_path, policy->artifacts[i]) == 0)
            return true;
    return false;
}

bool metalsharp_bottle_policy_valid(const MetalsharpBottlePolicy* policy) {
    if (policy == NULL || policy->profile_id == NULL || policy->pipeline_id == NULL || policy->runtime_lane == NULL ||
        policy->surface_id == NULL || policy->manifest_sha256 == NULL || policy->artifacts == NULL ||
        policy->artifact_count == 0 || strcmp(policy->profile_id, policy->pipeline_id) != 0 ||
        strcmp(policy->manifest_sha256, METALSHARP_DXMT_MANIFEST_SHA256) != 0)
        return false;
    const bool has_d3d12 = metalsharp_bottle_artifact_required(policy, "x86_64-windows/d3d12.dll");
    if (has_d3d12 != policy->includes_d3d12 || (policy->includes_d3d12 && strcmp(policy->profile_id, "m12") != 0))
        return false;
    if (policy->architecture == METALSHARP_BOTTLE_ARCH_I386)
        return strcmp(policy->runtime_lane, "dxmt") == 0 &&
               strcmp(policy->surface_id, METALSHARP_DXMT_I386_SURFACE_ID) == 0;
    return strcmp(policy->surface_id, METALSHARP_DXMT_SURFACE_ID) == 0;
}

bool metalsharp_bottle_runtime_ready(const char* profile_id, const char* m12_root, size_t m12_root_len) {
    const MetalsharpBottlePolicy* policy = metalsharp_bottle_policy(profile_id);
    if (!metalsharp_bottle_policy_valid(policy) || m12_root == NULL || m12_root_len == 0)
        return false;

    /* The frozen surface verifier proves both x64 roots, mirrors, receipts, and the preserved i386 lane. */
    return metalsharp_dxmt_surface_current(m12_root, m12_root_len);
}

typedef struct {
    char target[PATH_MAX];
    char stage[PATH_MAX];
    char backup[PATH_MAX];
    bool remove_only;
    bool had_target;
    bool promoted;
    bool has_expected;
    off_t expected_size;
    unsigned char expected_digest[CC_SHA256_DIGEST_LENGTH];
} DeploymentEntry;

typedef struct {
    char id[256];
    char profile[32];
    char prefix[PATH_MAX];
    char game[PATH_MAX];
    unsigned long steam_app_id;
    bool has_game;
    bool has_steam_app_id;
} BottleManifest;

static bool join_path(char* output, size_t size, const char* left, const char* right) {
    const int count = snprintf(output, size, "%s/%s", left, right);
    return count >= 0 && (size_t)count < size;
}

static bool copy_path_slice(char* output, size_t size, const char* value, size_t length) {
    if (value == NULL || length == 0 || length >= size)
        return false;
    memcpy(output, value, length);
    output[length] = '\0';
    return true;
}

static bool read_file(const char* path, char** output, size_t* length) {
    *output = NULL;
    *length = 0;
    FILE* file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        if (file != NULL)
            fclose(file);
        return false;
    }
    const long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    char* bytes = malloc((size_t)size + 1);
    if (bytes == NULL) {
        fclose(file);
        return false;
    }
    const size_t count = fread(bytes, 1, (size_t)size, file);
    const bool ok = count == (size_t)size && !ferror(file) && fclose(file) == 0;
    if (!ok) {
        free(bytes);
        return false;
    }
    bytes[count] = '\0';
    *output = bytes;
    *length = count;
    return true;
}

static const char* json_value(const char* json, const char* key) {
    char needle[128];
    if (snprintf(needle, sizeof(needle), "\"%s\"", key) >= (int)sizeof(needle))
        return NULL;
    const char* cursor = strstr(json, needle);
    if (cursor == NULL)
        return NULL;
    cursor += strlen(needle);
    while (isspace((unsigned char)*cursor))
        ++cursor;
    if (*cursor++ != ':')
        return NULL;
    while (isspace((unsigned char)*cursor))
        ++cursor;
    return cursor;
}

static bool json_string(const char* json, const char* key, char* output, size_t size, bool optional, bool* present) {
    const char* cursor = json_value(json, key);
    if (present != NULL)
        *present = false;
    if (cursor == NULL || strncmp(cursor, "null", 4) == 0)
        return optional;
    if (*cursor++ != '"')
        return false;
    size_t used = 0;
    while (*cursor != '\0' && *cursor != '"') {
        unsigned char value = (unsigned char)*cursor++;
        if (value == '\\') {
            value = (unsigned char)*cursor++;
            switch (value) {
            case '"':
            case '\\':
            case '/':
                break;
            case 'b':
                value = '\b';
                break;
            case 'f':
                value = '\f';
                break;
            case 'n':
                value = '\n';
                break;
            case 'r':
                value = '\r';
                break;
            case 't':
                value = '\t';
                break;
            default:
                return false;
            }
        }
        if (used + 1 >= size)
            return false;
        output[used++] = (char)value;
    }
    if (*cursor != '"')
        return false;
    output[used] = '\0';
    if (present != NULL)
        *present = true;
    return used != 0 || optional;
}

static bool json_unsigned(const char* json, const char* key, unsigned long* output, bool* present) {
    const char* cursor = json_value(json, key);
    *present = false;
    if (cursor == NULL || strncmp(cursor, "null", 4) == 0)
        return true;
    if (!isdigit((unsigned char)*cursor))
        return false;
    errno = 0;
    char* end = NULL;
    const unsigned long value = strtoul(cursor, &end, 10);
    if (errno != 0 || end == cursor)
        return false;
    *output = value;
    *present = true;
    return true;
}

static bool parse_manifest(const char* json, BottleManifest* manifest) {
    memset(manifest, 0, sizeof(*manifest));
    bool ignored = false;
    return json_string(json, "id", manifest->id, sizeof(manifest->id), false, &ignored) &&
           json_string(json, "runtime_profile", manifest->profile, sizeof(manifest->profile), false, &ignored) &&
           json_string(json, "prefix_path", manifest->prefix, sizeof(manifest->prefix), false, &ignored) &&
           json_string(json, "game_install_path", manifest->game, sizeof(manifest->game), true, &manifest->has_game) &&
           json_unsigned(json, "steam_app_id", &manifest->steam_app_id, &manifest->has_steam_app_id);
}

static bool ensure_directory(const char* path) {
    char copy[PATH_MAX];
    const size_t length = strlen(path);
    if (length == 0 || length >= sizeof(copy))
        return false;
    memcpy(copy, path, length + 1);
    for (char* cursor = copy + 1; *cursor != '\0'; ++cursor) {
        if (*cursor != '/')
            continue;
        *cursor = '\0';
        if (mkdir(copy, 0700) != 0 && errno != EEXIST)
            return false;
        *cursor = '/';
    }
    return mkdir(copy, 0700) == 0 || errno == EEXIST;
}

static bool copy_file(const char* source, const char* destination) {
    int input = open(source, O_RDONLY);
    if (input < 0)
        return false;
    struct stat info;
    bool ok = fstat(input, &info) == 0 && S_ISREG(info.st_mode);
    int output = ok ? open(destination, O_WRONLY | O_CREAT | O_TRUNC, info.st_mode & 0777) : -1;
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
    const int saved_errno = errno;
    if (output >= 0 && close(output) != 0)
        ok = false;
    close(input);
    errno = saved_errno;
    if (!ok)
        unlink(destination);
    return ok;
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

static const char* basename_of(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash == NULL ? path : slash + 1;
}

static bool parent_directory(const char* path, char* output, size_t size) {
    const char* slash = strrchr(path, '/');
    if (slash == NULL || slash == path || (size_t)(slash - path) >= size)
        return false;
    const size_t length = (size_t)(slash - path);
    memcpy(output, path, length);
    output[length] = '\0';
    return true;
}

static bool stage_copy(DeploymentEntry* entry, const char* source, const char* target, size_t index) {
    char parent[PATH_MAX];
    if (!parent_directory(target, parent, sizeof(parent)) || !ensure_directory(parent) ||
        snprintf(entry->target, sizeof(entry->target), "%s", target) >= (int)sizeof(entry->target) ||
        snprintf(entry->stage, sizeof(entry->stage), "%s.metalsharp-stage-%d-%zu", target, getpid(), index) >=
            (int)sizeof(entry->stage) ||
        snprintf(entry->backup, sizeof(entry->backup), "%s.metalsharp-backup-%d-%zu", target, getpid(), index) >=
            (int)sizeof(entry->backup) ||
        !copy_file(source, entry->stage) || !files_equal(source, entry->stage) ||
        !file_digest(source, entry->expected_digest, &entry->expected_size)) {
        unlink(entry->stage);
        return false;
    }
    entry->remove_only = false;
    entry->had_target = access(target, F_OK) == 0;
    entry->promoted = false;
    entry->has_expected = true;
    return true;
}

static bool stage_text(DeploymentEntry* entry, const char* text, const char* target, size_t index) {
    char parent[PATH_MAX];
    if (!parent_directory(target, parent, sizeof(parent)) || !ensure_directory(parent) ||
        snprintf(entry->target, sizeof(entry->target), "%s", target) >= (int)sizeof(entry->target) ||
        snprintf(entry->stage, sizeof(entry->stage), "%s.metalsharp-stage-%d-%zu", target, getpid(), index) >=
            (int)sizeof(entry->stage) ||
        snprintf(entry->backup, sizeof(entry->backup), "%s.metalsharp-backup-%d-%zu", target, getpid(), index) >=
            (int)sizeof(entry->backup))
        return false;
    FILE* file = fopen(entry->stage, "wb");
    bool ok = file != NULL && fwrite(text, 1, strlen(text), file) == strlen(text) && fflush(file) == 0 &&
              fsync(fileno(file)) == 0;
    if (file != NULL && fclose(file) != 0)
        ok = false;
    if (!ok) {
        unlink(entry->stage);
        return false;
    }
    entry->remove_only = false;
    entry->had_target = access(target, F_OK) == 0;
    entry->promoted = false;
    entry->has_expected = file_digest(entry->stage, entry->expected_digest, &entry->expected_size);
    if (!entry->has_expected) {
        unlink(entry->stage);
        return false;
    }
    return true;
}

static bool stage_removal(DeploymentEntry* entry, const char* target, size_t index) {
    if (snprintf(entry->target, sizeof(entry->target), "%s", target) >= (int)sizeof(entry->target) ||
        snprintf(entry->backup, sizeof(entry->backup), "%s.metalsharp-backup-%d-%zu", target, getpid(), index) >=
            (int)sizeof(entry->backup))
        return false;
    entry->stage[0] = '\0';
    entry->remove_only = true;
    entry->had_target = access(target, F_OK) == 0;
    entry->promoted = false;
    entry->has_expected = false;
    return true;
}

static void rollback_entries(DeploymentEntry* entries, size_t count) {
    while (count > 0) {
        DeploymentEntry* entry = &entries[--count];
        if (!entry->promoted) {
            if (!entry->remove_only)
                unlink(entry->stage);
            continue;
        }
        unlink(entry->target);
        if (entry->had_target)
            rename(entry->backup, entry->target);
        if (!entry->remove_only)
            unlink(entry->stage);
        entry->promoted = false;
    }
}

static bool promote_entries(DeploymentEntry* entries, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        DeploymentEntry* entry = &entries[i];
        if (entry->had_target && rename(entry->target, entry->backup) != 0) {
            rollback_entries(entries, i);
            return false;
        }
        if (!entry->remove_only && rename(entry->stage, entry->target) != 0) {
            if (entry->had_target)
                rename(entry->backup, entry->target);
            rollback_entries(entries, i);
            return false;
        }
        entry->promoted = true;
        if (getenv("METALSHARP_TEST_BOTTLE_FAIL_AFTER_PROMOTION") != NULL) {
            rollback_entries(entries, i + 1);
            return false;
        }
    }
    return true;
}

static void commit_entries(DeploymentEntry* entries, size_t count) {
    for (size_t i = 0; i < count; ++i)
        if (entries[i].had_target)
            unlink(entries[i].backup);
}

static bool verify_entries(const DeploymentEntry* entries, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (entries[i].remove_only) {
            if (access(entries[i].target, F_OK) == 0)
                return false;
        } else if (entries[i].has_expected) {
            unsigned char digest[CC_SHA256_DIGEST_LENGTH];
            off_t size = 0;
            if (!file_digest(entries[i].target, digest, &size) || size != entries[i].expected_size ||
                memcmp(digest, entries[i].expected_digest, sizeof(digest)) != 0)
                return false;
        }
    }
    return true;
}

static bool atomic_health(const char* manifest_path, const char* health) {
    char* json = NULL;
    size_t length = 0;
    if (!read_file(manifest_path, &json, &length))
        return false;
    const char* value = json_value(json, "health");
    if (value == NULL || *value != '"') {
        free(json);
        return false;
    }
    const char* end = strchr(value + 1, '"');
    if (end == NULL) {
        free(json);
        return false;
    }
    char temporary[PATH_MAX];
    if (snprintf(temporary, sizeof(temporary), "%s.metalsharp-health-%d", manifest_path, getpid()) >=
        (int)sizeof(temporary)) {
        free(json);
        return false;
    }
    FILE* file = fopen(temporary, "wb");
    const size_t prefix = (size_t)(value - json);
    bool ok = file != NULL && fwrite(json, 1, prefix, file) == prefix && fprintf(file, "\"%s\"", health) > 0 &&
              fwrite(end + 1, 1, length - (size_t)(end + 1 - json), file) == length - (size_t)(end + 1 - json) &&
              fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (file != NULL && fclose(file) != 0)
        ok = false;
    free(json);
    if (ok)
        ok = rename(temporary, manifest_path) == 0;
    if (!ok)
        unlink(temporary);
    return ok;
}

static bool derive_home(const char* manifest_path, char* home, size_t size) {
    const char* configured = getenv("METALSHARP_HOME");
    if (configured != NULL && configured[0] != '\0')
        return snprintf(home, size, "%s", configured) < (int)size;
    const char marker[] = "/bottles/";
    const char* found = strstr(manifest_path, marker);
    if (found == NULL || found == manifest_path || (size_t)(found - manifest_path) >= size)
        return false;
    const size_t length = (size_t)(found - manifest_path);
    memcpy(home, manifest_path, length);
    home[length] = '\0';
    return true;
}

static bool add_copy(DeploymentEntry* entries, size_t* count, size_t capacity, const char* source, const char* target,
                     bool overwrite) {
    if (*count >= capacity || (!overwrite && access(target, F_OK) == 0))
        return !overwrite && access(target, F_OK) == 0;
    if (!stage_copy(&entries[*count], source, target, *count))
        return false;
    ++*count;
    return true;
}

static bool write_failure_receipt(const char* receipt, const BottleManifest* manifest,
                                  const MetalsharpBottlePolicy* policy) {
    char text[2048];
    const int count = snprintf(text, sizeof(text),
                               "{\n  \"schema\": \"metalsharp.dxmt-bottle-deployment.v1\",\n  \"bottle_id\": \"%s\",\n"
                               "  \"profile\": \"%s\",\n  \"surface_id\": \"%s\",\n  \"manifest_sha256\": \"%s\",\n"
                               "  \"status\": \"needs_repair\"\n}\n",
                               manifest->id, manifest->profile, policy->surface_id, policy->manifest_sha256);
    if (count <= 0 || count >= (int)sizeof(text))
        return false;
    DeploymentEntry entry;
    if (!stage_text(&entry, text, receipt, 9999) || !promote_entries(&entry, 1))
        return false;
    commit_entries(&entry, 1);
    return true;
}

bool metalsharp_reconcile_bottle_manifest(const char* manifest_path_value, size_t manifest_path_len) {
    char manifest_path[PATH_MAX], home[PATH_MAX], m12_root[PATH_MAX], lib_root[PATH_MAX], lane_root[PATH_MAX];
    char* json = NULL;
    size_t json_length = 0;
    BottleManifest manifest;
    if (!copy_path_slice(manifest_path, sizeof(manifest_path), manifest_path_value, manifest_path_len) ||
        !read_file(manifest_path, &json, &json_length) || !parse_manifest(json, &manifest)) {
        free(json);
        return false;
    }
    free(json);
    const MetalsharpBottlePolicy* policy = metalsharp_bottle_policy(manifest.profile);
    if (policy == NULL)
        return true;
    if (!metalsharp_bottle_policy_valid(policy) || !derive_home(manifest_path, home, sizeof(home)) ||
        !join_path(lib_root, sizeof(lib_root), home, "runtime/wine/lib") ||
        !join_path(m12_root, sizeof(m12_root), lib_root, "dxmt_m12") ||
        !join_path(lane_root, sizeof(lane_root), lib_root, policy->runtime_lane) ||
        !metalsharp_bottle_runtime_ready(manifest.profile, m12_root, strlen(m12_root))) {
        atomic_health(manifest_path, "needs_repair");
        return false;
    }

    char bottle_dir[PATH_MAX], receipt[PATH_MAX];
    if (!parent_directory(manifest_path, bottle_dir, sizeof(bottle_dir)) ||
        !join_path(receipt, sizeof(receipt), bottle_dir, "dxmt-deployment.json")) {
        atomic_health(manifest_path, "needs_repair");
        return false;
    }

    DeploymentEntry entries[48];
    size_t count = 0;
    bool ok = true;
    char system_dir[PATH_MAX];
    ok = join_path(system_dir, sizeof(system_dir), manifest.prefix,
                   policy->architecture == METALSHARP_BOTTLE_ARCH_I386 ? "drive_c/windows/syswow64"
                                                                       : "drive_c/windows/system32");
    for (size_t i = 0; ok && i < policy->artifact_count; ++i) {
        if (strstr(policy->artifacts[i], "-windows/") == NULL)
            continue;
        char source[PATH_MAX], prefix_target[PATH_MAX], game_target[PATH_MAX];
        ok = join_path(source, sizeof(source), lane_root, policy->artifacts[i]) &&
             join_path(prefix_target, sizeof(prefix_target), system_dir, basename_of(policy->artifacts[i])) &&
             add_copy(entries, &count, 48, source, prefix_target, true);
        if (ok && manifest.has_game)
            ok = join_path(game_target, sizeof(game_target), manifest.game, basename_of(policy->artifacts[i])) &&
                 add_copy(entries, &count, 48, source, game_target, true);
    }

    if (ok && !policy->includes_d3d12) {
        char prefix_d3d12[PATH_MAX], game_d3d12[PATH_MAX];
        ok = join_path(prefix_d3d12, sizeof(prefix_d3d12), system_dir, "d3d12.dll") && count < 48 &&
             stage_removal(&entries[count], prefix_d3d12, count);
        if (ok) {
            ++count;
            if (manifest.has_game) {
                ok = join_path(game_d3d12, sizeof(game_d3d12), manifest.game, "d3d12.dll") && count < 48 &&
                     stage_removal(&entries[count], game_d3d12, count);
                if (ok)
                    ++count;
            }
        }
    }

    /* Restore real Steam components from .orig, then fill only missing files from Wine Steam. */
    if (ok && manifest.has_game && manifest.has_steam_app_id) {
        static const char* const steam_files[] = {
            "steam_api64.dll",           "steam_api.dll",           "steamclient64.dll", "steamclient.dll",
            "GameOverlayRenderer64.dll", "GameOverlayRenderer.dll",
        };
        char wine_steam[PATH_MAX];
        ok = join_path(wine_steam, sizeof(wine_steam), home, "prefix-steam/drive_c/Program Files (x86)/Steam");
        for (size_t i = 0; ok && i < sizeof(steam_files) / sizeof(steam_files[0]); ++i) {
            char target[PATH_MAX], original[PATH_MAX], source[PATH_MAX];
            ok = join_path(target, sizeof(target), manifest.game, steam_files[i]) &&
                 snprintf(original, sizeof(original), "%s.orig", target) < (int)sizeof(original) &&
                 join_path(source, sizeof(source), wine_steam, steam_files[i]);
            if (!ok)
                break;
            if (access(original, R_OK) == 0)
                ok = add_copy(entries, &count, 48, original, target, true);
            else if (access(target, F_OK) != 0 && access(source, R_OK) == 0)
                ok = add_copy(entries, &count, 48, source, target, false);
        }
        char appid_target[PATH_MAX], appid_text[64];
        if (ok)
            ok = join_path(appid_target, sizeof(appid_target), manifest.game, "steam_appid.txt") &&
                 snprintf(appid_text, sizeof(appid_text), "%lu\n", manifest.steam_app_id) < (int)sizeof(appid_text) &&
                 count < 48 && stage_text(&entries[count], appid_text, appid_target, count);
        if (ok)
            ++count;
    }

    char receipt_text[4096];
    const char* architecture = policy->architecture == METALSHARP_BOTTLE_ARCH_I386 ? "i386" : "x86_64";
    const int receipt_length =
        snprintf(receipt_text, sizeof(receipt_text),
                 "{\n  \"schema\": \"metalsharp.dxmt-bottle-deployment.v1\",\n  \"bottle_id\": \"%s\",\n"
                 "  \"profile\": \"%s\",\n  \"architecture\": \"%s\",\n  \"runtime_lane\": \"%s\",\n"
                 "  \"surface_id\": \"%s\",\n  \"manifest_sha256\": \"%s\",\n"
                 "  \"canonical_runtime_verified\": true,\n  \"wine_loader_mirrors_verified\": true,\n"
                 "  \"prefix_route_verified\": true,\n  \"game_local_verified\": %s,\n"
                 "  \"artifact_count\": %zu,\n  \"status\": \"ready\"\n}\n",
                 manifest.id, manifest.profile, architecture, policy->runtime_lane, policy->surface_id,
                 policy->manifest_sha256, manifest.has_game ? "true" : "false", policy->artifact_count);
    if (ok)
        ok = receipt_length > 0 && receipt_length < (int)sizeof(receipt_text) && count < 48 &&
             stage_text(&entries[count], receipt_text, receipt, count);
    if (ok)
        ++count;

    if (ok)
        ok = promote_entries(entries, count);
    if (ok)
        ok = verify_entries(entries, count);
    if (ok)
        ok = atomic_health(manifest_path, "ready");
    if (!ok) {
        rollback_entries(entries, count);
        atomic_health(manifest_path, "needs_repair");
        write_failure_receipt(receipt, &manifest, policy);
        return false;
    }
    commit_entries(entries, count);
    return true;
}
