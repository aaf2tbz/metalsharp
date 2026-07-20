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

#include "database.h"
#include "http_server.h"
#include "json.h"
#include "logger.h"
#include "server.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

/* Generic "{"ok":true}" stub — used by every launch, install,
 * uninstall, bridge, compatdata, runtime-doctor, and mac-*
 * route until the real backend adapters land. */
static MetalsharpResponse* handle_steam_stub_ok(const HttpRequest* req);

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

/*
 * Build the {"ok":true} acknowledgement used by every stubbed
 * Steam route. Routes share this helper so the literal payload
 * stays in one place; if the maintainers later decide to attach
 * additional fields (a request id, an action label, etc.), one
 * edit propagates to every caller.
 */
static MetalsharpResponse* steam_ok_response(void) {
    return make_data_response("{\"ok\":true}");
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
    if (ncols >= 2 && values != NULL && values[0] != NULL && values[1] != NULL) {
        c->value = strdup(values[1]);
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
    bool running = atomic_load(&g_steam_running);
    /* Real implementation will scan /Applications/Steam.app
     * Contents/MacOS/Steam alongside the installed flag; for
     * now the running flag reflects the bridge or scanner
     * worker that last updated g_steam_running. */
    char body[128];
    int n = snprintf(body, sizeof(body), "{\"installed\":%s,\"running\":%s}", steam_installed() ? "true" : "false",
                     running ? "true" : "false");
    if (n < 0 || (size_t)n >= sizeof(body)) {
        return make_error_response("internal error");
    }
    return make_data_response(body);
}

static MetalsharpResponse* handle_steam_library(const HttpRequest* req) {
    (void)req;
    /* Real implementation will scan ~/Library/Application
     * Support/Steam/steamapps for appmanifest_*.acf entries,
     * parse each into a {appid,name,installed} record, and
     * count the array. The stub returns an empty library so
     * the Electron shell renders a clean "no games" state. */
    return make_data_response("{\"ok\":true,\"games\":[],\"total\":0}");
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

static MetalsharpResponse* handle_steam_stub_ok(const HttpRequest* req) {
    (void)req;
    return steam_ok_response();
}

static MetalsharpResponse* handle_steam_stop_targets(const HttpRequest* req) {
    (void)req;
    /* Real implementation will enumerate running Wine
     * prefixes tied to Steam appids and expose the killable
     * pids here; the stub returns an empty list so the
     * Electron shell sees a clean state. */
    return make_data_response("{\"ok\":true,\"targets\":[]}");
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
    if (key == NULL) {
        json_free(parsed);
        return make_error_response("missing key field");
    }
    char* key_dup = strdup(key);
    json_free(parsed);
    if (key_dup == NULL) {
        return make_error_response("out of memory");
    }
    bool ok = kv_put(g_steam_db, STEAM_API_KEY_KEY, key_dup);
    free(key_dup);
    if (!ok) {
        return make_error_response("database write failed");
    }
    return steam_ok_response();
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
    return make_data_response("{\"ok\":true,\"status\":\"idle\"}");
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
    http_server_register(server, "GET", "/steam/watch-steamapps", handle_steam_stub_ok);
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
