/*
 * launch.c — Game launch orchestration for the Metalsharp C backend
 *
 * WHAT
 *   Implements the game-launch HTTP routes documented in
 *   contracts/electron-backend.v1.json. The /game/launch-auto
 *   route (and its legacy /launch alias) parses the requested
 *   appid and pipeline, resolves the effective pipeline rule
 *   from the per-appid overrides baked into mtsp-rules.toml,
 *   layers the maintained-C launch policy from app/src-c/
 *   on top, computes the WINEDLLOVERRIDES / WINEDLLPATH /
 *   DYLD_LIBRARY_PATH / DXMT_WINEMETAL_UNIXLIB /
 *   MS_GRAPHICS_BACKEND / WINEMSYNC environment that the
 *   bin/metalsharp-wine binary would receive, and returns the
 *   computed env as an env_pairs array. Actual Wine process
 *   spawning is stubbed (see TODO markers) so the route stays
 *   pure: the Electron shell inspects the env_pairs and either
 *   proxies a fork+exec locally or hands the values back to the
 *   maintained-C adapter in a later phase.
 *
 *   The /game/prepare, /game/running, /game/dual-info,
 *   /processes/force-kill, and /kill routes return either
 *   canonical {"ok":true} stubs or a graceful error envelope;
 *   /kill additionally signals the HTTP accept loop to exit by
 *   invoking http_server_stop on the bound server.
 *
 * IMPORTS
 *   "server.h"        MetalsharpResponse, METALSHARP_VERSION
 *   "http_server.h"   HttpServer, HttpRequest, http_server_register,
 *                      http_server_stop
 *   "database.h"      Database (passed in for forward compatibility;
 *                      no SQL is executed yet because every launch-
 *                      time fact lives in mtsp-rules.toml)
 *   "json.h"          json_parse, JsonValue, accessors
 *   "logger.h"        LOG_INFO, LOG_WARN, LOG_ERROR
 *   "config_parser.h" config_parse_rules, config_get_rule,
 *                      config_free_rules, MtspRules, PipelineRule
 *   launcher.h        MetalsharpLaunchPolicy, metalsharp_launch_policy,
 *                      metalsharp_launch_policy_valid
 *                      (declared in app/src-c/launcher.h)
 *   <stdarg.h>        va_list, va_start, va_end, va_copy (growable JSON helper)
 *   <stdbool.h>       bool, true, false
 *   <stddef.h>        size_t, NULL
 *   <stdio.h>         snprintf, vsnprintf
 *   <stdlib.h>        calloc, free
 *   <string.h>        memcpy, strlen, strcmp
 *
 * EXPORTS
 *   launch_register_routes(HttpServer *server, Database *db)
 *       Register every launch-route on the supplied HttpServer and
 *       bind the module to `db`. Called once during startup, before
 *       http_server_run(). Passing NULL on either side is a silent
 *       no-op so a half-initialised backend cannot crash inside
 *       the registry. Best-effort loads
 *       $METALSHARP_HOME/configs/mtsp-rules.toml into a module-local
 *       MtspRules container; a missing or malformed file is logged
 *       and ignored so the launch routes still serve requests with
 *       the maintained-C launch policy alone.
 *
 * SCHEMA
 *   Request body for POST /game/launch-auto and POST /launch:
 *     { "appid": <positive integer>, "pipeline": "<optional id>" }
 *   `appid` is required; `pipeline` defaults to "m12" when absent.
 *
 *   Response payload shape (every key is a member of the top-level
 *   data object so the HTTP layer renders it under "data"):
 *     {
 *       "ok": true,
 *       "appid": <integer>,
 *       "pipeline": "<resolved id>",
 *       "wine_binary": "<maintained policy wine path>",
 *       "bottle": "<policy pipeline id>",
 *       "env_pairs": [
 *         { "key": "WINEDLLOVERRIDES", "value": "..." },
 *         { "key": "WINEDLLPATH",      "value": "..." },
 *         { "key": "DYLD_LIBRARY_PATH","value": "..." },
 *         { "key": "DXMT_WINEMETAL_UNIXLIB","value": "..." },
 *         { "key": "MS_GRAPHICS_BACKEND","value": "..." },
 *         { "key": "MS_PIPELINE_ID",   "value": "<resolved id>" },
 *         { "key": "MS_APPID",         "value": "<decimal appid>" },
 *         { "key": "WINEMSYNC",        "value": "0" }
 *       ]
 *     }
 *
 *   Failure envelopes mirror the shared MetalsharpResponse:
 *     { "ok": false, "error": "<reason>" }
 *   Common reasons include: "launch-auto: missing appid",
 *   "launch-auto: invalid appid", "launch-auto: pipeline
 *   not found", "launch-auto: launch policy invalid".
 *
 * THREADING
 *   The HTTP server dispatches every request on a worker thread.
 *   The module-local Database handle, HttpServer handle, and
 *   MtspRules container are captured exactly once at
 *   registration and never swapped, so every worker reads a
 *   stable snapshot. Future per-appid overrides loaded from
 *   mtsp-rules.toml are immutable for the backend's lifetime
 *   so no synchronisation primitive is needed beyond what
 *   config_parse_rules already performs during initial load.
 */

