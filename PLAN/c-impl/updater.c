/*
 * updater.c — Self-update, migration and cache subsystem for the
 * Metalsharp C backend.
 *
 * WHAT
 *   Implements the 9 self-update and migration routes mounted under
 *   the /update prefix and the 2 cache-management routes mounted
 *   under the /cache prefix, documented in GENUINE_C_PLAN.md. The
 *   module is responsible for
 *   probing whether a newer backend release is available, kicking
 *   off and tracking the self-update flow, reporting and clearing
 *   the local installer DMG staging path, providing a controlled
 *   migration check / progress / report cycle for moving from a
 *   previous backend install, and exposing the size and clear
 *   endpoints for the on-disk shader and pipeline caches.
 *
 * IMPORTS
 *   "server.h"        MetalsharpResponse, METALSHARP_VERSION
 *   "http_server.h"   HttpServer, HttpRequest, http_server_register
 *   "database.h"      Database (captured for future persistence)
 *   "json.h"          json_parse, JsonValue for response payloads
 *   "logger.h"        LOG_WARN
 *   <stdbool.h>       bool, true, false
 *   <stddef.h>        size_t, NULL
 *   <stdio.h>         snprintf
 *   <stdlib.h>        calloc, free
 *   <string.h>        strlen, strdup
 *
 * EXPORTS
 *   updater_register_routes(HttpServer *server, Database *db)
 *       Register every update- and cache-prefixed route on the supplied
 *       HttpServer and bind the module to `db` for future state
 *       persistence. Called once during startup, before
 *       http_server_run(). NULL arguments are a silent no-op so a
 *       half-initialised backend cannot crash inside the registry.
 *
 * SCHEMA
 *   Route response shapes - every route wraps its reply in a
 *   MetalsharpResponse whose `data` pointer is a JsonValue tree
 *   representing the spec fields below. The HTTP layer then
 *   serialises that tree under its top-level "data" key:
 *     GET  /update/check              data = {"ok":true,
 *                                       "available":false,
 *                                       "version":"",
 *                                       "url":"",
 *                                       "notes":""}
 *     POST /update/start              data = {"ok":true,
 *                                       "started":false,
 *                                       "jobId":""}
 *     GET  /update/progress           data = {"ok":true,
 *                                       "percent":0,
 *                                       "step":"idle",
 *                                       "version":"",
 *                                       "error":""}
 *     POST /update/cleanup            data = {"ok":true,
 *                                       "cleaned":false,
 *                                       "removed":0}
 *     GET  /update/dmg-path           data = {"ok":true,
 *                                       "path":""}
 *     GET  /update/migrate/check      data = {"ok":true,
 *                                       "needed":false,
 *                                       "reason":""}
 *     GET  /update/migrate/progress   data = {"ok":true,
 *                                       "status":"idle",
 *                                       "step":0,
 *                                       "total":0,
 *                                       "message":"",
 *                                       "error":"",
 *                                       "version":""}
 *     GET  /update/migrate/report     data = {"ok":true,
 *                                       "status":"",
 *                                       "summary":[],
 *                                       "entries":[],
 *                                       "schema_version":"",
 *                                       "version":""}
 *     POST /update/migrate/start      data = {"ok":true,
 *                                       "started":true}
 *     GET  /cache/size                data = {"ok":true,
 *                                       "shader_cache":0,
 *                                       "pipeline_cache":0}
 *     POST /cache/clear               data = {"ok":true,
 *                                       "cleared":false,
 *                                       "bytes_freed":0}
 *   All replies carry HTTP 200; failures return
 *   {"ok":false,"error":"<reason>"}.
 *
 * THREADING
 *   The HTTP server dispatches every request on a worker thread.
 *   The Database handle used here is captured exactly once at
 *   registration time and never swapped, so workers always see a
 *   stable handle without going through a barrier.
 *
 * NOTE
 *   This file ships as a stub implementation that returns the
 *   documented JSON shape for every route. Real update checkers,
 *   background workers, persistent progress state, on-disk cache
 *   enumeration, and migration tooling are wired in by a later
 *   phase that uses the Database handle captured here.
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

/* ── Static module state ── */

