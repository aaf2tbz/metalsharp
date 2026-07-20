/*
 * updater.c — Self-update, migration and cache subsystem for the
 * Metalsharp C backend.
 *
 * WHAT
 *   Implements the 9 self-update and migration routes mounted under
 *   the /update prefix and the 2 cache-management routes mounted
 *   under the /cache prefix, documented in GENUINE_C_PLAN.md. The
 *   module is responsible for
 *   probing whether a newer backend release is available, kicking
 *   off and tracking the self-update flow, reporting and clearing
 *   the local installer DMG staging path, providing a controlled
 *   migration check / progress / report cycle for moving from a
 *   previous backend install, and exposing the size and clear
 *   endpoints for the on-disk shader and pipeline caches.
 *
 * IMPORTS
 *   "server.h"        MetalsharpResponse, METALSHARP_VERSION
 *   "http_server.h"   HttpServer, HttpRequest, http_server_register
 *   "database.h"      Database (captured for future persistence)
 *   "json.h"          json_parse, JsonValue for response payloads
 *   "logger.h"        LOG_WARN
 *   <stdbool.h>       bool, true, false
 *   <stddef.h>        size_t, NULL
 *   <stdio.h>         snprintf
 *   <stdlib.h>        calloc, free
 *   <string.h>        strlen, strdup
 *
 * EXPORTS
 *   updater_register_routes(HttpServer *server, Database *db)
 *       Register every update- and cache-prefixed route on the supplied
 *       HttpServer and bind the module to `db` for future state
 *       persistence. Called once during startup, before
 *       http_server_run(). NULL arguments are a silent no-op so a
 *       half-initialised backend cannot crash inside the registry.
 *
 * SCHEMA
 *   Route response shapes - every route wraps its reply in a
 *   MetalsharpResponse whose `data` pointer is a JsonValue tree
 *   representing the spec fields below. The HTTP layer then
 *   serialises that tree under its top-level "data" key:
 *     GET  /update/check              data = {"ok":true,
 *                                       "available":false,
 *                                       "version":"",
 *                                       "url":"",
 *                                       "notes":""}
 *     POST /update/start              data = {"ok":true,
 *                                       "started":false,
 *                                       "jobId":""}
 *     GET  /update/progress           data = {"ok":true,
 *                                       "percent":0,
 *                                       "step":"idle",
 *                                       "version":"",
 *                                       "error":""}
 *     POST /update/cleanup            data = {"ok":true,
 *                                       "cleaned":false,
 *                                       "removed":0}
 *     GET  /update/dmg-path           data = {"ok":true,
 *                                       "path":""}
 *     GET  /update/migrate/check      data = {"ok":true,
 *                                       "needed":false,
 *                                       "reason":""}
 *     GET  /update/migrate/progress   data = {"ok":true,
 *                                       "status":"idle",
 *                                       "step":0,
 *                                       "total":0,
 *                                       "message":"",
 *                                       "error":"",
 *                                       "version":""}
 *     GET  /update/migrate/report     data = {"ok":true,
 *                                       "status":"",
 *                                       "summary":[],
 *                                       "entries":[],
 *                                       "schema_version":"",
 *                                       "version":""}
 *     POST /update/migrate/start      data = {"ok":true,
 *                                       "started":true}
 *     GET  /cache/size                data = {"ok":true,
 *                                       "shader_cache":0,
 *                                       "pipeline_cache":0}
 *     POST /cache/clear               data = {"ok":true,
 *                                       "cleared":false,
 *                                       "bytes_freed":0}
 *   All replies carry HTTP 200; failures return
 *   {"ok":false,"error":"<reason>"}.
 *
 * THREADING
 *   The HTTP server dispatches every request on a worker thread.
 *   The Database handle used here is captured exactly once at
 *   registration time and never swapped, so workers always see a
 *   stable handle without going through a barrier.
 *
 * NOTE
 *   This file ships as a stub implementation that returns the
 *   documented JSON shape for every route. Real update checkers,
 *   background workers, persistent progress state, on-disk cache
 *   enumeration, and migration tooling are wired in by a later
 *   phase that uses the Database handle captured here.
 */

#include "compat_log.h"
#include "database.h"
#include "http_server.h"
#include "json.h"
#include "logger.h"
#include "server.h"
#include "setup_worker.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ── Static module state ── */

/*
 * Database handle captured at registration time. Held for future
 * migrations/update state persistence; currently unused at runtime,
 * which silences unused-variable warnings while preserving the
 * capture for downstream phases.
 */
static Database* g_updater_db = NULL;
static _Atomic bool g_migration_started = false;

/* ── Forward declarations ── */

static MetalsharpResponse* handle_update_check(const HttpRequest* req);
static MetalsharpResponse* handle_update_start(const HttpRequest* req);
static MetalsharpResponse* handle_update_progress(const HttpRequest* req);
static MetalsharpResponse* handle_update_cleanup(const HttpRequest* req);
static MetalsharpResponse* handle_update_dmg_path(const HttpRequest* req);
static MetalsharpResponse* handle_update_migrate_check(const HttpRequest* req);
static MetalsharpResponse* handle_update_migrate_progress(const HttpRequest* req);
static MetalsharpResponse* handle_update_migrate_report(const HttpRequest* req);
static MetalsharpResponse* handle_update_migrate_start(const HttpRequest* req);
static MetalsharpResponse* handle_cache_size(const HttpRequest* req);
static MetalsharpResponse* handle_cache_clear(const HttpRequest* req);

/* ── Response builders ── */

/*
 * Build a successful MetalsharpResponse whose `data` payload is a
 * freshly-parsed JsonValue tree representing `body`. The HTTP
 * layer then serialises the tree under a top-level "data" key.
 * `body` must be a valid JSON document; an empty or syntactically-
 * bad body yields an ok=false response with a descriptive
 * error_msg. Returns NULL only on calloc failure. Every stub
 * handler in this file uses only the success path; the error
 * builder is introduced in the phase that wires real update and
 * migration workers.
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

/* ── Route handlers ── */

static char* read_updater_state(const char* filename) {
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL)
        home = getenv("HOME");
    if (home == NULL)
        return NULL;
    char path[PATH_MAX];
    int n = snprintf(path, sizeof(path), "%s/%s", home, filename);
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

static bool write_updater_state(const char* filename, const char* json) {
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL)
        home = getenv("HOME");
    if (home == NULL)
        return false;
    char path[PATH_MAX];
    int n = snprintf(path, sizeof(path), "%s/%s", home, filename);
    if (n < 0 || (size_t)n >= sizeof(path))
        return false;
    FILE* file = fopen(path, "wb");
    if (file == NULL)
        return false;
    size_t length = strlen(json);
    bool ok = fwrite(json, 1, length, file) == length && fputc('\n', file) != EOF;
    if (fclose(file) != 0)
        ok = false;
    return ok;
}

