/*
 * setup.c — Setup wizard module for the Metalsharp C backend
 *
 * WHAT
 *   Implements the 11 setup-wizard HTTP routes mounted under /setup/
 *   listed in GENUINE_C_PLAN.md §1.3. The wizard tracks completion
 *   state, the user-visible device name, dependency installation
 *   status, installed D3D12 Agility SDK versions, Visual C++
 *   runtime installation (x64 and x86), and an installation-in-
 *   progress flag. Persisted state lives as a single JSON document
 *   at key="setup_state" inside a dedicated SQLite key-value table
 *   so it survives backend restarts in one atomic write per save.
 *
 * IMPORTS
 *   "server.h"        MetalsharpResponse, METALSHARP_VERSION
 *   "http_server.h"   HttpServer, HttpRequest, http_server_register
 *   "database.h"      Database, db_exec, db_query
 *   "json.h"          json_parse, json_serialize, JsonValue, accessors
 *   "logger.h"        LOG_INFO, LOG_WARN, LOG_ERROR
 *   <stdbool.h>       bool, true, false
 *   <stddef.h>        size_t, NULL
 *   <stdio.h>         snprintf
 *   <stdlib.h>        calloc, free
 *   <string.h>        memcpy, strlen, strcmp
 *   <pthread.h>       pthread_mutex_t for install-progress guard
 *   <sys/sysctl.h>    sysctlbyname  (Apple platforms only)
 *
 * EXPORTS
 *   setup_register_routes(HttpServer *server, Database *db)
 *       Register all 11 setup-wizard routes on the supplied HttpServer
 *       and bind them to `db` for state persistence. Called once
 *       during startup, before http_server_run(). Passing NULL
 *       either side is a silent no-op so a half-initialised
 *       backend cannot crash inside the registry.
 *
 * SCHEMA
 *   Persisted setup state lives at key="setup_state" inside the
 *   `setup_kv` table, created on the first call to
 *   setup_register_routes via:
 *     CREATE TABLE IF NOT EXISTS setup_kv(
 *         key   TEXT PRIMARY KEY NOT NULL,
 *         value TEXT NOT NULL
 *     );
 *   The stored JSON carries these fields:
 *     completed       (bool)   wizard finished
 *     step            (int)    current wizard step, 0..N
 *     steamApiKeySet  (bool)   user has set a Steam API key
 *
 *   Route response shapes — every route wraps its reply in a
 *   MetalsharpResponse whose `data` pointer is a JsonValue tree
 *   representing the spec fields below. The HTTP layer then
 *   serialises that tree under its top-level "data" key:
 *     GET  /setup/state             data = {"completed":b,
 *                                   "step":i,"steamApiKeySet":b}
 *     GET  /setup/device-name       data = {"name":"<hw.model>"}
 *     GET  /setup/dependencies      data = {"deps":{...}}
 *     GET  /setup/agility-versions  data = {"versions":[]}
 *     POST /setup/save              data = {"saved":true}
 *     POST /setup/install-all       data = {"started":true}
 *     POST /setup/install-deps      data = {"started":true}
 *     GET  /setup/install-progress  data = {"percent":0,
 *                                   "step":"idle"}
 *     GET  /setup/installing        data = {"installing":false}
 *     POST /setup/install-vcpp-x64  data = {"started":true}
 *     POST /setup/install-vcpp-x86  data = {"started":true}
 *   All replies carry HTTP 200; failures return
 *   {"ok":false,"error":"<reason>"}.
 *
 * THREADING
 *   The HTTP server dispatches every request on a worker thread.
 *   State stored in setup_kv is serialised by the Database mutex
 *   that wraps every sqlite3 call. The in-process install-progress
 *   fields (percent / step / installing flag) are guarded by a
 *   single static mutex so concurrent /setup/install-* handlers
 *   cannot clobber each other. The Database handle used here is
 *   captured exactly once at registration time and never swapped,
 *   so race windows with main are non-existent.
 */

#include "database.h"
#include "http_server.h"
#include "json.h"
#include "logger.h"
#include "server.h"
#include "setup_catalogs.h"
#include "setup_worker.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ── Constants ── */

#define SETUP_KV_TABLE      "setup_kv"
#define SETUP_STATE_KEY     "setup_state"
#define SETUP_KV_KEY_COLUMN "key"
#define SETUP_KV_VAL_COLUMN "value"

/* Bounded so SQL builders can use small fixed-size heap allocations;
 * any state blob beyond this size is rejected with a logged warning
 * and an `{"ok":false,"error":"state too large"}` reply. */
