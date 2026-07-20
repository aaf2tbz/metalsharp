/*
 * command_contract.c — Metalsharp backend: command contract replay
 *
 * WHAT  command contract replay (stub implementation)
 * IMPORTS  server.h, http_server.h
 * EXPORTS  command_contract_register_routes
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
    return sok("{\"ok\":true,\"schema_version\":1,\"encoder_boundaries\":[],\"violations\":[],"
               "\"visibility_summary\":{\"total_transitions\":0,\"render_passes\":0,"
               "\"split_barriers\":0,\"unfinished_split_barriers\":0,"
               "\"read_to_write_transitions\":0,\"write_to_read_transitions\":0}}");
}
__attribute__((unused)) __attribute__((unused)) static MetalsharpResponse* h_ok_error(const HttpRequest* req) {
    (void)req;
    return sok("{\"ok\":true,\"error\":\"\"}");
}

void command_contract_register_routes(HttpServer* s, Database* db) {
    (void)db;
    http_server_register(s, "POST", "/diagnostics/command-replay/validate", h_ok);
}
