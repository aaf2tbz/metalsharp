/*
 * kt_driver_model.c — Metalsharp backend: NT driver model emulation
 *
 * WHAT  NT driver model emulation (stub implementation)
 * IMPORTS  server.h, http_server.h
 * EXPORTS  kt_driver_model_register_routes
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

void kt_driver_model_register_routes(HttpServer* s, Database* db) {
    (void)db;
    http_server_register(s, "POST", "/kernel-translation/driver/create-device", h_ok);
    http_server_register(s, "POST", "/kernel-translation/driver/decode-ioctl", h_ok);
    http_server_register(s, "POST", "/kernel-translation/driver/dispatch-irp", h_ok);
    http_server_register(s, "POST", "/kernel-translation/driver/extension-template", h_ok);
    http_server_register(s, "POST", "/kernel-translation/driver/list", h_ok);
    http_server_register(s, "POST", "/kernel-translation/driver/list-devices", h_ok);
    http_server_register(s, "POST", "/kernel-translation/driver/list-ioctls", h_ok);
    http_server_register(s, "POST", "/kernel-translation/driver/list-irps", h_ok);
    http_server_register(s, "POST", "/kernel-translation/driver/load", h_ok);
    http_server_register(s, "POST", "/kernel-translation/driver/register-ioctl", h_ok);
    http_server_register(s, "POST", "/kernel-translation/driver/seed-demo", h_ok);
    http_server_register(s, "POST", "/kernel-translation/driver/type-mapping-survey", h_ok);
    http_server_register(s, "POST", "/kernel-translation/driver/unload", h_ok);
}
