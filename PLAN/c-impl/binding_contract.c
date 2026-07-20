/*
 * binding_contract.c — Metalsharp backend: binding contract validation
 *
 * WHAT  binding contract validation (stub implementation)
 * IMPORTS  server.h, http_server.h
 * EXPORTS  binding_contract_register_routes
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

void binding_contract_register_routes(HttpServer* s, Database* db) {
    (void)db;
    http_server_register(s, "POST", "/diagnostics/binding-contract/validate", h_ok);
}
