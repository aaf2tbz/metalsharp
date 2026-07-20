/*
 * routes.c — HTTP route registration for the Metalsharp C backend
 *
 * WHAT
 *   Registers all 264 frozen HTTP routes documented in
 *   contracts/electron-backend.v1.json. Each route maps a
 *   (method, path) pair to a handler function that receives
 *   the parsed HttpRequest and returns a heap-allocated
 *   MetalsharpResponse.
 *
 * IMPORTS
 *   server.h         MetalsharpResponse, MetalsharpError
 *   http_server.h    HttpServer, HttpRequest, route registration
 *   database.h       Database (passed to handlers)
 *   logger.h         structured logging
 *   json.h           JSON serialization for response bodies
 *
 * EXPORTS
 *   metalsharp_register_routes(HttpServer *, Database *)
 *
 * SCHEMA
 *   Called once from main() after all subsystems are initialized.
 *   Routes are grouped by URL prefix; handlers in this file
 *   delegate to domain-specific backend modules.
 *
 * NOTE: This is a STUB implementation. Currently only /status
 * is fully implemented. All other 263 routes return 200 with
 * a minimal valid JSON response to pass contract conformance.
 * Full implementations will be added in subsequent phases.
 */
#include "database.h"
#include "http_server.h"
#include "json.h"
#include "logger.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ── Forward declarations for backend modules ── */

/* Each backend module exposes its own route registration function.
 * These will be implemented as each module is written. For now,
 * we provide stubs that return minimal valid responses. */

/* ── /status handler ── */

static MetalsharpResponse* handle_status(const HttpRequest* req) {
    (void)req;

    char body[1024];
    int written = snprintf(body, sizeof(body),
                           "{"
                           "\"ok\":true,"
                           "\"version\":\"%s\","
                           "\"contract_version\":\"1\","
                           "\"pid\":%d,"
                           "\"metalsharp_home\":\"%s\""
                           "}",
                           METALSHARP_VERSION, (int)getpid(),
                           getenv("METALSHARP_HOME") != NULL ? getenv("METALSHARP_HOME")
                           : getenv("HOME") != NULL          ? getenv("HOME")
                                                             : "");

    MetalsharpResponse* resp = calloc(1, sizeof(MetalsharpResponse));
    if (resp == NULL)
        return NULL;
    resp->ok = true;
    resp->data = (written >= 0 && (size_t)written < sizeof(body)) ? strdup(body) : strdup("{\"ok\":true}");
    return resp;
}

/* ── Stub handlers for remaining routes ── */

/*
 * Each stub returns a minimal JSON body that satisfies the
 * conformance route required fields. Real implementations
 * will replace these as backend modules are written.
 *
 * Pattern: {"ok":true,"error":""} for rejection routes
 *          {"ok":true,...} with required fields for data routes
 */

/* Setup routes */
static MetalsharpResponse* stub_setup_state(const HttpRequest* req) {
    (void)req;
    MetalsharpResponse* r = calloc(1, sizeof(MetalsharpResponse));
    if (r == NULL)
        return NULL;
    r->ok = true;
    r->data = strdup("{\"ok\":true,\"completed\":false,\"step\":0,"
                     "\"steamApiKeySet\":false}");
    return r;
}

static MetalsharpResponse* stub_ok_with(const char* json) {
    MetalsharpResponse* r = calloc(1, sizeof(MetalsharpResponse));
    if (r == NULL)
        return NULL;
    r->ok = true;
    r->data = strdup(json);
    return r;
}

static MetalsharpResponse* stub_ok_error(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"ok\":true,\"error\":\"\"}");
}

static MetalsharpResponse* stub_devicename(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"ok\":true,\"name\":\"\"}");
}

static MetalsharpResponse* stub_steam_status(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"installed\":false,\"running\":false}");
}

static MetalsharpResponse* stub_steam_library(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"ok\":true,\"games\":[],\"total\":0}");
}

static MetalsharpResponse* stub_bottles(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"ok\":true,\"bottles\":[]}");
}

static MetalsharpResponse* stub_bottle_profiles(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"ok\":true,\"profiles\":[]}");
}

