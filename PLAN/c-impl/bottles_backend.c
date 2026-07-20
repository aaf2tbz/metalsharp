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

/* ── Static runtime-profile catalog ── */

typedef struct {
    const char* id;
    const char* label;
    const char* description;
} bottle_profile_def_t;

static const bottle_profile_def_t k_bottle_profiles[] = {
    {"gaming-x86", "Gaming (x86)", "32-bit only gaming bottle"},
    {"gaming-x64", "Gaming (x64)", "64-bit gaming bottle"},
    {"productivity", "Productivity", "Office and productivity apps"},
    {"legacy-32", "Legacy 32-bit", "Old 32-bit Windows software"},
    {"arm64-xlate", "ARM64 translation", "ARM64EC translation target"},
    {"dx-mt", "D3D12 -> Metal (DXMT)", "Agility SDK DXMT pipeline"},
    {"dx-vk", "D3D12 -> Vulkan (VKD3D)", "Agility SDK VKD3D-Proton"},
};
#define BOTTLES_PROFILES_COUNT (sizeof(k_bottle_profiles) / sizeof(k_bottle_profiles[0]))

/* ── Static redistributable-source catalog ── */

typedef struct {
    const char* id;
    const char* name;
    const char* url;
    const char* version;
} redist_source_def_t;

static const redist_source_def_t k_redist_sources[] = {
    {"vcredist_2019_x64", "Visual C++ 2019 x64", "https://aka.ms/vs/16/release/vc_redist.x64.exe", "14.28.29910"},
    {"vcredist_2019_x86", "Visual C++ 2019 x86", "https://aka.ms/vs/16/release/vc_redist.x86.exe", "14.28.29910"},
    {"vcredist_2022_x64", "Visual C++ 2022 x64", "https://aka.ms/vs/17/release/vc_redist.x64.exe", "14.36.32532"},
    {"vcredist_2022_x86", "Visual C++ 2022 x86", "https://aka.ms/vs/17/release/vc_redist.x86.exe", "14.36.32532"},
    {"dotnet48", ".NET Framework 4.8", "https://dotnet.microsoft.com/download/dotnet-framework/net48", "4.8"},
    {"dxmt", "DXMT", "https://github.com/3Shain/dxmt/releases", "1.0"},
    {"dxvk", "DXVK", "https://github.com/doitsujin/dxvk/releases", "2.3"},
    {"vkd3d", "VKD3D-Proton", "https://github.com/HansKristian-Work/vkd3d-proton/releases", "2.9"},
    {"corefonts", "Microsoft core fonts", "https://sourceforge.net/projects/corefonts/", ""},
    {"wine-mono", "Wine Mono", "https://github.com/wine-mono/wine-mono/releases", "8.0.0"},
};
#define BOTTLES_REDIST_COUNT (sizeof(k_redist_sources) / sizeof(k_redist_sources[0]))

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
 * POST route. Centralising the literal keeps the wire shape in
 * one place; if the maintainers later decide to attach additional
 * fields (a request id, an action label, etc.), one edit
 * propagates to every caller.
 */
static MetalsharpResponse* bottles_stub_ok_response(void) {
    return make_data_response("{\"ok\":true}");
}

/*
 * Build the {"ok":true,"error":""} reply used by POST
 * /bottles/repair-component. The error string is empty to satisfy
 * the wire contract while signalling "no failure detail".
 */
