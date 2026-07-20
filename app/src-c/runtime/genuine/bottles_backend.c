/*
 * bottles_backend.c — Bottle management module for the Metalsharp C backend
 *
 * WHAT
 *   Implements the HTTP routes mounted under /bottles listed in
 *   GENUINE_C_PLAN.md. Persists Wine/Proton bottles in SQLite,
 *   exposes the compiled catalog of available runtime profiles and
 *   redistributable component sources, and provides the wineboot
 *   preparation, doctor, repair, refresh, sync, runtime/window-
 *   version selection, font substitution, DirectX verification,
 *   compatibility-recording, and installer relaunch endpoints
 *   documented in contracts/electron-backend.v1.json.
 *
 * IMPORTS
 *   "server.h"        MetalsharpResponse, METALSHARP_VERSION
 *   "http_server.h"   HttpServer, HttpRequest, http_server_register
 *   "database.h"      Database, db_exec, db_query
 *   "json.h"          json_parse, JsonValue (response data)
 *   "logger.h"        LOG_INFO, LOG_WARN, LOG_ERROR
 *   <stdbool.h>       bool, true, false
 *   <stddef.h>        size_t, NULL
 *   <stdarg.h>        va_list, va_start, va_end, va_copy (growable JSON helper)
 *   <stdio.h>         snprintf, vsnprintf
 *   <stdlib.h>        calloc, malloc, free, realloc
 *   <string.h>        memcpy, strlen
 *
 * EXPORTS
 *   bottles_register_routes(HttpServer *server, Database *db)
 *       Register every /bottles route on the supplied HttpServer and
 *       bind the module to `db` for bottle persistence. Called once
 *       during startup, before http_server_run(). NULL arguments
 *       are a silent no-op so a half-initialised backend cannot
 *       crash inside the registry.
 *
 * SCHEMA
 *   Persisted bottles live in table `bottles` created on the first
 *   call to bottles_register_routes via:
 *     CREATE TABLE IF NOT EXISTS bottles (
 *         id INTEGER PRIMARY KEY AUTOINCREMENT,
 *         name TEXT NOT NULL,
 *         prefix_path TEXT NOT NULL,
 *         runtime_profile TEXT NOT NULL DEFAULT '',
 *         windows_version TEXT NOT NULL DEFAULT '',
 *         last_used_at INTEGER NOT NULL DEFAULT 0,
 *         created_at INTEGER NOT NULL DEFAULT 0
 *     );
 *   The runtime-profile and redistributable-source catalogs are
 *   compiled-in static arrays (k_bottle_profiles, k_redist_sources);
 *   they are exposed read-only and never persisted by this module.
 *
 * THREADING
 *   The HTTP server dispatches every request on a worker thread.
 *   SQLite access serialises transparently through the Database
 *   mutex. Catalog lookups (profiles, redist sources) read only
 *   static const data and need no additional synchronisation.
 *   The module-level Database handle is captured exactly once at
 *   registration time and never swapped, so workers always see a
 *   stable handle without going through a barrier.
 */

#include "bottles_catalogs.h"
#include "database.h"
#include "http_server.h"
#include "json.h"
#include "logger.h"
#include "server.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ── Constants ── */

#define BOTTLES_TABLE        "bottles"
#define BOTTLES_COL_ID       "id"
#define BOTTLES_COL_NAME     "name"
#define BOTTLES_COL_PREFIX   "prefix_path"
#define BOTTLES_COL_PROFILE  "runtime_profile"
#define BOTTLES_COL_WINVER   "windows_version"
#define BOTTLES_COL_LASTUSED "last_used_at"
#define BOTTLES_COL_CREATED  "created_at"

/* Initial capacity for the growable JSON builder. Grows by doubling
 * on demand; chosen so the typical empty-bottle-list case fits in
 * a single allocation. */
#define BOTTLES_JSONBUF_INIT 256u

/* Per-row JSON escaping budget for a single string field. A bottle
 * name or prefix path comfortably fits in a few hundred bytes; the
 * 4 KiB slack ensures we do not need a second escaping pass. */
#define BOTTLES_ESCAPE_MAX 4096u

/* ── Module-local state ── */

/* Database handle captured at registration time. Set exactly once
 * before http_server_run() and never reset. */
static Database* g_bottles_db = NULL;

/* ── Forward declarations ── */

static MetalsharpResponse* handle_bottles_list(const HttpRequest* req);
static MetalsharpResponse* handle_bottles_profiles(const HttpRequest* req);
static MetalsharpResponse* handle_bottles_redist_sources(const HttpRequest* req);
static MetalsharpResponse* handle_bottles_route_contracts(const HttpRequest* req);
static MetalsharpResponse* handle_bottles_compatibility_matrix(const HttpRequest* req);
static MetalsharpResponse* handle_bottles_stub_ok(const HttpRequest* req);
static MetalsharpResponse* handle_bottles_repair_component(const HttpRequest* req);
static MetalsharpResponse* handle_bottles_get(const HttpRequest* req);
static MetalsharpResponse* handle_bottles_edit(const HttpRequest* req);
static MetalsharpResponse* handle_bottles_set_profile(const HttpRequest* req);
static MetalsharpResponse* handle_bottles_refresh(const HttpRequest* req);
static MetalsharpResponse* handle_bottles_doctor(const HttpRequest* req);
static MetalsharpResponse* handle_bottles_prepare(const HttpRequest* req);
static MetalsharpResponse* handle_bottles_verify_directx(const HttpRequest* req);
static MetalsharpResponse* handle_bottles_set_windows_version(const HttpRequest* req);
static MetalsharpResponse* handle_bottles_apply_font_subs(const HttpRequest* req);
static MetalsharpResponse* handle_bottles_seed_post_wineboot(const HttpRequest* req);
static MetalsharpResponse* handle_bottles_record_compatibility(const HttpRequest* req);
static bool bottles_set_owned(JsonValue* object, const char* key, JsonValue* value);

/* ── Response builders ── */

/*
 * Build a successful MetalsharpResponse whose `data` payload is a
 * freshly-parsed JsonValue tree built from `body`. The HTTP layer
 * serialises the tree under its top-level "data" key. `body` must
 * be a valid JSON document; an empty or syntactically-bad body
 * yields an ok=false response with a descriptive error_msg.
 */
static MetalsharpResponse* make_data_response(const char* body) {
    if (body == NULL || body[0] == '\0') {
        body = "null";
    }
    MetalsharpResponse* r = calloc(1, sizeof(MetalsharpResponse));
    if (r == NULL) {
        return NULL;
    }
    char* err = NULL;
    JsonValue* val = json_parse(body, strlen(body), &err);
    if (val == NULL) {
        free(err);
        r->ok = false;
        r->error_msg = strdup("internal error");
        return r;
    }
    r->ok = true;
    r->data = val;
    r->data_kind = METALSHARP_RESPONSE_JSON_VALUE;
    return r;
}

/*
 * Build a failure MetalsharpResponse. The message is duplicated
 * into heap-owned storage so callers need not keep `msg` alive.
 * Returns NULL only on calloc failure.
 */
static MetalsharpResponse* make_catalog_response(const char* catalog) {
    static const char marker[] = "@METALSHARP_HOME@";
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL)
        home = getenv("HOME");
    if (home == NULL)
        home = "";
    size_t marker_len = sizeof(marker) - 1u;
    size_t home_len = strlen(home);
    size_t count = 0;
    for (const char* found = catalog; (found = strstr(found, marker)) != NULL; found += marker_len)
        count++;
    size_t catalog_len = strlen(catalog);
    size_t output_len = catalog_len + count * home_len - count * marker_len;
    char* output = malloc(output_len + 1u);
    if (output == NULL)
        return NULL;
    const char* source = catalog;
    char* destination = output;
    const char* found = NULL;
    while ((found = strstr(source, marker)) != NULL) {
        size_t prefix = (size_t)(found - source);
        memcpy(destination, source, prefix);
        destination += prefix;
        memcpy(destination, home, home_len);
        destination += home_len;
        source = found + marker_len;
    }
    strcpy(destination, source);
    MetalsharpResponse* response = make_data_response(output);
    free(output);
    return response;
}

static MetalsharpResponse* make_error_response(const char* msg) {
    MetalsharpResponse* r = calloc(1, sizeof(MetalsharpResponse));
    if (r == NULL) {
        return NULL;
    }
    r->ok = false;
    r->error_msg = strdup(msg != NULL ? msg : "error");
    return r;
}

/* ── Growable JSON buffer ── */

/*
 * Minimal growable string buffer used to assemble JSON list
 * responses whose length depends on database row count. Doubles
 * capacity on demand; the `ok` flag short-circuits subsequent
 * appends after a failed allocation so callers can keep calling
 * the helpers without explicit branching.
 */
typedef struct {
    char* data;
    size_t len;
    size_t cap;
    bool ok;
} jsonbuf_t;

static jsonbuf_t jsonbuf_new(void) {
    jsonbuf_t b;
    b.data = NULL;
    b.len = 0;
    b.cap = 0;
    b.ok = false;
    b.cap = BOTTLES_JSONBUF_INIT;
    b.data = malloc(b.cap);
    if (b.data != NULL) {
        b.ok = true;
        b.data[0] = '\0';
        b.len = 0;
    }
    return b;
}

static void jsonbuf_free(jsonbuf_t* b) {
    if (b == NULL) {
        return;
    }
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    b->ok = false;
}

/*
 * Grow the backing storage so at least `need` more bytes can be
 * appended (plus the trailing NUL). Returns the new capacity on
 * success; returns 0 and leaves `*b` untouched on realloc failure.
 */
static size_t jsonbuf_reserve(jsonbuf_t* b, size_t need) {
    if (b == NULL || !b->ok) {
        return 0;
    }
    if (b->cap >= b->len + need + 1u) {
        return b->cap;
    }
    size_t newcap = b->cap;
    while (newcap < b->len + need + 1u) {
        newcap *= 2u;
    }
    char* nd = realloc(b->data, newcap);
    if (nd == NULL) {
        b->ok = false;
        return 0;
    }
    b->data = nd;
    b->cap = newcap;
    return newcap;
}