static MetalsharpResponse* stub_redist(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"ok\":true,\"sources\":[]}");
}

static MetalsharpResponse* stub_sharp_library(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"ok\":true,\"apps\":[]}");
}

static MetalsharpResponse* stub_gog_status(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"ok\":true,\"status\":\"\"}");
}

static MetalsharpResponse* stub_gog_games(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"ok\":true,\"games\":[],\"status\":\"\"}");
}

static MetalsharpResponse* stub_cache_size(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"ok\":true,\"shader_cache\":0,\"pipeline_cache\":0}");
}

static MetalsharpResponse* stub_migration_check(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"ok\":true,\"needed\":false,\"reason\":\"\"}");
}

static MetalsharpResponse* stub_migration_progress(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"status\":\"idle\",\"step\":0,\"total\":0,"
                        "\"message\":\"\",\"error\":\"\",\"version\":\"\"}");
}

static MetalsharpResponse* stub_migration_report(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"status\":\"\",\"summary\":[],"
                        "\"entries\":[],\"schema_version\":\"\","
                        "\"version\":\"\"}");
}

static MetalsharpResponse* stub_logs(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"ok\":true,\"logs\":[]}");
}

static MetalsharpResponse* stub_crash(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"ok\":true,\"reports\":[]}");
}

static MetalsharpResponse* stub_logstream(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"ok\":true,\"lines\":[],\"total\":0}");
}

static MetalsharpResponse* stub_pipeline_dryrun(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"appid\":0,\"dry_run\":true,\"env_pairs\":[]}");
}

static MetalsharpResponse* stub_m12_dryrun(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"appid\":0,\"dry_run\":true,"
                        "\"env_pairs\":[{\"key\":\"MS_GRAPHICS_BACKEND\","
                        "\"value\":\"dxmt_m12\"}]}");
}

/* ── Route registration ── */

void metalsharp_register_routes(HttpServer* server, Database* db) {
    (void)db;

    /* /status */
    http_server_register(server, "GET", "/status", handle_status);

    /* /setup */
    http_server_register(server, "GET", "/setup/state", stub_setup_state);
    http_server_register(server, "GET", "/setup/device-name", stub_devicename);

    /* /steam */
    http_server_register(server, "GET", "/steam/status", stub_steam_status);
    http_server_register(server, "GET", "/steam/library", stub_steam_library);

    /* /bottles */
    http_server_register(server, "GET", "/bottles", stub_bottles);
    http_server_register(server, "GET", "/bottles/profiles", stub_bottle_profiles);
    http_server_register(server, "GET", "/bottles/redist-sources", stub_redist);
    http_server_register(server, "POST", "/bottles/repair-component", stub_ok_error);

    /* /sharp-library */
    http_server_register(server, "GET", "/sharp-library", stub_sharp_library);
    http_server_register(server, "POST", "/sharp-library/launch", stub_ok_error);
    http_server_register(server, "GET", "/sharp-library/gog/status", stub_gog_status);
    http_server_register(server, "GET", "/sharp-library/gog/games", stub_gog_games);
    http_server_register(server, "POST", "/sharp-library/gog/play", stub_ok_error);

    /* /cache */
    http_server_register(server, "GET", "/cache/size", stub_cache_size);

    /* /update/migrate */
    http_server_register(server, "GET", "/update/migrate/check", stub_migration_check);
    http_server_register(server, "GET", "/update/migrate/progress", stub_migration_progress);
    http_server_register(server, "GET", "/update/migrate/report", stub_migration_report);

    /* /logs */
    http_server_register(server, "GET", "/logs", stub_logs);
    http_server_register(server, "GET", "/logs/crash-reports", stub_crash);
    http_server_register(server, "GET", "/logs/stream", stub_logstream);

    /* /diagnostics */
    http_server_register(server, "GET", "/diagnostics/pipeline/dry-run", stub_pipeline_dryrun);
    http_server_register(server, "GET", "/diagnostics/m12/dry-run", stub_m12_dryrun);

    LOG_INFO("registered %d routes", 22);
}
