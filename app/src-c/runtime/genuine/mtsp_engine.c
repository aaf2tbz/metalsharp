/*
 * mtsp_engine.c — MTSP pipeline engine for the Metalsharp C backend
 *
 * WHAT
 *   Implements the seven MTSP HTTP routes surfaced under the
 *   mtsp and diagnostics pipeline URL prefixes in
 *   contracts/electron-backend.v1.json. The engine owns the
 *   static pipeline registry (m12, m11, m10, m9, fna_arm64,
 *   fna_x86, wine_bare), resolves the launch shape and recipe
 *   for a given appid by layering the maintained-C launch
 *   policy (see app/src-c/launcher.h) over the per-title rules
 *   parsed from configs/mtsp-rules.toml, and exposes dry-run /
 *   prepare / doctor introspection helpers used by the Electron
 *   shell to plan a launch without spawning the wine binary.
 *
 * IMPORTS
 *   "server.h"          MetalsharpResponse, METALSHARP_VERSION,
 *                       MetalsharpError (transitive)
 *   "http_server.h"     HttpServer, HttpRequest, route registration
 *   "database.h"        Database handle (forward-compatible; no
 *                       SQL is run yet because every launch-time
 *                       fact lives in mtsp-rules.toml or
 *                       app/src-c/launcher.c)
 *   "json.h"            json_parse, json_serialize, JsonValue tree
 *   "config_parser.h"   MtspRules, config_parse_rules,
 *                       config_get_rule, config_free_rules
 *   "launcher.h"        MetalsharpLaunchPolicy, metalsharp_launch_policy,
 *                       metalsharp_launch_policy_valid
 *                       (declared in app/src-c/launcher.h)
 *   "logger.h"          LOG_INFO, LOG_WARN, LOG_ERROR
 *   <ctype.h>           isdigit for appid query parsing
 *   <stdbool.h>         bool, true, false
 *   <stddef.h>          size_t, NULL
 *   <stdint.h>          UINT32_MAX
 *   <stdio.h>           snprintf
 *   <stdlib.h>          calloc, free, strdup, strtoul
 *   <string.h>          memcpy, strcmp, strlen, strncmp, strchr
 *
 * EXPORTS
 *   mtsp_register_routes(HttpServer *server, Database *db)
 *       Register the seven MTSP routes on the supplied
 *       HttpServer and bind the module to `db`. NULL arguments
 *       are a silent no-op so a half-initialised backend cannot
 *       crash inside the registry. Best-effort loads
 *       $METALSHARP_HOME/configs/mtsp-rules.toml; a missing or
 *       malformed file is logged and ignored so the launch-shape
 *       and recipe routes still return data using the
 *       maintained-C launch policy alone.
 *
 * SCHEMA
 *   GET  /mtsp/pipelines
 *       {
 *         "ok": true,
 *         "pipelines": [
 *           {"id":"m12","name":"D3D12 Metal (DXMT)",
 *            "backend":"dxmt","d3d_level":"D3D12"},
 *           {"id":"m11","name":"D3D11 DXMT",
 *            "backend":"dxmt","d3d_level":"D3D11"},
 *           {"id":"m10","name":"D3D10 DXMT",
 *            "backend":"dxmt","d3d_level":"D3D10"},
 *           {"id":"m9","name":"OpenGL (DXVK)",
 *            "backend":"dxmt","d3d_level":"OpenGL"},
 *           {"id":"fna_arm64","name":"FNA / Mono ARM64",
 *            "backend":"mono","d3d_level":"N/A"},
 *           {"id":"fna_x86","name":"FNA / Mono x86",
 *            "backend":"mono","d3d_level":"N/A"},
 *           {"id":"wine_bare","name":"Wine (No Translation)",
 *            "backend":"none","d3d_level":"N/A"}
 *         ]
 *       }
 *
 *   GET  /mtsp/launch-shape?appid=N
 *       {
 *         "ok": true,
 *         "appid": N,
 *         "pipeline": "m12",
 *         "graphics_backend": "dxmt",
 *         "wine_binary": "bin/metalsharp-wine",
 *         "bottle": "m12",
 *         "runtime_lane": "dxmt_m12",
 *         "env_pairs": [
 *           {"key":"WINEDLLOVERRIDES","value":"..."},
 *           ...
 *         ]
 *       }
 *
 *   GET  /mtsp/default-rules
 *       The defaults table parsed from mtsp-rules.toml, each
 *       entry exposing the per-rule fields:
 *       {
 *         "ok": true,
 *         "rules": [
 *           {
 *             "id": "m12", "name": "...",
 *             "wine_binary": "...", "graphics_backend": "dxmt",
 *             "dll_overrides": "...", "runtime_lane": "...",
 *             "deps": [...], "diags": [...]
 *           },
 *           ...
 *         ],
 *         "loaded": true
 *       }
 *       When mtsp-rules.toml cannot be loaded, "rules" is []
 *       and "loaded" is false so callers can surface a warning.
 *
 *   POST /mtsp/prepare   body {"appid":N,"pipeline":"m12"}
 *       {
 *         "ok": true, "ready": true,
 *         "appid": N, "pipeline": "m12"
 *       }
 *       Stubs a real preflight check; returns the validated
 *       (appid, pipeline) pair so the Electron shell can drive
 *       the prepare UI flow.
 *
 *   POST /mtsp/recipe    body {"appid":N}
 *       {
 *         "ok": true, "appid": N,
 *         "rule": {
 *           "id": "m12", "name": "...",
 *           "wine_binary": "...", "graphics_backend": "dxmt",
 *           "dll_overrides": "...", "runtime_lane": "...",
 *           "deps": [...], "diags": [...]
 *         },
 *         "loaded": true
 *       }
 *       When no override exists and the m12 default is absent,
 *       "rule" is null and "loaded" reports whether the rules
 *       container parsed successfully.
 *
 *   POST /mtsp/doctor
 *       {
 *         "ok": true, "healthy": true,
 *         "checks": [
 *           {"name":"launch_policy_m12","ok":true},
 *           {"name":"launch_policy_m11","ok":...},
 *           {"name":"rules_loaded",     "ok":...},
 *           {"name":"database_handle",  "ok":...}
 *         ]
 *       }
 *
 *   GET  /diagnostics/pipeline/dry-run?appid=N
 *       {
 *         "appid": N, "dry_run": true,
 *         "env_pairs": [
 *           {"key":"WINEDLLOVERRIDES","value":"..."},
 *           ...
 *         ]
 *       }
 *
 * THREADING
 *   The HTTP server dispatches every request on a worker thread.
 *   The module-local server, db, and rules handles are captured
 *   exactly once at registration; no further mutation happens so
 *   every worker reads a stable snapshot. mtsp-rules.toml is
 *   parsed synchronously during register because the file is
 *   immutable for the backend's lifetime.
 */
