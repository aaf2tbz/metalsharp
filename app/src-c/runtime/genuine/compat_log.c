#include "compat_log.h"

#include <dirent.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static pthread_mutex_t g_app_log_lock = PTHREAD_MUTEX_INITIALIZER;

static const char* app_home(void) {
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL)
        home = getenv("HOME");
    return home != NULL ? home : "";
}

static bool log_paths(char* directory, size_t directory_size, char* file, size_t file_size, char* name,
                      size_t name_size) {
    time_t now = time(NULL);
    struct tm local;
    if (localtime_r(&now, &local) == NULL)
        return false;
    if (strftime(name, name_size, "%Y-%m-%d.log", &local) == 0)
        return false;
    int directory_n = snprintf(directory, directory_size, "%s/logs", app_home());
    int file_n = snprintf(file, file_size, "%s/%s", directory, name);
    return directory_n > 0 && (size_t)directory_n < directory_size && file_n > 0 && (size_t)file_n < file_size;
}

void metalsharp_app_log(const char* format, ...) {
    char message[4096];
    va_list args;
    va_start(args, format);
    int message_n = vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    if (message_n < 0)
        return;

    char directory[PATH_MAX];
    char path[PATH_MAX];
    char name[64];
    if (!log_paths(directory, sizeof(directory), path, sizeof(path), name, sizeof(name)))
        return;
    (void)mkdir(directory, 0755);

    time_t now = time(NULL);
    struct tm local;
    char timestamp[128] = "";
    if (localtime_r(&now, &local) != NULL)
        (void)strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S %Z", &local);

    pthread_mutex_lock(&g_app_log_lock);
    FILE* output = fopen(path, "ab");
    if (output != NULL) {
        fprintf(output, "[%s] %s\n", timestamp, message);
        fclose(output);
    }
    pthread_mutex_unlock(&g_app_log_lock);
}

static void append_json_string(char** destination, size_t* remaining, const char* text) {
    if (*remaining < 3u)
        return;
    *(*destination)++ = '"';
    (*remaining)--;
    for (const unsigned char* p = (const unsigned char*)text; *p != '\0' && *remaining > 2u; p++) {
        if (*p == '"' || *p == '\\') {
            *(*destination)++ = '\\';
            *(*destination)++ = (char)*p;
            *remaining -= 2u;
        } else if (*p >= 0x20) {
            *(*destination)++ = (char)*p;
            (*remaining)--;
        }
    }
    *(*destination)++ = '"';
    (*remaining)--;
    **destination = '\0';
}

static char** read_log_lines(size_t* count_out, char* name, size_t name_size) {
    *count_out = 0;
    char directory[PATH_MAX];
    char path[PATH_MAX];
    if (!log_paths(directory, sizeof(directory), path, sizeof(path), name, name_size))
        return NULL;
    FILE* input = fopen(path, "rb");
    if (input == NULL)
        return NULL;
    char** lines = NULL;
    size_t count = 0;
    size_t capacity = 0;
    char* line = NULL;
    size_t line_capacity = 0;
    ssize_t length = 0;
    while ((length = getline(&line, &line_capacity, input)) >= 0) {
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
            line[--length] = '\0';
        if (count == capacity) {
            size_t next = capacity == 0 ? 16u : capacity * 2u;
            char** grown = realloc(lines, next * sizeof(*grown));
            if (grown == NULL)
                break;
            lines = grown;
            capacity = next;
        }
        lines[count] = strdup(line);
        if (lines[count] == NULL)
            break;
        count++;
    }
    free(line);
    fclose(input);
    *count_out = count;
    return lines;
}

static void free_lines(char** lines, size_t count) {
    for (size_t i = 0; i < count; i++)
        free(lines[i]);
    free(lines);
}

char* metalsharp_log_stream_json(size_t after) {
    char name[64];
    size_t count = 0;
    char** lines = read_log_lines(&count, name, sizeof(name));
    size_t capacity = 128u;
    for (size_t i = after; i < count; i++)
        capacity += strlen(lines[i]) * 2u + 4u;
    char* result = calloc(1, capacity);
    if (result == NULL) {
        free_lines(lines, count);
        return NULL;
    }
    int n = snprintf(result, capacity, "{\"ok\":true,\"total\":%zu,\"lines\":[", count);
    char* cursor = result + (n > 0 ? n : 0);
    size_t remaining = capacity - (size_t)(cursor - result);
    for (size_t i = after; i < count; i++) {
        if (i > after && remaining > 1u) {
            *cursor++ = ',';
            remaining--;
        }
        append_json_string(&cursor, &remaining, lines[i]);
    }
    snprintf(cursor, remaining, "]}");
    free_lines(lines, count);
    return result;
}

typedef struct {
    char path[PATH_MAX];
    char name[PATH_MAX];
    struct timespec modified;
} CompatLogFile;

static bool has_log_extension(const char* name) {
    size_t length = strlen(name);
    return length >= 4u && strcmp(name + length - 4u, ".log") == 0;
}

static void collect_log_file(CompatLogFile* files, size_t* count, size_t capacity, const char* path, const char* name) {
    if (*count >= capacity || !has_log_extension(name))
        return;
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
        return;
    CompatLogFile* file = &files[(*count)++];
    snprintf(file->path, sizeof(file->path), "%s", path);
    snprintf(file->name, sizeof(file->name), "%s", name);
#if defined(__APPLE__)
    file->modified = st.st_mtimespec;
#else
    file->modified = st.st_mtim;
#endif
}