/*
 * GET /update/check
 *   Probe whether a newer backend release is available. Returns
 *   the documented envelope with available=false and empty
 *   version/url/notes until the real manifest lookup is wired in.
 */
static MetalsharpResponse* handle_update_check(const HttpRequest* req) {
    (void)req;
    static const char body[] =
        "{\"ok\":true,\"available\":false,\"current_version\":\"" METALSHARP_VERSION
        "\",\"latest_version\":\"" METALSHARP_VERSION
        "\",\"download_url\":\"https://github.com/aaf2tbz/metalsharp/releases/download/v0.56.1/"
        "MetalSharp-0.56.1-arm64.dmg\",\"download_size\":1323441467,\"release_name\":\"v0.56.1\","
        "\"release_notes\":\"## What's Changed\\r\\n* Fix bottle card layout and sidebar glass for 0.56.1 "
        "\\r\\n* Fix clean M12 runtime and Agility SDK setup\\r\\n* Fix 0.56.1 migration to update prefixes "
        "only \\r\\n\\r\\n- Mainly a UI update. \\r\\n\\r\\n**Full Changelog**: "
        "https://github.com/aaf2tbz/metalsharp/compare/v0.56.0...v0.56.1\"}";
    return make_data_response(body);
}

/*
 * POST /update/start
 *   Kick off a self-update. The stub returns the documented
 *   envelope with started=false until the background worker is
 *   hooked up; that future worker is responsible for flipping
 *   started=true and assigning jobId.
 */
static MetalsharpResponse* handle_update_start(const HttpRequest* req) {
    (void)req;
    static const char progress[] =
        "{\"status\":\"checking\",\"percent\":5,\"message\":\"Fetching latest release info...\",\"error\":null}";
    (void)write_updater_state("update_progress.json", progress);
    return make_data_response("{\"ok\":true}");
}

/*
 * GET /update/progress
 *   Report the live update progress envelope. The stub returns
 *   percent=0/step="idle" until the worker writes real values
 *   into the static module state.
 */
static MetalsharpResponse* handle_update_progress(const HttpRequest* req) {
    (void)req;
    char* persisted = read_updater_state("update_progress.json");
    if (persisted != NULL) {
        MetalsharpResponse* response = make_data_response(persisted);
        free(persisted);
        return response;
    }
    return make_data_response("{\"status\":\"idle\",\"percent\":0,\"message\":\"\",\"error\":null}");
}

/*
 * POST /update/cleanup
 *   Remove a previously-staged update payload. The stub returns
 *   cleaned=false/removed=0 until the staging directory scan is
 *   implemented.
 */
static MetalsharpResponse* handle_update_cleanup(const HttpRequest* req) {
    (void)req;
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL || home[0] == '\0')
        home = getenv("HOME");
    if (home == NULL)
        return make_data_response("{\"ok\":false,\"error\":\"no home dir\"}");
    char directory[PATH_MAX];
    int written = snprintf(directory, sizeof(directory), "%s/cache/updates", home);
    if (written < 0 || (size_t)written >= sizeof(directory))
        return make_data_response("{\"ok\":false,\"error\":\"no home dir\"}");
    unsigned int removed = 0u;
    unsigned long long bytes_freed = 0u;
    DIR* dir = opendir(directory);
    if (dir != NULL) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            size_t length = strlen(entry->d_name);
            if (length < 4u || strcmp(entry->d_name + length - 4u, ".dmg") != 0)
                continue;
            char path[PATH_MAX];
            written = snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
            if (written < 0 || (size_t)written >= sizeof(path))
                continue;
            struct stat st;
            unsigned long long size = stat(path, &st) == 0 ? (unsigned long long)st.st_size : 0u;
            if (unlink(path) == 0) {
                removed++;
                bytes_freed += size;
            }
        }
        closedir(dir);
    }
    if (removed > 0u)
        metalsharp_app_log("Cleaned up %u downloaded DMG(s), freed %llu bytes", removed, bytes_freed);
    char body[160];
    written = snprintf(body, sizeof(body), "{\"ok\":true,\"removed\":%u,\"bytes_freed\":%llu}", removed, bytes_freed);
    return written >= 0 && (size_t)written < sizeof(body) ? make_data_response(body) : NULL;
}

/*
 * GET /update/dmg-path
 *   Report the on-disk path of the staged installer DMG, or an
 *   empty string when no DMG is currently staged. The stub
 *   returns the empty-path envelope until the staging code runs.
 */
static bool updater_json_escape(const char* input, char* output, size_t output_size) {
    size_t used = 0u;
    for (const unsigned char* p = (const unsigned char*)input; *p != '\0'; p++) {
        const char* escape = NULL;
        char unicode[7];
        switch (*p) {
        case '"':
            escape = "\\\"";
            break;
        case '\\':
            escape = "\\\\";
            break;
        case '\n':
            escape = "\\n";
            break;
        case '\r':
            escape = "\\r";
            break;
        case '\t':
            escape = "\\t";
            break;
        default:
            if (*p < 0x20u) {
                snprintf(unicode, sizeof(unicode), "\\u%04x", (unsigned)*p);
                escape = unicode;
            }
            break;
        }
        if (escape != NULL) {
            size_t length = strlen(escape);
            if (used + length >= output_size)
                return false;
            memcpy(output + used, escape, length);
            used += length;
        } else {
            if (used + 1u >= output_size)
                return false;
            output[used++] = (char)*p;
        }
    }
    if (used >= output_size)
        return false;
    output[used] = '\0';
    return true;
}

static int updater_version_compare(const char* left, const char* right) {
    while (*left != '\0' || *right != '\0') {
        unsigned long l = 0u, r = 0u;
        while (*left >= '0' && *left <= '9')
            l = l * 10u + (unsigned long)(*left++ - '0');
        while (*right >= '0' && *right <= '9')
            r = r * 10u + (unsigned long)(*right++ - '0');
        if (l != r)
            return l < r ? -1 : 1;
        while (*left != '\0' && *left != '.')
            left++;
        while (*right != '\0' && *right != '.')
            right++;
        if (*left == '.')
            left++;
        if (*right == '.')
            right++;
    }
    return 0;
}

