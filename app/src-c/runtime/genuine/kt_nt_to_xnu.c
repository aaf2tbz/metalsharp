/*
 * kt_nt_to_xnu.c — Kernel Translation: NT→XNU syscall translation
 *
 * WHAT
 *   Holds the NT→XNU syscall translation table (NtCreateFile →
 *   open(2), NtAllocateVirtualMemory → mach_vm_allocate, etc.).
 *   This stub does not yet route any HTTP surface; the route
 *   surface is exposed indirectly through kt_probe.c and
 *   kt_integration.c. A later phase wires the translation engine
 *   to the bottles backend and exposes it via dedicated routes.
 *
 * IMPORTS
 *   "server.h"        MetalsharpResponse, METALSHARP_VERSION
 *   "http_server.h"   HttpServer, HttpRequest, http_server_register
 *   "database.h"      Database (captured for future persistence)
 *
 * EXPORTS
 *   kt_nt_to_xnu_register_routes(HttpServer *server, Database *db)
 *       Register zero routes. Stub retains the registrar so
 *       kt_mod.c can iterate a uniform list.
 *
 * SCHEMA
 *   No routes. Module ships as a placeholder for the syscall
 *   translation engine.
 */
#include "database.h"
#include "http_server.h"
#include "server.h"

#include <stdbool.h>
#include <stddef.h>

static Database* g_kt_nt_to_xnu_db = NULL;

void kt_nt_to_xnu_register_routes(HttpServer* server, Database* db) {
    if (server == NULL || db == NULL) {
        return;
    }
    g_kt_nt_to_xnu_db = db;
    (void)g_kt_nt_to_xnu_db; /* captured for future persistence */
}