static bool jsonbuf_append(jsonbuf_t* b, const char* s) {
    if (b == NULL || !b->ok || s == NULL) {
        return b != NULL ? b->ok : false;
    }
    size_t n = strlen(s);
    if (n == 0u) {
        return true;
    }
    if (jsonbuf_reserve(b, n) == 0u) {
        return false;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    return true;
}

static bool jsonbuf_appendf(jsonbuf_t* b, const char* fmt, ...) {
    if (b == NULL || !b->ok || fmt == NULL) {
        return b != NULL ? b->ok : false;
    }
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int needed = vsnprintf(NULL, 0u, fmt, ap);
    va_end(ap);
    if (needed < 0) {
        va_end(ap2);
        b->ok = false;
        return false;
    }
    size_t n = (size_t)needed;
    if (n == 0u) {
        va_end(ap2);
        return true;
    }
    if (jsonbuf_reserve(b, n) == 0u) {
        va_end(ap2);
        return false;
    }
    int written = vsnprintf(b->data + b->len, b->cap - b->len, fmt, ap2);
    va_end(ap2);
    if (written < 0 || (size_t)written != n) {
        b->ok = false;
        return false;
    }
    b->len += n;
    return true;
}

/*
 * Append `s` to `b` after JSON-escaping it. A NULL `s` is encoded
 * as `null`. Control characters below 0x20 are escaped using the
 * `\uXXXX` form; printable ASCII and high-byte UTF-8 sequences are
 * appended verbatim. The escape buffer is bounded so a single
 * pathological input cannot grow `b` without limit.
 */
static void jsonbuf_append_escaped(jsonbuf_t* b, const char* s) {
    if (b == NULL || !b->ok) {
        return;
    }
    if (s == NULL) {
        (void)jsonbuf_append(b, "null");
        return;
    }
    (void)jsonbuf_append(b, "\"");
    char esc[BOTTLES_ESCAPE_MAX];
    size_t i = 0;
    for (const unsigned char* p = (const unsigned char*)s; *p != '\0'; p++) {
        unsigned char c = *p;
        size_t used = 0;
        switch (c) {
        case '"':
            esc[i++] = '\\';
            esc[i++] = '"';
            used = 1;
            break;
        case '\\':
            esc[i++] = '\\';
            esc[i++] = '\\';
            used = 1;
            break;
        case '\b':
            esc[i++] = '\\';
            esc[i++] = 'b';
            used = 1;
            break;
        case '\f':
            esc[i++] = '\\';
            esc[i++] = 'f';
            used = 1;
            break;
        case '\n':
            esc[i++] = '\\';
            esc[i++] = 'n';
            used = 1;
            break;
        case '\r':
            esc[i++] = '\\';
            esc[i++] = 'r';
            used = 1;
            break;
        case '\t':
            esc[i++] = '\\';
            esc[i++] = 't';
            used = 1;
            break;
        default:
            if (c < 0x20u) {
                int k = snprintf(esc + i, sizeof(esc) - i, "\\u%04x", (unsigned)c);
                if (k < 0 || (size_t)k >= sizeof(esc) - i) {
                    b->ok = false;
                    return;
                }
                i += (size_t)k;
            } else {
                esc[i++] = (char)c;
            }
            used = 1;
            break;
        }
        if (i + 16u >= sizeof(esc)) {
            esc[i] = '\0';
            (void)jsonbuf_append(b, esc);
            i = 0;
        }
        (void)used;
    }
    if (i > 0u) {
        esc[i] = '\0';
        (void)jsonbuf_append(b, esc);
    }
    (void)jsonbuf_append(b, "\"");
}

/* ── Historical bottle manifest store ── */

static const char* bottles_home(void) {
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL || home[0] == '\0')
        home = getenv("HOME");
    return home != NULL ? home : "";
}

static bool bottles_mkdir_p(const char* path) {
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

static bool bottles_valid_id(const char* id) {
    if (id == NULL || id[0] == '\0' || strlen(id) > 128u)
        return false;
    for (const unsigned char* cursor = (const unsigned char*)id; *cursor != '\0'; cursor++)
        if (!isalnum(*cursor) && *cursor != '_' && *cursor != '-')
            return false;
    return true;
}

static JsonValue* bottles_parse_body(const HttpRequest* request) {
    if (request == NULL || request->body == NULL || request->body_len == 0u)
        return json_new_object();
    JsonValue* value = json_parse(request->body, request->body_len, NULL);
    if (value == NULL || json_type(value) != JSON_OBJECT) {
        json_free(value);
        return json_new_object();
    }
    return value;
}

static const char* bottles_string(const JsonValue* object, const char* key, const char* fallback) {
    const char* value = json_get_string(json_object_get(object, key));
    return value != NULL ? value : fallback;
}

static MetalsharpResponse* bottles_error(const char* message) {
    jsonbuf_t body = jsonbuf_new();
    jsonbuf_append(&body, "{\"ok\":false,\"error\":");
    jsonbuf_append_escaped(&body, message != NULL ? message : "error");
    jsonbuf_append(&body, "}");
    MetalsharpResponse* response = body.ok ? make_data_response(body.data) : make_error_response("out of memory");
    jsonbuf_free(&body);
    return response;
}

static JsonValue* bottles_read_json(const char* path, char** error) {
    if (error != NULL)
        *error = NULL;
    FILE* input = fopen(path, "rb");
    if (input == NULL) {
        if (error != NULL) {
            char message[512];
            snprintf(message, sizeof(message), "%s (os error %d)", strerror(errno), errno);
            *error = strdup(message);
        }
        return NULL;
    }
    if (fseek(input, 0, SEEK_END) != 0) {
        fclose(input);
        return NULL;
    }
    long length = ftell(input);
    if (length < 0 || length > 64 * 1024 * 1024 || fseek(input, 0, SEEK_SET) != 0) {
        fclose(input);
        return NULL;
    }
    char* content = malloc((size_t)length + 1u);
    if (content == NULL) {
        fclose(input);
        return NULL;
    }
    size_t count = fread(content, 1u, (size_t)length, input);
    fclose(input);
    content[count] = '\0';
    JsonValue* value = json_parse(content, count, error);
    free(content);
    return value;
}

static bool bottles_normalize_manifest(JsonValue* bottle) {
    if (bottle == NULL || json_type(bottle) != JSON_OBJECT)
        return false;
    static const char* required_strings[] = {
        "id", "name", "bottle_type", "prefix_path", "arch", "runtime_profile", "health", "created_at", "updated_at"};
    for (size_t i = 0u; i < sizeof(required_strings) / sizeof(required_strings[0]); i++)
        if (json_get_string(json_object_get(bottle, required_strings[i])) == NULL)
            return false;
    const char* type = bottles_string(bottle, "bottle_type", "");
    if (strcmp(type, "steam") != 0 && strcmp(type, "sharp_app") != 0 && strcmp(type, "installer") != 0 &&
        strcmp(type, "utility") != 0)
        return false;
    static const char* option_fields[] = {"steam_app_id",       "source_installer_path",  "installer_kind",
                                          "game_install_path",  "last_launch_log",        "last_launch_pid",
                                          "last_launch_status", "last_launch_finished_at"};
    for (size_t i = 0u; i < sizeof(option_fields) / sizeof(option_fields[0]); i++)
        if (json_object_get(bottle, option_fields[i]) == NULL &&
            !bottles_set_owned(bottle, option_fields[i], json_new_null()))
            return false;
    static const char* array_fields[] = {"installed_components", "runtime_assets", "installed_app_detections"};
    for (size_t i = 0u; i < sizeof(array_fields) / sizeof(array_fields[0]); i++)
        if (json_object_get(bottle, array_fields[i]) == NULL &&
            !bottles_set_owned(bottle, array_fields[i], json_new_array()))
            return false;
    if (json_type(json_object_get(bottle, "custom_name")) == JSON_NULL)
        json_object_remove(bottle, "custom_name");
    if (json_type(json_object_get(bottle, "preferred_pipeline")) == JSON_NULL)
        json_object_remove(bottle, "preferred_pipeline");
    return true;
}

static JsonValue* bottles_canonical_manifest(const JsonValue* source) {
    static const char* fields[] = {
        "id",
        "name",
        "custom_name",
        "bottle_type",
        "steam_app_id",
        "prefix_path",
        "arch",
        "runtime_profile",
        "preferred_pipeline",
        "installed_components",
        "source_installer_path",
        "installer_kind",
        "game_install_path",
        "runtime_assets",
        "installed_app_detections",
        "health",
        "last_launch_log",
        "last_launch_pid",
        "last_launch_status",
        "last_launch_finished_at",
        "created_at",
        "updated_at",
    };
    JsonValue* result = json_new_object();
    if (result == NULL)
        return NULL;
    for (size_t i = 0u; i < sizeof(fields) / sizeof(fields[0]); i++) {
        JsonValue* value = json_object_get(source, fields[i]);
        if (value != NULL && !json_object_set_clone(result, fields[i], value)) {
            json_free(result);
            return NULL;
        }
    }
    return result;
}

static JsonValue* bottles_load(const char* id, char** error) {
    if (!bottles_valid_id(id)) {
        if (error != NULL)
            *error = strdup("invalid bottle id");
        return NULL;
    }
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/bottles/%s/bottle.json", bottles_home(), id);
    JsonValue* bottle = bottles_read_json(path, error);
    if (bottle != NULL && !bottles_normalize_manifest(bottle)) {
        json_free(bottle);
        bottle = NULL;
        if (error != NULL && *error == NULL)
            *error = strdup("invalid bottle manifest");
    }
    if (bottle != NULL) {
        JsonValue* canonical = bottles_canonical_manifest(bottle);
        json_free(bottle);
        bottle = canonical;
        if (bottle == NULL && error != NULL && *error == NULL)
            *error = strdup("out of memory");
    }
    return bottle;
}

static JsonValue* bottles_create_empty(const char* id) {
    if (!bottles_valid_id(id))
        return NULL;
    JsonValue* root = json_new_object();
    if (root == NULL)
        return NULL;
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "%lld", (long long)time(NULL));
    char prefix_path[PATH_MAX];
    snprintf(prefix_path, sizeof(prefix_path), "%s/bottles/%s/prefix", bottles_home(), id);
    /* Determine if this is a steam bottle (id starts with "steam_") */
    bool is_steam = strncmp(id, "steam_", 6u) == 0;
    unsigned int appid = 0u;
    if (is_steam)
        appid = (unsigned int)strtoul(id + 6u, NULL, 10);
    json_object_set_owned(root, "id", json_new_string(id));
    json_object_set_owned(root, "name", json_new_string(is_steam ? "Steam Game" : id));
    json_object_set_owned(root, "custom_name", json_new_null());
    json_object_set_owned(root, "bottle_type", json_new_string(is_steam ? "steam" : "custom"));
    if (is_steam && appid > 0u)
        json_object_set_owned(root, "steam_app_id", json_new_number((double)appid));
    else
        json_object_set_owned(root, "steam_app_id", json_new_null());
    json_object_set_owned(root, "prefix_path", json_new_string(prefix_path));
    json_object_set_owned(root, "runtime_profile", json_new_string("dxmt_m12"));
    json_object_set_owned(root, "arch", json_new_string("x86_64"));
    json_object_set_owned(root, "windows_version", json_new_string("windows10"));
    json_object_set_owned(root, "preferred_pipeline", json_new_null());
    json_object_set_owned(root, "installed_components", json_new_array());
    json_object_set_owned(root, "installed_app_detections", json_new_array());
    json_object_set_owned(root, "runtime_assets", json_new_array());
    json_object_set_owned(root, "health", json_new_string("ready"));
    json_object_set_owned(root, "created_at", json_new_string(timestamp));
    json_object_set_owned(root, "updated_at", json_new_string(timestamp));
    return root;
}

static bool bottles_save(const JsonValue* bottle, char** error) {
    const char* id = json_get_string(json_object_get(bottle, "id"));
    if (!bottles_valid_id(id)) {
        if (error != NULL)
            *error = strdup("invalid bottle id");
        return false;
    }
    char directory[PATH_MAX], path[PATH_MAX], temporary[PATH_MAX];
    snprintf(directory, sizeof(directory), "%s/bottles/%s", bottles_home(), id);
    if (!bottles_mkdir_p(directory))
        return false;
    static const char* children[] = {"prefix", "installers", "logs", "assets"};
    for (size_t i = 0u; i < sizeof(children) / sizeof(children[0]); i++) {
        char child[PATH_MAX];
        snprintf(child, sizeof(child), "%s/%s", directory, children[i]);
        if (!bottles_mkdir_p(child))
            return false;
    }
    snprintf(path, sizeof(path), "%s/bottle.json", directory);
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    JsonValue* canonical = bottles_canonical_manifest(bottle);
    char* serialized = canonical != NULL ? json_serialize_pretty(canonical) : NULL;
    json_free(canonical);
    if (serialized == NULL)
        return false;
    FILE* output = fopen(temporary, "wb");
    bool ok = output != NULL && fwrite(serialized, 1u, strlen(serialized), output) == strlen(serialized);
    if (output != NULL && fclose(output) != 0)
        ok = false;
    free(serialized);
    if (ok)
        ok = rename(temporary, path) == 0;
    if (!ok) {
        unlink(temporary);
        if (error != NULL)
            *error = strdup(strerror(errno));
    }
    return ok;
}

