/*
 * kt_anti_debug.c — Metalsharp backend: anti-debugging checks
 *
 * WHAT  anti-debugging checks (stub implementation)
 * IMPORTS  server.h, http_server.h
 * EXPORTS  kt_anti_debug_register_routes
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

void kt_anti_debug_register_routes(HttpServer* s, Database* db) {
    (void)db;
    http_server_register(s, "POST", "/kernel-translation/anti-debug/add-sanitize-rule", h_ok);
    http_server_register(s, "POST", "/kernel-translation/anti-debug/check-results", h_ok);
    http_server_register(s, "POST", "/kernel-translation/anti-debug/filesystem-check", h_ok);
    http_server_register(s, "POST", "/kernel-translation/anti-debug/full-breakpoint-map", h_ok);
    http_server_register(s, "POST", "/kernel-translation/anti-debug/hw-breakpoint-map", h_ok);
    http_server_register(s, "POST", "/kernel-translation/anti-debug/module-sanitize", h_ok);
    http_server_register(s, "POST", "/kernel-translation/anti-debug/run-all-checks", h_ok);
    http_server_register(s, "POST", "/kernel-translation/anti-debug/seed-demo", h_ok);
    http_server_register(s, "POST", "/kernel-translation/anti-debug/simulate-check", h_ok);
    http_server_register(s, "POST", "/kernel-translation/anti-debug/status-survey", h_ok);
    http_server_register(s, "POST", "/kernel-translation/anti-debug/timing-analysis", h_ok);
}