/*
 * Database handle captured at registration time. Held for future
 * migrations/update state persistence; currently unused at runtime,
 * which silences unused-variable warnings while preserving the
 * capture for downstream phases.
 */
static Database* g_updater_db = NULL;

/* ── Forward declarations ── */

static MetalsharpResponse* handle_update_check(const HttpRequest* req);
static MetalsharpResponse* handle_update_start(const HttpRequest* req);
static MetalsharpResponse* handle_update_progress(const HttpRequest* req);
static MetalsharpResponse* handle_update_cleanup(const HttpRequest* req);
static MetalsharpResponse* handle_update_dmg_path(const HttpRequest* req);
static MetalsharpResponse* handle_update_migrate_check(const HttpRequest* req);
static MetalsharpResponse* handle_update_migrate_progress(const HttpRequest* req);
static MetalsharpResponse* handle_update_migrate_report(const HttpRequest* req);
static MetalsharpResponse* handle_update_migrate_start(const HttpRequest* req);
static MetalsharpResponse* handle_cache_size(const HttpRequest* req);
static MetalsharpResponse* handle_cache_clear(const HttpRequest* req);

/* ── Response builders ── */

/*
 * Build a successful MetalsharpResponse whose `data` payload is a
 * freshly-parsed JsonValue tree representing `body`. The HTTP
 * layer then serialises the tree under a top-level "data" key.
 * `body` must be a valid JSON document; an empty or syntactically-
 * bad body yields an ok=false response with a descriptive
 * error_msg. Returns NULL only on calloc failure. Every stub
 * handler in this file uses only the success path; the error
 * builder is introduced in the phase that wires real update and
 * migration workers.
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
 * GET /update/check
 *   Probe whether a newer backend release is available. Returns
 *   the documented envelope with available=false and empty
 *   version/url/notes until the real manifest lookup is wired in.
 */
static MetalsharpResponse* handle_update_check(const HttpRequest* req) {
    (void)req;
    static const char body[] = "{\"ok\":true,"
                               "\"available\":false,"
                               "\"version\":\"\","
                               "\"url\":\"\","
                               "\"notes\":\"\"}";
    return make_data_response(body);
}

/*
 * POST /update/start
 *   Kick off a self-update. The stub returns the documented
 *   envelope with started=false until the background worker is
 *   hooked up; that future worker is responsible for flipping
 *   started=true and assigning jobId.
 */
static MetalsharpResponse* handle_update_start(const HttpRequest* req) {
    (void)req;
    static const char body[] = "{\"ok\":true,"
                               "\"started\":false,"
                               "\"jobId\":\"\"}";
    return make_data_response(body);
}

/*
 * GET /update/progress
 *   Report the live update progress envelope. The stub returns
 *   percent=0/step="idle" until the worker writes real values
 *   into the static module state.
 */
static MetalsharpResponse* handle_update_progress(const HttpRequest* req) {
    (void)req;
    static const char body[] = "{\"ok\":true,"
                               "\"percent\":0,"
                               "\"step\":\"idle\","
                               "\"version\":\"\","
                               "\"error\":\"\"}";
    return make_data_response(body);
}

/*
 * POST /update/cleanup
 *   Remove a previously-staged update payload. The stub returns
 *   cleaned=false/removed=0 until the staging directory scan is
 *   implemented.
 */
static MetalsharpResponse* handle_update_cleanup(const HttpRequest* req) {
    (void)req;
    static const char body[] = "{\"ok\":true,"
                               "\"cleaned\":false,"
                               "\"removed\":0}";
    return make_data_response(body);
}

/*
 * GET /update/dmg-path
 *   Report the on-disk path of the staged installer DMG, or an
 *   empty string when no DMG is currently staged. The stub
 *   returns the empty-path envelope until the staging code runs.
 */
static MetalsharpResponse* handle_update_dmg_path(const HttpRequest* req) {
    (void)req;
    static const char body[] = "{\"ok\":true,\"path\":\"\"}";
    return make_data_response(body);
}

