/* MetalFX Spatial Upscaling runtime state.
 * Behavioral port of the historical Rust metalfx.rs module. */

#include "database.h"
#include "http_server.h"
#include "json.h"
#include "logger.h"
#include "server.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

typedef struct {
    bool enabled;
    float factor;
    unsigned long long timestamp;
} MetalFxState;

static pthread_mutex_t g_metalfx_lock = PTHREAD_MUTEX_INITIALIZER;

static MetalsharpResponse* make_data_response(const char* body) {
    MetalsharpResponse* response = calloc(1, sizeof(*response));
    if (response == NULL)
        return NULL;
    char* parse_error = NULL;
    response->data = json_parse(body != NULL ? body : "null", body != NULL ? strlen(body) : 4u, &parse_error);
    if (response->data == NULL) {
        free(parse_error);
        response->error_msg = strdup("internal error");
        return response;
    }
    response->ok = true;
    response->data_kind = METALSHARP_RESPONSE_JSON_VALUE;
    return response;
}

static const char* metalfx_home(void) {
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL || home[0] == '\0')
        home = getenv("HOME");
    return home != NULL ? home : "";
}

static bool metalfx_path(char* output, size_t output_size, const char* relative) {
    int written =
        snprintf(output, output_size, "%s%s%s", metalfx_home(), metalfx_home()[0] != '\0' ? "/" : "", relative);
    return written >= 0 && (size_t)written < output_size;
}

static bool metalfx_mkdir_p(const char* path) {
    char copy[PATH_MAX];
    int written = snprintf(copy, sizeof(copy), "%s", path);
    if (written < 0 || (size_t)written >= sizeof(copy)) {
        errno = ENAMETOOLONG;
        return false;
    }
    for (char* p = copy + 1; *p != '\0'; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        if (mkdir(copy, 0755) != 0 && errno != EEXIST)
            return false;
        *p = '/';
    }
    return mkdir(copy, 0755) == 0 || errno == EEXIST;
}