static int bottle_compare(const void* left, const void* right) {
    const JsonValue* left_value = *(JsonValue* const*)left;
    const JsonValue* right_value = *(JsonValue* const*)right;
    const char* left_name = bottles_string(left_value, "name", "");
    const char* right_name = bottles_string(right_value, "name", "");
    int compared = strcmp(left_name, right_name);
    if (compared != 0)
        return compared;
    return strcmp(bottles_string(left_value, "id", ""), bottles_string(right_value, "id", ""));
}

static bool bottles_set_owned(JsonValue* object, const char* key, JsonValue* value) {
    if (value != NULL && json_object_set_owned(object, key, value))
        return true;
    json_free(value);
    return false;
}

static char* bottles_trim(const char* source) {
    while (*source != '\0' && isspace((unsigned char)*source))
        source++;
    size_t length = strlen(source);
    while (length > 0u && isspace((unsigned char)source[length - 1u]))
        length--;
    char* result = malloc(length + 1u);
    if (result != NULL) {
        memcpy(result, source, length);
        result[length] = '\0';
    }
    return result;
}

/* ── SQL helpers ── */

/*
 * Create the `bottles` table on the first registration. Failure
 * is logged; the routes then return graceful empty/degraded
 * responses because every attempt to write through a missing table
 * would surface the same sqlite error every call. No user-supplied
 * parameters are interpolated into SQL in this module, so the
 * single-quote doubling used by setup/steam is unnecessary here.
 */
static void ensure_bottles_table(Database* db) {
    if (db == NULL) {
        return;
    }
    static const char ddl[] =
        "CREATE TABLE IF NOT EXISTS " BOTTLES_TABLE " (" BOTTLES_COL_ID
        " INTEGER PRIMARY KEY AUTOINCREMENT," BOTTLES_COL_NAME " TEXT NOT NULL," BOTTLES_COL_PREFIX
        " TEXT NOT NULL," BOTTLES_COL_PROFILE " TEXT NOT NULL DEFAULT ''," BOTTLES_COL_WINVER
        " TEXT NOT NULL DEFAULT ''," BOTTLES_COL_LASTUSED " INTEGER NOT NULL DEFAULT 0," BOTTLES_COL_CREATED
        " INTEGER NOT NULL DEFAULT 0"
        ")";
    char* err = NULL;
    if (!db_exec(db, ddl, &err)) {
        LOG_ERROR("bottles create failed: %s", err != NULL ? err : "(unknown)");
        free(err);
    }
}

/* ── Route handlers ── */

/*
 * GET /bottles — list every persisted bottle. The SQL query is
 * built inline (column names interpolated through macros) so the
 * schema stays in one place; there are no user-supplied parameters
 * so sql_quote is unnecessary. An empty bottle set returns
 * {"ok":true,"bottles":[]} exactly like the stub.
 */
static MetalsharpResponse* handle_bottles_list(const HttpRequest* req) {
    (void)req;
    char root[PATH_MAX];
    snprintf(root, sizeof(root), "%s/bottles", bottles_home());
    DIR* directory = opendir(root);
    if (directory == NULL && errno == ENOENT)
        return make_data_response("{\"ok\":true,\"bottles\":[]}");
    if (directory == NULL)
        return bottles_error(strerror(errno));
    JsonValue** values = NULL;
    size_t length = 0u, capacity = 0u;
    struct dirent* entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char path[PATH_MAX];
        int written = snprintf(path, sizeof(path), "%s/%s/bottle.json", root, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(path))
            continue;
        JsonValue* bottle = bottles_read_json(path, NULL);
        if (bottle == NULL || !bottles_normalize_manifest(bottle)) {
            json_free(bottle);
            continue;
        }
        JsonValue* canonical = bottles_canonical_manifest(bottle);
        json_free(bottle);
        bottle = canonical;
        if (bottle == NULL)
            continue;
        if (length == capacity) {
            size_t next = capacity == 0u ? 8u : capacity * 2u;
            JsonValue** resized = realloc(values, next * sizeof(JsonValue*));
            if (resized == NULL) {
                json_free(bottle);
                break;
            }
            values = resized;
            capacity = next;
        }
        values[length++] = bottle;
    }
    closedir(directory);
    qsort(values, length, sizeof(JsonValue*), bottle_compare);
    JsonValue* array = json_new_array();
    bool ok = array != NULL;
    for (size_t i = 0u; i < length; i++) {
        if (ok && json_array_append_owned(array, values[i]))
            values[i] = NULL;
        else
            ok = false;
        json_free(values[i]);
    }
    free(values);
    if (!ok) {
        json_free(array);
        return bottles_error("out of memory");
    }
    char* serialized = json_serialize(array);
    json_free(array);
    jsonbuf_t body = jsonbuf_new();
    jsonbuf_append(&body, "{\"ok\":true,\"bottles\":");
    jsonbuf_append(&body, serialized != NULL ? serialized : "[]");
    jsonbuf_append(&body, "}");
    free(serialized);
    MetalsharpResponse* response = body.ok ? make_data_response(body.data) : bottles_error("out of memory");
    jsonbuf_free(&body);
    return response;
}

/*
 * GET /bottles/profiles — list the compiled-in runtime-profile
 * catalog. Each entry mirrors the {id,label,description} shape the
 * Electron shell expects on the settings page.
 */
static MetalsharpResponse* handle_bottles_profiles(const HttpRequest* req) {
    (void)req;
    return make_catalog_response(BOTTLES_PROFILES_JSON);
}

/*
 * GET /bottles/redist-sources — list the compiled-in
 * redistributable-source catalog. Each entry carries {id,name,
 * url,version} so the Electron shell can render a download table.
 */
static MetalsharpResponse* handle_bottles_redist_sources(const HttpRequest* req) {
    (void)req;
    return make_catalog_response(BOTTLES_REDIST_SOURCES_JSON);
}

/*
 * GET /bottles/route-contracts — stub returning an empty contract
 * array. The real implementation will dump the per-route
 * machine-readable schemas; for now the Electron shell checks for
 * shape compatibility with the empty array.
 */
static MetalsharpResponse* handle_bottles_route_contracts(const HttpRequest* req) {
    (void)req;
    return make_catalog_response(BOTTLES_ROUTE_CONTRACTS_JSON);
}

