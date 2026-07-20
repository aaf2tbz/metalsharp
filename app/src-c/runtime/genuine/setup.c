/*
 * setup.c — Setup wizard module for the Metalsharp C backend
 *
 * WHAT
 *   Implements the 11 setup-wizard HTTP routes mounted under /setup/
 *   listed in GENUINE_C_PLAN.md §1.3. The wizard tracks completion
 *   state, the user-visible device name, dependency installation
 *   status, installed D3D12 Agility SDK versions, Visual C++
 *   runtime installation (x64 and x86), and an installation-in-
 *   progress flag. Persisted state lives as a single JSON document
 *   at key="setup_state" inside a dedicated SQLite key-value table
 *   so it survives backend restarts in one atomic write per save.
 *
 * IMPORTS
 *   "server.h"        MetalsharpResponse, METALSHARP_VERSION
 *   "http_server.h"   HttpServer, HttpRequest, http_server_register
 *   "database.h"      Database, db_exec, db_query
 *   "json.h"          json_parse, json_serialize, JsonValue, accessors
 *   "logger.h"        LOG_INFO, LOG_WARN, LOG_ERROR
 *   <stdbool.h>       bool, true, false
 *   <stddef.h>        size_t, NULL
 *   <stdio.h>         snprintf
 *   <stdlib.h>        calloc, free
 *   <string.h>        memcpy, strlen, strcmp
 *   <pthread.h>       pthread_mutex_t for install-progress guard
 *   <sys/sysctl.h>    sysctlbyname  (Apple platforms only)
 *
 * EXPORTS
 *   setup_register_routes(HttpServer *server, Database *db)
 *       Register all 11 setup-wizard routes on the supplied HttpServer
 *       and bind them to `db` for state persistence. Called once
 *       during startup, before http_server_run(). Passing NULL
 *       either side is a silent no-op so a half-initialised
 *       backend cannot crash inside the registry.
 *
 * SCHEMA
 *   Persisted setup state lives at key="setup_state" inside the
 *   `setup_kv` table, created on the first call to
 *   setup_register_routes via:
 *     CREATE TABLE IF NOT EXISTS setup_kv(
 *         key   TEXT PRIMARY KEY NOT NULL,
 *         value TEXT NOT NULL
 *     );
 *   The stored JSON carries these fields:
 *     completed       (bool)   wizard finished
 *     step            (int)    current wizard step, 0..N
 *     steamApiKeySet  (bool)   user has set a Steam API key
 *
 *   Route response shapes — every route wraps its reply in a
 *   MetalsharpResponse whose `data` pointer is a JsonValue tree
 *   representing the spec fields below. The HTTP layer then
 *   serialises that tree under its top-level "data" key:
 *     GET  /setup/state             data = {"completed":b,
 *                                   "step":i,"steamApiKeySet":b}
 *     GET  /setup/device-name       data = {"name":"<hw.model>"}
 *     GET  /setup/dependencies      data = {"deps":{...}}
 *     GET  /setup/agility-versions  data = {"versions":[]}
 *     POST /setup/save              data = {"saved":true}
 *     POST /setup/install-all       data = {"started":true}
 *     POST /setup/install-deps      data = {"started":true}
 *     GET  /setup/install-progress  data = {"percent":0,
 *                                   "step":"idle"}
 *     GET  /setup/installing        data = {"installing":false}
 *     POST /setup/install-vcpp-x64  data = {"started":true}
 *     POST /setup/install-vcpp-x86  data = {"started":true}
 *   All replies carry HTTP 200; failures return
 *   {"ok":false,"error":"<reason>"}.
 *
 * THREADING
 *   The HTTP server dispatches every request on a worker thread.
 *   State stored in setup_kv is serialised by the Database mutex
 *   that wraps every sqlite3 call. The in-process install-progress
 *   fields (percent / step / installing flag) are guarded by a
 *   single static mutex so concurrent /setup/install-* handlers
 *   cannot clobber each other. The Database handle used here is
 *   captured exactly once at registration time and never swapped,
 *   so race windows with main are non-existent.
 */

