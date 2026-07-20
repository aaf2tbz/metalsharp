/*
 * sharp_library.c — Game library + GOG integration module
 *
 * WHAT
 *   Implements the HTTP routes mounted under /sharp-library and
 *   /sharp-library/gog in the backend contract. Persists imported
 *   apps in SQLite, exposes the registered-apps listing read from
 *   that table, and stubs every install / launch / uninstall,
 *   cover, engine, launch-args, doctor, import-bottle-app, and
 *   GOG auth / games / install / play / progress / stop /
 *   uninstall / logout / sync / remove-prefix endpoint behind a
 *   stable JSON acknowledgement. The real launch / install / GOG
 *   adapters live in their own modules and will replace these
 *   stubs in later phases.
 *
 * IMPORTS
 *   "server.h"        MetalsharpResponse, METALSHARP_VERSION
 *   "http_server.h"   HttpServer, HttpRequest, http_server_register
 *   "database.h"      Database, db_exec, db_query
 *   "json.h"          json_parse, json_serialize, JsonValue, accessors
 *   "logger.h"        LOG_INFO, LOG_WARN, LOG_ERROR
 *   <stdbool.h>       bool, true, false
 *   <stddef.h>        size_t, NULL
 *   <stdio.h>         snprintf, vsnprintf
 *   <stdlib.h>        calloc, malloc, free, realloc
 *   <string.h>        memcpy, strlen
 *   <stdarg.h>        va_list, va_start, va_end, va_copy
 *
 * EXPORTS
 *   sharp_library_register_routes(HttpServer *server, Database *db)
 *       Register every /sharp-library/\* and /sharp-library/gog/\*
 *       route on the supplied HttpServer, binding the module to
 *       `db` for imported-app persistence. Called once during
 *       startup, before http_server_run(). Passing NULL on either
 *       side is a silent no-op so a half-initialised backend
 *       cannot crash inside the registry.
 *
 * SCHEMA
 *   Persisted imported apps live in table `sharp_library_apps`
 *   created on the first call to sharp_library_register_routes via:
 *     CREATE TABLE IF NOT EXISTS sharp_library_apps(
 *         id            INTEGER PRIMARY KEY AUTOINCREMENT,
 *         name          TEXT    NOT NULL,
 *         bottle_id     INTEGER NOT NULL DEFAULT 0,
 *         cover_path    TEXT    NOT NULL DEFAULT '',
 *         cover_position INTEGER NOT NULL DEFAULT 0,
 *         engine        TEXT    NOT NULL DEFAULT '',
 *         launch_args   TEXT    NOT NULL DEFAULT '',
 *         imported_at   INTEGER NOT NULL DEFAULT 0
 *     );
 *
 *   Route response shapes — every successful reply carries HTTP
 *   200; failures return {"ok":false,"error":"<reason>"}. The HTTP
 *   layer wraps every successful handler payload under a top-level
 *   "data" key per the shared MetalsharpResponse envelope, so a
 *   handler returning {"ok":true,"apps":[]} surfaces on the wire
 *   as {"ok":true,"data":{"ok":true,"apps":[]}}.
 *
 *     GET  /sharp-library                   data = {"ok":true,"apps":[]}
 *     GET  /sharp-library/cover             data = {"ok":true}
 *     POST /sharp-library/install           data = {"ok":true}
 *     POST /sharp-library/launch            data = {"ok":true,"error":"not found"}
 *     POST /sharp-library/uninstall         data = {"ok":true}
 *     POST /sharp-library/set-cover         data = {"ok":true}
 *     POST /sharp-library/set-cover-position data = {"ok":true}
 *     POST /sharp-library/set-engine        data = {"ok":true}
 *     POST /sharp-library/set-launch-args   data = {"ok":true}
 *     POST /sharp-library/doctor            data = {"ok":true}
 *     POST /sharp-library/import-bottle-app data = {"ok":true}
 *     GET  /sharp-library/gog/status        data = {"ok":true,"status":""}
 *     POST /sharp-library/gog/auth-code     data = {"ok":true}
 *     GET  /sharp-library/gog/games         data = {"ok":true,"games":[],"status":""}
 *     POST /sharp-library/gog/import        data = {"ok":true}
 *     POST /sharp-library/gog/initialize-prefix data = {"ok":true}
 *     POST /sharp-library/gog/install       data = {"ok":true}
 *     POST /sharp-library/gog/play          data = {"ok":true,"error":""}
 *     POST /sharp-library/gog/progress      data = {"ok":true}
 *     POST /sharp-library/gog/stop          data = {"ok":true}
 *     POST /sharp-library/gog/uninstall     data = {"ok":true}
 *     POST /sharp-library/gog/logout        data = {"ok":true}
 *     POST /sharp-library/gog/sync          data = {"ok":true}
 *     POST /sharp-library/gog/remove-prefix data = {"ok":true}
 *
 * THREADING
 *   The HTTP server dispatches every request on a worker thread.
 *   SQLite access serialises transparently through the Database
 *   mutex. Catalog reads (SELECT * FROM sharp_library_apps) walk
 *   rows through the Database wrapper callback, which is safe to
 *   call from any worker without further coordination. The
 *   module-level Database handle is captured exactly once at
 *   registration time and never swapped.
 */

#include "database.h"
#include "http_server.h"
#include "json.h"
#include "launch.h"
#include "logger.h"
#include "server.h"
#include "sharp_library_empty_state.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

/* ── Constants ── */

#define SHARP_LIBRARY_TABLE        "sharp_library_apps"
#define SHARP_LIBRARY_COL_ID       "id"
#define SHARP_LIBRARY_COL_NAME     "name"
#define SHARP_LIBRARY_COL_BOTTLE   "bottle_id"
#define SHARP_LIBRARY_COL_COVER    "cover_path"
#define SHARP_LIBRARY_COL_POS      "cover_position"
#define SHARP_LIBRARY_COL_ENGINE   "engine"
#define SHARP_LIBRARY_COL_ARGS     "launch_args"
#define SHARP_LIBRARY_COL_IMPORTED "imported_at"

/* Initial capacity for the growable JSON builder used to assemble
 * list responses whose length depends on the database row count.
 * Doubles on demand; sized so the typical empty-app-list case fits
 * in a single allocation. */
#define SHARP_LIBRARY_JSONBUF_INIT 256u

/* Per-row JSON escaping budget for a single string field. App
 * names, cover paths, and launch arguments comfortably fit in a
 * few hundred bytes; the 4 KiB slack prevents a second escaping
 * pass and bounds worst-case memory growth. */
#define SHARP_LIBRARY_ESCAPE_MAX 4096u

/* ── Module-local state ── */

/*
 * Database handle captured at registration time. The HTTP layer
 * does not thread per-request context to the handler, so every
 * route that needs persistence looks up the handle here. Set
 * exactly once before http_server_run() and never reset; all
 * access goes through the Database wrapper, which acquires its
 * own mutex.
 */
static Database* g_sl_db = NULL;

/* ── Forward declarations ── */

static MetalsharpResponse* handle_sharp_library_list(const HttpRequest* req);
static MetalsharpResponse* handle_sharp_library_cover(const HttpRequest* req);
static MetalsharpResponse* handle_sharp_library_launch(const HttpRequest* req);
static MetalsharpResponse* handle_sharp_library_gog_status(const HttpRequest* req);
static MetalsharpResponse* handle_sharp_library_gog_games(const HttpRequest* req);
static MetalsharpResponse* handle_sharp_library_gog_initialize_prefix(const HttpRequest* req);
static MetalsharpResponse* handle_sharp_library_gog_auth_code(const HttpRequest* req);
static MetalsharpResponse* handle_sharp_library_gog_logout(const HttpRequest* req);
static MetalsharpResponse* handle_sharp_library_gog_remove_prefix(const HttpRequest* req);
static MetalsharpResponse* handle_sharp_library_gog_play(const HttpRequest* req);
static MetalsharpResponse* handle_sharp_library_gog_progress(const HttpRequest* req);
static bool gog_valid_token(const char* value);
static const char* gog_product_id(JsonValue* body);
static bool gog_save_cache(JsonValue* cache, const char* path);
static bool gog_spawn_logged(const char* label, const char* product_id, char* const command_args[], char* log_path,
                             size_t log_path_size, pid_t* pid_out, char* error, size_t error_size);
static bool gog_find_game_folder(const char* root, const char* product_id, char* output, size_t output_size);
static void gog_refresh_game(JsonValue* game);
static MetalsharpResponse* handle_sharp_library_gog_uninstall(const HttpRequest* req);
static MetalsharpResponse* handle_sharp_library_gog_stop(const HttpRequest* req);
static MetalsharpResponse* handle_sharp_library_gog_import(const HttpRequest* req);
static MetalsharpResponse* handle_sharp_library_gog_install(const HttpRequest* req);
static MetalsharpResponse* handle_sharp_library_install(const HttpRequest* req);
static MetalsharpResponse* handle_sharp_library_import_bottle(const HttpRequest* req);
static MetalsharpResponse* handle_sharp_library_uninstall(const HttpRequest* req);
static MetalsharpResponse* handle_sharp_library_doctor(const HttpRequest* req);
static MetalsharpResponse* handle_sharp_library_set_cover(const HttpRequest* req);
static MetalsharpResponse* handle_sharp_library_set_cover_position(const HttpRequest* req);
static MetalsharpResponse* handle_sharp_library_set_engine(const HttpRequest* req);
static MetalsharpResponse* handle_sharp_library_set_launch_args(const HttpRequest* req);
static const char* sl_normalize_engine(const char* engine);

/* Generic {"ok":true} stub — used by every install / uninstall /
 * set-cover / set-cover-position / set-engine / set-launch-args /
 * doctor / import-bottle-app / gog-auth-code / gog-import /
 * gog-initialize-prefix / gog-install / gog-progress / gog-stop /
 * gog-uninstall / gog-logout / gog-sync / gog-remove-prefix route
 * until the real backend adapters land. */
static MetalsharpResponse* handle_sharp_library_stub_ok(const HttpRequest* req);

/* ── Response builders ── */

/*
 * Build a successful MetalsharpResponse whose `data` payload is a
 * freshly-parsed JsonValue tree built from `body`. The HTTP layer
 * serialises the tree under its top-level "data" key. `body` must
 * be a valid JSON document; an empty or syntactically-bad body
 * yields an ok=false response with a descriptive error_msg.
 * Returns NULL only on calloc failure.
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
static MetalsharpResponse* make_error_response(const char* msg) {
    MetalsharpResponse* r = calloc(1, sizeof(MetalsharpResponse));
    if (r == NULL) {
        return NULL;
    }
    r->ok = false;
    r->error_msg = strdup(msg != NULL ? msg : "error");
    return r;
}

static MetalsharpResponse* make_sharp_template_response(const char* template) {
    static const char token[] = "@HOME@";
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL)
        home = getenv("HOME");
    if (home == NULL)
        home = "";
    size_t token_length = sizeof(token) - 1u;
    size_t home_length = strlen(home);
    size_t count = 0;
    for (const char* cursor = template; (cursor = strstr(cursor, token)) != NULL; cursor += token_length)
        count++;
    size_t length = strlen(template) + count * home_length - count * token_length;
    char* output = malloc(length + 1u);
    if (output == NULL)
        return NULL;
    const char* source = template;
    char* destination = output;
    const char* match = NULL;
    while ((match = strstr(source, token)) != NULL) {
        size_t prefix = (size_t)(match - source);
        memcpy(destination, source, prefix);
        destination += prefix;
        memcpy(destination, home, home_length);
        destination += home_length;
        source = match + token_length;
    }
    strcpy(destination, source);
    MetalsharpResponse* response = make_data_response(output);
    free(output);
    return response;
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
    b.len = 0u;
    b.cap = 0u;
    b.ok = false;
    b.cap = SHARP_LIBRARY_JSONBUF_INIT;
    b.data = malloc(b.cap);
    if (b.data != NULL) {
        b.ok = true;
        b.data[0] = '\0';
        b.len = 0u;
    }
    return b;
}

static void jsonbuf_free(jsonbuf_t* b) {
    if (b == NULL) {
        return;
    }
    free(b->data);
    b->data = NULL;
    b->len = 0u;
    b->cap = 0u;
    b->ok = false;
}

/*
 * Grow the backing storage so at least `need` more bytes can be
 * appended (plus the trailing NUL). Returns the new capacity on
 * success; returns 0 and leaves `*b` untouched on realloc failure.
 */
static size_t jsonbuf_reserve(jsonbuf_t* b, size_t need) {
    if (b == NULL || !b->ok) {
        return 0u;
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
        return 0u;
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
    char esc[SHARP_LIBRARY_ESCAPE_MAX];
    size_t i = 0u;
    for (const unsigned char* p = (const unsigned char*)s; *p != '\0'; p++) {
        unsigned char c = *p;
        switch (c) {
        case '"':
            esc[i++] = '\\';
            esc[i++] = '"';
            break;
        case '\\':
            esc[i++] = '\\';
            esc[i++] = '\\';
            break;
        case '\b':
            esc[i++] = '\\';
            esc[i++] = 'b';
            break;
        case '\f':
            esc[i++] = '\\';
            esc[i++] = 'f';
            break;
        case '\n':
            esc[i++] = '\\';
            esc[i++] = 'n';
            break;
        case '\r':
            esc[i++] = '\\';
            esc[i++] = 'r';
            break;
        case '\t':
            esc[i++] = '\\';
            esc[i++] = 't';
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
            break;
        }
        if (i + 16u >= sizeof(esc)) {
            esc[i] = '\0';
            (void)jsonbuf_append(b, esc);
            i = 0u;
        }
    }
    if (i > 0u) {
        esc[i] = '\0';
        (void)jsonbuf_append(b, esc);
    }
    (void)jsonbuf_append(b, "\"");
}

/* ── Historical sharp-library manifest helpers ── */

static bool sl_base_path(char* output, size_t output_size) {
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL || home[0] == '\0')
        home = getenv("HOME");
    if (home == NULL)
        return false;
    int written = snprintf(output, output_size, "%s/sharp-library", home);
    return written >= 0 && (size_t)written < output_size;
}

static bool sl_mkdir_p(const char* path) {
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

static bool sl_manifest_path(char* output, size_t output_size) {
    char base[PATH_MAX];
    if (!sl_base_path(base, sizeof(base)) || !sl_mkdir_p(base))
        return false;
    int written = snprintf(output, output_size, "%s/library.json", base);
    return written >= 0 && (size_t)written < output_size;
}

static JsonValue* sl_load_library(char** error) {
    if (error != NULL)
        *error = NULL;
    char path[PATH_MAX];
    if (!sl_manifest_path(path, sizeof(path))) {
        if (error != NULL)
            *error = strdup("create Sharp library directory failed");
        return NULL;
    }
    FILE* input = fopen(path, "rb");
    if (input == NULL) {
        if (errno == ENOENT)
            return json_new_array();
        if (error != NULL)
            *error = strdup(strerror(errno));
        return NULL;
    }
    if (fseek(input, 0, SEEK_END) != 0) {
        fclose(input);
        return NULL;
    }
    long length = ftell(input);
    if (length < 0 || length > 64 * 1024 * 1024 || fseek(input, 0, SEEK_SET) != 0) {
        fclose(input);
        if (error != NULL)
            *error = strdup("invalid Sharp library manifest size");
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
    JsonValue* library = json_parse(content, count, error);
    free(content);
    if (library != NULL && json_type(library) != JSON_ARRAY) {
        json_free(library);
        library = NULL;
        if (error != NULL && *error == NULL)
            *error = strdup("Sharp library manifest must be an array");
    }
    return library;
}

static bool sl_save_library(const JsonValue* library, char** error) {
    if (error != NULL)
        *error = NULL;
    char path[PATH_MAX];
    if (!sl_manifest_path(path, sizeof(path)))
        return false;
    char* serialized = json_serialize_pretty(library);
    if (serialized == NULL)
        return false;
    FILE* output = fopen(path, "wb");
    if (output == NULL) {
        if (error != NULL)
            *error = strdup(strerror(errno));
        free(serialized);
        return false;
    }
    size_t length = strlen(serialized);
    bool ok = fwrite(serialized, 1u, length, output) == length;
    if (fclose(output) != 0)
        ok = false;
    if (!ok && error != NULL)
        *error = strdup(strerror(errno));
    free(serialized);
    return ok;
}

static JsonValue* sl_parse_body(const HttpRequest* request) {
    if (request == NULL || request->body == NULL || request->body_len == 0u)
        return json_new_object();
    JsonValue* body = json_parse(request->body, request->body_len, NULL);
    if (body == NULL || json_type(body) != JSON_OBJECT) {
        json_free(body);
        return json_new_object();
    }
    return body;
}

static const char* sl_string(const JsonValue* object, const char* key, const char* fallback) {
    const char* value = json_get_string(json_object_get(object, key));
    return value != NULL ? value : fallback;
}

static ssize_t sl_find_app(const JsonValue* library, const char* id) {
    if (id == NULL)
        return -1;
    for (size_t i = 0u; i < json_array_length(library); i++) {
        const char* candidate = json_get_string(json_object_get(json_array_get(library, i), "id"));
        if (candidate != NULL && strcmp(candidate, id) == 0)
            return (ssize_t)i;
    }
    return -1;
}

static MetalsharpResponse* sl_error(const char* message) {
    jsonbuf_t body = jsonbuf_new();
    jsonbuf_append(&body, "{\"ok\":false,\"error\":");
    jsonbuf_append_escaped(&body, message != NULL ? message : "error");
    jsonbuf_append(&body, "}");
    MetalsharpResponse* response = body.ok ? make_data_response(body.data) : make_error_response("out of memory");
    jsonbuf_free(&body);
    return response;
}

static MetalsharpResponse* sl_ok(void) {
    return make_data_response("{\"ok\":true}");
}

static bool sl_safe_id(const char* id) {
    return id != NULL && id[0] != '\0' && strcmp(id, ".") != 0 && strcmp(id, "..") != 0 && id[0] != '/' &&
           strchr(id, '/') == NULL && strchr(id, '\\') == NULL;
}

static bool sl_remove_tree(const char* path) {
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
        int written = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(child) || !sl_remove_tree(child)) {
            ok = false;
            break;
        }
    }
    closedir(stream);
    return ok && rmdir(path) == 0;
}