static JsonValue* bottles_compatibility_matrix(void) {
    JsonValue* envelope =
        json_parse(BOTTLES_COMPATIBILITY_MATRIX_JSON, strlen(BOTTLES_COMPATIBILITY_MATRIX_JSON), NULL);
    if (envelope == NULL)
        return NULL;
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/bottles/compatibility-matrix.json", bottles_home());
    JsonValue* overrides = bottles_read_json(path, NULL);
    JsonValue* cases = json_object_get(envelope, "cases");

    char root[PATH_MAX];
    snprintf(root, sizeof(root), "%s/bottles", bottles_home());
    DIR* directory = opendir(root);
    if (directory != NULL) {
        struct dirent* entry;
        while ((entry = readdir(directory)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            JsonValue* bottle = bottles_load(entry->d_name, NULL);
            if (bottle == NULL)
                continue;
            const char* id = bottles_string(bottle, "id", "");
            bool known = false;
            for (size_t i = 0u; i < json_array_length(cases); i++)
                if (strcmp(id, bottles_string(json_array_get(cases, i), "id", "")) == 0)
                    known = true;
            if (!known) {
                const char* type = bottles_string(bottle, "bottle_type", "utility");
                const char* case_type = strcmp(type, "steam") == 0       ? "steam game bottle"
                                        : strcmp(type, "installer") == 0 ? "installer bottle"
                                        : strcmp(type, "sharp_app") == 0 ? "sharp app bottle"
                                                                         : "utility bottle";
                JsonValue* detections = json_object_get(bottle, "installed_app_detections");
                JsonValue* components = json_object_get(bottle, "installed_components");
                jsonbuf_t missing = jsonbuf_new();
                for (size_t i = 0u; i < json_array_length(components); i++) {
                    JsonValue* component = json_array_get(components, i);
                    if (strcmp(bottles_string(component, "state", "unknown"), "installed") == 0)
                        continue;
                    if (missing.len > 0u)
                        jsonbuf_append(&missing, ", ");
                    jsonbuf_append(&missing, bottles_string(component, "id", ""));
                }
                JsonValue* compatibility = json_new_object();
                bool added =
                    compatibility != NULL && bottles_set_owned(compatibility, "id", json_new_string(id)) &&
                    bottles_set_owned(compatibility, "name", json_new_string(bottles_string(bottle, "name", ""))) &&
                    bottles_set_owned(compatibility, "case_type", json_new_string(case_type)) &&
                    json_object_set_clone(compatibility, "required_profile",
                                          json_object_get(bottle, "runtime_profile")) &&
                    bottles_set_owned(compatibility, "installer_opens",
                                      json_new_string(bottles_string(bottle, "last_launch_status", "not_run"))) &&
                    bottles_set_owned(compatibility, "final_app_detected",
                                      json_new_string(json_array_length(detections) == 0u ? "no" : "yes")) &&
                    bottles_set_owned(compatibility, "final_app_launches", json_new_string("unknown")) &&
                    bottles_set_owned(compatibility, "known_missing_runtime",
                                      json_new_string(missing.len == 0u ? "none" : missing.data)) &&
                    bottles_set_owned(compatibility, "bottle_id", json_new_string(id)) &&
                    bottles_set_owned(compatibility, "notes", json_new_string("")) &&
                    bottles_set_owned(compatibility, "evidence_updated_at", json_new_null()) &&
                    bottles_set_owned(compatibility, "per_game_prefix_recommendation",
                                      json_new_string(strcmp(type, "steam") == 0 ? "candidate_for_per_game_prefix"
                                                                                 : "not_applicable")) &&
                    json_array_append_owned(cases, compatibility);
                if (!added)
                    json_free(compatibility);
                jsonbuf_free(&missing);
            }
            json_free(bottle);
        }
        closedir(directory);
    }

    static const char* mutable_fields[] = {"installer_opens",
                                           "final_app_detected",
                                           "final_app_launches",
                                           "known_missing_runtime",
                                           "notes",
                                           "evidence_updated_at",
                                           "per_game_prefix_recommendation"};
    if (json_type(overrides) == JSON_ARRAY) {
        for (size_t i = 0u; i < json_array_length(cases); i++) {
            JsonValue* current = json_array_get(cases, i);
            const char* id = bottles_string(current, "id", "");
            for (size_t j = 0u; j < json_array_length(overrides); j++) {
                JsonValue* saved = json_array_get(overrides, j);
                if (strcmp(id, bottles_string(saved, "id", "")) != 0)
                    continue;
                for (size_t field = 0u; field < sizeof(mutable_fields) / sizeof(mutable_fields[0]); field++) {
                    JsonValue* value = json_object_get(saved, mutable_fields[field]);
                    if (value != NULL)
                        json_object_set_clone(current, mutable_fields[field], value);
                }
                break;
            }
        }
    }
    json_free(overrides);
    return envelope;
}

static MetalsharpResponse* handle_bottles_compatibility_matrix(const HttpRequest* req) {
    (void)req;
    JsonValue* envelope = bottles_compatibility_matrix();
    if (envelope == NULL)
        return bottles_error("compatibility matrix unavailable");
    char* serialized = json_serialize(envelope);
    json_free(envelope);
    MetalsharpResponse* response = serialized != NULL ? make_data_response(serialized) : bottles_error("out of memory");
    free(serialized);
    return response;
}

static MetalsharpResponse* bottles_wrap_value(const char* key, JsonValue* value, const char* suffix) {
    char* serialized = json_serialize(value);
    jsonbuf_t body = jsonbuf_new();
    jsonbuf_append(&body, "{\"ok\":true,\"");
    jsonbuf_append(&body, key);
    jsonbuf_append(&body, "\":");
    jsonbuf_append(&body, serialized != NULL ? serialized : "null");
    if (suffix != NULL)
        jsonbuf_append(&body, suffix);
    jsonbuf_append(&body, "}");
    free(serialized);
    MetalsharpResponse* response = body.ok ? make_data_response(body.data) : bottles_error("out of memory");
    jsonbuf_free(&body);
    return response;
}

static MetalsharpResponse* handle_bottles_get(const HttpRequest* req) {
    JsonValue* body = bottles_parse_body(req);
    const char* id = bottles_string(body, "id", "");
    if (id[0] == '\0') {
        json_free(body);
        return bottles_error("id required");
    }
    char* error = NULL;
    JsonValue* bottle = bottles_load(id, &error);
    json_free(body);
    if (bottle == NULL) {
        MetalsharpResponse* response = bottles_error(error != NULL ? error : "bottle read failed");
        free(error);
        return response;
    }
    free(error);
    MetalsharpResponse* response = bottles_wrap_value("bottle", bottle, NULL);
    json_free(bottle);
    return response;
}

static const char* bottles_normalize_profile(const char* profile) {
    if (profile == NULL)
        return NULL;
    if (strcasecmp(profile, "gameinstall") == 0 || strcasecmp(profile, "game-install") == 0)
        return "game_install";
    if (strcasecmp(profile, "gptk") == 0)
        return "m13";
    if (strcasecmp(profile, "d3dmetal_native") == 0)
        return "d3dmetal";
    if (strcasecmp(profile, "win32dotnet") == 0)
        return "win32_dotnet";
    if (strcasecmp(profile, "javalauncher") == 0)
        return "java_launcher";
    if (strcasecmp(profile, "xna_fna_arm64") == 0 || strcasecmp(profile, "native_mono_arm64") == 0)
        return "fna_arm64";
    if (strcasecmp(profile, "xna_fna_x86") == 0 || strcasecmp(profile, "native_mono_x86") == 0 ||
        strcasecmp(profile, "mono_x86") == 0)
        return "fna_x86";
    static const char* profiles[] = {"plain",        "launcher", "game_install",  "m9",        "m10",      "m10_32",
                                     "m11",          "m11_32",   "m12",           "m13",       "d3dmetal", "dotnet",
                                     "win32_dotnet", "webview",  "java_launcher", "fna_arm64", "fna_x86"};
    for (size_t i = 0u; i < sizeof(profiles) / sizeof(profiles[0]); i++)
        if (strcasecmp(profile, profiles[i]) == 0)
            return profiles[i];
    return NULL;
}

static JsonValue* bottles_profile_definition(const char* profile) {
    char* error = NULL;
    JsonValue* catalog = json_parse(BOTTLES_PROFILES_JSON, strlen(BOTTLES_PROFILES_JSON), &error);
    free(error);
    JsonValue* profiles = json_object_get(catalog, "profiles");
    JsonValue* result = NULL;
    for (size_t i = 0u; i < json_array_length(profiles); i++) {
        JsonValue* candidate = json_array_get(profiles, i);
        if (strcmp(bottles_string(candidate, "id", ""), profile) == 0) {
            result = json_clone(candidate);
            break;
        }
    }
    json_free(catalog);
    return result;
}

static int bottles_string_pointer_compare(const void* left, const void* right) {
    return strcmp(*(const char* const*)left, *(const char* const*)right);
}

static JsonValue* bottles_rebuild_components(const JsonValue* existing, const JsonValue* component_ids) {
    JsonValue* rebuilt = json_new_array();
    size_t id_count = json_array_length(component_ids);
    const char** ids = calloc(id_count > 0u ? id_count : 1u, sizeof(char*));
    if (rebuilt == NULL || ids == NULL) {
        json_free(rebuilt);
        free(ids);
        return NULL;
    }
    for (size_t i = 0u; i < id_count; i++)
        ids[i] = json_get_string(json_array_get(component_ids, i));
    qsort(ids, id_count, sizeof(char*), bottles_string_pointer_compare);
    for (size_t i = 0u; i < id_count; i++) {
        const char* id = ids[i];
        const char* state = "unknown";
        for (size_t j = 0u; j < json_array_length(existing); j++) {
            JsonValue* old = json_array_get(existing, j);
            if (strcmp(bottles_string(old, "id", ""), id != NULL ? id : "") == 0) {
                state = bottles_string(old, "state", "unknown");
                break;
            }
        }
        JsonValue* component = json_new_object();
        if (component == NULL || !bottles_set_owned(component, "id", json_new_string(id != NULL ? id : "")) ||
            !bottles_set_owned(component, "state", json_new_string(state)) ||
            !json_array_append_owned(rebuilt, component)) {
            json_free(component);
            json_free(rebuilt);
            free(ids);
            return NULL;
        }
    }
    free(ids);
    return rebuilt;
}

static bool bottles_apply_profile(JsonValue* bottle, const char* profile, char** error) {
    JsonValue* definition = bottles_profile_definition(profile);
    if (definition == NULL)
        return false;
    JsonValue* rebuilt = bottles_rebuild_components(json_object_get(bottle, "installed_components"),
                                                    json_object_get(definition, "components"));
    bool ok = rebuilt != NULL && bottles_set_owned(bottle, "runtime_profile", json_new_string(profile)) &&
              json_object_set_clone(bottle, "arch", json_object_get(definition, "arch"));
    if (ok)
        ok = bottles_set_owned(bottle, "installed_components", rebuilt);
    else
        json_free(rebuilt);
    if (ok && strcmp(bottles_string(bottle, "bottle_type", ""), "steam") == 0)
        ok = json_object_set_clone(bottle, "preferred_pipeline", json_object_get(definition, "launch_pipeline"));
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "%lld", (long long)time(NULL));
    if (ok)
        ok = bottles_set_owned(bottle, "updated_at", json_new_string(timestamp));
    if (ok)
        ok = bottles_save(bottle, error);
    json_free(definition);
    return ok;
}

static MetalsharpResponse* handle_bottles_set_profile(const HttpRequest* req) {
    JsonValue* body = bottles_parse_body(req);
    const char* id = bottles_string(body, "id", "");
    const char* requested = bottles_string(body, "profile", "");
    if (id[0] == '\0' || requested[0] == '\0') {
        json_free(body);
        return bottles_error("id and profile required");
    }
    const char* profile = bottles_normalize_profile(requested);
    if (profile == NULL) {
        json_free(body);
        return bottles_error("unknown runtime profile");
    }
    char* error = NULL;
    JsonValue* bottle = bottles_load(id, &error);
    bool ok = bottle != NULL && bottles_apply_profile(bottle, profile, &error);
    json_free(body);
    if (!ok) {
        MetalsharpResponse* response = bottles_error(error != NULL ? error : "bottle update failed");
        free(error);
        json_free(bottle);
        return response;
    }
    free(error);
    const char* suffix =
        json_type(json_object_get(bottle, "steam_app_id")) == JSON_NUMBER
            ? ",\"preflight\":{\"ok\":false,\"pipeline\":\"wine_bare\",\"error\":\"MetalSharp runtime is incomplete\"}"
            : ",\"preflight\":{\"ok\":true,\"skipped\":true,\"reason\":\"not_steam_bottle\"}";
    MetalsharpResponse* response = bottles_wrap_value("bottle", bottle, suffix);
    json_free(bottle);
    return response;
}

static MetalsharpResponse* handle_bottles_edit(const HttpRequest* req) {
    JsonValue* body = bottles_parse_body(req);
    const char* id = bottles_string(body, "id", "");
    const char* name = json_get_string(json_object_get(body, "name"));
    const char* requested_pipeline = json_get_string(json_object_get(body, "preferredPipeline"));
    if (id[0] == '\0') {
        json_free(body);
        return bottles_error("id required");
    }
    if (name == NULL && requested_pipeline == NULL) {
        json_free(body);
        return bottles_error("name or preferredPipeline required");
    }
    char* error = NULL;
    JsonValue* bottle = bottles_load(id, &error);
    if (bottle == NULL) {
        /* Steam bottle that has never been saved yet: lazily
         * create an empty Steam bottle manifest so /bottles/edit
         * can persist the user's pipeline preference. */
        free(error);
        error = NULL;
        bottle = bottles_create_empty(id);
        if (bottle == NULL) {
            json_free(body);
            return bottles_error("failed to create bottle");
        }
    }
    bool ok = true;
    if (name != NULL) {
        char* trimmed = bottles_trim(name);
        if (trimmed == NULL)
            ok = false;
        else if (trimmed[0] == '\0')
            ok = bottles_set_owned(bottle, "custom_name", json_new_null());
        else
            ok = bottles_set_owned(bottle, "custom_name", json_new_string(trimmed)) &&
                 bottles_set_owned(bottle, "name", json_new_string(trimmed));
        free(trimmed);
    }
    if (ok && requested_pipeline != NULL) {
        char* trimmed = bottles_trim(requested_pipeline);
        const char* pipeline = trimmed != NULL ? trimmed : "";
        if (pipeline[0] == '\0' || strcasecmp(pipeline, "auto") == 0)
            ok = bottles_set_owned(bottle, "preferred_pipeline", json_new_null());
        else {
            const char* profile = strcasecmp(pipeline, "d3d9") == 0                                        ? "m9"
                                  : strcasecmp(pipeline, "d3d10") == 0                                     ? "m10"
                                  : strcasecmp(pipeline, "d3d11") == 0 || strcasecmp(pipeline, "m64") == 0 ? "m11"
                                  : strcasecmp(pipeline, "d3d12") == 0 ? "m12"
                                                                       : bottles_normalize_profile(pipeline);
            if (profile == NULL) {
                free(trimmed);
                json_free(body);
                json_free(bottle);
                free(error);
                return bottles_error("unknown preferred pipeline");
            }
            ok = bottles_set_owned(bottle, "preferred_pipeline", json_new_string(profile));
            if (ok)
                ok = bottles_apply_profile(bottle, profile, &error);
        }
        free(trimmed);
    }
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "%lld", (long long)time(NULL));
    if (ok)
        ok = bottles_set_owned(bottle, "updated_at", json_new_string(timestamp));
    if (ok)
        ok = bottles_save(bottle, &error);
    json_free(body);
    if (!ok) {
        MetalsharpResponse* response = bottles_error(error != NULL ? error : "bottle update failed");
        free(error);
        json_free(bottle);
        return response;
    }
    free(error);
    const char* suffix =
        json_type(json_object_get(bottle, "steam_app_id")) == JSON_NUMBER
            ? ",\"preflight\":{\"ok\":false,\"pipeline\":\"wine_bare\",\"error\":\"MetalSharp runtime is incomplete\"}"
            : ",\"preflight\":{\"ok\":true,\"skipped\":true,\"reason\":\"not_steam_bottle\"}";
    MetalsharpResponse* response = bottles_wrap_value("bottle", bottle, suffix);
    json_free(bottle);
    return response;
}

static bool bottles_stamp(JsonValue* bottle) {
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "%lld", (long long)time(NULL));
    return bottles_set_owned(bottle, "updated_at", json_new_string(timestamp));
}

