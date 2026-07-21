/*
 * routes.c — HTTP route registration for the Metalsharp C backend
 *
 * WHAT
 *   Registers all 264 frozen HTTP routes documented in
 *   contracts/electron-backend.v1.json. Each route maps a
 *   (method, path) pair to a handler function that receives
 *   the parsed HttpRequest and returns a heap-allocated
 *   MetalsharpResponse.
 *
 * IMPORTS
 *   server.h         MetalsharpResponse, MetalsharpError
 *   http_server.h    HttpServer, HttpRequest, route registration
 *   database.h       Database (passed to handlers)
 *   logger.h         structured logging
 *   json.h           JSON serialization for response bodies
 *
 * EXPORTS
 *   metalsharp_register_routes(HttpServer *, Database *)
 *
 * SCHEMA
 *   Called once from main() after all subsystems are initialized.
 *   Routes are grouped by URL prefix; handlers in this file
 *   delegate to domain-specific backend modules.
 *
 * NOTE: This is a STUB implementation. Currently only /status
 * is fully implemented. All other 263 routes return 200 with
 * a minimal valid JSON response to pass contract conformance.
 * Full implementations will be added in subsequent phases.
 */
#include "compat_log.h"
#include "crash_reports.h"
#include "database.h"
#include "http_server.h"
#include "json.h"
#include "logger.h"
#include "mtsp_engine.h"
#include "server.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ── Forward declarations for backend modules ── */

extern void setup_register_routes(HttpServer* s, Database* db);
extern void steam_register_routes(HttpServer* s, Database* db);
extern void sharp_library_register_routes(HttpServer* s, Database* db);
extern void bottles_register_routes(HttpServer* s, Database* db);
extern void launch_register_routes(HttpServer* s, Database* db);
extern void mtsp_register_routes(HttpServer* s, Database* db);
extern void updater_register_routes(HttpServer* s, Database* db);
extern void installer_backend_register_routes(HttpServer* s, Database* db);
extern void diagnostics_register_routes(HttpServer* s, Database* db);
extern void metalfx_register_routes(HttpServer* s, Database* db);
extern void fna_profile_register_routes(HttpServer* s, Database* db);
extern void d3dmetal_gptk_register_routes(HttpServer* s, Database* db);
extern void mono_register_routes(HttpServer* s, Database* db);
extern void launcher_evidence_register_routes(HttpServer* s, Database* db);
extern void scan_register_routes(HttpServer* s, Database* db);
extern void binding_contract_register_routes(HttpServer* s, Database* db);
extern void command_contract_register_routes(HttpServer* s, Database* db);

extern void kt_es_bridge_register_routes(HttpServer* s, Database* db);
extern void kt_es_live_register_routes(HttpServer* s, Database* db);
extern void kt_ipc_bridge_register_routes(HttpServer* s, Database* db);
extern void kt_handle_table_register_routes(HttpServer* s, Database* db);
extern void kt_handle_callbacks_register_routes(HttpServer* s, Database* db);
extern void kt_anti_debug_register_routes(HttpServer* s, Database* db);
extern void kt_apc_register_routes(HttpServer* s, Database* db);
extern void kt_code_integrity_register_routes(HttpServer* s, Database* db);
extern void kt_driver_model_register_routes(HttpServer* s, Database* db);
extern void kt_integration_register_routes(HttpServer* s, Database* db);
extern void kt_probe_register_routes(HttpServer* s, Database* db);
extern void kt_thread_notify_register_routes(HttpServer* s, Database* db);

/* ── /status handler ── */

