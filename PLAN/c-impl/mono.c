/*
 * mono.c — Wine Mono management routes (stub)
 *
 * WHAT
 *   Implements the three Wine Mono management HTTP routes mounted
 *   under /wine-mono/. The Mono runtime is bundled with upstream
 *   Wine and is what .NET Framework applications (managed XNA /
 *   MonoGame payloads, third-party .NET launchers) require to
 *   execute inside a bottle. The routes here let the Electron
 *   shell probe whether Mono is present, request a fresh
 *   install/refresh of the bundled Mono MSI into a bottle prefix,
 *   and reset the Mono install back to a clean state.
 *
 *   The real engine has not been ported yet; this file ships as
 *   a stub that returns the documented JSON envelope for each
 *   route so the Electron panels can probe without 404s. Once
 *   the Wine Mono install / reset workers are written (likely
 *   alongside the bottles_backend.c engine) the stubs will be
 *   replaced with real Wine MSI invocations.
 *
 * IMPORTS
 *   "server.h"        MetalsharpResponse, METALSHARP_VERSION
 *   "http_server.h"   HttpServer, HttpRequest, http_server_register
 *   "database.h"      Database (forward declaration only)
 *   "json.h"          json_parse, JsonValue
 *   "logger.h"        LOG_INFO, LOG_WARN
 *   <stdbool.h>       bool, true, false
 *   <stddef.h>        size_t, NULL
 *   <stdio.h>         snprintf
 *   <stdlib.h>        calloc, free
 *   <string.h>        strlen, strdup
 *
 * EXPORTS
 *   mono_register_routes(HttpServer *server, Database *db)
 *       Register the /wine-mono/status, /wine-mono/install, and
 *       /wine-mono/reset routes on the supplied HttpServer and
 *       bind the module to `db` for future state persistence.
 *       Called once during startup, before http_server_run().
 *       Passing NULL on either side is a silent no-op so a
 *       half-initialised backend cannot crash inside the registry.
 *
 * SCHEMA
 *   Route response shapes — every reply carries HTTP 200; failures
 *   return {"ok":false,"error":"<reason>"}. The HTTP layer wraps
 *   every successful handler payload under a top-level "data" key
 *   per the shared MetalsharpResponse envelope.
 *
 *     GET  /wine-mono/status    data = {"ok":true,"installed":false,
 *                                  "version":"","bottle":""}
 *     POST /wine-mono/install   data = {"ok":true,"started":false,
 *                                  "jobId":""}
 *     POST /wine-mono/reset     data = {"ok":true,"reset":false}
 *
 * THREADING
 *   The HTTP server dispatches every request on a worker thread.
 *   The handlers below are stateless: they ignore the request
 *   body, do not touch the supplied Database handle, and never
 *   reach for module globals. They return a freshly-allocated
 *   MetalsharpResponse so the concurrent workers never share
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

static MetalsharpResponse* handle_mono_status(const HttpRequest* req);
static MetalsharpResponse* handle_mono_install(const HttpRequest* req);
static MetalsharpResponse* handle_mono_reset(const HttpRequest* req);

/* ── Response builder ── */

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

/* ── Route handlers ── */

/*
 * GET /wine-mono/status
 *   Probe whether Mono is installed in the active bottle. The
 *   stub reports installed=false with empty version and bottle so
 *   the panel renders the "Mono not installed" state until the
 *   real per-prefix Mono MSI detection lands.
 */
static MetalsharpResponse* handle_mono_status(const HttpRequest* req) {
    (void)req;
    static const char body[] = "{\"ok\":true,"
                               "\"installed\":false,"
                               "\"version\":\"\","
                               "\"bottle\":\"\"}";
    return make_data_response(body);
}

/*
 * POST /wine-mono/install
 *   Kick off a Mono install into the active bottle. The stub
 *   reports started=false until the Wine MSI worker is wired up;
 *   that future worker is responsible for flipping started=true
 *   and assigning a jobId the panel can poll for progress.
 */
static MetalsharpResponse* handle_mono_install(const HttpRequest* req) {
    (void)req;
    static const char body[] = "{\"ok\":true,"
                               "\"started\":false,"
                               "\"jobId\":\"\"}";
    return make_data_response(body);
}

/*
 * POST /wine-mono/reset
 *   Reset the Mono install back to a clean state in the active
 *   bottle. The stub reports reset=false until the
 *   prefix-rewriting worker is implemented.
 */
static MetalsharpResponse* handle_mono_reset(const HttpRequest* req) {
    (void)req;
    static const char body[] = "{\"ok\":true,\"reset\":false}";
    return make_data_response(body);
}

/* ── Route registration ── */

/*
 * Register every /wine-mono/ route on the server. NULL arguments
 * are a silent no-op so a half-initialised backend cannot crash
 * inside the registry. Every registration happens before
 * http_server_run() so workers see a fully populated route table
 * the moment the server starts.
 */
void mono_register_routes(HttpServer* server, Database* db) {
    (void)db;
    if (server == NULL) {
        LOG_WARN("mono_register_routes called with NULL server");
        return;
    }

    http_server_register(server, "GET", "/wine-mono/status", handle_mono_status);
    http_server_register(server, "POST", "/wine-mono/install", handle_mono_install);
    http_server_register(server, "POST", "/wine-mono/reset", handle_mono_reset);

    LOG_INFO("wine-mono routes registered (3)");
}
