/*
 * config_parser.c — TOML rules parser for Metalsharp
 *
 * Implements the parser declared in config_parser.h. Walks the
 * TOML file line by line, tracks the active section context
 * ([pipeline_defaults.X[.Y]] or [overrides.X[.Y]]), and routes
 * each key = value pair into the right PipelineRule slot.
 *
 * The implementation is intentionally narrow: it only handles
 * the constructs present in configs/mtsp-rules.toml. Unknown
 * keys are silently dropped after the value is parsed and the
 * allocated strings are released, so callers never observe a
 * partially-parsed value. No external dependencies beyond the
 * C standard library, server.h, and hash_table.h.
 *
 * IMPORTS
 *   "config_parser.h"  public interface
 *   <ctype.h>           isspace for whitespace classification
 *   <stdbool.h>         bool, true, false
 *   <stddef.h>          size_t, NULL
 *   <stdio.h>           fopen, fgets, snprintf
 *   <stdlib.h>          malloc, free, strdup
 *   <string.h>          strchr, strcmp, strncmp, strlen, memcpy,
 *                       memmove
 *
 * EXPORTS
 *   config_parse_rules   (see header)
 *   config_free_rules    (see header)
 *   config_get_rule      (see header)
 *
 * SCHEMA
 *   See config_parser.h. The internal helpers below are static
 *   and have no observable effect beyond what the public API
 *   promises. Sections with an unrecognized leading kind cause
 *   the parse to abort with an error so a typo in the TOML is
 *   surfaced immediately rather than silently dropped.
 */

#include "config_parser.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Maximum bytes read from a single input line, including the
 * trailing NUL. Lines in mtsp-rules.toml are well under 1 KB;
 * 4 KB matches PATH_MAX and gives comfortable headroom for the
 * occasional long diagnostic check_dlls array.
 */
#define CP_LINE_MAX 4096

/*
 * Initial slot count for the two top-level maps. The overrides
 * table easily exceeds 100 entries in the production rules file,
 * but starting small keeps the load-factor rehash path well
 * exercised even on tiny fixtures.
 */
#define CP_MAP_INITIAL 16

/*
 * Fixed buffer size for the section header scratch buffer and
 * each of its parsed parts. 128 bytes is comfortable for the
 * longest path segment we expect ("pipeline_defaults" plus the
 * appid and the sub keys "dependencies" / "diagnostics").
 */
#define CP_NAME_MAX 128

/* Forward declaration for the rule lifecycle so callers above
 * the definition (rule_create and the teardown iterator) can
 * release rules without an implicit-declaration warning. */
static void rule_free_free(PipelineRule* rule);

/* ---------- value helpers ---------- */

/*
 * Iterator callback used to release every string value stored
 * in a deps / diags HashTable. The HashTable owns its own key
 * copy but does not free values, so we walk the table at
 * teardown and release the duplicates we inserted ourselves.
 */
static void cp_free_str_value_cb(const char* key, void* value, void* ctx) {
    (void)key;
    (void)ctx;
    free(value);
}

/* ---------- rule lifecycle ---------- */

/*
 * Allocate an empty PipelineRule with empty deps / diags tables.
 * Returns NULL on allocation failure; in that case the rule
 * (and any partially-built tables) is freed before returning.
 */
static PipelineRule* rule_create(void) {
    PipelineRule* r = calloc(1, sizeof(PipelineRule));
    if (r == NULL) {
        return NULL;
    }
    r->deps = ht_create(CP_MAP_INITIAL);
    r->diags = ht_create(CP_MAP_INITIAL);
    if (r->deps == NULL || r->diags == NULL) {
        rule_free_free(r);
        return NULL;
    }
    return r;
}

/*
 * Release every owned string and table inside `rule`, then the
 * rule itself. Safe to call with NULL.
 */
