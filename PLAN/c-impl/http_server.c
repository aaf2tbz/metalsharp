/*
 * http_server.c — POSIX HTTP/1.1 server for the Metalsharp C backend
 *
 * WHAT
 *   Self-contained HTTP/1.1 server using POSIX sockets (socket,
 *   bind, listen, accept). The server binds to 127.0.0.1 only,
 *   sets SO_REUSEADDR for quick restart, ignores SIGPIPE so
 *   writes to closed peers surface as EPIPE on send, and runs a
 *   fixed pool of worker threads (HTTP_SERVER_WORKERS) that pull
 *   accepted connections from a bounded, mutex-protected ring
 *   buffer. The accept thread blocks in http_server_run; calling
 *   http_server_stop from another thread closes the listening
 *   socket and broadcasts a condition variable so every worker
 *   exits cleanly. Each connection is read into a growable header
 *   buffer, parsed for the request line and Content-Length,
 *   dispatched to the registered route handler, and answered with
 *   a JSON-serialized MetalsharpResponse envelope. Unmatched
 *   routes return 404 with {"ok":false,"error":"not found"}.
 *
 * IMPORTS
 *   "http_server.h"   public interface
 *   "server.h"        MetalsharpResponse (transitive)
 *   "json.h"          JsonValue, json_serialize
 *   <errno.h>         errno, EINTR
 *   <netinet/in.h>    sockaddr_in, htons, ntohs
 *   <arpa/inet.h>     inet_pton
 *   <pthread.h>       pthread_* primitives
 *   <signal.h>        SIGPIPE, SIG_IGN
 *   <stdio.h>         snprintf
 *   <stdlib.h>        malloc, free, strdup, strtol
 *   <string.h>        memcpy, strlen, strcmp, strchr, strstr
 *   <strings.h>       strncasecmp (POSIX)
 *   <sys/socket.h>    socket, bind, listen, accept, recv, send,
 *                     shutdown, setsockopt, getsockname,
 *                     SO_REUSEADDR, SHUT_RDWR
 *   <sys/types.h>     general POSIX types
 *   <unistd.h>        close
 *
 * EXPORTS
 *   http_server_create    bind listening socket and spawn workers
 *   http_server_port      read bound port
 *   http_server_register  add a (method, path) -> handler binding
 *   http_server_run       blocking accept loop
 *   http_server_stop      request shutdown
 *   http_server_destroy   stop + free
 *
 * SCHEMA
 *   The route table is a dynamic array of {method, path, handler}
 *   tuples protected by a mutex. http_server_register appends a
 *   new entry; route lookup is a linear scan guarded by the same
 *   mutex. Workers consume client file descriptors from a fixed-
 *   capacity ring buffer guarded by a separate mutex + condition
 *   variable. When http_server_stop is called the shutdown flag
 *   is set under the queue mutex, the condition variable is
 *   broadcast, and the listening socket is closed; workers re-
 *   check the flag on wakeup and exit when set.
 */

#define _POSIX_C_SOURCE 200809L

#include "http_server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/* Number of worker threads serving accepted connections. */
#define HTTP_SERVER_WORKERS 4

/* Initial route table capacity; grows by doubling on overflow. */
#define HTTP_SERVER_ROUTES_INITIAL_CAP 16

/* Pending-connection queue capacity. */
#define HTTP_SERVER_QUEUE_CAP 64

/* Maximum HTTP header bytes we buffer per connection. */
#define HTTP_SERVER_HEADER_MAX (64 * 1024)

/* Maximum request body bytes we accept per connection. */
#define HTTP_SERVER_BODY_MAX (16 * 1024 * 1024)

/*
 * Route table entry. method and path are heap-allocated and
 * owned by the server; handler is an immutable function pointer
 * supplied by the caller.
 */
typedef struct {
    char* method;
    char* path;
    HttpRouteHandler handler;
} Route;

/*
 * Concrete HttpServer. The struct is opaque to callers; every
 * field is initialised in http_server_create and torn down in
 * http_server_destroy.
 */
