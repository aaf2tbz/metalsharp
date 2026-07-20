/*
 * scan.c — Metalsharp backend: library scanning
 *
 * WHAT  library scanning (stub implementation)
 * IMPORTS  server.h, http_server.h
 * EXPORTS  scan_register_routes
 * SCHEMA  Routes return valid JSON stubs. Real implementations deferred.
 */
#include "compat_log.h"
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
    metalsharp_app_log("Scanning for installed games...");
    return sok("{\"ok\":true,\"data\":{\"games\":[],\"steam\":{"
               "\"installed\":false,\"running\":false,\"installing\":false,\"path\":null,"
               "\"metalsharp_wine_available\":false,\"mac_installed\":false,\"mac_running\":false,"
               "\"mac_path\":null,\"mac_install_url\":\"https://store.steampowered.com/about/\","
               "\"login_state\":{\"state\":\"unknown\",\"account\":null}}}}");
}
__attribute__((unused)) __attribute__((unused)) static MetalsharpResponse* h_ok_error(const HttpRequest* req) {
    (void)req;
    return sok("{\"ok\":true,\"error\":\"\"}");
}

void scan_register_routes(HttpServer* s, Database* db) {
    (void)db;
    http_server_register(s, "GET", "/scan", h_ok);
}
