/*
 * http_server.h — POSIX HTTP/1.1 server for the Metalsharp C backend
 *
 * WHAT
 *   Minimal, self-contained HTTP/1.1 server built directly on POSIX
 *   sockets (no libmicrohttpd or any external HTTP library). Binds
 *   to the loopback interface only, accepts client connections on a
 *   worker thread pool, parses the request line and Content-Length
 *   header, dispatches to a registered route handler, and writes
 *   back a JSON-serialized MetalsharpResponse envelope. Used as
 *   the network entry point for the routes served by the backend.
 *
 * IMPORTS
 *   "server.h"      MetalsharpResponse, MetalsharpRouteHandler
 *   "json.h"        JsonValue, json_serialize
 *   <stdbool.h>     bool, true, false
 *   <stddef.h>      size_t, NULL
 *   <stdint.h>      fixed-width integer types
 *
 * EXPORTS
 *   HttpServer          Opaque server handle
 *   HttpRequest         Parsed HTTP request (method, path, body, query)
 *   HttpRouteHandler    Route handler function pointer type
 *   http_server_create  Allocate server and bind to loopback:port
 *   http_server_port    Return the actual bound TCP port
 *   http_server_register Add a (method, path) -> handler binding
 *   http_server_run     Blocking accept loop; dispatches until stopped
 *   http_server_stop    Signal graceful shutdown
 *   http_server_destroy Stop the server and release every resource
 *
 * SCHEMA
 *   HttpServer is an opaque, heap-allocated handle that owns its
 *   listening socket, worker threads, route table, and pending-
 *   connection queue. http_server_create binds to 127.0.0.1:port;
 *   passing port == 0 lets the kernel pick a free port, which
 *   http_server_port then reports. Routes are inserted with
 *   http_server_register and matched exactly on (method, path);
 *   the query string is delivered to the handler as a separate
 *   field. The first Content-Length header (case-insensitive) is
 *   honoured when reading the request body. Route handlers return
 *   a heap-allocated MetalsharpResponse: ok indicates success,
 *   error_msg is a heap string used only when ok is false, and
 *   data is an opaque pointer that, when non-NULL, is interpreted
 *   as a JsonValue * and serialized under a top-level "data"
 *   field. http_server_run blocks until http_server_stop is
 *   invoked from another thread; http_server_destroy joins the
 *   workers and frees every resource owned by the server.
 */
#ifndef METALSHARP_HTTP_SERVER_H
#define METALSHARP_HTTP_SERVER_H

#include "json.h"
#include "server.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Parsed HTTP request handed to a route handler. All pointers
 * reference storage owned by the HTTP server and remain valid
 * only for the duration of the handler call; handlers must copy
 * any data they need to retain. method is one of "GET", "POST",
 * etc. path is the URL path with the query string stripped. body
 * points to a buffer of length body_len when the request carried
 * a body (Content-Length > 0), otherwise body is NULL and
 * body_len is 0. query is the portion of the URL after '?', or
 * NULL when no query string was supplied.
 */
typedef struct {
    const char* method;
    const char* path;
    const char* body;
    size_t body_len;
    const char* query;
} HttpRequest;

/*
 * Route handler signature. The handler receives a parsed request
 * and returns a heap-allocated MetalsharpResponse that the HTTP
 * layer serializes to JSON, sends back to the client, and frees.
 * The function pointer is shape-compatible with MetalsharpRouteHandler
 * in server.h; callers may cast between the two types when a
 * single handler is intended to be reused across modules.
 */
typedef MetalsharpResponse* (*HttpRouteHandler)(const HttpRequest* request);

/*
 * Opaque HTTP server. The full definition lives in http_server.c;
 * callers interact with instances exclusively through the
 * http_server_* functions declared below.
 */
typedef struct HttpServer HttpServer;

/*
 * Allocate a server bound to 127.0.0.1:port. Passing port == 0
 * lets the kernel assign an unused port; use http_server_port to
 * discover which port was actually chosen. SO_REUSEADDR is set
 * so the server can be restarted without a TIME_WAIT delay. The
 * worker thread pool is spawned before this call returns. Returns
 * NULL on any failure (socket, setsockopt, bind, listen, or
 * pthread_create for the worker pool); the caller has no resource
 * to release in that case.
 */
HttpServer* http_server_create(int port);

/*
 * Return the actual TCP port the server is listening on. After
 * http_server_create(0) this is the kernel-assigned port; after
 * http_server_create(N) it is N. Returns 0 if server is NULL.
 */
int http_server_port(const HttpServer* server);

/*
 * Register a route handler for the (method, path) pair. method and
 * path are copied into server-owned storage; the caller does not
 * need to keep the buffers alive. Path matching is exact and
 * case-sensitive. Duplicate (method, path) pairs keep the most
 * recently registered handler. Safe to call before http_server_run
 * from the main thread; concurrent registration from multiple
 * threads is serialised internally.
 */
void http_server_register(HttpServer* server, const char* method, const char* path, HttpRouteHandler handler);

/*
 * Run the server's accept loop on the calling thread. New client
 * connections are pushed to an internal queue and consumed by the
 * worker thread pool created in http_server_create. Returns when
 * http_server_stop is invoked from another thread (closing the
 * listening socket causes accept to return an error). Safe to
 * call once per server.
 */
void http_server_run(HttpServer* server);

/*
 * Request graceful shutdown of a running server. Closes the
 * listening socket, sets the shutdown flag, and broadcasts a
 * condition variable so every blocked worker exits. Safe to call
 * from any thread; safe to call multiple times. Does not free the
 * server; pair with http_server_destroy.
 */
void http_server_stop(HttpServer* server);

/*
 * Stop the server (if running) and release every resource owned
 * by it, including worker threads, the listening socket, the
 * pending-connection queue, and the route table. Passing NULL is
 * a no-op; calling twice on the same server is also a no-op.
 * Must not be called from inside a route handler running on a
 * worker thread.
 */
void http_server_destroy(HttpServer* server);

#endif /* METALSHARP_HTTP_SERVER_H */