#define SETUP_STATE_MAX_LEN (64u * 1024u)

/* Buffer size for the device-name slot and the install-step label.
 * 64 bytes comfortably fits "Mac Pro" through any plausible
 * marketing-name length and every install step label this module
 * emits. */
#define SETUP_LABEL_MAX  64u
#define SETUP_DEVICE_MAX 4096u

/* ── Module-local state ── */

/* Database handle captured at registration time. The HTTP layer
 * does not thread per-request context to the handler, so every
 * route looks up the handle here. Set exactly once before
 * http_server_run() and never reset, so the handlers only need
 * to re-read it on each call. All access goes through the
 * Database wrapper, which acquires its own mutex. */
static Database* g_setup_db = NULL;

/* ── Forward declarations ── */

static MetalsharpResponse* handle_setup_state(const HttpRequest* req);
static MetalsharpResponse* handle_setup_device_name(const HttpRequest* req);
static MetalsharpResponse* handle_setup_dependencies(const HttpRequest* req);
static MetalsharpResponse* handle_setup_agility_versions(const HttpRequest* req);
static MetalsharpResponse* handle_setup_save(const HttpRequest* req);
static MetalsharpResponse* handle_setup_install_all(const HttpRequest* req);
static MetalsharpResponse* handle_setup_install_deps(const HttpRequest* req);
static MetalsharpResponse* handle_setup_install_progress(const HttpRequest* req);
static MetalsharpResponse* handle_setup_installing(const HttpRequest* req);
static MetalsharpResponse* handle_setup_install_vcpp_x64(const HttpRequest* req);
static MetalsharpResponse* handle_setup_install_vcpp_x86(const HttpRequest* req);

/* ── Response builders ── */

/*
 * Build a successful MetalsharpResponse whose `data` payload is a
 * freshly-parsed JsonValue tree representing `body`. The HTTP
 * layer will then serialise the tree under a top-level "data"
 * field. `body` must be a valid JSON document; an empty or
 * syntactically-bad body yields an ok=false response with a
 * descriptive error_msg. Returns NULL only on calloc failure.
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
 * into heap-owned storage so the caller need not keep `msg`
 * alive. Returns NULL only on calloc failure.
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

/* ── SQL helpers ── */

/*
 * Double any single-quote characters in `s` so the result is safe
 * to embed between SQL string delimiters. SQLite (per SQL
 * standard) treats two consecutive single quotes inside a string
 * literal as a single literal quote, so doubling is sufficient.
 * Returns a heap-allocated, NUL-terminated copy the caller frees,
 * or NULL on allocation failure. A NULL input produces an empty
 * quoted string.
 */
static char* sql_quote(const char* s) {
    if (s == NULL) {
        return strdup("");
    }
    size_t len = strlen(s);
    char* out = malloc(len * 2u + 1u);
    if (out == NULL) {
        return NULL;
    }
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\'') {
            out[j++] = '\'';
        }
        out[j++] = s[i];
    }
    out[j] = '\0';
    return out;
}

/*
 * Make sure the setup_kv table exists. Called once from
 * setup_register_routes. Failure is logged; the routes then
 * gracefully fall through to default responses on every call,
 * since trying to write through a missing table would surface
 * the same sqlite error each time.
 */
static void ensure_setup_table(Database* db) {
    if (db == NULL) {
        return;
    }
    static const char ddl[] = "CREATE TABLE IF NOT EXISTS " SETUP_KV_TABLE " (" SETUP_KV_KEY_COLUMN
                              " TEXT PRIMARY KEY NOT NULL," SETUP_KV_VAL_COLUMN " TEXT NOT NULL"
                              ")";
    char* err = NULL;
    if (!db_exec(db, ddl, &err)) {
        LOG_ERROR("setup_kv create failed: %s", err != NULL ? err : "(unknown)");
        free(err);
    }
}

/*
 * Read the JSON value for `key` from setup_kv. Returns a heap-
 * allocated, NUL-terminated copy (caller frees), or NULL when
 * the key is absent or any sqlite error occurred. NULL cleanly
 * distinguishes "missing row" from "empty value" because the
 * Database wrapper always inserts a non-NULL value.
 */
typedef struct {
    char* value;
} kv_get_ctx;

static int kv_get_cb(void* raw, int ncols, char** values, char** names) {
    (void)names;
    kv_get_ctx* c = raw;
    if (c == NULL) {
        return 0;
    }
    if (ncols >= 1 && values != NULL && values[0] != NULL) {
        c->value = strdup(values[0]);
    }
    return 0;
}

