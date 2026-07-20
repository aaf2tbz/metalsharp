/*
 * kt_code_integrity.c — Metalsharp backend: code signing and module integrity
 *
 * WHAT  code signing and module integrity (stub implementation)
 * IMPORTS  server.h, http_server.h
 * EXPORTS  kt_code_integrity_register_routes
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

void kt_code_integrity_register_routes(HttpServer* s, Database* db) {
    (void)db;
    http_server_register(s, "POST", "/kernel-translation/integrity/list-modules", h_ok);
    http_server_register(s, "POST", "/kernel-translation/integrity/query-process-signing", h_ok);
    http_server_register(s, "POST", "/kernel-translation/integrity/query-signing-level", h_ok);
    http_server_register(s, "POST", "/kernel-translation/integrity/register-macho-module", h_ok);
    http_server_register(s, "POST", "/kernel-translation/integrity/register-pe-module", h_ok);
    http_server_register(s, "POST", "/kernel-translation/integrity/seed-demo", h_ok);
    http_server_register(s, "POST", "/kernel-translation/integrity/set-cached-signing-level", h_ok);
}