struct HttpServer {
    int listen_fd;  /* listening socket, -1 when closed */
    int bound_port; /* actual bound port (post-bind)   */
    pthread_t workers[HTTP_SERVER_WORKERS];
    bool workers_started;

    pthread_mutex_t routes_lock;
    Route* routes;
    size_t route_count;
    size_t route_capacity;

    pthread_mutex_t queue_lock;
    pthread_cond_t queue_cond;
    int queue[HTTP_SERVER_QUEUE_CAP];
    size_t queue_head; /* index of next slot to read */
    size_t queue_tail; /* index of next slot to write */
    size_t queue_size; /* number of entries currently in queue */

    volatile int shutdown_flag; /* protected by queue_lock */
    bool destroyed;
};

/*
 * Module-level initialisation: ignore SIGPIPE so writes to a
 * peer that has closed its end of the connection return EPIPE
 * rather than terminating the process. Idempotent; called once
 * from http_server_create.
 */
static void install_sigpipe_handler(void) {
    signal(SIGPIPE, SIG_IGN);
}

/*
 * Push a client file descriptor onto the pending queue. Blocks
 * while the queue is full so the accept thread applies back-
 * pressure to the kernel. Wakes one waiting worker on success.
 * When the shutdown flag is already set, closes the fd and
 * returns without queueing.
 */
static void queue_push(HttpServer* s, int client_fd) {
    pthread_mutex_lock(&s->queue_lock);
    while (s->queue_size == HTTP_SERVER_QUEUE_CAP && !s->shutdown_flag) {
        pthread_cond_wait(&s->queue_cond, &s->queue_lock);
    }
    if (s->shutdown_flag) {
        pthread_mutex_unlock(&s->queue_lock);
        if (client_fd >= 0)
            close(client_fd);
        return;
    }
    s->queue[s->queue_tail] = client_fd;
    s->queue_tail = (s->queue_tail + 1) % HTTP_SERVER_QUEUE_CAP;
    s->queue_size += 1;
    pthread_cond_signal(&s->queue_cond);
    pthread_mutex_unlock(&s->queue_lock);
}

/*
 * Pop a client file descriptor from the pending queue. Blocks
 * while the queue is empty until either a connection arrives or
 * the server is shut down. Returns -1 to signal shutdown; the
 * caller exits its loop.
 */
static int queue_pop(HttpServer* s) {
    pthread_mutex_lock(&s->queue_lock);
    while (s->queue_size == 0 && !s->shutdown_flag) {
        pthread_cond_wait(&s->queue_cond, &s->queue_lock);
    }
    if (s->queue_size == 0 && s->shutdown_flag) {
        pthread_mutex_unlock(&s->queue_lock);
        return -1;
    }
    int fd = s->queue[s->queue_head];
    s->queue_head = (s->queue_head + 1) % HTTP_SERVER_QUEUE_CAP;
    s->queue_size -= 1;
    pthread_cond_signal(&s->queue_cond);
    pthread_mutex_unlock(&s->queue_lock);
    return fd;
}

/*
 * Find the route matching method and path exactly. Caller must
 * hold s->routes_lock for the duration of the call. Returns the
 * matching handler, or NULL when no route is registered for the
 * pair.
 */
static HttpRouteHandler route_lookup_locked(HttpServer* s, const char* method, const char* path) {
    for (size_t i = 0; i < s->route_count; i++) {
        if (strcmp(s->routes[i].method, method) == 0 && strcmp(s->routes[i].path, path) == 0) {
            return s->routes[i].handler;
        }
    }
    return NULL;
}

/*
 * JSON-escape a NUL-terminated string into a newly allocated
 * buffer. The caller frees the returned buffer. Returns NULL on
 * allocation failure. The input is treated as a C string; embedded
 * NUL bytes are not preserved.
 */