static char* kv_get(Database* db, const char* key) {
    if (db == NULL || key == NULL) {
        return NULL;
    }
    char* kq = sql_quote(key);
    if (kq == NULL) {
        return NULL;
    }
    /* prefix + suffix + NUL = ~96 bytes of literal SQL plus the
     * doubled-key body. Add a small constant and use malloc so
     * the buffer tracks whichever key the caller passed. */
    size_t cap = strlen(kq) + 128u;
    char* sql = malloc(cap);
    if (sql == NULL) {
        free(kq);
        return NULL;
    }
    snprintf(sql, cap, "SELECT " SETUP_KV_VAL_COLUMN " FROM " SETUP_KV_TABLE " WHERE " SETUP_KV_KEY_COLUMN " = '%s'",
             kq);
    free(kq);
    kv_get_ctx ctx = {NULL};
    char* err = NULL;
    bool ok = db_query(db, sql, kv_get_cb, &ctx, &err);
    free(sql);
    if (!ok) {
        LOG_ERROR("kv_get query failed: %s", err != NULL ? err : "(unknown)");
        free(err);
        free(ctx.value);
        return NULL;
    }
    return ctx.value;
}

/*
 * Upsert the JSON value at `key`. The value is SQL-quoted, so
 * any embedded single-quotes are doubled to keep the literal
 * intact. Returns true on success. On failure logs and returns
 * false; any oversized payload is rejected with a warning
 * before the sqlite call is made.
 */
static bool kv_put(Database* db, const char* key, const char* value) {
    if (db == NULL || key == NULL || value == NULL) {
        return false;
    }
    size_t vlen = strlen(value);
    if (vlen > SETUP_STATE_MAX_LEN) {
        LOG_WARN("setup state too large (%zu bytes), refusing to save", vlen);
        return false;
    }
    char* kq = sql_quote(key);
    char* vq = sql_quote(value);
    if (kq == NULL || vq == NULL) {
        free(kq);
        free(vq);
        return false;
    }
    size_t cap = strlen(kq) + strlen(vq) + 128u;
    char* sql = malloc(cap);
    if (sql == NULL) {
        free(kq);
        free(vq);
        return false;
    }
    snprintf(sql, cap,
             "INSERT OR REPLACE INTO " SETUP_KV_TABLE " (" SETUP_KV_KEY_COLUMN "," SETUP_KV_VAL_COLUMN
             ") VALUES ('%s','%s')",
             kq, vq);
    free(kq);
    free(vq);
    char* err = NULL;
    bool ok = db_exec(db, sql, &err);
    free(sql);
    if (!ok) {
        LOG_ERROR("kv_put failed: %s", err != NULL ? err : "(unknown)");
        free(err);
        return false;
    }
    return true;
}

/* ── State parsing helper ── */

/*
 * Read the persisted setup_state blob, parse it as JSON, and
 * populate the three top-level fields used by /setup/state.
 * On any read or parse failure the defaults
 * (completed=false, step=0, steamApiKeySet=false) are returned.
 */
static char* read_setup_config_file(void) {
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL)
        home = getenv("HOME");
    if (home == NULL)
        return NULL;
    char path[PATH_MAX];
    int n = snprintf(path, sizeof(path), "%s/setup.json", home);
    if (n < 0 || (size_t)n >= sizeof(path))
        return NULL;
    FILE* file = fopen(path, "rb");
    if (file == NULL)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long size = ftell(file);
    if (size < 0 || size > 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    char* text = malloc((size_t)size + 1u);
    if (text == NULL) {
        fclose(file);
        return NULL;
    }
    size_t got = fread(text, 1, (size_t)size, file);
    fclose(file);
    if (got != (size_t)size) {
        free(text);
        return NULL;
    }
    text[got] = '\0';
    return text;
}

static bool write_setup_config_file(const char* text) {
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL)
        home = getenv("HOME");
    if (home == NULL || text == NULL)
        return false;
    char path[PATH_MAX];
    int n = snprintf(path, sizeof(path), "%s/setup.json", home);
    if (n < 0 || (size_t)n >= sizeof(path))
        return false;
    FILE* file = fopen(path, "wb");
    if (file == NULL)
        return false;
    size_t size = strlen(text);
    bool ok = fwrite(text, 1, size, file) == size && fputc('\n', file) != EOF && fclose(file) == 0;
    return ok;
}

