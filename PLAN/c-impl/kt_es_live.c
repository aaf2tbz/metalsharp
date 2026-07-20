/*
 * kt_es_live.c — Kernel Translation: live ES monitoring
 *
 * WHAT
 *   Starts, stops, and queries a long-running Endpoint Security
 *   monitor that streams image/process/thread events into an
 *   in-memory ring buffer. The buffer is consumed by the GUI via
 *   /es-live/events and /es-live/processes.
 *
 * IMPORTS
 *   "server.h"        MetalsharpResponse, METALSHARP_VERSION
 *   "http_server.h"   HttpServer, HttpRequest, http_server_register
 *   "json.h"          json_parse, JsonValue for response payloads
 *   "database.h"      Database (captured for future persistence)
 *
 * EXPORTS
 *   kt_es_live_register_routes(HttpServer *server, Database *db)
 *       Register every /kernel-translation/es-live/ route.
 *
 * SCHEMA
 *   Every route returns a JsonValue *data payload matching the
 *   documented envelope. Stub bodies report "running":false with
 *   empty event/process lists until the live monitor is wired in.
 */
#include "database.h"
#include "http_server.h"
#include "json.h"
#include "server.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static Database* g_kt_es_live_db = NULL;

/* Forward declarations. */
static MetalsharpResponse* handle_es_live_start(const HttpRequest* req);
static MetalsharpResponse* handle_es_live_status(const HttpRequest* req);
static MetalsharpResponse* handle_es_live_stop(const HttpRequest* req);
static MetalsharpResponse* handle_es_live_events(const HttpRequest* req);
static MetalsharpResponse* handle_es_live_processes(const HttpRequest* req);

/* ── Response builder ── */

static MetalsharpResponse* make_ok_response(const char* body) {
    if (body == NULL || body[0] == '\0') {
        body = "{\"ok\":true}";
    }
    MetalsharpResponse* r = calloc(1, sizeof(MetalsharpResponse));
    if (r == NULL) {
        return NULL;
    }
    char* err = NULL;
    JsonValue* val = json_parse(body, strlen(body), &err);
    if (val == NULL) {
        free(err);
        r->ok = false;
        r->error_msg = strdup("internal error");
        return r;
    }
    r->ok = true;
    r->data = val;
    return r;
}

/* ── Route handlers ── */

static MetalsharpResponse* handle_es_live_start(const HttpRequest* req) {
    (void)req;
    return make_ok_response("{\"ok\":true,\"started\":false,"
                            "\"monitorId\":\"\"}");
}

static MetalsharpResponse* handle_es_live_status(const HttpRequest* req) {
    (void)req;
    return make_ok_response("{\"ok\":true,\"running\":false,"
                            "\"eventCount\":0,\"sinceMs\":0}");
}

static MetalsharpResponse* handle_es_live_stop(const HttpRequest* req) {
    (void)req;
    return make_ok_response("{\"ok\":true,\"stopped\":false}");
}

static MetalsharpResponse* handle_es_live_events(const HttpRequest* req) {
    (void)req;
    return make_ok_response("{\"ok\":true,\"events\":[]}");
}

static MetalsharpResponse* handle_es_live_processes(const HttpRequest* req) {
    (void)req;
    return make_ok_response("{\"ok\":true,\"processes\":[]}");
}

/* ── Route registration ── */

void kt_es_live_register_routes(HttpServer* server, Database* db) {
    if (server == NULL || db == NULL) {
        return;
    }
    g_kt_es_live_db = db;
    (void)g_kt_es_live_db;

    http_server_register(server, "POST", "/kernel-translation/es-live/start", handle_es_live_start);
    http_server_register(server, "GET", "/kernel-translation/es-live/status", handle_es_live_status);
    http_server_register(server, "POST", "/kernel-translation/es-live/stop", handle_es_live_stop);
    http_server_register(server, "GET", "/kernel-translation/es-live/events", handle_es_live_events);
    http_server_register(server, "GET", "/kernel-translation/es-live/processes", handle_es_live_processes);
}
