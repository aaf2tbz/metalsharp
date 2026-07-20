/*
 * kt_probe.c — Metalsharp backend: host system probe
 *
 * WHAT  host system probe (stub implementation)
 * IMPORTS  server.h, http_server.h
 * EXPORTS  kt_probe_register_routes
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
    return sok("{\"magic\":\"MSAB\",\"managed_runtime_env\":[\"METALSHARP_MONO_LIB\","
               "\"METALSHARP_MONO_ROOT\",\"METALSHARP_MONO_ASSEMBLY_DIR\","
               "\"METALSHARP_MONO_CONFIG_DIR\"],\"ok\":true,\"services\":[\"process\","
               "\"paths\",\"logging\",\"steam\",\"graphics\",\"audio\",\"input\","
               "\"managed_runtime\"],\"steam_bridge\":{\"active_port\":18733,"
               "\"default_port\":18733,\"env\":\"METALSHARP_STEAM_BRIDGE_PORT\"},"
               "\"version\":{\"major\":1,\"minor\":0}}");
}
__attribute__((unused)) __attribute__((unused)) static MetalsharpResponse* h_ok_error(const HttpRequest* req) {
    (void)req;
    return sok("{\"ok\":true,\"error\":\"\"}");
}

void kt_probe_register_routes(HttpServer* s, Database* db) {
    (void)db;
    http_server_register(s, "POST", "/kernel-translation/probe", h_ok);
    http_server_register(s, "POST", "/kernel-translation/host-probe", h_ok);
    http_server_register(s, "GET", "/runtime/host-abi", h_ok);
}