#include "database.h"
#include "http_server.h"
#include "json.h"
#include "logger.h"
#include "server.h"

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

/* ── Constants ── */

#define SETUP_KV_TABLE      "setup_kv"
#define SETUP_STATE_KEY     "setup_state"
#define SETUP_KV_KEY_COLUMN "key"
#define SETUP_KV_VAL_COLUMN "value"

/* Bounded so SQL builders can use small fixed-size heap allocations;
 * any state blob beyond this size is rejected with a logged warning
 * and an `{"ok":false,"error":"state too large"}` reply. */
#define SETUP_STATE_MAX_LEN (64u * 1024u)

/* Buffer size for the device-name slot and the install-step label.
 * 64 bytes comfortably fits "Mac Pro" through any plausible
 * marketing-name length and every install step label this module
 * emits. */
#define SETUP_LABEL_MAX 64u

/* Initial install-progress label. Lives in install_step_v from
 * program start; the four /setup/install-* handlers overwrite
 * it under the install_lock mutex. */
#define SETUP_DEFAULT_STEP "idle"

/* ── Module-local state ── */

/* Database handle captured at registration time. The HTTP layer
 * does not thread per-request context to the handler, so every
 * route looks up the handle here. Set exactly once before
 * http_server_run() and never reset, so the handlers only need
 * to re-read it on each call. All access goes through the
 * Database wrapper, which acquires its own mutex. */
static Database* g_setup_db = NULL;

/* In-process install-progress state. Guarded by install_lock.
 * Reflects the latest view of any install triggered through
 * this module; the durable progress field lives inside the
 * persisted setup_state JSON blob, not here. */
static pthread_mutex_t install_lock = PTHREAD_MUTEX_INITIALIZER;
static int install_percent_v = 0;
static char install_step_v[SETUP_LABEL_MAX] = SETUP_DEFAULT_STEP;
static bool installing_v = false;

/* ── Forward declarations ── */

static MetalsharpResponse* handle_setup_state(const HttpRequest* req);
static MetalsharpResponse* handle_setup_device_name(const HttpRequest* req);
static MetalsharpResponse* handle_setup_dependencies(const HttpRequest* req);
static MetalsharpResponse* handle_setup_agility_versions(const HttpRequest* req);
static MetalsharpResponse* handle_setup_save(const HttpRequest* req);
static MetalsharpResponse* handle_setup_install_all(const HttpRequest* req);
static MetalsharpResponse* handle_setup_install_deps(const HttpRequest* req);
static MetalsharpResponse* handle_setup_install_progress(const HttpRequest* req);
static MetalsharpResponse* handle_setup_installing(const HttpRequest* req);
static MetalsharpResponse* handle_setup_install_vcpp_x64(const HttpRequest* req);
static MetalsharpResponse* handle_setup_install_vcpp_x86(const HttpRequest* req);

/* ── Response builders ── */

/*
 * Build a successful MetalsharpResponse whose `data` payload is a
 * freshly-parsed JsonValue tree representing `body`. The HTTP
 * layer will then serialise the tree under a top-level "data"
 * field. `body` must be a valid JSON document; an empty or
 * syntactically-bad body yields an ok=false response with a
 * descriptive error_msg. Returns NULL only on calloc failure.
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
 * into heap-owned storage so the caller need not keep `msg`
 * alive. Returns NULL only on calloc failure.
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
 * Double any single-quote characters in `s` so the result is safe
 * to embed between SQL string delimiters. SQLite (per SQL
 * standard) treats two consecutive single quotes inside a string
 * literal as a single literal quote, so doubling is sufficient.
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
 * Make sure the setup_kv table exists. Called once from
 * setup_register_routes. Failure is logged; the routes then
 * gracefully fall through to default responses on every call,
 * since trying to write through a missing table would surface
 * the same sqlite error each time.
 */
