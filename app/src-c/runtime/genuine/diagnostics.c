/*
 * diagnostics.c — Diagnostic routes module for the Metalsharp C backend
 *
 * WHAT
 *   Implements the eight diagnostic HTTP routes mounted under
 *   /diagnostics/ listed in contracts/electron-backend.v1.json. Every
 *   route returns a stable JSON stub so the Electron shell's
 *   diagnostic panels can probe without receiving a 404. Each stub
 *   preserves the wire shape the real implementations will honour
 *   (`{"ok":true,...}` for GET reads, `{"ok":true}` for POST
 *   validators). The full launch timing, runtime-artifact, wineboot-
 *   state, PSO-manifest, cache-doctor, binding-contract, and
 *   command-replay engines live in their own modules and will replace
 *   these stubs once their pipelines are ported from the legacy Node
 *   backend.
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
 *   diagnostics_register_routes(HttpServer *server, Database *db)
 *       Register every /diagnostics/launch, /diagnostics/launch/timing,
 *       /diagnostics/runtime-artifacts, /diagnostics/wineboot-state,
 *       /diagnostics/pso-manifests, /diagnostics/cache-doctor,
 *       /diagnostics/binding-contract/validate, and
 *       /diagnostics/command-replay/validate route on the supplied
 *       HttpServer and bind the module to `db` for forward
 *       compatibility. Called once during startup, before
 *       http_server_run(). Passing NULL on either side is a silent
 *       no-op so a half-initialised backend cannot crash inside the
 *       registry.
 *
 * SCHEMA
 *   Every route wraps its reply in a MetalsharpResponse whose `data`
 *   pointer is a freshly-parsed JsonValue tree representing the JSON
 *   payload below. The HTTP layer serialises that tree under a
 *   top-level "data" key so a handler returning {"ok":true,"launched":false}
 *   surfaces on the wire as {"ok":true,"data":{"ok":true,"launched":false}}.
 *
 *     GET  /diagnostics/launch                  data = {"ok":true,"launched":false,
 *                                              "appid":0,"engine":"","pipeline":""}
 *     GET  /diagnostics/launch/timing           data = {"ok":true,"samples":[],
 *                                              "count":0,"average_ms":0.0}
 *     GET  /diagnostics/runtime-artifacts       data = {"ok":true,"artifacts":[],
 *                                              "directories":[]}
 *     GET  /diagnostics/wineboot-state          data = {"ok":true,"state":"unknown",
 *                                              "last_boot":0,"prefix":""}
 *     GET  /diagnostics/pso-manifests           data = {"ok":true,"manifests":[],
 *                                              "stale":[]}
 *     GET  /diagnostics/cache-doctor            data = {"ok":true,"healthy":true,
 *                                              "issues":[],"bytes":0}
 *     POST /diagnostics/binding-contract/validate  data = {"ok":true,"errors":[],
 *                                              "warnings":[]}
 *     POST /diagnostics/command-replay/validate data = {"ok":true,"valid":true,
 *                                              "issues":[]}
 *
 * THREADING
 *   The HTTP server dispatches every request on a worker thread. The
 *   handlers below are stateless: they ignore the request body, do not
 *   touch the supplied Database handle, and never reach for module
 *   globals. They return a freshly-allocated MetalsharpResponse so the
 *   concurrent workers never share ownership of any state.
 */

#include "database.h"
#include "diagnostics_empty_state.h"
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

static MetalsharpResponse* handle_diag_launch(const HttpRequest* req);
static MetalsharpResponse* handle_diag_launch_timing(const HttpRequest* req);
static MetalsharpResponse* handle_diag_runtime_artifacts(const HttpRequest* req);
static MetalsharpResponse* handle_diag_wineboot_state(const HttpRequest* req);
static MetalsharpResponse* handle_diag_pso_manifests(const HttpRequest* req);
static MetalsharpResponse* handle_diag_cache_doctor(const HttpRequest* req);
static MetalsharpResponse* handle_diag_binding_contract_validate(const HttpRequest* req);
static MetalsharpResponse* handle_diag_command_replay_validate(const HttpRequest* req);

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
    r->data_kind = METALSHARP_RESPONSE_JSON_VALUE;
    return r;
}

static MetalsharpResponse* make_template_response(const char* template) {
    static const char token[] = "@HOME@";
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL)
        home = getenv("HOME");
    if (home == NULL)
        home = "";
    size_t token_length = sizeof(token) - 1u;
    size_t home_length = strlen(home);
    size_t count = 0;
    for (const char* cursor = template; (cursor = strstr(cursor, token)) != NULL; cursor += token_length)
        count++;
    size_t output_length = strlen(template) + count * home_length - count * token_length;
    char* output = malloc(output_length + 1u);
    if (output == NULL)
        return NULL;
    const char* source = template;
    char* destination = output;
    const char* match = NULL;
    while ((match = strstr(source, token)) != NULL) {
        size_t prefix_length = (size_t)(match - source);
        memcpy(destination, source, prefix_length);
        destination += prefix_length;
        memcpy(destination, home, home_length);
        destination += home_length;
        source = match + token_length;
    }
    strcpy(destination, source);
    MetalsharpResponse* response = make_data_response(output);
    free(output);
    return response;
}

/* ── Route handlers ── */