static void rule_free_free(PipelineRule* rule) {
    if (rule == NULL) {
        return;
    }
    free(rule->pipeline);
    free(rule->name);
    free(rule->wine_binary);
    free(rule->graphics_backend);
    free(rule->dll_overrides);
    free(rule->runtime_lane);
    if (rule->deps != NULL) {
        ht_iterate(rule->deps, cp_free_str_value_cb, NULL);
        ht_destroy(rule->deps);
    }
    if (rule->diags != NULL) {
        ht_iterate(rule->diags, cp_free_str_value_cb, NULL);
        ht_destroy(rule->diags);
    }
    free(rule);
}

/* ---------- text helpers ---------- */

/*
 * Trim leading and trailing ASCII whitespace in place. The rule
 * file only uses full-line comments, so we deliberately do not
 * treat '#' as a comment delimiter here; doing so would corrupt
 * any value string that happens to contain a hash character.
 */
static void cp_trim_inplace(char* s) {
    if (s == NULL) {
        return;
    }
    char* p = s;
    while (*p != '\0' && isspace((unsigned char)*p)) {
        p++;
    }
    if (p != s) {
        size_t rest = strlen(p);
        memmove(s, p, rest + 1);
    }
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
}

/* ---------- escape decoding ---------- */

/*
 * Decode a single backslash-escape character. Returns true and
 * writes the decoded byte to *out for the recognized escapes;
 * returns false so the caller can copy the backslash verbatim.
 * Keeps the table tiny because mtsp-rules.toml only contains
 * printable ASCII.
 */
static bool cp_decode_escape(char c, char* out) {
    switch (c) {
    case 'n':
        *out = '\n';
        return true;
    case 't':
        *out = '\t';
        return true;
    case 'r':
        *out = '\r';
        return true;
    case '"':
        *out = '"';
        return true;
    case '\\':
        *out = '\\';
        return true;
    default:
        return false;
    }
}

/*
 * Copy `len` bytes from `start` into a fresh NUL-terminated
 * string, decoding backslash escapes as we go. Returns NULL on
 * allocation failure. Used by both the single-string and array
 * parsers so the escape rules stay in one place.
 */
static char* cp_copy_decoded(const char* start, size_t len) {
    char* out = malloc(len + 1);
    if (out == NULL) {
        return NULL;
    }
    char* dst = out;
    size_t i = 0;
    while (i < len) {
        char c = start[i];
        if (c == '\\' && i + 1 < len) {
            char decoded = 0;
            if (cp_decode_escape(start[i + 1], &decoded)) {
                *dst++ = decoded;
                i += 2;
                continue;
            }
        }
        *dst++ = c;
        i++;
    }
    *dst = '\0';
    return out;
}

/* ---------- value parsers ---------- */

/*
 * Find the closing quote of a TOML basic string that starts at
 * `open` (the opening quote itself). On success returns the
 * pointer to the matching close quote and writes the payload
 * length (excluding both quotes) to *out_len. Returns NULL when
 * the string is unterminated.
 */
static const char* cp_find_string_end(const char* open, size_t* out_len) {
    const char* p = open + 1;
    while (*p != '\0' && *p != '"') {
        if (*p == '\\' && p[1] != '\0') {
            p += 2;
            continue;
        }
        p++;
    }
    if (*p != '"') {
        return NULL;
    }
    *out_len = (size_t)(p - open - 1);
    return p;
}

/*
 * Parse a TOML basic string of the form "..." and store a
 * fresh heap-allocated, escape-decoded copy in *out. Returns
 * true on success, false on malformed input or allocation
 * failure. *out is set to NULL on failure.
 */
static bool cp_parse_string(const char* value, char** out) {
    if (value == NULL || out == NULL) {
        return false;
    }
    *out = NULL;
    while (*value != '\0' && isspace((unsigned char)*value)) {
        value++;
    }
    if (*value != '"') {
        return false;
    }
    size_t len = 0;
    const char* close = cp_find_string_end(value, &len);
    if (close == NULL) {
        return false;
    }
    *out = cp_copy_decoded(value + 1, len);
    return *out != NULL;
}

/*
 * Parse a TOML array of basic strings ["a", "b", "c"] and
 * insert each element into `ht`. The hash table keys are the
 * strings themselves (deep-copied by ht_insert); the values
 * are pointers to the same heap copies, which the rule
 * lifecycle releases via cp_free_str_value_cb. Returns true on
 * success; on failure any partially-inserted entries are
 * released before returning.
 */
