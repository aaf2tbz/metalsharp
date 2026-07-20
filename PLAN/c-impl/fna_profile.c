/*
 * fna_profile.c — FNA game classification module for the Metalsharp C backend
 *
 * WHAT
 *   Implements the three FNA classification HTTP routes mounted
 *   under /diagnostics/fna/ listed in contracts/electron-
 *   backend.v1.json. FNA is the XNA-reimplementation runtime used
 *   by some Steam Play titles; the classification engine reads
 *   signals (engine hints in manifest files, Mono runtime libraries,
 *   SDL2 native modules, .NET assemblies) and assigns each
 *   imported app one of three buckets: `native`, `fna`, or
 *   `unknown`. Until the signal extractor lands every route
 *   returns an empty-signals / unknown-classification stub so the
 *   Electron shell's FNA panel can probe without producing false
 *   positives.
 *
 * IMPORTS
 *   "server.h"        MetalsharpResponse, METALSHARP_VERSION
 *   "http_server.h"   HttpServer, HttpRequest, http_server_register
 *   "database.h"      Database, db_exec, db_query
 *   "json.h"          json_parse, JsonValue, accessors
 *   "logger.h"        LOG_INFO, LOG_WARN
 *   <stdbool.h>       bool, true, false
 *   <stddef.h>        size_t, NULL
 *   <stdio.h>         snprintf
 *   <stdlib.h>        calloc, free
 *   <string.h>        strlen
 *
 * EXPORTS
 *   fna_profile_register_routes(HttpServer *server, Database *db)
 *       Register every /diagnostics/fna/classify,
 *       /diagnostics/fna/explain, and /diagnostics/fna/signals
 *       route on the supplied HttpServer and bind the module to
 *       `db` for forward compatibility. Called once during
 *       startup, before http_server_run(). Passing NULL on either
 *       side is a silent no-op so a half-initialised backend
 *       cannot crash inside the registry.
 *
 * SCHEMA
 *   Every route wraps its reply in a MetalsharpResponse whose
 *   `data` pointer is a freshly-parsed JsonValue tree representing
 *   the JSON payload below. The HTTP layer serialises that tree
 *   under a top-level "data" key so a handler returning
 *   {"ok":true,"signals":[]} surfaces on the wire as
 *   {"ok":true,"data":{"ok":true,"signals":[]}}.
 *
 *     GET /diagnostics/fna/classify  data = {"ok":true,"signals":[],
 *                                          "classification":"unknown"}
 *     GET /diagnostics/fna/explain   data = {"ok":true,"signals":[],
 *                                          "classification":"unknown",
 *                                          "reason":"no signals collected"}
 *     GET /diagnostics/fna/signals   data = {"ok":true,"signals":[]}
 *
 *   Every reply carries HTTP 200. Failures return the shared
 *   failure envelope {"ok":false,"error":"<reason>"}.
 *
 * THREADING
 *   The HTTP server dispatches every request on a worker thread.
 *   The handlers below are stateless: they ignore the request,
 *   do not touch the supplied Database handle, and never reach
 *   for module globals. Concurrent workers therefore never share
 *   ownership of any state.
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

/* ── Forward declarations ── */

static MetalsharpResponse* handle_fna_classify(const HttpRequest* req);
static MetalsharpResponse* handle_fna_explain(const HttpRequest* req);
static MetalsharpResponse* handle_fna_signals(const HttpRequest* req);

/* ── Response builders ── */

/*
 * Build a successful MetalsharpResponse whose `data` payload is a
 * freshly-parsed JsonValue tree representing `body`. The HTTP layer
 * serialises the tree under a top-level "data" field. `body` must
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

/* ── Route handlers ── */

/*
 * GET /diagnostics/fna/classify. Stub returning an empty signals
 * array paired with an `unknown` classification because no signal
 * extractor is wired up yet. The Electron shell renders the
 * classification badge as "Unknown" until real data arrives.
 */
static MetalsharpResponse* handle_fna_classify(const HttpRequest* req) {
    (void)req;
    return make_data_response("{\"ok\":true,\"signals\":[],"
                              "\"classification\":\"unknown\"}");
}

/*
 * GET /diagnostics/fna/explain. Stub returning the explanation
 * shape (signals + classification + human-readable reason). The
 * reason field defaults to a self-describing placeholder so the
 * UI never presents a missing field as an error.
 */
static MetalsharpResponse* handle_fna_explain(const HttpRequest* req) {
    (void)req;
    return make_data_response("{\"ok\":true,\"signals\":[],"
                              "\"classification\":\"unknown\","
                              "\"reason\":\"no signals collected\"}");
}

/*
 * GET /diagnostics/fna/signals. Stub returning an empty signals
 * array because no signal collector is wired up yet. Each entry
 * in the live implementation will carry a signal name, weight,
 * and source path so the UI can render the per-feature evidence
 * list.
 */
static MetalsharpResponse* handle_fna_signals(const HttpRequest* req) {
    (void)req;
    return make_data_response("{\"ok\":true,\"signals\":[]}");
}

/* ── Route registration ── */

/*
 * Register every /diagnostics/fna/ route on the server. NULL
 * arguments are a silent no-op so a half-initialised backend
 * cannot crash inside the registry. Every registration happens
 * before http_server_run() so workers see a fully populated
 * route table the moment the server starts.
 */
void fna_profile_register_routes(HttpServer* server, Database* db) {
    (void)db;
    if (server == NULL) {
        LOG_WARN("fna_profile_register_routes called with NULL server");
        return;
    }

    http_server_register(server, "GET", "/diagnostics/fna/classify", handle_fna_classify);
    http_server_register(server, "GET", "/diagnostics/fna/explain", handle_fna_explain);
    http_server_register(server, "GET", "/diagnostics/fna/signals", handle_fna_signals);

    LOG_INFO("fna profile routes registered (3)");
}