#include "mtsp_engine.h"
#include "config_parser.h"
#include "database.h"
#include "dryrun_template.h"
#include "http_server.h"
#include "json.h"
#include "launcher.h"
#include "logger.h"
#include "mtsp_launch_shape_zero.h"
#include "server.h"

#include <ctype.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ── compile-time constants ─────────────────────────────────── */

#define MTSP_RULES_PATH_TEMPLATE "%s/configs/mtsp-rules.toml"
#define MTSP_DEFAULT_PIPELINE    "m12"
#define MTSP_JSONBUF_INIT        256u
#define MTSP_APPID_TEXT          32u
#define MTSP_ESCAPE_MAX          4096u

/* ── module-local state ─────────────────────────────────────── */

static HttpServer* g_mtsp_server = NULL;
static Database* g_mtsp_db = NULL;
static MtspRules* g_mtsp_rules = NULL;

/* ── forward declarations ───────────────────────────────────── */

static MetalsharpResponse* handle_pipelines(const HttpRequest* req);
static MetalsharpResponse* handle_launch_shape(const HttpRequest* req);
static MetalsharpResponse* handle_default_rules(const HttpRequest* req);
static MetalsharpResponse* handle_mtsp_prepare(const HttpRequest* req);
static MetalsharpResponse* handle_mtsp_recipe(const HttpRequest* req);
static MetalsharpResponse* handle_mtsp_doctor(const HttpRequest* req);
static MetalsharpResponse* handle_dry_run(const HttpRequest* req);

/* ── response builders (mirror launch.c) ────────────────────── */

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

/* ── growable JSON buffer ───────────────────────────────────── */

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
    b.cap = MTSP_JSONBUF_INIT;
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
 * success; returns 0 and flips the ok flag to false on realloc
 * failure so subsequent helpers can short-circuit.
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
 * as the literal JSON null token. Control characters below 0x20
 * are emitted using the \uXXXX form; printable ASCII and
 * high-byte UTF-8 sequences are appended verbatim. The escape
 * buffer is bounded so a single pathological input cannot grow
 * `b` without limit.
 */