static char* json_escape(const char* s) {
    if (!s)
        return NULL;
    size_t in_len = strlen(s);
    size_t cap = in_len * 6 + 8;
    char* out = (char*)malloc(cap);
    if (!out)
        return NULL;
    size_t i = 0;
    for (size_t k = 0; k < in_len; k++) {
        unsigned char c = (unsigned char)s[k];
        if (i + 8 >= cap) {
            cap *= 2;
            char* grown = (char*)realloc(out, cap);
            if (!grown) {
                free(out);
                return NULL;
            }
            out = grown;
        }
        switch (c) {
        case '"':
            out[i++] = '\\';
            out[i++] = '"';
            break;
        case '\\':
            out[i++] = '\\';
            out[i++] = '\\';
            break;
        case '\b':
            out[i++] = '\\';
            out[i++] = 'b';
            break;
        case '\f':
            out[i++] = '\\';
            out[i++] = 'f';
            break;
        case '\n':
            out[i++] = '\\';
            out[i++] = 'n';
            break;
        case '\r':
            out[i++] = '\\';
            out[i++] = 'r';
            break;
        case '\t':
            out[i++] = '\\';
            out[i++] = 't';
            break;
        default:
            if (c < 0x20) {
                int n = snprintf(out + i, cap - i, "\\u%04x", c);
                if (n < 0) {
                    free(out);
                    return NULL;
                }
                i += (size_t)n;
            } else {
                out[i++] = (char)c;
            }
            break;
        }
    }
    out[i] = '\0';
    return out;
}

/*
 * Serialize a MetalsharpResponse to a heap-allocated JSON object
 * string. The returned buffer is NUL-terminated and suitable for
 * sending verbatim in the HTTP response body; the caller frees
 * it. Returns NULL on allocation failure. When resp->data is
 * non-NULL it is cast to JsonValue * and serialized under a top-
 * level "data" field per the documented schema.
 */
static char* serialize_response(const MetalsharpResponse* resp) {
    if (!resp) {
        return strdup("{\"ok\":false,\"error\":\"internal error\"}");
    }

    char* esc_err = NULL;
    if (!resp->ok) {
        const char* msg = resp->error_msg ? resp->error_msg : "error";
        esc_err = json_escape(msg);
        if (!esc_err)
            return NULL;
    }

    char* data_json = NULL;
    if (resp->data) {
        data_json = json_serialize((const JsonValue*)resp->data);
        if (!data_json) {
            free(esc_err);
            return NULL;
        }
    }

    size_t total = 16;
    if (esc_err)
        total += strlen(esc_err) + 16;
    if (data_json)
        total += strlen(data_json) + 16;

    char* out = (char*)malloc(total);
    if (!out) {
        free(esc_err);
        free(data_json);
        return NULL;
    }

    size_t off = 0;
    int n = snprintf(out + off, total - off, "{\"ok\":%s", resp->ok ? "true" : "false");
    if (n < 0 || (size_t)n >= total - off) {
        free(out);
        free(esc_err);
        free(data_json);
        return NULL;
    }
    off += (size_t)n;

    if (esc_err) {
        n = snprintf(out + off, total - off, ",\"error\":\"%s\"", esc_err);
        if (n < 0 || (size_t)n >= total - off) {
            free(out);
            free(esc_err);
            free(data_json);
            return NULL;
        }
        off += (size_t)n;
    }
    if (data_json) {
        n = snprintf(out + off, total - off, ",\"data\":%s", data_json);
        if (n < 0 || (size_t)n >= total - off) {
            free(out);
            free(esc_err);
            free(data_json);
            return NULL;
        }
        off += (size_t)n;
    }
    if (off + 2 > total) {
        free(out);
        free(esc_err);
        free(data_json);
        return NULL;
    }
    out[off++] = '}';
    out[off] = '\0';

    free(esc_err);
    free(data_json);
    return out;
}

/*
 * Send `len` bytes from `buf` to `fd`, looping over partial
 * writes. Returns true on success, false on any send error
 * (peer closed, EPIPE, EINVAL). EINTR is retried transparently.
 */
static bool send_all(int fd, const void* buf, size_t len) {
    const char* p = (const char*)buf;
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, p + off, len - off, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (n == 0)
            return false;
        off += (size_t)n;
    }
    return true;
}

/*
 * Build a complete HTTP response (status line, headers, blank
 * line, body) and write it to fd. Returns true on success, false
 * on any send failure. status is the numeric HTTP status; reason
 * is its textual reason phrase; body is the JSON body (may be
 * empty). The Content-Length is computed from strlen(body).
 */