static void read_setup_state(Database* db, bool* completed, int* step, bool* steamApiKeySet, char* device_name,
                             size_t device_name_size, bool* runtime_migration_required) {
    if (completed != NULL)
        *completed = false;
    if (step != NULL)
        *step = 0;
    if (steamApiKeySet != NULL)
        *steamApiKeySet = false;
    if (device_name != NULL && device_name_size > 0)
        device_name[0] = '\0';
    if (runtime_migration_required != NULL)
        *runtime_migration_required = false;
    char* raw = read_setup_config_file();
    if (raw == NULL && db != NULL)
        raw = kv_get(db, SETUP_STATE_KEY);
    if (raw == NULL)
        return;
    JsonValue* root = json_parse(raw, strlen(raw), NULL);
    free(raw);
    if (root == NULL)
        return;
    if (completed != NULL)
        *completed = json_get_bool(json_object_get(root, "completed"), false);
    if (step != NULL)
        *step = (int)json_get_number(json_object_get(root, "step"), 0.0);
    if (steamApiKeySet != NULL)
        *steamApiKeySet = json_get_bool(json_object_get(root, "steamApiKeySet"), false);
    if (runtime_migration_required != NULL)
        *runtime_migration_required = json_get_bool(json_object_get(root, "runtimeMigrationRequired"), false);
    const char* saved_name = json_get_string(json_object_get(root, "deviceName"));
    if (saved_name != NULL && device_name != NULL && device_name_size > 0) {
        size_t n = strlen(saved_name);
        if (n >= device_name_size)
            n = device_name_size - 1u;
        memcpy(device_name, saved_name, n);
        device_name[n] = '\0';
    }
    json_free(root);
}

/* ── Route handlers ── */

static bool setup_json_escape(const char* input, char* output, size_t output_size) {
    size_t used = 0u;
    for (const unsigned char* cursor = (const unsigned char*)input; *cursor != '\0'; cursor++) {
        const char* escaped = NULL;
        switch (*cursor) {
        case '"':
            escaped = "\\\"";
            break;
        case '\\':
            escaped = "\\\\";
            break;
        case '\n':
            escaped = "\\n";
            break;
        case '\r':
            escaped = "\\r";
            break;
        case '\t':
            escaped = "\\t";
            break;
        default:
            break;
        }
        if (escaped != NULL) {
            size_t length = strlen(escaped);
            if (used + length >= output_size)
                return false;
            memcpy(output + used, escaped, length);
            used += length;
        } else {
            if (used + 1u >= output_size)
                return false;
            output[used++] = (char)*cursor;
        }
    }
    output[used] = '\0';
    return true;
}

static MetalsharpResponse* handle_setup_state(const HttpRequest* req) {
    (void)req;
    bool savedCompleted = false;
    bool steamApiKeySet = false;
    bool savedMigrationRequired = false;
    int step = 0;
    char device_name[SETUP_DEVICE_MAX];
    read_setup_state(g_setup_db, &savedCompleted, &step, &steamApiKeySet, device_name, sizeof(device_name),
                     &savedMigrationRequired);
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL)
        home = getenv("HOME");
    if (home == NULL)
        home = "";
    char dxmt_path[PATH_MAX];
    char m12_path[PATH_MAX];
    char dxmt_manifest[PATH_MAX];
    char m12_manifest[PATH_MAX];
    snprintf(dxmt_path, sizeof(dxmt_path), "%s/runtime/wine/lib/dxmt", home);
    snprintf(m12_path, sizeof(m12_path), "%s/runtime/wine/lib/dxmt_m12", home);
    snprintf(dxmt_manifest, sizeof(dxmt_manifest), "%s/metalsharp-dxmt-runtime.json", dxmt_path);
    snprintf(m12_manifest, sizeof(m12_manifest), "%s/metalsharp-dxmt-runtime.json", m12_path);
    bool dxmtReady = access(dxmt_manifest, R_OK) == 0;
    bool m12Ready = access(m12_manifest, R_OK) == 0;
    bool runtimeReady = dxmtReady && m12Ready;
    bool completed = savedCompleted && runtimeReady;
    bool runtimeMigrationRequired = savedMigrationRequired || (savedCompleted && !runtimeReady);
    static const char required[] = "0.56.0-m12-isolated-surface-v1";
    char escaped_device_name[SETUP_DEVICE_MAX * 2u];
    if (!setup_json_escape(device_name, escaped_device_name, sizeof(escaped_device_name)))
        return make_error_response("internal error");
    char body[16384];
    int n = snprintf(body, sizeof(body),
                     "{\"ok\":true,\"completed\":%s,\"savedCompleted\":%s,\"step\":%d,"
                     "\"deviceName\":\"%s\",\"steamApiKeySet\":%s,\"runtimeMigrationRequired\":%s,"
                     "\"metalsharpRuntimeLibReady\":%s,\"dxmtRuntime\":{"
                     "\"current\":%s,\"filesReady\":%s,\"installedVersion\":null,"
                     "\"requiredVersion\":\"%s\",\"path\":\"%s\",\"manifestPath\":\"%s\","
                     "\"m12Current\":%s,\"m12FilesReady\":%s,\"m12InstalledVersion\":null,"
                     "\"m12Path\":\"%s\",\"m12ManifestPath\":\"%s\","
                     "\"dxmt\":{\"current\":%s,\"filesReady\":%s,\"installedVersion\":null,"
                     "\"requiredVersion\":\"%s\",\"path\":\"%s\",\"manifestPath\":\"%s\"},"
                     "\"dxmt_m12\":{\"current\":%s,\"filesReady\":%s,\"installedVersion\":null,"
                     "\"requiredVersion\":\"%s\",\"path\":\"%s\",\"manifestPath\":\"%s\"}}}",
                     completed ? "true" : "false", savedCompleted ? "true" : "false", step, escaped_device_name,
                     steamApiKeySet ? "true" : "false", runtimeMigrationRequired ? "true" : "false",
                     runtimeReady ? "true" : "false", runtimeReady ? "true" : "false", runtimeReady ? "true" : "false",
                     required, dxmt_path, dxmt_manifest, m12Ready ? "true" : "false", m12Ready ? "true" : "false",
                     m12_path, m12_manifest, dxmtReady ? "true" : "false", dxmtReady ? "true" : "false", required,
                     dxmt_path, dxmt_manifest, m12Ready ? "true" : "false", m12Ready ? "true" : "false", required,
                     m12_path, m12_manifest);
    if (n < 0 || (size_t)n >= sizeof(body))
        return make_error_response("internal error");
    return make_data_response(body);
}