static bool cp_parse_string_array(const char* value, HashTable* ht) {
    if (value == NULL || ht == NULL) {
        return false;
    }
    while (*value != '\0' && isspace((unsigned char)*value)) {
        value++;
    }
    if (*value != '[') {
        return false;
    }
    value++;

    while (*value != '\0' && *value != ']') {
        while (*value != '\0' && (isspace((unsigned char)*value) || *value == ',')) {
            value++;
        }
        if (*value == ']' || *value == '\0') {
            break;
        }
        if (*value != '"') {
            return false;
        }
        size_t len = 0;
        const char* close = cp_find_string_end(value, &len);
        if (close == NULL) {
            return false;
        }
        char* item = cp_copy_decoded(value + 1, len);
        if (item == NULL) {
            return false;
        }
        if (!ht_insert(ht, item, item)) {
            free(item);
            return false;
        }
        value = close + 1;
    }

    return true;
}

/*
 * Detect the TOML value type that begins `value` and consume
 * it without storing the result. Used for fields whose meaning
 * is not modelled by PipelineRule (for example exe_names,
 * offline_capable, and the long list of m12 default extras
 * such as windows_dll_path) so the strings we allocate during
 * parsing are released instead of leaking.
 */
static void cp_discard_value(const char* value) {
    if (value == NULL) {
        return;
    }
    while (*value != '\0' && isspace((unsigned char)*value)) {
        value++;
    }
    if (*value == '"') {
        char* s = NULL;
        if (cp_parse_string(value, &s)) {
            free(s);
        }
        return;
    }
    if (*value == '[') {
        HashTable* tmp = ht_create(4);
        if (tmp == NULL) {
            return;
        }
        (void)cp_parse_string_array(value, tmp);
        ht_iterate(tmp, cp_free_str_value_cb, NULL);
        ht_destroy(tmp);
        return;
    }
    /* Booleans and bare scalars have no allocation to release. */
}

/* ---------- section parser ---------- */

/*
 * Pull the three-part section path out of a line that begins
 * with '[' and ends with ']'. Writes NUL-terminated copies
 * into `kind`, `key`, and `sub` (sub is empty when the header
 * has only two parts). Returns true on success, false on any
 * malformed input including missing brackets, missing dot, or
 * any part longer than CP_NAME_MAX.
 */
static bool cp_parse_section(const char* line, char* kind, char* key, char* sub) {
    if (line[0] != '[') {
        return false;
    }
    const char* close = strchr(line, ']');
    if (close == NULL) {
        return false;
    }
    size_t inner = (size_t)(close - line - 1);
    char buf[CP_LINE_MAX];
    if (inner >= sizeof(buf)) {
        return false;
    }
    memcpy(buf, line + 1, inner);
    buf[inner] = '\0';

    char* first = strchr(buf, '.');
    if (first == NULL) {
        return false;
    }
    *first = '\0';

    size_t kind_len = (size_t)(first - buf);
    if (kind_len == 0 || kind_len >= CP_NAME_MAX) {
        return false;
    }
    memcpy(kind, buf, kind_len);
    kind[kind_len] = '\0';

    char* second = strchr(first + 1, '.');
    if (second != NULL) {
        *second = '\0';
        size_t key_len = strlen(first + 1);
        if (key_len == 0 || key_len >= CP_NAME_MAX) {
            return false;
        }
        memcpy(key, first + 1, key_len + 1);

        size_t sub_len = strlen(second + 1);
        if (sub_len == 0 || sub_len >= CP_NAME_MAX) {
            return false;
        }
        memcpy(sub, second + 1, sub_len + 1);
    } else {
        size_t key_len = strlen(first + 1);
        if (key_len == 0 || key_len >= CP_NAME_MAX) {
            return false;
        }
        memcpy(key, first + 1, key_len + 1);
        sub[0] = '\0';
    }

    return true;
}

