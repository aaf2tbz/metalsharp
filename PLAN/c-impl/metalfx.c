/*
 * metalfx.c — MetalFX upscaling toggle module for the Metalsharp C backend
 *
 * WHAT
 *   Implements the two MetalFX HTTP routes mounted under /metalfx/
 *   listed in contracts/electron-backend.v1.json. MetalFX is Apple's
 *   spatial / temporal upscaler used by the maintained-C launch
 *   pipeline; this module exposes a read endpoint (state) and a
 *   write endpoint (toggle) so the Electron shell can surface the
 *   status and let the user flip the feature without dropping into
 *   a console. Currently both endpoints are stubs: the read endpoint
 *   always reports `enabled=false`, and the toggle acknowledges with
 *   `{"ok":true}` so the UI flow works. The real DXMT / Metal
 *   introspection will replace both stubs once the upscaler is
 *   wired into the launch policy.
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
 *   metalfx_register_routes(HttpServer *server, Database *db)
 *       Register both /metalfx/state (GET) and /metalfx/toggle (POST)
 *       routes on the supplied HttpServer and bind the module to `db`
 *       for forward compatibility. Called once during startup,
 *       before http_server_run(). Passing NULL on either side is a
 *       silent no-op so a half-initialised backend cannot crash
 *       inside the registry.
 *
 * SCHEMA
 *   Every route wraps its reply in a MetalsharpResponse whose `data`
 *   pointer is a freshly-parsed JsonValue tree representing the JSON
 *   payload below. The HTTP layer serialises that tree under a
 *   top-level "data" key so a handler returning {"ok":true,"enabled":false}
 *   surfaces on the wire as {"ok":true,"data":{"ok":true,"enabled":false}}.
 *
 *     GET  /metalfx/state    data = {"ok":true,"enabled":false,
 *                                "scale_factor":1.0,"mode":"off"}
 *     POST /metalfx/toggle   data = {"ok":true}
 *
 *   Both replies carry HTTP 200. Failures return the shared failure
 *   envelope {"ok":false,"error":"<reason>"}.
 *
 * THREADING
 *   The HTTP server dispatches every request on a worker thread.
 *   The handlers below are stateless: they ignore the request body
 *   and never reach for module globals. Concurrent workers therefore
 *   never share ownership of any state.
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

static MetalsharpResponse* handle_metalfx_state(const HttpRequest* req);
static MetalsharpResponse* handle_metalfx_toggle(const HttpRequest* req);

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
 * GET /metalfx/state. Stub reporting the feature as disabled
 * because the upscaler has not been wired into the launch policy
 * yet. `scale_factor` defaults to 1.0 and `mode` to "off" so the
 * UI renders a deterministic "MetalFX disabled" badge.
 */
static MetalsharpResponse* handle_metalfx_state(const HttpRequest* req) {
    (void)req;
    return make_data_response("{\"ok\":true,\"enabled\":false,"
                              "\"scale_factor\":1.0,\"mode\":\"off\"}");
}

/*
 * POST /metalfx/toggle. Stub acknowledgement. The real
 * implementation will flip the in-process flag, persist the
 * choice through the supplied Database handle, and re-emit the
 * new state — until then every toggle returns {"ok":true} so the
 * Electron shell's toggle control can probe without crashing.
 */
static MetalsharpResponse* handle_metalfx_toggle(const HttpRequest* req) {
    (void)req;
    return make_data_response("{\"ok\":true}");
}

/* ── Route registration ── */

/*
 * Register both /metalfx/ routes on the server. NULL arguments are
 * a silent no-op so a half-initialised backend cannot crash inside
 * the registry. Every registration happens before
 * http_server_run() so workers see a fully populated route table
 * the moment the server starts.
 */
void metalfx_register_routes(HttpServer* server, Database* db) {
    (void)db;
    if (server == NULL) {
        LOG_WARN("metalfx_register_routes called with NULL server");
        return;
    }

    http_server_register(server, "GET", "/metalfx/state", handle_metalfx_state);
    http_server_register(server, "POST", "/metalfx/toggle", handle_metalfx_toggle);

    LOG_INFO("metalfx routes registered (2)");
}
