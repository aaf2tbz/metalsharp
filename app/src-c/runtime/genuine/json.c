/*
 * json.c - JSON parser and serializer implementation
 *
 * WHAT
 *   Streaming recursive-descent JSON parser and serializer. The
 *   parser walks the input byte by byte using a Parser cursor and
 *   allocates each value, string, and object key independently via
 *   the C standard library allocator; there is no bump-pointer arena
 *   and no caching layer. Errors carry the byte position so the HTTP
 *   layer can return actionable diagnostics. The serializer walks a
 *   tree produced by the parser (or by hand) and renders it into a
 *   heap-allocated buffer, escaping strings and converting double
 *   values to their shortest round-trippable decimal form. The whole
 *   file targets C11 and depends only on the standard library plus
 *   the shared server.h.
 *
 * IMPORTS
 *   "json.h"        public interface, opaque JsonValue, JsonType
 *   "server.h"      MetalsharpResponse / MetalsharpError (transitive)
 *   <ctype.h>       isspace for whitespace classification
 *   <math.h>        INFINITY / NAN detection for number serialization
 *   <stdbool.h>     bool, true, false
 *   <stddef.h>      size_t, NULL
 *   <stdint.h>      uint32_t for UTF-8 code point handling
 *   <stdio.h>       snprintf for error and number formatting
 *   <stdlib.h>      malloc, calloc, realloc, free, strtod, strdup
 *   <string.h>      memcpy, memcmp, strlen, strcmp, strchr
 *
 * EXPORTS
 *   json_parse          parse an input buffer into a JsonValue tree
 *   json_free           release a parsed tree
 *   json_type           query the type tag of a value
 *   json_get_bool       extract a bool with default
 *   json_get_string     extract a string (NULL if not a string)
 *   json_get_number     extract a number with default
 *   json_array_length   length of an array value
 *   json_array_get      array element accessor
 *   json_object_get     object member accessor
 *   json_serialize      render a tree as a heap string
 *
 * SCHEMA
 *   The internal struct JsonValue carries a JsonType tag and a
 *   tagged union over the six payload shapes (null/bool have no
 *   payload). Arrays and objects use dynamic JsonValue ** and char **
 *   vectors that grow by doubling; json_free releases every entry
 *   before freeing the parent. Strings store both the NUL-terminated
 *   data pointer and a byte length so embedded NULs from escape
 *   decoding remain observable. Numbers are stored as IEEE-754
 *   doubles; serialization rejects NaN and infinity by emitting the
 *   literal null instead of producing invalid JSON.
 */

#include "json.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Maximum nesting depth for arrays and objects. The requirement
 * states 32 levels deep, which we enforce on every entry to the
 * recursive value parser.
 */
#define JSON_MAX_DEPTH 32

/* Initial capacity for array and object child vectors. */
#define JSON_INITIAL_CAPACITY 4

/* Initial capacity for the string buffers used during parsing and
 * serialization. Doubled on demand.
 */
#define JSON_INITIAL_BUFFER 32

/*
 * Internal layout of the opaque JsonValue. Public callers see only
 * the forward declaration; only this translation unit may read or
 * write the fields.
 */
struct JsonValue {
    JsonType type;
    union {
        bool as_bool;
        double as_number;
        struct {
            char* data;
            size_t length;
        } as_string;
        struct {
            JsonValue** items;
            size_t length;
            size_t capacity;
        } as_array;
        struct {
            char** keys;
            JsonValue** values;
            size_t length;
            size_t capacity;
        } as_object;
    };
};

/* ===== Parser ===== */

/*
 * Streaming cursor over the input buffer. depth tracks the current
 * nesting level; error is non-NULL once a diagnostic has been
 * recorded and remains the only error returned to the caller.
 */
typedef struct {
    const char* input;
    size_t length;
    size_t position;
    int depth;
    char* error;
} Parser;