/*
 * GET /update/migrate/check
 *   Decide whether a data-migration from the user's previous
 *   backend install is required. The stub returns needed=false
 *   until the migration-detection scan is implemented.
 */
static MetalsharpResponse* handle_update_migrate_check(const HttpRequest* req) {
    (void)req;
    static const char body[] = "{\"ok\":true,"
                               "\"needed\":false,"
                               "\"reason\":\"\"}";
    return make_data_response(body);
}

/*
 * GET /update/migrate/progress
 *   Report the migration progress envelope. The stub returns
 *   status="idle" with step=0/total=0 until the worker fills them
 *   in.
 */
static MetalsharpResponse* handle_update_migrate_progress(const HttpRequest* req) {
    (void)req;
    static const char body[] = "{\"ok\":true,"
                               "\"status\":\"idle\","
                               "\"step\":0,"
                               "\"total\":0,"
                               "\"message\":\"\","
                               "\"error\":\"\","
                               "\"version\":\"\"}";
    return make_data_response(body);
}

/*
 * GET /update/migrate/report
 *   Return the human-readable migration report once the worker
 *   finishes. The stub returns the documented envelope with an
 *   empty status, empty summary/entries arrays, and empty
 *   schema_version/version fields.
 */
static MetalsharpResponse* handle_update_migrate_report(const HttpRequest* req) {
    (void)req;
    static const char body[] = "{\"ok\":true,"
                               "\"status\":\"\","
                               "\"summary\":[],"
                               "\"entries\":[],"
                               "\"schema_version\":\"\","
                               "\"version\":\"\"}";
    return make_data_response(body);
}

/*
 * POST /update/migrate/start
 *   Kick off a migration pass. The stub returns started=true so
 *   the wizard's UI can transition into its progress view; the
 *   actual background worker will be added in a follow-up phase.
 */
static MetalsharpResponse* handle_update_migrate_start(const HttpRequest* req) {
    (void)req;
    static const char body[] = "{\"ok\":true,\"started\":true}";
    return make_data_response(body);
}

/*
 * GET /cache/size
 *   Report the on-disk size of the shader and pipeline caches
 *   in bytes. The stub returns 0 for both until the disk scan
 *   lands.
 */
static MetalsharpResponse* handle_cache_size(const HttpRequest* req) {
    (void)req;
    static const char body[] = "{\"ok\":true,"
                               "\"shader_cache\":0,"
                               "\"pipeline_cache\":0}";
    return make_data_response(body);
}

/*
 * POST /cache/clear
 *   Drop the on-disk shader and pipeline caches. The stub returns
 *   cleared=false/bytes_freed=0 until the cache eviction code is
 *   implemented.
 */
static MetalsharpResponse* handle_cache_clear(const HttpRequest* req) {
    (void)req;
    static const char body[] = "{\"ok\":true,"
                               "\"cleared\":false,"
                               "\"bytes_freed\":0}";
    return make_data_response(body);
}

/* ── Route registration ── */

void updater_register_routes(HttpServer* server, Database* db) {
    if (server == NULL || db == NULL) {
        LOG_WARN("updater_register_routes called with NULL arguments");
        return;
    }
    g_updater_db = db;
    (void)g_updater_db; /* captured for future persistence */

    http_server_register(server, "GET", "/update/check", handle_update_check);
    http_server_register(server, "POST", "/update/start", handle_update_start);
    http_server_register(server, "GET", "/update/progress", handle_update_progress);
    http_server_register(server, "POST", "/update/cleanup", handle_update_cleanup);
    http_server_register(server, "GET", "/update/dmg-path", handle_update_dmg_path);
    http_server_register(server, "GET", "/update/migrate/check", handle_update_migrate_check);
    http_server_register(server, "GET", "/update/migrate/progress", handle_update_migrate_progress);
    http_server_register(server, "GET", "/update/migrate/report", handle_update_migrate_report);
    http_server_register(server, "POST", "/update/migrate/start", handle_update_migrate_start);
    http_server_register(server, "GET", "/cache/size", handle_cache_size);
    http_server_register(server, "POST", "/cache/clear", handle_cache_clear);
}