#include "config_parser.h"
#include "database.h"
#include "http_server.h"
#include "json.h"
#include "logger.h"
#include "server.h"

#include "launcher.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Constants ── */

/*
 * Default pipeline the backend falls back to when the request
 * body carries no "pipeline" field. Matches the lane declared
 * the primary default in contract-spec.json → static_tables.
 */
#define LAUNCH_DEFAULT_PIPELINE "m12"

/*
 * Path template for the per-title MTSP rules file. The "{home}"
 * placeholder is substituted with the METALSHARP_HOME env var at
 * load time, so a backend started with METALSHARP_HOME=/foo reads
 * /foo/configs/mtsp-rules.toml without needing a C-side
 * configuration registry.
 */
#define LAUNCH_RULES_PATH_TEMPLATE "%s/configs/mtsp-rules.toml"

/*
 * Initial JSON builder capacity. Launch responses are small (a
 * dozen env pairs at most), so 256 bytes comfortably fits the
 * typical empty / fixed pipeline case; the buffer doubles as
 * needed.
 */
#define LAUNCH_JSONBUF_INIT 256u

/*
 * Per-string escape budget shared by every JSON builder helper.
 * 4 KiB matches PATH_MAX and gives comfortable headroom for any
 * possible wine-binary or DLL-override string in the maintained
 * launch policy.
 */
#define LAUNCH_ESCAPE_MAX 4096u

/*
 * Declared length of the integer text representation of an
 * unsigned 32-bit appid. 10 digits plus a sign covers the
 *     -2147483648..4294967295
 * range; unsigned values up to 4,294,967,295 need 10 digits.
 */
#define LAUNCH_APPID_TEXT 32u

/* ── Module-local state ── */

/*
 * Database handle captured at registration time. The HTTP layer
 * does not thread per-request context to the handler, so any
 * route that needs persistence looks up the handle here. Set
 * exactly once before http_server_run() and never reset; every
 * access goes through the Database wrapper which acquires its
 * own mutex.
 */
static Database* g_launch_db = NULL;

/*
 * Server handle captured at registration time so the /kill route
 * can request graceful shutdown from inside a worker thread. The
 * pointer is set exactly once and never replaced; http_server_stop
 * is documented as safe to call from any thread.
 */
static HttpServer* g_launch_server = NULL;

/*
 * Loaded MTSP rules container. NULL when the per-title rules
 * file was missing or malformed at registration time; in that
 * case /game/launch-auto and /game/resolve-routing fall back to
 * the maintained-C launch policy alone and skip the per-appid
 * override lookup.
 */
static MtspRules* g_launch_rules = NULL;

/* ── Forward declarations ── */

/* Route handlers. Each receives the parsed HTTP request and
 * returns a heap-allocated MetalsharpResponse that the HTTP
 * layer serialises to JSON, sends back to the client, and frees. */
