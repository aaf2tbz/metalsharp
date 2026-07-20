/*
 * kt_integration.c — Metalsharp backend: bottle kernel configuration
 *
 * WHAT  bottle kernel configuration (stub implementation)
 * IMPORTS  server.h, http_server.h
 * EXPORTS  kt_integration_register_routes
 * SCHEMA  Routes return valid JSON stubs. Real implementations deferred.
 */
#include "database.h"
#include "http_server.h"
#include "server.h"
#include <stdlib.h>
#include <string.h>

static MetalsharpResponse* sok(const char* j) {
    MetalsharpResponse* r = calloc(1, sizeof(MetalsharpResponse));
    if (r) {
        r->ok = true;
        r->data = j ? strdup(j) : strdup("{\"ok\":true}");
    }
    return r;
}
__attribute__((unused)) static MetalsharpResponse* h_ok(const HttpRequest* req) {
    (void)req;
    return sok("{\"ok\":true}");
}
__attribute__((unused)) __attribute__((unused)) static MetalsharpResponse* h_ok_error(const HttpRequest* req) {
    (void)req;
    return sok("{\"ok\":true,\"error\":\"\"}");
}

void kt_integration_register_routes(HttpServer* s, Database* db) {
    (void)db;
    http_server_register(s, "POST", "/kernel-translation/integration/bottle-configure", h_ok);
    http_server_register(s, "POST", "/kernel-translation/integration/bottle-get-config", h_ok);
    http_server_register(s, "POST", "/kernel-translation/integration/bottle-list-configs", h_ok);
    http_server_register(s, "POST", "/kernel-translation/integration/extension-activate", h_ok);
    http_server_register(s, "POST", "/kernel-translation/integration/extension-crash", h_ok);
    http_server_register(s, "POST", "/kernel-translation/integration/extension-deactivate", h_ok);
    http_server_register(s, "POST", "/kernel-translation/integration/extension-install", h_ok);
    http_server_register(s, "POST", "/kernel-translation/integration/extension-status", h_ok);
    http_server_register(s, "POST", "/kernel-translation/integration/fallback-mode", h_ok);
    http_server_register(s, "POST", "/kernel-translation/integration/full-stack-status", h_ok);
    http_server_register(s, "POST", "/kernel-translation/integration/list-multi-ac", h_ok);
    http_server_register(s, "POST", "/kernel-translation/integration/list-performance", h_ok);
    http_server_register(s, "POST", "/kernel-translation/integration/log-translation", h_ok);
    http_server_register(s, "POST", "/kernel-translation/integration/performance-profile", h_ok);
    http_server_register(s, "POST", "/kernel-translation/integration/query-translation-log", h_ok);
    http_server_register(s, "POST", "/kernel-translation/integration/register-multi-ac", h_ok);
    http_server_register(s, "POST", "/kernel-translation/integration/runtime-doctor", h_ok);
    http_server_register(s, "POST", "/kernel-translation/integration/seed-demo", h_ok);
    http_server_register(s, "POST", "/kernel-translation/integration/simulate-conflict", h_ok);
    http_server_register(s, "POST", "/kernel-translation/integration/simulate-pipeline", h_ok);
}