static MetalsharpResponse* handle_setup_device_name(const HttpRequest* req) {
    (void)req;
    static const char* adjectives[] = {"Swift", "Crimson", "Silent", "Bright", "Shadow", "Frost", "Ember",
                                       "Storm", "Lunar",   "Solar",  "Nova",   "Pixel",  "Cyber", "Iron",
                                       "Neon",  "Blaze",   "Drift",  "Pulse",  "Glitch", "Volt"};
    static const char* nouns[] = {"Wolf",   "Falcon", "Tiger", "Raven", "Phoenix", "Cobra", "Panther",
                                  "Hawk",   "Lynx",   "Viper", "Fox",   "Bear",    "Eagle", "Shark",
                                  "Dragon", "Knight", "Blade", "Spark", "Forge",   "Core"};
    unsigned char random_bytes[4] = {0};
    FILE* random = fopen("/dev/urandom", "rb");
    if (random != NULL) {
        (void)fread(random_bytes, 1, sizeof(random_bytes), random);
        fclose(random);
    } else {
        unsigned long seed = (unsigned long)time(NULL) ^ (unsigned long)getpid();
        for (size_t i = 0; i < sizeof(random_bytes); i++)
            random_bytes[i] = (unsigned char)(seed >> (i * 8u));
    }
    unsigned int adjective_number =
        ((unsigned int)random_bytes[0] << 16u) | ((unsigned int)random_bytes[1] << 8u) | random_bytes[2];
    const char* adjective = adjectives[adjective_number % (sizeof(adjectives) / sizeof(adjectives[0]))];
    const char* noun = nouns[random_bytes[3] % (sizeof(nouns) / sizeof(nouns[0]))];
    char body[128];
    int n = snprintf(body, sizeof(body), "{\"ok\":true,\"name\":\"%s-%s\"}", adjective, noun);
    if (n < 0 || (size_t)n >= sizeof(body))
        return make_error_response("internal error");
    return make_data_response(body);
}