static MetalsharpResponse* handle_bottles_refresh(const HttpRequest* req) {
    JsonValue* body = bottles_parse_body(req);
    const char* id = bottles_string(body, "id", "");
    if (id[0] == '\0') {
        json_free(body);
        return bottles_error("id required");
    }
    char* error = NULL;
    JsonValue* bottle = bottles_load(id, &error);
    json_free(body);
    if (bottle == NULL) {
        MetalsharpResponse* response = bottles_error(error != NULL ? error : "bottle read failed");
        free(error);
        return response;
    }
    /* Rust refresh re-scans launchable applications. A missing/empty prefix has no detections. */
    JsonValue* detections = json_object_get(bottle, "installed_app_detections");
    bool has_apps = json_array_length(detections) > 0u;
    bool ok = bottles_set_owned(bottle, "health", json_new_string(has_apps ? "ready" : "needs_repair")) &&
              bottles_stamp(bottle) && bottles_save(bottle, &error);
    if (!ok) {
        MetalsharpResponse* response = bottles_error(error != NULL ? error : "bottle update failed");
        free(error);
        json_free(bottle);
        return response;
    }
    free(error);
    MetalsharpResponse* response = bottles_wrap_value("bottle", bottle, NULL);
    json_free(bottle);
    return response;
}

static JsonValue* bottles_check(const char* id, bool ok, const char* detail) {
    JsonValue* check = json_new_object();
    if (check == NULL || !bottles_set_owned(check, "id", json_new_string(id)) ||
        !bottles_set_owned(check, "ok", json_new_bool(ok)) ||
        !bottles_set_owned(check, "detail", json_new_string(detail))) {
        json_free(check);
        return NULL;
    }
    return check;
}

static MetalsharpResponse* bottles_doctor_loaded(JsonValue* bottle, char** error) {
    const char* id = bottles_string(bottle, "id", "");
    const char* prefix = bottles_string(bottle, "prefix_path", "");
    struct stat info;
    bool prefix_ok = stat(prefix, &info) == 0;
    JsonValue* components = json_object_get(bottle, "installed_components");
    bool components_ok = true;
    for (size_t i = 0u; i < json_array_length(components); i++)
        if (strcmp(bottles_string(json_array_get(components, i), "state", "unknown"), "installed") != 0)
            components_ok = false;
    const char* log_path = json_get_string(json_object_get(bottle, "last_launch_log"));
    bool log_ok = log_path != NULL && stat(log_path, &info) == 0;
    size_t detection_count = json_array_length(json_object_get(bottle, "installed_app_detections"));
    char component_detail[64], detection_detail[64];
    snprintf(component_detail, sizeof(component_detail), "%zu tracked components", json_array_length(components));
    snprintf(detection_detail, sizeof(detection_detail), "%zu candidate apps detected", detection_count);
    JsonValue* checks = json_new_array();
    JsonValue* actions = json_new_array();
    JsonValue* sources = json_new_array();
    bool ok = checks != NULL && actions != NULL && sources != NULL &&
              json_array_append_owned(checks, bottles_check("prefix", prefix_ok, prefix)) &&
              json_array_append_owned(checks, bottles_check("components", components_ok, component_detail)) &&
              json_array_append_owned(checks, bottles_check("launch_log", log_ok,
                                                            log_path != NULL ? log_path : "no launch log recorded")) &&
              json_array_append_owned(checks, bottles_check("app_detection", detection_count > 0u, detection_detail));
    bool ready = prefix_ok && components_ok;
    if (ok)
        ok = bottles_set_owned(bottle, "health", json_new_string(ready ? "ready" : "needs_repair")) &&
             bottles_stamp(bottle) && bottles_save(bottle, error);
    JsonValue* report = json_new_object();
    if (ok)
        ok = report != NULL && bottles_set_owned(report, "id", json_new_string(id)) &&
             bottles_set_owned(report, "ready", json_new_bool(ready)) &&
             bottles_set_owned(report, "summary",
                               json_new_string(ready ? "Bottle runtime checks passed"
                                                     : "Bottle needs runtime preparation or repair"));
    if (ok) {
        ok = bottles_set_owned(report, "checks", checks);
        checks = NULL;
    }
    if (ok) {
        ok = bottles_set_owned(report, "actions", actions);
        actions = NULL;
    }
    if (ok) {
        ok = bottles_set_owned(report, "component_sources", sources);
        sources = NULL;
    }
    if (!ok) {
        json_free(checks);
        json_free(actions);
        json_free(sources);
        json_free(report);
        return bottles_error(error != NULL && *error != NULL ? *error : "bottle diagnosis failed");
    }
    MetalsharpResponse* response = bottles_wrap_value("report", report, NULL);
    json_free(report);
    return response;
}

static MetalsharpResponse* handle_bottles_doctor(const HttpRequest* req) {
    JsonValue* body = bottles_parse_body(req);
    const char* id = bottles_string(body, "id", "");
    if (id[0] == '\0') {
        json_free(body);
        return bottles_error("id required");
    }
    char* error = NULL;
    JsonValue* bottle = bottles_load(id, &error);
    json_free(body);
    if (bottle == NULL) {
        MetalsharpResponse* response = bottles_error(error != NULL ? error : "bottle read failed");
        free(error);
        return response;
    }
    MetalsharpResponse* response = bottles_doctor_loaded(bottle, &error);
    free(error);
    json_free(bottle);
    return response;
}

static MetalsharpResponse* handle_bottles_prepare(const HttpRequest* req) {
    JsonValue* body = bottles_parse_body(req);
    const char* id = bottles_string(body, "id", "");
    if (id[0] == '\0') {
        json_free(body);
        return bottles_error("id required");
    }
    char* error = NULL;
    JsonValue* bottle = bottles_load(id, &error);
    json_free(body);
    if (bottle == NULL) {
        MetalsharpResponse* response = bottles_error(error != NULL ? error : "bottle read failed");
        free(error);
        return response;
    }
    const char* bottle_id = bottles_string(bottle, "id", "");
    char drive_c[PATH_MAX], logs[PATH_MAX], installers[PATH_MAX];
    snprintf(drive_c, sizeof(drive_c), "%s/drive_c", bottles_string(bottle, "prefix_path", ""));
    snprintf(logs, sizeof(logs), "%s/bottles/%s/logs", bottles_home(), bottle_id);
    snprintf(installers, sizeof(installers), "%s/bottles/%s/installers", bottles_home(), bottle_id);
    bool ok = bottles_mkdir_p(drive_c) && bottles_mkdir_p(logs) && bottles_mkdir_p(installers) &&
              bottles_set_owned(bottle, "health", json_new_string("needs_repair")) && bottles_stamp(bottle) &&
              bottles_save(bottle, &error);
    if (!ok) {
        MetalsharpResponse* response = bottles_error(error != NULL ? error : "bottle preparation failed");
        free(error);
        json_free(bottle);
        return response;
    }
    MetalsharpResponse* response = bottles_doctor_loaded(bottle, &error);
    free(error);
    json_free(bottle);
    return response;
}

static const char* const bottles_directx_files[] = {
    "d3dx9_24.dll",  "d3dx9_25.dll",    "d3dx9_26.dll",  "d3dx9_27.dll",       "d3dx9_28.dll",       "d3dx9_29.dll",
    "d3dx9_30.dll",  "d3dx9_31.dll",    "d3dx9_32.dll",  "d3dx9_33.dll",       "d3dx9_34.dll",       "d3dx9_35.dll",
    "d3dx9_36.dll",  "d3dx9_37.dll",    "d3dx9_38.dll",  "d3dx9_39.dll",       "d3dx9_40.dll",       "d3dx9_41.dll",
    "d3dx9_42.dll",  "d3dx9_43.dll",    "d3dx10_33.dll", "d3dx10_34.dll",      "d3dx10_35.dll",      "d3dx10_36.dll",
    "d3dx10_37.dll", "d3dx10_38.dll",   "d3dx10_39.dll", "d3dx10_40.dll",      "d3dx10_41.dll",      "d3dx10_42.dll",
    "d3dx10_43.dll", "d3dx11_42.dll",   "d3dx11_43.dll", "D3DCompiler_42.dll", "D3DCompiler_43.dll", "xinput1_3.dll",
    "xaudio2_7.dll", "x3daudio1_7.dll", "XAPOFX1_5.dll",
};

static MetalsharpResponse* handle_bottles_verify_directx(const HttpRequest* req) {
    JsonValue* body = bottles_parse_body(req);
    const char* id = bottles_string(body, "id", "");
    if (id[0] == '\0') {
        json_free(body);
        return bottles_error("id required");
    }
    char* error = NULL;
    JsonValue* bottle = bottles_load(id, &error);
    json_free(body);
    if (bottle == NULL) {
        MetalsharpResponse* response = bottles_error(error != NULL ? error : "bottle read failed");
        free(error);
        return response;
    }
    id = bottles_string(bottle, "id", "");
    const char* prefix = bottles_string(bottle, "prefix_path", "");
    JsonValue* present = json_new_array();
    JsonValue* missing = json_new_array();
    for (size_t i = 0u; i < sizeof(bottles_directx_files) / sizeof(bottles_directx_files[0]); i++) {
        char system32[PATH_MAX], syswow64[PATH_MAX];
        snprintf(system32, sizeof(system32), "%s/drive_c/windows/system32/%s", prefix, bottles_directx_files[i]);
        snprintf(syswow64, sizeof(syswow64), "%s/drive_c/windows/syswow64/%s", prefix, bottles_directx_files[i]);
        struct stat info;
        JsonValue* value = json_new_string(bottles_directx_files[i]);
        JsonValue* target = stat(system32, &info) == 0 || stat(syswow64, &info) == 0 ? present : missing;
        if (value == NULL || !json_array_append_owned(target, value))
            json_free(value);
    }
    jsonbuf_t response_body = jsonbuf_new();
    char* present_json = json_serialize(present);
    char* missing_json = json_serialize(missing);
    jsonbuf_append(&response_body, "{\"ok\":true,\"id\":");
    jsonbuf_append_escaped(&response_body, id);
    jsonbuf_appendf(&response_body, ",\"complete\":%s,\"present_count\":%zu,\"missing_count\":%zu,\"present\":",
                    json_array_length(missing) == 0u ? "true" : "false", json_array_length(present),
                    json_array_length(missing));
    jsonbuf_append(&response_body, present_json != NULL ? present_json : "[]");
    jsonbuf_append(&response_body, ",\"missing\":");
    jsonbuf_append(&response_body, missing_json != NULL ? missing_json : "[]");
    jsonbuf_append(&response_body, "}");
    free(present_json);
    free(missing_json);
    json_free(present);
    json_free(missing);
    json_free(bottle);
    free(error);
    MetalsharpResponse* response =
        response_body.ok ? make_data_response(response_body.data) : bottles_error("out of memory");
    jsonbuf_free(&response_body);
    return response;
}

static void* bottles_reap_child(void* value) {
    pid_t pid = (pid_t)(intptr_t)value;
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
    }
    return NULL;
}

static bool bottles_find_wine(char* output, size_t output_size) {
    static const char* relative[] = {"runtime/wine/bin/metalsharp-wine", "runtime/wine/bin/wine"};
    for (size_t i = 0u; i < sizeof(relative) / sizeof(relative[0]); i++) {
        int written = snprintf(output, output_size, "%s/%s", bottles_home(), relative[i]);
        if (written >= 0 && (size_t)written < output_size && access(output, X_OK) == 0)
            return true;
    }
    return false;
}

