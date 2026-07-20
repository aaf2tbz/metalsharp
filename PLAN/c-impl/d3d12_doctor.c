/*
 * d3d12_doctor.c — D3D12 runtime health monitor (stub)
 *
 * WHAT
 *   Companion module to the Steam runtime-doctor pipeline. The
 *   POST /steam/d3d12-runtime-doctor route currently lives in
 *   steam.c (registered against `handle_steam_stub_ok`) and the
 *   underlying diagnostic engine has not been ported yet. This
 *   file exists so:
 *
 *     1. The header / linker contract is established. Adding the
 *        real D3D12 health monitor (DXMT driver probe, Agility
 *        SDK version reconciliation, VCRT runtime inspection) is
 *        a matter of filling in the body of the registration
 *        stub once the engine module lands.
 *
 *     2. The shared route surface — should internal helpers need
 *        a place to attach (e.g. a /diagnostics/d3d12-doctor GET
 *        route or a worker that the steam route dispatches into)
 *        — has a single owner.
 *
 *     3. The contract that documents `void
 *        d3d12_doctor_register_routes(HttpServer*, Database*)`
 *        resolves to a real symbol so link errors appear at build
 *        time if the module is ever renamed or removed.
 *
 * IMPORTS
 *   "server.h"        MetalsharpResponse, METALSHARP_VERSION
 *   "http_server.h"   HttpServer, HttpRequest (forward declarations only)
 *   "database.h"      Database (forward declaration only; no SQL yet)
 *   "logger.h"        LOG_INFO (single announcement)
 *   <stdbool.h>       bool, true, false
 *   <stddef.h>        size_t, NULL
 *
 * EXPORTS
 *   d3d12_doctor_register_routes(HttpServer *server, Database *db)
 *       Registration stub. Currently registers no routes; the
 *       POST /steam/d3d12-runtime-doctor route is owned by
 *       steam.c (registered as `handle_steam_stub_ok`) and will
 *       be relocated here once the runtime-doctor engine is
 *       ported from the legacy Node backend. Calling this function
 *       with NULL on either side is a silent no-op so a half-
 *       initialised backend cannot crash inside the registry. Logs
 *       a single confirmation line so the startup banner reveals
 *       the module is initialised.
 *
 * SCHEMA
 *   No persisted state. No JSON response surface. Future routes
 *   (e.g. GET /diagnostics/d3d12-doctor) will be added in a
 *   follow-up commit when their response shapes are settled.
 *
 * THREADING
 *   The HTTP server dispatches every request on a worker thread.
 *   Because this module currently owns no routes and no mutable
 *   state, it is thread-safe by construction.
 */

#include "database.h"
#include "http_server.h"
#include "logger.h"
#include "server.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * Registration stub. Currently owns no routes: the
 * /steam/d3d12-runtime-doctor POST lives in steam.c and will be
 * migrated here when the real runtime-doctor engine lands. The
 * NULL checks guarantee a missing server or database during
 * teardown does not crash the registry.
 */
void d3d12_doctor_register_routes(HttpServer* server, Database* db) {
    if (server == NULL || db == NULL) {
        /* D3D12 doctor is optional: a missing handle simply
         * leaves steam.c's runtime-doctor stub in charge until
         * the real engine is wired in. */
        return;
    }
    LOG_INFO("d3d12 doctor routes registered (0 — stub)");
}
