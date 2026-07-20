/*
 * platform.c — Platform helpers for the Metalsharp C backend
 *
 * WHAT
 *   Houses macOS / Darwin platform helpers used by every other
 *   backend module. The current surface is centred on
 *   `migrate_game_to_external`, which moves a previously-installed
 *   game's on-disk footprint from the bundled internal layout to
 *   an external user-visible path (typically under ~/Library so
 *   the user's data survives a Metalsharp reinstall). The route
 *   surface for this helper is wired by the installer_backend
 *   and bottles_backend modules; platform.c only owns the helper
 *   itself so a single source of truth governs the path-mapping
 *   rules across the backend.
 *
 * IMPORTS
 *   "server.h"        MetalsharpResponse, METALSHARP_VERSION, PATH_MAX
 *   "http_server.h"   HttpServer, Database forward declarations
 *   "database.h"      Database (forward declaration only)
 *   "logger.h"        LOG_INFO, LOG_WARN
 *   <stdbool.h>       bool, true, false
 *   <stddef.h>        size_t, NULL
 *   <stdio.h>         snprintf
 *   <stdlib.h>        calloc, free
 *   <string.h>        memcpy, strcpy, strlen, strncpy
 *
 * EXPORTS
 *   platform_register_routes(HttpServer *server, Database *db)
 *       No-op registration stub. platform.c owns no routes of its
 *       own — the migrate-to-external route is dispatched through
 *       installer_backend.c and bottles_backend.c — but the symbol
 *       is exported so the rest of the backend has a single
 *       registry point for platform-specific behaviour. Calling
 *       this function with NULL on either side is a silent no-op
 *       so a half-initialised backend cannot crash inside the
 *       registry. Logs a single confirmation line so the startup
 *       banner reveals the module is initialised.
 *
 *   bool migrate_game_to_external(const char* appid, const char* src_dir,
 *                                 char* dst_dir, size_t dst_len,
 *                                 char* error, size_t error_len)
 *       Move a previously-installed game's directory tree from
 *       the internal bundled layout to the external user-visible
 *       location. Returns true on success, false otherwise with
 *       `error` populated. The stub below always returns true with
 *       `dst_dir` set to the source path verbatim so downstream
 *       callers can keep their flow active without crashing.
 *
 * SCHEMA
 *   No persisted state. No JSON response surface. Route
 *   registrations will be added in a follow-up commit when the
 *   migrate-to-external endpoint moves out of installer_backend
 *   and bottles_backend and into this module.
 *
 * THREADING
 *   migrate_game_to_external is safe to call from multiple
 *   threads because the current stub only performs string copies
 *   into caller-owned buffers. The future implementation that
 *   actually performs file copies will need to serialise
 *   concurrent moves of the same appid.
 */

#include "database.h"
#include "http_server.h"
#include "logger.h"
#include "server.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Move a previously-installed game's on-disk footprint from the
 * internal bundled layout to the external user-visible location.
 *
 * The stub implementation copies the source path verbatim into
 * the caller's destination buffer and reports success so the
 * downstream installer / bottles flow can keep moving without
 * crashing. The real implementation will compute the external
 * path under ~/Library/Application Support/Metalsharp/games/<appid>
 * (matching the maintenance contract documented in
 * contracts/electron-backend.v1.json → migrate_game_to_external)
 * and atomically rename the source tree into place. Errors
 * encountered during the move populate the caller's `error`
 * buffer with a NUL-terminated diagnostic.
 */
bool migrate_game_to_external(const char* appid, const char* src_dir, char* dst_dir, size_t dst_len, char* error,
                              size_t error_len) {
    if (appid == NULL || src_dir == NULL || dst_dir == NULL || dst_len == 0) {
        if (error != NULL && error_len > 0) {
            snprintf(error, error_len, "migrate_game_to_external: invalid argument");
        }
        return false;
    }

    /* Stub: copy the source path verbatim into the destination
     * buffer. The real implementation will resolve the external
     * path and atomically rename the source tree. */
    size_t src_len = strlen(src_dir);
    if (src_len >= dst_len) {
        if (error != NULL && error_len > 0) {
            snprintf(error, error_len, "migrate_game_to_external: destination buffer too small");
        }
        return false;
    }
    memcpy(dst_dir, src_dir, src_len);
    dst_dir[src_len] = '\0';

    if (error != NULL && error_len > 0) {
        error[0] = '\0';
    }
    return true;
}

/*
 * Registration stub. platform.c currently owns no routes — the
 * migrate-to-external endpoint is dispatched through
 * installer_backend.c and bottles_backend.c — but the symbol is
 * exported so the rest of the backend has a single registry point
 * for platform-specific behaviour. The NULL checks guarantee a
 * missing server or database during teardown does not crash the
 * registry.
 */
void platform_register_routes(HttpServer* server, Database* db) {
    (void)db;
    if (server == NULL) {
        LOG_WARN("platform_register_routes called with NULL server");
        return;
    }
    LOG_INFO("platform routes registered (0 — helpers-only stub)");
}