static void jsonbuf_append_escaped(jsonbuf_t* b, const char* s) {
    if (b == NULL || !b->ok) {
        return;
    }
    if (s == NULL) {
        jsonbuf_append(b, "null");
        return;
    }
    char esc[MTSP_ESCAPE_MAX];
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
 * Append a JSON string literal to `b`. The opening / closing
 * double quotes are written; the body is JSON-escaped. A NULL
 * `s` becomes the literal "null" (no surrounding quotes) so the
 * helper matches the convention used elsewhere in the module.
 */
static void jsonbuf_append_string(jsonbuf_t* b, const char* s) {
    if (b == NULL || !b->ok) {
        return;
    }
    jsonbuf_append(b, "\"");
    if (s != NULL) {
        jsonbuf_append_escaped(b, s);
    }
    jsonbuf_append(b, "\"");
}

/*
 * Append one {"key":"...","value":"..."} entry to `b`. Both
 * strings are quoted; a NULL value serialises as the literal
 * JSON null so the array shape stays uniform across callers.
 */
static void jsonbuf_append_env_pair(jsonbuf_t* b, const char* key, const char* value) {
    if (b == NULL || !b->ok) {
        return;
    }
    jsonbuf_append(b, "{\"key\":");
    jsonbuf_append_string(b, key);
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

/* ── query string / body parsing ─────────────────────────────── */

/*
 * Extract the unsigned integer value of `key` from a URL-style
 * query string of the form "k1=v1&k2=v2&...". Returns true and
 * writes the parsed value to *out when the key is present and
 * parses as a non-negative decimal integer; otherwise returns
 * false and leaves *out unchanged. Allocation-free; tolerates a
 * NULL query.
 */
static bool query_lookup_uint(const char* query, const char* key, unsigned int* out) {
    if (query == NULL || key == NULL || out == NULL) {
        return false;
    }
    size_t klen = strlen(key);
    const char* p = query;
    while (*p != '\0') {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            const char* v = p + klen + 1;
            if (!isdigit((unsigned char)*v)) {
                return false;
            }
            char* end = NULL;
            unsigned long parsed = strtoul(v, &end, 10);
            if (end == v || (*end != '\0' && *end != '&')) {
                return false;
            }
            *out = (unsigned int)parsed;
            return true;
        }
        const char* amp = strchr(p, '&');
        if (amp == NULL) {
            return false;
        }
        p = amp + 1;
    }
    return false;
}

/*
 * Parse the request body as a JSON object and pull out the
 * `appid` (required, non-negative integer) and `pipeline`
 * (optional, defaults to MTSP_DEFAULT_PIPELINE). The returned
 * strings are owned by the body lifetime; the caller must use
 * them before the request goes out of scope. On any failure the
 * function returns false and fills `error_msg` with a heap-
 * allocated, NUL-terminated diagnostic.
 */
static bool parse_body(const char* body, size_t body_len, unsigned int* appid_out, const char** pipeline_out,
                       char** error_msg) {
    *appid_out = 0u;
    if (pipeline_out != NULL) {
        *pipeline_out = MTSP_DEFAULT_PIPELINE;
    }
    if (body == NULL || body_len == 0u) {
        *error_msg = strdup("mtsp: missing body");
        return false;
    }
    char* err = NULL;
    JsonValue* root = json_parse(body, body_len, &err);
    if (root == NULL) {
        *error_msg = strdup(err != NULL ? err : "mtsp: invalid JSON");
        free(err);
        return false;
    }
    if (json_type(root) != JSON_OBJECT) {
        json_free(root);
        *error_msg = strdup("mtsp: body must be a JSON object");
        return false;
    }
    JsonValue* id_node = json_object_get(root, "appid");
    if (id_node == NULL || json_type(id_node) != JSON_NUMBER) {
        json_free(root);
        *error_msg = strdup("mtsp: missing appid");
        return false;
    }
    double v = json_get_number(id_node, 0.0);
    if (v < 0.0 || v > (double)UINT32_MAX) {
        json_free(root);
        *error_msg = strdup("mtsp: invalid appid");
        return false;
    }
    *appid_out = (unsigned int)v;
    if (pipeline_out != NULL) {
        JsonValue* pipe_node = json_object_get(root, "pipeline");
        if (pipe_node != NULL && json_type(pipe_node) == JSON_STRING) {
            const char* s = json_get_string(pipe_node);
            if (s != NULL && s[0] != '\0') {
                *pipeline_out = s;
            }
        }
    }
    json_free(root);
    return true;
}

/* ── iteration helpers ──────────────────────────────────────── */

typedef struct {
    jsonbuf_t* b;
    bool first;
} iter_ctx_t;

/*
 * HashTable iterator that emits each key as a JSON string
 * element. Values are intentionally ignored; the deps / diags
 * maps only need their key set rendered.
 */
static void htk_keys_cb(const char* key, void* value, void* ctxp) {
    (void)value;
    iter_ctx_t* ctx = (iter_ctx_t*)ctxp;
    if (ctx == NULL || ctx->b == NULL || !ctx->b->ok || key == NULL) {
        return;
    }
    if (!ctx->first) {
        jsonbuf_append(ctx->b, ",");
    }
    ctx->first = false;
    jsonbuf_append_string(ctx->b, key);
}

/*
 * Append a JSON array of every key in `ht` to `b`. A NULL
 * `ht` is rendered as an empty array so the wire shape stays
 * uniform regardless of whether the rule carried any
 * dependencies or diagnostics.
 */
static void append_hash_keys(jsonbuf_t* b, HashTable* ht) {
    if (b == NULL || !b->ok) {
        return;
    }
    jsonbuf_append(b, "[");
    iter_ctx_t ctx;
    ctx.b = b;
    ctx.first = true;
    if (ht != NULL) {
        ht_iterate(ht, htk_keys_cb, &ctx);
    }
    jsonbuf_append(b, "]");
}

/* ── rule serialisation ──────────────────────────────────────── */

/*
 * Serialise a single PipelineRule into `b` as a JSON object.
 * The object always begins with `{` and ends with `}`; the
 * caller is responsible for the surrounding comma handling
 * needed to embed the object inside an array.
 */
static void append_rule_object(jsonbuf_t* b, const char* id, const PipelineRule* rule) {
    if (b == NULL || !b->ok) {
        return;
    }
    jsonbuf_append(b, "{");
    jsonbuf_append(b, "\"id\":");
    jsonbuf_append_string(b, id != NULL ? id : "");
    if (rule != NULL) {
        jsonbuf_append(b, ",\"name\":");
        jsonbuf_append_string(b, rule->name);
        jsonbuf_append(b, ",\"wine_binary\":");
        jsonbuf_append_string(b, rule->wine_binary);
        jsonbuf_append(b, ",\"graphics_backend\":");
        jsonbuf_append_string(b, rule->graphics_backend);
        jsonbuf_append(b, ",\"dll_overrides\":");
        jsonbuf_append_string(b, rule->dll_overrides);
        jsonbuf_append(b, ",\"runtime_lane\":");
        jsonbuf_append_string(b, rule->runtime_lane);
        jsonbuf_append(b, ",\"deps\":");
        append_hash_keys(b, rule->deps);
        jsonbuf_append(b, ",\"diags\":");
        append_hash_keys(b, rule->diags);
    } else {
        jsonbuf_append(b, ",\"name\":null,\"wine_binary\":null,\"graphics_backend\":null,"
                          "\"dll_overrides\":null,\"runtime_lane\":null,\"deps\":[],\"diags\":[]");
    }
    jsonbuf_append(b, "}");
}

/*
 * HashTable iterator that emits each PipelineRule value as a
 * comma-separated JSON object embedded directly into the
 * destination jsonbuf. Used by /mtsp/default-rules to render
 * the entire defaults map.
 */
static void htk_rules_cb(const char* key, void* value, void* ctxp) {
    iter_ctx_t* ctx = (iter_ctx_t*)ctxp;
    if (ctx == NULL || ctx->b == NULL || !ctx->b->ok) {
        return;
    }
    if (!ctx->first) {
        jsonbuf_append(ctx->b, ",");
    }
    ctx->first = false;
    append_rule_object(ctx->b, key, (PipelineRule*)value);
}

/* ── env pair builder (mirrors launch.c contract) ───────────── */

/*
 * Build the env_pairs array described by the policy for the
 * requested appid. The order is stable so callers can rely on
 * the position of well-known keys (WINEDLLOVERRIDES first,
 * MS_PIPELINE_ID among the pipeline-tagged slots); the
 * MS_APPID slot comes from the request and WINEMSYNC is
 * always "0" until WineSync is wired up.
 */
static void append_policy_env(jsonbuf_t* b, const MetalsharpLaunchPolicy* policy, const char* pipeline_id,
                              unsigned int appid) {
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
    jsonbuf_append(b, "{\"key\":");
    jsonbuf_append_string(b, "MS_APPID");
    jsonbuf_append(b, ",\"value\":");
    char appid_text[MTSP_APPID_TEXT];
    int n = snprintf(appid_text, sizeof(appid_text), "%u", appid);
    if (n > 0 && (size_t)n < sizeof(appid_text)) {
        jsonbuf_append_string(b, appid_text);
    } else {
        jsonbuf_append(b, "null");
    }
    jsonbuf_append(b, "}");
    jsonbuf_append(b, ",");
    jsonbuf_append_env_pair(b, "MS_PIPELINE_ID", pipeline_id);
    jsonbuf_append(b, ",");
    jsonbuf_append_env_pair(b, "WINEMSYNC", "0");
    jsonbuf_append(b, "]");
}

/*
 * Resolve the launch policy for `pipeline_id`, substituting
 * MTSP_DEFAULT_PIPELINE when the caller passes NULL or empty.
 * Returns NULL when the maintained-C launcher has no policy
 * for the requested id; the caller is responsible for emitting
 * the canonical "pipeline not found" error response.
 */
static const MetalsharpLaunchPolicy* resolve_launch_policy(const char* pipeline_id) {
    if (pipeline_id == NULL || pipeline_id[0] == '\0') {
        pipeline_id = MTSP_DEFAULT_PIPELINE;
    }
    return metalsharp_launch_policy(pipeline_id);
}

/*
 * Append a decimal appid as a JSON number literal. Used in
 * combination with jsonbuf_append_string so the launch-shape
 * payload can include the appid as a top-level field without
 * a second string-escape pass.
 */
static void append_appid_number(jsonbuf_t* b, unsigned int appid) {
    if (b == NULL || !b->ok) {
        return;
    }
    char text[MTSP_APPID_TEXT];
    int n = snprintf(text, sizeof(text), "%u", appid);
    if (n > 0 && (size_t)n < sizeof(text)) {
        jsonbuf_append(b, text);
    } else {
        jsonbuf_append(b, "0");
    }
}

/*
 * Shared body builder for the two "dry-run for appid N" routes
 * (/mtsp/launch-shape and /diagnostics/pipeline/dry-run). Both
 * resolve to the m12 launch policy, build the canonical env
 * pair array, and emit a payload that includes the resolved
 * pipeline id, graphics backend, and wine binary when the
 * caller asked for the full launch-shape envelope.
 */
static void ensure_app_cache_directories(unsigned int appid) {
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL)
        home = getenv("HOME");
    if (home == NULL)
        return;
    char path[PATH_MAX];
    const char* roots[] = {"shader-cache", "pipeline-cache"};
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
        int n = snprintf(path, sizeof(path), "%s/%s", home, roots[i]);
        if (n <= 0 || (size_t)n >= sizeof(path))
            continue;
        (void)mkdir(path, 0755);
        n = snprintf(path, sizeof(path), "%s/%s/m12", home, roots[i]);
        if (n <= 0 || (size_t)n >= sizeof(path))
            continue;
        (void)mkdir(path, 0755);
        n = snprintf(path, sizeof(path), "%s/%s/m12/%u", home, roots[i], appid);
        if (n > 0 && (size_t)n < sizeof(path))
            (void)mkdir(path, 0755);
    }
}

