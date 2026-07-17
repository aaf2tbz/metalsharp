#include "migration.h"

#include "bottles.h"
#include "installer.h"

#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

static bool regular_file(const char* path) {
    struct stat info;
    return lstat(path, &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0;
}

static bool directory_not_symlink(const char* path) {
    struct stat info;
    return lstat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static bool read_profile(const char* manifest, char profile[32]) {
    FILE* file = fopen(manifest, "rb");
    if (file == NULL)
        return false;
    char contents[65536];
    const size_t count = fread(contents, 1, sizeof(contents) - 1, file);
    const bool read_ok = !ferror(file) && feof(file);
    fclose(file);
    if (!read_ok)
        return false;
    contents[count] = '\0';

    const char* cursor = strstr(contents, "\"runtime_profile\"");
    if (cursor == NULL || (cursor = strchr(cursor, ':')) == NULL)
        return false;
    ++cursor;
    while (isspace((unsigned char)*cursor))
        ++cursor;
    if (*cursor++ != '"')
        return false;
    const char* end = strchr(cursor, '"');
    const size_t length = end == NULL ? 0 : (size_t)(end - cursor);
    if (length == 0 || length >= 32)
        return false;
    memcpy(profile, cursor, length);
    profile[length] = '\0';
    return true;
}

static void write_report(const char* home, const MetalsharpMigrationBottleSummary* summary) {
    char logs[PATH_MAX], path[PATH_MAX], temporary[PATH_MAX];
    if (!join_path(logs, sizeof(logs), home, "logs") || (mkdir(logs, 0700) != 0 && access(logs, F_OK) != 0) ||
        !join_path(path, sizeof(path), logs, "migration-bottle-refresh-latest.json") ||
        snprintf(temporary, sizeof(temporary), "%s.tmp-%d", path, getpid()) >= (int)sizeof(temporary))
        return;

    const MetalsharpBottlePolicy* m12 = metalsharp_bottle_policy("m12");
    const MetalsharpBottlePolicy* legacy = metalsharp_bottle_policy("m11");
    FILE* file = fopen(temporary, "wb");
    if (file == NULL)
        return;
    const int result =
        fprintf(file,
                "{\n  \"schema\": \"metalsharp.migration-bottle-refresh.v1\",\n"
                "  \"legacy_surface_id\": \"%s\",\n  \"m12_surface_id\": \"%s\",\n"
                "  \"manifest_sha256\": \"%s\",\n"
                "  \"discovered\": %zu,\n  \"managed\": %zu,\n  \"refreshed\": %zu,\n"
                "  \"skipped\": %zu,\n  \"failed\": %zu,\n  \"status\": \"%s\"\n}\n",
                legacy == NULL ? "unknown" : legacy->surface_id, m12 == NULL ? "unknown" : m12->surface_id,
                m12 == NULL ? "unknown" : m12->manifest_sha256,
                summary->discovered, summary->managed, summary->refreshed, summary->skipped, summary->failed,
                summary->failed == 0 ? "ready" : "needs_repair");
    bool ok = result > 0 && fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (fclose(file) != 0)
        ok = false;
    if (ok)
        ok = rename(temporary, path) == 0;
    if (!ok)
        unlink(temporary);
}

bool metalsharp_migration_refresh_bottles(const char* home_value, size_t home_len,
                                          MetalsharpMigrationBottleSummary* summary) {
    char home[PATH_MAX], bottles[PATH_MAX];
    MetalsharpMigrationBottleSummary result = {0};
    if (summary != NULL)
        *summary = result;
    if (!copy_slice(home, sizeof(home), home_value, home_len) || !join_path(bottles, sizeof(bottles), home, "bottles"))
        return false;

    /* The baseline installer continues to own legacy dxmt verification. The
     * migration extension adds only the isolated M12 v2 contract. */
    char m12_root[PATH_MAX];
    if (!join_path(m12_root, sizeof(m12_root), home, "runtime/wine/lib/dxmt_m12") ||
        !metalsharp_m12_runtime_complete(m12_root, strlen(m12_root))) {
        result.failed = 1;
        write_report(home, &result);
        if (summary != NULL)
            *summary = result;
        return false;
    }

    DIR* entries = opendir(bottles);
    if (entries == NULL) {
        write_report(home, &result);
        return true;
    }

    struct dirent* entry;
    while ((entry = readdir(entries)) != NULL) {
        if (entry->d_name[0] == '.' &&
            (entry->d_name[1] == '\0' || (entry->d_name[1] == '.' && entry->d_name[2] == '\0')))
            continue;
        char bottle[PATH_MAX], manifest[PATH_MAX], profile[32];
        if (!join_path(bottle, sizeof(bottle), bottles, entry->d_name) || !directory_not_symlink(bottle) ||
            !join_path(manifest, sizeof(manifest), bottle, "bottle.json") || !regular_file(manifest))
            continue;
        ++result.discovered;
        if (!read_profile(manifest, profile) || metalsharp_bottle_policy(profile) == NULL) {
            ++result.skipped;
            continue;
        }
        ++result.managed;
        if (metalsharp_reconcile_bottle_manifest(manifest, strlen(manifest)))
            ++result.refreshed;
        else
            ++result.failed;
    }
    closedir(entries);
    write_report(home, &result);
    if (summary != NULL)
        *summary = result;
    return result.failed == 0;
}

bool metalsharp_migration_refresh_saved_bottles(void) {
    const char* configured = getenv("METALSHARP_HOME");
    if (configured != NULL && configured[0] != '\0')
        return metalsharp_migration_refresh_bottles(configured, strlen(configured), NULL);
    const char* home = getenv("HOME");
    char metalsharp_home[PATH_MAX];
    if (home == NULL || !join_path(metalsharp_home, sizeof(metalsharp_home), home, ".metalsharp"))
        return false;
    return metalsharp_migration_refresh_bottles(metalsharp_home, strlen(metalsharp_home), NULL);
}