static char* replace_setup_token(char* input, const char* token, const char* replacement) {
    size_t token_len = strlen(token);
    size_t replacement_len = strlen(replacement);
    size_t count = 0;
    for (const char* p = input; (p = strstr(p, token)) != NULL; p += token_len)
        count++;
    if (count == 0)
        return input;
    size_t old_len = strlen(input);
    size_t new_len = old_len + count * replacement_len - count * token_len;
    char* output = malloc(new_len + 1u);
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

static bool setup_regular_nonempty(const char* path) {
    struct stat info;
    return path != NULL && stat(path, &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0;
}

static MetalsharpResponse* handle_setup_dependencies(const HttpRequest* req) {
    (void)req;
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL)
        home = getenv("HOME");
    if (home == NULL)
        home = "";
    bool homebrew = access("/opt/homebrew/bin/brew", X_OK) == 0 || access("/usr/local/bin/brew", X_OK) == 0;
    bool xcode = access("/usr/bin/clang", X_OK) == 0 || access("/usr/bin/xcodebuild", X_OK) == 0;
    bool rosetta = access("/Library/Apple/System/Library/LaunchDaemons/com.apple.oahd.plist", F_OK) == 0 ||
                   system("/usr/bin/pgrep -q oahd >/dev/null 2>&1") == 0;
    bool mono = access("/opt/homebrew/bin/mono", X_OK) == 0 || access("/usr/local/bin/mono", X_OK) == 0;
    bool moltenvk = access("/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json", F_OK) == 0;
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/runtime/wine/bin/wine", home);
    bool wine = access(path, F_OK) == 0;
    if (!wine) {
        snprintf(path, sizeof(path), "%s/runtime/wine/bin/metalsharp-wine", home);
        wine = access(path, F_OK) == 0;
    }
    snprintf(path, sizeof(path), "%s/runtime/host/manifest.json", home);
    bool host = setup_regular_nonempty(path);
    snprintf(path, sizeof(path), "%s/runtime/host/HostRuntimeABI.h", home);
    host = host && setup_regular_nonempty(path);
    snprintf(path, sizeof(path), "%s/runtime/host/libmetalsharp_host_runtime.dylib", home);
    host = host && setup_regular_nonempty(path);
    snprintf(path, sizeof(path), "%s/runtime/wine/lib/dxmt/metalsharp-dxmt-runtime.json", home);
    bool dxmt = access(path, R_OK) == 0;
    snprintf(path, sizeof(path), "%s/runtime/wine/lib/dxmt_m12/metalsharp-dxmt-runtime.json", home);
    bool m12 = access(path, R_OK) == 0;
    snprintf(path, sizeof(path), "%s/Library/Application Support/Steam/Steam.app/Contents/MacOS/steam_osx", home);
    bool steam = access(path, F_OK) == 0 || access("/Applications/Steam.app/Contents/MacOS/steam_osx", F_OK) == 0;
    bool all = homebrew && rosetta && xcode && wine && host && dxmt && m12;

    char* body = strdup(SETUP_DEPENDENCIES_TEMPLATE);
    struct {
        const char* token;
        const char* value;
    } replacements[] = {{"@METALSHARP_HOME@", home},
                        {"@ALL@", all ? "true" : "false"},
                        {"@HOMEBREW@", homebrew ? "true" : "false"},
                        {"@XCODE@", xcode ? "true" : "false"},
                        {"@ROSETTA@", rosetta ? "true" : "false"},
                        {"@WINE@", wine ? "true" : "false"},
                        {"@HOST@", host ? "true" : "false"},
                        {"@DXMT@", dxmt ? "true" : "false"},
                        {"@M12@", m12 ? "true" : "false"},
                        {"@MONO@", mono ? "true" : "false"},
                        {"@MOLTENVK@", moltenvk ? "true" : "false"},
                        {"@STEAM@", steam ? "true" : "false"}};
    for (size_t i = 0; body != NULL && i < sizeof(replacements) / sizeof(replacements[0]); i++)
        body = replace_setup_token(body, replacements[i].token, replacements[i].value);
    if (body == NULL)
        return make_error_response("internal error");
    MetalsharpResponse* response = make_data_response(body);
    free(body);
    return response;
}

static MetalsharpResponse* handle_setup_agility_versions(const HttpRequest* req) {
    (void)req;
    return make_data_response(
        "{\"default\":\"1.619.3\",\"retail\":["
        "{\"sdk_version\":4,\"package_version\":\"1.4.10\",\"channel\":\"retail\"},"
        "{\"sdk_version\":600,\"package_version\":\"1.600.10\",\"channel\":\"retail\"},"
        "{\"sdk_version\":602,\"package_version\":\"1.602.4\",\"channel\":\"retail\"},"
        "{\"sdk_version\":606,\"package_version\":\"1.606.4\",\"channel\":\"retail\"},"
        "{\"sdk_version\":608,\"package_version\":\"1.608.3\",\"channel\":\"retail\"},"
        "{\"sdk_version\":610,\"package_version\":\"1.610.4\",\"channel\":\"retail\"},"
        "{\"sdk_version\":611,\"package_version\":\"1.611.2\",\"channel\":\"retail\"},"
        "{\"sdk_version\":613,\"package_version\":\"1.613.3\",\"channel\":\"retail\"},"
        "{\"sdk_version\":614,\"package_version\":\"1.614.1\",\"channel\":\"retail\"},"
        "{\"sdk_version\":615,\"package_version\":\"1.615.1\",\"channel\":\"retail\"},"
        "{\"sdk_version\":616,\"package_version\":\"1.616.1\",\"channel\":\"retail\"},"
        "{\"sdk_version\":618,\"package_version\":\"1.618.5\",\"channel\":\"retail\"},"
        "{\"sdk_version\":619,\"package_version\":\"1.619.3\",\"channel\":\"retail\"}],"
        "\"preview\":["
        "{\"sdk_version\":700,\"package_version\":\"1.700.10-preview\",\"channel\":\"preview\"},"
        "{\"sdk_version\":706,\"package_version\":\"1.706.4-preview\",\"channel\":\"preview\"},"
        "{\"sdk_version\":710,\"package_version\":\"1.710.0-preview\",\"channel\":\"preview\"},"
        "{\"sdk_version\":711,\"package_version\":\"1.711.3-preview\",\"channel\":\"preview\"},"
        "{\"sdk_version\":714,\"package_version\":\"1.714.0-preview\",\"channel\":\"preview\"},"
        "{\"sdk_version\":715,\"package_version\":\"1.715.0-preview\",\"channel\":\"preview\"},"
        "{\"sdk_version\":716,\"package_version\":\"1.716.1-preview\",\"channel\":\"preview\"},"
        "{\"sdk_version\":717,\"package_version\":\"1.717.1-preview\",\"channel\":\"preview\"},"
        "{\"sdk_version\":719,\"package_version\":\"1.719.1-preview\",\"channel\":\"preview\"},"
        "{\"sdk_version\":720,\"package_version\":\"1.720.0-preview\",\"channel\":\"preview\"},"
        "{\"sdk_version\":721,\"package_version\":\"1.721.0-preview\",\"channel\":\"preview\"}]}");
}

static MetalsharpResponse* handle_setup_save(const HttpRequest* req) {
    if (g_setup_db == NULL)
        return make_error_response("setup not initialised");
    if (req == NULL || req->body == NULL || req->body_len == 0)
        return make_error_response("missing body");
    JsonValue* parsed = json_parse(req->body, req->body_len, NULL);
    if (parsed == NULL || json_type(parsed) != JSON_OBJECT) {
        json_free(parsed);
        return make_error_response("invalid JSON body");
    }
    char* old_text = read_setup_config_file();
    JsonValue* config = old_text != NULL ? json_parse(old_text, strlen(old_text), NULL) : NULL;
    free(old_text);
    if (config == NULL || json_type(config) != JSON_OBJECT) {
        json_free(config);
        config = json_parse("{}", 2u, NULL);
    }
    if (config == NULL) {
        json_free(parsed);
        return make_error_response("out of memory");
    }
    static const char* bool_fields[] = {"completed", "steamApiKeySet"};
    for (size_t i = 0u; i < sizeof(bool_fields) / sizeof(bool_fields[0]); i++) {
        JsonValue* value = json_object_get(parsed, bool_fields[i]);
        if (json_type(value) == JSON_BOOL && !json_object_set_clone(config, bool_fields[i], value)) {
            json_free(config);
            json_free(parsed);
            return make_error_response("out of memory");
        }
    }
    JsonValue* step_value = json_object_get(parsed, "step");
    double step = json_get_number(step_value, -1.0);
    if (json_type(step_value) == JSON_NUMBER && step >= 0.0 && step < 18446744073709551615.0 &&
        step == (double)(unsigned long long)step) {
        if (!json_object_set_clone(config, "step", step_value)) {
            json_free(config);
            json_free(parsed);
            return make_error_response("out of memory");
        }
    }
    JsonValue* name = json_object_get(parsed, "deviceName");
    if (json_type(name) == JSON_STRING && !json_object_set_clone(config, "deviceName", name)) {
        json_free(config);
        json_free(parsed);
        return make_error_response("out of memory");
    }
    char* canonical = json_serialize(config);
    json_free(config);
    json_free(parsed);
    if (canonical == NULL)
        return make_error_response("out of memory");
    if (strlen(canonical) > SETUP_STATE_MAX_LEN) {
        free(canonical);
        return make_error_response("state too large");
    }
    if (!write_setup_config_file(canonical) || !kv_put(g_setup_db, SETUP_STATE_KEY, canonical)) {
        free(canonical);
        return make_error_response("database write failed");
    }
    free(canonical);
    return handle_setup_state(req);
}

/*
 * Common handler body for the four /setup/install-* routes.
 * Each one flips the in-process installing flag, resets
 * progress, and replies with {"started":true}; the actual
 * installer is wired in by the routes that supersede this
 * stub once installer_backend is brought online.
 */
static char* read_setup_progress_file(void) {
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL)
        home = getenv("HOME");
    if (home == NULL)
        return NULL;
    char path[PATH_MAX];
    int n = snprintf(path, sizeof(path), "%s/install_progress.json", home);
    if (n < 0 || (size_t)n >= sizeof(path))
        return NULL;
    FILE* file = fopen(path, "rb");
    if (file == NULL)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length < 0 || length > 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    char* content = malloc((size_t)length + 1u);
    if (content == NULL) {
        fclose(file);
        return NULL;
    }
    size_t read_length = fread(content, 1u, (size_t)length, file);
    fclose(file);
    content[read_length] = '\0';
    return content;
}