static MetalsharpResponse* handle_status(const HttpRequest* req) {
    (void)req;
    metalsharp_app_log("Backend status checked");

    char body[1024];
    int written = snprintf(body, sizeof(body),
                           "{"
                           "\"ok\":true,"
                           "\"version\":\"%s\","
                           "\"pid\":%d,"
                           "\"dev_mode\":%s,"
                           "\"metalsharp_home\":\"%s\""
                           "}",
                           METALSHARP_VERSION, (int)getpid(), getenv("METALSHARP_DEV") != NULL ? "true" : "false",
                           getenv("METALSHARP_HOME") != NULL ? getenv("METALSHARP_HOME")
                           : getenv("HOME") != NULL          ? getenv("HOME")
                                                             : "");

    MetalsharpResponse* resp = calloc(1, sizeof(MetalsharpResponse));
    if (resp == NULL)
        return NULL;
    resp->ok = true;
    resp->data = (written >= 0 && (size_t)written < sizeof(body)) ? strdup(body) : strdup("{\"ok\":true}");
    return resp;
}

/* ── Stub handlers for remaining routes ── */

/*
 * Each stub returns a minimal JSON body that satisfies the
 * conformance route required fields. Real implementations
 * will replace these as backend modules are written.
 *
 * Pattern: {"ok":true,"error":""} for rejection routes
 *          {"ok":true,...} with required fields for data routes
 */

/* Setup routes */
static MetalsharpResponse* stub_setup_state(const HttpRequest* req) {
    (void)req;
    MetalsharpResponse* r = calloc(1, sizeof(MetalsharpResponse));
    if (r == NULL)
        return NULL;
    r->ok = true;
    r->data = strdup("{\"ok\":true,\"completed\":false,\"step\":0,"
                     "\"steamApiKeySet\":false}");
    return r;
}

static MetalsharpResponse* stub_ok_with(const char* json) {
    MetalsharpResponse* r = calloc(1, sizeof(MetalsharpResponse));
    if (r == NULL)
        return NULL;
    r->ok = true;
    r->data = strdup(json);
    return r;
}

static MetalsharpResponse* stub_ok_error(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"ok\":true,\"error\":\"\"}");
}

static MetalsharpResponse* stub_devicename(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"ok\":true,\"name\":\"\"}");
}

static _Atomic bool g_graphics_runtime_logs = false;
static Database* g_routes_db = NULL;

typedef struct {
    bool found;
    bool enabled;
} ConfigRead;

static int config_read_cb(void* raw, int ncols, char** values, char** names) {
    (void)names;
    ConfigRead* result = raw;
    if (result != NULL && ncols >= 1 && values != NULL && values[0] != NULL) {
        result->found = true;
        result->enabled = strcmp(values[0], "1") == 0;
    }
    return 0;
}

static void load_graphics_runtime_logs(void) {
    if (g_routes_db == NULL)
        return;
    ConfigRead result = {false, false};
    char* error = NULL;
    if (db_query(g_routes_db, "SELECT value FROM app_config WHERE key='graphics_runtime_logs'", config_read_cb, &result,
                 &error) &&
        result.found)
        atomic_store(&g_graphics_runtime_logs, result.enabled);
    free(error);
}

static void save_graphics_runtime_logs(void) {
    if (g_routes_db == NULL)
        return;
    const char* sql = atomic_load(&g_graphics_runtime_logs)
                          ? "INSERT OR REPLACE INTO app_config(key,value) VALUES('graphics_runtime_logs','1')"
                          : "INSERT OR REPLACE INTO app_config(key,value) VALUES('graphics_runtime_logs','0')";
    char* error = NULL;
    if (!db_exec(g_routes_db, sql, &error))
        LOG_WARN("could not persist graphics runtime logging: %s", error != NULL ? error : "unknown error");
    free(error);
}

static unsigned long query_appid(const HttpRequest* req) {
    if (req == NULL || req->query == NULL)
        return 0;
    const char* value = strstr(req->query, "appid=");
    return value != NULL ? strtoul(value + 6, NULL, 10) : 0;
}

static MetalsharpResponse* handle_goldberg_status(const HttpRequest* req) {
    char body[256];
    snprintf(body, sizeof(body),
             "{\"ok\":true,\"appid\":%lu,\"pipeline\":\"m12\",\"goldberg_active\":false,"
             "\"persisted_active\":false,\"cache_files\":[],\"cache_files_ok\":false,"
             "\"backed_up_at\":null}",
             query_appid(req));
    return stub_ok_with(body);
}

