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

#include "launch.h"
#include "compat_log.h"
#include "config_parser.h"
#include "database.h"
#include "http_server.h"
#include "json.h"
#include "logger.h"
#include "server.h"

#include "launcher.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

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
static MetalsharpResponse* handle_launch(const HttpRequest* req);
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
    r->data_kind = METALSHARP_RESPONSE_JSON_VALUE;
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
    jsonbuf_append(b, "\"");
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
    jsonbuf_append(b, "\"");
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
    char home_path[PATH_MAX] = "", resource_path[PATH_MAX] = "";
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL || home[0] == '\0')
        home = getenv("HOME");
    if (home != NULL)
        (void)snprintf(home_path, sizeof(home_path), LAUNCH_RULES_PATH_TEMPLATE, home);
#ifdef __APPLE__
    uint32_t executable_size = PATH_MAX;
    char executable[PATH_MAX];
    if (_NSGetExecutablePath(executable, &executable_size) == 0) {
        char* slash = strrchr(executable, '/');
        if (slash != NULL) {
            *slash = '\0';
            (void)snprintf(resource_path, sizeof(resource_path), "%s/../configs/mtsp-rules.toml", executable);
        }
    }
#endif
    const char* candidates[] = {home_path, resource_path, "configs/mtsp-rules.toml", "../configs/mtsp-rules.toml",
                                "../../configs/mtsp-rules.toml"};
    char* last_error = NULL;
    const char* last_path = home_path;
    for (size_t i = 0u; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (candidates[i][0] == '\0')
            continue;
        char* error = NULL;
        MtspRules* rules = config_parse_rules(candidates[i], &error);
        if (rules != NULL) {
            free(last_error);
            g_launch_rules = rules;
            LOG_INFO("launch: loaded mtsp-rules from %s", candidates[i]);
            return;
        }
        free(last_error);
        last_error = error;
        last_path = candidates[i];
    }
    LOG_WARN("launch: cannot load rules from %s: %s", last_path, last_error != NULL ? last_error : "(unknown)");
    free(last_error);
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
static bool parse_launch_body(const char* body, size_t body_len, char* pipeline_out, size_t pipeline_size,
                              unsigned int* appid_out, char** error_msg) {
    if (pipeline_out == NULL || pipeline_size == 0u || appid_out == NULL || error_msg == NULL)
        return false;
    snprintf(pipeline_out, pipeline_size, "%s", LAUNCH_DEFAULT_PIPELINE);
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
            int written = snprintf(pipeline_out, pipeline_size, "%s", s);
            if (written < 0 || (size_t)written >= pipeline_size) {
                json_free(root);
                *error_msg = strdup("launch-auto: pipeline too long");
                return false;
            }
        }
    }
    json_free(root);
    return true;
}

/* ── Legacy /launch process handoff ── */

static bool launch_join_path(char* output, size_t output_size, const char* left, const char* right) {
    if (output == NULL || output_size == 0u || left == NULL || right == NULL)
        return false;
    int written = snprintf(output, output_size, "%s%s%s", left,
                           left[0] != '\0' && left[strlen(left) - 1u] == '/' ? "" : "/", right);
    return written >= 0 && (size_t)written < output_size;
}

static bool launch_path_exists(const char* path) {
    struct stat st;
    return path != NULL && stat(path, &st) == 0;
}

static bool launch_find_wine(char* output, size_t output_size) {
    const char* home = getenv("METALSHARP_HOME");
    char path[PATH_MAX];
    if (home != NULL && home[0] != '\0') {
        static const char* relative_candidates[] = {"runtime/wine/bin/metalsharp-wine", "runtime/wine/bin/wine"};
        for (size_t i = 0u; i < sizeof(relative_candidates) / sizeof(relative_candidates[0]); i++) {
            if (launch_join_path(path, sizeof(path), home, relative_candidates[i]) && launch_path_exists(path)) {
                int written = snprintf(output, output_size, "%s", path);
                return written >= 0 && (size_t)written < output_size;
            }
        }
    }
    static const char* candidates[] = {"/opt/homebrew/bin/wine64", "/usr/bin/wine", "/usr/local/bin/wine"};
    for (size_t i = 0u; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (launch_path_exists(candidates[i])) {
            int written = snprintf(output, output_size, "%s", candidates[i]);
            return written >= 0 && (size_t)written < output_size;
        }
    }
    return false;
}