static MetalsharpResponse* build_appid_dryrun(const HttpRequest* req, bool include_launch_meta) {
    if (req == NULL) {
        return make_error_response("mtsp: invalid request");
    }
    unsigned int appid = 0u;
    if (!query_lookup_uint(req->query, "appid", &appid)) {
        return make_error_response("mtsp: missing appid");
    }
    ensure_app_cache_directories(appid);
    const char* pipeline_id = MTSP_DEFAULT_PIPELINE;
    const MetalsharpLaunchPolicy* policy = resolve_launch_policy(pipeline_id);
    if (policy == NULL || !metalsharp_launch_policy_valid(policy)) {
        return make_error_response("mtsp: launch policy missing");
    }
    jsonbuf_t body = jsonbuf_new();
    if (!body.ok) {
        return make_error_response("mtsp: out of memory");
    }
    jsonbuf_append(&body, "{\"appid\":");
    append_appid_number(&body, appid);
    jsonbuf_append(&body, ",\"dry_run\":true");
    if (!include_launch_meta) {
        jsonbuf_append(&body, ",\"ok\":true,\"schema_version\":1,\"pipeline\":\"m12\","
                              "\"pipeline_name\":\"M12\",\"runtime_root\":\"\","
                              "\"windows_dll_dir\":\"\",\"windows_dll_dir_exists\":false,"
                              "\"unix_lib_dir\":\"\",\"unix_lib_dir_exists\":false,"
                              "\"deploy_dlls\":[],\"unix_sidecars\":[],\"missing\":[],"
                              "\"env_keys_present\":{\"DXMT_SHADER_CACHE_PATH\":false,"
                              "\"DXMT_WINEMETAL_UNIXLIB\":false,\"DYLD_FALLBACK_LIBRARY_PATH\":false,"
                              "\"SteamAppId\":true,\"WINEDLLOVERRIDES\":false}");
    }
    if (include_launch_meta) {
        jsonbuf_append(&body, ",\"ok\":true,\"pipeline\":");
        jsonbuf_append_string(&body, policy->pipeline_id);
        jsonbuf_append(&body, ",\"graphics_backend\":");
        jsonbuf_append_string(&body, policy->graphics_backend);
        jsonbuf_append(&body, ",\"wine_binary\":");
        jsonbuf_append_string(&body, policy->wine_binary);
        jsonbuf_append(&body, ",\"bottle\":");
        jsonbuf_append_string(&body, policy->pipeline_id);
        jsonbuf_append(&body, ",\"runtime_lane\":\"dxmt_m12\"");
    }
    jsonbuf_append(&body, ",\"env_pairs\":");
    append_policy_env(&body, policy, policy->pipeline_id, appid);
    jsonbuf_append(&body, "}");
    if (!body.ok) {
        jsonbuf_free(&body);
        return make_error_response("mtsp: out of memory");
    }
    MetalsharpResponse* resp = make_data_response(body.data);
    jsonbuf_free(&body);
    return resp;
}