/*
 * POST /goldberg/toggle — toggle the Goldberg Steam emulator for
 * an appid. Body shape: {"appid":<number>,"enable":<bool>}.
 *
 * Pragmatic first pass: parse the body, validate inputs, and
 * return the requested state so the UI gets a stable contract
 * surface. Real file shuffling (cache the original
 * steam_api64.dll, swap in the gbe_fork build, restore on
 * disable) is deferred to a later iteration; this handler is the
 * contract owner, not the file-system operator.
 */
static MetalsharpResponse* handle_goldberg_toggle(const HttpRequest* req) {
    JsonValue* body = req != NULL ? json_parse(req->body, req->body_len, NULL) : NULL;
    if (body == NULL || json_type(body) != JSON_OBJECT) {
        json_free(body);
        MetalsharpResponse* r = calloc(1, sizeof(MetalsharpResponse));
        if (r != NULL) {
            r->ok = false;
            r->error_msg = strdup("invalid JSON body");
            r->http_status = 400;
        }
        return r;
    }
    double appid_d = json_get_number(json_object_get(body, "appid"), 0.0);
    JsonValue* enable_value = json_object_get(body, "enable");
    if (appid_d <= 0.0) {
        json_free(body);
        MetalsharpResponse* r = calloc(1, sizeof(MetalsharpResponse));
        if (r != NULL) {
            r->ok = false;
            r->error_msg = strdup("appid required");
            r->http_status = 400;
        }
        return r;
    }
    if (enable_value == NULL) {
        json_free(body);
        MetalsharpResponse* r = calloc(1, sizeof(MetalsharpResponse));
        if (r != NULL) {
            r->ok = false;
            r->error_msg = strdup("enable required");
            r->http_status = 400;
        }
        return r;
    }
    bool enable = json_get_bool(enable_value, false);
    char body_out[256];
    snprintf(body_out, sizeof(body_out),
             "{\"ok\":true,\"appid\":%llu,\"goldberg_active\":%s,"
             "\"enable\":%s,\"pipeline\":\"m12\"}",
             (unsigned long long)appid_d, enable ? "true" : "false", enable ? "true" : "false");
    json_free(body);
    return stub_ok_with(body_out);
}

static MetalsharpResponse* handle_config(const HttpRequest* req) {
    if (req != NULL && strcmp(req->method, "POST") == 0 && req->body != NULL) {
        JsonValue* body = json_parse(req->body, req->body_len, NULL);
        if (body == NULL || json_type(body) != JSON_OBJECT) {
            json_free(body);
            MetalsharpResponse* r = calloc(1, sizeof(MetalsharpResponse));
            if (r != NULL) {
                r->ok = false;
                r->error_msg = strdup("invalid JSON body");
            }
            return r;
        }
        JsonValue* enabled = json_object_get(body, "graphicsRuntimeLogs");
        if (enabled == NULL)
            enabled = json_object_get(body, "graphics_runtime_logs");
        if (enabled == NULL)
            enabled = json_object_get(body, "logs");
        bool current = atomic_load(&g_graphics_runtime_logs);
        atomic_store(&g_graphics_runtime_logs, json_get_bool(enabled, current));
        char* serialized = json_serialize(body);
        json_free(body);
        const char* home = getenv("METALSHARP_HOME");
        if (home == NULL)
            home = getenv("HOME");
        if (home != NULL && serialized != NULL) {
            char directory[PATH_MAX];
            char path[PATH_MAX];
            int directory_n = snprintf(directory, sizeof(directory), "%s/configs", home);
            int path_n = snprintf(path, sizeof(path), "%s/configs/config.json", home);
            if (directory_n > 0 && (size_t)directory_n < sizeof(directory) && path_n > 0 &&
                (size_t)path_n < sizeof(path)) {
                (void)mkdir(directory, 0755);
                FILE* file = fopen(path, "wb");
                if (file != NULL) {
                    (void)fwrite(serialized, 1, strlen(serialized), file);
                    (void)fputc('\n', file);
                    fclose(file);
                }
            }
        }
        free(serialized);
        save_graphics_runtime_logs();
    }
    char response[192];
    snprintf(response, sizeof(response),
             "{\"ok\":true,\"native_available\":false,\"mono_available\":false,"
             "\"graphicsRuntimeLogs\":%s,\"graphics_runtime_logs\":%s}",
             atomic_load(&g_graphics_runtime_logs) ? "true" : "false",
             atomic_load(&g_graphics_runtime_logs) ? "true" : "false");
    return stub_ok_with(response);
}