static bool launch_find_mono(char* output, size_t output_size) {
    static const char* candidates[] = {"/opt/homebrew/bin/mono", "/usr/local/bin/mono"};
    for (size_t i = 0u; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (launch_path_exists(candidates[i])) {
            int written = snprintf(output, output_size, "%s", candidates[i]);
            return written >= 0 && (size_t)written < output_size;
        }
    }
    return false;
}

static void launch_set_runtime_environment(const char* metalsharp_home) {
    if (metalsharp_home == NULL || metalsharp_home[0] == '\0')
        return;
    char value[PATH_MAX * 2u];
    int written = snprintf(value, sizeof(value), "%s/runtime/wine/lib:%s/runtime/wine/lib/wine/x86_64-unix",
                           metalsharp_home, metalsharp_home);
    if (written < 0 || (size_t)written >= sizeof(value))
        return;
#if defined(__APPLE__)
    (void)setenv("DYLD_FALLBACK_LIBRARY_PATH", value, 1);
#elif defined(__linux__)
    (void)setenv("LD_LIBRARY_PATH", value, 1);
#endif
}

static void* launch_reap_child(void* opaque) {
    pid_t pid = (pid_t)(intptr_t)opaque;
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
    }
    return NULL;
}

static bool launch_spawn(const char* executable, char* const argv[], const char* working_directory,
                         const char* wine_prefix, bool fna_environment, pid_t* pid_out, char* error,
                         size_t error_size) {
    int exec_pipe[2];
    if (pipe(exec_pipe) != 0) {
        snprintf(error, error_size, "%s", strerror(errno));
        return false;
    }
    (void)fcntl(exec_pipe[1], F_SETFD, FD_CLOEXEC);
    pid_t pid = fork();
    if (pid < 0) {
        snprintf(error, error_size, "%s", strerror(errno));
        close(exec_pipe[0]);
        close(exec_pipe[1]);
        return false;
    }
    if (pid == 0) {
        close(exec_pipe[0]);
        if (working_directory != NULL && chdir(working_directory) != 0) {
            int child_errno = errno;
            (void)write(exec_pipe[1], &child_errno, sizeof(child_errno));
            _exit(127);
        }
        if (wine_prefix != NULL)
            (void)setenv("WINEPREFIX", wine_prefix, 1);
        launch_set_runtime_environment(getenv("METALSHARP_HOME"));
        if (fna_environment) {
            (void)setenv("METAL_DEVICE_WRAPPER_TYPE", "0", 1);
#if defined(__APPLE__)
            (void)setenv("DYLD_LIBRARY_PATH", ".", 1);
#elif defined(__linux__)
            (void)setenv("LD_LIBRARY_PATH", ".", 1);
#endif
        }
        execv(executable, argv);
        int child_errno = errno;
        (void)write(exec_pipe[1], &child_errno, sizeof(child_errno));
        _exit(127);
    }
    close(exec_pipe[1]);
    int child_errno = 0;
    ssize_t read_size;
    do {
        read_size = read(exec_pipe[0], &child_errno, sizeof(child_errno));
    } while (read_size < 0 && errno == EINTR);
    close(exec_pipe[0]);
    if (read_size > 0) {
        while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
        }
        snprintf(error, error_size, "%s", strerror(child_errno));
        return false;
    }
    pthread_t reaper;
    if (pthread_create(&reaper, NULL, launch_reap_child, (void*)(intptr_t)pid) == 0)
        pthread_detach(reaper);
    *pid_out = pid;
    return true;
}