static bool write_http_response(int fd, int status, const char* reason, const char* body) {
    size_t body_len = body ? strlen(body) : 0;
    char header[512];
    int n = snprintf(header, sizeof(header),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: application/json\r\n"
                     "Content-Length: %zu\r\n"
                     "\r\n",
                     status, reason, body_len);
    if (n < 0 || (size_t)n >= sizeof(header))
        return false;
    if (!send_all(fd, header, (size_t)n))
        return false;
    if (body_len > 0 && !send_all(fd, body, body_len))
        return false;
    return true;
}

/*
 * Send a pre-built 404 not-found response. Body is a constant
 * string so no allocation is needed.
 */
static void send_not_found(int fd) {
    static const char body[] = "{\"ok\":false,\"error\":\"not found\"}";
    write_http_response(fd, 404, "Not Found", body);
}

/*
 * Send a 400 bad-request response with a JSON body describing the
 * failure. The message is JSON-escaped before transmission.
 */
static void send_bad_request(int fd, const char* msg) {
    char* escaped = json_escape(msg ? msg : "bad request");
    if (!escaped)
        return;
    size_t cap = strlen(escaped) + 64;
    char* body = (char*)malloc(cap);
    if (!body) {
        free(escaped);
        return;
    }
    snprintf(body, cap, "{\"ok\":false,\"error\":\"%s\"}", escaped);
    write_http_response(fd, 400, "Bad Request", body);
    free(body);
    free(escaped);
}

/*
 * Send a 500 internal-error response with a JSON body.
 */
static void send_internal_error(int fd) {
    static const char body[] = "{\"ok\":false,\"error\":\"internal error\"}";
    write_http_response(fd, 500, "Internal Server Error", body);
}

/*
 * Look up a header value (case-insensitive name) inside a NUL-
 * terminated header block. The scan stops at the first empty
 * line, which marks the end of the HTTP headers and prevents
 * accidental matches against body bytes that resemble a header.
 * Returns a pointer into the block at the first non-whitespace
 * character of the value, or NULL when the header is absent.
 * The returned pointer remains valid until the buffer is freed.
 */
static const char* find_header(const char* headers, const char* name) {
    if (!headers || !name)
        return NULL;
    size_t nlen = strlen(name);
    const char* p = headers;
    while (*p) {
        const char* eol = strstr(p, "\r\n");
        if (!eol)
            break;
        size_t llen = (size_t)(eol - p);
        if (llen == 0)
            break;
        if (llen > nlen + 1 && p[nlen] == ':' && strncasecmp(p, name, nlen) == 0) {
            const char* v = p + nlen + 1;
            while (*v == ' ' || *v == '\t')
                v++;
            return v;
        }
        p = eol + 2;
    }
    return NULL;
}

/*
 * Parse the first line of an HTTP request into method, path, and
 * query. method_out and path_out receive heap-allocated copies
 * the caller frees. query_out, when non-NULL, points into
 * path_out (no separate free). Returns true on success, false
 * on malformed input or allocation failure. Paths longer than
 * PATH_MAX are rejected.
 */
static bool parse_request_line(const char* line, char** method_out, char** path_out, const char** query_out) {
    *method_out = NULL;
    *path_out = NULL;
    if (query_out)
        *query_out = NULL;

    const char* sp1 = strchr(line, ' ');
    if (!sp1)
        return false;
    const char* sp2 = strchr(sp1 + 1, ' ');
    if (!sp2)
        return false;

    size_t method_len = (size_t)(sp1 - line);
    size_t path_len = (size_t)(sp2 - sp1 - 1);
    if (method_len == 0 || path_len == 0)
        return false;
    if (path_len > PATH_MAX)
        return false;

    char* method = (char*)malloc(method_len + 1);
    char* path = (char*)malloc(path_len + 1);
    if (!method || !path) {
        free(method);
        free(path);
        return false;
    }
    memcpy(method, line, method_len);
    method[method_len] = '\0';
    memcpy(path, sp1 + 1, path_len);
    path[path_len] = '\0';

    char* q = strchr(path, '?');
    if (q) {
        *q = '\0';
        if (query_out)
            *query_out = q + 1;
    }

    *method_out = method;
    *path_out = path;
    return true;
}

