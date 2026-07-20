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
#include "logger.h"
#include "server.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
static MetalsharpResponse* handle_sharp_library_gog_play(const HttpRequest* req);

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
 * POST route. Centralising the literal keeps the wire shape in one
 * place; if the maintainers later decide to attach additional
 * fields (a request id, an action label, etc.), one edit
 * propagates to every caller.
 */
static MetalsharpResponse* sharp_library_ok_response(void) {
    return make_data_response("{\"ok\":true}");
}

/*
 * Build the {"ok":true,"error":"not found"} reply used by POST
 * /sharp-library/launch. The error string is hard-coded to
 * "not found" because the route currently runs as a stub and
 * has no way to resolve an app-id from the request body; once a
 * real launch adapter lands, the error string will be populated
 * with the per-request diagnostic and the literal stays as a
 * safe default.
 */
static MetalsharpResponse* sharp_library_launch_not_found_response(void) {
    return make_data_response("{\"ok\":true,\"error\":\"not found\"}");
}

/*
 * Build the {"ok":true,"error":""} reply used by POST
 * /sharp-library/gog/play. The empty error string signals the
 * rejection shape that the Electron shell already renders as a
 * non-fatal toast; once a real GOG launch adapter lands, this
 * literal is replaced with a per-request diagnostic.
 */