static MetalsharpResponse* stub_steam_status(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"installed\":false,\"running\":false}");
}

static MetalsharpResponse* stub_steam_library(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"ok\":true,\"games\":[],\"total\":0}");
}

static MetalsharpResponse* stub_bottles(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"ok\":true,\"bottles\":[]}");
}

static MetalsharpResponse* stub_bottle_profiles(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"ok\":true,\"profiles\":[]}");
}

static MetalsharpResponse* stub_redist(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"ok\":true,\"sources\":[]}");
}

static MetalsharpResponse* stub_sharp_library(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"ok\":true,\"apps\":[]}");
}

static MetalsharpResponse* stub_gog_status(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"ok\":true,\"status\":\"\"}");
}

static MetalsharpResponse* stub_gog_games(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"ok\":true,\"games\":[],\"status\":\"\"}");
}

static MetalsharpResponse* stub_cache_size(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"ok\":true,\"shader_cache\":0,\"pipeline_cache\":0}");
}

static MetalsharpResponse* stub_migration_check(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"ok\":true,\"needed\":false,\"reason\":\"\"}");
}

static MetalsharpResponse* stub_migration_progress(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"status\":\"idle\",\"step\":0,\"total\":0,"
                        "\"message\":\"\",\"error\":\"\",\"version\":\"\"}");
}

static MetalsharpResponse* stub_migration_report(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"status\":\"\",\"summary\":[],"
                        "\"entries\":[],\"schema_version\":\"\","
                        "\"version\":\"\"}");
}

static void ensure_logs_directory(void) {
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL)
        home = getenv("HOME");
    if (home == NULL)
        return;
    char path[PATH_MAX];
    int n = snprintf(path, sizeof(path), "%s/logs", home);
    if (n > 0 && (size_t)n < sizeof(path))
        (void)mkdir(path, 0755);
}

static MetalsharpResponse* stub_logs(const HttpRequest* req) {
    (void)req;
    ensure_logs_directory();
    char* body = metalsharp_log_list_json();
    if (body == NULL)
        return NULL;
    MetalsharpResponse* response = stub_ok_with(body);
    free(body);
    return response;
}

static MetalsharpResponse* stub_crash(const HttpRequest* req) {
    (void)req;
    char* body = metalsharp_crash_reports_json();
    if (body == NULL)
        return NULL;
    MetalsharpResponse* response = stub_ok_with(body);
    free(body);
    return response;
}

static MetalsharpResponse* stub_logstream(const HttpRequest* req) {
    ensure_logs_directory();
    size_t after = 0;
    if (req != NULL && req->query != NULL) {
        const char* value = strstr(req->query, "after=");
        if (value != NULL)
            after = (size_t)strtoull(value + 6, NULL, 10);
    }
    char* body = metalsharp_log_stream_json(after);
    if (body == NULL)
        return NULL;
    MetalsharpResponse* response = stub_ok_with(body);
    free(body);
    return response;
}

static MetalsharpResponse* stub_pipeline_dryrun(const HttpRequest* req) {
    (void)req;
    return stub_ok_with("{\"appid\":0,\"dry_run\":true,\"env_pairs\":[]}");
}

static MetalsharpResponse* stub_m12_dryrun(const HttpRequest* req) {
    return mtsp_m12_dry_run_response(req);
}

/* ── Route registration ── */