static bool launch_ensure_wine_prefix(const char* wine, const char* prefix, char* error, size_t error_size) {
    char system32[PATH_MAX];
    if (!launch_join_path(system32, sizeof(system32), prefix, "drive_c/windows/system32")) {
        snprintf(error, error_size, "failed to initialize Wine prefix");
        return false;
    }
    if (launch_path_exists(system32))
        return true;
    pid_t pid = fork();
    if (pid < 0) {
        snprintf(error, error_size, "%s", strerror(errno));
        return false;
    }
    if (pid == 0) {
        (void)setenv("WINEPREFIX", prefix, 1);
        launch_set_runtime_environment(getenv("METALSHARP_HOME"));
        int null_fd = open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            (void)dup2(null_fd, STDOUT_FILENO);
            (void)dup2(null_fd, STDERR_FILENO);
            if (null_fd > STDERR_FILENO)
                close(null_fd);
        }
        execl(wine, wine, "wineboot", "--init", (char*)NULL);
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR)
            continue;
        snprintf(error, error_size, "%s", strerror(errno));
        return false;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        snprintf(error, error_size, "failed to initialize Wine prefix");
        return false;
    }
    return true;
}

static bool launch_name_has_suffix(const char* name, const char* suffix) {
    size_t name_length = strlen(name), suffix_length = strlen(suffix);
    return name_length >= suffix_length && strcmp(name + name_length - suffix_length, suffix) == 0;
}

static void launch_ascii_lower(char* value) {
    for (; value != NULL && *value != '\0'; value++) {
        if (*value >= 'A' && *value <= 'Z')
            *value = (char)(*value - 'A' + 'a');
    }
}

static bool launch_executable_name_allowed(const char* name, bool second_pass) {
    char lower[NAME_MAX + 1u];
    int written = snprintf(lower, sizeof(lower), "%s", name);
    if (written < 0 || (size_t)written >= sizeof(lower) || !launch_name_has_suffix(name, ".exe"))
        return false;
    launch_ascii_lower(lower);
    if (!second_pass && strncmp(lower, "terraria", 8u) == 0 && strstr(lower, "server") == NULL)
        return true;
    if (!second_pass && strncmp(lower, "hl2", 3u) == 0 && strstr(lower, "launcher") == NULL)
        return true;
    static const char* excluded[] = {"setup", "redist", "dotnet", "installer", "uninstall", "vcredist", "crashhandler"};
    for (size_t i = 0u; i < sizeof(excluded) / sizeof(excluded[0]); i++)
        if (strstr(lower, excluded[i]) != NULL)
            return false;
    if (second_pass && strstr(lower, "server") != NULL)
        return false;
    return true;
}

bool metalsharp_launch_wine_executable(const char* executable_path, const char* working_directory,
                                       const char* prefix_override, const char* const* arguments, size_t argument_count,
                                       pid_t* pid_out, char* error, size_t error_size) {
    if (executable_path == NULL || executable_path[0] == '\0' || pid_out == NULL || error == NULL || error_size == 0u) {
        if (error != NULL && error_size > 0u)
            snprintf(error, error_size, "invalid launch request");
        return false;
    }
    char wine[PATH_MAX], prefix[PATH_MAX];
    if (!launch_find_wine(wine, sizeof(wine))) {
        snprintf(error, error_size, "wine not found");
        return false;
    }
    if (prefix_override != NULL && prefix_override[0] != '\0') {
        int written = snprintf(prefix, sizeof(prefix), "%s", prefix_override);
        if (written < 0 || (size_t)written >= sizeof(prefix)) {
            snprintf(error, error_size, "File name too long (os error 63)");
            return false;
        }
    } else {
        const char* home = getenv("METALSHARP_HOME");
        int written = home != NULL && home[0] != '\0' ? snprintf(prefix, sizeof(prefix), "%s/prefix-steam", home)
                                                      : snprintf(prefix, sizeof(prefix), "%s/.metalsharp/prefix-steam",
                                                                 getenv("HOME") != NULL ? getenv("HOME") : "");
        if (written < 0 || (size_t)written >= sizeof(prefix)) {
            snprintf(error, error_size, "File name too long (os error 63)");
            return false;
        }
    }
    if (!launch_ensure_wine_prefix(wine, prefix, error, error_size))
        return false;
    if (argument_count > 4096u) {
        snprintf(error, error_size, "too many launch arguments");
        return false;
    }
    char** argv = calloc(argument_count + 3u, sizeof(char*));
    if (argv == NULL) {
        snprintf(error, error_size, "out of memory");
        return false;
    }
    argv[0] = wine;
    argv[1] = (char*)executable_path;
    for (size_t i = 0u; i < argument_count; i++)
        argv[i + 2u] = (char*)arguments[i];
    bool launched = launch_spawn(wine, argv, working_directory, prefix, false, pid_out, error, error_size);
    free(argv);
    return launched;
}