/*
 * Look up (or create) the PipelineRule for the given section
 * path. Returns NULL when the kind is unrecognized or when
 * allocation fails. The returned rule is owned by the MtspRules
 * container and must not be freed by the caller.
 */
static PipelineRule* cp_locate_rule(MtspRules* rules, const char* kind, const char* key) {
    HashTable* bucket = NULL;
    if (strcmp(kind, "pipeline_defaults") == 0) {
        bucket = rules->defaults;
    } else if (strcmp(kind, "overrides") == 0) {
        bucket = rules->overrides;
    } else {
        return NULL;
    }

    PipelineRule* existing = ht_get(bucket, key);
    if (existing != NULL) {
        return existing;
    }

    PipelineRule* fresh = rule_create();
    if (fresh == NULL) {
        return NULL;
    }
    if (!ht_insert(bucket, key, fresh)) {
        rule_free_free(fresh);
        return NULL;
    }
    return fresh;
}

/* ---------- field assignment ---------- */

/*
 * Assign the parsed value to the named PipelineRule slot for
 * the given (sub, key) pair. Known scalar fields are parsed
 * as strings and replace any previous value; known array
 * fields append into the appropriate HashTable; everything
 * else is parsed and discarded so no allocation leaks.
 * Returns true on success, false only on a real failure such
 * as allocation failure or a malformed value for a known
 * field.
 */
static bool cp_assign_field(PipelineRule* rule, const char* sub, const char* key, const char* value) {
    if (sub[0] == '\0') {
        if (strcmp(key, "pipeline") == 0) {
            free(rule->pipeline);
            rule->pipeline = NULL;
            return cp_parse_string(value, &rule->pipeline);
        }
        if (strcmp(key, "name") == 0) {
            free(rule->name);
            rule->name = NULL;
            return cp_parse_string(value, &rule->name);
        }
        if (strcmp(key, "wine_binary") == 0) {
            free(rule->wine_binary);
            rule->wine_binary = NULL;
            return cp_parse_string(value, &rule->wine_binary);
        }
        if (strcmp(key, "graphics_backend") == 0) {
            free(rule->graphics_backend);
            rule->graphics_backend = NULL;
            return cp_parse_string(value, &rule->graphics_backend);
        }
        if (strcmp(key, "dll_overrides") == 0) {
            free(rule->dll_overrides);
            rule->dll_overrides = NULL;
            return cp_parse_string(value, &rule->dll_overrides);
        }
        if (strcmp(key, "runtime_lane") == 0) {
            free(rule->runtime_lane);
            rule->runtime_lane = NULL;
            return cp_parse_string(value, &rule->runtime_lane);
        }
        cp_discard_value(value);
        return true;
    }

    if (strcmp(sub, "dependencies") == 0) {
        if (strcmp(key, "components") == 0) {
            return cp_parse_string_array(value, rule->deps);
        }
        cp_discard_value(value);
        return true;
    }

    if (strcmp(sub, "diagnostics") == 0) {
        if (strcmp(key, "check_dlls") == 0) {
            return cp_parse_string_array(value, rule->diags);
        }
        cp_discard_value(value);
        return true;
    }

    cp_discard_value(value);
    return true;
}

/* ---------- public API ---------- */

/*
 * Iterator callback used by config_free_rules to release every
 * PipelineRule held by a top-level map. The key and ctx are
 * unused; the value is the rule pointer.
 */
static void cp_free_rule_cb(const char* key, void* value, void* ctx) {
    (void)key;
    (void)ctx;
    rule_free_free((PipelineRule*)value);
}

/*
 * Parse the file at `path` into a fresh MtspRules container.
 * The full file is read line by line; blank lines and lines
 * whose first non-whitespace character is '#' are skipped.
 * Section headers update the active context; key = value lines
 * are dispatched into the appropriate PipelineRule field via
 * cp_assign_field. On any error the partially-built container
 * is released before returning so the caller never sees a
 * half-built rule.
 */