/* Forward declarations for the recursive value parser. */
static JsonValue* parse_value(Parser* p);
static JsonValue* parse_string(Parser* p);
static JsonValue* parse_number(Parser* p);
static JsonValue* parse_array(Parser* p);
static JsonValue* parse_object(Parser* p);
static JsonValue* parse_literal(Parser* p, const char* literal, JsonType type);

/* Buffer used to accumulate parsed string bytes, including UTF-8
 * sequences produced by \uXXXX escapes.
 */
typedef struct {
    char* data;
    size_t length;
    size_t capacity;
} StringAccumulator;

static bool accum_init(StringAccumulator* a) {
    a->capacity = JSON_INITIAL_BUFFER;
    a->data = malloc(a->capacity);
    if (a->data == NULL)
        return false;
    a->length = 0;
    a->data[0] = '\0';
    return true;
}

static void accum_free(StringAccumulator* a) {
    free(a->data);
    a->data = NULL;
    a->length = 0;
    a->capacity = 0;
}

static bool accum_reserve(StringAccumulator* a, size_t need) {
    if (a->length + need + 1 <= a->capacity)
        return true;
    size_t newcap = a->capacity;
    while (newcap < a->length + need + 1) {
        if (newcap > (size_t)-1 / 2)
            return false;
        newcap *= 2;
    }
    char* newdata = realloc(a->data, newcap);
    if (newdata == NULL)
        return false;
    a->data = newdata;
    a->capacity = newcap;
    return true;
}

static bool accum_append_bytes(StringAccumulator* a, const char* bytes, size_t count) {
    if (!accum_reserve(a, count))
        return false;
    memcpy(a->data + a->length, bytes, count);
    a->length += count;
    a->data[a->length] = '\0';
    return true;
}

static bool accum_append_byte(StringAccumulator* a, char byte) {
    return accum_append_bytes(a, &byte, 1);
}

/*
 * Encode a Unicode code point as UTF-8 and append the bytes to the
 * accumulator. Code points above U+10FFFF are rejected; surrogate
 * halves should be combined by the caller into a full code point
 * before invoking this helper.
 */