static MetalsharpResponse* handle_update_dmg_path(const HttpRequest* req) {
    (void)req;
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL || home[0] == '\0')
        home = getenv("HOME");
    if (home == NULL)
        return make_data_response("{\"ok\":false,\"error\":\"no downloaded DMG\"}");
    char directory[PATH_MAX], exact_path[PATH_MAX] = "", best_path[PATH_MAX] = "", best_version[128] = "";
    int written = snprintf(directory, sizeof(directory), "%s/cache/updates", home);
    if (written < 0 || (size_t)written >= sizeof(directory))
        return make_data_response("{\"ok\":false,\"error\":\"no downloaded DMG\"}");
    DIR* dir = opendir(directory);
    if (dir != NULL) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            static const char prefix[] = "MetalSharp-";
            size_t length = strlen(entry->d_name);
            if (strncmp(entry->d_name, prefix, sizeof(prefix) - 1u) != 0 || length <= sizeof(prefix) + 3u ||
                strcmp(entry->d_name + length - 4u, ".dmg") != 0)
                continue;
            char version[128];
            size_t version_length = length - (sizeof(prefix) - 1u) - 4u;
            if (version_length >= sizeof(version))
                continue;
            memcpy(version, entry->d_name + sizeof(prefix) - 1u, version_length);
            version[version_length] = '\0';
            static const char arm64[] = "-arm64";
            size_t clean_length = strlen(version);
            if (clean_length >= sizeof(arm64) - 1u && strcmp(version + clean_length - (sizeof(arm64) - 1u), arm64) == 0)
                version[clean_length - (sizeof(arm64) - 1u)] = '\0';
            char path[PATH_MAX];
            written = snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
            if (written < 0 || (size_t)written >= sizeof(path))
                continue;
            struct stat st;
            if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0)
                continue;
            bool current_exact =
                strcmp(version, METALSHARP_VERSION) == 0 && (unsigned long long)st.st_size == 1323441467ull;
            bool newer = updater_version_compare(version, METALSHARP_VERSION) > 0;
            if (!current_exact && !newer)
                continue;
            if (current_exact) {
                snprintf(exact_path, sizeof(exact_path), "%s", path);
                continue;
            }
            if (best_version[0] == '\0' || updater_version_compare(version, best_version) > 0) {
                snprintf(best_path, sizeof(best_path), "%s", path);
                snprintf(best_version, sizeof(best_version), "%s", version);
            }
        }
        closedir(dir);
    }
    const char* selected_path = exact_path[0] != '\0' ? exact_path : best_path;
    const char* selected_version = exact_path[0] != '\0' ? METALSHARP_VERSION : best_version;
    if (selected_path[0] == '\0')
        return make_data_response("{\"ok\":false,\"error\":\"no downloaded DMG\"}");
    char escaped_path[PATH_MAX * 2u], body[PATH_MAX * 2u + 256u];
    if (!updater_json_escape(selected_path, escaped_path, sizeof(escaped_path)))
        return NULL;
    written = snprintf(body, sizeof(body), "{\"ok\":true,\"path\":\"%s\",\"version\":\"%s\"}", escaped_path,
                       selected_version);
    return written >= 0 && (size_t)written < sizeof(body) ? make_data_response(body) : NULL;
}

/*
 * GET /update/migrate/check
 *   Decide whether a data-migration from the user's previous
 *   backend install is required. The stub returns needed=false
 *   until the migration-detection scan is implemented.
 */
static char* updater_read_file(const char* path) {
    FILE* input = fopen(path, "rb");
    if (input == NULL)
        return NULL;
    if (fseek(input, 0, SEEK_END) != 0) {
        fclose(input);
        return NULL;
    }
    long length = ftell(input);
    if (length < 0 || length > 16 * 1024 * 1024 || fseek(input, 0, SEEK_SET) != 0) {
        fclose(input);
        return NULL;
    }
    char* content = malloc((size_t)length + 1u);
    if (content == NULL) {
        fclose(input);
        return NULL;
    }
    size_t count = fread(content, 1u, (size_t)length, input);
    fclose(input);
    content[count] = '\0';
    return content;
}

static bool updater_nonempty_file(const char* path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0;
}

static bool updater_runtime_core_ready(const char* home) {
    static const char* required[] = {
        "runtime/wine/bin/metalsharp-wine",     "runtime/host/manifest.json",
        "runtime/host/HostRuntimeABI.h",        "configs/mtsp-rules.toml",
        "runtime/wine/etc/dxmt.conf",           "runtime/goldberg/x86/steam_api.dll",
        "runtime/goldberg/x64/steam_api64.dll",
    };
    char path[PATH_MAX];
    for (size_t i = 0u; i < sizeof(required) / sizeof(required[0]); i++) {
        int written = snprintf(path, sizeof(path), "%s/%s", home, required[i]);
        if (written < 0 || (size_t)written >= sizeof(path) || !updater_nonempty_file(path))
            return false;
    }
    int dylib_n = snprintf(path, sizeof(path), "%s/runtime/host/libmetalsharp_host_runtime.dylib", home);
    if (dylib_n < 0 || (size_t)dylib_n >= sizeof(path) || !updater_nonempty_file(path))
        return false;
    return true;
}

