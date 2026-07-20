/*
 * kt_es_bridge.c — Kernel Translation: Endpoint Security bridge
 *
 * WHAT
 *   Bridges the macOS Endpoint Security (ES) framework to the KT
 *   subsystem so Wine processes can subscribe to kernel-level
 *   image, process, and thread events. Each route manipulates the
 *   in-memory ES subscription state; a later phase wires real
 *   es_client_t handles and event dispatch.
 *
 * IMPORTS
 *   "server.h"        MetalsharpResponse, METALSHARP_VERSION
 *   "http_server.h"   HttpServer, HttpRequest, http_server_register
 *   "json.h"          json_parse, JsonValue for response payloads
 *   "database.h"      Database (captured for future persistence)
 *
 * EXPORTS
 *   kt_es_bridge_register_routes(HttpServer *server, Database *db)
 *       Register every /kernel-translation/es/ route on `server`
 *       and capture `db` for future event-log persistence.
 *
 * SCHEMA
 *   Every route returns MetalsharpResponse with ok=true and a
 *   JsonValue *data payload matching the documented schema. Stub
 *   bodies are minimal {"ok":true} envelopes; real payloads land
 *   in the phase that wires es_client_t.
 */
#include "database.h"
#include "http_server.h"
#include "json.h"
#include "server.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Database* g_kt_es_bridge_db = NULL;

/* Forward declarations for every route handler. */
static MetalsharpResponse* handle_es_status(const HttpRequest* req);
static MetalsharpResponse* handle_es_create_ipc_channel(const HttpRequest* req);
static MetalsharpResponse* handle_es_detect_events(const HttpRequest* req);
static MetalsharpResponse* handle_es_fire_image_event(const HttpRequest* req);
static MetalsharpResponse* handle_es_fire_process_event(const HttpRequest* req);
static MetalsharpResponse* handle_es_fire_thread_event(const HttpRequest* req);
static MetalsharpResponse* handle_es_image_events(const HttpRequest* req);
static MetalsharpResponse* handle_es_ipc_channels(const HttpRequest* req);
static MetalsharpResponse* handle_es_list_callbacks(const HttpRequest* req);
static MetalsharpResponse* handle_es_nt_callback_bridge(const HttpRequest* req);
static MetalsharpResponse* handle_es_process_events(const HttpRequest* req);
static MetalsharpResponse* handle_es_register_callback(const HttpRequest* req);
static MetalsharpResponse* handle_es_seed_demo(const HttpRequest* req);
static MetalsharpResponse* handle_es_thread_events(const HttpRequest* req);
static MetalsharpResponse* handle_es_unregister_callback(const HttpRequest* req);

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

static MetalsharpResponse* handle_es_status(const HttpRequest* req) {
    (void)req;
    return make_ok_response("{\"ok\":true,\"status\":\"idle\","
                            "\"subscriptions\":0,\"channels\":0}");
}

static MetalsharpResponse* handle_es_create_ipc_channel(const HttpRequest* req) {
    (void)req;
    return make_ok_response("{\"ok\":true,\"channelId\":\"\","
                            "\"created\":false}");
}

static MetalsharpResponse* handle_es_detect_events(const HttpRequest* req) {
    (void)req;
    return make_ok_response("{\"ok\":true,\"events\":[]}");
}

static MetalsharpResponse* handle_es_fire_image_event(const HttpRequest* req) {
    (void)req;
    return make_ok_response("{\"ok\":true,\"fired\":false}");
}

static MetalsharpResponse* handle_es_fire_process_event(const HttpRequest* req) {
    (void)req;
    return make_ok_response("{\"ok\":true,\"fired\":false}");
}

static MetalsharpResponse* handle_es_fire_thread_event(const HttpRequest* req) {
    (void)req;
    return make_ok_response("{\"ok\":true,\"fired\":false}");
}

static MetalsharpResponse* handle_es_image_events(const HttpRequest* req) {
    (void)req;
    return make_ok_response("{\"ok\":true,\"events\":[]}");
}

static MetalsharpResponse* handle_es_ipc_channels(const HttpRequest* req) {
    (void)req;
    return make_ok_response("{\"ok\":true,\"channels\":[]}");
}

static MetalsharpResponse* handle_es_list_callbacks(const HttpRequest* req) {
    (void)req;
    return make_ok_response("{\"ok\":true,\"callbacks\":[]}");
}

static MetalsharpResponse* handle_es_nt_callback_bridge(const HttpRequest* req) {
    (void)req;
    return make_ok_response("{\"ok\":true,\"bridged\":false}");
}

static MetalsharpResponse* handle_es_process_events(const HttpRequest* req) {
    (void)req;
    return make_ok_response("{\"ok\":true,\"events\":[]}");
}

static MetalsharpResponse* handle_es_register_callback(const HttpRequest* req) {
    (void)req;
    return make_ok_response("{\"ok\":true,\"callbackId\":\"\","
                            "\"registered\":false}");
}

static MetalsharpResponse* handle_es_seed_demo(const HttpRequest* req) {
    (void)req;
    return make_ok_response("{\"ok\":true,\"seeded\":false}");
}

static MetalsharpResponse* handle_es_thread_events(const HttpRequest* req) {
    (void)req;
    return make_ok_response("{\"ok\":true,\"events\":[]}");
}

static MetalsharpResponse* handle_es_unregister_callback(const HttpRequest* req) {
    (void)req;
    return make_ok_response("{\"ok\":true,\"unregistered\":false}");
}

/* ── Route registration ── */

void kt_es_bridge_register_routes(HttpServer* server, Database* db) {
    if (server == NULL || db == NULL) {
        return;
    }
    g_kt_es_bridge_db = db;
    (void)g_kt_es_bridge_db;

    http_server_register(server, "POST", "/kernel-translation/es/status", handle_es_status);
    http_server_register(server, "POST", "/kernel-translation/es/create-ipc-channel", handle_es_create_ipc_channel);
    http_server_register(server, "POST", "/kernel-translation/es/detect-events", handle_es_detect_events);
    http_server_register(server, "POST", "/kernel-translation/es/fire-image-event", handle_es_fire_image_event);
    http_server_register(server, "POST", "/kernel-translation/es/fire-process-event", handle_es_fire_process_event);
    http_server_register(server, "POST", "/kernel-translation/es/fire-thread-event", handle_es_fire_thread_event);
    http_server_register(server, "POST", "/kernel-translation/es/image-events", handle_es_image_events);
    http_server_register(server, "POST", "/kernel-translation/es/ipc-channels", handle_es_ipc_channels);
    http_server_register(server, "POST", "/kernel-translation/es/list-callbacks", handle_es_list_callbacks);
    http_server_register(server, "POST", "/kernel-translation/es/nt-callback-bridge", handle_es_nt_callback_bridge);
    http_server_register(server, "POST", "/kernel-translation/es/process-events", handle_es_process_events);
    http_server_register(server, "POST", "/kernel-translation/es/register-callback", handle_es_register_callback);
    http_server_register(server, "POST", "/kernel-translation/es/seed-demo", handle_es_seed_demo);
    http_server_register(server, "POST", "/kernel-translation/es/thread-events", handle_es_thread_events);
    http_server_register(server, "POST", "/kernel-translation/es/unregister-callback", handle_es_unregister_callback);
}