static bool sl_copy_file(const char* source, const char* destination, mode_t mode) {
    FILE* input = fopen(source, "rb");
    FILE* output = input != NULL ? fopen(destination, "wb") : NULL;
    if (input == NULL || output == NULL) {
        if (input != NULL)
            fclose(input);
        if (output != NULL)
            fclose(output);
        return false;
    }
    bool ok = true;
    char buffer[65536];
    size_t count;
    while ((count = fread(buffer, 1u, sizeof(buffer), input)) > 0u)
        if (fwrite(buffer, 1u, count, output) != count) {
            ok = false;
            break;
        }
    if (ferror(input))
        ok = false;
    fclose(input);
    if (fclose(output) != 0)
        ok = false;
    if (ok)
        (void)chmod(destination, mode & 0777u);
    return ok;
}

static bool sl_copy_tree(const char* source, const char* destination) {
    struct stat info;
    if (lstat(source, &info) != 0)
        return false;
    if (S_ISREG(info.st_mode))
        return sl_copy_file(source, destination, info.st_mode);
    if (!S_ISDIR(info.st_mode))
        return true;
    if (!sl_mkdir_p(destination))
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
        int source_written = snprintf(source_child, sizeof(source_child), "%s/%s", source, entry->d_name);
        int destination_written =
            snprintf(destination_child, sizeof(destination_child), "%s/%s", destination, entry->d_name);
        if (source_written < 0 || destination_written < 0 || (size_t)source_written >= sizeof(source_child) ||
            (size_t)destination_written >= sizeof(destination_child) ||
            !sl_copy_tree(source_child, destination_child)) {
            ok = false;
            break;
        }
    }
    closedir(stream);
    return ok;
}

static unsigned long long sl_directory_size(const char* path) {
    struct stat info;
    if (lstat(path, &info) != 0)
        return 0u;
    if (S_ISREG(info.st_mode))
        return (unsigned long long)info.st_size;
    if (!S_ISDIR(info.st_mode))
        return 0u;
    DIR* stream = opendir(path);
    if (stream == NULL)
        return 0u;
    unsigned long long total = 0u;
    struct dirent* entry;
    while ((entry = readdir(stream)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char child[PATH_MAX];
        int written = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        if (written > 0 && (size_t)written < sizeof(child))
            total += sl_directory_size(child);
    }
    closedir(stream);
    return total;
}

static bool sl_set_owned(JsonValue* object, const char* key, JsonValue* value) {
    if (value == NULL)
        return false;
    if (json_object_set_owned(object, key, value))
        return true;
    json_free(value);
    return false;
}

static MetalsharpResponse* sl_mutation_result(JsonValue* library, bool ok, char* error) {
    json_free(library);
    if (!ok) {
        MetalsharpResponse* response = sl_error(error != NULL ? error : "library write failed");
        free(error);
        return response;
    }
    free(error);
    return sl_ok();
}

/* ── SQL helpers ── */

/*
 * Create the `sharp_library_apps` table on the first registration.
 * Failure is logged; the routes then return graceful empty /
 * degraded responses because every attempt to write through a
 * missing table would surface the same sqlite error every call.
 * No user-supplied parameters are interpolated into SQL in this
 * module, so single-quote doubling is unnecessary.
 */
static void ensure_sharp_library_table(Database* db) {
    if (db == NULL) {
        return;
    }
    static const char ddl[] =
        "CREATE TABLE IF NOT EXISTS " SHARP_LIBRARY_TABLE " (" SHARP_LIBRARY_COL_ID
        " INTEGER PRIMARY KEY AUTOINCREMENT," SHARP_LIBRARY_COL_NAME " TEXT NOT NULL," SHARP_LIBRARY_COL_BOTTLE
        " INTEGER NOT NULL DEFAULT 0," SHARP_LIBRARY_COL_COVER " TEXT NOT NULL DEFAULT ''," SHARP_LIBRARY_COL_POS
        " INTEGER NOT NULL DEFAULT 0," SHARP_LIBRARY_COL_ENGINE " TEXT NOT NULL DEFAULT ''," SHARP_LIBRARY_COL_ARGS
        " TEXT NOT NULL DEFAULT ''," SHARP_LIBRARY_COL_IMPORTED " INTEGER NOT NULL DEFAULT 0"
        ")";
    char* err = NULL;
    if (!db_exec(db, ddl, &err)) {
        LOG_ERROR("sharp_library_apps create failed: %s", err != NULL ? err : "(unknown)");
        free(err);
    }
}

/* ── Route handlers ── */

/*
 * GET /sharp-library — list every imported app stored in the
 * sharp_library_apps table. The SQL query is built inline (column
 * names interpolated through macros) so the schema stays in one
 * place; there are no user-supplied parameters so single-quote
 * doubling is unnecessary. An empty table returns
 * {"ok":true,"apps":[]} exactly like the stub.
 */
static MetalsharpResponse* handle_sharp_library_list(const HttpRequest* req) {
    (void)req;
    char* error = NULL;
    JsonValue* library = sl_load_library(&error);
    if (library == NULL) {
        MetalsharpResponse* response = sl_error(error != NULL ? error : "library read failed");
        free(error);
        return response;
    }
    char* apps = json_serialize(library);
    json_free(library);
    if (apps == NULL)
        return sl_error("library serialization failed");
    jsonbuf_t body = jsonbuf_new();
    jsonbuf_append(&body, "{\"ok\":true,\"apps\":");
    jsonbuf_append(&body, apps);
    jsonbuf_append(&body, "}");
    free(apps);
    MetalsharpResponse* response = body.ok ? make_data_response(body.data) : sl_error("out of memory");
    jsonbuf_free(&body);
    return response;
}

/*
 * GET /sharp-library/cover — stub returning {"ok":true}. The real
 * implementation will serve the configured cover image bytes via
 * a content-type-aware HTTP reply; for now the route exists so
 * the Electron shell's "set cover" preview flow can probe without
 * receiving a 404.
 */
static MetalsharpResponse* handle_sharp_library_cover(const HttpRequest* req) {
    const char* marker = req != NULL && req->query != NULL ? strstr(req->query, "id=") : NULL;
    const char* id = marker != NULL ? marker + 3u : "";
    char id_buffer[512];
    size_t id_length = strcspn(id, "&");
    if (id_length >= sizeof(id_buffer))
        id_length = sizeof(id_buffer) - 1u;
    memcpy(id_buffer, id, id_length);
    id_buffer[id_length] = '\0';
    char* error = NULL;
    JsonValue* library = sl_load_library(&error);
    ssize_t index = library != NULL ? sl_find_app(library, id_buffer) : -1;
    const char* cover =
        index >= 0 ? json_get_string(json_object_get(json_array_get(library, (size_t)index), "cover")) : NULL;
    char cover_name[PATH_MAX] = "", path[PATH_MAX], base[PATH_MAX];
    if (cover != NULL)
        snprintf(cover_name, sizeof(cover_name), "%s", cover);
    json_free(library);
    free(error);
    if (cover_name[0] == '\0' || !sl_base_path(base, sizeof(base)))
        goto not_found;
    snprintf(path, sizeof(path), "%s/%s", base, cover_name);
    FILE* input = fopen(path, "rb");
    if (input == NULL || fseek(input, 0, SEEK_END) != 0)
        goto close_not_found;
    long length = ftell(input);
    if (length < 0 || fseek(input, 0, SEEK_SET) != 0)
        goto close_not_found;
    void* bytes = malloc((size_t)length > 0u ? (size_t)length : 1u);
    if (bytes == NULL)
        goto close_not_found;
    if (fread(bytes, 1u, (size_t)length, input) != (size_t)length) {
        free(bytes);
        goto close_not_found;
    }
    fclose(input);
    const char* extension = strrchr(path, '.');
    const char* mime = extension != NULL && strcasecmp(extension, ".png") == 0    ? "image/png"
                       : extension != NULL && strcasecmp(extension, ".svg") == 0  ? "image/svg+xml"
                       : extension != NULL && strcasecmp(extension, ".webp") == 0 ? "image/webp"
                                                                                  : "image/jpeg";
    MetalsharpResponse* response = calloc(1u, sizeof(MetalsharpResponse));
    if (response == NULL) {
        free(bytes);
        return NULL;
    }
    response->ok = true;
    response->data = bytes;
    response->data_kind = METALSHARP_RESPONSE_RAW;
    response->data_length = (size_t)length;
    response->content_type = strdup(mime);
    return response;

close_not_found:
    if (input != NULL)
        fclose(input);
not_found: {
    MetalsharpResponse* response = make_error_response("cover not found");
    if (response != NULL)
        response->http_status = 404;
    return response;
}
}

/*
 * POST /sharp-library/launch — rejection stub returning
 * {"ok":true,"error":"not found"}. The shape signals "no launch
 * adapter is wired up yet" while keeping the wire contract stable.
 * A future real implementation will parse the body, look up the
 * imported app, and either populate the `error` field with a
 * diagnostic or strip it to indicate success.
 */
static MetalsharpResponse* handle_sharp_library_launch(const HttpRequest* req) {
    JsonValue* body = sl_parse_body(req);
    const char* id = sl_string(body, "id", "");
    const char* engine = sl_string(body, "engine", "auto");
    if (id[0] == '\0') {
        json_free(body);
        return sl_error("id required");
    }
    char* load_error = NULL;
    JsonValue* library = sl_load_library(&load_error);
    ssize_t index = library != NULL ? sl_find_app(library, id) : -1;
    if (index < 0) {
        json_free(body);
        json_free(library);
        free(load_error);
        return sl_error("App not found");
    }
    JsonValue* app = json_array_get(library, (size_t)index);
    const char* install_directory = sl_string(app, "install_dir", "");
    const char* relative_executable = sl_string(app, "exe_path", "");
    char executable[PATH_MAX];
    int written = relative_executable[0] == '/'
                      ? snprintf(executable, sizeof(executable), "%s", relative_executable)
                      : snprintf(executable, sizeof(executable), "%s/%s", install_directory, relative_executable);
    if (written < 0 || (size_t)written >= sizeof(executable)) {
        json_free(body);
        json_free(library);
        free(load_error);
        return sl_error("File name too long (os error 63)");
    }
    struct stat executable_info;
    if (stat(executable, &executable_info) != 0) {
        char message[PATH_MAX + 32u];
        snprintf(message, sizeof(message), "EXE not found: %s", executable);
        json_free(body);
        json_free(library);
        free(load_error);
        return sl_error(message);
    }
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL)
        home = getenv("HOME");
    char runtime_check[PATH_MAX];
    snprintf(runtime_check, sizeof(runtime_check), "%s/runtime/wine/lib/wine/x86_64-unix", home != NULL ? home : "");
    struct stat runtime_info;
    if (stat(runtime_check, &runtime_info) != 0 || !S_ISDIR(runtime_info.st_mode)) {
        char message[PATH_MAX + 96u];
        snprintf(message, sizeof(message), "MetalSharp runtime is incomplete: lib/wine/x86_64-unix (%s)",
                 runtime_check);
        json_free(body);
        json_free(library);
        free(load_error);
        return sl_error(message);
    }
    const char* pipeline = sl_normalize_engine(engine);
    if (pipeline == NULL || strcmp(pipeline, "auto") == 0)
        pipeline = "wine_bare";
    JsonValue* base_arguments = json_object_get(app, "launch_args");
    JsonValue* user_arguments = json_object_get(app, "user_launch_args");
    size_t base_count = json_array_length(base_arguments), user_count = json_array_length(user_arguments);
    const char** arguments = calloc(base_count + user_count, sizeof(char*));
    size_t argument_count = 0u;
    if (arguments == NULL && base_count + user_count > 0u) {
        json_free(body);
        json_free(library);
        free(load_error);
        return sl_error("out of memory");
    }
    for (size_t i = 0u; i < base_count; i++) {
        const char* argument = json_get_string(json_array_get(base_arguments, i));
        if (argument != NULL)
            arguments[argument_count++] = argument;
    }
    for (size_t i = 0u; i < user_count; i++) {
        const char* argument = json_get_string(json_array_get(user_arguments, i));
        if (argument != NULL)
            arguments[argument_count++] = argument;
    }
    char launch_error[1024] = "";
    pid_t pid = 0;
    bool launched = metalsharp_launch_wine_executable(relative_executable, install_directory, NULL, arguments,
                                                      argument_count, &pid, launch_error, sizeof(launch_error));
    free(arguments);
    json_free(body);
    json_free(library);
    free(load_error);
    if (!launched)
        return sl_error(launch_error);
    jsonbuf_t response = jsonbuf_new();
    jsonbuf_appendf(&response, "{\"ok\":true,\"pid\":%ld,\"gameType\":\"metalsharp_wine\",\"pipeline\":", (long)pid);
    jsonbuf_append_escaped(&response, pipeline);
    jsonbuf_append(&response, ",\"exePath\":");
    jsonbuf_append_escaped(&response, executable);
    jsonbuf_append(&response, ",\"warnings\":[]}");
    MetalsharpResponse* result = response.ok ? make_data_response(response.data) : sl_error("out of memory");
    jsonbuf_free(&response);
    return result;
}