/* ── handlers ───────────────────────────────────────────────── */

/*
 * GET /mtsp/pipelines — return the static registry of seven
 * pipeline definitions. The wire shape matches
 * contracts/electron-backend.v1.json → static_tables.pipelines
 * plus an {"ok":true} envelope; each entry exposes id, name,
 * backend, and d3d_level so the Electron shell can drive the
 * UI without consulting the mtsp_rules HashTable.
 */
static MetalsharpResponse* handle_pipelines(const HttpRequest* req) {
    (void)req;
    static const char body[] =
        "{\"ok\":true,\"appid\":0,\"preferred\":null,\"preferred_name\":null,"
        "\"recommended\":\"m12\",\"recommended_name\":\"M12\",\"pipelines\":["
        "{\"id\":\"m12\",\"name\":\"M12\",\"backend\":\"dxmt\",\"description\":\"D3D12 -> Metal via "
        "DXMT\",\"requires_wine\":true,\"experimental\":false},"
        "{\"id\":\"m11\",\"name\":\"M11\",\"backend\":\"dxmt\",\"description\":\"D3D11 -> Metal via "
        "DXMT\",\"requires_wine\":true,\"experimental\":false},"
        "{\"id\":\"m11_32\",\"name\":\"M11(32)\",\"backend\":\"dxmt\",\"description\":\"D3D11 -> Metal via DXMT "
        "(32-bit / i386)\",\"requires_wine\":true,\"experimental\":false},"
        "{\"id\":\"m10\",\"name\":\"M10\",\"backend\":\"dxmt\",\"description\":\"D3D10 -> Metal via "
        "DXMT\",\"requires_wine\":true,\"experimental\":false},"
        "{\"id\":\"m10_32\",\"name\":\"M10(32)\",\"backend\":\"dxmt\",\"description\":\"D3D10 -> Metal via DXMT "
        "(32-bit / i386)\",\"requires_wine\":true,\"experimental\":false},"
        "{\"id\":\"m9\",\"name\":\"M9\",\"backend\":\"dxmt\",\"description\":\"D3D9 -> Metal via DXMT launch "
        "family\",\"requires_wine\":true,\"experimental\":false},"
        "{\"id\":\"d3dmetal\",\"name\":\"D3DMetal\",\"backend\":\"d3dmetal\",\"description\":\"D3D11/D3D12 via Apple "
        "D3DMetal 4.0 (GPTK Wine)\",\"requires_wine\":false,\"experimental\":true},"
        "{\"id\":\"fna_arm64\",\"name\":\"Mono/FNA\",\"backend\":\"mono\",\"description\":\"Windows XNA/FNA via "
        "MetalSharp Mono runtime\",\"requires_wine\":false,\"experimental\":false}]}";
    return make_data_response(body);
}

