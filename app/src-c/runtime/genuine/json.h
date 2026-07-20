/*
 * json.h - JSON parser and serializer for the Metalsharp C backend
 *
 * WHAT
 *   Public interface to a small, streaming recursive-descent JSON
 *   parser and serializer implemented in pure C11. The parser produces
 *   an opaque JsonValue tree that callers query via type-safe accessor
 *   functions. The serializer walks the same tree and produces a heap
 *   allocated JSON document suitable for writing to a socket. Designed
 *   to parse HTTP request bodies and serialize HTTP response payloads
 *   for the 264 routes served by the backend, and to round-trip
 *   structured configuration read from disk.
 *
 * IMPORTS
 *   <stdbool.h>   bool, true, false
 *   <stddef.h>    size_t, NULL
 *   "server.h"    MetalsharpResponse, MetalsharpError (transitive)
 *
 * EXPORTS
 *   JsonType          enumeration tagging each JSON value kind
 *   JsonValue         opaque forward declaration of the value type
 *   json_parse        parse a JSON document into a heap tree
 *   json_free         recursively release a parsed tree
 *   json_type         read the type tag of a value
 *   json_get_bool     extract a bool with fallback default
 *   json_get_string   extract a string (NULL if not a string)
 *   json_get_number   extract a number with fallback default
 *   json_array_length number of elements in an array value
 *   json_array_get    access an element by index (no ownership)
 *   json_object_get   access a member by key (no ownership)
 *   json_serialize    render a tree as a heap-allocated JSON string
 *
 * SCHEMA
 *   json_parse allocates and returns the root JsonValue, or NULL on
 *   parse failure. On failure *error receives a heap-allocated,
 *   NUL-terminated diagnostic string that the caller frees; on
 *   success *error is set to NULL. The returned tree owns every
 *   string, object key, and child value it references; json_free
 *   releases the entire subtree. The accessor functions return
 *   pointers directly into the tree (no ownership transfer), and
 *   remain valid until json_free is called on the parent. Object
 *   keys are stored uniquely; duplicate keys keep the first match
 *   for json_object_get but both are preserved by json_serialize.
 *   json_serialize returns a heap-allocated, NUL-terminated buffer
 *   the caller frees, or NULL on allocation failure.
 */
#ifndef METALSHARP_JSON_H
#define METALSHARP_JSON_H

#include "server.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * Type tag for a JsonValue. The integer value is stable and may be
 * compared directly; see json_type for the safe accessor.
 */
typedef enum {
    JSON_NULL = 0,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
} JsonType;

/*
 * Opaque JSON value. The full definition lives in json.c. Callers
 * interact with values exclusively through the accessor and
 * serialization functions declared below.
 */
typedef struct JsonValue JsonValue;

/*
 * Parse a JSON document. input may be any byte buffer of length len
 * (it does not need to be NUL-terminated). On success returns the
 * root of a newly allocated tree and sets *error to NULL. On
 * failure returns NULL and stores a heap-allocated diagnostic in
 * *error describing the first syntax violation encountered, including
 * the byte position. error may be NULL if the caller does not need
 * diagnostics. The returned tree must be released with json_free.
 */
JsonValue* json_parse(const char* input, size_t len, char** error);

/*
 * Recursively free a tree previously returned by json_parse. Passing
 * NULL is a no-op. After this call every pointer obtained from
 * json_get_string, json_array_get, json_object_get on the same tree
 * is dangling and must not be dereferenced.
 */
void json_free(JsonValue* value);

/*
 * Return the type tag of a value. Returns JSON_NULL when value is
 * NULL so callers can chain lookups safely.
 */
JsonType json_type(const JsonValue* value);

/*
 * Boolean accessor. Returns the wrapped bool when the value is a
 * JSON_BOOL, otherwise returns default_val.
 */
bool json_get_bool(const JsonValue* value, bool default_val);

/*
 * String accessor. Returns the wrapped C string (NUL-terminated,
 * owned by the tree) when the value is a JSON_STRING, otherwise
 * NULL. The pointer is valid until json_free is called on the tree.
 */
const char* json_get_string(const JsonValue* value);

/*
 * Number accessor. Returns the wrapped double when the value is a
 * JSON_NUMBER, otherwise returns default_val.
 */
double json_get_number(const JsonValue* value, double default_val);

/*
 * Array length accessor. Returns 0 when value is not an array so
 * callers can iterate safely without an explicit type check.
 */
size_t json_array_length(const JsonValue* value);

/*
 * Array element accessor. Returns the child at index, or NULL when
 * value is not an array or index is out of range. The returned
 * pointer is owned by the tree and must not be freed.
 */
JsonValue* json_array_get(const JsonValue* value, size_t index);

/*
 * Object member accessor. Returns the first child whose key matches
 * key by strcmp, or NULL when value is not an object, key is NULL,
 * or no such member exists. The returned pointer is owned by the
 * tree and must not be freed.
 */
JsonValue* json_object_get(const JsonValue* value, const char* key);

/*
 * Serialize a value to a heap-allocated, NUL-terminated JSON
 * document. Returns NULL on allocation failure. The caller frees
 * the returned buffer with the standard library free. Passing NULL
 * serializes as the literal JSON null.
 */
char* json_serialize(const JsonValue* value);

#endif /* METALSHARP_JSON_H */