static bool launch_find_game_exe_recursive(const char* directory, unsigned depth, bool second_pass, char* output,
                                           size_t output_size) {
    DIR* dir = opendir(directory);
    if (dir == NULL)
        return false;
    bool found = false;
    struct dirent* entry;
    while (!found && (entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char path[PATH_MAX];
        if (!launch_join_path(path, sizeof(path), directory, entry->d_name))
            continue;
        struct stat st;
        if (lstat(path, &st) != 0)
            continue;
        if (S_ISREG(st.st_mode) && launch_executable_name_allowed(entry->d_name, second_pass)) {
            int written = snprintf(output, output_size, "%s", path);
            found = written >= 0 && (size_t)written < output_size;
        } else if (S_ISDIR(st.st_mode) && depth < 3u) {
            found = launch_find_game_exe_recursive(path, depth + 1u, second_pass, output, output_size);
        }
    }
    closedir(dir);
    return found;
}

static bool launch_resolve_game_exe(unsigned int appid, char* output, size_t output_size) {
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL || home[0] == '\0')
        home = getenv("HOME");
    if (home == NULL)
        home = "";
    char game_root[PATH_MAX];
    int written =
        snprintf(game_root, sizeof(game_root), "%s%s%s/games/%u", home,
                 getenv("METALSHARP_HOME") != NULL && getenv("METALSHARP_HOME")[0] != '\0' ? "" : "/",
                 getenv("METALSHARP_HOME") != NULL && getenv("METALSHARP_HOME")[0] != '\0' ? "" : ".metalsharp", appid);
    if (written < 0 || (size_t)written >= sizeof(game_root))
        return false;
    if (launch_find_game_exe_recursive(game_root, 0u, false, output, output_size) ||
        launch_find_game_exe_recursive(game_root, 0u, true, output, output_size))
        return true;
    written = snprintf(output, output_size, "%s", game_root);
    return written >= 0 && (size_t)written < output_size;
}

static MetalsharpResponse* launch_internal_error(const char* message) {
    MetalsharpResponse* response = make_error_response(message);
    if (response != NULL)
        response->http_status = 500;
    return response;
}

