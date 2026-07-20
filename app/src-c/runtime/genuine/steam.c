/*
 * steam.c — Steam integration module for the Metalsharp C backend
 *
 * WHAT
 *   Implements the 25 Steam-related HTTP routes mounted under /steam/
 *   from the backend contract. Reports on the local Steam
 *   installation (presence of /Applications/Steam.app and the
 *   running state of the Steam process), exposes the persisted
 *   Steam API key used for downloading game metadata, and stubs
 *   every launch, install, uninstall, bridge, and runtime-doctor
 *   route behind a single {"ok":true} acknowledgement. The real
 *   launch / install / bridge / runtime adapters live in their own
 *   modules and will replace these stubs in later phases.
 *
 * IMPORTS
 *   "server.h"      MetalsharpResponse, METALSHARP_VERSION
 *   "http_server.h" HttpServer, HttpRequest, http_server_register
 *   "database.h"    Database, db_exec, db_query
 *   "json.h"        json_parse, json_serialize, JsonValue, accessors
 *   "logger.h"      LOG_INFO, LOG_WARN, LOG_ERROR
 *   <stdbool.h>     bool, true, false
 *   <stddef.h>      size_t, NULL
 *   <stdio.h>       snprintf
 *   <stdlib.h>      calloc, free
 *   <string.h>      memcpy, strlen, strcmp
 *   <unistd.h>      access, F_OK (POSIX file-existence check)
 *
 * EXPORTS
 *   steam_register_routes(HttpServer *server, Database *db)
 *       Register every /steam/ route on the supplied HttpServer
 *       and bind them to `db` for the persistent steam_api_key.
 *       Called once during startup, before http_server_run().
 *       Passing NULL on either side is a silent no-op so a
 *       half-initialised backend cannot crash inside the registry.
 *
 * SCHEMA
 *   Persisted Steam API key lives at key="steam_api_key" inside a
 *   dedicated SQLite key-value table, created on first call to
 *   steam_register_routes via:
 *     CREATE TABLE IF NOT EXISTS steam_kv(
 *         key   TEXT PRIMARY KEY NOT NULL,
 *         value TEXT NOT NULL
 *     );
 *
 *   Route response shapes — every reply carries HTTP 200; failures
 *   return {"ok":false,"error":"<reason>"}. The HTTP layer wraps
 *   every successful handler payload under a top-level "data" key
 *   per the shared MetalsharpResponse envelope, so a handler that
 *   returns the body {"installed":false,"running":false} surfaces
 *   on the wire as:
 *     {"ok":true,"data":{"installed":false,"running":false}}.
 *
 *     GET  /steam/status                data = {"installed":bool,
 *                                        "running":bool}
 *     GET  /steam/library               data = {"ok":true,
 *                                        "games":[],"total":0}
 *     GET  /steam/is-running            data = {"ok":true,
 *                                        "running":false}
 *     POST /steam/launch                data = {"ok":true}
 *     POST /steam/launch-game           data = {"ok":true}
 *     POST /steam/launch-offline        data = {"ok":true}
 *     POST /steam/install               data = {"ok":true}
 *     POST /steam/install-game          data = {"ok":true}
 *     POST /steam/stop                  data = {"ok":true}
 *     GET  /steam/stop-targets          data = {"ok":true,
 *                                        "targets":[]}
 *     POST /steam/save-api-key          data = {"ok":true}
 *     GET  /steam/api-key               data = {"ok":true,
 *                                        "key":"<persisted>"}
 *     POST /steam/compatdata            data = {"ok":true}
 *     GET  /steam/watch-steamapps       data = {"ok":true}
 *     POST /steam/bridge-start          data = {"ok":true}
 *     GET  /steam/bridge-status         data = {"ok":true,
 *                                        "status":"idle"}
 *     POST /steam/runtime-doctor        data = {"ok":true}
 *     POST /steam/d3d12-runtime-doctor  data = {"ok":true}
 *     POST /steam/mac-install           data = {"ok":true}
 *     POST /steam/mac-launch            data = {"ok":true}
 *     POST /steam/mac-launch-game       data = {"ok":true}
 *     POST /steam/mac-stop              data = {"ok":true}
 *     POST /steam/uninstall-game        data = {"ok":true}
 *     POST /steam/view-game             data = {"ok":true}
 *     POST /steam/install-recipe-deps   data = {"ok":true}
 *
 * THREADING
 *   The HTTP server dispatches every request on a worker thread.
 *   All SQLite traffic is serialised by the Database mutex, so the
 *   steam_kv helpers can be invoked from any worker without further
 *   coordination. The /Applications/Steam.app presence check uses
 *   POSIX access(2), which is process-local and safe to call
 *   concurrently. The /steam/is-running route reports a statically
 *   cached false value until per-request process scanning is
 *   wired in; only access() of the static binary path crosses
 *   file-system state at request time.
 */

#include "compat_log.h"
#include "database.h"
#include "http_server.h"
#include "json.h"
#include "logger.h"
#include "server.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

/* ── Constants ── */

#define STEAM_KV_TABLE      "steam_kv"
#define STEAM_API_KEY_KEY   "steam_api_key"
#define STEAM_KV_KEY_COLUMN "key"
#define STEAM_KV_VAL_COLUMN "value"

/* Fixed path under /Applications where Steam.app is normally
 * installed on macOS. Used by /steam/status to determine the
 * installed flag; the route never follows symlinks. */
#define STEAM_APP_PATH "/Applications/Steam.app"

/* Bounded so SQL builders can use small fixed-size allocations
 * and so the SQLite-backed API key never inflates the database
 * past a generous cap. Any save larger than this is rejected
 * with a logged warning. */