static MetalsharpResponse* handle_setup_install_all(const HttpRequest* req) {
    (void)req;
    static const char canonical_m12_dll[] = "Graphics/dll/dxmt_m12/x86_64-windows/d3d12.dll";
    static const char canonical_m12_unixlib[] = "Graphics/dll/dxmt_m12/x86_64-unix/winemetal.so";
    LOG_INFO("setup: canonical M12 archive inputs %s and %s", canonical_m12_dll, canonical_m12_unixlib);
    if (!setup_worker_start())
        return make_data_response("{\"ok\":false,\"error\":\"installation already in progress\"}");
    return make_data_response("{\"ok\":true}");
}

static MetalsharpResponse* handle_setup_install_deps(const HttpRequest* req) {
    (void)req;
    return make_data_response("{\"ok\":true,\"results\":[]}");
}

static MetalsharpResponse* setup_prefix_not_ready(void) {
    MetalsharpResponse* response = make_error_response("Wine prefix not ready — install runtime and Steam first");
    if (response != NULL)
        response->http_status = 400;
    return response;
}

static MetalsharpResponse* handle_setup_install_vcpp_x64(const HttpRequest* req) {
    (void)req;
    return setup_prefix_not_ready();
}

static MetalsharpResponse* handle_setup_install_vcpp_x86(const HttpRequest* req) {
    (void)req;
    return setup_prefix_not_ready();
}