static char* metalfx_read_file(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long size = ftell(file);
    if (size < 0 || size > 16 * 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    char* content = malloc((size_t)size + 1u);
    if (content == NULL) {
        fclose(file);
        return NULL;
    }
    size_t count = fread(content, 1u, (size_t)size, file);
    fclose(file);
    content[count] = '\0';
    return content;
}

static MetalFxState metalfx_read_state(void) {
    MetalFxState state = {.enabled = false, .factor = 2.0f, .timestamp = 0u};
    char path[PATH_MAX];
    if (!metalfx_path(path, sizeof(path), "etc/metalfx.overlay.json"))
        return state;
    char* content = metalfx_read_file(path);
    if (content == NULL)
        return state;
    JsonValue* root = json_parse(content, strlen(content), NULL);
    free(content);
    if (root == NULL || json_type(root) != JSON_OBJECT) {
        json_free(root);
        return state;
    }
    JsonValue* enabled = json_object_get(root, "enabled");
    JsonValue* factor = json_object_get(root, "factor");
    JsonValue* timestamp = json_object_get(root, "ts");
    if (enabled != NULL && json_type(enabled) == JSON_BOOL)
        state.enabled = json_get_bool(enabled, false);
    if (factor != NULL && json_type(factor) == JSON_NUMBER)
        state.factor = (float)json_get_number(factor, 2.0);
    if (timestamp != NULL && json_type(timestamp) == JSON_NUMBER)
        state.timestamp = (unsigned long long)json_get_number(timestamp, 0.0);
    json_free(root);
    return state;
}

static float metalfx_read_conf_factor(void) {
    char path[PATH_MAX];
    if (!metalfx_path(path, sizeof(path), "runtime/wine/etc/dxmt.conf"))
        return 2.0f;
    char* content = metalfx_read_file(path);
    if (content == NULL)
        return 2.0f;
    float result = 2.0f;
    for (char* line = content; line != NULL && *line != '\0';) {
        char* next = strchr(line, '\n');
        if (next != NULL)
            *next = '\0';
        while (*line == ' ' || *line == '\t' || *line == '\r')
            line++;
        static const char key[] = "d3d11.metalSpatialUpscaleFactor";
        if (strncmp(line, key, sizeof(key) - 1u) == 0) {
            char* value = line + sizeof(key) - 1u;
            while (*value == ' ' || *value == '\t')
                value++;
            if (*value == '=')
                value++;
            while (*value == ' ' || *value == '\t')
                value++;
            char* end = NULL;
            double parsed = strtod(value, &end);
            if (end != value)
                result = (float)parsed;
            break;
        }
        line = next != NULL ? next + 1 : NULL;
    }
    free(content);
    return result;
}

static bool metalfx_write_state(MetalFxState state, char* error, size_t error_size) {
    char directory[PATH_MAX], path[PATH_MAX];
    if (!metalfx_path(directory, sizeof(directory), "etc") || !metalfx_mkdir_p(directory)) {
        snprintf(error, error_size, "create etc dir: %s", strerror(errno));
        return false;
    }
    if (!metalfx_path(path, sizeof(path), "etc/metalfx.overlay.json")) {
        snprintf(error, error_size, "write metalfx state: %s", strerror(ENAMETOOLONG));
        return false;
    }
    FILE* file = fopen(path, "wb");
    if (file == NULL) {
        snprintf(error, error_size, "write metalfx state: %s", strerror(errno));
        return false;
    }
    int result = fprintf(file, "{\"enabled\":%s,\"factor\":%.9g,\"ts\":%llu}", state.enabled ? "true" : "false",
                         (double)state.factor, state.timestamp);
    int close_result = fclose(file);
    if (result < 0 || close_result != 0) {
        snprintf(error, error_size, "write metalfx state: %s", strerror(errno));
        return false;
    }
    return true;
}

static bool metalfx_line_is_factor(const char* line, size_t length) {
    while (length > 0u && (*line == ' ' || *line == '\t' || *line == '\r')) {
        line++;
        length--;
    }
    static const char key[] = "d3d11.metalSpatialUpscaleFactor";
    return length >= sizeof(key) - 1u && strncmp(line, key, sizeof(key) - 1u) == 0;
}

static bool metalfx_write_conf_factor(float factor, char* error, size_t error_size) {
    char directory[PATH_MAX], path[PATH_MAX];
    if (!metalfx_path(directory, sizeof(directory), "runtime/wine/etc") || !metalfx_mkdir_p(directory)) {
        snprintf(error, error_size, "create dxmt.conf dir: %s", strerror(errno));
        return false;
    }
    if (!metalfx_path(path, sizeof(path), "runtime/wine/etc/dxmt.conf")) {
        snprintf(error, error_size, "write dxmt.conf: %s", strerror(ENAMETOOLONG));
        return false;
    }
    char* existing = metalfx_read_file(path);
    if (existing == NULL)
        existing = strdup("");
    if (existing == NULL) {
        snprintf(error, error_size, "write dxmt.conf: %s", strerror(ENOMEM));
        return false;
    }
    size_t capacity = strlen(existing) + 128u;
    char* output = malloc(capacity);
    if (output == NULL) {
        free(existing);
        snprintf(error, error_size, "write dxmt.conf: %s", strerror(ENOMEM));
        return false;
    }
    size_t used = 0u;
    bool replaced = false;
    for (const char* cursor = existing; *cursor != '\0';) {
        const char* newline = strchr(cursor, '\n');
        size_t length = newline != NULL ? (size_t)(newline - cursor) : strlen(cursor);
        char replacement[96];
        const char* source = cursor;
        size_t source_length = length;
        if (metalfx_line_is_factor(cursor, length)) {
            int written = snprintf(replacement, sizeof(replacement), "d3d11.metalSpatialUpscaleFactor = %.2f", factor);
            source = replacement;
            source_length = written > 0 ? (size_t)written : 0u;
            replaced = true;
        }
        if (used + source_length + 2u > capacity) {
            capacity = (used + source_length + 2u) * 2u;
            char* grown = realloc(output, capacity);
            if (grown == NULL) {
                free(output);
                free(existing);
                snprintf(error, error_size, "write dxmt.conf: %s", strerror(ENOMEM));
                return false;
            }
            output = grown;
        }
        memcpy(output + used, source, source_length);
        used += source_length;
        output[used++] = '\n';
        cursor = newline != NULL ? newline + 1 : cursor + length;
    }
    if (!replaced) {
        int written = snprintf(output + used, capacity - used, "d3d11.metalSpatialUpscaleFactor = %.2f\n", factor);
        if (written < 0 || (size_t)written >= capacity - used) {
            free(output);
            free(existing);
            snprintf(error, error_size, "write dxmt.conf: %s", strerror(ENOMEM));
            return false;
        }
        used += (size_t)written;
    }
    free(existing);
    FILE* file = fopen(path, "wb");
    if (file == NULL) {
        free(output);
        snprintf(error, error_size, "write dxmt.conf: %s", strerror(errno));
        return false;
    }
    bool ok = fwrite(output, 1u, used, file) == used;
    if (fclose(file) != 0)
        ok = false;
    free(output);
    if (!ok) {
        snprintf(error, error_size, "write dxmt.conf: %s", strerror(errno));
        return false;
    }
    return true;
}

static MetalsharpResponse* metalfx_state_response(MetalFxState state, float conf_factor) {
    char body[512];
    int written = snprintf(
        body, sizeof(body),
        "{\"ok\":true,\"enabled\":%s,\"factor\":%.9g,\"conf_factor\":%.9g,"
        "\"source\":\"metalfx.overlay.json\","
        "\"applies\":\"on/off takes effect on the game's next swapchain recreate (alt-enter / resolution change); "
        "factor applies on relaunch\"}",
        state.enabled ? "true" : "false", (double)state.factor, (double)conf_factor);
    return written >= 0 && (size_t)written < sizeof(body) ? make_data_response(body) : NULL;
}

static MetalsharpResponse* handle_metalfx_state(const HttpRequest* req) {
    (void)req;
    pthread_mutex_lock(&g_metalfx_lock);
    MetalFxState state = metalfx_read_state();
    float conf_factor = metalfx_read_conf_factor();
    pthread_mutex_unlock(&g_metalfx_lock);
    return metalfx_state_response(state, conf_factor);
}

static MetalsharpResponse* handle_metalfx_toggle(const HttpRequest* req) {
    JsonValue* body = req != NULL ? json_parse(req->body, req->body_len, NULL) : NULL;
    pthread_mutex_lock(&g_metalfx_lock);
    MetalFxState state = metalfx_read_state();
    if (body != NULL && json_type(body) == JSON_OBJECT) {
        JsonValue* enabled = json_object_get(body, "enabled");
        JsonValue* factor = json_object_get(body, "factor");
        if (enabled != NULL && json_type(enabled) == JSON_BOOL)
            state.enabled = json_get_bool(enabled, state.enabled);
        if (factor != NULL && json_type(factor) == JSON_NUMBER) {
            float requested = (float)json_get_number(factor, state.factor);
            if (isfinite(requested) && requested >= 1.0f && requested <= 3.0f)
                state.factor = requested;
        }
    }
    json_free(body);
    state.timestamp = (unsigned long long)time(NULL);
    char state_error[512] = "", conf_error[512] = "";
    bool state_ok = metalfx_write_state(state, state_error, sizeof(state_error));
    bool conf_ok = metalfx_write_conf_factor(state.factor, conf_error, sizeof(conf_error));
    float conf_factor = metalfx_read_conf_factor();
    pthread_mutex_unlock(&g_metalfx_lock);
    if (state_ok && conf_ok)
        return metalfx_state_response(state, conf_factor);
    char body_text[1400];
    const char* separator = !state_ok && !conf_ok ? "; " : "";
    int written =
        snprintf(body_text, sizeof(body_text), "{\"ok\":false,\"error\":\"%s%s%s\",\"enabled\":%s,\"factor\":%.9g}",
                 state_ok ? "" : state_error, separator, conf_ok ? "" : conf_error, state.enabled ? "true" : "false",
                 (double)state.factor);
    return written >= 0 && (size_t)written < sizeof(body_text) ? make_data_response(body_text) : NULL;
}

void metalfx_register_routes(HttpServer* server, Database* db) {
    (void)db;
    if (server == NULL) {
        LOG_WARN("metalfx_register_routes called with NULL server");
        return;
    }
    http_server_register(server, "GET", "/metalfx/state", handle_metalfx_state);
    http_server_register(server, "POST", "/metalfx/toggle", handle_metalfx_toggle);
    LOG_INFO("metalfx routes registered (2)");
}
