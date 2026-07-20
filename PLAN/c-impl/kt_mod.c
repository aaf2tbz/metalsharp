/*
 * kt_mod.c — Kernel Translation: module root aggregator
 *
 * WHAT
 *   Root module for the kernel-translation (KT) subsystem. Holds
 *   no routes of its own; exists so a single registration entry
 *   point can fan out to every KT submodule (es_bridge, es_live,
 *   ipc_bridge, handle_table, handle_bridge, handle_callbacks,
 *   anti_debug, apc, code_integrity, driver_model, integration,
 *   probe, thread_notify, types, nt_to_xnu). Calling
 *   kt_mod_register_routes() at startup registers every KT route
 *   in one place.
 *
 * IMPORTS
 *   "server.h"        MetalsharpResponse, METALSHARP_VERSION
 *   "http_server.h"   HttpServer, HttpRequest, http_server_register
 *   "database.h"      Database (forwarded to each submodule)
 *
 * EXPORTS
 *   kt_mod_register_routes(HttpServer *server, Database *db)
 *       Register every kernel-translation submodule's routes on
 *       `server`. Each submodule receives `db` so future phases
 *       can persist state without re-plumbing. NULL arguments
 *       are a silent no-op.
 *
 * SCHEMA
 *   This file contributes zero routes. All kernel-translation
 *   routes live in the kt_*_register_routes functions defined in
 *   the sibling files. The aggregator only orchestrates them.
 */
#include "database.h"
#include "http_server.h"
#include "server.h"

#include <stdbool.h>
#include <stddef.h>

/* Forward declarations for every KT submodule registrar. */
extern void kt_es_bridge_register_routes(HttpServer* server, Database* db);
extern void kt_es_live_register_routes(HttpServer* server, Database* db);
extern void kt_ipc_bridge_register_routes(HttpServer* server, Database* db);
extern void kt_handle_table_register_routes(HttpServer* server, Database* db);
extern void kt_handle_bridge_register_routes(HttpServer* server, Database* db);
extern void kt_handle_callbacks_register_routes(HttpServer* server, Database* db);
extern void kt_anti_debug_register_routes(HttpServer* server, Database* db);
extern void kt_apc_register_routes(HttpServer* server, Database* db);
extern void kt_code_integrity_register_routes(HttpServer* server, Database* db);
extern void kt_driver_model_register_routes(HttpServer* server, Database* db);
extern void kt_integration_register_routes(HttpServer* server, Database* db);
extern void kt_probe_register_routes(HttpServer* server, Database* db);
extern void kt_thread_notify_register_routes(HttpServer* server, Database* db);

void kt_mod_register_routes(HttpServer* server, Database* db) {
    if (server == NULL || db == NULL) {
        return;
    }

    /* Order matches the user-facing surface area; harmless to reorder. */
    kt_es_bridge_register_routes(server, db);
    kt_es_live_register_routes(server, db);
    kt_ipc_bridge_register_routes(server, db);
    kt_handle_table_register_routes(server, db);
    kt_handle_bridge_register_routes(server, db);
    kt_handle_callbacks_register_routes(server, db);
    kt_anti_debug_register_routes(server, db);
    kt_apc_register_routes(server, db);
    kt_code_integrity_register_routes(server, db);
    kt_driver_model_register_routes(server, db);
    kt_integration_register_routes(server, db);
    kt_probe_register_routes(server, db);
    kt_thread_notify_register_routes(server, db);
}