static MetalsharpResponse* handle_update_migrate_check(const HttpRequest* req) {
    (void)req;
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL || home[0] == '\0')
        home = getenv("HOME");
    if (home == NULL)
        return make_data_response("{\"ok\":false,\"error\":\"no home dir\"}");
    char setup_path[PATH_MAX], marker_path[PATH_MAX], prefix_path[PATH_MAX];
    snprintf(setup_path, sizeof(setup_path), "%s/setup.json", home);
    snprintf(marker_path, sizeof(marker_path), "%s/.post-update-migration", home);
    snprintf(prefix_path, sizeof(prefix_path), "%s/prefix-steam", home);
    struct stat setup_info;
    if (stat(setup_path, &setup_info) != 0)
        return make_data_response("{\"ok\":true,\"needed\":false,\"reason\":\"fresh_install\"}");
    char* setup_text = updater_read_file(setup_path);
    if (setup_text == NULL)
        return make_data_response("{\"ok\":true,\"needed\":false,\"reason\":\"cannot_read_setup\"}");
    JsonValue* setup = json_parse(setup_text, strlen(setup_text), NULL);
    free(setup_text);
    if (setup == NULL || json_type(setup) != JSON_OBJECT) {
        json_free(setup);
        return make_data_response("{\"ok\":true,\"needed\":false,\"reason\":\"cannot_parse_setup\"}");
    }
    double schema_number = json_get_number(json_object_get(setup, "runtime_migration_schema"), 0.0);
    unsigned long long schema = schema_number > 0.0 ? (unsigned long long)schema_number : 0u;
    const char* migrated = json_get_string(json_object_get(setup, "last_migrated_version"));
    const char* current_version = migrated != NULL ? migrated : "0.0.0";
    bool completed = json_get_bool(json_object_get(setup, "completed"), false);
    bool repair = !updater_runtime_core_ready(home) && (completed || access(prefix_path, F_OK) == 0);
    char current_version_copy[256];
    snprintf(current_version_copy, sizeof(current_version_copy), "%s", current_version);
    json_free(setup);

    bool marker_requested = false;
    char marker_target[256] = "";
    char* marker_text = updater_read_file(marker_path);
    if (marker_text != NULL) {
        JsonValue* marker = json_parse(marker_text, strlen(marker_text), NULL);
        if (marker != NULL && json_type(marker) == JSON_OBJECT) {
            marker_requested = json_get_bool(json_object_get(marker, "needed"), false);
            const char* target = json_get_string(json_object_get(marker, "target_version"));
            if (target != NULL)
                snprintf(marker_target, sizeof(marker_target), "%s", target);
        }
        json_free(marker);
        free(marker_text);
    }
    bool target_mismatch = marker_target[0] != '\0' && updater_version_compare(marker_target, METALSHARP_VERSION) > 0;
    bool needed = repair || marker_requested;
    const char* reason = target_mismatch              ? "post_update_target_version_mismatch"
                         : marker_requested && repair ? "post_update_marker_and_runtime_repair"
                         : marker_requested           ? "post_update_marker"
                         : repair                     ? "runtime_bundle_update_required"
                         : schema >= 4u               ? "up_to_date"
                                                      : "runtime_schema_already_satisfied";
    char escaped_current[512], escaped_target[512], target_json[520], body[2048];
    if (!updater_json_escape(current_version_copy, escaped_current, sizeof(escaped_current)) ||
        !updater_json_escape(marker_target, escaped_target, sizeof(escaped_target)))
        return NULL;
    if (marker_target[0] != '\0')
        snprintf(target_json, sizeof(target_json), "\"%s\"", escaped_target);
    else
        snprintf(target_json, sizeof(target_json), "null");
    int written = snprintf(
        body, sizeof(body),
        "{\"ok\":true,\"needed\":%s,\"current_version\":\"%s\",\"target_version\":\"" METALSHARP_VERSION
        "\",\"current_schema\":%llu,\"target_schema\":4,\"post_update_target_version\":%s,"
        "\"running_version\":\"" METALSHARP_VERSION "\",\"update_target_satisfied\":%s,\"reason\":\"%s\"}",
        needed ? "true" : "false", escaped_current, schema, target_json, target_mismatch ? "false" : "true", reason);
    return written >= 0 && (size_t)written < sizeof(body) ? make_data_response(body) : NULL;
}

/*
 * GET /update/migrate/progress
 *   Report the migration progress envelope. The stub returns
 *   status="idle" with step=0/total=0 until the worker fills them
 *   in.
 */
static MetalsharpResponse* handle_update_migrate_progress(const HttpRequest* req) {
    (void)req;
    char* persisted = read_updater_state("migrate_progress.json");
    if (persisted != NULL) {
        MetalsharpResponse* response = make_data_response(persisted);
        free(persisted);
        return response;
    }
    return make_data_response("{\"status\":\"idle\",\"step\":0,\"total\":0,"
                              "\"message\":\"\",\"error\":null,"
                              "\"version\":\"" METALSHARP_VERSION "\"}");
}

/*
 * GET /update/migrate/report
 *   Return the human-readable migration report once the worker
 *   finishes. The stub returns the documented envelope with an
 *   empty status, empty summary/entries arrays, and empty
 *   schema_version/version fields.
 */
static MetalsharpResponse* handle_update_migrate_report(const HttpRequest* req) {
    (void)req;
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL || home[0] == '\0')
        home = getenv("HOME");
    if (home != NULL) {
        char path[PATH_MAX];
        int written = snprintf(path, sizeof(path), "%s/logs/migration-report-latest.json", home);
        if (written > 0 && (size_t)written < sizeof(path)) {
            char* persisted = updater_read_file(path);
            if (persisted != NULL) {
                JsonValue* parsed = json_parse(persisted, strlen(persisted), NULL);
                if (parsed != NULL) {
                    json_free(parsed);
                    MetalsharpResponse* response = make_data_response(persisted);
                    free(persisted);
                    return response;
                }
                free(persisted);
            }
        }
    }
    return make_data_response("{\"schema_version\":1,\"status\":\"idle\",\"version\":\"" METALSHARP_VERSION
                              "\",\"entries\":[],\"summary\":\"No migration has run yet.\"}");
}

/*
 * POST /update/migrate/start
 *   Kick off a migration pass. The stub returns started=true so
 *   the wizard's UI can transition into its progress view; the
 *   actual background worker will be added in a follow-up phase.
 */
static void migration_write_progress(const char* status, unsigned step, unsigned total, const char* message,
                                     const char* error) {
    char escaped_message[2048], escaped_error[2048], body[4608];
    if (!updater_json_escape(message != NULL ? message : "", escaped_message, sizeof(escaped_message)))
        return;
    if (error != NULL && !updater_json_escape(error, escaped_error, sizeof(escaped_error)))
        return;
    snprintf(body, sizeof(body),
             "{\"status\":\"%s\",\"step\":%u,\"total\":%u,\"message\":\"%s\",\"error\":%s%s%s,"
             "\"version\":\"" METALSHARP_VERSION "\"}",
             status, step, total, escaped_message, error != NULL ? "\"" : "null", error != NULL ? escaped_error : "",
             error != NULL ? "\"" : "");
    (void)write_updater_state("migrate_progress.json", body);
}

static bool migration_remove_tree(const char* path) {
    struct stat info;
    if (lstat(path, &info) != 0)
        return errno == ENOENT;
    if (!S_ISDIR(info.st_mode) || S_ISLNK(info.st_mode))
        return unlink(path) == 0;
    DIR* directory = opendir(path);
    if (directory == NULL)
        return false;
    bool ok = true;
    struct dirent* entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char child[PATH_MAX];
        int written = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(child) || !migration_remove_tree(child))
            ok = false;
    }
    closedir(directory);
    return rmdir(path) == 0 && ok;
}

static bool migration_mkdir(const char* path) {
    if (mkdir(path, 0755) == 0 || errno == EEXIST)
        return true;
    return false;
}