static MetalsharpResponse* handle_sharp_library_install(const HttpRequest* req) {
    JsonValue* body = sl_parse_body(req);
    const char* source = sl_string(body, "srcPath", "");
    if (source[0] == '\0') {
        json_free(body);
        return sl_error("srcPath required");
    }
    struct stat info;
    if (stat(source, &info) != 0 || !S_ISREG(info.st_mode)) {
        json_free(body);
        return sl_error("Source EXE not found");
    }
    const char* extension = strrchr(source, '.');
    if (extension == NULL || (strcasecmp(extension, ".exe") != 0 && strcasecmp(extension, ".msi") != 0)) {
        json_free(body);
        return sl_error("Only .exe and .msi Windows program installers are supported");
    }
    const char* filename = strrchr(source, '/');
    filename = filename != NULL ? filename + 1u : source;
    char lowercase_name[PATH_MAX];
    size_t filename_length = strlen(filename);
    if (filename_length >= sizeof(lowercase_name))
        filename_length = sizeof(lowercase_name) - 1u;
    for (size_t i = 0u; i < filename_length; i++)
        lowercase_name[i] = (char)tolower((unsigned char)filename[i]);
    lowercase_name[filename_length] = '\0';
    bool installer = strcasecmp(extension, ".msi") == 0 || strstr(lowercase_name, "setup") != NULL ||
                     strstr(lowercase_name, "install") != NULL || strstr(lowercase_name, "launcher") != NULL ||
                     strstr(lowercase_name, "bootstrap") != NULL || strstr(lowercase_name, "update") != NULL;
    if (installer) {
        json_free(body);
        return sl_error("MetalSharp Wine not found — run setup first");
    }
    const char* custom_name = json_get_string(json_object_get(body, "name"));
    char app_name[PATH_MAX];
    if (custom_name != NULL)
        snprintf(app_name, sizeof(app_name), "%s", custom_name);
    else {
        snprintf(app_name, sizeof(app_name), "%s", filename);
        size_t length = strlen(app_name);
        if (length >= 4u && strcmp(app_name + length - 4u, ".exe") == 0)
            app_name[length - 4u] = '\0';
    }
    char clean[PATH_MAX];
    size_t clean_length = 0u;
    for (const unsigned char* cursor = (const unsigned char*)app_name;
         *cursor != '\0' && clean_length + 1u < sizeof(clean); cursor++)
        clean[clean_length++] = isalnum(*cursor) ? (char)tolower(*cursor) : '_';
    clean[clean_length] = '\0';
    char* clean_start = clean;
    while (*clean_start == '_')
        clean_start++;
    while (clean_length > (size_t)(clean_start - clean) && clean[clean_length - 1u] == '_')
        clean[--clean_length] = '\0';
    struct timeval now;
    gettimeofday(&now, NULL);
    unsigned long long milliseconds = (unsigned long long)now.tv_sec * 1000u + (unsigned long long)now.tv_usec / 1000u;
    char id[PATH_MAX];
    snprintf(id, sizeof(id), "%s_%llu", clean_start, milliseconds);
    char source_root[PATH_MAX];
    snprintf(source_root, sizeof(source_root), "%s", source);
    char* slash = strrchr(source_root, '/');
    if (slash != NULL)
        *slash = '\0';
    else
        snprintf(source_root, sizeof(source_root), ".");
    char relative_executable[PATH_MAX];
    snprintf(relative_executable, sizeof(relative_executable), "%s", filename);
    char base[PATH_MAX], destination[PATH_MAX];
    if (!sl_base_path(base, sizeof(base)) || !sl_mkdir_p(base)) {
        json_free(body);
        return sl_error("create Sharp library directory failed");
    }
    snprintf(destination, sizeof(destination), "%s/%s", base, id);
    if (!sl_copy_tree(source_root, destination)) {
        (void)sl_remove_tree(destination);
        json_free(body);
        return sl_error(strerror(errno));
    }
    unsigned long long size = sl_directory_size(destination);
    char installed_at[32];
    snprintf(installed_at, sizeof(installed_at), "%lld", (long long)now.tv_sec);
    JsonValue* app = json_new_object();
    bool app_ok =
        app != NULL && sl_set_owned(app, "id", json_new_string(id)) &&
        sl_set_owned(app, "name", json_new_string(app_name)) &&
        sl_set_owned(app, "exe_path", json_new_string(relative_executable)) &&
        sl_set_owned(app, "install_dir", json_new_string(destination)) && sl_set_owned(app, "cover", json_new_null()) &&
        sl_set_owned(app, "cover_position_x", json_new_number(50.0)) &&
        sl_set_owned(app, "cover_position_y", json_new_number(50.0)) &&
        sl_set_owned(app, "engine", json_new_string("auto")) && sl_set_owned(app, "launch_args", json_new_array()) &&
        sl_set_owned(app, "user_launch_args", json_new_array()) && sl_set_owned(app, "bottle_id", json_new_null()) &&
        sl_set_owned(app, "installed_at", json_new_string(installed_at)) &&
        sl_set_owned(app, "size_bytes", json_new_number((double)size));
    char* error = NULL;
    JsonValue* library = app_ok ? sl_load_library(&error) : NULL;
    JsonValue* response_app = app_ok ? json_clone(app) : NULL;
    bool saved =
        library != NULL && app != NULL && json_array_append_owned(library, app) && sl_save_library(library, &error);
    if (!saved && app != NULL && (library == NULL || sl_find_app(library, id) < 0))
        json_free(app);
    json_free(body);
    json_free(library);
    if (!saved || response_app == NULL) {
        json_free(response_app);
        (void)sl_remove_tree(destination);
        MetalsharpResponse* response = sl_error(error != NULL ? error : "library write failed");
        free(error);
        return response;
    }
    free(error);
    char* serialized_app = json_serialize(response_app);
    json_free(response_app);
    jsonbuf_t response = jsonbuf_new();
    jsonbuf_append(&response, "{\"ok\":true,\"app\":");
    jsonbuf_append(&response, serialized_app != NULL ? serialized_app : "null");
    jsonbuf_append(&response, "}");
    free(serialized_app);
    MetalsharpResponse* result = response.ok ? make_data_response(response.data) : sl_error("out of memory");
    jsonbuf_free(&response);
    return result;
}

static MetalsharpResponse* handle_sharp_library_import_bottle(const HttpRequest* req) {
    JsonValue* body = sl_parse_body(req);
    const char* bottle_id = sl_string(body, "bottleId", "");
    const char* executable = sl_string(body, "exePath", "");
    if (bottle_id[0] == '\0' || executable[0] == '\0') {
        json_free(body);
        return sl_error("bottleId and exePath required");
    }
    if (!sl_safe_id(bottle_id)) {
        json_free(body);
        return sl_error("Invalid bottle id");
    }
    struct stat info;
    const char* extension = strrchr(executable, '.');
    if (stat(executable, &info) != 0 || !S_ISREG(info.st_mode) || extension == NULL ||
        strcasecmp(extension, ".exe") != 0) {
        json_free(body);
        return sl_error("Bottle app executable not found");
    }
    json_free(body);
    return sl_error("Bottle not found");
}

static MetalsharpResponse* handle_sharp_library_uninstall(const HttpRequest* req) {
    JsonValue* body = sl_parse_body(req);
    const char* id = sl_string(body, "id", "");
    if (id[0] == '\0') {
        json_free(body);
        return sl_error("id required");
    }
    char* error = NULL;
    JsonValue* library = sl_load_library(&error);
    if (library == NULL) {
        json_free(body);
        MetalsharpResponse* response = sl_error(error != NULL ? error : "library read failed");
        free(error);
        return response;
    }
    ssize_t index = sl_find_app(library, id);
    if (index < 0) {
        json_free(body);
        json_free(library);
        free(error);
        return sl_error("App not found");
    }
    if (!sl_safe_id(id)) {
        json_free(body);
        json_free(library);
        free(error);
        return sl_error("Invalid app id");
    }
    char base[PATH_MAX], path[PATH_MAX];
    if (sl_base_path(base, sizeof(base))) {
        snprintf(path, sizeof(path), "%s/%s", base, id);
        (void)sl_remove_tree(path);
        snprintf(path, sizeof(path), "%s/%s.cover", base, id);
        (void)unlink(path);
    }
    json_array_remove(library, (size_t)index);
    bool ok = sl_save_library(library, &error);
    json_free(body);
    return sl_mutation_result(library, ok, error);
}

static MetalsharpResponse* handle_sharp_library_set_cover_position(const HttpRequest* req) {
    JsonValue* body = sl_parse_body(req);
    const char* id = sl_string(body, "id", "");
    if (id[0] == '\0') {
        json_free(body);
        return sl_error("id required");
    }
    double x = json_get_number(json_object_get(body, "x"), 50.0);
    double y = json_get_number(json_object_get(body, "y"), 50.0);
    if (x < 0.0)
        x = 0.0;
    if (y < 0.0)
        y = 0.0;
    if (x > 100.0)
        x = 100.0;
    if (y > 100.0)
        y = 100.0;
    char* error = NULL;
    JsonValue* library = sl_load_library(&error);
    ssize_t index = library != NULL ? sl_find_app(library, id) : -1;
    if (index < 0) {
        json_free(body);
        json_free(library);
        free(error);
        return sl_error("App not found");
    }
    JsonValue* app = json_array_get(library, (size_t)index);
    bool changed = sl_set_owned(app, "cover_position_x", json_new_number((double)(unsigned)x)) &&
                   sl_set_owned(app, "cover_position_y", json_new_number((double)(unsigned)y));
    bool ok = changed && sl_save_library(library, &error);
    json_free(body);
    return sl_mutation_result(library, ok, error);
}