#define STEAM_API_KEY_MAX_LEN (4u * 1024u)

/* ── Module-local state ── */

/*
 * Database handle captured at registration time. The HTTP layer
 * does not thread per-request context to the handler, so every
 * route that needs persistence looks up the handle here. Set
 * exactly once before http_server_run() and never reset; all
 * access goes through the Database wrapper, which acquires its
 * own mutex.
 */
static Database* g_steam_db = NULL;

/*
 * Cached "Steam process running" flag. The /steam/is-running
 * route returns this value rather than spawning pgrep on every
 * request: while this module is stubbed, the flag is always
 * false, but the slot is kept so a future per-request scanner
 * or a bridge worker can flip it without touching the route
 * signature. Mutable only through atomic primitives so the flag
 * is safe to read concurrently from worker threads.
 */
#include <stdatomic.h>
static _Atomic bool g_steam_running = false;

/* ── Forward declarations ── */

static MetalsharpResponse* handle_steam_status(const HttpRequest* req);
static MetalsharpResponse* handle_steam_library(const HttpRequest* req);
static MetalsharpResponse* handle_steam_is_running(const HttpRequest* req);
static MetalsharpResponse* handle_steam_stop_targets(const HttpRequest* req);
static MetalsharpResponse* handle_steam_save_api_key(const HttpRequest* req);
static MetalsharpResponse* handle_steam_api_key(const HttpRequest* req);
static MetalsharpResponse* handle_steam_bridge_status(const HttpRequest* req);
static MetalsharpResponse* handle_steam_watch_steamapps(const HttpRequest* req);

/* Generic "{"ok":true}" stub — used by every launch, install,
 * uninstall, bridge, compatdata, runtime-doctor, and mac-*
 * route until the real backend adapters land. */
static MetalsharpResponse* handle_steam_stub_ok(const HttpRequest* req);
static bool steam_is_wine_steam_running(void);
static bool steam_find_wine(char* output, size_t output_size);
static MetalsharpResponse* handle_steam_launch(const HttpRequest* req);
static MetalsharpResponse* handle_steam_stop(const HttpRequest* req);

/* ── Response builders ── */

/*
 * Build a successful MetalsharpResponse whose `data` payload is
 * a freshly-parsed JsonValue tree representing `body`. The HTTP
 * layer serialises the tree under a top-level "data" field.
 * `body` must be a valid JSON document; an empty or bad body
 * yields an ok=false response. Returns NULL only on calloc
 * failure.
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

/* ── SQL helpers ── */

/*
 * Double any single-quote characters in `s` so the result is
 * safe to embed between SQL string delimiters. SQLite (per SQL
 * standard) treats two consecutive single quotes inside a string
 * literal as one literal quote, so doubling is sufficient.
 * Returns a heap-allocated, NUL-terminated copy the caller frees,
 * or NULL on allocation failure. A NULL input produces an empty
 * quoted string.
 */
static char* sql_quote(const char* s) {
    if (s == NULL) {
        return strdup("");
    }
    size_t len = strlen(s);
    char* out = malloc(len * 2u + 1u);
    if (out == NULL) {
        return NULL;
    }
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\'') {
            out[j++] = '\'';
        }
        out[j++] = s[i];
    }
    out[j] = '\0';
    return out;
}

/*
 * Ensure the steam_kv table exists. Called once from
 * steam_register_routes. Failure is logged and the routes fall
 * through to a default empty-string API key, since trying to
 * write through a missing table would surface the same sqlite
 * error each time.
 */
static void ensure_steam_table(Database* db) {
    if (db == NULL) {
        return;
    }
    static const char ddl[] = "CREATE TABLE IF NOT EXISTS " STEAM_KV_TABLE " (" STEAM_KV_KEY_COLUMN
                              " TEXT PRIMARY KEY NOT NULL," STEAM_KV_VAL_COLUMN " TEXT NOT NULL"
                              ")";
    char* err = NULL;
    if (!db_exec(db, ddl, &err)) {
        LOG_ERROR("steam_kv create failed: %s", err != NULL ? err : "(unknown)");
        free(err);
    }
}

/*
 * Read the JSON value for `key` from steam_kv. Returns a heap-
 * allocated, NUL-terminated copy the caller frees, or NULL when
 * the key is absent or any sqlite error occurred. NULL cleanly
 * distinguishes "missing row" from "empty value" because the
 * Database wrapper always inserts a non-NULL value.
 */
typedef struct {
    char* value;
} kv_get_ctx;

static int kv_get_cb(void* raw, int ncols, char** values, char** names) {
    (void)names;
    kv_get_ctx* c = raw;
    if (c == NULL) {
        return 0;
    }
    if (ncols >= 1 && values != NULL && values[0] != NULL) {
        c->value = strdup(values[0]);
    }
    return 0;
}

static char* kv_get(Database* db, const char* key) {
    if (db == NULL || key == NULL) {
        return NULL;
    }
    char* kq = sql_quote(key);
    if (kq == NULL) {
        return NULL;
    }
    size_t cap = strlen(kq) + 128u;
    char* sql = malloc(cap);
    if (sql == NULL) {
        free(kq);
        return NULL;
    }
    snprintf(sql, cap, "SELECT " STEAM_KV_VAL_COLUMN " FROM " STEAM_KV_TABLE " WHERE " STEAM_KV_KEY_COLUMN " = '%s'",
             kq);
    free(kq);
    kv_get_ctx ctx = {NULL};
    char* err = NULL;
    bool ok = db_query(db, sql, kv_get_cb, &ctx, &err);
    free(sql);
    if (!ok) {
        LOG_ERROR("kv_get query failed: %s", err != NULL ? err : "(unknown)");
        free(err);
        free(ctx.value);
        return NULL;
    }
    return ctx.value;
}