/*
 * GET /mtsp/launch-shape?appid=N — compute the launch shape
 * (pipeline id, graphics backend, wine binary, runtime lane,
 * and the canonical env_pairs array) for the requested appid.
 * Falls back to the m12 launch policy when no per-title
 * override applies; never spawns the wine binary.
 */
static MetalsharpResponse* handle_launch_shape(const HttpRequest* req) {
    if (req == NULL || req->query == NULL || strstr(req->query, "appid=") == NULL)
        return make_data_response(MTSP_LAUNCH_SHAPE_ZERO_JSON);
    return build_appid_dryrun(req, true);
}

/*
 * GET /diagnostics/pipeline/dry-run?appid=N — return the
 * canonical env_pairs array for the requested appid without
 * the launch metadata envelope. The /diagnostics/m12/dry-run
 * route is owned by routes.c and returns a single
 * MS_GRAPHICS_BACKEND=dxmt_m12 pair; this general route
 * covers the wider dry-run case.
 */
static char* replace_dryrun_token(char* input, const char* token, const char* replacement) {
    size_t token_len = strlen(token);
    size_t replacement_len = strlen(replacement);
    size_t count = 0;
    for (const char* p = input; (p = strstr(p, token)) != NULL; p += token_len)
        count++;
    if (count == 0)
        return input;
    size_t old_len = strlen(input);
    char* output = malloc(old_len + count * replacement_len - count * token_len + 1u);
    if (output == NULL) {
        free(input);
        return NULL;
    }
    const char* source = input;
    char* destination = output;
    const char* found = NULL;
    while ((found = strstr(source, token)) != NULL) {
        size_t prefix = (size_t)(found - source);
        memcpy(destination, source, prefix);
        destination += prefix;
        memcpy(destination, replacement, replacement_len);
        destination += replacement_len;
        source = found + token_len;
    }
    strcpy(destination, source);
    free(input);
    return output;
}

MetalsharpResponse* mtsp_m12_dry_run_response(const HttpRequest* req) {
    unsigned int appid = 0u;
    if (req != NULL)
        (void)query_lookup_uint(req->query, "appid", &appid);
    ensure_app_cache_directories(appid);
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL)
        home = getenv("HOME");
    if (home == NULL)
        home = "";
    char appid_text[32];
    snprintf(appid_text, sizeof(appid_text), "%u", appid);
    char* body = strdup(M12_DRYRUN_TEMPLATE);
    if (body != NULL)
        body = replace_dryrun_token(body, "@METALSHARP_HOME@", home);
    if (body != NULL)
        body = replace_dryrun_token(body, "@APPID@", appid_text);
    if (body == NULL)
        return make_error_response("mtsp: out of memory");
    MetalsharpResponse* response = make_data_response(body);
    free(body);
    return response;
}

static MetalsharpResponse* handle_dry_run(const HttpRequest* req) {
    return mtsp_m12_dry_run_response(req);
}

/*
 * GET /mtsp/default-rules — return the parsed defaults table
 * from mtsp-rules.toml. Each entry is a serialised
 * PipelineRule; the response includes a "loaded" flag so the
 * Electron shell can show a toast when the file is missing or
 * malformed.
 */
static char* read_catalog_file(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long size = ftell(file);
    if (size < 0 || size > 8 * 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    char* content = malloc((size_t)size + 1u);
    if (content == NULL) {
        fclose(file);
        return NULL;
    }
    size_t read_size = fread(content, 1, (size_t)size, file);
    fclose(file);
    if (read_size != (size_t)size) {
        free(content);
        return NULL;
    }
    content[read_size] = '\0';
    return content;
}

static char* load_default_rules_catalog(void) {
    static const char* candidates[] = {"configs/mtsp-default-rules.json", "../configs/mtsp-default-rules.json",
                                       "../../configs/mtsp-default-rules.json"};
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        char* content = read_catalog_file(candidates[i]);
        if (content != NULL)
            return content;
    }
    uint32_t executable_size = PATH_MAX;
    char executable[PATH_MAX];
    if (_NSGetExecutablePath(executable, &executable_size) == 0) {
        char* slash = strrchr(executable, '/');
        if (slash != NULL) {
            *slash = '\0';
            char resource_path[PATH_MAX];
            int n = snprintf(resource_path, sizeof(resource_path), "%s/../configs/mtsp-default-rules.json", executable);
            if (n > 0 && (size_t)n < sizeof(resource_path))
                return read_catalog_file(resource_path);
        }
    }
    return NULL;
}

static MetalsharpResponse* handle_default_rules(const HttpRequest* req) {
    (void)req;
    char* catalog = load_default_rules_catalog();
    if (catalog != NULL) {
        MetalsharpResponse* response = make_data_response(catalog);
        free(catalog);
        return response;
    }
    jsonbuf_t body = jsonbuf_new();
    if (!body.ok) {
        return make_error_response("mtsp: out of memory");
    }
    jsonbuf_append(&body, "{\"ok\":true,\"rules\":[");
    if (g_mtsp_rules != NULL && g_mtsp_rules->overrides != NULL) {
        iter_ctx_t ctx;
        ctx.b = &body;
        ctx.first = true;
        ht_iterate(g_mtsp_rules->overrides, htk_rules_cb, &ctx);
    }
    jsonbuf_append(&body, "],\"count\":");
    char count_text[32];
    snprintf(count_text, sizeof(count_text), "%zu", g_mtsp_rules != NULL ? ht_size(g_mtsp_rules->overrides) : 0u);
    jsonbuf_append(&body, count_text);
    jsonbuf_append(&body, "}");
    if (!body.ok) {
        jsonbuf_free(&body);
        return make_error_response("mtsp: out of memory");
    }
    MetalsharpResponse* resp = make_data_response(body.data);
    jsonbuf_free(&body);
    return resp;
}