static char* sl_trimmed(const char* value) {
    if (value == NULL)
        return NULL;
    while (*value != '\0' && (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n'))
        value++;
    size_t length = strlen(value);
    while (length > 0u && (value[length - 1u] == ' ' || value[length - 1u] == '\t' || value[length - 1u] == '\r' ||
                           value[length - 1u] == '\n'))
        length--;
    char* result = malloc(length + 1u);
    if (result != NULL) {
        memcpy(result, value, length);
        result[length] = '\0';
    }
    return result;
}

static MetalsharpResponse* handle_sharp_library_set_launch_args(const HttpRequest* req) {
    JsonValue* body = sl_parse_body(req);
    const char* id = sl_string(body, "id", "");
    if (id[0] == '\0') {
        json_free(body);
        return sl_error("id required");
    }
    char* error = NULL;
    JsonValue* library = sl_load_library(&error);
    ssize_t index = library != NULL ? sl_find_app(library, id) : -1;
    if (index < 0) {
        json_free(body);
        json_free(library);
        free(error);
        return sl_error("App not found");
    }
    JsonValue* arguments = json_new_array();
    JsonValue* requested = json_object_get(body, "args");
    for (size_t i = 0u; i < json_array_length(requested); i++) {
        const char* source = json_get_string(json_array_get(requested, i));
        char* trimmed = sl_trimmed(source);
        if (trimmed != NULL && trimmed[0] != '\0') {
            JsonValue* argument = json_new_string(trimmed);
            if (argument == NULL || !json_array_append_owned(arguments, argument))
                json_free(argument);
        }
        free(trimmed);
    }
    bool changed = sl_set_owned(json_array_get(library, (size_t)index), "user_launch_args", arguments);
    bool ok = changed && sl_save_library(library, &error);
    json_free(body);
    return sl_mutation_result(library, ok, error);
}

static const char* sl_normalize_engine(const char* engine) {
    if (strcasecmp(engine, "auto") == 0)
        return "auto";
    if (strcasecmp(engine, "m64") == 0 || strcasecmp(engine, "d3d11") == 0 || strcasecmp(engine, "dxmt") == 0)
        return "m11";
    if (strcasecmp(engine, "d3d9") == 0)
        return "m9";
    if (strcasecmp(engine, "d3d10") == 0)
        return "m10";
    if (strcasecmp(engine, "d3d12") == 0)
        return "m12";
    static const char* valid[] = {"wine_bare", "m9",  "m10",       "m10_32", "m11",         "m11_32",  "m12",
                                  "m13",       "m32", "fna_arm64", "steam",  "macos_steam", "d3dmetal"};
    for (size_t i = 0u; i < sizeof(valid) / sizeof(valid[0]); i++)
        if (strcasecmp(engine, valid[i]) == 0)
            return valid[i];
    return NULL;
}

static MetalsharpResponse* handle_sharp_library_set_engine(const HttpRequest* req) {
    JsonValue* body = sl_parse_body(req);
    const char* id = sl_string(body, "id", "");
    const char* engine = sl_string(body, "engine", "wine_bare");
    if (id[0] == '\0') {
        json_free(body);
        return sl_error("id required");
    }
    const char* normalized = sl_normalize_engine(engine);
    if (normalized == NULL) {
        char message[512];
        snprintf(message, sizeof(message),
                 "Unknown engine: %s. Valid: auto, wine_bare, m64, m9, m10, m11, m12, m32, d3d9, d3d10, d3d11, d3d12",
                 engine);
        json_free(body);
        return sl_error(message);
    }
    char* error = NULL;
    JsonValue* library = sl_load_library(&error);
    ssize_t index = library != NULL ? sl_find_app(library, id) : -1;
    if (index < 0) {
        json_free(body);
        json_free(library);
        free(error);
        return sl_error("App not found");
    }
    bool changed = sl_set_owned(json_array_get(library, (size_t)index), "engine", json_new_string(normalized));
    bool ok = changed && sl_save_library(library, &error);
    json_free(body);
    return sl_mutation_result(library, ok, error);
}

static MetalsharpResponse* handle_sharp_library_set_cover(const HttpRequest* req) {
    JsonValue* body = sl_parse_body(req);
    const char* id = sl_string(body, "id", "");
    const char* source = sl_string(body, "coverPath", "");
    if (id[0] == '\0' || source[0] == '\0') {
        json_free(body);
        return sl_error("id and coverPath required");
    }
    struct stat info;
    if (stat(source, &info) != 0 || !S_ISREG(info.st_mode)) {
        json_free(body);
        return sl_error("Cover image not found");
    }
    if (info.st_size > 5 * 1024 * 1024) {
        json_free(body);
        return sl_error("Cover image must be under 5MB");
    }
    const char* extension = strrchr(source, '.');
    extension = extension != NULL && extension[1] != '\0' ? extension + 1 : "jpg";
    char lowercase_extension[32];
    size_t extension_length = strlen(extension);
    if (extension_length >= sizeof(lowercase_extension))
        extension_length = sizeof(lowercase_extension) - 1u;
    for (size_t i = 0u; i < extension_length; i++)
        lowercase_extension[i] = (char)tolower((unsigned char)extension[i]);
    lowercase_extension[extension_length] = '\0';
    extension = lowercase_extension;
    char base[PATH_MAX], filename[PATH_MAX], destination[PATH_MAX];
    if (!sl_base_path(base, sizeof(base)) || !sl_mkdir_p(base)) {
        json_free(body);
        return sl_error("create Sharp library directory failed");
    }
    snprintf(filename, sizeof(filename), "%s.%s", id, extension);
    snprintf(destination, sizeof(destination), "%s/%s", base, filename);
    FILE* input = fopen(source, "rb");
    FILE* output = input != NULL ? fopen(destination, "wb") : NULL;
    bool copied = input != NULL && output != NULL;
    char buffer[65536];
    while (copied) {
        size_t count = fread(buffer, 1u, sizeof(buffer), input);
        if (count > 0u && fwrite(buffer, 1u, count, output) != count)
            copied = false;
        if (count < sizeof(buffer)) {
            if (ferror(input))
                copied = false;
            break;
        }
    }
    if (input != NULL)
        fclose(input);
    if (output != NULL && fclose(output) != 0)
        copied = false;
    if (!copied) {
        json_free(body);
        return sl_error(strerror(errno));
    }
    char* error = NULL;
    JsonValue* library = sl_load_library(&error);
    ssize_t index = library != NULL ? sl_find_app(library, id) : -1;
    bool ok = true;
    if (index >= 0) {
        ok = sl_set_owned(json_array_get(library, (size_t)index), "cover", json_new_string(filename)) &&
             sl_save_library(library, &error);
    }
    json_free(body);
    return sl_mutation_result(library, ok, error);
}

static MetalsharpResponse* handle_sharp_library_doctor(const HttpRequest* req) {
    JsonValue* body = sl_parse_body(req);
    const char* id = sl_string(body, "id", "");
    if (id[0] == '\0') {
        json_free(body);
        return sl_error("id required");
    }
    char* error = NULL;
    JsonValue* library = sl_load_library(&error);
    bool found = library != NULL && sl_find_app(library, id) >= 0;
    json_free(body);
    json_free(library);
    free(error);
    return found ? sl_error("MetalSharp Wine not found — run setup first") : sl_error("App not found");
}

/*
 * GET /sharp-library/gog/status — stub returning
 * {"ok":true,"status":""}. The real implementation will report the
 * GOG OAuth phase (signed-out, code-received, token-exchange,
 * token-valid); the empty status string keeps the wire contract
 * stable until the GOG auth module lands.
 */
static const char* gog_home(void) {
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL || home[0] == '\0')
        home = getenv("HOME");
    return home != NULL ? home : "";
}

static bool gog_join(char* output, size_t output_size, const char* left, const char* right) {
    int written = snprintf(output, output_size, "%s%s%s", left,
                           left[0] != '\0' && left[strlen(left) - 1u] == '/' ? "" : "/", right);
    return written >= 0 && (size_t)written < output_size;
}

static bool gog_file(const char* path) {
    struct stat st;
    return path != NULL && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool gog_directory(const char* path) {
    struct stat st;
    return path != NULL && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool gog_mkdir_p(const char* path) {
    char copy[PATH_MAX];
    int written = snprintf(copy, sizeof(copy), "%s", path);
    if (written < 0 || (size_t)written >= sizeof(copy))
        return false;
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

static bool gog_find_binary(char* output, size_t output_size) {
    const char* explicit_path = getenv("METALSHARP_GOGDL_BIN");
    if (explicit_path != NULL && explicit_path[0] != '\0' && gog_file(explicit_path)) {
        int written = snprintf(output, output_size, "%s", explicit_path);
        return written >= 0 && (size_t)written < output_size;
    }
    char candidate[PATH_MAX];
    static const char* relative[] = {"tools/gogdl", "runtime/gogdl"};
    for (size_t i = 0u; i < sizeof(relative) / sizeof(relative[0]); i++) {
        if (gog_join(candidate, sizeof(candidate), gog_home(), relative[i]) && gog_file(candidate)) {
            int written = snprintf(output, output_size, "%s", candidate);
            return written >= 0 && (size_t)written < output_size;
        }
    }
    const char* path_env = getenv("PATH");
    if (path_env == NULL)
        return false;
    char* paths = strdup(path_env);
    if (paths == NULL)
        return false;
    bool found = false;
    char* cursor = paths;
    while (!found && cursor != NULL) {
        char* separator = strchr(cursor, ':');
        if (separator != NULL)
            *separator = '\0';
        const char* directory = cursor[0] != '\0' ? cursor : ".";
        if (gog_join(candidate, sizeof(candidate), directory, "gogdl") && gog_file(candidate)) {
            int written = snprintf(output, output_size, "%s", candidate);
            found = written >= 0 && (size_t)written < output_size;
        }
        cursor = separator != NULL ? separator + 1 : NULL;
    }
    free(paths);
    return found;
}

static bool gog_wine_binary(char* output, size_t output_size) {
    char candidate[PATH_MAX];
    static const char* relative[] = {"runtime/wine/bin/metalsharp-wine", "runtime/wine/bin/wine"};
    for (size_t i = 0u; i < sizeof(relative) / sizeof(relative[0]); i++) {
        if (gog_join(candidate, sizeof(candidate), gog_home(), relative[i]) && gog_file(candidate)) {
            int written = snprintf(output, output_size, "%s", candidate);
            return written >= 0 && (size_t)written < output_size;
        }
    }
    (void)gog_join(output, output_size, gog_home(), "runtime/wine/bin/wine");
    return false;
}

static bool gog_auth_nonempty(void) {
    char path[PATH_MAX];
    struct stat st;
    return gog_join(path, sizeof(path), gog_home(), "gog_store/auth.json") && stat(path, &st) == 0 &&
           S_ISREG(st.st_mode) && st.st_size > 16;
}

static void gog_stage_oauth_marker(void) {
    char directory[PATH_MAX], marker[PATH_MAX];
    if (!gog_join(directory, sizeof(directory), gog_home(), "tools/gog-oauth-helper") || !gog_mkdir_p(directory) ||
        !gog_join(marker, sizeof(marker), directory, ".inline-helper"))
        return;
    FILE* file = fopen(marker, "wb");
    if (file != NULL)
        fclose(file);
}

static bool gog_find_command(const char* name, const char* const absolute[], size_t absolute_count, char* output,
                             size_t output_size) {
    for (size_t i = 0u; i < absolute_count; i++) {
        if (gog_file(absolute[i])) {
            int written = snprintf(output, output_size, "%s", absolute[i]);
            return written >= 0 && (size_t)written < output_size;
        }
    }
    const char* path_env = getenv("PATH");
    if (path_env == NULL)
        return false;
    char* paths = strdup(path_env);
    if (paths == NULL)
        return false;
    bool found = false;
    for (char* cursor = paths; cursor != NULL && !found;) {
        char* separator = strchr(cursor, ':');
        if (separator != NULL)
            *separator = '\0';
        char candidate[PATH_MAX];
        if (gog_join(candidate, sizeof(candidate), cursor[0] != '\0' ? cursor : ".", name) && gog_file(candidate)) {
            int written = snprintf(output, output_size, "%s", candidate);
            found = written >= 0 && (size_t)written < output_size;
        }
        cursor = separator != NULL ? separator + 1 : NULL;
    }
    free(paths);
    return found;
}

static bool gog_remove_tree(const char* path) {
    struct stat st;
    if (lstat(path, &st) != 0)
        return errno == ENOENT;
    if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode))
        return unlink(path) == 0;
    DIR* dir = opendir(path);
    if (dir == NULL)
        return false;
    bool ok = true;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char child[PATH_MAX];
        if (!gog_join(child, sizeof(child), path, entry->d_name) || !gog_remove_tree(child)) {
            ok = false;
            break;
        }
    }
    closedir(dir);
    return ok && rmdir(path) == 0;
}

static void gog_trim_output(char* text) {
    char* first = text;
    while (*first == ' ' || *first == '\t' || *first == '\r' || *first == '\n')
        first++;
    if (first != text)
        memmove(text, first, strlen(first) + 1u);
    size_t length = strlen(text);
    while (length > 0u && (text[length - 1u] == ' ' || text[length - 1u] == '\t' || text[length - 1u] == '\r' ||
                           text[length - 1u] == '\n'))
        text[--length] = '\0';
}

static size_t gog_read_capture(const char* path, char* output, size_t output_size) {
    output[0] = '\0';
    FILE* file = fopen(path, "rb");
    if (file == NULL)
        return 0u;
    size_t count = fread(output, 1u, output_size - 1u, file);
    output[count] = '\0';
    fclose(file);
    gog_trim_output(output);
    return strlen(output);
}

static bool gog_run_bootstrap(char* const argv[], const char* context, bool pip_environment, char* error,
                              size_t error_size) {
    char stdout_path[] = "/tmp/metalsharp-gog-stdout-XXXXXX";
    char stderr_path[] = "/tmp/metalsharp-gog-stderr-XXXXXX";
    int stdout_fd = mkstemp(stdout_path), stderr_fd = mkstemp(stderr_path);
    if (stdout_fd < 0 || stderr_fd < 0) {
        if (stdout_fd >= 0) {
            close(stdout_fd);
            unlink(stdout_path);
        }
        if (stderr_fd >= 0) {
            close(stderr_fd);
            unlink(stderr_path);
        }
        snprintf(error, error_size, "%s failed to start: %s", context, strerror(errno));
        return false;
    }
    int exec_pipe[2];
    if (pipe(exec_pipe) != 0) {
        close(stdout_fd);
        close(stderr_fd);
        unlink(stdout_path);
        unlink(stderr_path);
        snprintf(error, error_size, "%s failed to start: %s", context, strerror(errno));
        return false;
    }
    (void)fcntl(exec_pipe[1], F_SETFD, FD_CLOEXEC);
    pid_t pid = fork();
    if (pid < 0) {
        close(stdout_fd);
        close(stderr_fd);
        close(exec_pipe[0]);
        close(exec_pipe[1]);
        unlink(stdout_path);
        unlink(stderr_path);
        snprintf(error, error_size, "%s failed to start: %s", context, strerror(errno));
        return false;
    }
    if (pid == 0) {
        close(exec_pipe[0]);
        (void)dup2(stdout_fd, STDOUT_FILENO);
        (void)dup2(stderr_fd, STDERR_FILENO);
        close(stdout_fd);
        close(stderr_fd);
        if (pip_environment)
            (void)setenv("PIP_DISABLE_PIP_VERSION_CHECK", "1", 1);
        execv(argv[0], argv);
        int child_errno = errno;
        (void)write(exec_pipe[1], &child_errno, sizeof(child_errno));
        _exit(127);
    }
    close(stdout_fd);
    close(stderr_fd);
    close(exec_pipe[1]);
    int child_errno = 0;
    ssize_t exec_result;
    do {
        exec_result = read(exec_pipe[0], &child_errno, sizeof(child_errno));
    } while (exec_result < 0 && errno == EINTR);
    close(exec_pipe[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    if (exec_result > 0) {
        snprintf(error, error_size, "%s failed to start: %s", context, strerror(child_errno));
        unlink(stdout_path);
        unlink(stderr_path);
        return false;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        unlink(stdout_path);
        unlink(stderr_path);
        return true;
    }
    char details[8193];
    if (gog_read_capture(stderr_path, details, sizeof(details)) == 0u)
        (void)gog_read_capture(stdout_path, details, sizeof(details));
    unlink(stdout_path);
    unlink(stderr_path);
    if (details[0] != '\0')
        snprintf(error, error_size, "%s failed: %s", context, details);
    else if (WIFEXITED(status))
        snprintf(error, error_size, "%s failed with Some(%d)", context, WEXITSTATUS(status));
    else
        snprintf(error, error_size, "%s failed with None", context);
    return false;
}

static bool gog_write_wrapper(const char* wrapper, const char* target, char* error, size_t error_size) {
    char parent[PATH_MAX];
    int written = snprintf(parent, sizeof(parent), "%s", wrapper);
    char* slash = written >= 0 && (size_t)written < sizeof(parent) ? strrchr(parent, '/') : NULL;
    if (slash == NULL) {
        snprintf(error, error_size, "failed to write %s: invalid path", wrapper);
        return false;
    }
    *slash = '\0';
    if (!gog_mkdir_p(parent)) {
        snprintf(error, error_size, "failed to create %s: %s", parent, strerror(errno));
        return false;
    }
    FILE* file = fopen(wrapper, "wb");
    if (file == NULL) {
        snprintf(error, error_size, "failed to write %s: %s", wrapper, strerror(errno));
        return false;
    }
    if (strchr(target, '\'') != NULL) {
        fclose(file);
        snprintf(error, error_size, "failed to write %s: unsupported quote in path", wrapper);
        return false;
    }
    int result = fprintf(file, "#!/bin/sh\nexec '%s' \"$@\"\n", target);
    if (fclose(file) != 0 || result < 0) {
        snprintf(error, error_size, "failed to write %s: %s", wrapper, strerror(errno));
        return false;
    }
    if (chmod(wrapper, 0755) != 0) {
        snprintf(error, error_size, "failed to mark %s executable: %s", wrapper, strerror(errno));
        return false;
    }
    return true;
}

static bool gog_ensure_available(char* error, size_t error_size) {
    char binary[PATH_MAX];
    if (gog_find_binary(binary, sizeof(binary))) {
        gog_stage_oauth_marker();
        return true;
    }
    char tools[PATH_MAX], wrapper[PATH_MAX], venv[PATH_MAX], venv_python[PATH_MAX], venv_gogdl[PATH_MAX];
    char source[PATH_MAX], source_git[PATH_MAX];
    gog_join(tools, sizeof(tools), gog_home(), "tools");
    gog_join(wrapper, sizeof(wrapper), tools, "gogdl");
    gog_join(venv, sizeof(venv), tools, "gogdl-venv");
    gog_join(venv_python, sizeof(venv_python), venv, "bin/python");
    gog_join(venv_gogdl, sizeof(venv_gogdl), venv, "bin/gogdl");
    gog_join(source, sizeof(source), tools, "heroic-gogdl");
    gog_join(source_git, sizeof(source_git), source, ".git");
    if (gog_file(venv_gogdl)) {
        if (!gog_write_wrapper(wrapper, venv_gogdl, error, error_size))
            return false;
        if (gog_find_binary(binary, sizeof(binary)))
            return true;
    }
    if (!gog_mkdir_p(tools)) {
        snprintf(error, error_size, "failed to create GOG tools dir: %s", strerror(errno));
        return false;
    }
    static const char* python_candidates[] = {"/usr/bin/python3", "/opt/homebrew/bin/python3",
                                              "/usr/local/bin/python3"};
    static const char* git_candidates[] = {"/usr/bin/git", "/opt/homebrew/bin/git", "/usr/local/bin/git"};
    char python[PATH_MAX], git[PATH_MAX];
    if (!gog_find_command("python3", python_candidates, sizeof(python_candidates) / sizeof(python_candidates[0]),
                          python, sizeof(python))) {
        snprintf(error, error_size, "python3 is required to prepare GOG support");
        return false;
    }
    if (!gog_find_command("git", git_candidates, sizeof(git_candidates) / sizeof(git_candidates[0]), git,
                          sizeof(git))) {
        snprintf(error, error_size, "git is required to prepare GOG support");
        return false;
    }
    if (!gog_file(venv_python)) {
        char* argv[] = {python, "-m", "venv", venv, NULL};
        if (!gog_run_bootstrap(argv, "GOG support environment setup", false, error, error_size))
            return false;
    }
    if (!gog_directory(source_git)) {
        if (gog_directory(source) && !gog_remove_tree(source)) {
            snprintf(error, error_size, "failed to reset GOG support source: %s", strerror(errno));
            return false;
        }
        char* argv[] = {git,
                        "clone",
                        "--depth",
                        "1",
                        "--recurse-submodules",
                        "https://github.com/Heroic-Games-Launcher/heroic-gogdl.git",
                        source,
                        NULL};
        if (!gog_run_bootstrap(argv, "GOG support source setup", false, error, error_size))
            return false;
    } else {
        char* argv[] = {git, "-C", source, "submodule", "update", "--init", "--recursive", NULL};
        if (!gog_run_bootstrap(argv, "GOG support source refresh", false, error, error_size))
            return false;
    }
    char* pip_argv[] = {venv_python, "-m", "pip", "install", "--upgrade", source, NULL};
    if (!gog_run_bootstrap(pip_argv, "GOG support install", true, error, error_size) ||
        !gog_write_wrapper(wrapper, venv_gogdl, error, error_size))
        return false;
    char* verify_argv[] = {wrapper, "--version", NULL};
    if (!gog_run_bootstrap(verify_argv, "GOG support verification", false, error, error_size))
        return false;
    gog_stage_oauth_marker();
    return true;
}

static MetalsharpResponse* gog_status_response_with_result(bool include_games, bool ok, const char* error) {
    const char* home = gog_home();
    char gogdl[PATH_MAX], wine[PATH_MAX], prefix[PATH_MAX], drive_c[PATH_MAX], oauth_marker[PATH_MAX];
    bool gogdl_available = gog_find_binary(gogdl, sizeof(gogdl));
    (void)gog_wine_binary(wine, sizeof(wine));
    gog_join(prefix, sizeof(prefix), home, "bottles/gog-prefix/prefix");
    gog_join(drive_c, sizeof(drive_c), prefix, "drive_c");
    gog_join(oauth_marker, sizeof(oauth_marker), home, "tools/gog-oauth-helper/.inline-helper");
    bool prefix_initialized = gog_directory(drive_c);
    bool authenticated = gog_auth_nonempty();
    bool oauth_available = gog_file(oauth_marker);
    const char* status = !gogdl_available      ? "missing_gogdl"
                         : !prefix_initialized ? "needs_prefix"
                         : !authenticated      ? "needs_login"
                                               : "ready";
    jsonbuf_t body = jsonbuf_new();
    if (!body.ok)
        return make_error_response("internal error");
    (void)jsonbuf_append(&body, "{\"ok\":");
    (void)jsonbuf_append(&body, ok ? "true" : "false");
    if (!ok && error != NULL) {
        (void)jsonbuf_append(&body, ",\"error\":");
        jsonbuf_append_escaped(&body, error);
    }
    if (include_games)
        (void)jsonbuf_append(&body, ",\"games\":[],\"lastSyncAt\":null");
    (void)jsonbuf_append(&body, ",\"status\":{\"id\":\"gog\",\"name\":\"GOG\",\"status\":");
    jsonbuf_append_escaped(&body, status);
    (void)jsonbuf_appendf(&body,
                          ",\"ready\":%s,\"authUrl\":\"https://auth.gog.com/auth?client_id=46899977096215655&"
                          "redirect_uri=https%%3A%%2F%%2Fembed.gog.com%%2Fon_login_success%%3Forigin%%3Dclient&"
                          "response_type=code&layout=galaxy\",\"authenticated\":%s,\"gogdlAvailable\":%s,"
                          "\"gogdlPath\":",
                          strcmp(status, "ready") == 0 ? "true" : "false", authenticated ? "true" : "false",
                          gogdl_available ? "true" : "false");
    if (gogdl_available)
        jsonbuf_append_escaped(&body, gogdl);
    else
        (void)jsonbuf_append(&body, "null");
    (void)jsonbuf_append(&body, ",\"authConfigPath\":");
    char path[PATH_MAX];
    gog_join(path, sizeof(path), home, "gog_store/auth.json");
    jsonbuf_append_escaped(&body, path);
    (void)jsonbuf_append(&body, ",\"configPath\":");
    gog_join(path, sizeof(path), home, "gogdl");
    jsonbuf_append_escaped(&body, path);
    (void)jsonbuf_append(&body, ",\"supportPath\":");
    gog_join(path, sizeof(path), home, "gogdl/gog-support");
    jsonbuf_append_escaped(&body, path);
    (void)jsonbuf_append(&body, ",\"oauthHelperPath\":");
    gog_join(path, sizeof(path), home, "tools/gog-oauth-helper");
    jsonbuf_append_escaped(&body, path);
    (void)jsonbuf_appendf(&body,
                          ",\"oauthHelperAvailable\":%s,\"oauthHelperScript\":\"(inline Electron BrowserWindow)\","
                          "\"bottleId\":\"gog-prefix\",\"winePrefix\":",
                          oauth_available ? "true" : "false");
    jsonbuf_append_escaped(&body, prefix);
    (void)jsonbuf_appendf(&body, ",\"prefixInitialized\":%s,\"winePath\":", prefix_initialized ? "true" : "false");
    jsonbuf_append_escaped(&body, wine);
    (void)jsonbuf_append(&body, "}}");
    MetalsharpResponse* response = body.ok ? make_data_response(body.data) : make_error_response("internal error");
    jsonbuf_free(&body);
    return response;
}

static MetalsharpResponse* gog_status_response(bool include_games) {
    return gog_status_response_with_result(include_games, true, NULL);
}

static MetalsharpResponse* handle_sharp_library_gog_status(const HttpRequest* req) {
    (void)req;
    return gog_status_response(false);
}

typedef struct {
    int exit_code;
    char stdout_text[8193];
    char stderr_text[8193];
} GogCommandOutput;

static bool gog_run_capture(char* const argv[], const char* config, const char* support, GogCommandOutput* output,
                            char* error, size_t error_size) {
    char stdout_path[] = "/tmp/metalsharp-gog-command-stdout-XXXXXX";
    char stderr_path[] = "/tmp/metalsharp-gog-command-stderr-XXXXXX";
    int stdout_fd = mkstemp(stdout_path), stderr_fd = mkstemp(stderr_path);
    if (stdout_fd < 0 || stderr_fd < 0) {
        if (stdout_fd >= 0) {
            close(stdout_fd);
            unlink(stdout_path);
        }
        if (stderr_fd >= 0) {
            close(stderr_fd);
            unlink(stderr_path);
        }
        snprintf(error, error_size, "failed to run gogdl: %s", strerror(errno));
        return false;
    }
    int exec_pipe[2];
    if (pipe(exec_pipe) != 0) {
        close(stdout_fd);
        close(stderr_fd);
        unlink(stdout_path);
        unlink(stderr_path);
        snprintf(error, error_size, "failed to run gogdl: %s", strerror(errno));
        return false;
    }
    (void)fcntl(exec_pipe[1], F_SETFD, FD_CLOEXEC);
    pid_t pid = fork();
    if (pid < 0) {
        close(stdout_fd);
        close(stderr_fd);
        close(exec_pipe[0]);
        close(exec_pipe[1]);
        unlink(stdout_path);
        unlink(stderr_path);
        snprintf(error, error_size, "failed to run gogdl: %s", strerror(errno));
        return false;
    }
    if (pid == 0) {
        close(exec_pipe[0]);
        (void)dup2(stdout_fd, STDOUT_FILENO);
        (void)dup2(stderr_fd, STDERR_FILENO);
        close(stdout_fd);
        close(stderr_fd);
        if (config != NULL)
            (void)setenv("GOGDL_CONFIG_PATH", config, 1);
        if (support != NULL)
            (void)setenv("GOGDL_SUPPORT_PATH", support, 1);
        execv(argv[0], argv);
        int child_errno = errno;
        (void)write(exec_pipe[1], &child_errno, sizeof(child_errno));
        _exit(127);
    }
    close(stdout_fd);
    close(stderr_fd);
    close(exec_pipe[1]);
    int child_errno = 0;
    ssize_t exec_result;
    do {
        exec_result = read(exec_pipe[0], &child_errno, sizeof(child_errno));
    } while (exec_result < 0 && errno == EINTR);
    close(exec_pipe[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    if (exec_result > 0) {
        unlink(stdout_path);
        unlink(stderr_path);
        snprintf(error, error_size, "failed to run gogdl: %s", strerror(child_errno));
        return false;
    }
    (void)gog_read_capture(stdout_path, output->stdout_text, sizeof(output->stdout_text));
    (void)gog_read_capture(stderr_path, output->stderr_text, sizeof(output->stderr_text));
    unlink(stdout_path);
    unlink(stderr_path);
    output->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return true;
}

static char* gog_status_json(void) {
    MetalsharpResponse* response = gog_status_response(false);
    if (response == NULL)
        return NULL;
    JsonValue* status = response->data != NULL ? json_object_get((JsonValue*)response->data, "status") : NULL;
    char* encoded = status != NULL ? json_serialize(status) : NULL;
    if (response->data != NULL)
        json_free((JsonValue*)response->data);
    free(response->error_msg);
    free(response);
    return encoded;
}

static MetalsharpResponse* gog_auth_error(const char* error) {
    char* status = gog_status_json();
    if (status == NULL)
        return make_error_response("internal error");
    jsonbuf_t body = jsonbuf_new();
    (void)jsonbuf_append(&body, "{\"ok\":false,\"error\":");
    jsonbuf_append_escaped(&body, error);
    (void)jsonbuf_append(&body, ",\"status\":");
    (void)jsonbuf_append(&body, status);
    (void)jsonbuf_append(&body, "}");
    free(status);
    MetalsharpResponse* response = body.ok ? make_data_response(body.data) : make_error_response("internal error");
    jsonbuf_free(&body);
    return response;
}

static MetalsharpResponse* gog_auth_command_error(const GogCommandOutput* output) {
    char* status = gog_status_json();
    if (status == NULL)
        return make_error_response("internal error");
    jsonbuf_t body = jsonbuf_new();
    (void)jsonbuf_append(&body, "{\"ok\":false,\"error\":\"gogdl auth failed\",\"status\":");
    (void)jsonbuf_appendf(&body, "%d", output->exit_code);
    (void)jsonbuf_append(&body, ",\"stdout\":");
    jsonbuf_append_escaped(&body, output->stdout_text);
    (void)jsonbuf_append(&body, ",\"stderr\":");
    jsonbuf_append_escaped(&body, output->stderr_text);
    (void)jsonbuf_append(&body, ",\"statusInfo\":");
    (void)jsonbuf_append(&body, status);
    (void)jsonbuf_append(&body, "}");
    free(status);
    MetalsharpResponse* response = body.ok ? make_data_response(body.data) : make_error_response("internal error");
    jsonbuf_free(&body);
    return response;
}

static MetalsharpResponse* handle_sharp_library_gog_auth_code(const HttpRequest* req) {
    JsonValue* root = req != NULL ? json_parse(req->body, req->body_len, NULL) : NULL;
    const char* code_value =
        root != NULL && json_type(root) == JSON_OBJECT ? json_get_string(json_object_get(root, "code")) : NULL;
    char* code = code_value != NULL ? strdup(code_value) : NULL;
    json_free(root);
    if (code == NULL)
        return gog_auth_error("missing authorization code");

    char binary[PATH_MAX], auth_dir[PATH_MAX], auth_path[PATH_MAX], config[PATH_MAX], support[PATH_MAX];
    if (!gog_find_binary(binary, sizeof(binary))) {
        free(code);
        return gog_auth_error("gogdl binary not found; install it or set METALSHARP_GOGDL_BIN");
    }
    gog_join(auth_dir, sizeof(auth_dir), gog_home(), "gog_store");
    gog_join(auth_path, sizeof(auth_path), auth_dir, "auth.json");
    gog_join(config, sizeof(config), gog_home(), "gogdl");
    gog_join(support, sizeof(support), config, "gog-support");
    if (!gog_mkdir_p(auth_dir) || !gog_mkdir_p(config) || !gog_mkdir_p(support)) {
        free(code);
        return gog_auth_error("failed to create GOGDL config");
    }
    char* argv[] = {binary, "--auth-config-path", auth_path, "auth", "--code", code, NULL};
    GogCommandOutput output;
    char command_error[512];
    bool ran = gog_run_capture(argv, config, support, &output, command_error, sizeof(command_error));
    free(code);
    if (!ran)
        return gog_auth_error(command_error);
    if (output.exit_code != 0)
        return gog_auth_command_error(&output);
    if (output.stdout_text[0] != '\0') {
        JsonValue* result = json_parse(output.stdout_text, strlen(output.stdout_text), NULL);
        bool rejected = result != NULL && json_type(result) == JSON_OBJECT &&
                        json_get_bool(json_object_get(result, "error"), false);
        json_free(result);
        if (rejected)
            return gog_auth_error("GOG rejected the authorization code; try logging in again");
    }
    if (!gog_auth_nonempty())
        return gog_auth_error("gogdl auth did not write auth.json");
    char* status = gog_status_json();
    if (status == NULL)
        return make_error_response("internal error");
    jsonbuf_t body = jsonbuf_new();
    (void)jsonbuf_append(&body, "{\"ok\":true,\"authenticated\":true,\"status\":");
    (void)jsonbuf_append(&body, status);
    (void)jsonbuf_append(&body, "}");
    free(status);
    MetalsharpResponse* response = body.ok ? make_data_response(body.data) : make_error_response("internal error");
    jsonbuf_free(&body);
    return response;
}

static void gog_set_runtime_environment(void) {
    char value[PATH_MAX * 2u];
    int written = snprintf(value, sizeof(value), "%s/runtime/wine/lib:%s/runtime/wine/lib/wine/x86_64-unix", gog_home(),
                           gog_home());
    if (written < 0 || (size_t)written >= sizeof(value))
        return;
#if defined(__APPLE__)
    (void)setenv("DYLD_FALLBACK_LIBRARY_PATH", value, 1);
#elif defined(__linux__)
    (void)setenv("LD_LIBRARY_PATH", value, 1);
#endif
}

static MetalsharpResponse* handle_sharp_library_gog_initialize_prefix(const HttpRequest* req) {
    (void)req;
    char bootstrap_error[16384];
    if (!gog_ensure_available(bootstrap_error, sizeof(bootstrap_error)))
        return gog_status_response_with_result(false, false, bootstrap_error);

    char prefix[PATH_MAX], drive_c[PATH_MAX], wine[PATH_MAX];
    if (!gog_join(prefix, sizeof(prefix), gog_home(), "bottles/gog-prefix/prefix") || !gog_mkdir_p(prefix)) {
        char error[512];
        snprintf(error, sizeof(error), "failed to create GOG prefix: %s", strerror(errno));
        return gog_status_response_with_result(false, false, error);
    }
    if (!gog_join(drive_c, sizeof(drive_c), prefix, "drive_c"))
        return gog_status_response_with_result(false, false, "failed to create GOG prefix: File name too long");
    if (gog_directory(drive_c))
        return gog_status_response(false);
    if (!gog_wine_binary(wine, sizeof(wine))) {
        char error[PATH_MAX + 64u];
        snprintf(error, sizeof(error), "MetalSharp Wine not found: %s", wine);
        return gog_status_response_with_result(false, false, error);
    }

    int exec_pipe[2];
    if (pipe(exec_pipe) != 0) {
        char error[512];
        snprintf(error, sizeof(error), "failed to initialize GOG prefix: %s", strerror(errno));
        return gog_status_response_with_result(false, false, error);
    }
    (void)fcntl(exec_pipe[1], F_SETFD, FD_CLOEXEC);
    pid_t pid = fork();
    if (pid < 0) {
        char error[512];
        snprintf(error, sizeof(error), "failed to initialize GOG prefix: %s", strerror(errno));
        close(exec_pipe[0]);
        close(exec_pipe[1]);
        return gog_status_response_with_result(false, false, error);
    }
    if (pid == 0) {
        close(exec_pipe[0]);
        (void)setenv("WINEPREFIX", prefix, 1);
        (void)setenv("WINEMSYNC", "1", 1);
        (void)setenv("WINEDEBUG", "-all", 1);
        (void)setenv("MS_FWD_COMPAT_GL_CTX", "1", 1);
        gog_set_runtime_environment();
        int null_fd = open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            (void)dup2(null_fd, STDOUT_FILENO);
            (void)dup2(null_fd, STDERR_FILENO);
            if (null_fd > STDERR_FILENO)
                close(null_fd);
        }
        execl(wine, wine, "wineboot", "-u", (char*)NULL);
        int child_errno = errno;
        (void)write(exec_pipe[1], &child_errno, sizeof(child_errno));
        _exit(127);
    }
    close(exec_pipe[1]);
    int child_errno = 0;
    ssize_t exec_result;
    do {
        exec_result = read(exec_pipe[0], &child_errno, sizeof(child_errno));
    } while (exec_result < 0 && errno == EINTR);
    close(exec_pipe[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    if (exec_result > 0) {
        char error[512];
        snprintf(error, sizeof(error), "failed to initialize GOG prefix: %s", strerror(child_errno));
        return gog_status_response_with_result(false, false, error);
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        char error[128];
        if (WIFEXITED(status))
            snprintf(error, sizeof(error), "wineboot failed with Some(%d)", WEXITSTATUS(status));
        else
            snprintf(error, sizeof(error), "wineboot failed with None");
        return gog_status_response_with_result(false, false, error);
    }
    return gog_status_response(false);
}

static MetalsharpResponse* handle_sharp_library_gog_logout(const HttpRequest* req) {
    (void)req;
    char auth[PATH_MAX];
    if (gog_join(auth, sizeof(auth), gog_home(), "gog_store/auth.json"))
        (void)unlink(auth);
    return gog_status_response(false);
}

static void gog_stop_prefix_server(const char* prefix) {
    char wineserver[PATH_MAX];
    if (!gog_join(wineserver, sizeof(wineserver), gog_home(), "runtime/wine/bin/wineserver"))
        return;
    pid_t pid = fork();
    if (pid < 0)
        return;
    if (pid == 0) {
        (void)setenv("WINEPREFIX", prefix, 1);
        int null_fd = open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            (void)dup2(null_fd, STDOUT_FILENO);
            (void)dup2(null_fd, STDERR_FILENO);
            if (null_fd > STDERR_FILENO)
                close(null_fd);
        }
        execl(wineserver, wineserver, "-k", (char*)NULL);
        _exit(127);
    }
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
    }
}

static MetalsharpResponse* handle_sharp_library_gog_remove_prefix(const HttpRequest* req) {
    (void)req;
    char bottle[PATH_MAX], prefix[PATH_MAX];
    if (gog_join(bottle, sizeof(bottle), gog_home(), "bottles/gog-prefix") &&
        gog_join(prefix, sizeof(prefix), bottle, "prefix")) {
        if (gog_directory(bottle))
            gog_stop_prefix_server(prefix);
        (void)gog_remove_tree(bottle);
    }
    return gog_status_response(false);
}

/*
 * GET /sharp-library/gog/games — stub returning
 * {"ok":true,"games":[],"status":""}. The real implementation will
 * hit the GOG Galaxy API with the persisted token, filter the
 * catalogue to entries the user owns, and report the result. The
 * empty games array and empty status string keep the wire
 * contract stable until the GOG auth module lands.
 */
static char* gog_read_file(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length < 0 || length > 64 * 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    char* content = malloc((size_t)length + 1u);
    if (content == NULL) {
        fclose(file);
        return NULL;
    }
    size_t count = fread(content, 1u, (size_t)length, file);
    fclose(file);
    content[count] = '\0';
    return content;
}

static MetalsharpResponse* handle_sharp_library_gog_games(const HttpRequest* req) {
    (void)req;
    char directory[PATH_MAX], library[PATH_MAX];
    if (!gog_join(directory, sizeof(directory), gog_home(), "gog") ||
        !gog_join(library, sizeof(library), directory, "library.json") || !gog_mkdir_p(directory))
        return make_error_response("failed to prepare GOG library cache");
    char* content = gog_read_file(library);
    JsonValue* cache = content != NULL ? json_parse(content, strlen(content), NULL) : NULL;
    free(content);
    if (cache == NULL || json_type(cache) != JSON_OBJECT) {
        json_free(cache);
        cache = json_new_object();
        if (cache == NULL || !json_object_set_owned(cache, "games", json_new_array()) ||
            !json_object_set_owned(cache, "lastSyncAt", json_new_null())) {
            json_free(cache);
            return make_error_response("internal error");
        }
    }
    JsonValue* games = json_object_get(cache, "games");
    if (json_type(games) != JSON_ARRAY) {
        if (!json_object_set_owned(cache, "games", json_new_array())) {
            json_free(cache);
            return make_error_response("internal error");
        }
        games = json_object_get(cache, "games");
    }
    for (size_t i = 0u; i < json_array_length(games); i++)
        gog_refresh_game(json_array_get(games, i));
    JsonValue* last_sync = json_object_get(cache, "lastSyncAt");
    if (last_sync == NULL) {
        if (!json_object_set_owned(cache, "lastSyncAt", json_new_null())) {
            json_free(cache);
            return make_error_response("internal error");
        }
        last_sync = json_object_get(cache, "lastSyncAt");
    }
    char* persisted = json_serialize_pretty(cache);
    if (persisted != NULL) {
        FILE* file = fopen(library, "wb");
        if (file != NULL) {
            (void)fwrite(persisted, 1u, strlen(persisted), file);
            fclose(file);
        }
        free(persisted);
    }
    char* games_json = json_serialize(games);
    char* last_sync_json = json_serialize(last_sync);
    char* status_json = gog_status_json();
    json_free(cache);
    if (games_json == NULL || last_sync_json == NULL || status_json == NULL) {
        free(games_json);
        free(last_sync_json);
        free(status_json);
        return make_error_response("internal error");
    }
    jsonbuf_t body = jsonbuf_new();
    (void)jsonbuf_append(&body, "{\"ok\":true,\"games\":");
    (void)jsonbuf_append(&body, games_json);
    (void)jsonbuf_append(&body, ",\"status\":");
    (void)jsonbuf_append(&body, status_json);
    (void)jsonbuf_append(&body, ",\"lastSyncAt\":");
    (void)jsonbuf_append(&body, last_sync_json);
    (void)jsonbuf_append(&body, "}");
    free(games_json);
    free(last_sync_json);
    free(status_json);
    MetalsharpResponse* response = body.ok ? make_data_response(body.data) : make_error_response("internal error");
    jsonbuf_free(&body);
    return response;
}

/*
 * POST /sharp-library/gog/play — rejection stub returning
 * {"ok":true,"error":""}. The shape signals "no GOG launch
 * adapter is wired up yet" while keeping the wire contract
 * stable. A future real implementation will look up the GOG game
 * id from the request body, validate the persisted OAuth token,
 * and either strip the empty `error` field to indicate success
 * or populate it with a diagnostic.
 */
static MetalsharpResponse* handle_sharp_library_gog_play(const HttpRequest* req) {
    JsonValue* body = req != NULL ? json_parse(req->body, req->body_len, NULL) : NULL;
    const char* body_id = body != NULL ? gog_product_id(body) : NULL;
    if (body_id == NULL || body_id[0] == '\0') {
        json_free(body);
        return make_error_response("missing productId");
    }
    if (!gog_valid_token(body_id)) {
        json_free(body);
        return make_error_response("invalid productId");
    }
    char product_id[192];
    snprintf(product_id, sizeof(product_id), "%s", body_id);
    json_free(body);
    MetalsharpResponse* initialized = handle_sharp_library_gog_initialize_prefix(NULL);
    JsonValue* initialized_data = initialized != NULL && initialized->data_kind == METALSHARP_RESPONSE_JSON_VALUE
                                      ? (JsonValue*)initialized->data
                                      : NULL;
    bool initialized_ok = json_get_bool(json_object_get(initialized_data, "ok"), false);
    const char* initialized_error = json_get_string(json_object_get(initialized_data, "error"));
    char error_copy[1024];
    snprintf(error_copy, sizeof(error_copy), "%s",
             initialized_error != NULL ? initialized_error : "GOG prefix initialization failed");
    if (initialized != NULL) {
        if (initialized->data_kind == METALSHARP_RESPONSE_JSON_VALUE)
            json_free((JsonValue*)initialized->data);
        else
            free(initialized->data);
        free(initialized->error_msg);
        free(initialized->content_type);
        free(initialized);
    }
    if (!initialized_ok)
        return make_error_response(error_copy);
    char library[PATH_MAX];
    gog_join(library, sizeof(library), gog_home(), "gog/library.json");
    char* content = gog_read_file(library);
    JsonValue* cache = content != NULL ? json_parse(content, strlen(content), NULL) : NULL;
    free(content);
    JsonValue* games = json_object_get(cache, "games");
    JsonValue* game = NULL;
    for (size_t i = 0u; i < json_array_length(games); i++) {
        JsonValue* candidate = json_array_get(games, i);
        const char* id = json_get_string(json_object_get(candidate, "productId"));
        if (id != NULL && strcmp(id, product_id) == 0) {
            game = candidate;
            break;
        }
    }
    if (game == NULL) {
        json_free(cache);
        return make_error_response("game not found");
    }
    const char* folder_value = json_get_string(json_object_get(game, "gameFolder"));
    if (folder_value == NULL || folder_value[0] == '\0') {
        json_free(cache);
        return make_error_response("game is not installed or imported");
    }
    char folder[PATH_MAX], platform[32], wine[PATH_MAX], prefix[PATH_MAX];
    snprintf(folder, sizeof(folder), "%s", folder_value);
    snprintf(platform, sizeof(platform), "%s",
             json_get_string(json_object_get(game, "platform")) != NULL
                 ? json_get_string(json_object_get(game, "platform"))
                 : "windows");
    (void)gog_wine_binary(wine, sizeof(wine));
    gog_join(prefix, sizeof(prefix), gog_home(), "bottles/gog-prefix/prefix");
    char* args[] = {"launch", folder, product_id,      "--platform", platform,
                    "--wine", wine,   "--wine-prefix", prefix,       NULL};
    char log_path[PATH_MAX], spawn_error[512];
    pid_t pid = 0;
    if (!gog_spawn_logged("launch", product_id, args, log_path, sizeof(log_path), &pid, spawn_error,
                          sizeof(spawn_error))) {
        json_free(cache);
        return make_error_response(spawn_error);
    }
    bool updated = json_object_set_owned(game, "lastLaunchPid", json_new_number((double)pid)) &&
                   json_object_set_owned(game, "lastLogPath", json_new_string(log_path)) &&
                   json_object_set_owned(game, "running", json_new_bool(true)) &&
                   json_object_set_owned(game, "status", json_new_string("running")) &&
                   json_object_set_owned(game, "lastError", json_new_null());
    if (!updated || !gog_save_cache(cache, library)) {
        json_free(cache);
        return make_error_response("failed to save GOG library cache");
    }
    char* game_json = json_serialize(game);
    json_free(cache);
    if (game_json == NULL)
        return make_error_response("internal error");
    jsonbuf_t output = jsonbuf_new();
    (void)jsonbuf_appendf(&output, "{\"ok\":true,\"pid\":%ld,\"logPath\":", (long)pid);
    jsonbuf_append_escaped(&output, log_path);
    (void)jsonbuf_append(&output, ",\"winePrefix\":");
    jsonbuf_append_escaped(&output, prefix);
    (void)jsonbuf_append(&output, ",\"game\":");
    (void)jsonbuf_append(&output, game_json);
    (void)jsonbuf_append(&output, "}");
    free(game_json);
    MetalsharpResponse* response = output.ok ? make_data_response(output.data) : make_error_response("internal error");
    jsonbuf_free(&output);
    return response;
}

static bool gog_valid_token(const char* value) {
    size_t length = value != NULL ? strlen(value) : 0u;
    if (length == 0u || length > 180u)
        return false;
    for (size_t i = 0u; i < length; i++)
        if (!isalnum((unsigned char)value[i]) && value[i] != '_' && value[i] != '-' && value[i] != '.')
            return false;
    return true;
}

static const char* gog_product_id(JsonValue* body) {
    const char* id = json_get_string(json_object_get(body, "productId"));
    if (id == NULL || id[0] == '\0')
        id = json_get_string(json_object_get(body, "id"));
    return id;
}

static bool gog_save_cache(JsonValue* cache, const char* path) {
    char directory[PATH_MAX];
    int written = snprintf(directory, sizeof(directory), "%s", path);
    char* slash = written >= 0 && (size_t)written < sizeof(directory) ? strrchr(directory, '/') : NULL;
    if (slash == NULL)
        return false;
    *slash = '\0';
    if (!gog_mkdir_p(directory))
        return false;
    char* serialized = json_serialize_pretty(cache);
    if (serialized == NULL)
        return false;
    FILE* file = fopen(path, "wb");
    size_t length = strlen(serialized);
    bool ok = file != NULL && fwrite(serialized, 1u, length, file) == length;
    if (file != NULL && fclose(file) != 0)
        ok = false;
    free(serialized);
    return ok;
}

static MetalsharpResponse* handle_sharp_library_gog_progress(const HttpRequest* req) {
    JsonValue* body = req != NULL ? json_parse(req->body, req->body_len, NULL) : NULL;
    const char* body_id = body != NULL ? gog_product_id(body) : NULL;
    if (body_id == NULL || body_id[0] == '\0') {
        json_free(body);
        return make_error_response("missing productId");
    }
    if (!gog_valid_token(body_id)) {
        json_free(body);
        return make_error_response("invalid productId");
    }
    char product_id[192];
    snprintf(product_id, sizeof(product_id), "%s", body_id);
    json_free(body);
    char library[PATH_MAX];
    if (!gog_join(library, sizeof(library), gog_home(), "gog/library.json"))
        return make_error_response("game not found");
    char* content = gog_read_file(library);
    JsonValue* cache = content != NULL ? json_parse(content, strlen(content), NULL) : NULL;
    free(content);
    JsonValue* games = json_object_get(cache, "games");
    JsonValue* selected = NULL;
    for (size_t i = 0u; i < json_array_length(games); i++) {
        JsonValue* game = json_array_get(games, i);
        if (strcmp(json_get_string(json_object_get(game, "productId")) != NULL
                       ? json_get_string(json_object_get(game, "productId"))
                       : "",
                   product_id) == 0) {
            selected = game;
            break;
        }
    }
    if (selected == NULL) {
        json_free(cache);
        return make_error_response("game not found");
    }
    const char* selected_log_path = json_get_string(json_object_get(selected, "lastLogPath"));
    char log_path[PATH_MAX];
    bool has_log_path = selected_log_path != NULL &&
                        snprintf(log_path, sizeof(log_path), "%s", selected_log_path) >= 0 &&
                        strlen(selected_log_path) < sizeof(log_path);
    char* log = has_log_path ? gog_read_file(log_path) : NULL;
    double percent = json_get_bool(json_object_get(selected, "installed"), false) ? 100.0 : 0.0;
    bool has_exit_code = false;
    int exit_code = 0;
    if (log != NULL) {
        for (char* line = strtok(log, "\n"); line != NULL; line = strtok(NULL, "\n")) {
            char* progress = strstr(line, "Progress:");
            if (progress != NULL) {
                char* end = NULL;
                double parsed = strtod(progress + strlen("Progress:"), &end);
                if (end != progress + strlen("Progress:"))
                    percent = parsed;
            }
            char* exited = strstr(line, "gogdl exited with Some(");
            if (exited != NULL) {
                char* end = NULL;
                long parsed = strtol(exited + strlen("gogdl exited with Some("), &end, 10);
                if (end != exited + strlen("gogdl exited with Some(")) {
                    exit_code = (int)parsed;
                    has_exit_code = true;
                }
            }
        }
    }
    bool stale_manifest_noop = log != NULL && strstr(log, "Nothing to do") != NULL;
    free(log);
    double pid_number = json_get_number(json_object_get(selected, "lastInstallPid"), 0.0);
    bool active = pid_number > 0.0 && pid_number <= (double)INT_MAX && kill((pid_t)pid_number, 0) == 0;
    const char* game_status = json_get_string(json_object_get(selected, "status"));
    if (game_status != NULL && strcmp(game_status, "downloading") == 0 && has_exit_code) {
        bool transition = exit_code != 0;
        char transition_error[PATH_MAX + 256u];
        transition_error[0] = '\0';
        if (exit_code == 0) {
            const char* configured_root = json_get_string(json_object_get(selected, "installRoot"));
            char root[PATH_MAX], folder[PATH_MAX];
            if (configured_root != NULL)
                snprintf(root, sizeof(root), "%s", configured_root);
            else
                snprintf(root, sizeof(root), "%s/gog-games/%s", gog_home(), product_id);
            if (!gog_find_game_folder(root, product_id, folder, sizeof(folder))) {
                transition = true;
                snprintf(transition_error, sizeof(transition_error),
                         "download finished but import failed: goggame-%s.info not found under %s", product_id, root);
            } else {
                jsonbuf_t import_body = jsonbuf_new();
                (void)jsonbuf_append(&import_body, "{\"productId\":");
                jsonbuf_append_escaped(&import_body, product_id);
                (void)jsonbuf_append(&import_body, ",\"installPath\":");
                jsonbuf_append_escaped(&import_body, root);
                (void)jsonbuf_append(&import_body, "}");
                HttpRequest import_request;
                memset(&import_request, 0, sizeof(import_request));
                import_request.body = import_body.data;
                import_request.body_len = import_body.len;
                MetalsharpResponse* imported = import_body.ok ? handle_sharp_library_gog_import(&import_request) : NULL;
                JsonValue* imported_data = imported != NULL && imported->data_kind == METALSHARP_RESPONSE_JSON_VALUE
                                               ? (JsonValue*)imported->data
                                               : NULL;
                bool import_ok = json_get_bool(json_object_get(imported_data, "ok"), false);
                const char* import_error = json_get_string(json_object_get(imported_data, "error"));
                if (!import_ok) {
                    transition = true;
                    snprintf(transition_error, sizeof(transition_error), "download finished but import failed: %s",
                             import_error != NULL ? import_error : "GOG import failed");
                }
                if (imported != NULL) {
                    if (imported->data_kind == METALSHARP_RESPONSE_JSON_VALUE)
                        json_free((JsonValue*)imported->data);
                    else
                        free(imported->data);
                    free(imported->error_msg);
                    free(imported->content_type);
                    free(imported);
                }
                jsonbuf_free(&import_body);
                if (import_ok) {
                    json_free(cache);
                    char* refreshed_content = gog_read_file(library);
                    cache = refreshed_content != NULL ? json_parse(refreshed_content, strlen(refreshed_content), NULL)
                                                      : NULL;
                    free(refreshed_content);
                    games = json_object_get(cache, "games");
                    selected = NULL;
                    for (size_t i = 0u; i < json_array_length(games); i++) {
                        JsonValue* candidate = json_array_get(games, i);
                        const char* id = json_get_string(json_object_get(candidate, "productId"));
                        if (id != NULL && strcmp(id, product_id) == 0) {
                            selected = candidate;
                            break;
                        }
                    }
                    if (selected == NULL) {
                        json_free(cache);
                        return make_error_response("game not found");
                    }
                }
            }
        } else {
            snprintf(transition_error, sizeof(transition_error), "gogdl exited with Some(%d)", exit_code);
        }
        if (transition) {
            char state_root[PATH_MAX], state_path[PATH_MAX];
            if (gog_join(state_root, sizeof(state_root), gog_home(), "gogdl/gog-support") &&
                gog_join(state_path, sizeof(state_path), state_root, product_id))
                (void)gog_remove_tree(state_path);
            if (gog_join(state_root, sizeof(state_root), gog_home(), "gogdl/heroic_gogdl/manifests") &&
                gog_join(state_path, sizeof(state_path), state_root, product_id))
                (void)gog_remove_tree(state_path);
            json_object_set_owned(selected, "status",
                                  json_new_string(stale_manifest_noop ? "not_installed" : "install_failed"));
            json_object_set_owned(selected, "lastError",
                                  stale_manifest_noop ? json_new_null() : json_new_string(transition_error));
            json_object_set_owned(selected, "lastInstallPid", json_new_null());
            json_object_set_owned(selected, "lastLogPath", json_new_null());
            json_object_set_owned(selected, "primaryExe", json_new_null());
            json_object_set_owned(selected, "primaryTaskName", json_new_null());
            json_object_set_owned(selected, "gameFolder", json_new_null());
            json_object_set_owned(selected, "installed", json_new_bool(false));
            (void)gog_save_cache(cache, library);
        }
    }
    char* game_json = json_serialize(selected);
    json_free(cache);
    if (game_json == NULL)
        return make_error_response("internal error");
    jsonbuf_t output = jsonbuf_new();
    (void)jsonbuf_append(&output, "{\"ok\":true,\"productId\":");
    jsonbuf_append_escaped(&output, product_id);
    (void)jsonbuf_appendf(&output, ",\"percent\":%.15g,\"active\":%s,\"exitCode\":", percent,
                          active ? "true" : "false");
    if (has_exit_code)
        (void)jsonbuf_appendf(&output, "%d", exit_code);
    else
        (void)jsonbuf_append(&output, "null");
    (void)jsonbuf_append(&output, ",\"logPath\":");
    if (has_log_path)
        jsonbuf_append_escaped(&output, log_path);
    else
        (void)jsonbuf_append(&output, "null");
    (void)jsonbuf_append(&output, ",\"game\":");
    (void)jsonbuf_append(&output, game_json);
    (void)jsonbuf_append(&output, "}");
    free(game_json);
    MetalsharpResponse* response = output.ok ? make_data_response(output.data) : make_error_response("internal error");
    jsonbuf_free(&output);
    return response;
}

typedef struct {
    pid_t pid;
    char log_path[PATH_MAX];
} GogReaper;

static void* gog_reap_logged_child(void* value) {
    GogReaper* reaper = value;
    int status = 0;
    while (waitpid(reaper->pid, &status, 0) < 0 && errno == EINTR) {
    }
    FILE* log = fopen(reaper->log_path, "ab");
    if (log != NULL) {
        if (WIFEXITED(status))
            fprintf(log, "gogdl exited with Some(%d)\n", WEXITSTATUS(status));
        else
            fputs("gogdl exited with None\n", log);
        fclose(log);
    }
    free(reaper);
    return NULL;
}

static bool gog_spawn_logged(const char* label, const char* product_id, char* const command_args[], char* log_path,
                             size_t log_path_size, pid_t* pid_out, char* error, size_t error_size) {
    char binary[PATH_MAX], auth_dir[PATH_MAX], auth_path[PATH_MAX], config[PATH_MAX], support[PATH_MAX], logs[PATH_MAX];
    if (!gog_find_binary(binary, sizeof(binary))) {
        snprintf(error, error_size, "gogdl binary not found; install it or set METALSHARP_GOGDL_BIN");
        return false;
    }
    gog_join(auth_dir, sizeof(auth_dir), gog_home(), "gog_store");
    gog_join(auth_path, sizeof(auth_path), auth_dir, "auth.json");
    gog_join(config, sizeof(config), gog_home(), "gogdl");
    gog_join(support, sizeof(support), config, "gog-support");
    gog_join(logs, sizeof(logs), gog_home(), "logs/gog");
    if (!gog_mkdir_p(auth_dir) || !gog_mkdir_p(config) || !gog_mkdir_p(support) || !gog_mkdir_p(logs)) {
        snprintf(error, error_size, "failed to create GOG support dir: %s", strerror(errno));
        return false;
    }
    int written =
        snprintf(log_path, log_path_size, "%s/%s-%s-%lld.log", logs, label, product_id, (long long)time(NULL));
    if (written < 0 || (size_t)written >= log_path_size) {
        snprintf(error, error_size, "failed to open GOG log: File name too long");
        return false;
    }
    FILE* log = fopen(log_path, "ab");
    if (log == NULL) {
        snprintf(error, error_size, "failed to open GOG log: %s", strerror(errno));
        return false;
    }
    fprintf(log, "gogdl %s started at %lld\nargs=[", label, (long long)time(NULL));
    for (size_t i = 0u; command_args[i] != NULL; i++)
        fprintf(log, "%s\"%s\"", i == 0u ? "" : ", ", command_args[i]);
    fputs("]\n", log);
    fclose(log);
    size_t argument_count = 0u;
    while (command_args[argument_count] != NULL)
        argument_count++;
    char** argv = calloc(argument_count + 4u, sizeof(char*));
    if (argv == NULL) {
        snprintf(error, error_size, "failed to spawn gogdl: out of memory");
        return false;
    }
    argv[0] = binary;
    argv[1] = "--auth-config-path";
    argv[2] = auth_path;
    for (size_t i = 0u; i < argument_count; i++)
        argv[i + 3u] = command_args[i];
    int exec_pipe[2];
    if (pipe(exec_pipe) != 0) {
        free(argv);
        snprintf(error, error_size, "failed to spawn gogdl: %s", strerror(errno));
        return false;
    }
    (void)fcntl(exec_pipe[1], F_SETFD, FD_CLOEXEC);
    pid_t pid = fork();
    if (pid < 0) {
        free(argv);
        close(exec_pipe[0]);
        close(exec_pipe[1]);
        snprintf(error, error_size, "failed to spawn gogdl: %s", strerror(errno));
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
        (void)setenv("GOGDL_CONFIG_PATH", config, 1);
        (void)setenv("GOGDL_SUPPORT_PATH", support, 1);
        execv(binary, argv);
        int child_error = errno;
        (void)write(exec_pipe[1], &child_error, sizeof(child_error));
        _exit(127);
    }
    free(argv);
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
        snprintf(error, error_size, "failed to spawn gogdl: %s", strerror(child_error));
        return false;
    }
    GogReaper* reaper = calloc(1u, sizeof(GogReaper));
    pthread_t thread;
    if (reaper != NULL) {
        reaper->pid = pid;
        snprintf(reaper->log_path, sizeof(reaper->log_path), "%s", log_path);
        if (pthread_create(&thread, NULL, gog_reap_logged_child, reaper) == 0)
            pthread_detach(thread);
        else
            free(reaper);
    }
    *pid_out = pid;
    return true;
}

static bool gog_find_game_folder(const char* root, const char* product_id, char* output, size_t output_size) {
    char marker_name[256], marker[PATH_MAX];
    int marker_length = snprintf(marker_name, sizeof(marker_name), "goggame-%s.info", product_id);
    if (marker_length < 0 || (size_t)marker_length >= sizeof(marker_name))
        return false;
    if (gog_join(marker, sizeof(marker), root, marker_name) && gog_file(marker)) {
        int written = snprintf(output, output_size, "%s", root);
        return written >= 0 && (size_t)written < output_size;
    }
    DIR* directory = opendir(root);
    if (directory == NULL)
        return false;
    bool found = false;
    struct dirent* entry;
    while (!found && (entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char child[PATH_MAX];
        if (!gog_join(child, sizeof(child), root, entry->d_name) || !gog_directory(child) ||
            !gog_join(marker, sizeof(marker), child, marker_name) || !gog_file(marker))
            continue;
        int written = snprintf(output, output_size, "%s", child);
        found = written >= 0 && (size_t)written < output_size;
    }
    closedir(directory);
    return found;
}

static void gog_refresh_game(JsonValue* game) {
    const char* product_id = json_get_string(json_object_get(game, "productId"));
    if (product_id == NULL || product_id[0] == '\0')
        return;
    const char* configured_root = json_get_string(json_object_get(game, "installRoot"));
    char root[PATH_MAX], folder[PATH_MAX];
    if (configured_root != NULL)
        snprintf(root, sizeof(root), "%s", configured_root);
    else
        snprintf(root, sizeof(root), "%s/gog-games/%s", gog_home(), product_id);
    if (gog_find_game_folder(root, product_id, folder, sizeof(folder))) {
        char info_path[PATH_MAX];
        snprintf(info_path, sizeof(info_path), "%s/goggame-%s.info", folder, product_id);
        char* info_content = gog_read_file(info_path);
        JsonValue* info = info_content != NULL ? json_parse(info_content, strlen(info_content), NULL) : NULL;
        free(info_content);
        const char* title = json_get_string(json_object_get(info, "name"));
        if (title != NULL)
            json_object_set_owned(game, "title", json_new_string(title));
        JsonValue* tasks = json_object_get(info, "playTasks");
        JsonValue* primary = NULL;
        for (size_t i = 0u; i < json_array_length(tasks); i++) {
            JsonValue* task = json_array_get(tasks, i);
            if (primary == NULL)
                primary = task;
            if (json_get_bool(json_object_get(task, "isPrimary"), false)) {
                primary = task;
                break;
            }
        }
        const char* task_name = json_get_string(json_object_get(primary, "name"));
        const char* executable = json_get_string(json_object_get(primary, "path"));
        if (task_name != NULL)
            json_object_set_owned(game, "primaryTaskName", json_new_string(task_name));
        if (executable != NULL)
            json_object_set_owned(game, "primaryExe", json_new_string(executable));
        json_free(info);
        json_object_set_owned(game, "installRoot", json_new_string(root));
        json_object_set_owned(game, "gameFolder", json_new_string(folder));
        json_object_set_owned(game, "installed", json_new_bool(true));
        const char* status = json_get_string(json_object_get(game, "status"));
        if (status == NULL || status[0] == '\0' || strcmp(status, "not_installed") == 0 ||
            strcmp(status, "downloading") == 0)
            json_object_set_owned(game, "status", json_new_string("installed"));
    } else {
        const char* status = json_get_string(json_object_get(game, "status"));
        if (status == NULL || status[0] == '\0')
            json_object_set_owned(game, "status", json_new_string("not_installed"));
    }
    double pid = json_get_number(json_object_get(game, "lastLaunchPid"), 0.0);
    if (pid > 0.0 && pid <= (double)INT_MAX) {
        bool running = kill((pid_t)pid, 0) == 0;
        json_object_set_owned(game, "running", json_new_bool(running));
        if (running)
            json_object_set_owned(game, "status", json_new_string("running"));
        else if (json_get_bool(json_object_get(game, "installed"), false) &&
                 strcmp(json_get_string(json_object_get(game, "status")) != NULL
                            ? json_get_string(json_object_get(game, "status"))
                            : "",
                        "running") == 0)
            json_object_set_owned(game, "status", json_new_string("installed"));
    }
}

static JsonValue* gog_new_game(const char* product_id) {
    JsonValue* game = json_new_object();
    char title[256];
    snprintf(title, sizeof(title), "GOG %s", product_id);
    bool ok = game != NULL && json_object_set_owned(game, "productId", json_new_string(product_id)) &&
              json_object_set_owned(game, "title", json_new_string(title)) &&
              json_object_set_owned(game, "platform", json_new_string("windows"));
    static const char* nullable[] = {"slug",          "imageUrl",       "iconUrl",         "installRoot",
                                     "gameFolder",    "primaryExe",     "primaryTaskName", "downloadSizeBytes",
                                     "diskSizeBytes", "lastInstallPid", "lastLaunchPid",   "lastLogPath",
                                     "lastError"};
    for (size_t i = 0u; ok && i < sizeof(nullable) / sizeof(nullable[0]); i++)
        ok = json_object_set_owned(game, nullable[i], json_new_null());
    ok = ok && json_object_set_owned(game, "installed", json_new_bool(false)) &&
         json_object_set_owned(game, "running", json_new_bool(false)) &&
         json_object_set_owned(game, "status", json_new_string("not_installed"));
    if (!ok) {
        json_free(game);
        return NULL;
    }
    return game;
}

static MetalsharpResponse* handle_sharp_library_gog_install(const HttpRequest* req) {
    JsonValue* body = req != NULL ? json_parse(req->body, req->body_len, NULL) : NULL;
    const char* body_id = body != NULL ? gog_product_id(body) : NULL;
    if (body_id == NULL || body_id[0] == '\0') {
        json_free(body);
        return make_error_response("missing productId");
    }
    if (!gog_valid_token(body_id)) {
        json_free(body);
        return make_error_response("invalid productId");
    }
    char product_id[192], platform[16], install_root[PATH_MAX];
    snprintf(product_id, sizeof(product_id), "%s", body_id);
    const char* requested_platform = json_get_string(json_object_get(body, "platform"));
    if (requested_platform == NULL || requested_platform[0] == '\0')
        requested_platform = "windows";
    if (strcmp(requested_platform, "windows") != 0 && strcmp(requested_platform, "osx") != 0 &&
        strcmp(requested_platform, "linux") != 0) {
        json_free(body);
        return make_error_response("platform must be windows, osx, or linux");
    }
    snprintf(platform, sizeof(platform), "%s", requested_platform);
    const char* requested_root = json_get_string(json_object_get(body, "installPath"));
    if (requested_root != NULL && requested_root[0] != '\0')
        snprintf(install_root, sizeof(install_root), "%s", requested_root);
    else
        snprintf(install_root, sizeof(install_root), "%s/gog-games/%s", gog_home(), product_id);
    const char* requested_language = json_get_string(json_object_get(body, "language"));
    char language[128];
    bool has_language = requested_language != NULL && requested_language[0] != '\0' &&
                        snprintf(language, sizeof(language), "%s", requested_language) >= 0 &&
                        strlen(requested_language) < sizeof(language);
    json_free(body);
    if (!gog_mkdir_p(install_root)) {
        char message[512];
        snprintf(message, sizeof(message), "failed to create install path: %s", strerror(errno));
        return make_error_response(message);
    }
    char product_support[PATH_MAX];
    snprintf(product_support, sizeof(product_support), "%s/gogdl/gog-support/%s", gog_home(), product_id);
    (void)gog_mkdir_p(product_support);
    char* args[12] = {"download",  product_id,      "--platform",  platform, "--path", install_root,
                      "--support", product_support, "--with-dlcs", NULL,     NULL,     NULL};
    if (has_language) {
        args[9] = "--lang";
        args[10] = language;
    }
    char log_path[PATH_MAX], spawn_error[512];
    pid_t pid = 0;
    if (!gog_spawn_logged("download", product_id, args, log_path, sizeof(log_path), &pid, spawn_error,
                          sizeof(spawn_error)))
        return make_error_response(spawn_error);
    char library[PATH_MAX];
    gog_join(library, sizeof(library), gog_home(), "gog/library.json");
    char* content = gog_read_file(library);
    JsonValue* cache = content != NULL ? json_parse(content, strlen(content), NULL) : NULL;
    free(content);
    if (cache == NULL || json_type(cache) != JSON_OBJECT) {
        json_free(cache);
        cache = json_new_object();
        json_object_set_owned(cache, "games", json_new_array());
        json_object_set_owned(cache, "lastSyncAt", json_new_null());
    }
    JsonValue* games = json_object_get(cache, "games");
    JsonValue* game = NULL;
    for (size_t i = 0u; i < json_array_length(games); i++) {
        JsonValue* candidate = json_array_get(games, i);
        const char* id = json_get_string(json_object_get(candidate, "productId"));
        if (id != NULL && strcmp(id, product_id) == 0) {
            game = candidate;
            break;
        }
    }
    if (game == NULL) {
        game = gog_new_game(product_id);
        if (game == NULL || !json_array_append_owned(games, game)) {
            json_free(game);
            json_free(cache);
            return make_error_response("internal error");
        }
    }
    bool updated = json_object_set_owned(game, "platform", json_new_string(platform)) &&
                   json_object_set_owned(game, "installRoot", json_new_string(install_root)) &&
                   json_object_set_owned(game, "status", json_new_string("downloading")) &&
                   json_object_set_owned(game, "installed", json_new_bool(false)) &&
                   json_object_set_owned(game, "running", json_new_bool(false)) &&
                   json_object_set_owned(game, "gameFolder", json_new_null()) &&
                   json_object_set_owned(game, "primaryExe", json_new_null()) &&
                   json_object_set_owned(game, "primaryTaskName", json_new_null()) &&
                   json_object_set_owned(game, "lastInstallPid", json_new_number((double)pid)) &&
                   json_object_set_owned(game, "lastLogPath", json_new_string(log_path)) &&
                   json_object_set_owned(game, "lastError", json_new_null());
    if (!updated || !gog_save_cache(cache, library)) {
        json_free(cache);
        return make_error_response("failed to save GOG library cache");
    }
    char* game_json = json_serialize(game);
    json_free(cache);
    if (game_json == NULL)
        return make_error_response("internal error");
    jsonbuf_t output = jsonbuf_new();
    (void)jsonbuf_appendf(&output, "{\"ok\":true,\"pid\":%ld,\"logPath\":", (long)pid);
    jsonbuf_append_escaped(&output, log_path);
    (void)jsonbuf_append(&output, ",\"game\":");
    (void)jsonbuf_append(&output, game_json);
    (void)jsonbuf_append(&output, "}");
    free(game_json);
    MetalsharpResponse* response = output.ok ? make_data_response(output.data) : make_error_response("internal error");
    jsonbuf_free(&output);
    return response;
}

static MetalsharpResponse* handle_sharp_library_gog_import(const HttpRequest* req) {
    JsonValue* body = req != NULL ? json_parse(req->body, req->body_len, NULL) : NULL;
    const char* body_id = body != NULL ? gog_product_id(body) : NULL;
    if (body_id == NULL || body_id[0] == '\0') {
        json_free(body);
        return make_error_response("missing productId");
    }
    if (!gog_valid_token(body_id)) {
        json_free(body);
        return make_error_response("invalid productId");
    }
    char product_id[192], root[PATH_MAX];
    snprintf(product_id, sizeof(product_id), "%s", body_id);
    const char* install_path = json_get_string(json_object_get(body, "installPath"));
    if (install_path != NULL && install_path[0] != '\0')
        snprintf(root, sizeof(root), "%s", install_path);
    else
        snprintf(root, sizeof(root), "%s/gog-games/%s", gog_home(), product_id);
    json_free(body);
    char folder[PATH_MAX];
    if (!gog_find_game_folder(root, product_id, folder, sizeof(folder))) {
        char message[PATH_MAX + 256u];
        snprintf(message, sizeof(message), "goggame-%s.info not found under %s", product_id, root);
        return make_error_response(message);
    }
    char binary[PATH_MAX], auth_dir[PATH_MAX], auth_path[PATH_MAX], config[PATH_MAX], support[PATH_MAX];
    if (!gog_find_binary(binary, sizeof(binary)))
        return make_error_response("gogdl binary not found; install it or set METALSHARP_GOGDL_BIN");
    gog_join(auth_dir, sizeof(auth_dir), gog_home(), "gog_store");
    gog_join(auth_path, sizeof(auth_path), auth_dir, "auth.json");
    gog_join(config, sizeof(config), gog_home(), "gogdl");
    gog_join(support, sizeof(support), config, "gog-support");
    if (!gog_mkdir_p(auth_dir) || !gog_mkdir_p(config) || !gog_mkdir_p(support))
        return make_error_response("failed to create GOGDL config");
    char* argv[] = {binary, "--auth-config-path", auth_path, "import", folder, NULL};
    GogCommandOutput command;
    char command_error[512];
    if (!gog_run_capture(argv, config, support, &command, command_error, sizeof(command_error)))
        return make_error_response(command_error);
    if (command.exit_code != 0) {
        char* stderr_text = command.stderr_text;
        while (isspace((unsigned char)*stderr_text))
            stderr_text++;
        size_t length = strlen(stderr_text);
        while (length > 0u && isspace((unsigned char)stderr_text[length - 1u]))
            stderr_text[--length] = '\0';
        char message[8704];
        snprintf(message, sizeof(message), "gogdl import failed: %s", stderr_text);
        return make_error_response(message);
    }
    JsonValue* imported = command.stdout_text[0] != '\0'
                              ? json_parse(command.stdout_text, strlen(command.stdout_text), NULL)
                              : json_new_object();
    if (imported == NULL)
        imported = json_new_object();
    char library[PATH_MAX];
    gog_join(library, sizeof(library), gog_home(), "gog/library.json");
    char* cache_content = gog_read_file(library);
    JsonValue* cache = cache_content != NULL ? json_parse(cache_content, strlen(cache_content), NULL) : NULL;
    free(cache_content);
    if (cache == NULL || json_type(cache) != JSON_OBJECT) {
        json_free(cache);
        cache = json_new_object();
        json_object_set_owned(cache, "games", json_new_array());
        json_object_set_owned(cache, "lastSyncAt", json_new_null());
    }
    JsonValue* games = json_object_get(cache, "games");
    JsonValue* game = NULL;
    for (size_t i = 0u; i < json_array_length(games); i++) {
        JsonValue* candidate = json_array_get(games, i);
        const char* id = json_get_string(json_object_get(candidate, "productId"));
        if (id != NULL && strcmp(id, product_id) == 0) {
            game = candidate;
            break;
        }
    }
    if (game == NULL) {
        game = gog_new_game(product_id);
        if (game == NULL || !json_array_append_owned(games, game)) {
            json_free(game);
            json_free(imported);
            json_free(cache);
            return make_error_response("internal error");
        }
    }
    const char* title = json_get_string(json_object_get(imported, "title"));
    if (title != NULL)
        json_object_set_owned(game, "title", json_new_string(title));
    JsonValue* tasks = json_object_get(imported, "tasks");
    JsonValue* primary = NULL;
    for (size_t i = 0u; i < json_array_length(tasks); i++) {
        JsonValue* task = json_array_get(tasks, i);
        if (primary == NULL)
            primary = task;
        if (json_get_bool(json_object_get(task, "isPrimary"), false)) {
            primary = task;
            break;
        }
    }
    const char* task_name = json_get_string(json_object_get(primary, "name"));
    const char* executable = json_get_string(json_object_get(primary, "path"));
    bool updated =
        json_object_set_owned(game, "primaryTaskName",
                              task_name != NULL ? json_new_string(task_name) : json_new_null()) &&
        json_object_set_owned(game, "primaryExe", executable != NULL ? json_new_string(executable) : json_new_null()) &&
        json_object_set_owned(game, "gameFolder", json_new_string(folder)) &&
        json_object_set_owned(game, "installRoot", json_new_string(root)) &&
        json_object_set_owned(game, "installed", json_new_bool(true)) &&
        json_object_set_owned(game, "running", json_new_bool(false)) &&
        json_object_set_owned(game, "status", json_new_string("installed")) &&
        json_object_set_owned(game, "lastError", json_new_null());
    json_free(imported);
    if (!updated || !gog_save_cache(cache, library)) {
        json_free(cache);
        return make_error_response("failed to save GOG library cache");
    }
    char* game_json = json_serialize(game);
    json_free(cache);
    if (game_json == NULL)
        return make_error_response("internal error");
    jsonbuf_t output = jsonbuf_new();
    (void)jsonbuf_append(&output, "{\"ok\":true,\"game\":");
    (void)jsonbuf_append(&output, game_json);
    (void)jsonbuf_append(&output, "}");
    free(game_json);
    MetalsharpResponse* response = output.ok ? make_data_response(output.data) : make_error_response("internal error");
    jsonbuf_free(&output);
    return response;
}

static MetalsharpResponse* handle_sharp_library_gog_stop(const HttpRequest* req) {
    JsonValue* body = req != NULL ? json_parse(req->body, req->body_len, NULL) : NULL;
    const char* body_id = body != NULL ? gog_product_id(body) : NULL;
    if (body_id == NULL || body_id[0] == '\0') {
        json_free(body);
        return make_error_response("missing productId");
    }
    if (!gog_valid_token(body_id)) {
        json_free(body);
        return make_error_response("invalid productId");
    }
    char product_id[192];
    snprintf(product_id, sizeof(product_id), "%s", body_id);
    json_free(body);
    char library[PATH_MAX];
    if (!gog_join(library, sizeof(library), gog_home(), "gog/library.json"))
        return make_error_response("game not found");
    char* content = gog_read_file(library);
    JsonValue* cache = content != NULL ? json_parse(content, strlen(content), NULL) : NULL;
    free(content);
    JsonValue* games = json_object_get(cache, "games");
    JsonValue* selected = NULL;
    for (size_t i = 0u; i < json_array_length(games); i++) {
        JsonValue* game = json_array_get(games, i);
        const char* id = json_get_string(json_object_get(game, "productId"));
        if (id != NULL && strcmp(id, product_id) == 0) {
            selected = game;
            break;
        }
    }
    if (selected == NULL) {
        json_free(cache);
        return make_error_response("game not found");
    }
    double direct_pid = json_get_number(json_object_get(selected, "lastLaunchPid"), 0.0);
    if (direct_pid > 0.0 && direct_pid <= (double)INT_MAX)
        (void)kill((pid_t)direct_pid, SIGTERM);
    const char* folder_value = json_get_string(json_object_get(selected, "gameFolder"));
    char folder[PATH_MAX];
    bool has_folder = folder_value != NULL && snprintf(folder, sizeof(folder), "%s", folder_value) >= 0 &&
                      strlen(folder_value) < sizeof(folder);
    char prefix[PATH_MAX];
    (void)gog_join(prefix, sizeof(prefix), gog_home(), "bottles/gog-prefix/prefix");
    JsonValue* killed = json_new_array();
    FILE* processes = popen("/bin/ps -axo pid=,command=", "r");
    if (processes != NULL) {
        char line[8192];
        while (fgets(line, sizeof(line), processes) != NULL) {
            char* cursor = line;
            while (isspace((unsigned char)*cursor))
                cursor++;
            char* end = NULL;
            long pid = strtol(cursor, &end, 10);
            while (end != NULL && isspace((unsigned char)*end))
                end++;
            if (pid <= 0 || pid > INT_MAX || end == NULL)
                continue;
            if ((has_folder && strstr(end, folder) != NULL) || strstr(end, prefix) != NULL) {
                (void)kill((pid_t)pid, SIGTERM);
                JsonValue* value = json_new_number((double)pid);
                if (value == NULL || !json_array_append_owned(killed, value))
                    json_free(value);
            }
        }
        pclose(processes);
    }
    gog_stop_prefix_server(prefix);
    bool installed = json_get_bool(json_object_get(selected, "installed"), false);
    bool updated = json_object_set_owned(selected, "running", json_new_bool(false)) &&
                   (!installed || json_object_set_owned(selected, "status", json_new_string("installed"))) &&
                   json_object_set_owned(selected, "lastLaunchPid", json_new_null());
    if (!updated || !gog_save_cache(cache, library)) {
        json_free(killed);
        json_free(cache);
        return make_error_response("failed to save GOG library cache");
    }
    char* killed_json = json_serialize(killed);
    char* game_json = json_serialize(selected);
    json_free(killed);
    json_free(cache);
    if (killed_json == NULL || game_json == NULL) {
        free(killed_json);
        free(game_json);
        return make_error_response("internal error");
    }
    jsonbuf_t output = jsonbuf_new();
    (void)jsonbuf_append(&output, "{\"ok\":true,\"killedPids\":");
    (void)jsonbuf_append(&output, killed_json);
    (void)jsonbuf_append(&output, ",\"game\":");
    (void)jsonbuf_append(&output, game_json);
    (void)jsonbuf_append(&output, "}");
    free(killed_json);
    free(game_json);
    MetalsharpResponse* response = output.ok ? make_data_response(output.data) : make_error_response("internal error");
    jsonbuf_free(&output);
    return response;
}

static MetalsharpResponse* handle_sharp_library_gog_uninstall(const HttpRequest* req) {
    JsonValue* body = req != NULL ? json_parse(req->body, req->body_len, NULL) : NULL;
    const char* body_id = body != NULL ? gog_product_id(body) : NULL;
    if (body_id == NULL || body_id[0] == '\0') {
        json_free(body);
        return make_error_response("missing productId");
    }
    if (!gog_valid_token(body_id)) {
        json_free(body);
        return make_error_response("invalid productId");
    }
    char product_id[192];
    snprintf(product_id, sizeof(product_id), "%s", body_id);
    json_free(body);
    char library[PATH_MAX];
    if (!gog_join(library, sizeof(library), gog_home(), "gog/library.json"))
        return make_error_response("game not found");
    char* content = gog_read_file(library);
    JsonValue* cache = content != NULL ? json_parse(content, strlen(content), NULL) : NULL;
    free(content);
    JsonValue* games = json_object_get(cache, "games");
    JsonValue* selected = NULL;
    for (size_t i = 0u; i < json_array_length(games); i++) {
        JsonValue* game = json_array_get(games, i);
        const char* id = json_get_string(json_object_get(game, "productId"));
        if (id != NULL && strcmp(id, product_id) == 0) {
            selected = game;
            break;
        }
    }
    if (selected == NULL) {
        json_free(cache);
        return make_error_response("game not found");
    }
    char removed_path[PATH_MAX];
    bool has_removed_path = false;
    const char* folder = json_get_string(json_object_get(selected, "gameFolder"));
    if (folder != NULL && folder[0] != '\0') {
        char marker[PATH_MAX];
        int marker_length = snprintf(marker, sizeof(marker), "%s/goggame-%s.info", folder, product_id);
        if (marker_length >= 0 && (size_t)marker_length < sizeof(marker) && access(marker, F_OK) == 0) {
            int path_length = snprintf(removed_path, sizeof(removed_path), "%s", folder);
            has_removed_path = path_length >= 0 && (size_t)path_length < sizeof(removed_path);
        }
    }
    if (has_removed_path && !gog_remove_tree(removed_path)) {
        char message[PATH_MAX + 128u];
        snprintf(message, sizeof(message), "failed to remove %s: %s", removed_path, strerror(errno));
        json_free(cache);
        return make_error_response(message);
    }
    char state_root[PATH_MAX], state_path[PATH_MAX];
    if (gog_join(state_root, sizeof(state_root), gog_home(), "gogdl/gog-support") &&
        gog_join(state_path, sizeof(state_path), state_root, product_id))
        (void)gog_remove_tree(state_path);
    if (gog_join(state_root, sizeof(state_root), gog_home(), "gogdl/heroic_gogdl/manifests") &&
        gog_join(state_path, sizeof(state_path), state_root, product_id))
        (void)gog_remove_tree(state_path);
    bool updated = json_object_set_owned(selected, "installed", json_new_bool(false)) &&
                   json_object_set_owned(selected, "running", json_new_bool(false)) &&
                   json_object_set_owned(selected, "status", json_new_string("not_installed")) &&
                   json_object_set_owned(selected, "installRoot", json_new_null()) &&
                   json_object_set_owned(selected, "gameFolder", json_new_null()) &&
                   json_object_set_owned(selected, "primaryExe", json_new_null()) &&
                   json_object_set_owned(selected, "primaryTaskName", json_new_null()) &&
                   json_object_set_owned(selected, "lastInstallPid", json_new_null()) &&
                   json_object_set_owned(selected, "lastLaunchPid", json_new_null()) &&
                   json_object_set_owned(selected, "lastLogPath", json_new_null()) &&
                   json_object_set_owned(selected, "lastError", json_new_null());
    if (!updated || !gog_save_cache(cache, library)) {
        json_free(cache);
        return make_error_response("failed to save GOG library cache");
    }
    char* game_json = json_serialize(selected);
    json_free(cache);
    if (game_json == NULL)
        return make_error_response("internal error");
    jsonbuf_t output = jsonbuf_new();
    (void)jsonbuf_append(&output, "{\"ok\":true,\"removedPath\":");
    if (has_removed_path)
        jsonbuf_append_escaped(&output, removed_path);
    else
        (void)jsonbuf_append(&output, "null");
    (void)jsonbuf_append(&output, ",\"game\":");
    (void)jsonbuf_append(&output, game_json);
    (void)jsonbuf_append(&output, "}");
    free(game_json);
    MetalsharpResponse* response = output.ok ? make_data_response(output.data) : make_error_response("internal error");
    jsonbuf_free(&output);
    return response;
}

/*
 * Common body for the stub POST routes. Every entry below returns
 * the {"ok":true} envelope; the real implementations will be
 * filled in once the GOG auth / install / progress / stop /
 * uninstall / logout / sync / remove-prefix pipelines have been
 * ported from the legacy Node backend.
 */
static MetalsharpResponse* handle_sharp_library_stub_ok(const HttpRequest* req) {
    if (req == NULL)
        return make_error_response("invalid request");
    const char* path = req->path;
    if (strcmp(path, "/sharp-library/import-bottle-app") == 0)
        return make_error_response("bottleId and exePath required");
    if (strcmp(path, "/sharp-library/install") == 0)
        return make_error_response("srcPath required");
    if (strcmp(path, "/sharp-library/set-cover") == 0)
        return make_error_response("id and coverPath required");
    if (strcmp(path, "/sharp-library/gog/auth-code") == 0)
        return handle_sharp_library_gog_auth_code(req);
    if (strcmp(path, "/sharp-library/gog/initialize-prefix") == 0)
        return handle_sharp_library_gog_initialize_prefix(req);
    if (strcmp(path, "/sharp-library/gog/logout") == 0)
        return handle_sharp_library_gog_logout(req);
    if (strcmp(path, "/sharp-library/gog/remove-prefix") == 0)
        return handle_sharp_library_gog_remove_prefix(req);
    if (strcmp(path, "/sharp-library/gog/sync") == 0)
        return make_sharp_template_response(SHARP_SHARP_LIBRARY_GOG_SYNC_JSON);
    if (strcmp(path, "/sharp-library/gog/import") == 0 || strcmp(path, "/sharp-library/gog/install") == 0 ||
        strcmp(path, "/sharp-library/gog/progress") == 0 || strcmp(path, "/sharp-library/gog/stop") == 0 ||
        strcmp(path, "/sharp-library/gog/uninstall") == 0)
        return make_error_response("missing productId");
    return make_error_response("id required");
}

/* ── Route registration ── */

/*
 * Register every /sharp-library/\* and /sharp-library/gog/\* route
 * on the server and bind the module to `db`. NULL arguments are a
 * silent no-op so a half-initialised backend cannot crash inside
 * the registry. Every registration happens before
 * http_server_run() so workers see a fully populated route table
 * the moment the server starts.
 */
void sharp_library_register_routes(HttpServer* server, Database* db) {
    if (server == NULL || db == NULL) {
        LOG_WARN("sharp_library_register_routes called with NULL arguments");
        return;
    }
    g_sl_db = db;
    ensure_sharp_library_table(db);

    /* Game library — implementation handlers (distinct response
     * shapes or genuine SQL work). */
    http_server_register(server, "GET", "/sharp-library", handle_sharp_library_list);
    http_server_register(server, "GET", "/sharp-library/cover", handle_sharp_library_cover);

    /* Game library — launch returns the rejection shape because
     * no launch adapter is wired up yet. */
    http_server_register(server, "POST", "/sharp-library/launch", handle_sharp_library_launch);

    /* Game library — stub {"ok":true} acknowledgement for every
     * remaining POST route until the real backend adapters land.
     * Registered against the same handler so the literal payload
     * stays consistent across the entire stubbed surface. */
    http_server_register(server, "POST", "/sharp-library/install", handle_sharp_library_install);
    http_server_register(server, "POST", "/sharp-library/uninstall", handle_sharp_library_uninstall);
    http_server_register(server, "POST", "/sharp-library/set-cover", handle_sharp_library_set_cover);
    http_server_register(server, "POST", "/sharp-library/set-cover-position", handle_sharp_library_set_cover_position);
    http_server_register(server, "POST", "/sharp-library/set-engine", handle_sharp_library_set_engine);
    http_server_register(server, "POST", "/sharp-library/set-launch-args", handle_sharp_library_set_launch_args);
    http_server_register(server, "POST", "/sharp-library/doctor", handle_sharp_library_doctor);
    http_server_register(server, "POST", "/sharp-library/import-bottle-app", handle_sharp_library_import_bottle);

    /* GOG — implementation handlers (distinct response shapes). */
    http_server_register(server, "GET", "/sharp-library/gog/status", handle_sharp_library_gog_status);
    http_server_register(server, "GET", "/sharp-library/gog/games", handle_sharp_library_gog_games);

    /* GOG — play returns the rejection shape because no GOG
     * launch adapter is wired up yet. */
    http_server_register(server, "POST", "/sharp-library/gog/play", handle_sharp_library_gog_play);

    /* GOG — stub {"ok":true} acknowledgement for every remaining
     * POST route until the real backend adapters land. Registered
     * against the same handler so the literal payload stays
     * consistent across the entire stubbed surface. */
    http_server_register(server, "POST", "/sharp-library/gog/auth-code", handle_sharp_library_gog_auth_code);
    http_server_register(server, "POST", "/sharp-library/gog/import", handle_sharp_library_gog_import);
    http_server_register(server, "POST", "/sharp-library/gog/initialize-prefix",
                         handle_sharp_library_gog_initialize_prefix);
    http_server_register(server, "POST", "/sharp-library/gog/install", handle_sharp_library_gog_install);
    http_server_register(server, "POST", "/sharp-library/gog/progress", handle_sharp_library_gog_progress);
    http_server_register(server, "POST", "/sharp-library/gog/stop", handle_sharp_library_gog_stop);
    http_server_register(server, "POST", "/sharp-library/gog/uninstall", handle_sharp_library_gog_uninstall);
    http_server_register(server, "POST", "/sharp-library/gog/logout", handle_sharp_library_gog_logout);
    http_server_register(server, "POST", "/sharp-library/gog/sync", handle_sharp_library_stub_ok);
    http_server_register(server, "POST", "/sharp-library/gog/remove-prefix", handle_sharp_library_gog_remove_prefix);

    LOG_INFO("sharp-library routes registered (24)");
}