/*
 * Read the request line and headers from fd into a newly
 * allocated buffer. Returns the buffer (NUL-terminated) and
 * stores the byte offset of the first body byte in *body_offset,
 * the total bytes received in *total_len, and the parsed
 * Content-Length in *content_length (0 when absent or invalid).
 * Returns NULL on read failure, header overflow past
 * HTTP_SERVER_HEADER_MAX, or allocation failure.
 */
static char* read_headers(int fd, size_t* body_offset, size_t* content_length, size_t* total_len) {
    size_t cap = 4096;
    size_t len = 0;
    char* buf = (char*)malloc(cap);
    if (!buf)
        return NULL;

    size_t header_end = 0;
    for (;;) {
        if (len + 1 >= cap) {
            if (cap >= HTTP_SERVER_HEADER_MAX) {
                free(buf);
                return NULL;
            }
            size_t newcap = cap * 2;
            if (newcap > HTTP_SERVER_HEADER_MAX)
                newcap = HTTP_SERVER_HEADER_MAX;
            char* grown = (char*)realloc(buf, newcap);
            if (!grown) {
                free(buf);
                return NULL;
            }
            buf = grown;
            cap = newcap;
        }
        ssize_t n;
        do {
            n = recv(fd, buf + len, cap - 1 - len, 0);
        } while (n < 0 && errno == EINTR);
        if (n <= 0) {
            free(buf);
            return NULL;
        }
        len += (size_t)n;
        buf[len] = '\0';
        char* end = strstr(buf, "\r\n\r\n");
        if (end) {
            header_end = (size_t)(end - buf) + 4;
            break;
        }
    }

    *body_offset = header_end;
    *total_len = len;

    const char* cl = find_header(buf, "Content-Length");
    if (cl) {
        char* endp = NULL;
        long v = strtol(cl, &endp, 10);
        if (endp != cl && v >= 0) {
            *content_length = (size_t)v;
        } else {
            *content_length = 0;
        }
    } else {
        *content_length = 0;
    }

    return buf;
}

/*
 * Read the request body from fd into a newly allocated, NUL-
 * terminated buffer. already_buf / already_have describe body
 * bytes already buffered past the header terminator inside the
 * headers block; total is the parsed Content-Length (capped at
 * HTTP_SERVER_BODY_MAX). Sets *out_len to the actual byte count
 * read (may be less than total if the peer closed early).
 * Returns NULL on allocation failure.
 */
static char* read_body(int fd, const char* already_buf, size_t already_have, size_t total, size_t* out_len) {
    *out_len = 0;
    if (total == 0)
        return NULL;
    if (total > HTTP_SERVER_BODY_MAX)
        total = HTTP_SERVER_BODY_MAX;

    char* body = (char*)malloc(total + 1);
    if (!body)
        return NULL;

    size_t off = 0;
    if (already_have > 0 && already_buf) {
        size_t copy = already_have < total ? already_have : total;
        memcpy(body, already_buf, copy);
        off = copy;
    }

    while (off < total) {
        ssize_t n;
        do {
            n = recv(fd, body + off, total - off, 0);
        } while (n < 0 && errno == EINTR);
        if (n <= 0)
            break;
        off += (size_t)n;
    }
    *out_len = off;
    body[off] = '\0';
    return body;
}

/*
 * Serve a single accepted connection: read request, dispatch
 * route, serialize response, write back, close. Always closes
 * the socket on return.
 */
