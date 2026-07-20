/*
 * main.c — Metalsharp backend entry point
 *
 * WHAT
 *   Process entry point for the metalsharp-backend binary. Reads
 *   environment, initializes every subsystem in dependency order,
 *   starts the HTTP server, and blocks until shutdown.
 *
 * IMPORTS
 *   server.h         shared types, version, pipeline enum
 *   logger.h         structured logging
 *   database.h       SQLite wrapper
 *   http_server.h    HTTP/1.1 server + route dispatch
 *   json.h           JSON parsing (for typed route bodies)
 *   config_parser.h  mtsp-rules pipeline configuration
 *   launcher.h       maintained-C launch policy
 *   bottles.h        maintained-C bottle policy
 *
 * EXPORTS
 *   int main(int argc, char **argv)   process entry point
 *
 * SCHEMA
 *   main() → logger_init → db_open → http_server_create →
 *   register_routes → http_server_run. On SIGINT/SIGTERM, sets
 *   shutdown flag; http_server_run returns, cleanup runs, exit(0).
 */
#include "database.h"
#include "http_server.h"
#include "logger.h"
#include "server.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Forward declarations for route handlers (defined in routes.c). */
extern void metalsharp_register_routes(HttpServer* server, Database* db);

/* ---- Signal handling ---- */

static volatile sig_atomic_t g_shutdown_requested = 0;

static void on_signal(int sig) {
    (void)sig;
    g_shutdown_requested = 1;
}

/* Return non-zero when shutdown is requested (for http_server_run). */
int metalsharp_shutdown_requested(void) {
    return g_shutdown_requested;
}

/* ---- Environment helpers ---- */

static const char* home_from_env(void) {
    const char* val = getenv("METALSHARP_HOME");
    if (val != NULL && val[0] != '\0')
        return val;
    val = getenv("HOME");
    return val;
}

static int port_from_env(void) {
    const char* val = getenv("METALSHARP_PORT");
    if (val == NULL || val[0] == '\0')
        return 0; /* OS-assigned */
    long port = 0;
    char* end = NULL;
    port = strtol(val, &end, 10);
    if (end == val || *end != '\0' || port < 0 || port > 65535)
        return 0;
    return (int)port;
}

/* ---- One-time initialization ---- */

static bool metalsharp_init(const char** home_out, int* port_out, Database** db_out, HttpServer** srv_out) {
    *db_out = NULL;
    *srv_out = NULL;

    /* 1. Logger — stderr by default, INFO level. */
    logger_init(stderr);

    /* 2. Home directory. */
    const char* home = home_from_env();
    if (home == NULL) {
        LOG_ERROR("METALSHARP_HOME and HOME are both unset; cannot start");
        return false;
    }
    *home_out = home;

    char db_path[PATH_MAX];
    int written = snprintf(db_path, sizeof(db_path), "%s/metalsharp.db", home);
    if (written < 0 || (size_t)written >= sizeof(db_path)) {
        LOG_ERROR("home path too long: %s", home);
        return false;
    }

    /* 3. Database. */
    *db_out = db_open(db_path);
    if (*db_out == NULL) {
        LOG_ERROR("cannot open database at %s", db_path);
        return false;
    }

    /* 4. HTTP server. */
    int port = port_from_env();
    *srv_out = http_server_create(port);
    if (*srv_out == NULL) {
        LOG_ERROR("cannot create HTTP server on port %d", port);
        db_close(*db_out);
        *db_out = NULL;
        return false;
    }
    *port_out = http_server_port(*srv_out);

    LOG_INFO("metalsharp-backend %s starting", METALSHARP_VERSION);
    LOG_INFO("  home: %s", home);
    LOG_INFO("  database: %s", db_path);
    LOG_INFO("  port: %d", *port_out);

    return true;
}

/* ---- Main ---- */

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    const char* home = NULL;
    int port = 0;
    Database* db = NULL;
    HttpServer* srv = NULL;

    if (!metalsharp_init(&home, &port, &db, &srv)) {
        LOG_ERROR("initialization failed");
        return 1;
    }

    /* Register all 264 routes (routes.c). */
    metalsharp_register_routes(srv, db);

    /* Signal handlers for graceful shutdown. */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    /* Announce port to Electron (it reads stdout). */
    printf("METALSHARP_PORT=%d\n", port);
    fflush(stdout);

    /* Block until shutdown signal or /kill route. */
    http_server_run(srv);

    LOG_INFO("shutting down");
    http_server_destroy(srv);
    db_close(db);
    LOG_INFO("shutdown complete");

    return 0;
}