MtspRules* config_parse_rules(const char* path, char** error) {
    if (path == NULL) {
        if (error != NULL) {
            *error = strdup("config_parse_rules: path is NULL");
        }
        return NULL;
    }

    FILE* f = fopen(path, "r");
    if (f == NULL) {
        if (error != NULL) {
            *error = strdup("config_parse_rules: cannot open file");
        }
        return NULL;
    }

    MtspRules* rules = malloc(sizeof(MtspRules));
    if (rules == NULL) {
        fclose(f);
        if (error != NULL) {
            *error = strdup("config_parse_rules: out of memory");
        }
        return NULL;
    }
    rules->defaults = ht_create(CP_MAP_INITIAL);
    rules->overrides = ht_create(CP_MAP_INITIAL);
    if (rules->defaults == NULL || rules->overrides == NULL) {
        config_free_rules(rules);
        fclose(f);
        if (error != NULL) {
            *error = strdup("config_parse_rules: out of memory");
        }
        return NULL;
    }

    char cur_kind[CP_NAME_MAX] = {0};
    char cur_key[CP_NAME_MAX] = {0};
    char cur_sub[CP_NAME_MAX] = {0};
    bool in_section = false;
    int lineno = 0;

    char line[CP_LINE_MAX];
    while (fgets(line, sizeof(line), f) != NULL) {
        lineno++;
        cp_trim_inplace(line);

        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        if (line[0] == '[') {
            if (!cp_parse_section(line, cur_kind, cur_key, cur_sub)) {
                if (error != NULL) {
                    char buf[160];
                    snprintf(buf, sizeof(buf), "config_parse_rules: line %d: malformed section header", lineno);
                    *error = strdup(buf);
                }
                config_free_rules(rules);
                fclose(f);
                return NULL;
            }
            in_section = true;
            continue;
        }

        if (!in_section) {
            continue;
        }

        char* eq = strchr(line, '=');
        if (eq == NULL) {
            continue;
        }
        *eq = '\0';
        char* key = line;
        char* value = eq + 1;
        cp_trim_inplace(key);
        cp_trim_inplace(value);

        PipelineRule* rule = cp_locate_rule(rules, cur_kind, cur_key);
        if (rule == NULL) {
            if (error != NULL) {
                char buf[200];
                snprintf(buf, sizeof(buf), "config_parse_rules: line %d: unknown section kind '%s'", lineno, cur_kind);
                *error = strdup(buf);
            }
            config_free_rules(rules);
            fclose(f);
            return NULL;
        }

        if (!cp_assign_field(rule, cur_sub, key, value)) {
            if (error != NULL) {
                char buf[200];
                snprintf(buf, sizeof(buf), "config_parse_rules: line %d: failed to parse '%s'", lineno, key);
                *error = strdup(buf);
            }
            config_free_rules(rules);
            fclose(f);
            return NULL;
        }
    }

    fclose(f);
    if (error != NULL) {
        *error = NULL;
    }
    return rules;
}

/*
 * Walk both top-level maps and release every rule they hold,
 * then destroy the maps and the container itself. Safe to call
 * with NULL and safe when the maps were only partially built.
 */
void config_free_rules(MtspRules* rules) {
    if (rules == NULL) {
        return;
    }
    if (rules->defaults != NULL) {
        ht_iterate(rules->defaults, cp_free_rule_cb, NULL);
        ht_destroy(rules->defaults);
    }
    if (rules->overrides != NULL) {
        ht_iterate(rules->overrides, cp_free_rule_cb, NULL);
        ht_destroy(rules->overrides);
    }
    free(rules);
}

/*
 * Resolve the effective rule for `appid`. Looks up the override
 * map first; if that misses, falls back to the m12 default rule
 * (the catalog's primary lane). Returns NULL when neither path
 * yields a rule.
 */
PipelineRule* config_get_rule(MtspRules* rules, unsigned int appid) {
    if (rules == NULL) {
        return NULL;
    }
    char key[32];
    snprintf(key, sizeof(key), "%u", appid);
    PipelineRule* rule = ht_get(rules->overrides, key);
    if (rule != NULL) {
        return rule;
    }
    return ht_get(rules->defaults, "m12");
}
