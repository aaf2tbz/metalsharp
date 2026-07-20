/*
 * server.h — Shared types for the Metalsharp C backend
 *
 * WHAT
 *   Central header that declares the shared error codes, MTSP
 *   pipeline identifiers, response envelope, and route handler
 *   function pointer used by every backend module. Including
 *   this header gives callers a uniform vocabulary for status
 *   reporting and route dispatch.
 *
 * IMPORTS
 *   <stdbool.h>   bool, true, false
 *   <stddef.h>    size_t, NULL
 *   <stdint.h>    fixed-width integer types
 *   <limits.h>    PATH_MAX when supplied by the platform
 *
 * EXPORTS
 *   METALSHARP_VERSION        Backend version string literal
 *   PATH_MAX                  Maximum path length (4096 fallback)
 *   MetalsharpError           Shared error code enumeration
 *   MetalsharpPipeline        MTSP pipeline identifier enumeration
 *   MetalsharpResponse        Standard response envelope struct
 *   MetalsharpRouteHandler    Route handler function pointer type
 *
 * SCHEMA
 *   Every HTTP route returns a heap-allocated MetalsharpResponse.
 *   ok == true means the request succeeded. When ok == false,
 *   error_msg is a heap-allocated, NUL-terminated string owned
 *   by the response; the caller frees both response and error_msg.
 *   data is an opaque, handler-defined payload whose concrete
 *   type is determined by the route implementation.
 */
#ifndef METALSHARP_SERVER_H
#define METALSHARP_SERVER_H

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Backend version. Mirrors source_version in contract-spec.json. */
#define METALSHARP_VERSION "0.56.1"

/*
 * Maximum path length. macOS does not always define PATH_MAX in
 * <limits.h>; fall back to a generous cap so all path helpers
 * can rely on a constant buffer size.
 */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/*
 * Shared error code set. Every backend module that needs to report
 * failure uses one of these values, letting the HTTP layer map a
 * status into the JSON envelope uniformly.
 */
typedef enum {
    METALSHARP_OK = 0,
    METALSHARP_ERROR_NOT_FOUND,
    METALSHARP_ERROR_PERMISSION_DENIED,
    METALSHARP_ERROR_CONFIG_INVALID,
    METALSHARP_ERROR_RUNTIME_MISSING,
    METALSHARP_ERROR_BOTTLE_CORRUPT,
    METALSHARP_ERROR_NETWORK_ERROR,
    METALSHARP_ERROR_INTERNAL_ERROR,
} MetalsharpError;

/*
 * MTSP pipeline identifiers. The integer value matches the narrow
 * pipeline_code expected by the maintained C launch adapter
 * (see contract-spec.json → static_tables.pipeline_ids).
 */
typedef enum {
    METALSHARP_PIPELINE_WINE_BARE = 0,
    METALSHARP_PIPELINE_M9 = 1,
    METALSHARP_PIPELINE_M10 = 2,
    METALSHARP_PIPELINE_M10_32 = 3,
    METALSHARP_PIPELINE_M11 = 4,
    METALSHARP_PIPELINE_M11_32 = 5,
    METALSHARP_PIPELINE_M12 = 6,
    METALSHARP_PIPELINE_FNA_ARM64 = 7,
    METALSHARP_PIPELINE_FNA_X86 = 8,
} MetalsharpPipeline;

/*
 * Standard response envelope returned by every route handler.
 * The HTTP layer is responsible for serializing this struct to
 * JSON. error_msg is heap-allocated when ok == false and is
 * owned by the response; the HTTP layer frees it alongside the
 * response after serialization. data is an opaque pointer whose
 * concrete type is determined by the handler.
 */
typedef enum {
    METALSHARP_RESPONSE_JSON_TEXT = 0,
    METALSHARP_RESPONSE_JSON_VALUE,
    METALSHARP_RESPONSE_RAW,
} MetalsharpResponseDataKind;

typedef struct {
    bool ok;
    char* error_msg;
    void* data;
    MetalsharpResponseDataKind data_kind;
    size_t data_length; /* RAW byte length; ignored for JSON */
    char* content_type; /* optional heap-owned MIME type */
    int http_status;    /* 0 means HTTP 200 */
} MetalsharpResponse;

/*
 * Function pointer type for route handlers. Each handler receives
 * a pointer to the parsed request (the concrete type is defined
 * by the HTTP layer) and returns a heap-allocated
 * MetalsharpResponse. The HTTP layer owns the returned response
 * and is responsible for freeing it after serialization.
 */
typedef MetalsharpResponse* (*MetalsharpRouteHandler)(const void* request);

#endif /* METALSHARP_SERVER_H */