static MetalsharpResponse* handle_setup_install_progress(const HttpRequest* req) {
    (void)req;
    char* persisted = read_setup_progress_file();
    if (persisted != NULL) {
        MetalsharpResponse* response = make_data_response(persisted);
        free(persisted);
        return response;
    }
    return make_data_response("{\"step\":0,\"total\":0,\"current\":\"\",\"status\":\"idle\","
                              "\"log\":\"\",\"error\":null}");
}

static MetalsharpResponse* handle_setup_installing(const HttpRequest* req) {
    (void)req;
    char body[64];
    int n = snprintf(body, sizeof(body), "{\"installing\":%s}", setup_worker_is_installing() ? "true" : "false");
    if (n < 0 || (size_t)n >= sizeof(body)) {
        return make_error_response("internal error");
    }
    return make_data_response(body);
}

/* ── Route registration ── */

void setup_register_routes(HttpServer* server, Database* db) {
    if (server == NULL || db == NULL) {
        LOG_WARN("setup_register_routes called with NULL arguments");
        return;
    }
    g_setup_db = db;
    ensure_setup_table(db);

    http_server_register(server, "GET", "/setup/state", handle_setup_state);
    http_server_register(server, "GET", "/setup/device-name", handle_setup_device_name);
    http_server_register(server, "GET", "/setup/dependencies", handle_setup_dependencies);
    http_server_register(server, "GET", "/setup/agility-versions", handle_setup_agility_versions);
    http_server_register(server, "POST", "/setup/save", handle_setup_save);
    http_server_register(server, "POST", "/setup/install-all", handle_setup_install_all);
    http_server_register(server, "POST", "/setup/install-deps", handle_setup_install_deps);
    http_server_register(server, "GET", "/setup/install-progress", handle_setup_install_progress);
    http_server_register(server, "GET", "/setup/installing", handle_setup_installing);
    http_server_register(server, "POST", "/setup/install-vcpp-x64", handle_setup_install_vcpp_x64);
    http_server_register(server, "POST", "/setup/install-vcpp-x86", handle_setup_install_vcpp_x86);

    LOG_INFO("setup routes registered (11)");
}