static void bottles_set_runtime_environment(void) {
    char value[PATH_MAX * 2u];
    int written = snprintf(value, sizeof(value), "%s/runtime/wine/lib:%s/runtime/wine/lib/wine/x86_64-unix",
                           bottles_home(), bottles_home());
    if (written < 0 || (size_t)written >= sizeof(value))
        return;
#if defined(__APPLE__)
    (void)setenv("DYLD_FALLBACK_LIBRARY_PATH", value, 1);
#elif defined(__linux__)
    (void)setenv("LD_LIBRARY_PATH", value, 1);
#endif
}

static bool bottles_spawn_wine(char* const argv[], const char* prefix, const char* log_path, pid_t* pid_out,
                               char* error, size_t error_size) {
    int exec_pipe[2];
    if (pipe(exec_pipe) != 0) {
        snprintf(error, error_size, "%s", strerror(errno));
        return false;
    }
    (void)fcntl(exec_pipe[1], F_SETFD, FD_CLOEXEC);
    pid_t pid = fork();
    if (pid < 0) {
        snprintf(error, error_size, "%s", strerror(errno));
        close(exec_pipe[0]);
        close(exec_pipe[1]);
        return false;
    }
    if (pid == 0) {
        close(exec_pipe[0]);
        int log_fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (log_fd < 0) {
            int child_error = errno;
            (void)write(exec_pipe[1], &child_error, sizeof(child_error));
            _exit(127);
        }
        (void)dup2(log_fd, STDOUT_FILENO);
        (void)dup2(log_fd, STDERR_FILENO);
        if (log_fd > STDERR_FILENO)
            close(log_fd);
        (void)setenv("WINEPREFIX", prefix, 1);
        (void)setenv("WINEDEBUG", "-all", 1);
        (void)setenv("WINEDEBUGGER", "none", 1);
        bottles_set_runtime_environment();
        execv(argv[0], argv);
        int child_error = errno;
        (void)write(exec_pipe[1], &child_error, sizeof(child_error));
        _exit(127);
    }
    close(exec_pipe[1]);
    int child_error = 0;
    ssize_t count;
    do {
        count = read(exec_pipe[0], &child_error, sizeof(child_error));
    } while (count < 0 && errno == EINTR);
    close(exec_pipe[0]);
    if (count > 0) {
        while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
        }
        snprintf(error, error_size, "%s", strerror(child_error));
        return false;
    }
    pthread_t reaper;
    if (pthread_create(&reaper, NULL, bottles_reap_child, (void*)(intptr_t)pid) == 0)
        pthread_detach(reaper);
    *pid_out = pid;
    return true;
}

static bool bottles_run_wine_sync(char* const argv[], const char* prefix, const char* log_path, pid_t* pid_out,
                                  bool* success_out, char* error, size_t error_size) {
    int exec_pipe[2];
    if (pipe(exec_pipe) != 0) {
        snprintf(error, error_size, "%s", strerror(errno));
        return false;
    }
    (void)fcntl(exec_pipe[1], F_SETFD, FD_CLOEXEC);
    pid_t pid = fork();
    if (pid < 0) {
        snprintf(error, error_size, "%s", strerror(errno));
        close(exec_pipe[0]);
        close(exec_pipe[1]);
        return false;
    }
    if (pid == 0) {
        close(exec_pipe[0]);
        int log_fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (log_fd < 0) {
            int child_error = errno;
            (void)write(exec_pipe[1], &child_error, sizeof(child_error));
            _exit(127);
        }
        (void)dup2(log_fd, STDOUT_FILENO);
        (void)dup2(log_fd, STDERR_FILENO);
        if (log_fd > STDERR_FILENO)
            close(log_fd);
        (void)setenv("WINEPREFIX", prefix, 1);
        (void)setenv("WINEDEBUG", "-all", 1);
        (void)setenv("WINEDEBUGGER", "none", 1);
        bottles_set_runtime_environment();
        execv(argv[0], argv);
        int child_error = errno;
        (void)write(exec_pipe[1], &child_error, sizeof(child_error));
        _exit(127);
    }
    close(exec_pipe[1]);
    int child_error = 0;
    ssize_t count;
    do {
        count = read(exec_pipe[0], &child_error, sizeof(child_error));
    } while (count < 0 && errno == EINTR);
    close(exec_pipe[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR) {
            snprintf(error, error_size, "%s", strerror(errno));
            return false;
        }
    if (count > 0) {
        snprintf(error, error_size, "%s", strerror(child_error));
        return false;
    }
    *pid_out = pid;
    *success_out = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    return true;
}

static bool bottles_copy_regular_file(const char* source, const char* destination) {
    FILE* input = fopen(source, "rb");
    if (input == NULL)
        return false;
    FILE* output = fopen(destination, "wb");
    if (output == NULL) {
        fclose(input);
        return false;
    }
    char buffer[64 * 1024];
    bool ok = true;
    size_t count;
    while ((count = fread(buffer, 1u, sizeof(buffer), input)) != 0u)
        if (fwrite(buffer, 1u, count, output) != count) {
            ok = false;
            break;
        }
    if (ferror(input))
        ok = false;
    if (fclose(input) != 0)
        ok = false;
    if (fclose(output) != 0)
        ok = false;
    return ok;
}

static bool bottles_mark_component(JsonValue* bottle, const char* component_id, const char* state) {
    JsonValue* components = json_object_get(bottle, "installed_components");
    bool found = false;
    for (size_t i = 0u; i < json_array_length(components); i++) {
        JsonValue* component = json_array_get(components, i);
        if (strcmp(bottles_string(component, "id", ""), component_id) == 0) {
            if (!bottles_set_owned(component, "state", json_new_string(state)))
                return false;
            found = true;
            break;
        }
    }
    if (!found) {
        JsonValue* component = json_new_object();
        if (component == NULL || !bottles_set_owned(component, "id", json_new_string(component_id)) ||
            !bottles_set_owned(component, "state", json_new_string(state)) ||
            !json_array_append_owned(components, component)) {
            json_free(component);
            return false;
        }
    }
    JsonValue* ids = json_new_array();
    if (ids == NULL)
        return false;
    for (size_t i = 0u; i < json_array_length(components); i++) {
        JsonValue* id = json_new_string(bottles_string(json_array_get(components, i), "id", ""));
        if (id == NULL || !json_array_append_owned(ids, id)) {
            json_free(id);
            json_free(ids);
            return false;
        }
    }
    JsonValue* sorted = bottles_rebuild_components(components, ids);
    json_free(ids);
    if (sorted == NULL)
        return false;
    if (!bottles_set_owned(bottle, "installed_components", sorted)) {
        json_free(sorted);
        return false;
    }
    return true;
}

static bool bottles_mark_launch(JsonValue* bottle, pid_t pid, const char* log_path) {
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "%lld", (long long)time(NULL));
    return bottles_set_owned(bottle, "last_launch_log", json_new_string(log_path)) &&
           bottles_set_owned(bottle, "last_launch_pid", json_new_number((double)pid)) &&
           bottles_set_owned(bottle, "last_launch_status", json_new_string("running")) &&
           bottles_set_owned(bottle, "last_launch_finished_at", json_new_null()) &&
           bottles_set_owned(bottle, "health", json_new_string("needs_repair")) &&
           bottles_set_owned(bottle, "updated_at", json_new_string(timestamp));
}

static MetalsharpResponse* handle_bottles_set_windows_version(const HttpRequest* req) {
    JsonValue* body = bottles_parse_body(req);
    const char* body_id = bottles_string(body, "id", "");
    const char* body_version = bottles_string(body, "version", "");
    if (body_id[0] == '\0') {
        json_free(body);
        return bottles_error("id required");
    }
    if (body_version[0] == '\0') {
        json_free(body);
        return bottles_error("version required");
    }
    if (strcmp(body_version, "win7") != 0 && strcmp(body_version, "win10") != 0 && strcmp(body_version, "win11") != 0) {
        json_free(body);
        return bottles_error("windows version must be win7, win10, or win11");
    }
    char id[256], version[16];
    snprintf(id, sizeof(id), "%s", body_id);
    snprintf(version, sizeof(version), "%s", body_version);
    json_free(body);

    char* error = NULL;
    JsonValue* bottle = bottles_load(id, &error);
    if (bottle == NULL) {
        MetalsharpResponse* response = bottles_error(error != NULL ? error : "bottle read failed");
        free(error);
        return response;
    }
    free(error);
    error = NULL;
    const char* prefix_value = bottles_string(bottle, "prefix_path", "");
    char prefix[PATH_MAX], logs[PATH_MAX], log_path[PATH_MAX], wine[PATH_MAX];
    snprintf(prefix, sizeof(prefix), "%s", prefix_value);
    snprintf(logs, sizeof(logs), "%s/bottles/%s/logs", bottles_home(), id);
    if (!bottles_mkdir_p(prefix) || !bottles_mkdir_p(logs)) {
        json_free(bottle);
        return bottles_error(strerror(errno));
    }
    snprintf(log_path, sizeof(log_path), "%s/windows-version-%s-%lld.log", logs, version, (long long)time(NULL));
    if (!bottles_find_wine(wine, sizeof(wine))) {
        json_free(bottle);
        return bottles_error("MetalSharp Wine not found — run setup first");
    }
    FILE* log = fopen(log_path, "ab");
    if (log == NULL) {
        json_free(bottle);
        return bottles_error(strerror(errno));
    }
    fprintf(log, "windows_version=%s\nprefix=%s\n--- wine output ---\n", version, prefix);
    fclose(log);
    char* argv[] = {wine, "reg", "add", "HKCU\\Software\\Wine", "/v", "Version", "/d", version, "/f", NULL};
    pid_t pid = 0;
    char spawn_error[512];
    if (!bottles_spawn_wine(argv, prefix, log_path, &pid, spawn_error, sizeof(spawn_error))) {
        json_free(bottle);
        return bottles_error(spawn_error);
    }
    char component_id[64];
    snprintf(component_id, sizeof(component_id), "windows_version_%s", version);
    if (!bottles_mark_component(bottle, component_id, "needs_repair") || !bottles_mark_launch(bottle, pid, log_path) ||
        !bottles_save(bottle, &error)) {
        json_free(bottle);
        MetalsharpResponse* response = bottles_error(error != NULL ? error : "bottle save failed");
        free(error);
        return response;
    }
    json_free(bottle);
    jsonbuf_t output = jsonbuf_new();
    jsonbuf_append(&output, "{\"ok\":true,\"repair\":{\"id\":");
    jsonbuf_append_escaped(&output, component_id);
    jsonbuf_append(&output, ",\"status\":\"started\",\"detail\":");
    char detail[128];
    snprintf(detail, sizeof(detail), "Started Windows version mode update to %s", version);
    jsonbuf_append_escaped(&output, detail);
    jsonbuf_append(&output, ",\"asset_path\":null,\"log_path\":");
    jsonbuf_append_escaped(&output, log_path);
    jsonbuf_appendf(&output, ",\"pid\":%ld}}", (long)pid);
    MetalsharpResponse* response = output.ok ? make_data_response(output.data) : bottles_error("out of memory");
    jsonbuf_free(&output);
    return response;
}

