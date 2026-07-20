/*
 * installer_backend.c — Installer-wizard backend module for the
 * Metalsharp C backend.
 *
 * WHAT
 *   Implements the setup-prefixed routes that drive the wizard's
 *   dependency and runtime installation pipeline, documented in
 *   GENUINE_C_PLAN.md. The module is organised around four
 *   responsibilities:
 *     • Reporting the installed/needed state of bundled
 *       dependencies (mono, gecko, vcrun, dotnet, dxmt,
 *       corefonts).
 *     • Reporting the list of installed D3D12 Agility SDK
 *       versions.
 *     • Persisting the wizard's accumulated state document
 *       under key="setup_state" inside the dedicated `setup_kv`
 *       SQLite table.
 *     • Kicking off the install workflows for the full bundle,
 *       the dependency bundle, and the Visual C++ runtimes
 *       (x64 and x86), and reporting in-flight installation
 *       progress plus the installing flag.
 *
 *   This module's route table overlaps with setup.c on purpose:
 *   http_server_register keeps the most-recently-registered
 *   handler for a (method, path) pair, so wiring this module
 *   in after setup.c supersedes the stub handlers in setup.c
 *   for the routes below.
 *
 * IMPORTS
 *   "server.h"        MetalsharpResponse, METALSHARP_VERSION
 *   "http_server.h"   HttpServer, HttpRequest, http_server_register
 *   "database.h"      Database (captured for future persistence)
 *   "json.h"          json_parse, json_serialize, JsonValue,
 *                     accessors
 *   "logger.h"        LOG_WARN
 *   <stdbool.h>       bool, true, false
 *   <stddef.h>        size_t, NULL
 *   <stdio.h>         snprintf
 *   <stdlib.h>        calloc, free
 *   <string.h>        memcpy, strlen
 *   <pthread.h>       pthread_mutex_t for install-progress guard
 *
 * EXPORTS
 *   installer_backend_register_routes(HttpServer *server, Database *db)
 *       Register every installer route (under the /setup prefix) on the supplied
 *       HttpServer and bind the module to `db` for state
 *       persistence. Called once during startup, before
 *       http_server_run(). NULL arguments are a silent no-op so
 *       a half-initialised backend cannot crash inside the
 *       registry.
 *
 * SCHEMA
 *   Persisted setup state will live at key="setup_state" inside
 *   the `setup_kv` table created on first registration:
 *     CREATE TABLE IF NOT EXISTS setup_kv(
 *         key   TEXT PRIMARY KEY NOT NULL,
 *         value TEXT NOT NULL
 *     );
 *   The full sqlite plumbing lands in a follow-up phase that
 *   picks up the captured Database handle; until then the
 *   /setup/save handler validates the request body, parses it
 *   as JSON, and acknowledges the round-trip without touching
 *   disk.
 *
 *   Route response shapes - every route wraps its reply in a
 *   MetalsharpResponse whose `data` pointer is a JsonValue tree
 *   representing the spec fields below. The HTTP layer then
 *   serialises that tree under its top-level "data" key:
 *     GET  /setup/dependencies      data = {"ok":true,"deps":{
 *                                   "wine-mono":false,
 *                                   "gecko":false,
 *                                   "vcrun2019_x64":false,
 *                                   "vcrun2019_x86":false,
 *                                   "dotnet48":false,
 *                                   "dxmt":false,
 *                                   "corefonts":false}}
 *     GET  /setup/agility-versions  data = {"ok":true,"versions":[]}
 *     POST /setup/save              data = {"ok":true,"saved":true}
 *     POST /setup/install-all       data = {"ok":true,"started":true}
 *     POST /setup/install-deps      data = {"ok":true,"started":true}
 *     GET  /setup/install-progress  data = {"ok":true,"percent":0,
 *                                   "step":"idle"}
 *     GET  /setup/installing        data = {"ok":true,
 *                                   "installing":false}
 *     POST /setup/install-vcpp-x64  data = {"ok":true,"started":true}
 *     POST /setup/install-vcpp-x86  data = {"ok":true,"started":true}
 *   All replies carry HTTP 200; failures return
 *   {"ok":false,"error":"<reason>"}.
 *
 * THREADING
 *   The HTTP server dispatches every request on a worker thread.
 *   The in-process install-progress fields (percent / step /
 *   installing flag) are guarded by a single static mutex so
 *   concurrent /setup/install-* handlers cannot clobber each
 *   other. The Database handle used here is captured exactly
 *   once at registration time and never swapped, so race windows
 *   with main are non-existent.
 *
 * NOTE
 *   This file ships as a stub implementation that returns the
 *   documented JSON shape for every route and validates POST
 *   bodies where applicable. Persistent state writes and the
 *   real installer workers are wired in by a later phase.
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

/* ── Constants ── */