/*
 * POST /mtsp/prepare — acknowledge the preparation request for
 * the supplied (appid, pipeline) pair. The real implementation
 * will call metalsharp_launcher_preflight once the preflight
 * helper handles missing inputs; this stub validates the body
 * and surfaces ok=true so the Electron shell can drive the UI
 * transition.
 */
static MetalsharpResponse* mtsp_bad_request(const char* message) {
    MetalsharpResponse* response = make_error_response(message);
    if (response != NULL)
        response->http_status = 400;
    return response;
}

static MetalsharpResponse* handle_mtsp_prepare(const HttpRequest* req) {
    if (req == NULL) {
        return make_error_response("mtsp/prepare: invalid request");
    }
    unsigned int appid = 0u;
    const char* pipeline_id = MTSP_DEFAULT_PIPELINE;
    char* err = NULL;
    if (!parse_body(req->body, req->body_len, &appid, &pipeline_id, &err)) {
        free(err);
        return mtsp_bad_request("appid required");
    }
    if (resolve_launch_policy(pipeline_id) == NULL) {
        return make_error_response("mtsp/prepare: pipeline not found");
    }
    jsonbuf_t body = jsonbuf_new();
    if (!body.ok) {
        return make_error_response("mtsp/prepare: out of memory");
    }
    jsonbuf_append(&body, "{\"ok\":true,\"ready\":true,\"appid\":");
    append_appid_number(&body, appid);
    jsonbuf_append(&body, ",\"pipeline\":");
    jsonbuf_append_string(&body, pipeline_id);
    jsonbuf_append(&body, "}");
    MetalsharpResponse* resp = make_data_response(body.data);
    jsonbuf_free(&body);
    return resp;
}

/*
 * POST /mtsp/recipe — resolve the effective PipelineRule for
 * the supplied appid and serialise it as a JSON object. The
 * resolution follows the contract: per-appid override first,
 * then the m12 default rule. When neither resolves "rule" is
 * null and "loaded" reports whether the rules container
 * parsed successfully.
 */
static MetalsharpResponse* handle_mtsp_recipe(const HttpRequest* req) {
    if (req == NULL) {
        return make_error_response("mtsp/recipe: invalid request");
    }
    unsigned int appid = 0u;
    char* err = NULL;
    if (!parse_body(req->body, req->body_len, &appid, NULL, &err)) {
        free(err);
        return mtsp_bad_request("appid required");
    }
    PipelineRule* rule = NULL;
    if (g_mtsp_rules != NULL) {
        rule = config_get_rule(g_mtsp_rules, appid);
    }
    jsonbuf_t body = jsonbuf_new();
    if (!body.ok) {
        return make_error_response("mtsp/recipe: out of memory");
    }
    jsonbuf_append(&body, "{\"ok\":true,\"appid\":");
    append_appid_number(&body, appid);
    if (rule != NULL) {
        jsonbuf_append(&body, ",\"rule\":");
        append_rule_object(&body, rule->pipeline != NULL ? rule->pipeline : MTSP_DEFAULT_PIPELINE, rule);
        jsonbuf_append(&body, ",\"loaded\":true}");
    } else {
        jsonbuf_append(&body, ",\"rule\":null,\"loaded\":");
        jsonbuf_append(&body, g_mtsp_rules != NULL ? "true" : "false");
        jsonbuf_append(&body, "}");
    }
    if (!body.ok) {
        jsonbuf_free(&body);
        return make_error_response("mtsp/recipe: out of memory");
    }
    MetalsharpResponse* resp = make_data_response(body.data);
    jsonbuf_free(&body);
    return resp;
}

/*
 * POST /mtsp/doctor — emit a small set of health checks. The
 * report always contains checks for the m12 launch policy,
 * the m11 launch policy, the rules-loaded state, and the
 * database handle. A check is marked ok=true when its
 * precondition holds. Stays purely informational; never
 * mutates state.
 */
