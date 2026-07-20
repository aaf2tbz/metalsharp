/*
 * kt_thread_notify.c — Metalsharp backend: thread create/terminate notifications
 *
 * WHAT  thread create/terminate notifications (stub implementation)
 * IMPORTS  server.h, http_server.h
 * EXPORTS  kt_thread_notify_register_routes
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

void kt_thread_notify_register_routes(HttpServer* s, Database* db) {
    (void)db;
    http_server_register(s, "POST", "/kernel-translation/thread/compute-delta", h_ok);
    http_server_register(s, "POST", "/kernel-translation/thread/configure-notifications", h_ok);
    http_server_register(s, "POST", "/kernel-translation/thread/create-watcher", h_ok);
    http_server_register(s, "POST", "/kernel-translation/thread/destroy-watcher", h_ok);
    http_server_register(s, "POST", "/kernel-translation/thread/info", h_ok);
    http_server_register(s, "POST", "/kernel-translation/thread/list-deltas", h_ok);
    http_server_register(s, "POST", "/kernel-translation/thread/list-watchers", h_ok);
    http_server_register(s, "POST", "/kernel-translation/thread/mechanism-survey", h_ok);
    http_server_register(s, "POST", "/kernel-translation/thread/poll-watcher", h_ok);
    http_server_register(s, "POST", "/kernel-translation/thread/seed-demo", h_ok);
    http_server_register(s, "POST", "/kernel-translation/thread/snapshot", h_ok);
    http_server_register(s, "POST", "/kernel-translation/thread/watcher-status", h_ok);
}