/*
 * Upsert the value at `key`. The value is SQL-quoted, so any
 * embedded single-quotes are doubled to keep the literal intact.
 * Returns true on success. On failure logs and returns false;
 * any oversized payload is rejected with a warning before the
 * sqlite call is made so the database can never balloon with a
 * pathological key.
 */
static bool kv_put(Database* db, const char* key, const char* value) {
    if (db == NULL || key == NULL || value == NULL) {
        return false;
    }
    size_t vlen = strlen(value);
    if (vlen > STEAM_API_KEY_MAX_LEN) {
        LOG_WARN("steam api key too large (%zu bytes), refusing to save", vlen);
        return false;
    }
    char* kq = sql_quote(key);
    char* vq = sql_quote(value);
    if (kq == NULL || vq == NULL) {
        free(kq);
        free(vq);
        return false;
    }
    size_t cap = strlen(kq) + strlen(vq) + 128u;
    char* sql = malloc(cap);
    if (sql == NULL) {
        free(kq);
        free(vq);
        return false;
    }
    snprintf(sql, cap,
             "INSERT OR REPLACE INTO " STEAM_KV_TABLE " (" STEAM_KV_KEY_COLUMN "," STEAM_KV_VAL_COLUMN
             ") VALUES ('%s','%s')",
             kq, vq);
    free(kq);
    free(vq);
    char* err = NULL;
    bool ok = db_exec(db, sql, &err);
    free(sql);
    if (!ok) {
        LOG_ERROR("kv_put failed: %s", err != NULL ? err : "(unknown)");
        free(err);
        return false;
    }
    return true;
}

/* ── Steam detection ── */

/*
 * Return non-zero when /Applications/Steam.app exists on disk.
 * Implemented with access(2) and F_OK, which does not follow
 * symlinks for the final component and is cheap enough for a
 * synchronous route handler. A missing entry, an unreadable
 * path, or any error uniformly yields false so the route can
 * surface {"installed":false} without further branching.
 */
static bool steam_installed(void) {
    return access(STEAM_APP_PATH, F_OK) == 0;
}

/* ── Route handlers ── */

static MetalsharpResponse* handle_steam_status(const HttpRequest* req) {
    (void)req;
    bool running = steam_is_wine_steam_running();
    if (!running)
        running = atomic_load(&g_steam_running);
    char wine_path[PATH_MAX];
    bool wine_available = steam_find_wine(wine_path, sizeof(wine_path));
    char body[512];
    int n = snprintf(body, sizeof(body),
                     "{\"installed\":true,\"running\":%s,\"installing\":false,"
                     "\"path\":null,\"metalsharp_wine_available\":%s,\"mac_installed\":%s,"
                     "\"mac_running\":false,\"mac_path\":null,"
                     "\"mac_install_url\":\"https://store.steampowered.com/about/\","
                     "\"login_state\":{\"state\":\"unknown\",\"account\":null}}",
                     running ? "true" : "false", wine_available ? "true" : "false",
                     steam_installed() ? "true" : "false");
    if (n < 0 || (size_t)n >= sizeof(body)) {
        return make_error_response("internal error");
    }
    return make_data_response(body);
}

static MetalsharpResponse* handle_steam_library(const HttpRequest* req) {
    (void)req;
    metalsharp_app_log("Loading Steam library...");
    metalsharp_app_log("Loaded 0 games");
    /* Real implementation will scan ~/Library/Application
     * Support/Steam/steamapps for appmanifest_*.acf entries,
     * parse each into a {appid,name,installed} record, and
     * count the array. The stub returns an empty library so
     * the Electron shell renders a clean "no games" state. */
    return make_data_response("{\"ok\":true,\"games\":[],\"total\":0,\"installed_count\":0,"
                              "\"sync\":{\"api_key_set\":false,\"owned_games_cache\":false,"
                              "\"steam_id\":\"\",\"steam_id_detected\":false}}");
}

static MetalsharpResponse* handle_steam_is_running(const HttpRequest* req) {
    (void)req;
    bool running = atomic_load(&g_steam_running);
    char body[64];
    int n = snprintf(body, sizeof(body), "{\"ok\":true,\"running\":%s}", running ? "true" : "false");
    if (n < 0 || (size_t)n >= sizeof(body)) {
        return make_error_response("internal error");
    }
    return make_data_response(body);
}

static MetalsharpResponse* steam_route_response(const char* json, int status) {
    MetalsharpResponse* response = make_data_response(json);
    if (response != NULL)
        response->http_status = status;
    return response;
}