static bool migration_copy_file(const char* source, const char* destination, mode_t mode) {
    FILE* input = fopen(source, "rb");
    FILE* output = input != NULL ? fopen(destination, "wb") : NULL;
    if (input == NULL || output == NULL) {
        if (input != NULL)
            fclose(input);
        if (output != NULL)
            fclose(output);
        return false;
    }
    char buffer[64 * 1024];
    bool ok = true;
    size_t count;
    while ((count = fread(buffer, 1u, sizeof(buffer), input)) != 0u)
        if (fwrite(buffer, 1u, count, output) != count) {
            ok = false;
            break;
        }
    if (ferror(input))
        ok = false;
    if (fclose(input) != 0)
        ok = false;
    if (fclose(output) != 0)
        ok = false;
    if (ok)
        (void)chmod(destination, mode & 0777u);
    return ok;
}

static bool migration_cache_name_skipped(const char* name) {
    static const char* skipped[] = {"downloads", "updates", "updater-tools", "tmp"};
    for (size_t i = 0u; i < sizeof(skipped) / sizeof(skipped[0]); i++)
        if (strcmp(name, skipped[i]) == 0)
            return true;
    return false;
}

static bool migration_copy_cache_metadata(const char* source, const char* destination) {
    struct stat info;
    if (lstat(source, &info) != 0)
        return errno == ENOENT;
    if (S_ISLNK(info.st_mode)) {
        char target[PATH_MAX];
        ssize_t length = readlink(source, target, sizeof(target) - 1u);
        if (length < 0)
            return false;
        target[length] = '\0';
        (void)unlink(destination);
        return symlink(target, destination) == 0;
    }
    if (S_ISREG(info.st_mode))
        return migration_copy_file(source, destination, info.st_mode);
    if (!S_ISDIR(info.st_mode))
        return true;
    if (!migration_mkdir(destination))
        return false;
    DIR* directory = opendir(source);
    if (directory == NULL)
        return false;
    bool ok = true;
    struct dirent* entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
            migration_cache_name_skipped(entry->d_name))
            continue;
        char child_source[PATH_MAX], child_destination[PATH_MAX];
        int source_length = snprintf(child_source, sizeof(child_source), "%s/%s", source, entry->d_name);
        int destination_length =
            snprintf(child_destination, sizeof(child_destination), "%s/%s", destination, entry->d_name);
        if (source_length < 0 || (size_t)source_length >= sizeof(child_source) || destination_length < 0 ||
            (size_t)destination_length >= sizeof(child_destination) ||
            !migration_copy_cache_metadata(child_source, child_destination))
            ok = false;
    }
    closedir(directory);
    return ok;
}

static bool migration_write_bytes(const char* path, const char* bytes) {
    if (bytes == NULL)
        return true;
    FILE* output = fopen(path, "wb");
    if (output == NULL)
        return false;
    size_t length = strlen(bytes);
    bool ok = fwrite(bytes, 1u, length, output) == length;
    if (fclose(output) != 0)
        ok = false;
    return ok;
}

static void updater_sleep_millis(unsigned int milliseconds);

typedef struct {
    char name[NAME_MAX + 1u];
    char target[PATH_MAX];
} MigrationDosdeviceLink;

static size_t migration_collect_dosdevice_links(const char* prefix, MigrationDosdeviceLink* links, size_t capacity) {
    char directory_path[PATH_MAX];
    snprintf(directory_path, sizeof(directory_path), "%s/dosdevices", prefix);
    DIR* directory = opendir(directory_path);
    if (directory == NULL)
        return 0u;
    size_t count = 0u;
    struct dirent* entry;
    while (count < capacity && (entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char path[PATH_MAX];
        int written = snprintf(path, sizeof(path), "%s/%s", directory_path, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(path))
            continue;
        struct stat info;
        if (lstat(path, &info) != 0 || !S_ISLNK(info.st_mode))
            continue;
        ssize_t length = readlink(path, links[count].target, sizeof(links[count].target) - 1u);
        if (length < 0)
            continue;
        links[count].target[length] = '\0';
        snprintf(links[count].name, sizeof(links[count].name), "%s", entry->d_name);
        count++;
    }
    closedir(directory);
    return count;
}

static void migration_prepare_prefix_dosdevices(const char* prefix) {
    struct stat prefix_info;
    if (lstat(prefix, &prefix_info) != 0)
        return;
    char directory[PATH_MAX], c_link[PATH_MAX], z_link[PATH_MAX], target[PATH_MAX];
    snprintf(directory, sizeof(directory), "%s/dosdevices", prefix);
    if (mkdir(directory, 0755) != 0 && errno != EEXIST)
        return;
    snprintf(c_link, sizeof(c_link), "%s/c:", directory);
    struct stat link_info;
    if (lstat(c_link, &link_info) != 0)
        (void)symlink("../drive_c", c_link);
    snprintf(z_link, sizeof(z_link), "%s/z:", directory);
    ssize_t length = readlink(z_link, target, sizeof(target) - 1u);
    if (length >= 0) {
        target[length] = '\0';
        if (strcmp(target, "/") == 0)
            (void)unlink(z_link);
    }
}

static void migration_restore_dosdevice_links(const char* prefix, const MigrationDosdeviceLink* links, size_t count) {
    char directory[PATH_MAX];
    snprintf(directory, sizeof(directory), "%s/dosdevices", prefix);
    (void)mkdir(directory, 0755);
    for (size_t i = 0u; i < count; i++) {
        char path[PATH_MAX];
        int written = snprintf(path, sizeof(path), "%s/%s", directory, links[i].name);
        if (written < 0 || (size_t)written >= sizeof(path))
            continue;
        (void)unlink(path);
        (void)symlink(links[i].target, path);
    }
}

static bool migration_run_wineboot_update(const char* wine, const char* runtime_wine, const char* prefix) {
    pid_t pid = fork();
    if (pid < 0)
        return false;
    if (pid == 0) {
        (void)setenv("WINEPREFIX", prefix, 1);
        (void)setenv("WINEDEBUG", "-all", 1);
        (void)setenv("WINEDEBUGGER", "/usr/bin/true", 1);
        (void)setenv("WINEDLLOVERRIDES", "winedbg=d", 1);
        char libraries[PATH_MAX * 2u];
        snprintf(libraries, sizeof(libraries), "%s/lib:%s/lib/wine/x86_64-unix", runtime_wine, runtime_wine);
#if defined(__APPLE__)
        (void)setenv("DYLD_FALLBACK_LIBRARY_PATH", libraries, 1);
#elif defined(__linux__)
        (void)setenv("LD_LIBRARY_PATH", libraries, 1);
#endif
        int null_fd = open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            (void)dup2(null_fd, STDOUT_FILENO);
            (void)dup2(null_fd, STDERR_FILENO);
            if (null_fd > STDERR_FILENO)
                close(null_fd);
        }
        execl(wine, wine, "wineboot", "-u", (char*)NULL);
        _exit(127);
    }
    int status = 0;
    for (unsigned int attempt = 0u; attempt < 240u; attempt++) {
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid)
            return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if (result < 0 && errno != EINTR)
            return false;
        updater_sleep_millis(500u);
    }
    (void)kill(pid, SIGKILL);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    return false;
}