static MetalsharpResponse* handle_launch(const HttpRequest* req) {
    JsonValue* body = req != NULL ? json_parse(req->body, req->body_len, NULL) : NULL;
    const char* exe_value =
        body != NULL && json_type(body) == JSON_OBJECT ? json_get_string(json_object_get(body, "exePath")) : NULL;
    const char* exe = exe_value != NULL ? exe_value : "";
    JsonValue* steam_id_value =
        body != NULL && json_type(body) == JSON_OBJECT ? json_object_get(body, "steamAppId") : NULL;
    bool has_steam_id = steam_id_value != NULL && json_type(steam_id_value) == JSON_NUMBER &&
                        json_get_number(steam_id_value, -1.0) >= 0.0;
    unsigned int steam_id = has_steam_id ? (unsigned int)json_get_number(steam_id_value, 0.0) : 0u;
    char resolved[PATH_MAX];
    if (has_steam_id && strstr(exe, ".exe") == NULL) {
        if (!launch_resolve_game_exe(steam_id, resolved, sizeof(resolved))) {
            json_free(body);
            return launch_internal_error("game path too long");
        }
    } else {
        int written = snprintf(resolved, sizeof(resolved), "%s", exe);
        if (written < 0 || (size_t)written >= sizeof(resolved)) {
            json_free(body);
            return launch_internal_error("File name too long (os error 63)");
        }
    }
    metalsharp_app_log("Launching: %s", resolved);

    bool use_mono = false;
    if (has_steam_id) {
        const char* home = getenv("METALSHARP_HOME");
        char marker[PATH_MAX];
        if (home != NULL && home[0] != '\0') {
            int written = snprintf(marker, sizeof(marker), "%s/games/%u/.metalsharp_prepared", home, steam_id);
            if (written >= 0 && (size_t)written < sizeof(marker)) {
                FILE* file = fopen(marker, "rb");
                if (file != NULL) {
                    char contents[4096];
                    size_t length = fread(contents, 1u, sizeof(contents) - 1u, file);
                    contents[length] = '\0';
                    fclose(file);
                    use_mono = strstr(contents, "is_dotnet=true") != NULL;
                }
            }
        }
    }

    char executable[PATH_MAX], error[256];
    pid_t pid = 0;
    bool launched = false;
    if (use_mono) {
        metalsharp_app_log("Detected XNA/FNA game — using mono runtime");
        if (!launch_find_mono(executable, sizeof(executable))) {
            snprintf(error, sizeof(error), "mono not found — install with: brew install mono");
        } else {
            char working_directory[PATH_MAX];
            int written = snprintf(working_directory, sizeof(working_directory), "%s", resolved);
            char* slash =
                written >= 0 && (size_t)written < sizeof(working_directory) ? strrchr(working_directory, '/') : NULL;
            if (slash == NULL) {
                snprintf(error, sizeof(error), "no parent dir for exe");
            } else {
                *slash = '\0';
                char* argv[] = {executable, resolved, NULL};
                launched = launch_spawn(executable, argv, working_directory, NULL, true, &pid, error, sizeof(error));
            }
        }
    } else if (!launch_find_wine(executable, sizeof(executable))) {
        snprintf(error, sizeof(error), "wine not found");
    } else {
        const char* home = getenv("METALSHARP_HOME");
        char prefix[PATH_MAX];
        if (home == NULL || home[0] == '\0') {
            const char* user_home = getenv("HOME");
            int written =
                snprintf(prefix, sizeof(prefix), "%s/.metalsharp/prefix-steam", user_home != NULL ? user_home : "");
            if (written < 0 || (size_t)written >= sizeof(prefix))
                snprintf(error, sizeof(error), "File name too long (os error 63)");
            else if (launch_ensure_wine_prefix(executable, prefix, error, sizeof(error))) {
                char* argv[] = {executable, resolved, NULL};
                launched = launch_spawn(executable, argv, NULL, prefix, false, &pid, error, sizeof(error));
            }
        } else {
            int written = snprintf(prefix, sizeof(prefix), "%s/prefix-steam", home);
            if (written < 0 || (size_t)written >= sizeof(prefix))
                snprintf(error, sizeof(error), "File name too long (os error 63)");
            else if (launch_ensure_wine_prefix(executable, prefix, error, sizeof(error))) {
                char* argv[] = {executable, resolved, NULL};
                launched = launch_spawn(executable, argv, NULL, prefix, false, &pid, error, sizeof(error));
            }
        }
    }
    json_free(body);
    if (!launched) {
        metalsharp_app_log("Launch failed: %s", error);
        return launch_internal_error(error);
    }
    metalsharp_app_log("Process started: pid %ld", (long)pid);
    char response[96];
    int written = snprintf(response, sizeof(response), "{\"ok\":true,\"pid\":%ld}", (long)pid);
    if (written < 0 || (size_t)written >= sizeof(response))
        return launch_internal_error("internal error");
    return make_data_response(response);
}

/* ── /game/launch-auto ── */

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
static MetalsharpResponse* launch_bad_request(const char* message) {
    MetalsharpResponse* response = make_error_response(message);
    if (response != NULL)
        response->http_status = 400;
    return response;
}