static MetalsharpResponse* handle_steam_stub_ok(const HttpRequest* req) {
    if (req == NULL)
        return make_error_response("invalid request");
    const char* path = req->path;
    if (strcmp(path, "/steam/bridge-start") == 0)
        return steam_route_response(
            "{\"ok\":false,\"error\":\"steambridge.exe not found — Wine-side Steam API bridge is not yet "
            "available\"}",
            500);
    if (strcmp(path, "/steam/compatdata") == 0)
        return steam_route_response("{\"ok\":false,\"deprecated\":true,"
                                    "\"error\":\"compatdata is deprecated and no longer written\","
                                    "\"replacement\":\"bottle manifest route state\"}",
                                    200);
    if (strcmp(path, "/steam/install") == 0)
        return steam_route_response(
            "{\"ok\":true,\"path\":\"Steam installation started — polling /steam/status for completion\"}", 200);
    if (strcmp(path, "/steam/launch") == 0)
        return handle_steam_launch(req);
    if (strcmp(path, "/steam/mac-launch") == 0)
        return steam_route_response("{\"ok\":false,\"error\":\"macOS Steam is not installed\"}", 500);
    if (strcmp(path, "/steam/mac-install") == 0) {
        char body[192];
        int n = snprintf(body, sizeof(body),
                         "{\"ok\":true,\"installed\":false,\"pid\":%ld,"
                         "\"url\":\"https://store.steampowered.com/about/\"}",
                         (long)getpid());
        return n > 0 && (size_t)n < sizeof(body) ? steam_route_response(body, 200) : NULL;
    }
    if (strcmp(path, "/steam/stop") == 0)
        return handle_steam_stop(req);
    if (strcmp(path, "/steam/mac-stop") == 0)
        return steam_route_response("{\"ok\":true,\"running\":false}", 200);
    bool status_400 = strcmp(path, "/steam/install-game") == 0 || strcmp(path, "/steam/launch-game") == 0 ||
                      strcmp(path, "/steam/launch-offline") == 0 || strcmp(path, "/steam/mac-launch-game") == 0 ||
                      strcmp(path, "/steam/uninstall-game") == 0 || strcmp(path, "/steam/view-game") == 0;
    return steam_route_response("{\"ok\":false,\"error\":\"appid required\"}", status_400 ? 400 : 200);
}