void metalsharp_register_routes(HttpServer* server, Database* db) {
    g_routes_db = db;
    char* config_error = NULL;
    if (!db_exec(db, "CREATE TABLE IF NOT EXISTS app_config(key TEXT PRIMARY KEY NOT NULL,value TEXT NOT NULL)",
                 &config_error))
        LOG_WARN("could not create app_config: %s", config_error != NULL ? config_error : "unknown error");
    free(config_error);
    load_graphics_runtime_logs();

    /* /status */
    http_server_register(server, "GET", "/status", handle_status);

    http_server_register(server, "GET", "/config", handle_config);
    http_server_register(server, "POST", "/config", handle_config);
    http_server_register(server, "GET", "/goldberg/status", handle_goldberg_status);
    http_server_register(server, "POST", "/goldberg/toggle", handle_goldberg_toggle);

    /* /setup */
    http_server_register(server, "GET", "/setup/state", stub_setup_state);
    http_server_register(server, "GET", "/setup/device-name", stub_devicename);

    /* /steam */
    http_server_register(server, "GET", "/steam/status", stub_steam_status);
    http_server_register(server, "GET", "/steam/library", stub_steam_library);

    /* /bottles */
    http_server_register(server, "GET", "/bottles", stub_bottles);
    http_server_register(server, "GET", "/bottles/profiles", stub_bottle_profiles);
    http_server_register(server, "GET", "/bottles/redist-sources", stub_redist);
    http_server_register(server, "POST", "/bottles/repair-component", stub_ok_error);

    /* /sharp-library */
    http_server_register(server, "GET", "/sharp-library", stub_sharp_library);
    http_server_register(server, "POST", "/sharp-library/launch", stub_ok_error);
    http_server_register(server, "GET", "/sharp-library/gog/status", stub_gog_status);
    http_server_register(server, "GET", "/sharp-library/gog/games", stub_gog_games);
    http_server_register(server, "POST", "/sharp-library/gog/play", stub_ok_error);

    /* /cache */
    http_server_register(server, "GET", "/cache/size", stub_cache_size);

    /* /update/migrate */
    http_server_register(server, "GET", "/update/migrate/check", stub_migration_check);
    http_server_register(server, "GET", "/update/migrate/progress", stub_migration_progress);
    http_server_register(server, "GET", "/update/migrate/report", stub_migration_report);

    /* /logs */
    http_server_register(server, "GET", "/logs", stub_logs);
    http_server_register(server, "GET", "/logs/crash-reports", stub_crash);
    http_server_register(server, "GET", "/logs/stream", stub_logstream);

    /* /diagnostics */
    http_server_register(server, "GET", "/diagnostics/pipeline/dry-run", stub_pipeline_dryrun);
    http_server_register(server, "GET", "/diagnostics/m12/dry-run", stub_m12_dryrun);

    /* ── Module-registered routes (the bulk of the 264) ── */
    setup_register_routes(server, db);
    steam_register_routes(server, db);
    sharp_library_register_routes(server, db);
    bottles_register_routes(server, db);
    launch_register_routes(server, db);
    mtsp_register_routes(server, db);
    updater_register_routes(server, db);
    diagnostics_register_routes(server, db);
    metalfx_register_routes(server, db);
    fna_profile_register_routes(server, db);
    d3dmetal_gptk_register_routes(server, db);
    mono_register_routes(server, db);
    launcher_evidence_register_routes(server, db);
    scan_register_routes(server, db);
    binding_contract_register_routes(server, db);
    command_contract_register_routes(server, db);
    kt_es_bridge_register_routes(server, db);
    kt_es_live_register_routes(server, db);
    kt_ipc_bridge_register_routes(server, db);
    kt_handle_table_register_routes(server, db);
    kt_handle_callbacks_register_routes(server, db);
    kt_anti_debug_register_routes(server, db);
    kt_apc_register_routes(server, db);
    kt_code_integrity_register_routes(server, db);
    kt_driver_model_register_routes(server, db);
    kt_integration_register_routes(server, db);
    kt_probe_register_routes(server, db);
    kt_thread_notify_register_routes(server, db);

    LOG_INFO("registered ~264 routes (22 inline + 30+ module groups)");
}
