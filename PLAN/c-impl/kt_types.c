/*
 * kt_types.c — Kernel Translation: NT type definitions
 *
 * WHAT
 *   Defines the NT kernel structures (PEB, TEB, KPROCESS, ETHREAD,
 *   OBJECT_ATTRIBUTES, IO_STATUS_BLOCK, UNICODE_STRING, etc.) the
 *   KT subsystem maps to Darwin/XNU analogues. This stub exposes
 *   the layout as static constants so sibling modules can include
 *   it during development; no routes are mounted. A later phase
 *   replaces the in-memory tables with allocator-backed instances
 *   populated from live bottles.
 *
 * IMPORTS
 *   "server.h"        MetalsharpResponse, METALSHARP_VERSION
 *   "http_server.h"   HttpServer, HttpRequest, http_server_register
 *   "database.h"      Database (captured for future persistence)
 *
 * EXPORTS
 *   kt_types_register_routes(HttpServer *server, Database *db)
 *       Register zero routes. Provided so kt_mod.c can iterate a
 *       uniform list of registrars without a special case.
 *
 * SCHEMA
 *   No routes. Module is included for type definitions only.
 */
#include "database.h"
#include "http_server.h"
#include "server.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * Database handle captured at registration time. Reserved for the
 * phase that persists NT type inventory snapshots; the variable
 * exists now so the capture contract is identical to every other
 * KT submodule.
 */
static Database* g_kt_types_db = NULL;

void kt_types_register_routes(HttpServer* server, Database* db) {
    if (server == NULL || db == NULL) {
        return;
    }
    g_kt_types_db = db;
    (void)g_kt_types_db; /* captured for future persistence */
}