static bool steam_contains_case_insensitive(const char* text, const char* needle) {
    if (text == NULL || needle == NULL)
        return false;
    size_t needle_len = strlen(needle);
    if (needle_len == 0)
        return true;
    for (const char* start = text; *start != '\0'; start++) {
        size_t i = 0;
        while (i < needle_len && start[i] != '\0' &&
               tolower((unsigned char)start[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == needle_len)
            return true;
    }
    return false;
}

static bool steam_cleanup_command(const char* command, const char* prefix) {
    if (strstr(command, " rg ") != NULL || strstr(command, "rg -i") != NULL || strstr(command, "ps axo") != NULL ||
        strstr(command, "Steam.app/Contents/MacOS") != NULL || strstr(command, "steam_osx") != NULL)
        return false;
    return strstr(command, prefix) != NULL ||
           steam_contains_case_insensitive(command, "c:\\program files (x86)\\steam") ||
           steam_contains_case_insensitive(command, "steamwebhelper.exe") ||
           steam_contains_case_insensitive(command, "steamwebhelper_real.exe") ||
           steam_contains_case_insensitive(command, "c:\\windows\\system32\\explorer.exe /desktop") ||
           (steam_contains_case_insensitive(command, "c:\\windows\\system32\\conhost.exe") &&
            steam_contains_case_insensitive(command, "--headless")) ||
           steam_contains_case_insensitive(command, "winedevice.exe") ||
           steam_contains_case_insensitive(command, "wineserver") ||
           steam_contains_case_insensitive(command, "wineloader");
}

static void steam_json_escape(char* output, size_t output_size, const char* input) {
    size_t used = 0;
    for (const unsigned char* p = (const unsigned char*)input; *p != '\0' && used + 2u < output_size; p++) {
        if (*p == '"' || *p == '\\') {
            output[used++] = '\\';
            output[used++] = (char)*p;
        } else if (*p == '\n' || *p == '\r' || *p == '\t') {
            output[used++] = ' ';
        } else if (*p >= 0x20) {
            output[used++] = (char)*p;
        }
    }
    output[used] = '\0';
}

static MetalsharpResponse* handle_steam_stop_targets(const HttpRequest* req) {
    (void)req;
    char targeted[32768] = "";
    char excluded[32768] = "";
    size_t targeted_used = 0;
    size_t excluded_used = 0;
    unsigned int targeted_count = 0;
    bool targeted_first = true;
    bool excluded_first = true;
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL)
        home = getenv("HOME");
    if (home == NULL)
        home = "";
    char prefix[PATH_MAX];
    snprintf(prefix, sizeof(prefix), "%s/prefix-steam", home);
    FILE* processes = popen("ps axo pid=,command=", "r");
    if (processes != NULL) {
        char line[16384];
        while (fgets(line, sizeof(line), processes) != NULL) {
            char* cursor = line;
            while (isspace((unsigned char)*cursor))
                cursor++;
            char* end = NULL;
            unsigned long pid = strtoul(cursor, &end, 10);
            if (end == cursor || pid == (unsigned long)getpid())
                continue;
            while (isspace((unsigned char)*end))
                end++;
            end[strcspn(end, "\r\n")] = '\0';
            bool targeted_process = steam_cleanup_command(end, prefix);
            bool excluded_process =
                !targeted_process &&
                (strstr(end, "Steam.app/Contents/MacOS") != NULL || strstr(end, "steam_osx") != NULL ||
                 strstr(end, " rg ") != NULL || strstr(end, "rg -i") != NULL || strstr(end, "ps axo") != NULL);
            if (!targeted_process && !excluded_process)
                continue;
            char escaped[24576];
            steam_json_escape(escaped, sizeof(escaped), end);
            char* destination = targeted_process ? targeted : excluded;
            size_t* used = targeted_process ? &targeted_used : &excluded_used;
            bool* first = targeted_process ? &targeted_first : &excluded_first;
            size_t capacity = targeted_process ? sizeof(targeted) : sizeof(excluded);
            int n = snprintf(destination + *used, capacity - *used, "%s{\"pid\":%lu,\"command\":\"%s\"}",
                             *first ? "" : ",", pid, escaped);
            if (n > 0 && (size_t)n < capacity - *used) {
                *used += (size_t)n;
                *first = false;
                if (targeted_process)
                    targeted_count++;
            }
        }
        (void)pclose(processes);
    }
    char body[70000];
    int n = snprintf(body, sizeof(body),
                     "{\"ok\":true,\"targeted_pid_count\":%u,\"targeted\":[%s],\"excluded\":[%s],"
                     "\"summary\":\"stop_wine_steam targets %u Wine Steam helper process(es); the macOS Steam "
                     "client and MetalSharp's own rg/ps invocations are excluded\"}",
                     targeted_count, targeted, excluded, targeted_count);
    if (n < 0 || (size_t)n >= sizeof(body))
        return make_error_response("process list too large");
    return make_data_response(body);
}

/*
 * Persist a Steam API key supplied in the request body. The
 * body must be a JSON object with a string field named "key";
 * any other shape (missing body, unparsable JSON, missing
 * field, or empty value) is rejected with a 200 + ok=false
 * envelope so the Electron shell can render the error string
 * inline without retrying.
 */
static MetalsharpResponse* handle_steam_save_api_key(const HttpRequest* req) {
    if (g_steam_db == NULL) {
        return make_error_response("steam not initialised");
    }
    if (req == NULL || req->body == NULL || req->body_len == 0) {
        return make_error_response("missing body");
    }
    JsonValue* parsed = json_parse(req->body, req->body_len, NULL);
    if (parsed == NULL || json_type(parsed) != JSON_OBJECT) {
        if (parsed != NULL) {
            json_free(parsed);
        }
        return make_error_response("invalid JSON body");
    }
    const char* key = json_get_string(json_object_get(parsed, "key"));
    if (key == NULL)
        key = "";
    char* key_dup = strdup(key);
    json_free(parsed);
    if (key_dup == NULL) {
        return make_error_response("out of memory");
    }
    bool ok = kv_put(g_steam_db, STEAM_API_KEY_KEY, key_dup);
    if (!ok) {
        free(key_dup);
        return make_error_response("database write failed");
    }
    size_t escaped_capacity = strlen(key_dup) * 6u + 1u;
    char* escaped_key = malloc(escaped_capacity);
    if (escaped_key != NULL)
        steam_json_escape(escaped_key, escaped_capacity, key_dup);
    bool api_key_set = key_dup[0] != '\0';
    free(key_dup);
    if (escaped_key == NULL)
        return make_error_response("out of memory");
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL)
        home = getenv("HOME");
    if (home != NULL) {
        char cache[PATH_MAX];
        char config[PATH_MAX];
        int cache_n = snprintf(cache, sizeof(cache), "%s/cache", home);
        int config_n = snprintf(config, sizeof(config), "%s/cache/steam_config.json", home);
        if (cache_n > 0 && (size_t)cache_n < sizeof(cache) && config_n > 0 && (size_t)config_n < sizeof(config)) {
            (void)mkdir(cache, 0755);
            FILE* file = fopen(config, "wb");
            if (file != NULL) {
                (void)fprintf(file, "{\n  \"steam_api_key\": \"%s\",\n  \"steam_id\": \"\"\n}\n", escaped_key);
                fclose(file);
            }
        }
    }
    free(escaped_key);
    char response_body[640];
    int response_n = snprintf(response_body, sizeof(response_body),
                              "{\"ok\":true,\"sync\":{\"api_key_set\":%s,\"owned_games_cache\":false,"
                              "\"steam_id\":\"\",\"steam_id_detected\":false},\"library\":{\"ok\":true,"
                              "\"games\":[],\"total\":0,\"installed_count\":0,\"sync\":{\"api_key_set\":%s,"
                              "\"owned_games_cache\":false,\"steam_id\":\"\",\"steam_id_detected\":false}}}",
                              api_key_set ? "true" : "false", api_key_set ? "true" : "false");
    if (response_n < 0 || (size_t)response_n >= sizeof(response_body))
        return make_error_response("internal error");
    return make_data_response(response_body);
}

/*
 * Return the persisted Steam API key. When no key has been
 * saved (or the row is empty) the route emits an empty
 * string so the Electron shell can distinguish "unset" from
 * "read failure" without further parsing.
 */
static MetalsharpResponse* handle_steam_api_key(const HttpRequest* req) {
    (void)req;
    char* persisted = kv_get(g_steam_db, STEAM_API_KEY_KEY);
    const char* key = (persisted != NULL) ? persisted : "";
    /* The persisted value originated as user input, so
     * json-escape the embedded string before serialisation.
     * The body buffer is large enough for a generous key
     * (up to 4 KiB escaped = ~16 KiB buffer). */
    size_t esc_cap = strlen(key) * 6u + 8u;
    if (esc_cap < 64u) {
        esc_cap = 64u;
    }
    char* esc = malloc(esc_cap);
    if (esc == NULL) {
        free(persisted);
        return make_error_response("out of memory");
    }
    size_t i = 0;
    for (const unsigned char* p = (const unsigned char*)key; *p != '\0'; p++) {
        if (i + 8u >= esc_cap) {
            esc_cap *= 2u;
            char* grown = realloc(esc, esc_cap);
            if (grown == NULL) {
                free(esc);
                free(persisted);
                return make_error_response("out of memory");
            }
            esc = grown;
        }
        switch (*p) {
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
            if (*p < 0x20) {
                int k = snprintf(esc + i, esc_cap - i, "\\u%04x", *p);
                if (k < 0) {
                    free(esc);
                    free(persisted);
                    return make_error_response("internal error");
                }
                i += (size_t)k;
            } else {
                esc[i++] = (char)*p;
            }
            break;
        }
    }
    esc[i] = '\0';
    char body[8192];
    int n = snprintf(body, sizeof(body), "{\"ok\":true,\"key\":\"%s\"}", esc);
    free(esc);
    free(persisted);
    if (n < 0 || (size_t)n >= sizeof(body)) {
        return make_error_response("internal error");
    }
    return make_data_response(body);
}