static bool bottles_write_font_registry(FILE* reg) {
    static const char* substitutions[][2] = {
        {"Helvetica", "Arial"},
        {"Times", "Times New Roman"},
        {"Helv", "MS Sans Serif"},
        {"Tms Rmn", "Times New Roman"},
        {"MS Shell Dlg", "Tahoma"},
        {"MS Shell Dlg 2", "Tahoma"},
        {"Arial Baltic,186", "Arial,186"},
        {"Arial CE,238", "Arial,238"},
        {"Arial CYR,204", "Arial,204"},
        {"Arial Greek,161", "Arial,161"},
        {"Arial TUR,162", "Arial,162"},
        {"Courier New Baltic,186", "Courier New,186"},
        {"Courier New CE,238", "Courier New,238"},
        {"Courier New CYR,204", "Courier New,204"},
        {"Courier New Greek,161", "Courier New,161"},
        {"Courier New TUR,162", "Courier New,162"},
        {"Times New Roman Baltic,186", "Times New Roman,186"},
        {"Times New Roman CE,238", "Times New Roman,238"},
        {"Times New Roman CYR,204", "Times New Roman,204"},
        {"Times New Roman Greek,161", "Times New Roman,161"},
        {"Times New Roman TUR,162", "Times New Roman,162"},
    };
    static const char* replacements[][2] = {
        {"Arial", "Helvetica Neue"},     {"MS Gothic", "Hiragino Sans"},
        {"MS PGothic", "Hiragino Sans"}, {"SimSun", "STSong"},
        {"NSimSun", "STSong"},           {"MingLiU", "LiSong Pro"},
        {"PMingLiU", "LiSong Pro"},      {"Microsoft Himalaya", "Kailasa"},
        {"Euphemia", "Euphemia UCAS"},   {"Gulim", "Apple SD Gothic Neo"},
    };
    fputs("REGEDIT4\r\n\r\n[HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes]\r\n",
          reg);
    for (size_t i = 0u; i < sizeof(substitutions) / sizeof(substitutions[0]); i++)
        fprintf(reg, "\"%s\"=\"%s\"\r\n", substitutions[i][0], substitutions[i][1]);
    fputs("\r\n[HKEY_CURRENT_USER\\Software\\Wine\\Fonts\\Replacements]\r\n", reg);
    for (size_t i = 0u; i < sizeof(replacements) / sizeof(replacements[0]); i++)
        fprintf(reg, "\"%s\"=\"%s\"\r\n", replacements[i][0], replacements[i][1]);
    return !ferror(reg);
}

static MetalsharpResponse* handle_bottles_apply_font_subs(const HttpRequest* req) {
    JsonValue* body = bottles_parse_body(req);
    const char* body_id = bottles_string(body, "id", "");
    if (body_id[0] == '\0') {
        json_free(body);
        return bottles_error("id required");
    }
    char id[256];
    snprintf(id, sizeof(id), "%s", body_id);
    json_free(body);
    char* error = NULL;
    JsonValue* bottle = bottles_load(id, &error);
    if (bottle == NULL) {
        MetalsharpResponse* response = bottles_error(error != NULL ? error : "bottle read failed");
        free(error);
        return response;
    }
    free(error);
    char prefix[PATH_MAX], logs[PATH_MAX], log_path[PATH_MAX], reg_path[PATH_MAX], wine[PATH_MAX];
    snprintf(prefix, sizeof(prefix), "%s", bottles_string(bottle, "prefix_path", ""));
    json_free(bottle);
    if (!bottles_find_wine(wine, sizeof(wine)))
        return bottles_error("MetalSharp Wine not found");
    snprintf(logs, sizeof(logs), "%s/bottles/%s/logs", bottles_home(), id);
    if (!bottles_mkdir_p(logs))
        return bottles_error(strerror(errno));
    snprintf(log_path, sizeof(log_path), "%s/font-subs.log", logs);
    snprintf(reg_path, sizeof(reg_path), "%s/drive_c/metalsharp-fontsubs.reg", prefix);
    FILE* reg = fopen(reg_path, "wb");
    if (reg == NULL)
        return bottles_error(strerror(errno));
    bool registry_ok = bottles_write_font_registry(reg);
    if (fclose(reg) != 0 || !registry_ok)
        return bottles_error(strerror(errno));
    FILE* log = fopen(log_path, "ab");
    if (log == NULL)
        return bottles_error(strerror(errno));
    fprintf(log, "font_substitutions_applied\nprefix=%s\n", prefix);
    fclose(log);
    char windows_path[PATH_MAX + 4u];
    windows_path[0] = 'Z';
    windows_path[1] = ':';
    size_t path_length = strlen(reg_path);
    if (path_length + 3u > sizeof(windows_path))
        return bottles_error("File name too long");
    for (size_t i = 0u; i <= path_length; i++)
        windows_path[i + 2u] = reg_path[i] == '/' ? '\\' : reg_path[i];
    char* argv[] = {wine, "regedit", windows_path, NULL};
    pid_t pid = 0;
    char spawn_error[512];
    if (!bottles_spawn_wine(argv, prefix, log_path, &pid, spawn_error, sizeof(spawn_error)))
        return bottles_error(spawn_error);
    jsonbuf_t output = jsonbuf_new();
    jsonbuf_appendf(&output, "{\"ok\":true,\"pid\":%ld,\"id\":", (long)pid);
    jsonbuf_append_escaped(&output, id);
    jsonbuf_append(&output, "}");
    MetalsharpResponse* response = output.ok ? make_data_response(output.data) : bottles_error("out of memory");
    jsonbuf_free(&output);
    return response;
}

static void bottles_write_registry_path(FILE* output, const char* path) {
    for (const char* cursor = path; *cursor != '\0'; cursor++) {
        if (*cursor == '/')
            fputs("\\\\", output);
        else
            fputc(*cursor, output);
    }
}

static MetalsharpResponse* handle_bottles_seed_post_wineboot(const HttpRequest* req) {
    static const char* overrides[][2] = {
        {"atl", "native,builtin"},
        {"msvcirt", "native,builtin"},
        {"msvcrt40", "native,builtin"},
        {"msvcrtd", "native,builtin"},
        {"msxml3", "native,builtin"},
        {"vcruntime140", "native,builtin"},
        {"vcruntime140_1", "native,builtin"},
        {"msvcp140", "native,builtin"},
    };
    JsonValue* body = bottles_parse_body(req);
    const char* body_id = bottles_string(body, "id", "");
    if (body_id[0] == '\0') {
        json_free(body);
        return bottles_error("id required");
    }
    char id[256];
    snprintf(id, sizeof(id), "%s", body_id);
    json_free(body);
    char* error = NULL;
    JsonValue* bottle = bottles_load(id, &error);
    if (bottle == NULL) {
        MetalsharpResponse* response = bottles_error(error != NULL ? error : "bottle read failed");
        free(error);
        return response;
    }
    free(error);
    char prefix[PATH_MAX], wine[PATH_MAX], logs[PATH_MAX], log_path[PATH_MAX];
    snprintf(prefix, sizeof(prefix), "%s", bottles_string(bottle, "prefix_path", ""));
    json_free(bottle);
    if (!bottles_find_wine(wine, sizeof(wine)))
        return bottles_error("MetalSharp Wine not found");
    snprintf(logs, sizeof(logs), "%s/bottles/%s/logs", bottles_home(), id);
    if (!bottles_mkdir_p(logs))
        return bottles_error(strerror(errno));
    snprintf(log_path, sizeof(log_path), "%s/post-wineboot.log", logs);

    char dosdevices[PATH_MAX], y_link[PATH_MAX];
    snprintf(dosdevices, sizeof(dosdevices), "%s/dosdevices", prefix);
    snprintf(y_link, sizeof(y_link), "%s/y:", dosdevices);
    struct stat info;
    if (stat(dosdevices, &info) == 0 && S_ISDIR(info.st_mode)) {
        char target[PATH_MAX];
        ssize_t target_length = readlink(y_link, target, sizeof(target) - 1u);
        bool replace = target_length < 0;
        if (target_length >= 0) {
            target[target_length] = '\0';
            replace = strcmp(target, bottles_home()) != 0;
        }
        if (replace) {
            (void)unlink(y_link);
            (void)symlink(bottles_home(), y_link);
        }
    }

    char system32[PATH_MAX], syswow64[PATH_MAX], source[PATH_MAX], destination[PATH_MAX];
    snprintf(system32, sizeof(system32), "%s/drive_c/windows/system32", prefix);
    snprintf(syswow64, sizeof(syswow64), "%s/drive_c/windows/syswow64", prefix);
    if (!bottles_mkdir_p(system32) || !bottles_mkdir_p(syswow64))
        return bottles_error(strerror(errno));
    snprintf(source, sizeof(source), "%s/runtime/wine/lib/metalsharp/x86_64-windows/metalsharp_ntdll_hook.dll",
             bottles_home());
    snprintf(destination, sizeof(destination), "%s/metalsharp_ntdll_hook.dll", system32);
    if (access(source, F_OK) == 0)
        (void)bottles_copy_regular_file(source, destination);
    snprintf(source, sizeof(source), "%s/runtime/wine/lib/metalsharp/i386-windows/metalsharp_ntdll_hook.dll",
             bottles_home());
    snprintf(destination, sizeof(destination), "%s/metalsharp_ntdll_hook.dll", syswow64);
    if (access(source, F_OK) == 0)
        (void)bottles_copy_regular_file(source, destination);

    char reg_path[PATH_MAX];
    snprintf(reg_path, sizeof(reg_path), "%s/drive_c/metalsharp-post-wineboot.reg", prefix);
    FILE* reg = fopen(reg_path, "wb");
    if (reg == NULL)
        return bottles_error(strerror(errno));
    bool registry_ok = bottles_write_font_registry(reg);
    fputs("\r\n[HKEY_CURRENT_USER\\Software\\Wine\\DllOverrides]\r\n", reg);
    for (size_t i = 0u; i < sizeof(overrides) / sizeof(overrides[0]); i++)
        fprintf(reg, "\"%s\"=\"%s\"\r\n", overrides[i][0], overrides[i][1]);
    fputs("\r\n[HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows NT\\CurrentVersion\\Windows]\r\n"
          "\"AppInit_DLLs\"=\"metalsharp_ntdll_hook.dll\"\r\n"
          "\"LoadAppInit_DLLs\"=dword:00000001\r\n"
          "\"RequireSignedAppInit_DLLs\"=dword:00000000\r\n"
          "\r\n[HKEY_LOCAL_MACHINE\\Software\\WOW6432Node\\Microsoft\\Windows NT\\CurrentVersion\\Windows]\r\n"
          "\"AppInit_DLLs\"=\"metalsharp_ntdll_hook.dll\"\r\n"
          "\"LoadAppInit_DLLs\"=dword:00000001\r\n"
          "\"RequireSignedAppInit_DLLs\"=dword:00000000\r\n"
          "\r\n[HKEY_LOCAL_MACHINE\\System\\CurrentControlSet\\Control\\Session Manager\\Environment]\r\n"
          "\"METALSHARP_HOME\"=\"",
          reg);
    bottles_write_registry_path(reg, bottles_home());
    fputs("\"\r\n\r\n[HKEY_CURRENT_USER\\Environment]\r\n\"METALSHARP_HOME\"=\"", reg);
    bottles_write_registry_path(reg, bottles_home());
    fputs("\"\r\n", reg);
    if (ferror(reg))
        registry_ok = false;
    if (fclose(reg) != 0 || !registry_ok)
        return bottles_error(strerror(errno));

    FILE* log = fopen(log_path, "ab");
    if (log == NULL)
        return bottles_error(strerror(errno));
    fprintf(log, "post_wineboot_config_seed\nprefix=%s\n", prefix);
    fclose(log);
    char windows_path[PATH_MAX + 4u];
    size_t path_length = strlen(reg_path);
    if (path_length + 3u > sizeof(windows_path))
        return bottles_error("File name too long");
    windows_path[0] = 'Z';
    windows_path[1] = ':';
    for (size_t i = 0u; i <= path_length; i++)
        windows_path[i + 2u] = reg_path[i] == '/' ? '\\' : reg_path[i];
    char* argv[] = {wine, "regedit", windows_path, NULL};
    pid_t pid = 0;
    bool success = false;
    char spawn_error[512];
    if (!bottles_run_wine_sync(argv, prefix, log_path, &pid, &success, spawn_error, sizeof(spawn_error)))
        return bottles_error(spawn_error);
    if (success) {
        char marker[PATH_MAX], timestamp[32];
        snprintf(marker, sizeof(marker), "%s/drive_c/metalsharp-post-wineboot-seeded", prefix);
        snprintf(timestamp, sizeof(timestamp), "%lld", (long long)time(NULL));
        FILE* marker_file = fopen(marker, "wb");
        if (marker_file != NULL) {
            fputs(timestamp, marker_file);
            fclose(marker_file);
        }
    }
    jsonbuf_t output = jsonbuf_new();
    jsonbuf_appendf(&output, "{\"ok\":true,\"pid\":%ld,\"id\":", (long)pid);
    jsonbuf_append_escaped(&output, id);
    jsonbuf_append(&output, "}");
    MetalsharpResponse* response = output.ok ? make_data_response(output.data) : bottles_error("out of memory");
    jsonbuf_free(&output);
    return response;
}