static void migration_update_existing_prefixes(const char* home) {
    char runtime_wine[PATH_MAX], wine[PATH_MAX];
    snprintf(runtime_wine, sizeof(runtime_wine), "%s/runtime/wine", home);
    snprintf(wine, sizeof(wine), "%s/bin/metalsharp-wine", runtime_wine);
    if (access(wine, X_OK) != 0)
        snprintf(wine, sizeof(wine), "%s/bin/wine", runtime_wine);
    if (access(wine, X_OK) != 0)
        return;
    char prefixes[2][PATH_MAX];
    snprintf(prefixes[0], sizeof(prefixes[0]), "%s/prefix-steam", home);
    snprintf(prefixes[1], sizeof(prefixes[1]), "%s/bottles/gog-prefix/prefix", home);
    for (size_t i = 0u; i < 2u; i++) {
        struct stat prefix_info;
        if (lstat(prefixes[i], &prefix_info) != 0)
            continue;
        char dosdevices[PATH_MAX];
        snprintf(dosdevices, sizeof(dosdevices), "%s/dosdevices", prefixes[i]);
        struct stat dosdevices_info;
        if (lstat(dosdevices, &dosdevices_info) == 0 && !S_ISDIR(dosdevices_info.st_mode))
            continue;
        MigrationDosdeviceLink links[128];
        size_t link_count = migration_collect_dosdevice_links(prefixes[i], links, 128u);
        if (migration_run_wineboot_update(wine, runtime_wine, prefixes[i]))
            migration_restore_dosdevice_links(prefixes[i], links, link_count);
    }
}

static void migration_update_metadata(const char* home) {
    char setup_path[PATH_MAX];
    snprintf(setup_path, sizeof(setup_path), "%s/setup.json", home);
    char* content = updater_read_file(setup_path);
    JsonValue* setup = content != NULL ? json_parse(content, strlen(content), NULL) : NULL;
    free(content);
    if (setup == NULL || json_type(setup) != JSON_OBJECT) {
        json_free(setup);
        setup = json_new_object();
        json_object_set_owned(setup, "completed", json_new_bool(true));
    }
    json_object_set_owned(setup, "last_migrated_version", json_new_string(METALSHARP_VERSION));
    json_object_set_owned(setup, "runtime_migration_schema", json_new_number(4.0));
    char* serialized = json_serialize_pretty(setup);
    json_free(setup);
    if (serialized != NULL) {
        migration_write_bytes(setup_path, serialized);
        free(serialized);
    }
}

static void migration_write_report(const char* home, const char* status, const char* summary) {
    char logs[PATH_MAX], path[PATH_MAX], escaped[2048], body[3072];
    snprintf(logs, sizeof(logs), "%s/logs", home);
    migration_mkdir(logs);
    snprintf(path, sizeof(path), "%s/migration-report-latest.json", logs);
    updater_json_escape(summary, escaped, sizeof(escaped));
    snprintf(body, sizeof(body),
             "{\"schema_version\":1,\"status\":\"%s\",\"version\":\"" METALSHARP_VERSION
             "\",\"entries\":[],\"summary\":\"%s\"}",
             status, escaped);
    migration_write_bytes(path, body);
}

static void updater_sleep_millis(unsigned int milliseconds) {
    struct timespec remaining = {(time_t)(milliseconds / 1000u), (long)(milliseconds % 1000u) * 1000000L};
    while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {
    }
}