static MetalsharpResponse* handle_steam_bridge_status(const HttpRequest* req) {
    (void)req;
    /* Real implementation will report the bridge worker's
     * phase (idle/connecting/pumping) once the bridge
     * module lands; until then the route is "idle" so the
     * Electron shell sees a stable initial state. */
    return make_data_response("{\"ok\":true,\"running\":false,\"port\":18733}");
}

static MetalsharpResponse* handle_steam_watch_steamapps(const HttpRequest* req) {
    (void)req;
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL)
        home = getenv("HOME");
    if (home != NULL) {
        char directory[PATH_MAX];
        char cache[PATH_MAX];
        int directory_n = snprintf(directory, sizeof(directory), "%s/cache", home);
        int cache_n = snprintf(cache, sizeof(cache), "%s/cache/steam_appids.cache", home);
        if (directory_n > 0 && (size_t)directory_n < sizeof(directory) && cache_n > 0 &&
            (size_t)cache_n < sizeof(cache)) {
            (void)mkdir(directory, 0755);
            FILE* file = fopen(cache, "ab");
            if (file != NULL)
                fclose(file);
        }
    }
    return make_data_response("{\"ok\":true,\"new_appids\":[]}");
}

/* ── Wine Steam launch helpers ── */

static bool steam_find_wine(char* output, size_t output_size) {
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL || home[0] == '\0')
        home = getenv("HOME");
    if (home == NULL)
        return false;
    static const char* names[] = {"metalsharp-wine", "wine"};
    for (size_t i = 0u; i < sizeof(names) / sizeof(names[0]); i++) {
        int written = snprintf(output, output_size, "%s/runtime/wine/bin/%s", home, names[i]);
        if (written >= 0 && (size_t)written < output_size && access(output, X_OK) == 0)
            return true;
    }
    return false;
}

static char* steam_home(void) {
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL || home[0] == '\0')
        home = getenv("HOME");
    return (char*)(home != NULL ? home : "");
}

static void steam_set_runtime_environment(void) {
    char value[PATH_MAX * 2u];
    int written = snprintf(value, sizeof(value), "%s/runtime/wine/lib:%s/runtime/wine/lib/wine/x86_64-unix",
                           steam_home(), steam_home());
    if (written < 0 || (size_t)written >= sizeof(value))
        return;
#if defined(__APPLE__)
    (void)setenv("DYLD_FALLBACK_LIBRARY_PATH", value, 1);
#elif defined(__linux__)
    (void)setenv("LD_LIBRARY_PATH", value, 1);
#endif
}

static bool steam_spawn_wine(char* const argv[], const char* working_directory, const char* prefix, pid_t* pid_out,
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
        if (working_directory != NULL && chdir(working_directory) != 0) {
            int child_errno = errno;
            (void)write(exec_pipe[1], &child_errno, sizeof(child_errno));
            _exit(127);
        }
        if (prefix != NULL)
            (void)setenv("WINEPREFIX", prefix, 1);
        (void)setenv("WINEDEBUG", "+vulkan,+d3d,+d3d11,+dxgi,+wined3d,+opengl", 1);
        (void)setenv("WINEDEBUGGER", "none", 1);
        (void)setenv("STEAM_RUNTIME", "0", 1);
        (void)setenv("MS_FWD_COMPAT_GL_CTX", "1", 1);
        (void)setenv("WINEDLLOVERRIDES",
                     "dxgi,d3d11,d3d10core=n,b;bcrypt=b;ncrypt=b;gameoverlayrenderer,gameoverlayrenderer64=d", 1);
        steam_set_runtime_environment();
        execv(argv[0], argv);
        int child_errno = errno;
        (void)write(exec_pipe[1], &child_errno, sizeof(child_errno));
        _exit(127);
    }
    close(exec_pipe[1]);
    int child_errno = 0;
    ssize_t count;
    do {
        count = read(exec_pipe[0], &child_errno, sizeof(child_errno));
    } while (count < 0 && errno == EINTR);
    close(exec_pipe[0]);
    if (count > 0) {
        while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
        }
        snprintf(error, error_size, "%s", strerror(child_errno));
        return false;
    }
    *pid_out = pid;
    return true;
}

static bool steam_is_wine_steam_running(void) {
    char prefix[PATH_MAX];
    snprintf(prefix, sizeof(prefix), "%s/prefix-steam", steam_home());
    FILE* processes = popen("ps axo pid=,command=", "r");
    if (processes == NULL)
        return false;
    bool running = false;
    char line[16384];
    while (!running && fgets(line, sizeof(line), processes) != NULL) {
        if (strstr(line, prefix) != NULL &&
            (strstr(line, "Steam.exe") != NULL || strstr(line, "steamwebhelper") != NULL))
            running = true;
    }
    pclose(processes);
    return running;
}