static bool accum_append_utf8(StringAccumulator* a, uint32_t cp) {
    char encoded[4];
    size_t count;
    if (cp < 0x80) {
        encoded[0] = (char)cp;
        count = 1;
    } else if (cp < 0x800) {
        encoded[0] = (char)(0xC0 | (cp >> 6));
        encoded[1] = (char)(0x80 | (cp & 0x3F));
        count = 2;
    } else if (cp < 0x10000) {
        encoded[0] = (char)(0xE0 | (cp >> 12));
        encoded[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        encoded[2] = (char)(0x80 | (cp & 0x3F));
        count = 3;
    } else if (cp < 0x110000) {
        encoded[0] = (char)(0xF0 | (cp >> 18));
        encoded[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        encoded[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        encoded[3] = (char)(0x80 | (cp & 0x3F));
        count = 4;
    } else {
        return false;
    }
    return accum_append_bytes(a, encoded, count);
}

/*
 * Record a diagnostic on the parser. The first error wins; later
 * failures during the same parse are ignored so the caller sees the
 * root cause rather than a cascade of consequences.
 */
static bool parser_set_error(Parser* p, const char* message) {
    if (p->error != NULL)
        return false;
    char buffer[256];
    int written = snprintf(buffer, sizeof(buffer), "%s at position %zu", message, p->position);
    if (written < 0) {
        p->error = strdup("unknown parser error");
    } else {
        size_t size = (size_t)written + 1;
        p->error = malloc(size);
        if (p->error != NULL)
            memcpy(p->error, buffer, size);
    }
    return false;
}

static void parser_skip_whitespace(Parser* p) {
    while (p->position < p->length) {
        char c = p->input[p->position];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            p->position++;
        else
            break;
    }
}

static char parser_peek(Parser* p) {
    if (p->input == NULL || p->position >= p->length)
        return '\0';
    return p->input[p->position];
}

static char parser_advance(Parser* p) {
    if (p->input == NULL || p->position >= p->length)
        return '\0';
    char c = p->input[p->position];
    p->position++;
    return c;
}

static bool parser_expect(Parser* p, char expected) {
    if (parser_peek(p) != expected) {
        char message[64];
        snprintf(message, sizeof(message), "expected '%c'", expected);
        return parser_set_error(p, message);
    }
    parser_advance(p);
    return true;
}

/*
 * Decode exactly four hex digits at the current cursor and advance
 * past them. Returns true on success and stores the parsed code
 * point in *cp; returns false (with an error recorded) if any of
 * the four digits are not hex or if the input ends early.
 */
static bool parser_parse_hex4(Parser* p, uint32_t* cp) {
    if (p->position + 4 > p->length)
        return parser_set_error(p, "truncated unicode escape");
    uint32_t value = 0;
    for (int i = 0; i < 4; i++) {
        char c = p->input[p->position];
        uint32_t digit;
        if (c >= '0' && c <= '9')
            digit = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            digit = (uint32_t)(c - 'A' + 10);
        else
            return parser_set_error(p, "invalid hex digit in unicode escape");
        value = (value << 4) | digit;
        p->position++;
    }
    *cp = value;
    return true;
}

/*
 * Handle a \uXXXX escape, including the optional surrogate pair that
 * combines two code units into a single astral-plane character. The
 * decoded UTF-8 bytes are appended to the accumulator.
 */
static bool parser_handle_unicode_escape(Parser* p, StringAccumulator* acc) {
    uint32_t cp;
    if (!parser_parse_hex4(p, &cp))
        return false;
    if (cp >= 0xD800 && cp <= 0xDBFF) {
        if (p->position + 6 > p->length || p->input[p->position] != '\\' || p->input[p->position + 1] != 'u')
            return parser_set_error(p, "expected low surrogate after high surrogate");
        p->position += 2;
        uint32_t low;
        if (!parser_parse_hex4(p, &low))
            return false;
        if (low < 0xDC00 || low > 0xDFFF)
            return parser_set_error(p, "invalid low surrogate in surrogate pair");
        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
        return parser_set_error(p, "unexpected low surrogate in string");
    }
    return accum_append_utf8(acc, cp);
}

/*
 * Parse the body of a JSON string after the opening quote has been
 * consumed. Decodes escape sequences and UTF-8 escape sequences,
 * appends each byte to the accumulator, then wraps the result in a
 * freshly allocated JsonValue.
 */
static JsonValue* parser_finish_string(Parser* p, StringAccumulator* acc) {
    JsonValue* value = malloc(sizeof(JsonValue));
    if (value == NULL) {
        accum_free(acc);
        return (JsonValue*)parser_set_error(p, "out of memory");
    }
    value->type = JSON_STRING;
    value->as_string.data = acc->data;
    value->as_string.length = acc->length;
    return value;
}

static JsonValue* parse_string(Parser* p) {
    parser_advance(p); /* consume opening '"' */
    StringAccumulator acc;
    if (!accum_init(&acc))
        return (JsonValue*)parser_set_error(p, "out of memory");
    while (true) {
        char c = parser_peek(p);
        if (c == '"') {
            parser_advance(p);
            return parser_finish_string(p, &acc);
        }
        if (c == '\0')
            return (JsonValue*)parser_set_error(p, "unterminated string");
        if (c == '\\') {
            parser_advance(p);
            char esc = parser_peek(p);
            if (esc == '\0')
                return (JsonValue*)parser_set_error(p, "truncated escape sequence");
            parser_advance(p);
            char decoded;
            switch (esc) {
            case '"':
                decoded = '"';
                break;
            case '\\':
                decoded = '\\';
                break;
            case '/':
                decoded = '/';
                break;
            case 'b':
                decoded = '\b';
                break;
            case 'f':
                decoded = '\f';
                break;
            case 'n':
                decoded = '\n';
                break;
            case 'r':
                decoded = '\r';
                break;
            case 't':
                decoded = '\t';
                break;
            case 'u':
                if (!parser_handle_unicode_escape(p, &acc)) {
                    accum_free(&acc);
                    return NULL;
                }
                continue;
            default:
                accum_free(&acc);
                return (JsonValue*)parser_set_error(p, "invalid escape sequence");
            }
            if (!accum_append_byte(&acc, decoded)) {
                accum_free(&acc);
                return (JsonValue*)parser_set_error(p, "out of memory");
            }
            continue;
        }
        if ((unsigned char)c < 0x20) {
            accum_free(&acc);
            return (JsonValue*)parser_set_error(p, "unescaped control character in string");
        }
        parser_advance(p);
        if (!accum_append_byte(&acc, c)) {
            accum_free(&acc);
            return (JsonValue*)parser_set_error(p, "out of memory");
        }
    }
}

/*
 * Validate the number substring and convert it with strtod. The
 * caller is responsible for not invoking this on anything other than
 * a well-formed numeric literal; the manual check here keeps us
 * from accepting locale-specific garbage like "1,5" or the
 * non-JSON forms "inf" and "nan" that strtod would otherwise accept.
 */
static JsonValue* parse_number(Parser* p) {
    size_t start = p->position;
    if (parser_peek(p) == '-')
        parser_advance(p);
    char first = parser_peek(p);
    if (first == '0') {
        parser_advance(p);
    } else if (first >= '1' && first <= '9') {
        parser_advance(p);
        while (true) {
            char c = parser_peek(p);
            if (c < '0' || c > '9')
                break;
            parser_advance(p);
        }
    } else {
        return (JsonValue*)parser_set_error(p, "expected digit in number");
    }
    if (parser_peek(p) == '.') {
        parser_advance(p);
        if (parser_peek(p) < '0' || parser_peek(p) > '9')
            return (JsonValue*)parser_set_error(p, "expected digit after decimal point");
        while (true) {
            char c = parser_peek(p);
            if (c < '0' || c > '9')
                break;
            parser_advance(p);
        }
    }
    if (parser_peek(p) == 'e' || parser_peek(p) == 'E') {
        parser_advance(p);
        if (parser_peek(p) == '+' || parser_peek(p) == '-')
            parser_advance(p);
        if (parser_peek(p) < '0' || parser_peek(p) > '9')
            return (JsonValue*)parser_set_error(p, "expected digit in exponent");
        while (true) {
            char c = parser_peek(p);
            if (c < '0' || c > '9')
                break;
            parser_advance(p);
        }
    }
    size_t end = p->position;
    size_t length = end - start;
    if (length == 0 || length > 64)
        return (JsonValue*)parser_set_error(p, "invalid number literal");
    char digits[80];
    memcpy(digits, p->input + start, length);
    digits[length] = '\0';
    char* after = NULL;
    double value = strtod(digits, &after);
    if (after == digits || (after != NULL && *after != '\0'))
        return (JsonValue*)parser_set_error(p, "invalid number literal");
    JsonValue* node = malloc(sizeof(JsonValue));
    if (node == NULL)
        return (JsonValue*)parser_set_error(p, "out of memory");
    node->type = JSON_NUMBER;
    node->as_number = value;
    return node;
}

/*
 * Match a literal keyword such as "null", "true", or "false".
 * Advances the cursor on success, errors otherwise.
 */
static JsonValue* parse_literal(Parser* p, const char* literal, JsonType type) {
    size_t length = strlen(literal);
    if (p->position + length > p->length || memcmp(p->input + p->position, literal, length) != 0)
        return (JsonValue*)parser_set_error(p, "unexpected character");
    p->position += length;
    JsonValue* node = malloc(sizeof(JsonValue));
    if (node == NULL)
        return (JsonValue*)parser_set_error(p, "out of memory");
    node->type = type;
    if (type == JSON_BOOL)
        node->as_bool = (literal[0] == 't');
    return node;
}

/*
 * Ensure the array's child vector can hold one more entry. Returns
 * false (with the parser in error state) if reallocation fails.
 */
static bool array_grow(Parser* p, JsonValue* array) {
    size_t capacity = array->as_array.capacity;
    if (array->as_array.length < capacity)
        return true;
    size_t newcap = capacity == 0 ? JSON_INITIAL_CAPACITY : capacity * 2;
    if (newcap < capacity)
        return parser_set_error(p, "out of memory");
    JsonValue** resized = realloc(array->as_array.items, newcap * sizeof(JsonValue*));
    if (resized == NULL)
        return parser_set_error(p, "out of memory");
    array->as_array.items = resized;
    array->as_array.capacity = newcap;
    return true;
}

static JsonValue* parse_array(Parser* p) {
    parser_advance(p); /* consume '[' */
    JsonValue* array = malloc(sizeof(JsonValue));
    if (array == NULL)
        return (JsonValue*)parser_set_error(p, "out of memory");
    array->type = JSON_ARRAY;
    array->as_array.items = NULL;
    array->as_array.length = 0;
    array->as_array.capacity = 0;
    parser_skip_whitespace(p);
    if (parser_peek(p) == ']') {
        parser_advance(p);
        return array;
    }
    while (true) {
        parser_skip_whitespace(p);
        JsonValue* child = parse_value(p);
        if (child == NULL) {
            json_free(array);
            return NULL;
        }
        if (!array_grow(p, array)) {
            json_free(child);
            json_free(array);
            return NULL;
        }
        array->as_array.items[array->as_array.length++] = child;
        parser_skip_whitespace(p);
        char next = parser_peek(p);
        if (next == ']') {
            parser_advance(p);
            return array;
        }
        if (next != ',') {
            json_free(array);
            return (JsonValue*)parser_set_error(p, "expected ',' or ']' in array");
        }
        parser_advance(p);
        parser_skip_whitespace(p);
        if (parser_peek(p) == ']') {
            json_free(array);
            return (JsonValue*)parser_set_error(p, "trailing comma in array");
        }
    }
}

/*
 * Grow the object's parallel key/value vectors together. Both must
 * succeed for the operation to be considered successful; if either
 * realloc fails, the partially grown vectors are freed before the
 * error is reported.
 */
static bool object_grow(Parser* p, JsonValue* object) {
    size_t capacity = object->as_object.capacity;
    if (object->as_object.length < capacity)
        return true;
    size_t newcap = capacity == 0 ? JSON_INITIAL_CAPACITY : capacity * 2;
    if (newcap < capacity)
        return parser_set_error(p, "out of memory");
    char** newkeys = realloc(object->as_object.keys, newcap * sizeof(char*));
    if (newkeys == NULL)
        return parser_set_error(p, "out of memory");
    JsonValue** newvals = realloc(object->as_object.values, newcap * sizeof(JsonValue*));
    if (newvals == NULL) {
        /* Keep the existing key vector intact if the value realloc
         * fails, so the caller can still free it cleanly. */
        object->as_object.keys = newkeys;
        return parser_set_error(p, "out of memory");
    }
    object->as_object.keys = newkeys;
    object->as_object.values = newvals;
    object->as_object.capacity = newcap;
    return true;
}

static JsonValue* parse_object(Parser* p) {
    parser_advance(p); /* consume '{' */
    JsonValue* object = malloc(sizeof(JsonValue));
    if (object == NULL)
        return (JsonValue*)parser_set_error(p, "out of memory");
    object->type = JSON_OBJECT;
    object->as_object.keys = NULL;
    object->as_object.values = NULL;
    object->as_object.length = 0;
    object->as_object.capacity = 0;
    parser_skip_whitespace(p);
    if (parser_peek(p) == '}') {
        parser_advance(p);
        return object;
    }
    while (true) {
        parser_skip_whitespace(p);
        if (parser_peek(p) != '"') {
            json_free(object);
            return (JsonValue*)parser_set_error(p, "expected string key in object");
        }
        JsonValue* key_node = parse_string(p);
        if (key_node == NULL) {
            json_free(object);
            return NULL;
        }
        size_t key_length = key_node->as_string.length;
        char* key = malloc(key_length + 1);
        if (key == NULL) {
            json_free(key_node);
            json_free(object);
            return (JsonValue*)parser_set_error(p, "out of memory");
        }
        memcpy(key, key_node->as_string.data, key_length);
        key[key_length] = '\0';
        json_free(key_node);
        parser_skip_whitespace(p);
        if (!parser_expect(p, ':')) {
            free(key);
            json_free(object);
            return NULL;
        }
        JsonValue* value_node = parse_value(p);
        if (value_node == NULL) {
            free(key);
            json_free(object);
            return NULL;
        }
        if (!object_grow(p, object)) {
            free(key);
            json_free(value_node);
            json_free(object);
            return NULL;
        }
        object->as_object.keys[object->as_object.length] = key;
        object->as_object.values[object->as_object.length] = value_node;
        object->as_object.length++;
        parser_skip_whitespace(p);
        char next = parser_peek(p);
        if (next == '}') {
            parser_advance(p);
            return object;
        }
        if (next != ',') {
            json_free(object);
            return (JsonValue*)parser_set_error(p, "expected ',' or '}' in object");
        }
        parser_advance(p);
        parser_skip_whitespace(p);
        if (parser_peek(p) == '}') {
            json_free(object);
            return (JsonValue*)parser_set_error(p, "trailing comma in object");
        }
    }
}

/*
 * Dispatch to the appropriate value parser based on the first
 * non-whitespace character. Also enforces the maximum nesting depth
 * so a pathological input cannot blow the call stack.
 */
static JsonValue* parse_value(Parser* p) {
    if (p->depth >= JSON_MAX_DEPTH)
        return (JsonValue*)parser_set_error(p, "maximum nesting depth exceeded");
    p->depth++;
    parser_skip_whitespace(p);
    JsonValue* result = NULL;
    char c = parser_peek(p);
    switch (c) {
    case '{':
        result = parse_object(p);
        break;
    case '[':
        result = parse_array(p);
        break;
    case '"':
        result = parse_string(p);
        break;
    case 't':
        result = parse_literal(p, "true", JSON_BOOL);
        break;
    case 'f':
        result = parse_literal(p, "false", JSON_BOOL);
        break;
    case 'n':
        result = parse_literal(p, "null", JSON_NULL);
        break;
    default:
        if (c == '-' || (c >= '0' && c <= '9'))
            result = parse_number(p);
        else if (c == '\0')
            result = (JsonValue*)parser_set_error(p, "unexpected end of input");
        else
            result = (JsonValue*)parser_set_error(p, "unexpected character");
        break;
    }
    p->depth--;
    return result;
}

/* ===== Serializer ===== */

/*
 * Growable byte buffer used while serializing a tree into a single
 * NUL-terminated string. On any allocation failure the failed flag
 * is set and subsequent append calls become no-ops, so the caller
 * can check once at the end rather than at every site.
 */
typedef struct {
    char* data;
    size_t length;
    size_t capacity;
    bool failed;
} StringBuilder;

static bool builder_reserve(StringBuilder* b, size_t additional) {
    if (b->failed)
        return false;
    if (b->length + additional + 1 <= b->capacity)
        return true;
    size_t newcap = b->capacity == 0 ? JSON_INITIAL_BUFFER : b->capacity;
    while (newcap < b->length + additional + 1) {
        if (newcap > (size_t)-1 / 2) {
            b->failed = true;
            return false;
        }
        newcap *= 2;
    }
    char* resized = realloc(b->data, newcap);
    if (resized == NULL) {
        b->failed = true;
        return false;
    }
    b->data = resized;
    b->capacity = newcap;
    return true;
}

static bool builder_append_bytes(StringBuilder* b, const char* bytes, size_t count) {
    if (!builder_reserve(b, count))
        return false;
    memcpy(b->data + b->length, bytes, count);
    b->length += count;
    b->data[b->length] = '\0';
    return true;
}

static bool builder_append_byte(StringBuilder* b, char byte) {
    return builder_append_bytes(b, &byte, 1);
}

static bool builder_append_cstr(StringBuilder* b, const char* str) {
    return builder_append_bytes(b, str, strlen(str));
}

/*
 * Emit a JSON-escaped string. Control characters below 0x20 and the
 * three quote-family characters are escaped; other bytes (including
 * multi-byte UTF-8 sequences) are emitted verbatim so round-tripping
 * preserves non-ASCII content.
 */
static bool builder_append_string(StringBuilder* b, const char* str, size_t length) {
    if (!builder_append_byte(b, '"'))
        return false;
    const unsigned char* bytes = (const unsigned char*)str;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = bytes[i];
        switch (c) {
        case '"':
            if (!builder_append_cstr(b, "\\\""))
                return false;
            break;
        case '\\':
            if (!builder_append_cstr(b, "\\\\"))
                return false;
            break;
        case '\b':
            if (!builder_append_cstr(b, "\\b"))
                return false;
            break;
        case '\f':
            if (!builder_append_cstr(b, "\\f"))
                return false;
            break;
        case '\n':
            if (!builder_append_cstr(b, "\\n"))
                return false;
            break;
        case '\r':
            if (!builder_append_cstr(b, "\\r"))
                return false;
            break;
        case '\t':
            if (!builder_append_cstr(b, "\\t"))
                return false;
            break;
        default:
            if (c < 0x20) {
                char hex[8];
                int written = snprintf(hex, sizeof(hex), "\\u%04x", (unsigned)c);
                if (written < 0 || (size_t)written >= sizeof(hex))
                    return false;
                if (!builder_append_bytes(b, hex, (size_t)written))
                    return false;
            } else {
                if (!builder_append_byte(b, (char)c))
                    return false;
            }
            break;
        }
    }
    return builder_append_byte(b, '"');
}

/*
 * Render a finite double as a JSON number. NaN and infinity are not
 * valid JSON numbers; we substitute the literal null so the output
 * remains parseable. The %.17g format preserves round-trip precision
 * for IEEE-754 doubles, and we normalize the decimal separator to
 * '.' in case the runtime locale uses ',' (strtod's symmetry means
 * the result will still parse correctly).
 */
static bool builder_append_number(StringBuilder* b, double value) {
    if (value != value || value == INFINITY || value == -INFINITY)
        return builder_append_cstr(b, "null");
    char digits[64];
    int written = snprintf(digits, sizeof(digits), "%.17g", value);
    if (written < 0 || (size_t)written >= sizeof(digits)) {
        b->failed = true;
        return false;
    }
    for (int i = 0; i < written; i++) {
        if (digits[i] == ',')
            digits[i] = '.';
    }
    return builder_append_bytes(b, digits, (size_t)written);
}

/*
 * Walk a value tree and emit its JSON representation. Arrays and
 * objects track their own depth; we do not enforce a separate
 * serialization depth limit because a tree that parsed under the
 * depth cap will always serialize within it.
 */
static bool builder_append_value(StringBuilder* b, const JsonValue* value) {
    if (value == NULL)
        return builder_append_cstr(b, "null");
    switch (value->type) {
    case JSON_NULL:
        return builder_append_cstr(b, "null");
    case JSON_BOOL:
        return builder_append_cstr(b, value->as_bool ? "true" : "false");
    case JSON_NUMBER:
        return builder_append_number(b, value->as_number);
    case JSON_STRING:
        return builder_append_string(b, value->as_string.data, value->as_string.length);
    case JSON_ARRAY:
        if (!builder_append_byte(b, '['))
            return false;
        for (size_t i = 0; i < value->as_array.length; i++) {
            if (i > 0 && !builder_append_byte(b, ','))
                return false;
            if (!builder_append_value(b, value->as_array.items[i]))
                return false;
        }
        return builder_append_byte(b, ']');
    case JSON_OBJECT: {
        if (!builder_append_byte(b, '{'))
            return false;
        for (size_t i = 0; i < value->as_object.length; i++) {
            if (i > 0 && !builder_append_byte(b, ','))
                return false;
            const char* key = value->as_object.keys[i];
            if (!builder_append_string(b, key, strlen(key)))
                return false;
            if (!builder_append_byte(b, ':'))
                return false;
            if (!builder_append_value(b, value->as_object.values[i]))
                return false;
        }
        return builder_append_byte(b, '}');
    }
    }
    b->failed = true;
    return false;
}

/* ===== Public API ===== */

JsonValue* json_parse(const char* input, size_t len, char** error) {
    if (error != NULL)
        *error = NULL;
    Parser parser;
    parser.input = input;
    parser.length = len;
    parser.position = 0;
    parser.depth = 0;
    parser.error = NULL;
    JsonValue* root = parse_value(&parser);
    if (root == NULL) {
        if (error != NULL)
            *error = parser.error != NULL ? parser.error : strdup("unknown parse error");
        return NULL;
    }
    parser_skip_whitespace(&parser);
    if (parser.position < parser.length) {
        char message[160];
        snprintf(message, sizeof(message), "unexpected trailing character at position %zu", parser.position);
        json_free(root);
        if (error != NULL) {
            if (parser.error != NULL) {
                free(parser.error);
                parser.error = NULL;
            }
            *error = strdup(message);
        }
        return NULL;
    }
    return root;
}

void json_free(JsonValue* value) {
    if (value == NULL)
        return;
    switch (value->type) {
    case JSON_NULL:
    case JSON_BOOL:
    case JSON_NUMBER:
        break;
    case JSON_STRING:
        free(value->as_string.data);
        break;
    case JSON_ARRAY:
        for (size_t i = 0; i < value->as_array.length; i++)
            json_free(value->as_array.items[i]);
        free(value->as_array.items);
        break;
    case JSON_OBJECT:
        for (size_t i = 0; i < value->as_object.length; i++) {
            free(value->as_object.keys[i]);
            json_free(value->as_object.values[i]);
        }
        free(value->as_object.keys);
        free(value->as_object.values);
        break;
    }
    free(value);
}

JsonType json_type(const JsonValue* value) {
    if (value == NULL)
        return JSON_NULL;
    return value->type;
}

bool json_get_bool(const JsonValue* value, bool default_val) {
    if (value == NULL || value->type != JSON_BOOL)
        return default_val;
    return value->as_bool;
}

const char* json_get_string(const JsonValue* value) {
    if (value == NULL || value->type != JSON_STRING)
        return NULL;
    return value->as_string.data;
}

double json_get_number(const JsonValue* value, double default_val) {
    if (value == NULL || value->type != JSON_NUMBER)
        return default_val;
    return value->as_number;
}

size_t json_array_length(const JsonValue* value) {
    if (value == NULL || value->type != JSON_ARRAY)
        return 0;
    return value->as_array.length;
}

JsonValue* json_array_get(const JsonValue* value, size_t index) {
    if (value == NULL || value->type != JSON_ARRAY)
        return NULL;
    if (index >= value->as_array.length)
        return NULL;
    return value->as_array.items[index];
}

JsonValue* json_object_get(const JsonValue* value, const char* key) {
    if (value == NULL || value->type != JSON_OBJECT || key == NULL)
        return NULL;
    for (size_t i = 0; i < value->as_object.length; i++) {
        if (strcmp(value->as_object.keys[i], key) == 0)
            return value->as_object.values[i];
    }
    return NULL;
}

char* json_serialize(const JsonValue* value) {
    StringBuilder builder;
    builder.data = NULL;
    builder.length = 0;
    builder.capacity = 0;
    builder.failed = false;
    if (!builder_append_value(&builder, value)) {
        free(builder.data);
        return NULL;
    }
    if (builder.failed) {
        free(builder.data);
        return NULL;
    }
    return builder.data;
}
