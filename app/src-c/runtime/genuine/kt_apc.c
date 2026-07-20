/*
 * kt_apc.c — Metalsharp backend: APC injection and trampoline
 *
 * WHAT  APC injection and trampoline (stub implementation)
 * IMPORTS  server.h, http_server.h
 * EXPORTS  kt_apc_register_routes
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

void kt_apc_register_routes(HttpServer* s, Database* db) {
    (void)db;
    http_server_register(s, "POST", "/kernel-translation/apc/allocate-trampoline", h_ok);
    http_server_register(s, "POST", "/kernel-translation/apc/get-thread-context", h_ok);
    http_server_register(s, "POST", "/kernel-translation/apc/inject-sequence", h_ok);
    http_server_register(s, "POST", "/kernel-translation/apc/queue", h_ok);
    http_server_register(s, "POST", "/kernel-translation/apc/queue-status", h_ok);
    http_server_register(s, "POST", "/kernel-translation/apc/set-thread-context", h_ok);
    http_server_register(s, "POST", "/kernel-translation/apc/suspend-thread", h_ok);
    http_server_register(s, "POST", "/kernel-translation/apc/test-alert", h_ok);
    http_server_register(s, "POST", "/kernel-translation/apc/trampoline-status", h_ok);
    http_server_register(s, "POST", "/kernel-translation/apc/wait-alertable", h_ok);
}