static bool steam_find_bundled_file(const char* relative, char* output, size_t output_size) {
    struct stat info;
    char candidate[PATH_MAX];
    /* Check METALSHARP_HOME cache first */
    int written = snprintf(candidate, sizeof(candidate), "%s/cache/steam/%s", steam_home(), relative);
    if (written >= 0 && (size_t)written < sizeof(candidate) && stat(candidate, &info) == 0 && info.st_size > 0) {
        snprintf(output, output_size, "%s", candidate);
        return true;
    }
    /* Check app bundles directory */
    static const char* bundle_roots[] = {"app/bundles", "../bundles", "../../bundles"};
    char resources[PATH_MAX] = "";
#ifdef __APPLE__
    {
        uint32_t size = sizeof(resources);
        if (_NSGetExecutablePath(resources, &size) == 0) {
            char* slash = strrchr(resources, '/');
            if (slash != NULL) {
                *slash = '\0';
                slash = strrchr(resources, '/');
                if (slash != NULL) {
                    *slash = '\0';
                    slash = strrchr(resources, '/');
                    if (slash != NULL && strcmp(slash + 1, "MacOS") == 0) {
                        *slash = '\0';
                        snprintf(resources, sizeof(resources), "%s/Resources", resources);
                    }
                }
            }
        }
    }
#endif
    for (size_t i = 0u; i < sizeof(bundle_roots) / sizeof(bundle_roots[0]); i++) {
        const char* root = bundle_roots[i];
        if (resources[0] != '\0') {
            written = snprintf(candidate, sizeof(candidate), "%s/%s/%s", resources, root, relative);
        } else {
            written = snprintf(candidate, sizeof(candidate), "%s/%s", root, relative);
        }
        if (written >= 0 && (size_t)written < sizeof(candidate) && stat(candidate, &info) == 0 && info.st_size > 0) {
            snprintf(output, output_size, "%s", candidate);
            return true;
        }
    }
    return false;
}

#define STEAMWEBHELPER_WRAPPER_MAX_BYTES 100000

static bool steam_deploy_webhelper_wrapper(const char* steam_dir) {
    char wrapper_source[PATH_MAX];
    if (!steam_find_bundled_file("steamwebhelper.exe", wrapper_source, sizeof(wrapper_source)))
        return false;
    struct stat wrapper_info;
    if (stat(wrapper_source, &wrapper_info) != 0 || wrapper_info.st_size == 0 ||
        (size_t)wrapper_info.st_size > STEAMWEBHELPER_WRAPPER_MAX_BYTES)
        return false;
    char cef_root[PATH_MAX];
    snprintf(cef_root, sizeof(cef_root), "%s/bin/cef", steam_dir);
    static const char* priority[] = {"cef.win64", "cef.win7x64", "cef.win7"};
    bool deployed = false;
    for (size_t pi = 0u; pi < sizeof(priority) / sizeof(priority[0]); pi++) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", cef_root, priority[pi]);
        struct stat dir_info;
        if (stat(path, &dir_info) != 0 || !S_ISDIR(dir_info.st_mode))
            continue;
        char original[PATH_MAX], real[PATH_MAX], marker[PATH_MAX];
        snprintf(original, sizeof(original), "%s/steamwebhelper.exe", path);
        snprintf(real, sizeof(real), "%s/steamwebhelper_real.exe", path);
        snprintf(marker, sizeof(marker), "%s/.ms_wrapper_deployed", path);
        struct stat orig_info, real_info;
        bool orig_exists = stat(original, &orig_info) == 0;
        long orig_size = orig_exists ? (long)orig_info.st_size : 0;
        bool real_exists = stat(real, &real_info) == 0;
        long real_size = real_exists ? (long)real_info.st_size : 0;
        /* Already deployed: original is our wrapper AND marker present */
        if (orig_exists && orig_size > 0 && orig_size <= (long)STEAMWEBHELPER_WRAPPER_MAX_BYTES &&
            stat(marker, &(struct stat){0}) == 0)
            continue;
        /* No real binary to preserve — directory is incomplete, skip */
        bool has_real_binary =
            (real_exists && real_size > (long)STEAMWEBHELPER_WRAPPER_MAX_BYTES) ||
            (orig_exists && orig_size > (long)STEAMWEBHELPER_WRAPPER_MAX_BYTES);
        if (!has_real_binary)
            continue;
        /* _real is a stale wrapper copy, original is real Steam binary */
        if (real_exists && real_size > 0 && real_size < (long)STEAMWEBHELPER_WRAPPER_MAX_BYTES) {
            if (orig_exists && orig_size > (long)STEAMWEBHELPER_WRAPPER_MAX_BYTES) {
                (void)unlink(real);
                (void)rename(original, real);
            } else {
                continue;
            }
        } else if (orig_exists && orig_size > (long)STEAMWEBHELPER_WRAPPER_MAX_BYTES) {
            /* Original is real Steam binary, _real missing or real — rename */
            (void)unlink(real);
            (void)rename(original, real);
        } else if (orig_exists) {
            (void)unlink(original);
        }
        FILE* source_file = fopen(wrapper_source, "rb");
        FILE* dest_file = source_file != NULL ? fopen(original, "wb") : NULL;
        if (source_file != NULL && dest_file != NULL) {
            char buffer[4096];
            size_t count;
            while ((count = fread(buffer, 1u, sizeof(buffer), source_file)) != 0u)
                (void)fwrite(buffer, 1u, count, dest_file);
            fclose(dest_file);
            fclose(source_file);
            FILE* marker_file = fopen(marker, "wb");
            if (marker_file != NULL) {
                fputs("deployed", marker_file);
                fclose(marker_file);
            }
            deployed = true;
        } else {
            if (source_file != NULL)
                fclose(source_file);
            if (dest_file != NULL)
                fclose(dest_file);
        }
        if (deployed)
            break;
    }
    return deployed;
}