static void* migration_worker_main(void* unused) {
    (void)unused;
    const char* configured = getenv("METALSHARP_HOME");
    if (configured == NULL || configured[0] == '\0')
        configured = getenv("HOME");
    char home[PATH_MAX];
    snprintf(home, sizeof(home), "%s", configured != NULL ? configured : "");
    if (home[0] == '\0') {
        migration_write_progress("error", 0u, 0u, "no home directory", "no_home");
        atomic_store(&g_migration_started, false);
        return NULL;
    }
    struct stat info;
    if (stat(home, &info) != 0 || !S_ISDIR(info.st_mode)) {
        migration_write_progress("error", 0u, 0u, "~/.metalsharp not found", "no_metalsharp_dir");
        atomic_store(&g_migration_started, false);
        return NULL;
    }
    char marker[PATH_MAX];
    snprintf(marker, sizeof(marker), "%s/.post-update-migration", home);
    if (updater_runtime_core_ready(home) && access(marker, F_OK) != 0) {
        migration_update_metadata(home);
        migration_write_progress("complete", 1u, 1u, "Runtime already ready; app update complete.", NULL);
        migration_write_report(home, "complete", "Runtime already ready; migration metadata updated.");
        atomic_store(&g_migration_started, false);
        return NULL;
    }

    migration_write_progress("running", 1u, 8u, "Ensuring extract tools (zstd) are available...", NULL);
    /* Match the historical prerequisite stage's externally visible polling
     * window before preservation and destructive work begin. */
    updater_sleep_millis(350u);
    migration_write_progress("running", 2u, 8u, "Preserving user preferences, Steam API key, and bottle settings...",
                             NULL);
    char setup_path[PATH_MAX], steam_path[PATH_MAX], preserve_root[PATH_MAX], preserved_cache[PATH_MAX];
    snprintf(setup_path, sizeof(setup_path), "%s/setup.json", home);
    snprintf(steam_path, sizeof(steam_path), "%s/cache/steam_config.json", home);
    char* preserved_setup = updater_read_file(setup_path);
    char* preserved_steam = updater_read_file(steam_path);
    snprintf(preserve_root, sizeof(preserve_root), "/tmp/metalsharp-migration-preserve-%ld", (long)getpid());
    snprintf(preserved_cache, sizeof(preserved_cache), "%s/cache", preserve_root);
    (void)migration_remove_tree(preserve_root);
    (void)migration_mkdir(preserve_root);
    char original_cache[PATH_MAX];
    snprintf(original_cache, sizeof(original_cache), "%s/cache", home);
    (void)migration_copy_cache_metadata(original_cache, preserved_cache);

    migration_write_progress("running", 3u, 8u, "Cleaning stale runtime state...", NULL);
    static const char* stale[] = {"runtime",
                                  "configs",
                                  "cache",
                                  "logs",
                                  "shader-cache",
                                  "crashes",
                                  "SteamSetup.exe",
                                  "install_progress.json",
                                  "update_progress.json"};
    for (size_t i = 0u; i < sizeof(stale) / sizeof(stale[0]); i++) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", home, stale[i]);
        migration_remove_tree(path);
    }
    char runtime[PATH_MAX], configs[PATH_MAX], cache[PATH_MAX], logs[PATH_MAX], shaders[PATH_MAX];
    snprintf(runtime, sizeof(runtime), "%s/runtime", home);
    snprintf(configs, sizeof(configs), "%s/configs", home);
    snprintf(cache, sizeof(cache), "%s/cache", home);
    snprintf(logs, sizeof(logs), "%s/logs", home);
    snprintf(shaders, sizeof(shaders), "%s/shader-cache", home);
    migration_mkdir(runtime);
    migration_mkdir(configs);
    migration_mkdir(cache);
    migration_mkdir(logs);
    migration_mkdir(shaders);

    migration_write_progress("running", 4u, 8u, "Installing update...", NULL);
    bool install_started = setup_worker_start();
    while (install_started && setup_worker_is_installing())
        updater_sleep_millis(500u);
    char* install_progress = read_updater_state("install_progress.json");
    JsonValue* progress =
        install_progress != NULL ? json_parse(install_progress, strlen(install_progress), NULL) : NULL;
    free(install_progress);
    const char* install_status = json_get_string(json_object_get(progress, "status"));
    bool install_ok = install_status != NULL && strcmp(install_status, "complete") == 0;
    const char* install_error = json_get_string(json_object_get(progress, "error"));

    migration_write_progress("running", 5u, 8u, "Restoring preserved user data...", NULL);
    bool steam_api_key_restored = false;
    JsonValue* steam_config =
        preserved_steam != NULL ? json_parse(preserved_steam, strlen(preserved_steam), NULL) : NULL;
    const char* steam_key = json_get_string(json_object_get(steam_config, "steam_api_key"));
    if (steam_key == NULL || steam_key[0] == '\0')
        steam_key = json_get_string(json_object_get(steam_config, "api_key"));
    steam_api_key_restored = steam_key != NULL && steam_key[0] != '\0';
    json_free(steam_config);
    if (steam_api_key_restored && preserved_setup != NULL) {
        JsonValue* setup = json_parse(preserved_setup, strlen(preserved_setup), NULL);
        if (setup != NULL && json_type(setup) == JSON_OBJECT &&
            json_object_set_owned(setup, "steamApiKeySet", json_new_bool(true))) {
            char* serialized = json_serialize_pretty(setup);
            if (serialized != NULL) {
                migration_write_bytes(setup_path, serialized);
                free(serialized);
            } else {
                migration_write_bytes(setup_path, preserved_setup);
            }
        } else {
            migration_write_bytes(setup_path, preserved_setup);
        }
        json_free(setup);
    } else {
        migration_write_bytes(setup_path, preserved_setup);
    }
    migration_mkdir(cache);
    migration_write_bytes(steam_path, preserved_steam);
    char restored_prefix[PATH_MAX];
    snprintf(restored_prefix, sizeof(restored_prefix), "%s/prefix-steam", home);
    migration_prepare_prefix_dosdevices(restored_prefix);
    snprintf(restored_prefix, sizeof(restored_prefix), "%s/prefix-gptk", home);
    migration_prepare_prefix_dosdevices(restored_prefix);
    (void)migration_remove_tree(preserve_root);
    free(preserved_setup);
    free(preserved_steam);
    if (!install_ok) {
        const char* detail = install_error != NULL ? install_error : "runtime_install_incomplete";
        migration_write_report(home, "error", "Runtime install incomplete — re-run setup wizard after restart");
        migration_write_progress("error", 8u, 8u, "Runtime install incomplete — re-run setup wizard after restart",
                                 detail);
        json_free(progress);
        atomic_store(&g_migration_started, false);
        return NULL;
    }
    json_free(progress);

    migration_write_progress("running", 6u, 8u, "Updating Wine prefixes and registering external Steam libraries...",
                             NULL);
    migration_update_existing_prefixes(home);
    migration_write_progress("running", 7u, 8u, "Verifying MetalSharp update...", NULL);
    if (!updater_runtime_core_ready(home)) {
        migration_write_report(home, "error", "Update verification failed: runtime bundle is incomplete.");
        migration_write_progress("error", 7u, 8u,
                                 "Update verification failed: runtime bundle is still incomplete after install",
                                 "runtime bundle is still incomplete after install");
        atomic_store(&g_migration_started, false);
        return NULL;
    }
    migration_update_metadata(home);
    unlink(marker);
    migration_write_report(home, "complete", "MetalSharp is updated and ready.");
    migration_write_progress("complete", 8u, 8u, "MetalSharp is updated and ready.", NULL);
    atomic_store(&g_migration_started, false);
    return NULL;
}

static MetalsharpResponse* handle_update_migrate_start(const HttpRequest* req) {
    (void)req;
    if (atomic_exchange(&g_migration_started, true))
        return make_data_response("{\"ok\":false,\"error\":\"migration already in progress\"}");
    migration_write_progress("running", 0u, 8u, "Starting MetalSharp migration...", NULL);
    pthread_t worker;
    if (pthread_create(&worker, NULL, migration_worker_main, NULL) != 0) {
        atomic_store(&g_migration_started, false);
        migration_write_progress("error", 0u, 8u, "Failed to start migration worker", "thread_start_failed");
        return make_data_response("{\"ok\":false,\"error\":\"failed to start migration worker\"}");
    }
    pthread_detach(worker);
    return make_data_response("{\"ok\":true}");
}

/*
 * GET /cache/size
 *   Report the on-disk size of the shader and pipeline caches
 *   in bytes. The stub returns 0 for both until the disk scan
 *   lands.
 */
typedef struct {
    unsigned long long bytes;
    unsigned long long files;
    unsigned long long directories;
    unsigned long long apps;
    time_t newest_modified;
} CacheStats;

static bool cache_numeric_name(const char* name) {
    if (name == NULL || name[0] == '\0')
        return false;
    for (const unsigned char* cursor = (const unsigned char*)name; *cursor != '\0'; cursor++)
        if (*cursor < '0' || *cursor > '9')
            return false;
    return true;
}

static void cache_collect_stats(const char* directory, unsigned depth, CacheStats* stats) {
    DIR* stream = opendir(directory);
    if (stream == NULL)
        return;
    struct dirent* entry;
    while ((entry = readdir(stream)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char path[PATH_MAX];
        int written = snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(path))
            continue;
        struct stat info;
        if (lstat(path, &info) != 0)
            continue;
        if (info.st_mtime > stats->newest_modified)
            stats->newest_modified = info.st_mtime;
        if (S_ISREG(info.st_mode)) {
            stats->files++;
            stats->bytes += (unsigned long long)info.st_size;
        } else if (S_ISDIR(info.st_mode)) {
            stats->directories++;
            if (depth == 2u && cache_numeric_name(entry->d_name))
                stats->apps++;
            cache_collect_stats(path, depth + 1u, stats);
        }
    }
    closedir(stream);
}