/*
 * GET /diagnostics/launch. Stub returning the launch summary shape
 * the diagnostic panel reads. `launched` stays false because no
 * persistent launch tracker exists yet; `appid`/`engine`/`pipeline`
 * default to empty/zero to keep the wire contract stable.
 */
static MetalsharpResponse* handle_diag_launch(const HttpRequest* req) {
    (void)req;
    return make_template_response(DIAG_DIAGNOSTICS_LAUNCH_JSON);
}

/*
 * GET /diagnostics/launch/timing. Stub returning the empty timing
 * sample shape. `samples` is an empty array because no real timing
 * recorder is wired up yet; the panel renders "no data" cleanly
 * when both count and the array are zero.
 */
static MetalsharpResponse* handle_diag_launch_timing(const HttpRequest* req) {
    (void)req;
    return make_template_response(DIAG_DIAGNOSTICS_LAUNCH_TIMING_JSON);
}

/*
 * GET /diagnostics/runtime-artifacts. Stub returning the empty
 * artifacts shape. The real implementation will enumerate the
 * directory tree under $METALSHARP_HOME/runtime and emit per-file
 * descriptors; for now both lists are empty.
 */
static MetalsharpResponse* handle_diag_runtime_artifacts(const HttpRequest* req) {
    (void)req;
    return make_template_response(DIAG_DIAGNOSTICS_RUNTIME_ARTIFACTS_JSON);
}

/*
 * GET /diagnostics/wineboot-state. Stub reporting `unknown` because
 * no Wine prefix has been probed. `last_boot` stays 0 and `prefix`
 * stays empty so the panel's badge renders the unknown state.
 */
static MetalsharpResponse* handle_diag_wineboot_state(const HttpRequest* req) {
    (void)req;
    return make_template_response(DIAG_DIAGNOSTICS_WINEBOOT_STATE_JSON);
}

/*
 * GET /diagnostics/pso-manifests. Stub returning the empty manifest
 * shape. Real implementation will scan the PSO cache directory for
 * pipeline-state-object manifest files and report fresh and stale
 * entries separately; both lists start empty.
 */
static MetalsharpResponse* handle_diag_pso_manifests(const HttpRequest* req) {
    (void)req;
    return make_template_response(DIAG_DIAGNOSTICS_PSO_MANIFESTS_JSON);
}

/*
 * GET /diagnostics/cache-doctor. Stub reporting a healthy cache with
 * no issues. The real implementation will verify shader / pipeline
 * cache integrity and report per-issue descriptors; the empty
 * issues array + healthy=true keeps the panel in the "no work"
 * state.
 */
static MetalsharpResponse* handle_diag_cache_doctor(const HttpRequest* req) {
    (void)req;
    return make_template_response(DIAG_DIAGNOSTICS_CACHE_DOCTOR_JSON);
}

/*
 * POST /diagnostics/binding-contract/validate. Stub returning the
 * empty validation shape so the diagnostic panel can probe without
 * producing false positives. Real implementation will parse the
 * provided contract bundle and report errors and warnings per
 * field.
 */
static MetalsharpResponse* handle_diag_binding_contract_validate(const HttpRequest* req) {
    (void)req;
    MetalsharpResponse* response = make_template_response(DIAG_DIAGNOSTICS_BINDING_CONTRACT_VALIDATE_JSON);
    if (response != NULL)
        response->http_status = 400;
    return response;
}

/*
 * POST /diagnostics/command-replay/validate. Stub returning the
 * valid shape so the panel can probe. Real implementation will
 * replay the supplied command array, compare outputs against
 * recorded baselines, and populate `issues` with any divergences.
 */
static MetalsharpResponse* handle_diag_command_replay_validate(const HttpRequest* req) {
    (void)req;
    return make_template_response(DIAG_DIAGNOSTICS_COMMAND_REPLAY_VALIDATE_JSON);
}

/* ── Route registration ── */

/*
 * Register every /diagnostics/ route on the server. NULL arguments
 * are a silent no-op so a half-initialised backend cannot crash
 * inside the registry. Every registration happens before
 * http_server_run() so workers see a fully populated route table
 * the moment the server starts.
 */
void diagnostics_register_routes(HttpServer* server, Database* db) {
    (void)db;
    if (server == NULL) {
        LOG_WARN("diagnostics_register_routes called with NULL server");
        return;
    }

    http_server_register(server, "GET", "/diagnostics/launch", handle_diag_launch);
    http_server_register(server, "GET", "/diagnostics/launch/timing", handle_diag_launch_timing);
    http_server_register(server, "GET", "/diagnostics/runtime-artifacts", handle_diag_runtime_artifacts);
    http_server_register(server, "GET", "/diagnostics/wineboot-state", handle_diag_wineboot_state);
    http_server_register(server, "GET", "/diagnostics/pso-manifests", handle_diag_pso_manifests);
    http_server_register(server, "GET", "/diagnostics/cache-doctor", handle_diag_cache_doctor);
    http_server_register(server, "POST", "/diagnostics/binding-contract/validate",
                         handle_diag_binding_contract_validate);
    http_server_register(server, "POST", "/diagnostics/command-replay/validate", handle_diag_command_replay_validate);

    LOG_INFO("diagnostics routes registered (8)");
}