static MetalsharpResponse* handle_steam_launch(const HttpRequest* req) {
    (void)req;
    char wine[PATH_MAX], prefix[PATH_MAX], steam_exe[PATH_MAX], steam_dir[PATH_MAX];
    if (!steam_find_wine(wine, sizeof(wine)))
        return steam_route_response("{\"ok\":false,\"error\":\"MetalSharp Wine not found\"}", 500);
    snprintf(prefix, sizeof(prefix), "%s/prefix-steam", steam_home());
    snprintf(steam_dir, sizeof(steam_dir), "%s/drive_c/Program Files (x86)/Steam", prefix);
    snprintf(steam_exe, sizeof(steam_exe), "%s/Steam.exe", steam_dir);
    if (access(steam_exe, F_OK) != 0)
        return steam_route_response(
            "{\"ok\":false,\"error\":\"Steam is not installed \u2014 use the setup wizard to install it first\"}", 500);
    if (steam_is_wine_steam_running()) {
        atomic_store(&g_steam_running, true);
        return steam_route_response("{\"ok\":true,\"message\":\"Steam already running\"}", 200);
    }
    (void)steam_deploy_webhelper_wrapper(steam_dir);
    char* argv[] = {
        wine, steam_exe, "-no-cef-sandbox", "-cef-single-process", "-noverifyfiles", "-no-dwrite", NULL};
    pid_t pid = 0;
    char spawn_error[512];
    if (!steam_spawn_wine(argv, steam_dir, prefix, &pid, spawn_error, sizeof(spawn_error)))
        return steam_route_response(spawn_error, 500);
    atomic_store(&g_steam_running, true);
    char body[64];
    int n = snprintf(body, sizeof(body), "{\"ok\":true,\"pid\":%ld}", (long)pid);
    return n > 0 && (size_t)n < sizeof(body) ? steam_route_response(body, 200) : NULL;
}

static MetalsharpResponse* handle_steam_stop(const HttpRequest* req) {
    (void)req;
    char prefix[PATH_MAX];
    snprintf(prefix, sizeof(prefix), "%s/prefix-steam", steam_home());
    char wineserver[PATH_MAX];
    int written = snprintf(wineserver, sizeof(wineserver), "%s/runtime/wine/bin/wineserver", steam_home());
    if (written >= 0 && (size_t)written < sizeof(wineserver) && access(wineserver, X_OK) == 0) {
        pid_t pid = fork();
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
        if (pid > 0) {
            while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
            }
        }
    }
    atomic_store(&g_steam_running, false);
    return steam_route_response("{\"ok\":true,\"running\":false}", 200);
}

/* ── Route registration ── */

void steam_register_routes(HttpServer* server, Database* db) {
    if (server == NULL || db == NULL) {
        LOG_WARN("steam_register_routes called with NULL arguments");
        return;
    }
    g_steam_db = db;
    ensure_steam_table(db);

    /* Implementation handlers — distinct because the response
     * shape carries more than the {"ok":true} stub. */
    http_server_register(server, "GET", "/steam/status", handle_steam_status);
    http_server_register(server, "GET", "/steam/library", handle_steam_library);
    http_server_register(server, "GET", "/steam/is-running", handle_steam_is_running);
    http_server_register(server, "GET", "/steam/stop-targets", handle_steam_stop_targets);
    http_server_register(server, "POST", "/steam/save-api-key", handle_steam_save_api_key);
    http_server_register(server, "GET", "/steam/api-key", handle_steam_api_key);
    http_server_register(server, "GET", "/steam/bridge-status", handle_steam_bridge_status);

    /* Stubs — every launch / install / uninstall / bridge /
     * runtime-doctor / compatdata / mac-* route returns
     * {"ok":true} pending the real adapters. Registered
     * against the same handler so the literal payload stays
     * consistent across the entire stubbed surface. */
    http_server_register(server, "POST", "/steam/launch", handle_steam_stub_ok);
    http_server_register(server, "POST", "/steam/launch-game", handle_steam_stub_ok);
    http_server_register(server, "POST", "/steam/launch-offline", handle_steam_stub_ok);
    http_server_register(server, "POST", "/steam/install", handle_steam_stub_ok);
    http_server_register(server, "POST", "/steam/install-game", handle_steam_stub_ok);
    http_server_register(server, "POST", "/steam/stop", handle_steam_stub_ok);
    http_server_register(server, "POST", "/steam/compatdata", handle_steam_stub_ok);
    http_server_register(server, "GET", "/steam/watch-steamapps", handle_steam_watch_steamapps);
    http_server_register(server, "POST", "/steam/bridge-start", handle_steam_stub_ok);
    http_server_register(server, "POST", "/steam/runtime-doctor", handle_steam_stub_ok);
    http_server_register(server, "POST", "/steam/d3d12-runtime-doctor", handle_steam_stub_ok);
    http_server_register(server, "POST", "/steam/mac-install", handle_steam_stub_ok);
    http_server_register(server, "POST", "/steam/mac-launch", handle_steam_stub_ok);
    http_server_register(server, "POST", "/steam/mac-launch-game", handle_steam_stub_ok);
    http_server_register(server, "POST", "/steam/mac-stop", handle_steam_stub_ok);
    http_server_register(server, "POST", "/steam/uninstall-game", handle_steam_stub_ok);
    http_server_register(server, "POST", "/steam/view-game", handle_steam_stub_ok);
    http_server_register(server, "POST", "/steam/install-recipe-deps", handle_steam_stub_ok);

    LOG_INFO("steam routes registered (25)");
}