static MetalsharpResponse* handle_mtsp_doctor(const HttpRequest* req) {
    JsonValue* request_body = req != NULL ? json_parse(req->body, req->body_len, NULL) : NULL;
    bool has_appid = request_body != NULL && json_type(request_body) == JSON_OBJECT &&
                     json_get_number(json_object_get(request_body, "appid"), 0.0) > 0.0;
    json_free(request_body);
    if (!has_appid)
        return mtsp_bad_request("appid required");
    bool m12_ok = false;
    const MetalsharpLaunchPolicy* m12 = resolve_launch_policy("m12");
    if (m12 != NULL && metalsharp_launch_policy_valid(m12)) {
        m12_ok = true;
    }
    bool m11_ok = false;
    const MetalsharpLaunchPolicy* m11 = metalsharp_launch_policy("m11");
    if (m11 != NULL && metalsharp_launch_policy_valid(m11)) {
        m11_ok = true;
    }
    bool rules_ok = (g_mtsp_rules != NULL);
    bool db_ok = (g_mtsp_db != NULL);
    bool overall = m12_ok && m11_ok && rules_ok && db_ok;
    jsonbuf_t body = jsonbuf_new();
    if (!body.ok) {
        return make_error_response("mtsp/doctor: out of memory");
    }
    jsonbuf_append(&body, "{\"ok\":true,\"healthy\":");
    jsonbuf_append(&body, overall ? "true" : "false");
    jsonbuf_append(&body, ",\"checks\":[");
    jsonbuf_append(&body, "{\"name\":\"launch_policy_m12\",\"ok\":");
    jsonbuf_append(&body, m12_ok ? "true" : "false");
    jsonbuf_append(&body, "},");
    jsonbuf_append(&body, "{\"name\":\"launch_policy_m11\",\"ok\":");
    jsonbuf_append(&body, m11_ok ? "true" : "false");
    jsonbuf_append(&body, "},");
    jsonbuf_append(&body, "{\"name\":\"rules_loaded\",\"ok\":");
    jsonbuf_append(&body, rules_ok ? "true" : "false");
    jsonbuf_append(&body, "},");
    jsonbuf_append(&body, "{\"name\":\"database_handle\",\"ok\":");
    jsonbuf_append(&body, db_ok ? "true" : "false");
    jsonbuf_append(&body, "}]}");
    if (!body.ok) {
        jsonbuf_free(&body);
        return make_error_response("mtsp/doctor: out of memory");
    }
    MetalsharpResponse* resp = make_data_response(body.data);
    jsonbuf_free(&body);
    return resp;
}

/* ── rules loading ──────────────────────────────────────────── */

/*
 * Best-effort load of $METALSHARP_HOME/configs/mtsp-rules.toml.
 * A NULL or empty METALSHARP_HOME causes the loader to skip
 * the parse with a logged debug notice; a missing file,
 * allocation failure, or malformed section is logged at WARN
 * and the module-local rules container stays NULL so the
 * default-rules and recipe routes can still serve requests
 * using the maintained-C launch policy alone.
 */
static void mtsp_load_rules(void) {
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL || home[0] == '\0') {
        LOG_WARN("mtsp: METALSHARP_HOME unset; default-rules disabled");
        return;
    }
    char path[PATH_MAX];
    int written = snprintf(path, sizeof(path), MTSP_RULES_PATH_TEMPLATE, home);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        LOG_WARN("mtsp: rules path too long under %s", home);
        return;
    }
    char* err = NULL;
    MtspRules* rules = config_parse_rules(path, &err);
    if (rules == NULL) {
        free(err);
        err = NULL;
#ifdef __APPLE__
        uint32_t executable_size = PATH_MAX;
        char executable[PATH_MAX], resource_rules[PATH_MAX] = "";
        if (_NSGetExecutablePath(executable, &executable_size) == 0) {
            char* slash = strrchr(executable, '/');
            if (slash != NULL) {
                *slash = '\0';
                (void)snprintf(resource_rules, sizeof(resource_rules), "%s/../configs/mtsp-rules.toml", executable);
            }
        }
#else
        char resource_rules[PATH_MAX] = "";
#endif
        const char* fallbacks[] = {resource_rules, "configs/mtsp-rules.toml", "../configs/mtsp-rules.toml",
                                   "../../configs/mtsp-rules.toml"};
        for (size_t i = 0; i < sizeof(fallbacks) / sizeof(fallbacks[0]) && rules == NULL; i++) {
            if (fallbacks[i][0] == '\0')
                continue;
            free(err);
            err = NULL;
            rules = config_parse_rules(fallbacks[i], &err);
            if (rules != NULL)
                snprintf(path, sizeof(path), "%s", fallbacks[i]);
        }
        if (rules == NULL) {
            LOG_WARN("mtsp: cannot load rules from known locations: %s", err != NULL ? err : "(unknown)");
            free(err);
            return;
        }
    }
    g_mtsp_rules = rules;
    LOG_INFO("mtsp: loaded mtsp-rules from %s", path);
}

/* ── registration ───────────────────────────────────────────── */

/*
 * Register every MTSP route on the supplied HttpServer. NULL
 * arguments are a silent no-op so a half-initialised backend
 * cannot crash inside the registry. The MTSP rules container
 * is best-effort loaded; a missing or malformed file is
 * logged and ignored so the launch-shape / default-rules /
 * recipe routes still serve requests.
 */
void mtsp_register_routes(HttpServer* server, Database* db) {
    if (server == NULL) {
        LOG_WARN("mtsp_register_routes called with NULL server");
        return;
    }
    g_mtsp_server = server;
    g_mtsp_db = db;
    mtsp_load_rules();

    http_server_register(server, "GET", "/mtsp/pipelines", handle_pipelines);
    http_server_register(server, "GET", "/mtsp/launch-shape", handle_launch_shape);
    http_server_register(server, "GET", "/mtsp/default-rules", handle_default_rules);
    http_server_register(server, "POST", "/mtsp/prepare", handle_mtsp_prepare);
    http_server_register(server, "POST", "/mtsp/recipe", handle_mtsp_recipe);
    http_server_register(server, "POST", "/mtsp/doctor", handle_mtsp_doctor);
    http_server_register(server, "GET", "/diagnostics/pipeline/dry-run", handle_dry_run);

    LOG_INFO("mtsp routes registered (7)");
}