static void serve_connection(HttpServer* s, int fd) {
    size_t body_offset = 0;
    size_t content_length = 0;
    size_t total_len = 0;
    char* headers = read_headers(fd, &body_offset, &content_length, &total_len);
    if (!headers) {
        send_bad_request(fd, "could not read request");
        close(fd);
        return;
    }

    /* Isolate the request line so strchr in parse_request_line
     * doesn't wander into the header section. */
    char* eol = strstr(headers, "\r\n");
    if (!eol) {
        free(headers);
        send_bad_request(fd, "malformed request line");
        close(fd);
        return;
    }
    *eol = '\0';

    char* method = NULL;
    char* path = NULL;
    const char* query = NULL;
    if (!parse_request_line(headers, &method, &path, &query)) {
        *eol = '\r';
        free(headers);
        send_bad_request(fd, "malformed request line");
        close(fd);
        return;
    }
    *eol = '\r';

    /* Body bytes already in the headers buffer, past header_end. */
    size_t already = (total_len > body_offset) ? (total_len - body_offset) : 0;

    char* body = NULL;
    size_t body_len = 0;
    if (content_length > 0) {
        body = read_body(fd, headers + body_offset, already, content_length, &body_len);
    }

    HttpRequest req = {
        .method = method,
        .path = path,
        .body = body,
        .body_len = body_len,
        .query = query,
    };

    HttpRouteHandler handler = NULL;
    pthread_mutex_lock(&s->routes_lock);
    handler = route_lookup_locked(s, method, path);
    pthread_mutex_unlock(&s->routes_lock);

    if (!handler) {
        send_not_found(fd);
    } else {
        MetalsharpResponse* resp = handler(&req);
        if (!resp) {
            send_internal_error(fd);
        } else {
            char* json = serialize_response(resp);
            if (!json) {
                send_internal_error(fd);
            } else {
                write_http_response(fd, 200, "OK", json);
                free(json);
            }
            if (resp->error_msg)
                free(resp->error_msg);
            free(resp);
        }
    }

    free(method);
    free(path);
    free(body);
    free(headers);
    shutdown(fd, SHUT_WR);
    close(fd);
}

/*
 * Worker thread entry point. Repeatedly pulls connections from
 * the queue and serves them until the server is shut down.
 */
static void* worker_main(void* arg) {
    HttpServer* s = (HttpServer*)arg;
    for (;;) {
        int fd = queue_pop(s);
        if (fd < 0)
            break;
        serve_connection(s, fd);
    }
    return NULL;
}

/*
 * Create and bind the listening socket to 127.0.0.1:port.
 * Returns the socket fd on success or -1 on failure. On success
 * *out_port is updated with the actual bound port (relevant
 * when port == 0 and the kernel assigns one).
 */
static int create_listen_socket(int port, int* out_port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    int one = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0) {
        close(fd);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    if (listen(fd, 64) < 0) {
        close(fd);
        return -1;
    }

    struct sockaddr_in bound;
    socklen_t blen = sizeof(bound);
    if (getsockname(fd, (struct sockaddr*)&bound, &blen) == 0) {
        *out_port = ntohs(bound.sin_port);
    } else {
        *out_port = port;
    }
    return fd;
}

/*
 * Allocate, initialise, and return a new server. Spawns the
 * worker pool on success. Returns NULL on any failure.
 */
HttpServer* http_server_create(int port) {
    install_sigpipe_handler();

    HttpServer* s = (HttpServer*)calloc(1, sizeof(HttpServer));
    if (!s)
        return NULL;

    s->listen_fd = -1;

    if (pthread_mutex_init(&s->routes_lock, NULL) != 0) {
        free(s);
        return NULL;
    }
    if (pthread_mutex_init(&s->queue_lock, NULL) != 0) {
        pthread_mutex_destroy(&s->routes_lock);
        free(s);
        return NULL;
    }
    if (pthread_cond_init(&s->queue_cond, NULL) != 0) {
        pthread_mutex_destroy(&s->queue_lock);
        pthread_mutex_destroy(&s->routes_lock);
        free(s);
        return NULL;
    }

    s->route_capacity = HTTP_SERVER_ROUTES_INITIAL_CAP;
    s->routes = (Route*)calloc(s->route_capacity, sizeof(Route));
    if (!s->routes) {
        pthread_cond_destroy(&s->queue_cond);
        pthread_mutex_destroy(&s->queue_lock);
        pthread_mutex_destroy(&s->routes_lock);
        free(s);
        return NULL;
    }

    s->listen_fd = create_listen_socket(port, &s->bound_port);
    if (s->listen_fd < 0) {
        free(s->routes);
        pthread_cond_destroy(&s->queue_cond);
        pthread_mutex_destroy(&s->queue_lock);
        pthread_mutex_destroy(&s->routes_lock);
        free(s);
        return NULL;
    }

    for (int i = 0; i < HTTP_SERVER_WORKERS; i++) {
        if (pthread_create(&s->workers[i], NULL, worker_main, s) != 0) {
            /* Failed to spawn: tear down what we have. The
             * already-started workers will exit when they
             * observe shutdown_flag or fail to pop from the
             * queue once we close it. */
            http_server_stop(s);
            for (int j = 0; j < i; j++) {
                pthread_join(s->workers[j], NULL);
            }
            free(s->routes);
            pthread_cond_destroy(&s->queue_cond);
            pthread_mutex_destroy(&s->queue_lock);
            pthread_mutex_destroy(&s->routes_lock);
            free(s);
            return NULL;
        }
    }
    s->workers_started = true;
    return s;
}