static int compare_log_files(const void* left, const void* right) {
    const CompatLogFile* a = left;
    const CompatLogFile* b = right;
    if (a->modified.tv_sec != b->modified.tv_sec)
        return a->modified.tv_sec < b->modified.tv_sec ? 1 : -1;
    if (a->modified.tv_nsec != b->modified.tv_nsec)
        return a->modified.tv_nsec < b->modified.tv_nsec ? 1 : -1;
    return strcmp(b->name, a->name);
}

static char* read_whole_log(const char* path, size_t* size_out) {
    *size_out = 0u;
    FILE* input = fopen(path, "rb");
    if (input == NULL)
        return NULL;
    if (fseek(input, 0, SEEK_END) != 0) {
        fclose(input);
        return NULL;
    }
    long length = ftell(input);
    if (length < 0 || fseek(input, 0, SEEK_SET) != 0) {
        fclose(input);
        return NULL;
    }
    char* content = malloc((size_t)length + 1u);
    if (content == NULL) {
        fclose(input);
        return NULL;
    }
    size_t read_count = fread(content, 1u, (size_t)length, input);
    fclose(input);
    content[read_count] = '\0';
    *size_out = read_count;
    return content;
}

char* metalsharp_log_list_json(void) {
    char directory[PATH_MAX], dated_path[PATH_MAX], dated_name[64];
    if (!log_paths(directory, sizeof(directory), dated_path, sizeof(dated_path), dated_name, sizeof(dated_name)))
        return NULL;
    CompatLogFile* files = calloc(256u, sizeof(*files));
    if (files == NULL)
        return NULL;
    size_t file_count = 0u;
    DIR* root = opendir(directory);
    if (root != NULL) {
        struct dirent* entry;
        while ((entry = readdir(root)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            char path[PATH_MAX];
            int path_n = snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
            if (path_n < 0 || (size_t)path_n >= sizeof(path))
                continue;
            struct stat st;
            if (stat(path, &st) != 0)
                continue;
            if (S_ISREG(st.st_mode)) {
                collect_log_file(files, &file_count, 256u, path, entry->d_name);
            } else if (S_ISDIR(st.st_mode)) {
                DIR* nested = opendir(path);
                if (nested == NULL)
                    continue;
                struct dirent* child;
                while ((child = readdir(nested)) != NULL) {
                    if (strcmp(child->d_name, ".") == 0 || strcmp(child->d_name, "..") == 0)
                        continue;
                    char child_path[PATH_MAX], relative[PATH_MAX];
                    int child_n = snprintf(child_path, sizeof(child_path), "%s/%s", path, child->d_name);
                    int relative_n = snprintf(relative, sizeof(relative), "%s/%s", entry->d_name, child->d_name);
                    if (child_n > 0 && (size_t)child_n < sizeof(child_path) && relative_n > 0 &&
                        (size_t)relative_n < sizeof(relative))
                        collect_log_file(files, &file_count, 256u, child_path, relative);
                }
                closedir(nested);
            }
        }
        closedir(root);
    }
    if (file_count == 0u) {
        free(files);
        return strdup("{\"ok\":true,\"logs\":[{\"name\":\"app.log\",\"lines\":[\"No logs yet. Logs will appear "
                      "here as you use MetalSharp.\"]}]}");
    }
    qsort(files, file_count, sizeof(files[0]), compare_log_files);
    if (file_count > 8u)
        file_count = 8u;
    char* contents[8] = {0};
    size_t content_sizes[8] = {0};
    size_t capacity = 256u;
    for (size_t i = 0u; i < file_count; i++) {
        contents[i] = read_whole_log(files[i].path, &content_sizes[i]);
        capacity += strlen(files[i].name) * 2u + content_sizes[i] * 2u + 128u;
    }
    char* result = calloc(1, capacity);
    if (result == NULL) {
        for (size_t i = 0u; i < file_count; i++)
            free(contents[i]);
        free(files);
        return NULL;
    }
    char* cursor = result;
    size_t remaining = capacity;
    int prefix = snprintf(cursor, remaining, "{\"ok\":true,\"logs\":[");
    cursor += prefix;
    remaining -= (size_t)prefix;
    for (size_t i = 0u; i < file_count; i++) {
        if (i > 0u) {
            *cursor++ = ',';
            remaining--;
        }
        int object_prefix = snprintf(cursor, remaining, "{\"name\":");
        cursor += object_prefix;
        remaining -= (size_t)object_prefix;
        append_json_string(&cursor, &remaining, files[i].name);
        int lines_prefix = snprintf(cursor, remaining, ",\"lines\":[");
        cursor += lines_prefix;
        remaining -= (size_t)lines_prefix;
        char* content = contents[i];
        size_t line_count = 0u;
        if (content != NULL) {
            bool ends_with_newline = content_sizes[i] > 0u && content[content_sizes[i] - 1u] == '\n';
            for (size_t p = 0u; p < content_sizes[i]; p++) {
                if (content[p] == '\n') {
                    content[p] = '\0';
                    line_count++;
                }
            }
            if (content_sizes[i] > 0u && !ends_with_newline)
                line_count++;
            size_t skip = line_count > 500u ? line_count - 500u : 0u;
            char* line = content;
            size_t emitted = 0u;
            for (size_t current = 0u; current < line_count; current++) {
                size_t length = strlen(line);
                if (current >= skip) {
                    if (length > 0u && line[length - 1u] == '\r')
                        line[length - 1u] = '\0';
                    if (emitted++ > 0u) {
                        *cursor++ = ',';
                        remaining--;
                    }
                    append_json_string(&cursor, &remaining, line);
                }
                line += length + 1u;
            }
        }
        int object_suffix = snprintf(cursor, remaining, "]}");
        cursor += object_suffix;
        remaining -= (size_t)object_suffix;
        free(contents[i]);
    }
    snprintf(cursor, remaining, "]}");
    free(files);
    return result;
}
