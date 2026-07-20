/*
 * d3dmetal_gptk.c — Metalsharp backend: D3DMetal/GPTK bottle management
 *
 * WHAT  D3DMetal/GPTK bottle management (stub implementation)
 * IMPORTS  server.h, http_server.h
 * EXPORTS  d3dmetal_gptk_register_routes
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
    if (req != NULL && strcmp(req->path, "/d3dmetal/bottles/save") == 0)
        return sok("{\"ok\":false,\"error\":\"appid required\"}");
    return sok("{\"ok\":false,\"error\":\"appid or bottleId required\"}");
}
__attribute__((unused)) __attribute__((unused)) static MetalsharpResponse* h_ok_error(const HttpRequest* req) {
    (void)req;
    return sok("{\"ok\":true,\"error\":\"\"}");
}

void d3dmetal_gptk_register_routes(HttpServer* s, Database* db) {
    (void)db;
    http_server_register(s, "POST", "/d3dmetal/bottles/install-homebrew-gptk", h_ok);
    http_server_register(s, "POST", "/d3dmetal/bottles/install-rosetta", h_ok);
    http_server_register(s, "POST", "/d3dmetal/bottles/install-x64-redist", h_ok);
    http_server_register(s, "POST", "/d3dmetal/bottles/play", h_ok);
    http_server_register(s, "POST", "/d3dmetal/bottles/repair-gptk-payload", h_ok);
    http_server_register(s, "POST", "/d3dmetal/bottles/save", h_ok);
    http_server_register(s, "POST", "/d3dmetal/bottles/seed-prefix", h_ok);
    http_server_register(s, "POST", "/d3dmetal/bottles/status", h_ok);
}
