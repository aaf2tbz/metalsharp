/*
 * config_parser.h — TOML config parser for mtsp-rules.toml
 *
 * WHAT
 *   Parses the Metalsharp MTSP pipeline rules configuration file
 *   (configs/mtsp-rules.toml) into a typed in-memory representation.
 *   Only the subset of TOML actually used by mtsp-rules.toml is
 *   implemented: section headers of the form [a.b[.c]], string
 *   values, string arrays, and boolean literals. Comments and blank
 *   lines are skipped. The parser is intentionally narrow and
 *   dependency-free; it is not a full TOML 1.0 implementation.
 *
 * IMPORTS
 *   "server.h"      Shared error codes and the standard PATH_MAX.
 *   "hash_table.h"  String-keyed hash table used for the maps.
 *   <stdbool.h>     bool, true, false
 *   <stddef.h>      size_t, NULL
 *
 * EXPORTS
 *   PipelineRule        Per-rule fields: pipeline id, display name,
 *                       dependency / diagnostic maps, and the four
 *                       pipeline-default string slots (wine_binary,
 *                       graphics_backend, dll_overrides, runtime_lane).
 *   MtspRules           Top-level container with two HashTables:
 *                       defaults keyed by pipeline id, overrides
 *                       keyed by appid (decimal string).
 *   config_parse_rules  Parse a TOML rules file. Returns a heap-
 *                       allocated MtspRules on success, NULL on
 *                       failure; *error receives a heap-allocated
 *                       diagnostic string the caller must free.
 *   config_free_rules   Release every string, hash table, and rule
 *                       owned by the rules container.
 *   config_get_rule     Resolve the effective rule for an appid.
 *                       Looks up the override map first, then falls
 *                       back to the m12 default rule (the default
 *                       lane for the catalog). Returns NULL when no
 *                       rule resolves.
 *
 * SCHEMA
 *   PipelineRule owns its scalar strings (allocated via strdup) and
 *   the two HashTables. deps and diags are HashTables whose keys are
 *   component ids / DLL filenames and whose values are heap-allocated
 *   string duplicates; config_free_rules releases every owned string
 *   and the tables themselves. MtspRules owns its two maps and the
 *   PipelineRule values they reference; config_free_rules releases
 *   the rules before destroying the tables. config_get_rule returns
 *   a pointer into the MtspRules container; it does not allocate a
 *   new rule, so the result becomes invalid when the container is
 *   freed. Unknown keys encountered while parsing (for example
 *   exe_names or offline_capable) are parsed and discarded so the
 *   caller never has to clean up partially-parsed values.
 */
#ifndef METALSHARP_CONFIG_PARSER_H
#define METALSHARP_CONFIG_PARSER_H

#include "hash_table.h"
#include "server.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * Single pipeline rule. All char * fields are heap-allocated
 * NUL-terminated copies owned by the rule (or NULL when unset);
 * deps and diags are always non-NULL HashTables that may be
 * empty. The free path releases every owned string and table.
 */
typedef struct {
    char* pipeline;         /* pipeline identifier, e.g. "m12" */
    char* name;             /* display name, e.g. "Terraria" */
    HashTable* deps;        /* component-id set (dependencies.components) */
    HashTable* diags;       /* DLL filename set (diagnostics.check_dlls) */
    char* wine_binary;      /* path to wine binary */
    char* graphics_backend; /* renderer name, e.g. "dxmt" */
    char* dll_overrides;    /* WINEDLLOVERRIDES string */
    char* runtime_lane;     /* lane identifier, e.g. "dxmt_m12" */
} PipelineRule;

/*
 * Top-level container holding the two maps produced by a parse.
 * defaults maps pipeline id ("m12") to the PipelineRule that
 * applies when that pipeline is selected; overrides maps a
 * decimal appid string ("105600") to the per-title rule.
 */
typedef struct {
    HashTable* defaults;  /* keyed by pipeline id, e.g. "m12" */
    HashTable* overrides; /* keyed by appid as decimal string */
} MtspRules;

/*
 * Parse the MTSP rules file at `path` into a fresh MtspRules.
 * Returns NULL on any failure (file missing, allocation failure,
 * malformed section header, malformed value); when `error` is
 * non-NULL the failure path writes a heap-allocated diagnostic
 * string through it that the caller releases with free(). The
 * returned container must eventually be released with
 * config_free_rules.
 */
MtspRules* config_parse_rules(const char* path, char** error);

/*
 * Release every rule, every owned string, and both top-level
 * maps, then free the container itself. Safe to call with NULL.
 * After this call every pointer obtained from the container is
 * dangling and must not be dereferenced.
 */
void config_free_rules(MtspRules* rules);

/*
 * Resolve the effective rule for `appid`. Looks up the override
 * map first; if that misses, falls back to the m12 default rule
 * (the catalog's primary lane). Returns NULL when no override
 * exists for the appid and no m12 default is configured. The
 * returned pointer is owned by the MtspRules container.
 */
PipelineRule* config_get_rule(MtspRules* rules, unsigned int appid);

#endif /* METALSHARP_CONFIG_PARSER_H */
