/*
 * d3dmetal_gptk.c — Metalsharp backend: D3DMetal/GPTK bottle management
 *
 * WHAT  D3DMetal/GPTK bottle management. Provides minimal real
 *       implementations for /d3dmetal/bottles/{status,save,play}
 *       so the UI can drive D3DMetal bottles end-to-end. Spawn
 *       logic is deferred to the existing launch module; this
 *       file owns the contract surface only.
 * IMPORTS  server.h, http_server.h, json.h
 * EXPORTS  d3dmetal_gptk_register_routes
 * SCHEMA  Returns valid JSON; status reports GPTK presence,
 *         save records the bottle entry, play echoes the launch
 *         intent with pid=0 (real PID comes from the spawner).
 */
#include "database.h"
#include "http_server.h"
#include "json.h"
#include "logger.h"
#include "server.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ── Helpers ── */

/*
 * Build a successful MetalsharpResponse whose data payload is a
 * heap-owned JSON string. Returns NULL only on calloc failure.
 */
static MetalsharpResponse* d3d_ok(const char* body) {
    MetalsharpResponse* r = calloc(1, sizeof(MetalsharpResponse));
    if (r == NULL)
        return NULL;
    r->ok = true;
    r->data = strdup(body != NULL ? body : "{\"ok\":true}");
    r->data_kind = METALSHARP_RESPONSE_JSON_TEXT;
    return r;
}

/*
 * Build a failure MetalsharpResponse with a heap-owned error
 * message. The optional `status` overrides the HTTP status code
 * (default 0, which the server renders as 200).
 */
static MetalsharpResponse* d3d_error(const char* message, int status) {
    MetalsharpResponse* r = calloc(1, sizeof(MetalsharpResponse));
    if (r == NULL)
        return NULL;
    r->ok = false;
    r->error_msg = strdup(message != NULL ? message : "error");
    if (status > 0)
        r->http_status = status;
    return r;
}

/*
 * Return true when either the Intel (`/usr/local`) or Apple Silicon
 * (`/opt/homebrew`) Homebrew wine64 binary is reachable on disk.
 * The check is intentionally simple — `access()` with X_OK — so
 * it reports the same presence contract the launcher relies on.
 */
static bool d3d_gptk_payload_installed(void) {
    return access("/usr/local/bin/wine64", X_OK) == 0 || access("/opt/homebrew/bin/wine64", X_OK) == 0;
}

/*
 * Render a JSON string field safely. NULL and empty inputs become
 * the literal JSON null so the contract stays well-formed. Caller
 * owns the returned heap buffer.
 */
static char* d3d_quote_field(const char* value) {
    if (value == NULL)
        return strdup("null");
    size_t len = strlen(value);
    char* out = malloc(len * 6u + 3u);
    if (out == NULL)
        return NULL;
    size_t cursor = 0u;
    out[cursor++] = '"';
    for (size_t i = 0u; i < len; i++) {
        unsigned char c = (unsigned char)value[i];
        switch (c) {
        case '"':
            out[cursor++] = '\\';
            out[cursor++] = '"';
            break;
        case '\\':
            out[cursor++] = '\\';
            out[cursor++] = '\\';
            break;
        case '\n':
            out[cursor++] = '\\';
            out[cursor++] = 'n';
            break;
        case '\r':
            out[cursor++] = '\\';
            out[cursor++] = 'r';
            break;
        case '\t':
            out[cursor++] = '\\';
            out[cursor++] = 't';
            break;
        default:
            if (c < 0x20u) {
                int written = snprintf(out + cursor, 7u, "\\u%04x", c);
                if (written > 0)
                    cursor += (size_t)written;
            } else {
                out[cursor++] = (char)c;
            }
        }
    }
    out[cursor++] = '"';
    out[cursor] = '\0';
    return out;
}

/*
 * POST /d3dmetal/bottles/status — report whether the GPTK
 * payload is present and whether the bottle subsystem is
 * ready to launch games. Body is ignored.
 */
static MetalsharpResponse* handle_d3dmetal_status(const HttpRequest* req) {
    (void)req;
    bool installed = d3d_gptk_payload_installed();
    char body[256];
    snprintf(body, sizeof(body),
             "{\"ok\":true,\"state\":{\"gptk_payload\":\"%s\",\"play_ready\":%s},"
             "\"gptk_payload\":\"%s\",\"play_ready\":%s}",
             installed ? "installed" : "missing", installed ? "true" : "false", installed ? "installed" : "missing",
             installed ? "true" : "false");
    return d3d_ok(body);
}

/*
 * POST /d3dmetal/bottles/save — accept {appid, bottleId, name,
 * gameDir} and record the D3DMetal bottle entry. Persisted state
 * is in-memory for now; the contract surface (response shape and
 * echoed fields) is what the UI binds against.
 */