static MetalsharpResponse* bottles_repair_ok_response(void) {
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

/* ── Bottle list callback context ── */

/*
 * Per-query plumbing handed to db_query for GET /bottles. The
 * callback writes each row's JSON object straight into `buf`,
 * separating consecutive entries with a comma. `first` flips to
 * false after the first row so the leading separator is omitted.
 * `failed` short-circuits subsequent rows after a memory error.
 */
typedef struct {
    jsonbuf_t* buf;
    bool first;
} bottle_row_ctx;

static int bottle_row_cb(void* raw, int ncols, char** values, char** names) {
    (void)names;
    bottle_row_ctx* ctx = (bottle_row_ctx*)raw;
    if (ctx == NULL || ctx->buf == NULL || !ctx->buf->ok) {
        return 0;
    }
    if (!ctx->first) {
        (void)jsonbuf_append(ctx->buf, ",");
    }
    ctx->first = false;
    const char* id_val = (ncols >= 1 && values != NULL) ? values[0] : NULL;
    const char* name_val = (ncols >= 2 && values != NULL) ? values[1] : NULL;
    const char* prefix_val = (ncols >= 3 && values != NULL) ? values[2] : NULL;
    const char* profile_val = (ncols >= 4 && values != NULL) ? values[3] : NULL;
    const char* winver_val = (ncols >= 5 && values != NULL) ? values[4] : NULL;
    const char* lastused_val = (ncols >= 6 && values != NULL) ? values[5] : NULL;
    const char* created_val = (ncols >= 7 && values != NULL) ? values[6] : NULL;
    (void)jsonbuf_append(ctx->buf, "{");
    (void)jsonbuf_appendf(ctx->buf, "\"id\":%s,", id_val != NULL ? id_val : "0");
    (void)jsonbuf_append(ctx->buf, "\"name\":");
    jsonbuf_append_escaped(ctx->buf, name_val);
    (void)jsonbuf_append(ctx->buf, ",\"prefix_path\":");
    jsonbuf_append_escaped(ctx->buf, prefix_val);
    (void)jsonbuf_append(ctx->buf, ",\"runtime_profile\":");
    jsonbuf_append_escaped(ctx->buf, profile_val);
    (void)jsonbuf_append(ctx->buf, ",\"windows_version\":");
    jsonbuf_append_escaped(ctx->buf, winver_val);
    (void)jsonbuf_appendf(ctx->buf, ",\"last_used_at\":%s,", lastused_val != NULL ? lastused_val : "0");
    (void)jsonbuf_appendf(ctx->buf, "\"created_at\":%s", created_val != NULL ? created_val : "0");
    (void)jsonbuf_append(ctx->buf, "}");
    return ctx->buf->ok ? 0 : 1;
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
    if (g_bottles_db == NULL) {
        return make_data_response("{\"ok\":true,\"bottles\":[]}");
    }
    jsonbuf_t body = jsonbuf_new();
    if (!body.ok) {
        return make_error_response("out of memory");
    }
    (void)jsonbuf_append(&body, "{\"ok\":true,\"bottles\":[");
    bottle_row_ctx ctx = {&body, true};
    static const char sql[] = "SELECT " BOTTLES_COL_ID "," BOTTLES_COL_NAME "," BOTTLES_COL_PREFIX
                              "," BOTTLES_COL_PROFILE "," BOTTLES_COL_WINVER "," BOTTLES_COL_LASTUSED
                              "," BOTTLES_COL_CREATED " FROM " BOTTLES_TABLE " ORDER BY " BOTTLES_COL_ID;
    char* err = NULL;
    bool ok = db_query(g_bottles_db, sql, bottle_row_cb, &ctx, &err);
    if (!ok) {
        LOG_WARN("GET /bottles query failed: %s", err != NULL ? err : "(unknown)");
        free(err);
        jsonbuf_free(&body);
        return make_error_response("bottle query failed");
    }
    (void)jsonbuf_append(&body, "]}");
    if (!body.ok) {
        jsonbuf_free(&body);
        return make_error_response("bottle serialization failed");
    }
    MetalsharpResponse* resp = make_data_response(body.data);
    jsonbuf_free(&body);
    return resp;
}

/*
 * GET /bottles/profiles — list the compiled-in runtime-profile
 * catalog. Each entry mirrors the {id,label,description} shape the
 * Electron shell expects on the settings page.
 */
static MetalsharpResponse* handle_bottles_profiles(const HttpRequest* req) {
    (void)req;
    jsonbuf_t body = jsonbuf_new();
    if (!body.ok) {
        return make_error_response("out of memory");
    }
    (void)jsonbuf_append(&body, "{\"ok\":true,\"profiles\":[");
    for (size_t i = 0u; i < BOTTLES_PROFILES_COUNT; i++) {
        if (i > 0u) {
            (void)jsonbuf_append(&body, ",");
        }
        const bottle_profile_def_t* p = &k_bottle_profiles[i];
        (void)jsonbuf_append(&body, "{");
        (void)jsonbuf_append(&body, "\"id\":");
        jsonbuf_append_escaped(&body, p->id);
        (void)jsonbuf_append(&body, ",\"label\":");
        jsonbuf_append_escaped(&body, p->label);
        (void)jsonbuf_append(&body, ",\"description\":");
        jsonbuf_append_escaped(&body, p->description);
        (void)jsonbuf_append(&body, "}");
    }
    (void)jsonbuf_append(&body, "]}");
    if (!body.ok) {
        jsonbuf_free(&body);
        return make_error_response("profile serialization failed");
    }
    MetalsharpResponse* resp = make_data_response(body.data);
    jsonbuf_free(&body);
    return resp;
}

/*
 * GET /bottles/redist-sources — list the compiled-in
 * redistributable-source catalog. Each entry carries {id,name,
 * url,version} so the Electron shell can render a download table.
 */
static MetalsharpResponse* handle_bottles_redist_sources(const HttpRequest* req) {
    (void)req;
    jsonbuf_t body = jsonbuf_new();
    if (!body.ok) {
        return make_error_response("out of memory");
    }
    (void)jsonbuf_append(&body, "{\"ok\":true,\"sources\":[");
    for (size_t i = 0u; i < BOTTLES_REDIST_COUNT; i++) {
        if (i > 0u) {
            (void)jsonbuf_append(&body, ",");
        }
        const redist_source_def_t* r = &k_redist_sources[i];
        (void)jsonbuf_append(&body, "{");
        (void)jsonbuf_append(&body, "\"id\":");
        jsonbuf_append_escaped(&body, r->id);
        (void)jsonbuf_append(&body, ",\"name\":");
        jsonbuf_append_escaped(&body, r->name);
        (void)jsonbuf_append(&body, ",\"url\":");
        jsonbuf_append_escaped(&body, r->url);
        (void)jsonbuf_append(&body, ",\"version\":");
        jsonbuf_append_escaped(&body, r->version);
        (void)jsonbuf_append(&body, "}");
    }
    (void)jsonbuf_append(&body, "]}");
    if (!body.ok) {
        jsonbuf_free(&body);
        return make_error_response("redist serialization failed");
    }
    MetalsharpResponse* resp = make_data_response(body.data);
    jsonbuf_free(&body);
    return resp;
}

/*
 * GET /bottles/route-contracts — stub returning an empty contract
 * array. The real implementation will dump the per-route
 * machine-readable schemas; for now the Electron shell checks for
 * shape compatibility with the empty array.
 */
static MetalsharpResponse* handle_bottles_route_contracts(const HttpRequest* req) {
    (void)req;
    return make_data_response("{\"ok\":true,\"contracts\":[]}");
}

/*
 * GET /bottles/compatibility-matrix — stub returning an empty
 * matrix. The real implementation will dump the per-(bottle,
 * game) compatibility record; for now the empty array satisfies
 * the wire contract.
 */
static MetalsharpResponse* handle_bottles_compatibility_matrix(const HttpRequest* req) {
    (void)req;
    return make_data_response("{\"ok\":true,\"matrix\":[]}");
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
    (void)req;
    return bottles_stub_ok_response();
}

/*
 * POST /bottles/repair-component — recovery action with a
 * distinct response shape. Returns {"ok":true,"error":""} so the
 * Electron shell always sees an empty error string on the happy
 * path; the real implementation will populate `error` with the
 * repair-stage diagnostic on failure.
 */
static MetalsharpResponse* handle_bottles_repair_component(const HttpRequest* req) {
    (void)req;
    return bottles_repair_ok_response();
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

    /* POST endpoints (stubs) */
    http_server_register(server, "POST", "/bottles/prepare", handle_bottles_stub_ok);
    http_server_register(server, "POST", "/bottles/edit", handle_bottles_stub_ok);
    http_server_register(server, "POST", "/bottles/get", handle_bottles_stub_ok);
    http_server_register(server, "POST", "/bottles/doctor", handle_bottles_stub_ok);
    http_server_register(server, "POST", "/bottles/repair-component", handle_bottles_repair_component);
    http_server_register(server, "POST", "/bottles/refresh", handle_bottles_stub_ok);
    http_server_register(server, "POST", "/bottles/sync-steam", handle_bottles_stub_ok);
    http_server_register(server, "POST", "/bottles/seed-post-wineboot", handle_bottles_stub_ok);
    http_server_register(server, "POST", "/bottles/set-runtime-profile", handle_bottles_stub_ok);
    http_server_register(server, "POST", "/bottles/set-windows-version", handle_bottles_stub_ok);
    http_server_register(server, "POST", "/bottles/apply-font-subs", handle_bottles_stub_ok);
    http_server_register(server, "POST", "/bottles/verify-directx", handle_bottles_stub_ok);
    http_server_register(server, "POST", "/bottles/record-compatibility", handle_bottles_stub_ok);
    http_server_register(server, "POST", "/bottles/relaunch-installer", handle_bottles_stub_ok);

    LOG_INFO("bottles routes registered (19)");
}