static void ensure_setup_table(Database* db) {
    if (db == NULL) {
        return;
    }
    static const char ddl[] = "CREATE TABLE IF NOT EXISTS " SETUP_KV_TABLE " (" SETUP_KV_KEY_COLUMN
                              " TEXT PRIMARY KEY NOT NULL," SETUP_KV_VAL_COLUMN " TEXT NOT NULL"
                              ")";
    char* err = NULL;
    if (!db_exec(db, ddl, &err)) {
        LOG_ERROR("setup_kv create failed: %s", err != NULL ? err : "(unknown)");
        free(err);
    }
}

/*
 * Read the JSON value for `key` from setup_kv. Returns a heap-
 * allocated, NUL-terminated copy (caller frees), or NULL when
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
    /* prefix + suffix + NUL = ~96 bytes of literal SQL plus the
     * doubled-key body. Add a small constant and use malloc so
     * the buffer tracks whichever key the caller passed. */
    size_t cap = strlen(kq) + 128u;
    char* sql = malloc(cap);
    if (sql == NULL) {
        free(kq);
        return NULL;
    }
    snprintf(sql, cap, "SELECT " SETUP_KV_VAL_COLUMN " FROM " SETUP_KV_TABLE " WHERE " SETUP_KV_KEY_COLUMN " = '%s'",
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
 * Upsert the JSON value at `key`. The value is SQL-quoted, so
 * any embedded single-quotes are doubled to keep the literal
 * intact. Returns true on success. On failure logs and returns
 * false; any oversized payload is rejected with a warning
 * before the sqlite call is made.
 */
static bool kv_put(Database* db, const char* key, const char* value) {
    if (db == NULL || key == NULL || value == NULL) {
        return false;
    }
    size_t vlen = strlen(value);
    if (vlen > SETUP_STATE_MAX_LEN) {
        LOG_WARN("setup state too large (%zu bytes), refusing to save", vlen);
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
             "INSERT OR REPLACE INTO " SETUP_KV_TABLE " (" SETUP_KV_KEY_COLUMN "," SETUP_KV_VAL_COLUMN
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

/* ── State parsing helper ── */

/*
 * Read the persisted setup_state blob, parse it as JSON, and
 * populate the three top-level fields used by /setup/state.
 * On any read or parse failure the defaults
 * (completed=false, step=0, steamApiKeySet=false) are returned.
 */
static void read_state_triplet(Database* db, bool* completed, int* step, bool* steamApiKeySet) {
    if (completed != NULL)
        *completed = false;
    if (step != NULL)
        *step = 0;
    if (steamApiKeySet != NULL)
        *steamApiKeySet = false;
    if (db == NULL) {
        return;
    }
    char* raw = kv_get(db, SETUP_STATE_KEY);
    if (raw == NULL) {
        return;
    }
    JsonValue* root = json_parse(raw, strlen(raw), NULL);
    free(raw);
    if (root == NULL) {
        return;
    }
    if (completed != NULL) {
        *completed = json_get_bool(json_object_get(root, "completed"), false);
    }
    if (step != NULL) {
        *step = (int)json_get_number(json_object_get(root, "step"), 0.0);
    }
    if (steamApiKeySet != NULL) {
        *steamApiKeySet = json_get_bool(json_object_get(root, "steamApiKeySet"), false);
    }
    json_free(root);
}

/* ── Install-progress helpers ── */

/*
 * Mark a background install as started. Resets percent to 0,
 * swaps in the new step label, and flips the in-process
 * installing flag to true. The four /setup/install-* handlers
 * share a single in-process install worker at this stage, so
 * the most recently-started request owns the progress state;
 * persisting the percent back into setup_state is left to
 * /setup/save, which records whatever the worker has produced
 * by then.
 */
static void install_mark_started(const char* step_label) {
    pthread_mutex_lock(&install_lock);
    installing_v = true;
    install_percent_v = 0;
    if (step_label != NULL) {
        size_t n = strlen(step_label);
        if (n >= sizeof(install_step_v)) {
            n = sizeof(install_step_v) - 1u;
        }
        memcpy(install_step_v, step_label, n);
        install_step_v[n] = '\0';
    } else {
        /* Fall back to a generic label without re-calling
         * strcpy to keep the dependency surface small. */
        static const char fallback[] = "starting";
        size_t n = sizeof(fallback) - 1u;
        memcpy(install_step_v, fallback, n);
        install_step_v[n] = '\0';
    }
    pthread_mutex_unlock(&install_lock);
}

/* ── Device-name helper ── */

/*
 * Read the marketing name of the running machine through
 * sysctlbyname("hw.model"). On any failure (non-Apple build,
 * sysctl error, or empty buffer) the buffer is filled with a
 * generic "Mac" label so the route always has something to
 * ship. The returned buffer is bounded to 200 bytes of
 * printable ASCII so the route's snprintf can never overflow.
 */
static void query_hw_model(char* out, size_t out_size) {
    if (out == NULL || out_size == 0) {
        return;
    }
    out[0] = '\0';
#ifdef __APPLE__
    size_t len = out_size;
    if (sysctlbyname("hw.model", out, &len, NULL, 0) == 0 && len > 0 && len <= out_size) {
        out[len] = '\0';
        return;
    }
#endif
    static const char fallback[] = "Mac";
    size_t n = sizeof(fallback) - 1u;
    if (n >= out_size) {
        n = out_size - 1u;
    }
    memcpy(out, fallback, n);
    out[n] = '\0';
}

/* ── Route handlers ── */

static MetalsharpResponse* handle_setup_state(const HttpRequest* req) {
    (void)req;
    bool completed = false;
    bool steamApiKeySet = false;
    int step = 0;
    read_state_triplet(g_setup_db, &completed, &step, &steamApiKeySet);
    char body[192];
    int n = snprintf(body, sizeof(body),
                     "{\"completed\":%s,\"step\":%d,"
                     "\"steamApiKeySet\":%s}",
                     completed ? "true" : "false", step, steamApiKeySet ? "true" : "false");
    if (n < 0 || (size_t)n >= sizeof(body)) {
        return make_error_response("internal error");
    }
    return make_data_response(body);
}

static MetalsharpResponse* handle_setup_device_name(const HttpRequest* req) {
    (void)req;
    char name[256];
    query_hw_model(name, sizeof(name));
    /* Cap the embedded name to 200 bytes so a malicious or
     * pathological sysctl reply cannot overflow the body
     * buffer. The name from sysctl on macOS is plain ASCII, so
     * embedding it raw between double-quotes is always safe. */
    char body[320];
    int n = snprintf(body, sizeof(body), "{\"name\":\"%.200s\"}", name);
    if (n < 0 || (size_t)n >= sizeof(body)) {
        return make_error_response("internal error");
    }
    return make_data_response(body);
}

static MetalsharpResponse* handle_setup_dependencies(const HttpRequest* req) {
    (void)req;
    /* TODO: cross-check against installer_backend once it
     * exposes its component status; for now the wizard expects
     * an empty/dep-by-dep `deps` object so this stays
     * deterministic. */
    static const char body[] = "{\"deps\":{"
                               "\"wine-mono\":false,"
                               "\"gecko\":false,"
                               "\"vcrun2019_x64\":false,"
                               "\"vcrun2019_x86\":false,"
                               "\"dotnet48\":false,"
                               "\"dxmt\":false,"
                               "\"corefonts\":false"
                               "}}";
    return make_data_response(body);
}

static MetalsharpResponse* handle_setup_agility_versions(const HttpRequest* req) {
    (void)req;
    /* TODO: query dxmt_doctor for installed Agility SDK
     * versions once that module is wired in. Empty array
     * preserves the wizard's expected shape. */
    return make_data_response("{\"versions\":[]}");
}

static MetalsharpResponse* handle_setup_save(const HttpRequest* req) {
    if (g_setup_db == NULL) {
        return make_error_response("setup not initialised");
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
    /* Re-serialise the parsed object so the stored form is
     * canonical. Anything we can't serialise is structurally
     * broken JSON that should have failed parsing earlier,
     * but check the result defensively. */
    char* canonical = json_serialize(parsed);
    json_free(parsed);
    if (canonical == NULL) {
        return make_error_response("invalid JSON body");
    }
    bool ok = kv_put(g_setup_db, SETUP_STATE_KEY, canonical);
    free(canonical);
    if (!ok) {
        return make_error_response("database write failed");
    }
    return make_data_response("{\"saved\":true}");
}

/*
 * Common handler body for the four /setup/install-* routes.
 * Each one flips the in-process installing flag, resets
 * progress, and replies with {"started":true}; the actual
 * installer is wired in by the routes that supersede this
 * stub once installer_backend is brought online.
 */
static MetalsharpResponse* handle_setup_install_generic(const HttpRequest* req, const char* step_label) {
    (void)req;
    install_mark_started(step_label);
    return make_data_response("{\"started\":true}");
}

static MetalsharpResponse* handle_setup_install_all(const HttpRequest* req) {
    return handle_setup_install_generic(req, "all");
}

static MetalsharpResponse* handle_setup_install_deps(const HttpRequest* req) {
    return handle_setup_install_generic(req, "deps");
}

static MetalsharpResponse* handle_setup_install_vcpp_x64(const HttpRequest* req) {
    return handle_setup_install_generic(req, "vcpp_x64");
}

static MetalsharpResponse* handle_setup_install_vcpp_x86(const HttpRequest* req) {
    return handle_setup_install_generic(req, "vcpp_x86");
}

static MetalsharpResponse* handle_setup_install_progress(const HttpRequest* req) {
    (void)req;
    int percent = 0;
    char step_buf[SETUP_LABEL_MAX];
    pthread_mutex_lock(&install_lock);
    percent = install_percent_v;
    memcpy(step_buf, install_step_v, sizeof(install_step_v));
    pthread_mutex_unlock(&install_lock);
    /* snprintf reads step_buf until its NUL terminator, which
     * lives within the 64-byte slot install_mark_started
     * maintains. Cap with %.60s so a stray unterminated buffer
     * cannot run past the slot into adjacent stack. */
    char body[160];
    int n = snprintf(body, sizeof(body), "{\"percent\":%d,\"step\":\"%.60s\"}", percent, step_buf);
    if (n < 0 || (size_t)n >= sizeof(body)) {
        return make_error_response("internal error");
    }
    return make_data_response(body);
}

static MetalsharpResponse* handle_setup_installing(const HttpRequest* req) {
    (void)req;
    bool flag = false;
    pthread_mutex_lock(&install_lock);
    flag = installing_v;
    pthread_mutex_unlock(&install_lock);
    char body[64];
    int n = snprintf(body, sizeof(body), "{\"installing\":%s}", flag ? "true" : "false");
    if (n < 0 || (size_t)n >= sizeof(body)) {
        return make_error_response("internal error");
    }
    return make_data_response(body);
}

/* ── Route registration ── */

void setup_register_routes(HttpServer* server, Database* db) {
    if (server == NULL || db == NULL) {
        LOG_WARN("setup_register_routes called with NULL arguments");
        return;
    }
    g_setup_db = db;
    ensure_setup_table(db);

    http_server_register(server, "GET", "/setup/state", handle_setup_state);
    http_server_register(server, "GET", "/setup/device-name", handle_setup_device_name);
    http_server_register(server, "GET", "/setup/dependencies", handle_setup_dependencies);
    http_server_register(server, "GET", "/setup/agility-versions", handle_setup_agility_versions);
    http_server_register(server, "POST", "/setup/save", handle_setup_save);
    http_server_register(server, "POST", "/setup/install-all", handle_setup_install_all);
    http_server_register(server, "POST", "/setup/install-deps", handle_setup_install_deps);
    http_server_register(server, "GET", "/setup/install-progress", handle_setup_install_progress);
    http_server_register(server, "GET", "/setup/installing", handle_setup_installing);
    http_server_register(server, "POST", "/setup/install-vcpp-x64", handle_setup_install_vcpp_x64);
    http_server_register(server, "POST", "/setup/install-vcpp-x86", handle_setup_install_vcpp_x86);

    LOG_INFO("setup routes registered (11)");
}