static MetalsharpResponse* handle_game_launch_auto(const HttpRequest* req);
static MetalsharpResponse* handle_game_prepare(const HttpRequest* req);
static MetalsharpResponse* handle_game_resolve_routing(const HttpRequest* req);
static MetalsharpResponse* handle_game_running(const HttpRequest* req);
static MetalsharpResponse* handle_game_dual_info(const HttpRequest* req);
static MetalsharpResponse* handle_processes_force_kill(const HttpRequest* req);
static MetalsharpResponse* handle_kill(const HttpRequest* req);

/* ── Response builders ── */

/*
 * Build a successful MetalsharpResponse whose `data` payload is
 * a freshly-parsed JsonValue tree representing `body`. The HTTP
 * layer serialises the tree under a top-level "data" field. A
 * NULL or empty body is coerced to the literal "null" so the
 * caller never sees an invalid parse failure. Returns NULL only
 * on calloc failure; bad JSON is funnelled into an ok=false
 * response with a descriptive error_msg.
 */
static MetalsharpResponse* make_data_response(const char* body) {
    if (body == NULL || body[0] == '\0') {
        body = "null";
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

/*
 * Build a failure MetalsharpResponse. The message is duplicated
 * into heap-owned storage so callers need not keep `msg` alive.
 * Returns NULL only on calloc failure.
 */
static MetalsharpResponse* make_error_response(const char* msg) {
    MetalsharpResponse* r = calloc(1, sizeof(MetalsharpResponse));
    if (r == NULL) {
        return NULL;
    }
    r->ok = false;
    r->error_msg = strdup(msg != NULL ? msg : "error");
    return r;
}

/*
 * Build the {"ok":true} acknowledgement used by the
 * /game/prepare, /processes/force-kill, and /kill routes.
 * Centralising the literal keeps the wire shape in one place.
 */
static MetalsharpResponse* launch_stub_ok(void) {
    return make_data_response("{\"ok\":true}");
}

/* ── Growable JSON buffer ── */

/*
 * Minimal growable string buffer used to assemble the launch
 * envelope. Doubles capacity on demand; the `ok` flag short-
 * circuits subsequent appends after a failed allocation so
 * callers can keep invoking the helpers without explicit
 * branching at every site.
 */
typedef struct {
    char* data;
    size_t len;
    size_t cap;
    bool ok;
} jsonbuf_t;

static jsonbuf_t jsonbuf_new(void) {
    jsonbuf_t b;
    b.data = NULL;
    b.len = 0u;
    b.cap = 0u;
    b.ok = false;
    b.cap = LAUNCH_JSONBUF_INIT;
    b.data = malloc(b.cap);
    if (b.data != NULL) {
        b.ok = true;
        b.data[0] = '\0';
        b.len = 0u;
    }
    return b;
}

static void jsonbuf_free(jsonbuf_t* b) {
    if (b == NULL) {
        return;
    }
    free(b->data);
    b->data = NULL;
    b->len = 0u;
    b->cap = 0u;
    b->ok = false;
}

/*
 * Grow the backing storage so at least `need` more bytes can be
 * appended (plus the trailing NUL). Returns the new capacity on
 * success; returns 0 and leaves `*b` untouched on realloc failure.
 */
static size_t jsonbuf_reserve(jsonbuf_t* b, size_t need) {
    if (b == NULL || !b->ok) {
        return 0u;
    }
    if (b->cap >= b->len + need + 1u) {
        return b->cap;
    }
    size_t newcap = b->cap;
    while (newcap < b->len + need + 1u) {
        newcap *= 2u;
    }
    char* nd = realloc(b->data, newcap);
    if (nd == NULL) {
        b->ok = false;
        return 0u;
    }
    b->data = nd;
    b->cap = newcap;
    return newcap;
}

static void jsonbuf_append(jsonbuf_t* b, const char* s) {
    if (b == NULL || !b->ok || s == NULL) {
        return;
    }
    size_t n = strlen(s);
    if (n == 0u) {
        return;
    }
    if (jsonbuf_reserve(b, n) == 0u) {
        return;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

/*
 * Append `s` to `b` after JSON-escaping it. A NULL `s` is encoded
 * as `null`. Control characters below 0x20 are emitted using the
 *     \uXXXX
 * form; printable ASCII and high-byte UTF-8 sequences are appended
 * verbatim. The escape buffer is bounded so a single pathological
 * input cannot grow `b` without limit.
 */
static void jsonbuf_append_escaped(jsonbuf_t* b, const char* s) {
    if (b == NULL || !b->ok) {
        return;
    }
    if (s == NULL) {
        jsonbuf_append(b, "null");
        return;
    }
    char esc[LAUNCH_ESCAPE_MAX];
    size_t i = 0u;
    for (const unsigned char* p = (const unsigned char*)s; *p != '\0'; p++) {
        unsigned char c = *p;
        switch (c) {
        case '"':
            esc[i++] = '\\';
            esc[i++] = '"';
            break;
        case '\\':
            esc[i++] = '\\';
            esc[i++] = '\\';
            break;
        case '\b':
            esc[i++] = '\\';
            esc[i++] = 'b';
            break;
        case '\f':
            esc[i++] = '\\';
            esc[i++] = 'f';
            break;
        case '\n':
            esc[i++] = '\\';
            esc[i++] = 'n';
            break;
        case '\r':
            esc[i++] = '\\';
            esc[i++] = 'r';
            break;
        case '\t':
            esc[i++] = '\\';
            esc[i++] = 't';
            break;
        default:
            if (c < 0x20u) {
                int k = snprintf(esc + i, sizeof(esc) - i, "\\u%04x", (unsigned)c);
                if (k < 0 || (size_t)k >= sizeof(esc) - i) {
                    b->ok = false;
                    return;
                }
                i += (size_t)k;
            } else {
                esc[i++] = (char)c;
            }
            break;
        }
        if (i + 16u >= sizeof(esc)) {
            esc[i] = '\0';
            jsonbuf_append(b, esc);
            i = 0u;
        }
    }
    if (i > 0u) {
        esc[i] = '\0';
        jsonbuf_append(b, esc);
    }
}

/*
 * Append one {"key":"...","value":"..."} entry to `b`. Both
 * strings are JSON-escaped; a NULL value serialises as JSON null
 * (still quoted-as-string-shape) so the array shape stays
 * uniform.
 */
static void jsonbuf_append_env_pair(jsonbuf_t* b, const char* key, const char* value) {
    if (b == NULL || !b->ok) {
        return;
    }
    jsonbuf_append(b, "{\"key\":");
    jsonbuf_append_escaped(b, key);
    jsonbuf_append(b, ",\"value\":");
    if (value == NULL) {
        jsonbuf_append(b, "null");
    } else {
        jsonbuf_append(b, "\"");
        jsonbuf_append_escaped(b, value);
        jsonbuf_append(b, "\"");
    }
    jsonbuf_append(b, "}");
}

/* ── Rules loading ── */

/*
 * Best-effort load of the MTSP rules file at
 *     $METALSHARP_HOME/configs/mtsp-rules.toml
 * The METALSHARP_HOME env var is consulted at load time; a NULL
 * or empty value causes the loader to skip the parse with a
 * logged debug-level notice. A missing file, an allocation
 * failure, or a malformed file is logged at WARN level and the
 * module-local rules container stays NULL so the request
 * handlers can fall back to the maintained-C launch policy.
 */
static void launch_load_rules(void) {
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL || home[0] == '\0') {
        LOG_WARN("launch: METALSHARP_HOME unset; per-title rules disabled");
        return;
    }
    char path[PATH_MAX];
    int written = snprintf(path, sizeof(path), LAUNCH_RULES_PATH_TEMPLATE, home);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        LOG_WARN("launch: rules path too long under %s", home);
        return;
    }
    char* err = NULL;
    MtspRules* rules = config_parse_rules(path, &err);
    if (rules == NULL) {
        LOG_WARN("launch: cannot load rules from %s: %s", path, err != NULL ? err : "(unknown)");
        free(err);
        return;
    }
    g_launch_rules = rules;
    LOG_INFO("launch: loaded mtsp-rules from %s", path);
}

/* ── Pipeline resolution ── */

/*
 * Resolve the launch policy for the supplied pipeline id. When
 * `pipeline_id` is NULL or empty the caller-provided default
 * (LAUNCH_DEFAULT_PIPELINE) is substituted; the maintained-C
 * launcher validates the id and returns NULL when the id is not
 * one of the compiled-in policies. Invalid policies are
 * surfaced as NULL so callers can distinguish a missing id from
 * a known-bad id. The returned pointer is owned by the
 * maintained-C launcher module; callers must not free it.
 */
static const MetalsharpLaunchPolicy* resolve_launch_policy(const char* pipeline_id) {
    if (pipeline_id == NULL || pipeline_id[0] == '\0') {
        pipeline_id = LAUNCH_DEFAULT_PIPELINE;
    }
    return metalsharp_launch_policy(pipeline_id);
}

/*
 * Build the env_pairs array described by `policy`. The order is
 * stable so callers can rely on the position of well-known keys
 * (WINEDLLOVERRIDES first, MS_PIPELINE_ID last among the
 * pipeline-tagged slots); the MS_APPID and WINEMSYNC slots come
 * from the request and from the maintained launch policy
 * defaults respectively.
 */
static void jsonbuf_append_policy_env(jsonbuf_t* b, const MetalsharpLaunchPolicy* policy, unsigned int appid) {
    if (b == NULL || !b->ok || policy == NULL) {
        return;
    }
    jsonbuf_append(b, "[");
    jsonbuf_append_env_pair(b, "WINEDLLOVERRIDES", policy->dll_overrides);
    jsonbuf_append(b, ",");
    jsonbuf_append_env_pair(b, "WINEDLLPATH", policy->windows_dll_path);
    jsonbuf_append(b, ",");
    jsonbuf_append_env_pair(b, "DYLD_LIBRARY_PATH", policy->unix_library_path);
    jsonbuf_append(b, ",");
    jsonbuf_append_env_pair(b, "DXMT_WINEMETAL_UNIXLIB", policy->winemetal_unixlib);
    jsonbuf_append(b, ",");
    jsonbuf_append_env_pair(b, "MS_GRAPHICS_BACKEND", policy->graphics_backend);
    jsonbuf_append(b, ",");
    char appid_text[LAUNCH_APPID_TEXT];
    int n = snprintf(appid_text, sizeof(appid_text), "%u", appid);
    if (n > 0 && (size_t)n < sizeof(appid_text)) {
        jsonbuf_append_env_pair(b, "MS_APPID", appid_text);
    } else {
        jsonbuf_append_env_pair(b, "MS_APPID", NULL);
    }
    jsonbuf_append(b, ",");
    jsonbuf_append_env_pair(b, "MS_PIPELINE_ID", policy->pipeline_id);
    jsonbuf_append(b, ",");
    jsonbuf_append_env_pair(b, "WINEMSYNC", "0");
    jsonbuf_append(b, "]");
}

/* ── Request body parsing ── */

/*
 * Parse the request body as a JSON object and extract the
 * `appid` (required) and `pipeline` (optional) fields. The
 * returned values are owned by the request body lifetime; the
 * caller must finish using them before any subsequent read of
 * the request. On any failure the function returns false and
 * fills `error_msg` with a heap-allocated, NUL-terminated
 * diagnostic the caller must release with free().
 */
static bool parse_launch_body(const char* body, size_t body_len, const char** pipeline_out, unsigned int* appid_out,
                              char** error_msg) {
    *pipeline_out = LAUNCH_DEFAULT_PIPELINE;
    *appid_out = 0u;
    if (body == NULL || body_len == 0u) {
        *error_msg = strdup("launch-auto: missing appid");
        return false;
    }
    char* err = NULL;
    JsonValue* root = json_parse(body, body_len, &err);
    if (root == NULL) {
        *error_msg = strdup(err != NULL ? err : "launch-auto: invalid JSON");
        free(err);
        return false;
    }
    if (json_type(root) != JSON_OBJECT) {
        json_free(root);
        *error_msg = strdup("launch-auto: invalid JSON");
        return false;
    }
    JsonValue* id_node = json_object_get(root, "appid");
    if (id_node == NULL || json_type(id_node) != JSON_NUMBER) {
        json_free(root);
        *error_msg = strdup("launch-auto: missing appid");
        return false;
    }
    double id_value = json_get_number(id_node, 0.0);
    if (id_value <= 0.0 || id_value > (double)UINT32_MAX) {
        json_free(root);
        *error_msg = strdup("launch-auto: invalid appid");
        return false;
    }
    *appid_out = (unsigned int)id_value;
    JsonValue* pipe_node = json_object_get(root, "pipeline");
    if (pipe_node != NULL && json_type(pipe_node) == JSON_STRING) {
        const char* s = json_get_string(pipe_node);
        if (s != NULL && s[0] != '\0') {
            *pipeline_out = s;
        }
    }
    json_free(root);
    return true;
}

/* ── /game/launch-auto and /launch ── */

/*
 * Shared handler for POST /game/launch-auto and POST /launch.
 * Both routes accept the same LaunchAutoRequest body and
 * return the same LaunchResult payload; /launch is the legacy
 * alias kept for backward compatibility with the original
 *     run_game
 * flow. The route computes the env without spawning Wine so the
 * Electron shell can preview or override the env before
 * launching the wine binary.
 */
static MetalsharpResponse* handle_launch_auto_impl(const HttpRequest* req) {
    if (req == NULL) {
        return make_error_response("launch-auto: invalid request");
    }
    const char* pipeline_id = LAUNCH_DEFAULT_PIPELINE;
    unsigned int appid = 0u;
    char* err = NULL;
    if (!parse_launch_body(req->body, req->body_len, &pipeline_id, &appid, &err)) {
        MetalsharpResponse* resp = make_error_response(err != NULL ? err : "launch-auto: invalid request");
        free(err);
        return resp;
    }
    const MetalsharpLaunchPolicy* policy = resolve_launch_policy(pipeline_id);
    if (policy == NULL || !metalsharp_launch_policy_valid(policy)) {
        return make_error_response("launch-auto: pipeline not found");
    }
    if (g_launch_rules != NULL) {
        PipelineRule* rule = config_get_rule(g_launch_rules, appid);
        (void)rule; /* captured for future diagnostics; env is built from
                     * the maintained policy because mtsp-rules.toml only
                     * carries override metadata, not the actual env */
    }
    jsonbuf_t body = jsonbuf_new();
    if (!body.ok) {
        return make_error_response("launch-auto: out of memory");
    }
    jsonbuf_append(&body, "{\"ok\":true,\"appid\":");
    char appid_text[LAUNCH_APPID_TEXT];
    int n = snprintf(appid_text, sizeof(appid_text), "%u", appid);
    if (n > 0 && (size_t)n < sizeof(appid_text)) {
        jsonbuf_append(&body, appid_text);
    } else {
        jsonbuf_append(&body, "0");
    }
    jsonbuf_append(&body, ",\"pipeline\":");
    jsonbuf_append_escaped(&body, policy->pipeline_id);
    jsonbuf_append(&body, ",\"wine_binary\":");
    jsonbuf_append_escaped(&body, policy->wine_binary);
    jsonbuf_append(&body, ",\"bottle\":");
    jsonbuf_append_escaped(&body, policy->pipeline_id);
    jsonbuf_append(&body, ",\"env_pairs\":");
    jsonbuf_append_policy_env(&body, policy, appid);
    jsonbuf_append(&body, "}");
    if (!body.ok) {
        jsonbuf_free(&body);
        return make_error_response("launch-auto: out of memory");
    }
    MetalsharpResponse* resp = make_data_response(body.data);
    jsonbuf_free(&body);
    if (resp == NULL) {
        return make_error_response("launch-auto: out of memory");
    }
    /* TODO: actual Wine process spawning is intentionally stubbed;
     * the Electron shell (or a future C adapter) uses the env_pairs
     * above to launch bin/metalsharp-wine directly. */
    return resp;
}

static MetalsharpResponse* handle_game_launch_auto(const HttpRequest* req) {
    return handle_launch_auto_impl(req);
}

/* ── /game/prepare ── */

/*
 * POST /game/prepare — validate the bottle and deploy the
 * required DLLs for the supplied pipeline. The real
 * implementation will fork a wineboot preparation helper and
 * stage DXMT/Wine/Mono components into the prefix; this stub
 * acknowledges the request so the Electron shell can drive the
 * UI transition. The request body is intentionally ignored for
 * the same reason: the configuration lives entirely in the
 * METALSHARP_HOME tree.
 */
static MetalsharpResponse* handle_game_prepare(const HttpRequest* req) {
    (void)req;
    return launch_stub_ok();
}

/* ── /game/resolve-routing ── */

/*
 * POST /game/resolve-routing — return the resolved pipeline for
 * the supplied appid. Pulls the effective PipelineRule from the
 * loaded MtspRules (per-appid override or m12 default) and
 * falls back to the maintained-C launch policy when no rules
 * are loaded. The route never invokes any wine or DXMT side
 * effect; it is purely informational.
 */
static MetalsharpResponse* handle_game_resolve_routing(const HttpRequest* req) {
    if (req == NULL) {
        return make_error_response("resolve-routing: invalid request");
    }
    const char* pipeline_id = LAUNCH_DEFAULT_PIPELINE;
    unsigned int appid = 0u;
    char* err = NULL;
    if (!parse_launch_body(req->body, req->body_len, &pipeline_id, &appid, &err)) {
        MetalsharpResponse* resp = make_error_response(err != NULL ? err : "resolve-routing: invalid request");
        free(err);
        return resp;
    }
    const MetalsharpLaunchPolicy* policy = resolve_launch_policy(pipeline_id);
    if (policy == NULL || !metalsharp_launch_policy_valid(policy)) {
        return make_error_response("resolve-routing: pipeline not found");
    }
    const char* resolved_pipeline = policy->pipeline_id;
    const char* rule_name = NULL;
    if (g_launch_rules != NULL) {
        PipelineRule* rule = config_get_rule(g_launch_rules, appid);
        if (rule != NULL) {
            rule_name = rule->name;
            if (rule->pipeline != NULL && rule->pipeline[0] != '\0') {
                resolved_pipeline = rule->pipeline;
            }
        }
    }
    jsonbuf_t body = jsonbuf_new();
    if (!body.ok) {
        return make_error_response("resolve-routing: out of memory");
    }
    jsonbuf_append(&body, "{\"ok\":true,\"appid\":");
    char appid_text[LAUNCH_JSONBUF_INIT];
    int n = snprintf(appid_text, sizeof(appid_text), "%u", appid);
    if (n > 0 && (size_t)n < sizeof(appid_text)) {
        jsonbuf_append(&body, appid_text);
    } else {
        jsonbuf_append(&body, "0");
    }
    jsonbuf_append(&body, ",\"resolved_pipeline\":");
    jsonbuf_append_escaped(&body, resolved_pipeline);
    jsonbuf_append(&body, ",\"wine_binary\":");
    jsonbuf_append_escaped(&body, policy->wine_binary);
    jsonbuf_append(&body, ",\"graphics_backend\":");
    jsonbuf_append_escaped(&body, policy->graphics_backend);
    jsonbuf_append(&body, ",\"name\":");
    jsonbuf_append_escaped(&body, rule_name);
    jsonbuf_append(&body, "}");
    if (!body.ok) {
        jsonbuf_free(&body);
        return make_error_response("resolve-routing: out of memory");
    }
    MetalsharpResponse* resp = make_data_response(body.data);
    jsonbuf_free(&body);
    return resp;
}

/* ── /game/running ── */

/*
 * GET /game/running — list the running game processes spawned
 * by previous /game/launch-auto invocations. The tracking set
 * lives in global_state.RUNNING_GAMES in the contract; this
 * module currently does not own the set (per-process spawning
 * is stubbed) so the response always reports an empty list.
 * Returning {"ok":true,"running":[]} keeps the wire shape
 * stable across the stub-to-real transition.
 */
static MetalsharpResponse* handle_game_running(const HttpRequest* req) {
    (void)req;
    return make_data_response("{\"ok\":true,\"running\":[]}");
}

/* ── /game/dual-info ── */

/*
 * GET /game/dual-info — return dual-binary (Intel + Apple
 * Silicon) availability metadata for the current platform. The
 * real implementation will call the maintained-C launcher to
 * inspect bin/metalsharp-wine-universal and report both slices;
 * the stub reports no dual-binary layout so the Electron shell
 * falls back to the single-slice install.
 */
static MetalsharpResponse* handle_game_dual_info(const HttpRequest* req) {
    (void)req;
    return make_data_response("{\"ok\":true,\"dual_binary\":false,\"slices\":[]}");
}

/* ── /processes/force-kill ── */

/*
 * POST /processes/force-kill — terminate a running game process
 * identified by the request body. The real implementation will
 * look the PID up in the RUNNING_GAMES set and deliver SIGKILL;
 * the stub acknowledges the request so the Electron shell can
 * complete the kill flow without surfacing a 404. The body is
 * intentionally ignored until process spawning is wired up.
 */
static MetalsharpResponse* handle_processes_force_kill(const HttpRequest* req) {
    (void)req;
    return launch_stub_ok();
}

/* ── /kill ── */

/*
 * POST /kill — graceful backend shutdown. Calls http_server_stop
 * on the bound server so the accept loop exits; the main thread
 * then runs the cleanup sequence declared in
 *     contract-spec.json → shutdown_sequence
 * The route always returns {"ok":true} so the Electron shell
 * observes an immediate acknowledgement even if the accept loop
 * has not yet drained.
 */
static MetalsharpResponse* handle_kill(const HttpRequest* req) {
    (void)req;
    if (g_launch_server != NULL) {
        http_server_stop(g_launch_server);
    }
    return launch_stub_ok();
}

/* ── Route registration ── */

/*
 * Register every launch route on the supplied HttpServer and
 * bind the module to `db`. NULL arguments are a silent no-op so
 * a half-initialised backend cannot crash inside the registry.
 * The MTSP rules container is best-effort loaded; a missing or
 * malformed file is logged and ignored so the launch routes
 * still serve requests.
 */
void launch_register_routes(HttpServer* server, Database* db) {
    if (server == NULL) {
        LOG_WARN("launch_register_routes called with NULL server");
        return;
    }
    g_launch_server = server;
    g_launch_db = db;
    launch_load_rules();

    /* Primary launch endpoint plus its /launch legacy alias.
     * Both routes resolve to the same implementation because the
     * contract documents identical request/response shapes. */
    http_server_register(server, "POST", "/game/launch-auto", handle_game_launch_auto);
    http_server_register(server, "POST", "/launch", handle_game_launch_auto);

    /* Prepare / resolve / inspect family. */
    http_server_register(server, "POST", "/game/prepare", handle_game_prepare);
    http_server_register(server, "POST", "/game/resolve-routing", handle_game_resolve_routing);
    http_server_register(server, "GET", "/game/running", handle_game_running);
    http_server_register(server, "GET", "/game/dual-info", handle_game_dual_info);

    /* Process management: force-kill a running game, or shut
     * the entire backend down. */
    http_server_register(server, "POST", "/processes/force-kill", handle_processes_force_kill);
    http_server_register(server, "POST", "/kill", handle_kill);

    LOG_INFO("launch routes registered (8)");
}