static MetalsharpResponse* handle_launch_auto_impl(const HttpRequest* req) {
    if (req == NULL) {
        return make_error_response("launch-auto: invalid request");
    }
    char pipeline_id[256];
    unsigned int appid = 0u;
    char* err = NULL;
    if (!parse_launch_body(req->body, req->body_len, pipeline_id, sizeof(pipeline_id), &appid, &err)) {
        free(err);
        return launch_bad_request("appid required");
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
    JsonValue* body = req != NULL ? json_parse(req->body, req->body_len, NULL) : NULL;
    bool has_appid =
        body != NULL && json_type(body) == JSON_OBJECT && json_get_number(json_object_get(body, "appid"), 0.0) > 0.0;
    json_free(body);
    if (!has_appid)
        return launch_bad_request("appid required");
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
    char pipeline_id[256];
    unsigned int appid = 0u;
    char* err = NULL;
    if (!parse_launch_body(req->body, req->body_len, pipeline_id, sizeof(pipeline_id), &appid, &err)) {
        free(err);
        return launch_bad_request("appid required");
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
static bool dual_join(char* output, size_t output_size, const char* left, const char* right) {
    int written = snprintf(output, output_size, "%s%s%s", left,
                           left[0] != '\0' && left[strlen(left) - 1u] == '/' ? "" : "/", right);
    return written >= 0 && (size_t)written < output_size;
}

static bool dual_directory(const char* path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static bool dual_valid_exe(const char* name) {
    static const char* rejected[] = {"setup",     "redist",   "dotnet",       "installer",
                                     "uninstall", "vcredist", "crashhandler", "server"};
    char lower[PATH_MAX];
    size_t length = strlen(name);
    if (length >= sizeof(lower))
        return false;
    for (size_t i = 0u; i <= length; i++)
        lower[i] = (char)tolower((unsigned char)name[i]);
    for (size_t i = 0u; i < sizeof(rejected) / sizeof(rejected[0]); i++)
        if (strstr(lower, rejected[i]) != NULL)
            return false;
    return true;
}

static bool dual_has_windows_exe(const char* directory, unsigned depth) {
    if (depth > 5u)
        return false;
    DIR* stream = opendir(directory);
    if (stream == NULL)
        return false;
    bool found = false;
    struct dirent* entry;
    while (!found && (entry = readdir(stream)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char path[PATH_MAX];
        if (!dual_join(path, sizeof(path), directory, entry->d_name))
            continue;
        struct stat info;
        if (stat(path, &info) != 0)
            continue;
        size_t length = strlen(entry->d_name);
        if (S_ISREG(info.st_mode) && length >= 4u && strcasecmp(entry->d_name + length - 4u, ".exe") == 0 &&
            dual_valid_exe(entry->d_name))
            found = true;
        else if (S_ISDIR(info.st_mode))
            found = dual_has_windows_exe(path, depth + 1u);
    }
    closedir(stream);
    return found;
}

static bool dual_find_macos_app(const char* directory, unsigned depth, char* output, size_t output_size) {
    if (depth > 2u)
        return false;
    DIR* stream = opendir(directory);
    if (stream == NULL)
        return false;
    bool found = false;
    struct dirent* entry;
    while (!found && (entry = readdir(stream)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char path[PATH_MAX];
        if (!dual_join(path, sizeof(path), directory, entry->d_name) || !dual_directory(path))
            continue;
        size_t length = strlen(entry->d_name);
        if (length >= 4u && strcmp(entry->d_name + length - 4u, ".app") == 0) {
            snprintf(output, output_size, "%s", path);
            found = true;
        } else {
            found = dual_find_macos_app(path, depth + 1u, output, output_size);
        }
    }
    closedir(stream);
    return found;
}

static bool dual_manifest_install_dir(const char* path, char* output, size_t output_size) {
    FILE* input = fopen(path, "rb");
    if (input == NULL)
        return false;
    char line[4096];
    bool found = false;
    while (fgets(line, sizeof(line), input) != NULL) {
        char* cursor = line;
        while (*cursor == ' ' || *cursor == '\t')
            cursor++;
        if (strncmp(cursor, "\"installdir\"", 12u) != 0)
            continue;
        cursor += 12u;
        while (*cursor == ' ' || *cursor == '\t')
            cursor++;
        if (*cursor == '"')
            cursor++;
        char* end = strchr(cursor, '"');
        if (end == NULL)
            end = cursor + strcspn(cursor, "\r\n");
        size_t length = (size_t)(end - cursor);
        if (length > 0u && length < output_size) {
            memcpy(output, cursor, length);
            output[length] = '\0';
            found = true;
        }
        break;
    }
    fclose(input);
    return found;
}

static void dual_classify(const char* directory, char* macos_dir, size_t macos_size, char* wine_dir, size_t wine_size) {
    if (wine_dir[0] == '\0' && dual_has_windows_exe(directory, 0u))
        snprintf(wine_dir, wine_size, "%s", directory);
    char app[PATH_MAX];
    if (macos_dir[0] == '\0' && dual_find_macos_app(directory, 0u, app, sizeof(app)))
        snprintf(macos_dir, macos_size, "%s", directory);
    if (wine_dir[0] == '\0' && macos_dir[0] == '\0')
        snprintf(macos_dir, macos_size, "%s", directory);
}

static MetalsharpResponse* handle_game_dual_info(const HttpRequest* req) {
    const char* value = req != NULL && req->query != NULL ? strstr(req->query, "appid=") : NULL;
    if (value == NULL)
        return launch_bad_request("appid required");
    value += 6;
    char* end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);
    if (end == value || parsed == 0u || parsed > 4294967295ul)
        return launch_bad_request("appid required");
    unsigned int appid = (unsigned int)parsed;
    const char* home = getenv("HOME");
    if (home == NULL)
        home = "";
    char manifest_name[64];
    snprintf(manifest_name, sizeof(manifest_name), "appmanifest_%u.acf", appid);
    const char* mac_roots[] = {"Library/Application Support/Steam/steamapps", ".steam/steam/steamapps",
                               ".local/share/Steam/steamapps"};
    char install_name[PATH_MAX] = "", macos_dir[PATH_MAX] = "", wine_dir[PATH_MAX] = "", macos_app[PATH_MAX] = "";
    for (size_t i = 0u; i < sizeof(mac_roots) / sizeof(mac_roots[0]); i++) {
        char steamapps[PATH_MAX], manifest[PATH_MAX];
        if (!dual_join(steamapps, sizeof(steamapps), home, mac_roots[i]) ||
            !dual_join(manifest, sizeof(manifest), steamapps, manifest_name))
            continue;
        char current_name[PATH_MAX];
        if (!dual_manifest_install_dir(manifest, current_name, sizeof(current_name)))
            continue;
        char common[PATH_MAX], game[PATH_MAX];
        if (dual_join(common, sizeof(common), steamapps, "common") &&
            dual_join(game, sizeof(game), common, current_name) && dual_directory(game)) {
            dual_classify(game, macos_dir, sizeof(macos_dir), wine_dir, sizeof(wine_dir));
            if (install_name[0] == '\0')
                snprintf(install_name, sizeof(install_name), "%s", current_name);
        }
    }
    char wine_steamapps[PATH_MAX], wine_manifest[PATH_MAX], current_name[PATH_MAX] = "";
    dual_join(wine_steamapps, sizeof(wine_steamapps), home,
              ".metalsharp/prefix-steam/drive_c/Program Files (x86)/Steam/steamapps");
    if (install_name[0] != '\0') {
        char common[PATH_MAX], game[PATH_MAX];
        if (dual_join(common, sizeof(common), wine_steamapps, "common") &&
            dual_join(game, sizeof(game), common, install_name) && dual_directory(game))
            dual_classify(game, macos_dir, sizeof(macos_dir), wine_dir, sizeof(wine_dir));
    } else if (dual_join(wine_manifest, sizeof(wine_manifest), wine_steamapps, manifest_name) &&
               dual_manifest_install_dir(wine_manifest, current_name, sizeof(current_name))) {
        char common[PATH_MAX], game[PATH_MAX];
        if (dual_join(common, sizeof(common), wine_steamapps, "common") &&
            dual_join(game, sizeof(game), common, current_name) && dual_directory(game))
            dual_classify(game, macos_dir, sizeof(macos_dir), wine_dir, sizeof(wine_dir));
    }
    if (macos_dir[0] != '\0')
        (void)dual_find_macos_app(macos_dir, 0u, macos_app, sizeof(macos_app));
    jsonbuf_t body = jsonbuf_new();
    if (!body.ok)
        return make_error_response("out of memory");
    char prefix[128];
    snprintf(prefix, sizeof(prefix), "{\"ok\":true,\"appid\":%u,\"has_native_build\":%s,\"macos_dir\":", appid,
             macos_app[0] != '\0' ? "true" : "false");
    jsonbuf_append(&body, prefix);
    jsonbuf_append_escaped(&body, macos_dir[0] != '\0' ? macos_dir : NULL);
    jsonbuf_append(&body, ",\"macos_app\":");
    jsonbuf_append_escaped(&body, macos_app[0] != '\0' ? macos_app : NULL);
    jsonbuf_append(&body, ",\"wine_dir\":");
    jsonbuf_append_escaped(&body, wine_dir[0] != '\0' ? wine_dir : NULL);
    jsonbuf_append(&body, "}");
    MetalsharpResponse* response = body.ok ? make_data_response(body.data) : make_error_response("out of memory");
    jsonbuf_free(&body);
    return response;
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
    char body[160];
    int n = snprintf(body, sizeof(body),
                     "{\"ok\":true,\"backendPid\":%ld,\"terminated\":[],\"killed\":[],\"errors\":[]}", (long)getpid());
    if (n < 0 || (size_t)n >= sizeof(body))
        return NULL;
    return make_data_response(body);
}

/* ── /kill ── */

static void launch_run_quiet_command(char* const argv[]) {
    pid_t pid = fork();
    if (pid < 0)
        return;
    if (pid == 0) {
        int null_fd = open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            (void)dup2(null_fd, STDOUT_FILENO);
            (void)dup2(null_fd, STDERR_FILENO);
            if (null_fd > STDERR_FILENO)
                close(null_fd);
        }
        execvp(argv[0], argv);
        _exit(127);
    }
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
    }
}

static void launch_kill_process_tree(long pid) {
    if (pid <= 0)
        return;
    char pid_text[32];
    snprintf(pid_text, sizeof(pid_text), "%ld", pid);
    char* pkill_children[] = {"pkill", "-9", "-P", pid_text, NULL};
    launch_run_quiet_command(pkill_children);
    char* kill_parent[] = {"kill", "-9", pid_text, NULL};
    launch_run_quiet_command(kill_parent);
    usleep(300000u);
    char* kill_crash_handler[] = {"pkill", "-9", "-f", "UnityCrashHandler", NULL};
    launch_run_quiet_command(kill_crash_handler);
}

/*
 * POST /kill — terminate the game process identified by `pid`.
 * The historical implementation first kills direct children, then the
 * requested process, waits briefly, and finally removes Unity crash helpers.
 * A missing or already-exited process remains an idempotent success.
 */
static MetalsharpResponse* handle_kill(const HttpRequest* req) {
    long pid = 0;
    bool has_appid = false;
    unsigned int appid = 0u;
    if (req != NULL && req->body != NULL) {
        JsonValue* body = json_parse(req->body, req->body_len, NULL);
        if (body != NULL && json_type(body) == JSON_OBJECT) {
            pid = (long)json_get_number(json_object_get(body, "pid"), 0.0);
            JsonValue* appid_value = json_object_get(body, "appid");
            if (appid_value != NULL && json_type(appid_value) == JSON_NUMBER) {
                has_appid = true;
                appid = (unsigned int)json_get_number(appid_value, 0.0);
            }
        }
        json_free(body);
    }
    if (pid <= 0 && !has_appid)
        return launch_bad_request("pid required");
    /* Running-game PID registration is populated by launch-auto once the MTSP
     * process handoff is active. Until then the request PID is the same fallback
     * used by the Rust backend when an appid has no registered process. */
    long target_pid = pid;
    launch_kill_process_tree(target_pid);
    if (has_appid)
        metalsharp_app_log("[STOPPED] appid %u | pid %ld", appid, target_pid);
    else
        metalsharp_app_log("[STOPPED] pid %ld", target_pid);
    char response[96];
    int n = snprintf(response, sizeof(response), "{\"ok\":true,\"pid\":%ld}", target_pid);
    if (n < 0 || (size_t)n >= sizeof(response))
        return make_error_response("internal error");
    return make_data_response(response);
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
    http_server_register(server, "POST", "/launch", handle_launch);

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
