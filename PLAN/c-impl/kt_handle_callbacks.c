/*
 * kt_handle_callbacks.c — Metalsharp backend: ObRegisterCallbacks emulation
 *
 * WHAT  ObRegisterCallbacks emulation (stub implementation)
 * IMPORTS  server.h, http_server.h
 * EXPORTS  kt_handle_callbacks_register_routes
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

void kt_handle_callbacks_register_routes(HttpServer* s, Database* db) {
    (void)db;
    http_server_register(s, "POST", "/kernel-translation/ob/access-log", h_ok);
    http_server_register(s, "POST", "/kernel-translation/ob/capability-survey", h_ok);
    http_server_register(s, "POST", "/kernel-translation/ob/list-protected", h_ok);
    http_server_register(s, "POST", "/kernel-translation/ob/list-registrations", h_ok);
    http_server_register(s, "POST", "/kernel-translation/ob/post-operations", h_ok);
    http_server_register(s, "POST", "/kernel-translation/ob/pre-operations", h_ok);
    http_server_register(s, "POST", "/kernel-translation/ob/protect-process", h_ok);
    http_server_register(s, "POST", "/kernel-translation/ob/register-callback", h_ok);
    http_server_register(s, "POST", "/kernel-translation/ob/seed-demo", h_ok);
    http_server_register(s, "POST", "/kernel-translation/ob/simulate-operation", h_ok);
    http_server_register(s, "POST", "/kernel-translation/ob/unprotect-process", h_ok);
    http_server_register(s, "POST", "/kernel-translation/ob/unregister-callback", h_ok);
}
