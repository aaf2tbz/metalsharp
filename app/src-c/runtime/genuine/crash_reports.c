#include "crash_reports.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#define REPORT_LIMIT 4096u

typedef struct {
    char file[PATH_MAX];
    char name[PATH_MAX];
    char source[256];
    char pipeline[32];
    char timestamp[128];
    unsigned long long size_bytes;
} CrashReport;

typedef struct {
    CrashReport* items;
    size_t count;
    size_t capacity;
} CrashReports;

static const char* report_home(void) {
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL || home[0] == '\0')
        home = getenv("HOME");
    return home != NULL ? home : "";
}

static bool path_join(char* output, size_t output_size, const char* left, const char* right) {
    int written = snprintf(output, output_size, "%s%s%s", left,
                           left[0] != '\0' && left[strlen(left) - 1u] == '/' ? "" : "/", right);
    return written >= 0 && (size_t)written < output_size;
}

static bool directory_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
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

static void local_timestamp(time_t value, char* output, size_t output_size) {
    struct tm local;
    if (localtime_r(&value, &local) != NULL && strftime(output, output_size, "%Y-%m-%d %H:%M:%S %Z", &local) > 0u)
        return;
    snprintf(output, output_size, "%lld", (long long)value);
}

static const char* base_name(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash != NULL ? slash + 1 : path;
}