int http_server_port(const HttpServer* server) {
    if (!server)
        return 0;
    return server->bound_port;
}

void http_server_register(HttpServer* server, const char* method, const char* path, HttpRouteHandler handler) {
    if (!server || !method || !path || !handler)
        return;
    char* mcopy = strdup(method);
    char* pcopy = strdup(path);
    if (!mcopy || !pcopy) {
        free(mcopy);
        free(pcopy);
        return;
    }

    pthread_mutex_lock(&server->routes_lock);
    if (server->route_count == server->route_capacity) {
        size_t newcap = server->route_capacity * 2;
        Route* grown = (Route*)realloc(server->routes, newcap * sizeof(Route));
        if (!grown) {
            pthread_mutex_unlock(&server->routes_lock);
            free(mcopy);
            free(pcopy);
            return;
        }
        server->routes = grown;
        server->route_capacity = newcap;
    }
    server->routes[server->route_count].method = mcopy;
    server->routes[server->route_count].path = pcopy;
    server->routes[server->route_count].handler = handler;
    server->route_count += 1;
    pthread_mutex_unlock(&server->routes_lock);
}

void http_server_run(HttpServer* server) {
    if (!server)
        return;
    for (;;) {
        int client_fd;
        do {
            client_fd = accept(server->listen_fd, NULL, NULL);
        } while (client_fd < 0 && errno == EINTR);
        if (client_fd < 0)
            break;
        queue_push(server, client_fd);
    }
    /* Wake any workers still blocked so they can exit. */
    pthread_mutex_lock(&server->queue_lock);
    pthread_cond_broadcast(&server->queue_cond);
    pthread_mutex_unlock(&server->queue_lock);
}

void http_server_stop(HttpServer* server) {
    if (!server)
        return;
    pthread_mutex_lock(&server->queue_lock);
    server->shutdown_flag = 1;
    pthread_cond_broadcast(&server->queue_cond);
    pthread_mutex_unlock(&server->queue_lock);

    int fd = server->listen_fd;
    if (fd >= 0) {
        server->listen_fd = -1;
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }
}

void http_server_destroy(HttpServer* server) {
    if (!server || server->destroyed)
        return;
    server->destroyed = true;
    http_server_stop(server);
    if (server->workers_started) {
        for (int i = 0; i < HTTP_SERVER_WORKERS; i++) {
            pthread_join(server->workers[i], NULL);
        }
        server->workers_started = false;
    }
    /* Drain any unprocessed connections. */
    for (size_t i = 0; i < server->queue_size; i++) {
        size_t idx = (server->queue_head + i) % HTTP_SERVER_QUEUE_CAP;
        if (server->queue[idx] >= 0)
            close(server->queue[idx]);
    }
    server->queue_size = 0;
    for (size_t i = 0; i < server->route_count; i++) {
        free(server->routes[i].method);
        free(server->routes[i].path);
    }
    free(server->routes);
    server->routes = NULL;
    pthread_cond_destroy(&server->queue_cond);
    pthread_mutex_destroy(&server->queue_lock);
    pthread_mutex_destroy(&server->routes_lock);
    free(server);
}