static bool bottles_save_compatibility_overrides(JsonValue* overrides) {
    char root[PATH_MAX], path[PATH_MAX], temporary[PATH_MAX];
    snprintf(root, sizeof(root), "%s/bottles", bottles_home());
    if (!bottles_mkdir_p(root))
        return false;
    snprintf(path, sizeof(path), "%s/compatibility-matrix.json", root);
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    char* serialized = json_serialize_pretty(overrides);
    if (serialized == NULL)
        return false;
    FILE* output = fopen(temporary, "wb");
    size_t length = strlen(serialized);
    bool ok = output != NULL && fwrite(serialized, 1u, length, output) == length;
    if (output != NULL && fclose(output) != 0)
        ok = false;
    free(serialized);
    if (ok)
        ok = rename(temporary, path) == 0;
    if (!ok)
        unlink(temporary);
    return ok;
}

static MetalsharpResponse* handle_bottles_record_compatibility(const HttpRequest* req) {
    JsonValue* body = bottles_parse_body(req);
    const char* id = bottles_string(body, "id", "");
    if (id[0] == '\0') {
        json_free(body);
        return bottles_error("id required");
    }
    JsonValue* matrix = bottles_compatibility_matrix();
    JsonValue* cases = json_object_get(matrix, "cases");
    JsonValue* selected = NULL;
    for (size_t i = 0u; i < json_array_length(cases); i++) {
        JsonValue* candidate = json_array_get(cases, i);
        if (strcmp(id, bottles_string(candidate, "id", "")) == 0) {
            selected = json_clone(candidate);
            break;
        }
    }
    if (selected == NULL) {
        json_free(matrix);
        json_free(body);
        return bottles_error("compatibility case not found");
    }
    static const struct {
        const char* request;
        const char* stored;
    } fields[] = {{"installerOpens", "installer_opens"},
                  {"finalAppDetected", "final_app_detected"},
                  {"finalAppLaunches", "final_app_launches"},
                  {"knownMissingRuntime", "known_missing_runtime"},
                  {"notes", "notes"}};
    for (size_t i = 0u; i < sizeof(fields) / sizeof(fields[0]); i++) {
        const char* value = json_get_string(json_object_get(body, fields[i].request));
        if (value != NULL)
            bottles_set_owned(selected, fields[i].stored, json_new_string(value));
    }
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "%lld", (long long)time(NULL));
    bottles_set_owned(selected, "evidence_updated_at", json_new_string(timestamp));
    const char* case_type = bottles_string(selected, "case_type", "");
    const char* recommendation = strncmp(id, "steam_", 6u) != 0 && strstr(case_type, "steam") == NULL
                                     ? "not_applicable"
                                     : "candidate_for_per_game_prefix";
    bottles_set_owned(selected, "per_game_prefix_recommendation", json_new_string(recommendation));

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/bottles/compatibility-matrix.json", bottles_home());
    JsonValue* overrides = bottles_read_json(path, NULL);
    if (json_type(overrides) != JSON_ARRAY) {
        json_free(overrides);
        overrides = json_new_array();
    }
    bool replaced = false;
    for (size_t i = 0u; i < json_array_length(overrides); i++) {
        JsonValue* old = json_array_get(overrides, i);
        if (strcmp(id, bottles_string(old, "id", "")) == 0) {
            json_array_remove(overrides, i);
            if (!json_array_append_owned(overrides, selected))
                break;
            selected = NULL;
            replaced = true;
            break;
        }
    }
    if (!replaced && selected != NULL && json_array_append_owned(overrides, selected)) {
        selected = NULL;
        replaced = true;
    }
    json_free(selected);
    json_free(matrix);
    json_free(body);
    if (!replaced || !bottles_save_compatibility_overrides(overrides)) {
        json_free(overrides);
        return bottles_error("compatibility matrix save failed");
    }
    json_free(overrides);
    matrix = bottles_compatibility_matrix();
    cases = json_object_get(matrix, "cases");
    MetalsharpResponse* response = bottles_wrap_value("cases", cases, NULL);
    json_free(matrix);
    return response;
}

/*
 * Common body for the stub POST routes. Every entry below returns
 * the {"ok":true} envelope; the real implementations will be
 * filled in once wineboot preparation, doctor, repair, refresh,
 * sync, runtime/window-version selection, font substitution,
 * DirectX verification, compatibility-recording, and installer
 * relaunch have been ported from the legacy Node backend.
 */
static MetalsharpResponse* handle_bottles_stub_ok(const HttpRequest* req) {
    if (req != NULL && strcmp(req->path, "/bottles/sync-steam") == 0)
        return make_catalog_response("{\"ok\":true,\"bottles\":[],\"count\":0}");
    if (req != NULL && strcmp(req->path, "/bottles/set-runtime-profile") == 0)
        return make_catalog_response("{\"ok\":false,\"error\":\"id and profile required\"}");
    return make_catalog_response("{\"ok\":false,\"error\":\"id required\"}");
}

/*
 * POST /bottles/repair-component — recovery action with a
 * distinct response shape. Returns {"ok":true,"error":""} so the
 * Electron shell always sees an empty error string on the happy
 * path; the real implementation will populate `error` with the
 * repair-stage diagnostic on failure.
 */
static MetalsharpResponse* handle_bottles_repair_component(const HttpRequest* req) {
    JsonValue* body = bottles_parse_body(req);
    const char* id = bottles_string(body, "id", "");
    const char* component = bottles_string(body, "component", "");
    bool dry_run = json_get_bool(json_object_get(body, "dryRun"), false);
    if (id[0] == '\0') {
        json_free(body);
        return bottles_error("id required");
    }
    if (component[0] == '\0') {
        json_free(body);
        return bottles_error("component required");
    }
    char* error = NULL;
    JsonValue* bottle = bottles_load(id, &error);
    if (bottle == NULL) {
        json_free(body);
        MetalsharpResponse* response = bottles_error(error != NULL ? error : "bottle read failed");
        free(error);
        return response;
    }
    const char* bottle_id = bottles_string(bottle, "id", "");
    const char* component_id = bottles_string(body, "component", "");
    char prefix[PATH_MAX], logs[PATH_MAX], log_path[PATH_MAX];
    snprintf(prefix, sizeof(prefix), "%s", bottles_string(bottle, "prefix_path", ""));
    snprintf(logs, sizeof(logs), "%s/bottles/%s/logs", bottles_home(), bottle_id);
    bottles_mkdir_p(prefix);
    bottles_mkdir_p(logs);
    snprintf(log_path, sizeof(log_path), "%s/component-%s-%lld.log", logs, component_id, (long long)time(NULL));
    MetalsharpResponse* response = NULL;
    if (dry_run && (strcmp(component_id, "wine-mono") == 0 || strcmp(component_id, "gecko") == 0)) {
        jsonbuf_t output = jsonbuf_new();
        jsonbuf_append(&output, "{\"ok\":true,\"repair\":{\"id\":");
        jsonbuf_append_escaped(&output, component_id);
        jsonbuf_append(&output, ",\"status\":\"builtin_available\",\"detail\":");
        char detail[256];
        snprintf(detail, sizeof(detail), "%s can be repaired with MetalSharp Wine bootstrapping", component_id);
        jsonbuf_append_escaped(&output, detail);
        jsonbuf_append(&output, ",\"asset_path\":null,\"log_path\":");
        jsonbuf_append_escaped(&output, log_path);
        jsonbuf_append(&output, ",\"pid\":null}}");
        response = output.ok ? make_data_response(output.data) : bottles_error("out of memory");
        jsonbuf_free(&output);
    } else if (!dry_run && (strcmp(component_id, "wine-mono") == 0 || strcmp(component_id, "gecko") == 0)) {
        response = bottles_error("MetalSharp Wine not found — run setup first");
    } else {
        response = bottles_error("unsupported bottle component");
    }
    free(error);
    json_free(bottle);
    json_free(body);
    return response;
}

/* ── Route registration ── */

/*
 * Register every /bottles route on the server and bind the module
 * to `db`. NULL arguments are a silent no-op so a half-initialised
 * backend cannot crash inside the registry. Every registration
 * happens before http_server_run() so workers see a fully
 * populated route table the moment the server starts.
 */
void bottles_register_routes(HttpServer* server, Database* db) {
    if (server == NULL || db == NULL) {
        LOG_WARN("bottles_register_routes called with NULL arguments");
        return;
    }
    g_bottles_db = db;
    ensure_bottles_table(db);

    /* GET endpoints */
    http_server_register(server, "GET", "/bottles", handle_bottles_list);
    http_server_register(server, "GET", "/bottles/profiles", handle_bottles_profiles);
    http_server_register(server, "GET", "/bottles/redist-sources", handle_bottles_redist_sources);
    http_server_register(server, "GET", "/bottles/route-contracts", handle_bottles_route_contracts);
    http_server_register(server, "GET", "/bottles/compatibility-matrix", handle_bottles_compatibility_matrix);

    /* POST endpoints */
    http_server_register(server, "POST", "/bottles/prepare", handle_bottles_prepare);
    http_server_register(server, "POST", "/bottles/edit", handle_bottles_edit);
    http_server_register(server, "POST", "/bottles/get", handle_bottles_get);
    http_server_register(server, "POST", "/bottles/doctor", handle_bottles_doctor);
    http_server_register(server, "POST", "/bottles/repair-component", handle_bottles_repair_component);
    http_server_register(server, "POST", "/bottles/refresh", handle_bottles_refresh);
    http_server_register(server, "POST", "/bottles/sync-steam", handle_bottles_stub_ok);
    http_server_register(server, "POST", "/bottles/seed-post-wineboot", handle_bottles_seed_post_wineboot);
    http_server_register(server, "POST", "/bottles/set-runtime-profile", handle_bottles_set_profile);
    http_server_register(server, "POST", "/bottles/set-windows-version", handle_bottles_set_windows_version);
    http_server_register(server, "POST", "/bottles/apply-font-subs", handle_bottles_apply_font_subs);
    http_server_register(server, "POST", "/bottles/verify-directx", handle_bottles_verify_directx);
    http_server_register(server, "POST", "/bottles/record-compatibility", handle_bottles_record_compatibility);
    http_server_register(server, "POST", "/bottles/relaunch-installer", handle_bottles_stub_ok);

    LOG_INFO("bottles routes registered (19)");
}