static MetalsharpResponse* sharp_library_gog_play_rejection_response(void) {
    return make_data_response("{\"ok\":true,\"error\":\"\"}");
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

/* ── App list callback context ── */

/*
 * Per-query plumbing handed to db_query for GET /sharp-library.
 * The callback writes each row's JSON object straight into `buf`,
 * separating consecutive entries with a comma. `first` flips to
 * false after the first row so the leading separator is omitted.
 * `failed` short-circuits subsequent rows after a memory error
 * by returning non-zero, which SQLite propagates back through
 * db_query as a statement-level failure.
 */
typedef struct {
    jsonbuf_t* buf;
    bool first;
} app_row_ctx;

static int app_row_cb(void* raw, int ncols, char** values, char** names) {
    (void)names;
    app_row_ctx* ctx = (app_row_ctx*)raw;
    if (ctx == NULL || ctx->buf == NULL || !ctx->buf->ok) {
        return 0;
    }
    if (!ctx->first) {
        (void)jsonbuf_append(ctx->buf, ",");
    }
    ctx->first = false;
    const char* id_val = (ncols >= 1 && values != NULL) ? values[0] : NULL;
    const char* name_val = (ncols >= 2 && values != NULL) ? values[1] : NULL;
    const char* bottle_val = (ncols >= 3 && values != NULL) ? values[2] : NULL;
    const char* cover_val = (ncols >= 4 && values != NULL) ? values[3] : NULL;
    const char* pos_val = (ncols >= 5 && values != NULL) ? values[4] : NULL;
    const char* engine_val = (ncols >= 6 && values != NULL) ? values[5] : NULL;
    const char* args_val = (ncols >= 7 && values != NULL) ? values[6] : NULL;
    const char* imported_val = (ncols >= 8 && values != NULL) ? values[7] : NULL;
    (void)jsonbuf_append(ctx->buf, "{");
    (void)jsonbuf_appendf(ctx->buf, "\"id\":%s,", id_val != NULL ? id_val : "0");
    (void)jsonbuf_append(ctx->buf, "\"name\":");
    jsonbuf_append_escaped(ctx->buf, name_val);
    (void)jsonbuf_appendf(ctx->buf, ",\"bottle_id\":%s,", bottle_val != NULL ? bottle_val : "0");
    (void)jsonbuf_append(ctx->buf, "\"cover_path\":");
    jsonbuf_append_escaped(ctx->buf, cover_val);
    (void)jsonbuf_appendf(ctx->buf, ",\"cover_position\":%s,", pos_val != NULL ? pos_val : "0");
    (void)jsonbuf_append(ctx->buf, "\"engine\":");
    jsonbuf_append_escaped(ctx->buf, engine_val);
    (void)jsonbuf_append(ctx->buf, ",\"launch_args\":");
    jsonbuf_append_escaped(ctx->buf, args_val);
    (void)jsonbuf_appendf(ctx->buf, ",\"imported_at\":%s", imported_val != NULL ? imported_val : "0");
    (void)jsonbuf_append(ctx->buf, "}");
    return ctx->buf->ok ? 0 : 1;
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
    if (g_sl_db == NULL) {
        return make_data_response("{\"ok\":true,\"apps\":[]}");
    }
    jsonbuf_t body = jsonbuf_new();
    if (!body.ok) {
        return make_error_response("out of memory");
    }
    (void)jsonbuf_append(&body, "{\"ok\":true,\"apps\":[");
    app_row_ctx ctx = {&body, true};
    static const char sql[] =
        "SELECT " SHARP_LIBRARY_COL_ID "," SHARP_LIBRARY_COL_NAME "," SHARP_LIBRARY_COL_BOTTLE
        "," SHARP_LIBRARY_COL_COVER "," SHARP_LIBRARY_COL_POS "," SHARP_LIBRARY_COL_ENGINE "," SHARP_LIBRARY_COL_ARGS
        "," SHARP_LIBRARY_COL_IMPORTED " FROM " SHARP_LIBRARY_TABLE " ORDER BY " SHARP_LIBRARY_COL_ID;
    char* err = NULL;
    bool ok = db_query(g_sl_db, sql, app_row_cb, &ctx, &err);
    if (!ok) {
        LOG_WARN("GET /sharp-library query failed: %s", err != NULL ? err : "(unknown)");
        free(err);
        jsonbuf_free(&body);
        return make_error_response("library query failed");
    }
    (void)jsonbuf_append(&body, "]}");
    if (!body.ok) {
        jsonbuf_free(&body);
        return make_error_response("library serialization failed");
    }
    MetalsharpResponse* resp = make_data_response(body.data);
    jsonbuf_free(&body);
    return resp;
}

/*
 * GET /sharp-library/cover — stub returning {"ok":true}. The real
 * implementation will serve the configured cover image bytes via
 * a content-type-aware HTTP reply; for now the route exists so
 * the Electron shell's "set cover" preview flow can probe without
 * receiving a 404.
 */
static MetalsharpResponse* handle_sharp_library_cover(const HttpRequest* req) {
    (void)req;
    return sharp_library_ok_response();
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
    (void)req;
    return sharp_library_launch_not_found_response();
}

/*
 * GET /sharp-library/gog/status — stub returning
 * {"ok":true,"status":""}. The real implementation will report the
 * GOG OAuth phase (signed-out, code-received, token-exchange,
 * token-valid); the empty status string keeps the wire contract
 * stable until the GOG auth module lands.
 */
static MetalsharpResponse* handle_sharp_library_gog_status(const HttpRequest* req) {
    (void)req;
    return make_data_response("{\"ok\":true,\"status\":\"\"}");
}

/*
 * GET /sharp-library/gog/games — stub returning
 * {"ok":true,"games":[],"status":""}. The real implementation will
 * hit the GOG Galaxy API with the persisted token, filter the
 * catalogue to entries the user owns, and report the result. The
 * empty games array and empty status string keep the wire
 * contract stable until the GOG auth module lands.
 */
static MetalsharpResponse* handle_sharp_library_gog_games(const HttpRequest* req) {
    (void)req;
    return make_data_response("{\"ok\":true,\"games\":[],\"status\":\"\"}");
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
    (void)req;
    return sharp_library_gog_play_rejection_response();
}

/*
 * Common body for the stub POST routes. Every entry below returns
 * the {"ok":true} envelope; the real implementations will be
 * filled in once the GOG auth / install / progress / stop /
 * uninstall / logout / sync / remove-prefix pipelines have been
 * ported from the legacy Node backend.
 */
static MetalsharpResponse* handle_sharp_library_stub_ok(const HttpRequest* req) {
    (void)req;
    return sharp_library_ok_response();
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
    http_server_register(server, "POST", "/sharp-library/install", handle_sharp_library_stub_ok);
    http_server_register(server, "POST", "/sharp-library/uninstall", handle_sharp_library_stub_ok);
    http_server_register(server, "POST", "/sharp-library/set-cover", handle_sharp_library_stub_ok);
    http_server_register(server, "POST", "/sharp-library/set-cover-position", handle_sharp_library_stub_ok);
    http_server_register(server, "POST", "/sharp-library/set-engine", handle_sharp_library_stub_ok);
    http_server_register(server, "POST", "/sharp-library/set-launch-args", handle_sharp_library_stub_ok);
    http_server_register(server, "POST", "/sharp-library/doctor", handle_sharp_library_stub_ok);
    http_server_register(server, "POST", "/sharp-library/import-bottle-app", handle_sharp_library_stub_ok);

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
    http_server_register(server, "POST", "/sharp-library/gog/auth-code", handle_sharp_library_stub_ok);
    http_server_register(server, "POST", "/sharp-library/gog/import", handle_sharp_library_stub_ok);
    http_server_register(server, "POST", "/sharp-library/gog/initialize-prefix", handle_sharp_library_stub_ok);
    http_server_register(server, "POST", "/sharp-library/gog/install", handle_sharp_library_stub_ok);
    http_server_register(server, "POST", "/sharp-library/gog/progress", handle_sharp_library_stub_ok);
    http_server_register(server, "POST", "/sharp-library/gog/stop", handle_sharp_library_stub_ok);
    http_server_register(server, "POST", "/sharp-library/gog/uninstall", handle_sharp_library_stub_ok);
    http_server_register(server, "POST", "/sharp-library/gog/logout", handle_sharp_library_stub_ok);
    http_server_register(server, "POST", "/sharp-library/gog/sync", handle_sharp_library_stub_ok);
    http_server_register(server, "POST", "/sharp-library/gog/remove-prefix", handle_sharp_library_stub_ok);

    LOG_INFO("sharp-library routes registered (24)");
}