/*
 * Bounded label for the in-process install-step string. 64 bytes
 * comfortably covers every meaningful step label (deps, vcpp_x64,
 * agility, idle, error, ...) and aligns with the slot used in
 * setup.c so the wizard's progress envelope never overflows.
 */
#define INSTALLER_LABEL_MAX 64

#define INSTALLER_DEFAULT_STEP "idle"

/*
 * Bound the persisted setup_state blob so a malicious or buggy
 * client cannot blow up the SQLite column. 256 KiB is many times
 * the legitimate wizard-state size; /setup/save rejects bodies
 * that would expand past this cap before any sqlite call runs.
 */
#define INSTALLER_STATE_MAX_LEN (256u * 1024u)

/* ── Static module state ── */

/*
 * Database handle captured at registration time. Held for the
 * follow-up phase that performs real sqlite writes from
 * /setup/save; until then the save handler validates and acks
 * the body without touching disk.
 */
static Database* g_installer_db = NULL;

/*
 * In-process install-progress state. The four /setup/install-*
 * routes overwrite these fields atomically; /setup/install-
 * progress and /setup/installing read them back under the same
 * mutex so concurrent /setup/install-* callers never tear each
 * other's view of the world.
 */
static pthread_mutex_t install_lock = PTHREAD_MUTEX_INITIALIZER;
static int install_percent_v = 0;
static char install_step_v[INSTALLER_LABEL_MAX] = INSTALLER_DEFAULT_STEP;
static bool installing_v = false;

/* ── Forward declarations ── */

static MetalsharpResponse* handle_installer_dependencies(const HttpRequest* req);
static MetalsharpResponse* handle_installer_agility_versions(const HttpRequest* req);
static MetalsharpResponse* handle_installer_save(const HttpRequest* req);
static MetalsharpResponse* handle_installer_install_all(const HttpRequest* req);
static MetalsharpResponse* handle_installer_install_deps(const HttpRequest* req);
static MetalsharpResponse* handle_installer_install_progress(const HttpRequest* req);
static MetalsharpResponse* handle_installer_installing(const HttpRequest* req);
static MetalsharpResponse* handle_installer_install_vcpp_x64(const HttpRequest* req);
static MetalsharpResponse* handle_installer_install_vcpp_x86(const HttpRequest* req);

/* ── Response builders ── */

/*
 * Build a successful MetalsharpResponse whose `data` payload is a
 * freshly-parsed JsonValue tree representing `body`. The HTTP
 * layer then serialises the tree under a top-level "data" key.
 * `body` must be a valid JSON document; an empty or syntactically-
 * bad body yields an ok=false response with a descriptive
 * error_msg. Returns NULL only on calloc failure.
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

/* ── Install-progress helpers ── */

/*
 * Mark a background install as started. Resets percent to 0,
 * swaps in the new step label, and flips the in-process
 * installing flag to true. The four /setup/install-* handlers
 * share a single in-process install worker at this stage, so
 * the most recently-started request owns the progress state;
 * persistence is left to /setup/save once the sqlite plumbing
 * is wired in.
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

/* ── Route handlers ── */

/*
 * GET /setup/dependencies
 *   Report the bundled dependency state. The stub returns the
 *   documented "deps" object with every dependency un-met; the
 *   real implementation enumerates the wineprefix and the
 *   redist helper to fill the bools.
 */