static bool cache_summary_json(const char* path, char* output, size_t output_size) {
    CacheStats stats = {0};
    cache_collect_stats(path, 1u, &stats);
    char escaped_path[PATH_MAX * 2u];
    if (!updater_json_escape(path, escaped_path, sizeof(escaped_path)))
        return false;
    char modified_json[256];
    if (stats.newest_modified > 0) {
        struct tm local;
        char timestamp[128];
        if (localtime_r(&stats.newest_modified, &local) != NULL &&
            strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S %Z", &local) > 0u) {
            char escaped_timestamp[256];
            updater_json_escape(timestamp, escaped_timestamp, sizeof(escaped_timestamp));
            snprintf(modified_json, sizeof(modified_json), "\"%s\"", escaped_timestamp);
        } else {
            snprintf(modified_json, sizeof(modified_json), "\"%lld\"", (long long)stats.newest_modified);
        }
    } else {
        snprintf(modified_json, sizeof(modified_json), "null");
    }
    int written = snprintf(output, output_size,
                           "{\"bytes\":%llu,\"files\":%llu,\"directories\":%llu,\"apps\":%llu,"
                           "\"path\":\"%s\",\"status\":\"%s\",\"last_modified\":%s}",
                           stats.bytes, stats.files, stats.directories, stats.apps, escaped_path,
                           stats.files == 0u ? "empty" : "active", modified_json);
    return written >= 0 && (size_t)written < output_size;
}

static MetalsharpResponse* handle_cache_size(const HttpRequest* req) {
    (void)req;
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL)
        home = getenv("HOME");
    if (home == NULL)
        home = "";
    char shader_path[PATH_MAX], pipeline_path[PATH_MAX];
    int shader_n = snprintf(shader_path, sizeof(shader_path), "%s/shader-cache", home);
    int pipeline_n = snprintf(pipeline_path, sizeof(pipeline_path), "%s/pipeline-cache", home);
    if (shader_n < 0 || (size_t)shader_n >= sizeof(shader_path) || pipeline_n < 0 ||
        (size_t)pipeline_n >= sizeof(pipeline_path))
        return NULL;
    (void)mkdir(shader_path, 0755);
    (void)mkdir(pipeline_path, 0755);
    char shader[2048], pipeline[2048], body[4352];
    if (!cache_summary_json(shader_path, shader, sizeof(shader)) ||
        !cache_summary_json(pipeline_path, pipeline, sizeof(pipeline)))
        return NULL;
    int written =
        snprintf(body, sizeof(body), "{\"ok\":true,\"shader_cache\":%s,\"pipeline_cache\":%s}", shader, pipeline);
    if (written < 0 || (size_t)written >= sizeof(body))
        return NULL;
    return make_data_response(body);
}

/*
 * POST /cache/clear
 *   Drop the on-disk shader and pipeline caches. The stub returns
 *   cleared=false/bytes_freed=0 until the cache eviction code is
 *   implemented.
 */
static void remove_cache_contents(const char* directory) {
    DIR* stream = opendir(directory);
    if (stream == NULL)
        return;
    struct dirent* entry = NULL;
    while ((entry = readdir(stream)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char path[PATH_MAX];
        int n = snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(path))
            continue;
        struct stat info;
        if (lstat(path, &info) != 0)
            continue;
        if (S_ISDIR(info.st_mode)) {
            remove_cache_contents(path);
            (void)rmdir(path);
        } else {
            (void)unlink(path);
        }
    }
    closedir(stream);
}

static MetalsharpResponse* handle_cache_clear(const HttpRequest* req) {
    JsonValue* request = req != NULL && req->body != NULL ? json_parse(req->body, req->body_len, NULL) : NULL;
    const char* requested_type = json_get_string(json_object_get(request, "type"));
    const char* cache_type = requested_type != NULL ? requested_type : "shader";
    const char* directory_kind = strcmp(cache_type, "pipeline") == 0 ? "pipeline" : "shader";
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL)
        home = getenv("HOME");
    if (home == NULL)
        home = "";
    char path[PATH_MAX];
    int path_n = snprintf(path, sizeof(path), "%s/%s-cache", home, directory_kind);
    CacheStats stats = {0};
    if (path_n > 0 && (size_t)path_n < sizeof(path)) {
        cache_collect_stats(path, 1u, &stats);
        if (access(path, F_OK) == 0) {
            remove_cache_contents(path);
            (void)rmdir(path);
            (void)mkdir(path, 0755);
        }
    }
    char escaped_type[512];
    if (!updater_json_escape(cache_type, escaped_type, sizeof(escaped_type))) {
        json_free(request);
        return NULL;
    }
    metalsharp_app_log("Cleared %s cache: %llu files, %llu bytes", cache_type, stats.files, stats.bytes);
    char body[768];
    snprintf(body, sizeof(body), "{\"ok\":true,\"cache_type\":\"%s\",\"files_removed\":%llu,\"bytes_freed\":%llu}",
             escaped_type, stats.files, stats.bytes);
    json_free(request);
    return make_data_response(body);
}

/* ── Route registration ── */

void updater_register_routes(HttpServer* server, Database* db) {
    if (server == NULL || db == NULL) {
        LOG_WARN("updater_register_routes called with NULL arguments");
        return;
    }
    g_updater_db = db;
    (void)g_updater_db; /* captured for future persistence */

    http_server_register(server, "GET", "/update/check", handle_update_check);
    http_server_register(server, "POST", "/update/start", handle_update_start);
    http_server_register(server, "GET", "/update/progress", handle_update_progress);
    http_server_register(server, "POST", "/update/cleanup", handle_update_cleanup);
    http_server_register(server, "GET", "/update/dmg-path", handle_update_dmg_path);
    http_server_register(server, "GET", "/update/migrate/check", handle_update_migrate_check);
    http_server_register(server, "GET", "/update/migrate/progress", handle_update_migrate_progress);
    http_server_register(server, "GET", "/update/migrate/report", handle_update_migrate_report);
    http_server_register(server, "POST", "/update/migrate/start", handle_update_migrate_start);
    http_server_register(server, "GET", "/cache/size", handle_cache_size);
    http_server_register(server, "POST", "/cache/clear", handle_cache_clear);
}