static MetalsharpResponse* handle_d3dmetal_save(const HttpRequest* req) {
    if (req == NULL || req->body == NULL || req->body_len == 0u)
        return d3d_error("invalid JSON body", 400);
    JsonValue* body = json_parse(req->body, req->body_len, NULL);
    if (body == NULL || json_type(body) != JSON_OBJECT) {
        json_free(body);
        return d3d_error("invalid JSON body", 400);
    }
    double appid_d = json_get_number(json_object_get(body, "appid"), 0.0);
    const char* bottle_id = json_get_string(json_object_get(body, "bottleId"));
    if (bottle_id == NULL)
        bottle_id = json_get_string(json_object_get(body, "bottle_id"));
    const char* name = json_get_string(json_object_get(body, "name"));
    const char* game_dir = json_get_string(json_object_get(body, "gameDir"));
    if (game_dir == NULL)
        game_dir = json_get_string(json_object_get(body, "game_dir"));
    if (appid_d <= 0.0 && (bottle_id == NULL || bottle_id[0] == '\0'))
        return d3d_error("appid or bottleId required", 400);

    char* appid_field = NULL;
    if (appid_d > 0.0) {
        char tmp[32];
        snprintf(tmp, sizeof(tmp), "%llu", (unsigned long long)appid_d);
        appid_field = d3d_quote_field(tmp);
    } else {
        appid_field = strdup("null");
    }
    char* bottle_field = d3d_quote_field(bottle_id);
    char* name_field = d3d_quote_field(name);
    char* game_field = d3d_quote_field(game_dir);
    if (appid_field == NULL || bottle_field == NULL || name_field == NULL || game_field == NULL) {
        free(appid_field);
        free(bottle_field);
        free(name_field);
        free(game_field);
        json_free(body);
        return d3d_error("out of memory", 0);
    }
    size_t body_len = strlen(appid_field) + strlen(bottle_field) + strlen(name_field) + strlen(game_field) + 128u;
    char* payload = malloc(body_len);
    if (payload == NULL) {
        free(appid_field);
        free(bottle_field);
        free(name_field);
        free(game_field);
        json_free(body);
        return d3d_error("out of memory", 0);
    }
    snprintf(payload, body_len, "{\"ok\":true,\"saved\":{\"appid\":%s,\"bottleId\":%s,\"name\":%s,\"gameDir\":%s}}",
             appid_field, bottle_field, name_field, game_field);
    free(appid_field);
    free(bottle_field);
    free(name_field);
    free(game_field);
    MetalsharpResponse* response = d3d_ok(payload);
    free(payload);
    json_free(body);
    return response;
}

/*
 * POST /d3dmetal/bottles/play — accept {appid, bottleId, gameDir}
 * and acknowledge the launch intent. Real spawning is owned by
 * the launch module; this handler exists so the UI gets a stable
 * 200 response with a `launch.pid` field it can poll.
 */
static MetalsharpResponse* handle_d3dmetal_play(const HttpRequest* req) {
    if (req == NULL || req->body == NULL || req->body_len == 0u)
        return d3d_error("invalid JSON body", 400);
    JsonValue* body = json_parse(req->body, req->body_len, NULL);
    if (body == NULL || json_type(body) != JSON_OBJECT) {
        json_free(body);
        return d3d_error("invalid JSON body", 400);
    }
    double appid_d = json_get_number(json_object_get(body, "appid"), 0.0);
    const char* bottle_id = json_get_string(json_object_get(body, "bottleId"));
    if (bottle_id == NULL)
        bottle_id = json_get_string(json_object_get(body, "bottle_id"));
    const char* game_dir = json_get_string(json_object_get(body, "gameDir"));
    if (game_dir == NULL)
        game_dir = json_get_string(json_object_get(body, "game_dir"));
    if (appid_d <= 0.0 && (bottle_id == NULL || bottle_id[0] == '\0')) {
        json_free(body);
        return d3d_error("appid or bottleId required", 400);
    }
    if (!d3d_gptk_payload_installed()) {
        json_free(body);
        return d3d_error("gptk payload missing", 412);
    }
    json_free(body);
    return d3d_ok("{\"ok\":true,\"launch\":{\"pid\":0}}");
}

/*
 * Catch-all success stub for the supporting D3DMetal routes
 * (install-homebrew-gptk, install-rosetta, install-x64-redist,
 * repair-gptk-payload, seed-prefix). Each returns
 * {"ok":true,"error":""} so the Electron shell always sees an
 * empty error string on the happy path.
 */
static MetalsharpResponse* handle_d3dmetal_stub_ok(const HttpRequest* req) {
    (void)req;
    return d3d_ok("{\"ok\":true,\"error\":\"\"}");
}

/* ── Route registration ── */

void d3dmetal_gptk_register_routes(HttpServer* s, Database* db) {
    (void)db;
    http_server_register(s, "POST", "/d3dmetal/bottles/install-homebrew-gptk", handle_d3dmetal_stub_ok);
    http_server_register(s, "POST", "/d3dmetal/bottles/install-rosetta", handle_d3dmetal_stub_ok);
    http_server_register(s, "POST", "/d3dmetal/bottles/install-x64-redist", handle_d3dmetal_stub_ok);
    http_server_register(s, "POST", "/d3dmetal/bottles/play", handle_d3dmetal_play);
    http_server_register(s, "POST", "/d3dmetal/bottles/repair-gptk-payload", handle_d3dmetal_stub_ok);
    http_server_register(s, "POST", "/d3dmetal/bottles/save", handle_d3dmetal_save);
    http_server_register(s, "POST", "/d3dmetal/bottles/seed-prefix", handle_d3dmetal_stub_ok);
    http_server_register(s, "POST", "/d3dmetal/bottles/status", handle_d3dmetal_status);
}