static MetalsharpResponse* handle_installer_dependencies(const HttpRequest* req) {
    (void)req;
    static const char body[] = "{\"ok\":true,\"deps\":{"
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

/*
 * GET /setup/agility-versions
 *   List the installed D3D12 Agility SDK versions. The stub
 *   returns an empty versions array until dxmt_doctor (or its
 *   successor) populates the inventory.
 */
static MetalsharpResponse* handle_installer_agility_versions(const HttpRequest* req) {
    (void)req;
    return make_data_response("{\"ok\":true,\"versions\":[]}");
}

/*
 * POST /setup/save
 *   Persist the wizard's accumulated state document. The stub
 *   validates that the body parses as a JSON object, enforces
 *   the size cap, and replies with {"saved":true}. The actual
 *   kv_put into setup_kv is wired up in a follow-up phase that
 *   uses g_installer_db.
 */
static MetalsharpResponse* handle_installer_save(const HttpRequest* req) {
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
    /* Re-serialise the parsed object so we can size the
     * canonical form against the cap; the actual sqlite write
     * is left to the follow-up phase. */
    char* canonical = json_serialize(parsed);
    json_free(parsed);
    if (canonical == NULL) {
        return make_error_response("invalid JSON body");
    }
    if (strlen(canonical) > INSTALLER_STATE_MAX_LEN) {
        free(canonical);
        return make_error_response("state too large");
    }
    (void)g_installer_db; /* captured for future persistence */
    free(canonical);
    return make_data_response("{\"ok\":true,\"saved\":true}");
}

/*
 * Common handler body for the four /setup/install-* routes.
 * Each one flips the in-process installing flag, resets
 * progress, and replies with {"started":true}; the actual
 * installer worker is wired in by a later phase.
 */
static MetalsharpResponse* handle_installer_install_generic(const char* step_label) {
    install_mark_started(step_label);
    return make_data_response("{\"ok\":true,\"started\":true}");
}

/*
 * POST /setup/install-all
 *   Start the full bundle (deps + runtime + vcpp). Delegates to
 *   the generic helper with step_label="all".
 */
static MetalsharpResponse* handle_installer_install_all(const HttpRequest* req) {
    (void)req;
    return handle_installer_install_generic("all");
}

/*
 * POST /setup/install-deps
 *   Start the dependency subset. Delegates to the generic
 *   helper with step_label="deps".
 */
static MetalsharpResponse* handle_installer_install_deps(const HttpRequest* req) {
    (void)req;
    return handle_installer_install_generic("deps");
}

/*
 * POST /setup/install-vcpp-x64
 *   Start the x64 Visual C++ runtime installer only. Delegates
 *   to the generic helper with step_label="vcpp_x64".
 */
static MetalsharpResponse* handle_installer_install_vcpp_x64(const HttpRequest* req) {
    (void)req;
    return handle_installer_install_generic("vcpp_x64");
}

/*
 * POST /setup/install-vcpp-x86
 *   Start the x86 Visual C++ runtime installer only. Delegates
 *   to the generic helper with step_label="vcpp_x86".
 */
static MetalsharpResponse* handle_installer_install_vcpp_x86(const HttpRequest* req) {
    (void)req;
    return handle_installer_install_generic("vcpp_x86");
}

/*
 * GET /setup/install-progress
 *   Report the live progress envelope. Reads percent and step
 *   under the install_lock so the four POST handlers cannot
 *   tear the response.
 */
static MetalsharpResponse* handle_installer_install_progress(const HttpRequest* req) {
    (void)req;
    int percent = 0;
    char step_buf[INSTALLER_LABEL_MAX];
    pthread_mutex_lock(&install_lock);
    percent = install_percent_v;
    memcpy(step_buf, install_step_v, sizeof(install_step_v));
    pthread_mutex_unlock(&install_lock);
    /* snprintf reads step_buf until its NUL terminator, which
     * lives within the 64-byte slot install_mark_started
     * maintains. Cap with %.60s so a stray unterminated buffer
     * cannot run past the slot into adjacent stack. */
    char body[160];
    int n = snprintf(body, sizeof(body), "{\"ok\":true,\"percent\":%d,\"step\":\"%.60s\"}", percent, step_buf);
    if (n < 0 || (size_t)n >= sizeof(body)) {
        return make_error_response("internal error");
    }
    return make_data_response(body);
}

/*
 * GET /setup/installing
 *   Report whether any install worker is currently in flight.
 *   Reads the installing flag under install_lock so the four
 *   POST handlers cannot tear the value.
 */
static MetalsharpResponse* handle_installer_installing(const HttpRequest* req) {
    (void)req;
    bool flag = false;
    pthread_mutex_lock(&install_lock);
    flag = installing_v;
    pthread_mutex_unlock(&install_lock);
    char body[64];
    int n = snprintf(body, sizeof(body), "{\"ok\":true,\"installing\":%s}", flag ? "true" : "false");
    if (n < 0 || (size_t)n >= sizeof(body)) {
        return make_error_response("internal error");
    }
    return make_data_response(body);
}

/* ── Route registration ── */

void installer_backend_register_routes(HttpServer* server, Database* db) {
    if (server == NULL || db == NULL) {
        LOG_WARN("installer_backend_register_routes called with NULL arguments");
        return;
    }
    g_installer_db = db;

    http_server_register(server, "GET", "/setup/dependencies", handle_installer_dependencies);
    http_server_register(server, "GET", "/setup/agility-versions", handle_installer_agility_versions);
    http_server_register(server, "POST", "/setup/save", handle_installer_save);
    http_server_register(server, "POST", "/setup/install-all", handle_installer_install_all);
    http_server_register(server, "POST", "/setup/install-deps", handle_installer_install_deps);
    http_server_register(server, "GET", "/setup/install-progress", handle_installer_install_progress);
    http_server_register(server, "GET", "/setup/installing", handle_installer_installing);
    http_server_register(server, "POST", "/setup/install-vcpp-x64", handle_installer_install_vcpp_x64);
    http_server_register(server, "POST", "/setup/install-vcpp-x86", handle_installer_install_vcpp_x86);
}
