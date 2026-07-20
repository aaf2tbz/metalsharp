/*
 * kt_handle_table.c — Metalsharp backend: virtual NT handle table
 *
 * WHAT  virtual NT handle table (stub implementation)
 * IMPORTS  server.h, http_server.h
 * EXPORTS  kt_handle_table_register_routes
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

void kt_handle_table_register_routes(HttpServer* s, Database* db) {
    (void)db;
    http_server_register(s, "POST", "/kernel-translation/handle/close", h_ok);
    http_server_register(s, "POST", "/kernel-translation/handle/create", h_ok);
    http_server_register(s, "POST", "/kernel-translation/handle/duplicate", h_ok);
    http_server_register(s, "POST", "/kernel-translation/handle/enumerate", h_ok);
    http_server_register(s, "POST", "/kernel-translation/handle/enumerate-fds", h_ok);
    http_server_register(s, "POST", "/kernel-translation/handle/enumerate-ports", h_ok);
    http_server_register(s, "POST", "/kernel-translation/handle/query", h_ok);
    http_server_register(s, "POST", "/kernel-translation/handle/seed-demo", h_ok);
    http_server_register(s, "POST", "/kernel-translation/handle/snapshot-all", h_ok);
    http_server_register(s, "POST", "/kernel-translation/handle/system-info", h_ok);
    http_server_register(s, "POST", "/kernel-translation/handle/table-status", h_ok);
    http_server_register(s, "POST", "/kernel-translation/handle/unified-snapshot", h_ok);
}