static bool contains_case_insensitive(const char* text, const char* needle) {
    size_t needle_length = strlen(needle);
    if (needle_length == 0u)
        return true;
    for (const char* cursor = text; *cursor != '\0'; cursor++) {
        size_t i = 0u;
        while (i < needle_length && cursor[i] != '\0' &&
               tolower((unsigned char)cursor[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == needle_length)
            return true;
    }
    return false;
}

static bool positive_numeric_id(const char* value) {
    if (value == NULL || value[0] == '\0')
        return false;
    unsigned long long parsed = 0u;
    for (const unsigned char* cursor = (const unsigned char*)value; *cursor != '\0'; cursor++) {
        if (!isdigit(*cursor))
            return false;
        parsed = parsed * 10u + (unsigned long long)(*cursor - '0');
        if (parsed > 4294967295ull)
            return false;
    }
    return parsed > 0u;
}

static const char* pipeline_for_appid(const char* appid) {
    if (!positive_numeric_id(appid))
        return "M11";
    char installed[PATH_MAX];
    path_join(installed, sizeof(installed), report_home(), "configs/mtsp-rules.toml");
    char resource_rules[PATH_MAX] = "";
#ifdef __APPLE__
    uint32_t executable_size = PATH_MAX;
    char executable[PATH_MAX];
    if (_NSGetExecutablePath(executable, &executable_size) == 0) {
        char* slash = strrchr(executable, '/');
        if (slash != NULL) {
            *slash = '\0';
            (void)snprintf(resource_rules, sizeof(resource_rules), "%s/../configs/mtsp-rules.toml", executable);
        }
    }
#endif
    const char* candidates[] = {installed, resource_rules, "configs/mtsp-rules.toml", "../configs/mtsp-rules.toml",
                                "../../configs/mtsp-rules.toml"};
    char expected[320];
    snprintf(expected, sizeof(expected), "[overrides.%s]", appid);
    for (size_t candidate = 0u; candidate < sizeof(candidates) / sizeof(candidates[0]); candidate++) {
        if (candidates[candidate][0] == '\0')
            continue;
        FILE* input = fopen(candidates[candidate], "rb");
        if (input == NULL)
            continue;
        char line[2048];
        bool section = false;
        while (fgets(line, sizeof(line), input) != NULL) {
            char* cursor = line;
            while (*cursor == ' ' || *cursor == '\t')
                cursor++;
            size_t length = strlen(cursor);
            while (length > 0u && (cursor[length - 1u] == '\n' || cursor[length - 1u] == '\r' ||
                                   cursor[length - 1u] == ' ' || cursor[length - 1u] == '\t'))
                cursor[--length] = '\0';
            if (cursor[0] == '[') {
                section = strcmp(cursor, expected) == 0;
                continue;
            }
            if (!section || strncmp(cursor, "pipeline", 8u) != 0)
                continue;
            char* quote = strchr(cursor, '"');
            if (quote == NULL)
                continue;
            quote++;
            char* end = strchr(quote, '"');
            if (end == NULL)
                continue;
            *end = '\0';
            fclose(input);
            if (strcmp(quote, "m12") == 0)
                return "M12";
            if (strcmp(quote, "m11") == 0 || strcmp(quote, "dxmt") == 0)
                return "M11";
            if (strcmp(quote, "m9") == 0)
                return "M9";
            if (strcmp(quote, "fna-arm64") == 0)
                return "FNA/Mono";
            return "Other";
        }
        fclose(input);
    }
    return "M12";
}

static bool crash_name(const char* name) {
    static const char* patterns[] = {"crash", ".dmp", ".mdmp", "crashdump", "crash_report"};
    for (size_t i = 0u; i < sizeof(patterns) / sizeof(patterns[0]); i++)
        if (contains_case_insensitive(name, patterns[i]))
            return true;
    return false;
}

static bool reports_push(CrashReports* reports, const char* path, const char* name, const char* source,
                         const char* pipeline, const struct stat* st) {
    if (reports->count >= REPORT_LIMIT)
        return false;
    if (reports->count == reports->capacity) {
        size_t next = reports->capacity == 0u ? 32u : reports->capacity * 2u;
        if (next > REPORT_LIMIT)
            next = REPORT_LIMIT;
        CrashReport* grown = realloc(reports->items, next * sizeof(*grown));
        if (grown == NULL)
            return false;
        reports->items = grown;
        reports->capacity = next;
    }
    CrashReport* report = &reports->items[reports->count++];
    memset(report, 0, sizeof(*report));
    snprintf(report->file, sizeof(report->file), "%s", path);
    snprintf(report->name, sizeof(report->name), "%s", name);
    snprintf(report->source, sizeof(report->source), "%s", source);
    snprintf(report->pipeline, sizeof(report->pipeline), "%s", pipeline);
#if defined(__APPLE__)
    local_timestamp(st->st_mtimespec.tv_sec, report->timestamp, sizeof(report->timestamp));
#else
    local_timestamp(st->st_mtim.tv_sec, report->timestamp, sizeof(report->timestamp));
#endif
    report->size_bytes = (unsigned long long)st->st_size;
    return true;
}

static void slugify(const char* input, char* output, size_t output_size) {
    char mapped[PATH_MAX * 2u];
    size_t mapped_length = 0u;
    for (const unsigned char* cursor = (const unsigned char*)input;
         *cursor != '\0' && mapped_length + 1u < sizeof(mapped); cursor++) {
        mapped[mapped_length++] = *cursor < 0x80u && isalnum(*cursor) ? (char)tolower(*cursor) : '-';
    }
    size_t start = 0u;
    while (start < mapped_length && mapped[start] == '-')
        start++;
    while (mapped_length > start && mapped[mapped_length - 1u] == '-')
        mapped_length--;
    if (start == mapped_length) {
        snprintf(output, output_size, "unknown");
        return;
    }
    size_t length = mapped_length - start;
    if (length > 80u)
        length = 80u;
    if (length >= output_size)
        length = output_size - 1u;
    memcpy(output, mapped + start, length);
    output[length] = '\0';
}

static void persist_crash_log(const char* source, const char* path, const char* timestamp,
                              unsigned long long size_bytes) {
    char directory[PATH_MAX], source_slug[96], name_slug[96], timestamp_slug[96], filename[384], destination[PATH_MAX];
    if (!path_join(directory, sizeof(directory), report_home(), "logs/crash-reports") || !mkdir_p(directory))
        return;
    slugify(source, source_slug, sizeof(source_slug));
    slugify(base_name(path), name_slug, sizeof(name_slug));
    slugify(timestamp, timestamp_slug, sizeof(timestamp_slug));
    int filename_length =
        snprintf(filename, sizeof(filename), "crash-%s-%s-%s.log", source_slug, name_slug, timestamp_slug);
    if (filename_length < 0 || (size_t)filename_length >= sizeof(filename) ||
        !path_join(destination, sizeof(destination), directory, filename))
        return;
    struct stat existing;
    if (stat(destination, &existing) == 0)
        return;
    FILE* output = fopen(destination, "wb");
    if (output == NULL)
        return;
    char now[128];
    local_timestamp(time(NULL), now, sizeof(now));
    fprintf(output, "timestamp: %s\ncrash_timestamp: %s\nsource: %s\nfile: %s\nsize_bytes: %llu", now, timestamp,
            source, path, size_bytes);
    fclose(output);
}

static void scan_crash_files(const char* directory, const char* source, const char* pipeline, CrashReports* reports,
                             unsigned depth) {
    if (depth > 2u)
        return;
    DIR* stream = opendir(directory);
    if (stream == NULL)
        return;
    struct dirent* entry;
    while ((entry = readdir(stream)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char path[PATH_MAX];
        if (!path_join(path, sizeof(path), directory, entry->d_name))
            continue;
        struct stat st;
        if (stat(path, &st) != 0)
            continue;
        if (crash_name(entry->d_name) && reports_push(reports, path, entry->d_name, source, pipeline, &st))
            persist_crash_log(source, path, reports->items[reports->count - 1u].timestamp,
                              (unsigned long long)st.st_size);
        if (S_ISDIR(st.st_mode))
            scan_crash_files(path, source, pipeline, reports, depth + 1u);
    }
    closedir(stream);
}

static bool dump_name(const char* name) {
    size_t length = strlen(name);
    return (length >= 4u && strcasecmp(name + length - 4u, ".dmp") == 0) ||
           (length >= 5u && strcasecmp(name + length - 5u, ".mdmp") == 0) || contains_case_insensitive(name, "crash");
}

static void scan_steam_dumps(const char* directory, CrashReports* reports) {
    DIR* stream = opendir(directory);
    if (stream == NULL)
        return;
    struct dirent* entry;
    while ((entry = readdir(stream)) != NULL) {
        if (!dump_name(entry->d_name))
            continue;
        char path[PATH_MAX];
        if (!path_join(path, sizeof(path), directory, entry->d_name))
            continue;
        struct stat st;
        if (stat(path, &st) != 0)
            continue;
        const char* pipeline = "System";
        char copy[PATH_MAX];
        snprintf(copy, sizeof(copy), "%s", entry->d_name);
        char* first = strchr(copy, '_');
        if (first != NULL && first[1] != '\0')
            pipeline = "System";
        (void)reports_push(reports, path, entry->d_name, "steam-dumps", pipeline, &st);
    }
    closedir(stream);
}

static int compare_reports(const void* left, const void* right) {
    const CrashReport* a = left;
    const CrashReport* b = right;
    return strcmp(b->timestamp, a->timestamp);
}

static void append_json_string(char** cursor, size_t* remaining, const char* value) {
    if (*remaining < 3u)
        return;
    *(*cursor)++ = '"';
    (*remaining)--;
    for (const unsigned char* p = (const unsigned char*)value; *p != '\0' && *remaining > 7u; p++) {
        const char* escape = NULL;
        char unicode[7];
        if (*p == '"')
            escape = "\\\"";
        else if (*p == '\\')
            escape = "\\\\";
        else if (*p == '\n')
            escape = "\\n";
        else if (*p == '\r')
            escape = "\\r";
        else if (*p == '\t')
            escape = "\\t";
        else if (*p < 0x20u) {
            snprintf(unicode, sizeof(unicode), "\\u%04x", *p);
            escape = unicode;
        }
        if (escape != NULL) {
            size_t length = strlen(escape);
            memcpy(*cursor, escape, length);
            *cursor += length;
            *remaining -= length;
        } else {
            *(*cursor)++ = (char)*p;
            (*remaining)--;
        }
    }
    *(*cursor)++ = '"';
    (*remaining)--;
    **cursor = '\0';
}

char* metalsharp_crash_reports_json(void) {
    CrashReports reports = {0};
    char root[PATH_MAX];
    if (!path_join(root, sizeof(root), report_home(), "games"))
        return NULL;
    DIR* games = opendir(root);
    if (games != NULL) {
        struct dirent* entry;
        while ((entry = readdir(games)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            char path[PATH_MAX];
            if (path_join(path, sizeof(path), root, entry->d_name) && directory_exists(path))
                scan_crash_files(path, entry->d_name, pipeline_for_appid(entry->d_name), &reports, 0u);
        }
        closedir(games);
    }
    if (path_join(root, sizeof(root), report_home(), "bottles")) {
        DIR* bottles = opendir(root);
        if (bottles != NULL) {
            struct dirent* entry;
            while ((entry = readdir(bottles)) != NULL) {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                    continue;
                char bottle[PATH_MAX], logs[PATH_MAX];
                if (path_join(bottle, sizeof(bottle), root, entry->d_name) &&
                    path_join(logs, sizeof(logs), bottle, "logs") && directory_exists(logs)) {
                    const char* steam_id = strncmp(entry->d_name, "steam_", 6u) == 0 ? entry->d_name + 6u : NULL;
                    scan_crash_files(logs, entry->d_name, pipeline_for_appid(steam_id), &reports, 0u);
                }
            }
            closedir(bottles);
        }
    }
    if (path_join(root, sizeof(root), report_home(), "prefix-steam/drive_c/Program Files (x86)/Steam/dumps") &&
        directory_exists(root))
        scan_steam_dumps(root, &reports);
    static const char* system_paths[] = {
        "prefix-steam/drive_c/users/steamuser/AppData/Local/CrashDumps",
        "prefix-steam/drive_c/ProgramData/CrashDumps",
    };
    for (size_t i = 0u; i < sizeof(system_paths) / sizeof(system_paths[0]); i++) {
        if (path_join(root, sizeof(root), report_home(), system_paths[i]) && directory_exists(root))
            scan_crash_files(root, "system", "System", &reports, 0u);
    }
    qsort(reports.items, reports.count, sizeof(*reports.items), compare_reports);
    size_t capacity = 128u;
    for (size_t i = 0u; i < reports.count; i++)
        capacity += (strlen(reports.items[i].file) + strlen(reports.items[i].name) + strlen(reports.items[i].source) +
                     strlen(reports.items[i].pipeline) + strlen(reports.items[i].timestamp)) *
                        2u +
                    192u;
    char* result = calloc(1, capacity);
    if (result == NULL) {
        free(reports.items);
        return NULL;
    }
    char* cursor = result;
    size_t remaining = capacity;
    int prefix = snprintf(cursor, remaining, "{\"ok\":true,\"reports\":[");
    cursor += prefix;
    remaining -= (size_t)prefix;
    for (size_t i = 0u; i < reports.count; i++) {
        if (i > 0u) {
            *cursor++ = ',';
            remaining--;
        }
        int n = snprintf(cursor, remaining, "{\"file\":");
        cursor += n;
        remaining -= (size_t)n;
        append_json_string(&cursor, &remaining, reports.items[i].file);
        n = snprintf(cursor, remaining, ",\"name\":");
        cursor += n;
        remaining -= (size_t)n;
        append_json_string(&cursor, &remaining, reports.items[i].name);
        n = snprintf(cursor, remaining, ",\"source\":");
        cursor += n;
        remaining -= (size_t)n;
        append_json_string(&cursor, &remaining, reports.items[i].source);
        n = snprintf(cursor, remaining, ",\"pipeline\":");
        cursor += n;
        remaining -= (size_t)n;
        append_json_string(&cursor, &remaining, reports.items[i].pipeline);
        n = snprintf(cursor, remaining, ",\"timestamp\":");
        cursor += n;
        remaining -= (size_t)n;
        append_json_string(&cursor, &remaining, reports.items[i].timestamp);
        n = snprintf(cursor, remaining, ",\"size_bytes\":%llu}", reports.items[i].size_bytes);
        cursor += n;
        remaining -= (size_t)n;
    }
    snprintf(cursor, remaining, "]}");
    free(reports.items);
    return result;
}
